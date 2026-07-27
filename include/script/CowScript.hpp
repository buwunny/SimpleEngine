#ifndef COW_SCRIPT_HPP
#define COW_SCRIPT_HPP

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace cowscript
{

    struct Value
    {
        enum Type
        {
            Null,
            Number,
            Bool,
            Str,
            Handle
        } type = Null;
        double num = 0.0;
        bool boolean = false;
        std::string str;
        // For Handle values: `str` carries the kind (e.g. "transform"),
        // `handle` carries an opaque pointer into engine state.
        void *handle = nullptr;

        static Value makeNumber(double n)
        {
            Value v;
            v.type = Number;
            v.num = n;
            return v;
        }
        static Value makeBool(bool b)
        {
            Value v;
            v.type = Bool;
            v.boolean = b;
            return v;
        }
        static Value makeString(std::string s)
        {
            Value v;
            v.type = Str;
            v.str = std::move(s);
            return v;
        }
        static Value makeHandle(std::string kind, void *ptr)
        {
            Value v;
            v.type = Handle;
            v.str = std::move(kind);
            v.handle = ptr;
            return v;
        }
        static Value makeNull() { return Value{}; }

        bool truthy() const;
        std::string toString() const;
        double toNumber() const;
    };

    using BuiltinFn = std::function<Value(const std::vector<Value> &)>;
    using PropertyGetFn = std::function<Value(const Value &target, const std::string &prop)>;
    using PropertySetFn = std::function<void(const Value &target, const std::string &prop, const Value &value)>;

    // Execution limits, enforced per event call.
    //
    // A published .cow runs on the shared room server, so it is untrusted code:
    // an author who writes `while true {}` must not be able to pin a core and
    // take the world down for everyone else in it. Three separate bounds,
    // because each catches something the others cannot:
    //
    //   maxSteps     total interpreter steps. The per-loop iteration cap this
    //                replaces could not see nested loops -- two nested `while`s
    //                each under their own cap still multiply out to 10^12 steps.
    //   maxCallDepth nested user function calls. Unbounded recursion overflows
    //                the *native* stack, which is a SIGSEGV that kills the whole
    //                room process (every player in it), not just the script.
    //   maxMillis    wall-clock backstop for work the step counter under-counts,
    //                e.g. a builtin that is itself slow. 0 disables it.
    //
    // Defaults are the permissive editor/singleplayer values: generous enough
    // that no plausible legitimate script trips them. The server tightens them
    // (see ScriptHost::serverLimits) because there the cost is borne by other
    // players rather than by the author alone.
    struct Limits
    {
        unsigned long long maxSteps = 5000000ull;
        int maxCallDepth = 128;
        double maxMillis = 0.0;
    };

    class Script
    {
    public:
        Script();
        ~Script();
        Script(const Script &) = delete;
        Script &operator=(const Script &) = delete;

        // Parse the source code. Returns an empty string on success or a human-readable
        // error message describing the parse problem.
        std::string compile(const std::string &source);

        // Register a built-in function callable from the script under `name`.
        void setBuiltin(const std::string &name, BuiltinFn fn);

        // Register property accessors used when the script does `handle.prop` or
        // `handle.prop = value`. Both must be set for full read/write support; a
        // missing handler causes a runtime error on use.
        void setPropertyGetter(PropertyGetFn fn);
        void setPropertySetter(PropertySetFn fn);

        // True if the script defines `on <name>(...)`.
        bool hasEvent(const std::string &name) const;

        // Invoke the `on <name>(args)` event handler if it exists. Returns an empty string
        // on success, or a runtime-error message describing the failure.
        std::string callEvent(const std::string &name, const std::vector<Value> &args);

        // Execution limits for this script. Must be set before compile() to also
        // bound the top-level statements, which run at compile time.
        void setLimits(const Limits &limits);
        const Limits &limits() const;

        // True if the last callEvent()/compile() failure was a limit breach
        // rather than an ordinary runtime error. Callers use this to disable a
        // script permanently instead of re-running it every tick.
        bool lastErrorWasLimit() const;

        const std::string &source() const { return src; }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        std::string src;
        bool lastLimit = false;
    };

    // Read a script file. Searches common candidate paths (cwd, ASSET_ROOT, parents).
    // Returns the source. If outFoundPath is not null, writes the resolved path to it.
    std::string readScriptFile(const std::string &path, std::string *outFoundPath = nullptr);

    // Token used by the editor for syntax highlighting.
    enum class TokenKind
    {
        Text,
        Comment,
        Keyword,
        Number,
        String,
        Identifier,
        Builtin,
        Operator,
        Punctuation,
        Whitespace
    };

    struct Token
    {
        TokenKind kind = TokenKind::Text;
        int start = 0;
        int length = 0;
    };

    // Tokenize source code into syntax-highlightable spans. Always produces a contiguous,
    // covering set of tokens (so the editor can render them sequentially).
    std::vector<Token> highlight(const std::string &source);

} // namespace cowscript

#endif // COW_SCRIPT_HPP
