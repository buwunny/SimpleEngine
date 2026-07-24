#ifndef NET_PROTOCOL_HPP
#define NET_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Wire protocol shared verbatim by the client (native + WASM) and the headless
// server. Every message is a 1-byte MsgType followed by its body. The transport
// delivers one message per datagram (unreliable) or per length-prefixed stream
// frame (reliable); framing is the transport's job, not this file's.
//
// Compatibility rule: only ever APPEND fields/messages and bump kProtocolVersion
// — never reorder or repurpose, since old clients and servers must interoperate.
namespace net
{
    // Bumped to 2 for the shared-physics/despawn generation, 4 for player names,
    // 5 for replicated explosions (an old client would decode MsgType 9 as
    // garbage and drop the connection's framing).
    // The server refuses a ClientHello whose version doesn't match, so a stale
    // binary (an old server left holding the port, or a browser-cached old
    // client) is rejected loudly instead of silently interoperating and
    // producing duplicate/ghost objects.
    inline constexpr uint16_t kProtocolVersion = 5;

    // Longest player name the server will keep; anything longer is truncated.
    // Nametags are drawn in the world above a player's head, so this is a
    // legibility limit as much as a wire-size one.
    inline constexpr size_t kMaxPlayerNameLen = 16;

    // netId ranges partition the replicated-entity id space so a client can tell,
    // from the id alone, what kind of thing a snapshot entry is:
    //   [1, kPlayerNetIdBase)          scene dynamic objects — id == Identity.id,
    //                                  which both ends assign deterministically at
    //                                  scene load, so the client already has them.
    //   [kPlayerNetIdBase, kSpawnNetIdBase)  players (own = predicted, others = avatars)
    //   [kSpawnNetIdBase, 2^32)        runtime-spawned objects (announced via SpawnEntity)
    inline constexpr uint32_t kPlayerNetIdBase = 0x40000000u;
    inline constexpr uint32_t kSpawnNetIdBase = 0x60000000u;

    // A Snapshot is sent unreliable (UDP datagram / QUIC datagram), so it must fit
    // in the smallest datagram along the path. The tightest is the WebTransport
    // QUIC datagram (~1200 B). Each EntityState is 44 B plus an 11 B header, so we
    // cap a single Snapshot message at this many entities (~891 B) and split a
    // larger world across several datagrams. The client folds each entity into its
    // own sample buffer, so partial snapshots interpolate fine and a dropped chunk
    // only stalls its own entities for one tick instead of freezing the world.
    inline constexpr size_t kMaxSnapshotEntities = 20;

    // What visual to build for a spawned object the client never created.
    enum class SpawnKind : uint8_t
    {
        Cube = 0,
        Cow = 1,
        Plane = 2,
    };

    enum class MsgType : uint8_t
    {
        ClientHello = 1,
        ServerWelcome = 2,
        InputCommand = 3,
        Snapshot = 4,
        PlayerJoin = 5,
        PlayerLeave = 6,
        SpawnEntity = 7,
        DespawnEntity = 8,
        Explosion = 9,
    };

    // Reliability channel a message should travel on.
    enum class Channel : uint8_t
    {
        Unreliable = 0, // datagrams: inputs, snapshots (latest-wins, loss-tolerant)
        Reliable = 1,   // streams: handshake + lifecycle events (must arrive, in order)
    };

    // --- Messages -----------------------------------------------------------

    // Client -> server, first thing after the transport connects.
    struct ClientHello
    {
        uint16_t protocolVersion = kProtocolVersion;
        // Display name the player typed in the join gate. Untrusted: the server
        // sanitises it (see GameServer) before it reaches any other client, and
        // an empty name means "assign me one".
        std::string name;
    };

    // Server -> client, accepting the connection.
    struct ServerWelcome
    {
        uint32_t playerNetId = 0; // netId of the entity this client controls
        uint32_t sceneId = 0;     // room/scene identifier
        uint16_t tickRate = 60;   // server simulation Hz
    };

    // Client -> server, one per fixed sim tick. Mirrors ecs::PlayerInput.
    struct InputCommand
    {
        uint32_t sequence = 0;
        uint64_t keys = 0; // bitmask over ecs::kInputKeyNames
        float lookYaw = 0.0f;
        float lookPitch = 0.0f;
        float dt = 0.0f; // fixed dt this input covers (server bookkeeping)
    };

    // One replicated entity within a Snapshot.
    struct EntityState
    {
        uint32_t netId = 0;
        glm::vec3 pos{0.0f};
        glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w,x,y,z)
        glm::vec3 vel{0.0f};
    };

    // Server -> client, the authoritative world state for one server tick.
    struct Snapshot
    {
        uint32_t serverTick = 0;
        uint32_t ackSeq = 0; // last InputCommand.sequence the server has applied
        std::vector<EntityState> entities;
    };

    // Server -> client lifecycle events (reliable).
    //
    // PlayerJoin is both the "someone arrived" event and how a client learns a
    // player's name, so the server also replays one per player already in the
    // room to each newcomer. Otherwise a late joiner would only ever meet the
    // others through snapshots, which carry no name.
    struct PlayerJoin
    {
        uint32_t netId = 0;
        std::string name; // already sanitised by the server
    };
    struct PlayerLeave
    {
        uint32_t netId = 0;
    };

    // Server -> client: a runtime-spawned object the client must create a visual
    // for (scene objects are derived locally and need no SpawnEntity). Sent
    // reliably on spawn and re-sent to late-joining clients.
    struct SpawnEntity
    {
        uint32_t netId = 0;
        SpawnKind kind = SpawnKind::Cube;
        glm::vec3 scale{1.0f};
        glm::vec4 color{1.0f};
    };
    struct DespawnEntity
    {
        uint32_t netId = 0;
    };

    // Server -> client: a blast went off. Sent because knockback is the one
    // server-side effect a client cannot infer from snapshots in time — a
    // rocket jump is over in a few hundred milliseconds, and reaching the
    // player through position corrections alone at snapshotHz turns an instant
    // launch into a rubber-band. On receipt the client replays the same blast
    // against its predicted player, so the launch begins on the frame it
    // arrives and the snapshots that follow only trim the error.
    //
    // Mirrors PhysicsWorld::BlastParams; kept as loose fields rather than
    // including the physics header, since the protocol is shared with the web
    // client and must stay free of Bullet.
    struct Explosion
    {
        glm::vec3 pos{0.0f};
        float radius = 6.0f;
        float speed = 24.0f;
        float upBias = 0.35f;
        float spin = 8.0f;
    };

    using Message = std::variant<ClientHello, ServerWelcome, InputCommand,
                                 Snapshot, PlayerJoin, PlayerLeave,
                                 SpawnEntity, DespawnEntity, Explosion>;

    // The MsgType tag for a given message alternative.
    MsgType typeOf(const Message &m);
    // The channel a message type should be sent on.
    Channel channelFor(MsgType t);

    // Serialise a message (type byte + body) to a fresh buffer.
    std::vector<uint8_t> encode(const Message &m);
    // Parse one message. Returns nullopt on a truncated/unknown buffer.
    std::optional<Message> decode(const uint8_t *data, size_t len);
    inline std::optional<Message> decode(const std::vector<uint8_t> &b)
    {
        return decode(b.data(), b.size());
    }
}

#endif // NET_PROTOCOL_HPP
