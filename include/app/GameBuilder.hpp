#ifndef GAME_BUILDER_HPP
#define GAME_BUILDER_HPP

#include <string>
#include <vector>
#include <functional>

class Scene;

// GameBuilder packages the currently-loaded scene + scripts together with the
// embedded game runtime template into a redistributable artifact:
//   - Native: writes a self-contained folder to a user-chosen directory
//   - Web:    builds an in-memory zip and triggers a browser download
//
// The "embedded template" is a frozen copy of the game executable + bundled
// engine assets produced at editor build time (see EmbeddedTemplate.hpp).
class GameBuilder
{
public:
    enum class Target
    {
        Linux,
        Windows,
        Web,
    };

    struct Result
    {
        bool ok = false;
        std::string message;   // human-readable message (path on success, error on failure)
    };

    // True if the host platform can produce builds for this target without
    // additional toolchains. (e.g. on Linux: Linux + Web available; Windows
    // disabled.)  Web editor: only Web is producible.
    static bool isTargetAvailable(Target t);

    // Display label for a target ("Linux Game...", "Web Game (.zip)...", etc.)
    static const char *targetLabel(Target t);

    // Top-level entry: dispatch to native folder-picker or web zip-download
    // path based on target. Logger is invoked for progress lines.
    static Result build(Target t, Scene *scene,
                        const std::function<void(const std::string &)> &log);

    // ---------------------------------------------------------------- publish
    //
    // Uploads the same scene/scripts/models payload the exporter stages, as a
    // JSON envelope, to a CowEngine control plane. The game then appears in the
    // server browser and can be joined by other players.
    //
    // Publishing is asynchronous: on the web it is a `fetch`, and this build has
    // no Asyncify, so the call cannot block. Callers start it and then poll
    // publishPoll() once a frame — the same shape as the CowNet transport shim.

    enum class PublishState
    {
        Idle,
        Pending,
        Success,
        Failed,
    };

    struct PublishStatus
    {
        PublishState state = PublishState::Idle;
        std::string message;  // error text, or a human-readable success summary
        std::string gameId;
        std::string editKey;  // only set the first time a game is published
        int version = 0;
    };

    // Base URL of the control plane, e.g. "https://cowengine.com". Web: baked
    // into the editor shell as __COWENGINE_API__ (or ?api= for local testing);
    // native: the COWENGINE_API environment variable. Empty = publishing off.
    static std::string apiBase();
    static bool publishAvailable();

    // Package the live scene plus the on-disk scripts/ and models/ trees into
    // the upload envelope. Exposed separately so it can be inspected and tested
    // without performing a network call.
    static std::string buildBundleJson(Scene *scene, const std::string &title,
                                       const std::string &description);

    // Begin a publish. If this project was published before and its edit key is
    // known, this updates that game (a new version) instead of creating one.
    // Returns false if a publish is already in flight or none is possible.
    //
    // `publishKey` is the server's invite key (COW_PUBLISH_TOKENS), sent as
    // X-Cow-Publish-Key. Servers with no allow-list ignore it. It is remembered
    // between publishes, so the UI only has to ask once — see savedPublishKey().
    static bool publishStart(Scene *scene, const std::string &title,
                             const std::string &description,
                             const std::string &publishKey,
                             const std::function<void(const std::string &)> &log);

    // The invite key this editor last published with, for prefilling the field.
    static std::string publishKey();

    // Poll an in-flight publish. Once it returns Success or Failed the result is
    // latched until the next publishStart().
    static PublishStatus publishPoll();

    // The game id this editor last published, or empty. Drives "update" vs
    // "publish new" in the UI.
    static std::string lastPublishedId();
};

#endif // GAME_BUILDER_HPP
