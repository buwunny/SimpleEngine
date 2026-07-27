// Headless test for CowScript execution limits (M2 hardening).
//
// A published .cow is untrusted code that runs on the shared room server, so
// the interpreter has to survive a hostile author, not just a careless one.
// This covers the three bounds and the properties that make them useful:
//   * a flat `while true` is stopped
//   * NESTED loops are stopped -- the old per-loop iteration cap restarted on
//     every loop entry, so two nested loops each stayed "under" it while
//     multiplying out to ~10^12 steps
//   * unbounded recursion is an error, NOT a native stack overflow (a SIGSEGV
//     there would kill the room process and every player in it)
//   * the wall-clock backstop catches what the step counter under-counts
//   * budgets are per call, so a long-lived script doesn't accumulate its way
//     into a false positive
//   * limit breaches are distinguishable from ordinary runtime errors, which
//     is what lets the caller disable a runaway instead of re-running it 60
//     times a second forever
//   * the real shipped scripts still parse and run inside the server budget

#include "script/CowScript.hpp"
#include "script/ScriptHost.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while(0)

namespace
{
    // Compile + run one `update` call, returning the error string ("" on success).
    std::string runUpdate(const std::string &source, const cowscript::Limits &limits,
                          bool *wasLimit = nullptr)
    {
        cowscript::Script s;
        s.setLimits(limits);
        std::string err = s.compile(source);
        if (!err.empty())
        {
            if (wasLimit)
                *wasLimit = s.lastErrorWasLimit();
            return err;
        }
        std::vector<cowscript::Value> args{cowscript::Value::makeNumber(1.0 / 60.0)};
        err = s.callEvent("update", args);
        if (wasLimit)
            *wasLimit = s.lastErrorWasLimit();
        return err;
    }
}

int main()
{
    const cowscript::Limits server = ScriptHost::serverLimits();

    // ---- 1. flat infinite loop -------------------------------------------
    {
        bool wasLimit = false;
        std::string err = runUpdate("on update(dt) { while (true) { } }", server, &wasLimit);
        CHECK(!err.empty());
        CHECK(wasLimit);
        printf("flat infinite loop: %s\n", err.c_str());
    }

    // ---- 2. nested loops: what the per-loop cap could not see -------------
    // Each loop alone stays under the 1e6 per-loop cap; together they are ~1e12
    // steps. The clock is switched off so this pins the STEP budget
    // specifically -- otherwise the wall-clock backstop fires first and the
    // test would pass without the step counter ever being the thing that
    // worked.
    {
        cowscript::Limits stepsOnly = server;
        stepsOnly.maxMillis = 0.0;
        bool wasLimit = false;
        std::string err = runUpdate(
            "on update(dt) {"
            "  let i = 0"
            "  while (i < 999999) {"
            "    let j = 0"
            "    while (j < 999999) { j = j + 1 }"
            "    i = i + 1"
            "  }"
            "}",
            stepsOnly, &wasLimit);
        CHECK(!err.empty());
        CHECK(wasLimit);
        CHECK(err.find("step budget") != std::string::npos);
        printf("nested loops: %s\n", err.c_str());
    }

    // ---- 3. unbounded recursion must not smash the native stack -----------
    // Reaching this line at all is most of the test: an unguarded interpreter
    // segfaults here and takes the whole process with it.
    {
        bool wasLimit = false;
        std::string err = runUpdate(
            "fn boom(n) { return boom(n + 1) }"
            "on update(dt) { boom(0) }",
            server, &wasLimit);
        CHECK(!err.empty());
        CHECK(wasLimit);
        printf("runaway recursion: %s\n", err.c_str());
    }

    // ---- 4. wall-clock backstop -------------------------------------------
    // Isolate the clock: steps are effectively unlimited, and the loops are
    // NESTED so the per-loop iteration cap (which would otherwise fire first
    // and mask this) never trips either. Only the deadline can end this.
    {
        cowscript::Limits timeOnly;
        timeOnly.maxSteps = 100000000000ull;
        timeOnly.maxCallDepth = 64;
        timeOnly.maxMillis = 20.0;

        auto t0 = std::chrono::steady_clock::now();
        bool wasLimit = false;
        std::string err = runUpdate(
            "on update(dt) {"
            "  let i = 0"
            "  while (i < 999999) {"
            "    let j = 0"
            "    while (j < 999999) { j = j + 1 }"
            "    i = i + 1"
            "  }"
            "}",
            timeOnly, &wasLimit);
        double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
        CHECK(!err.empty());
        CHECK(wasLimit);
        CHECK(err.find("time budget") != std::string::npos);
        // Generous upper bound: this asserts the deadline actually fires, not
        // that it is precise (it is sampled every 1024 steps).
        CHECK(ms < 2000.0);
        printf("time budget: %s (after %.1f ms)\n", err.c_str(), ms);
    }

    // ---- 5. budget is per call, not cumulative ----------------------------
    // A script doing modest work every tick must still be running after a long
    // session, so the counter has to reset on each entry.
    {
        cowscript::Script s;
        s.setLimits(server);
        CHECK(s.compile("let total = 0\n"
                        "on update(dt) {"
                        "  let i = 0"
                        "  while (i < 200) { total = total + 1  i = i + 1 }"
                        "}") == "");
        std::vector<cowscript::Value> args{cowscript::Value::makeNumber(1.0 / 60.0)};
        bool allOk = true;
        for (int tick = 0; tick < 2000; ++tick) // ~33 s of wall-clock at 60 Hz
        {
            if (!s.callEvent("update", args).empty())
            {
                allOk = false;
                break;
            }
        }
        CHECK(allOk);
        printf("per-call reset: 2000 ticks x 200 iterations %s\n", allOk ? "ok" : "FAILED");
    }

    // ---- 6. a limit breach is distinguishable from a normal runtime error --
    // Both are errors, but only one means "never run this again".
    {
        bool wasLimit = true;
        std::string err = runUpdate("on update(dt) { nonexistent_function() }", server, &wasLimit);
        CHECK(!err.empty());
        CHECK(!wasLimit); // ordinary runtime error, script stays enabled
        printf("ordinary runtime error: %s (limit=%d)\n", err.c_str(), (int)wasLimit);
    }

    // ---- 7. top-level statements are bounded too ---------------------------
    // They run at compile time, outside any event, and were previously
    // unbounded.
    {
        cowscript::Script s;
        s.setLimits(server);
        std::string err = s.compile("let x = 0\n while (true) { x = x + 1 }\n on update(dt) { }");
        CHECK(!err.empty());
        CHECK(s.lastErrorWasLimit());
        printf("top-level loop: %s\n", err.c_str());
    }

    // ---- 8. legitimate shipped scripts still run under the server budget ---
    // The bound is worthless if it also stops real games.
    {
        const char *paths[] = {"scripts/spin.cow", "scripts/despawn_after.cow",
                               "scripts/jump_on_space.cow", "scripts/player_movement.cow"};
        for (const char *p : paths)
        {
            std::string found;
            std::string src = cowscript::readScriptFile(p, &found);
            if (src.empty())
            {
                printf("SKIP %s (not found from cwd)\n", p);
                continue;
            }
            cowscript::Script s;
            s.setLimits(server);
            // No builtins bound, so calls into engine builtins report "Unknown
            // function" — an ordinary runtime error. What matters here is that
            // nothing trips a *limit*.
            std::string err = s.compile(src);
            CHECK(!s.lastErrorWasLimit());
            printf("%s: compile %s (limit=%d)\n", p,
                   err.empty() ? "ok" : err.c_str(), (int)s.lastErrorWasLimit());
        }
    }

    printf(failures ? "\nFAILED (%d)\n" : "\nAll script limit tests passed\n", failures);
    return failures ? 1 : 0;
}
