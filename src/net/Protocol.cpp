#include "net/Protocol.hpp"
#include "net/ByteIO.hpp"

namespace net
{
    MsgType typeOf(const Message &m)
    {
        struct V
        {
            MsgType operator()(const ClientHello &) const { return MsgType::ClientHello; }
            MsgType operator()(const ServerWelcome &) const { return MsgType::ServerWelcome; }
            MsgType operator()(const InputCommand &) const { return MsgType::InputCommand; }
            MsgType operator()(const Snapshot &) const { return MsgType::Snapshot; }
            MsgType operator()(const PlayerJoin &) const { return MsgType::PlayerJoin; }
            MsgType operator()(const PlayerLeave &) const { return MsgType::PlayerLeave; }
            MsgType operator()(const SpawnEntity &) const { return MsgType::SpawnEntity; }
            MsgType operator()(const DespawnEntity &) const { return MsgType::DespawnEntity; }
            MsgType operator()(const Explosion &) const { return MsgType::Explosion; }
        };
        return std::visit(V{}, m);
    }

    Channel channelFor(MsgType t)
    {
        switch (t)
        {
        case MsgType::InputCommand:
        case MsgType::Snapshot:
            return Channel::Unreliable;
        default:
            return Channel::Reliable;
        }
    }

    namespace
    {
        void writeBody(ByteWriter &w, const ClientHello &m)
        {
            w.u16(m.protocolVersion);
            w.str(m.name);
        }
        void writeBody(ByteWriter &w, const ServerWelcome &m)
        {
            w.u32(m.playerNetId);
            w.u32(m.sceneId);
            w.u16(m.tickRate);
        }
        void writeBody(ByteWriter &w, const InputCommand &m)
        {
            w.u32(m.sequence);
            w.u64(m.keys);
            w.f32(m.lookYaw);
            w.f32(m.lookPitch);
            w.f32(m.dt);
        }
        void writeBody(ByteWriter &w, const Snapshot &m)
        {
            w.u32(m.serverTick);
            w.u32(m.ackSeq);
            w.u16(static_cast<uint16_t>(m.entities.size()));
            for (const auto &e : m.entities)
            {
                w.u32(e.netId);
                w.vec3(e.pos);
                w.quat(e.rot);
                w.vec3(e.vel);
            }
        }
        void writeBody(ByteWriter &w, const PlayerJoin &m)
        {
            w.u32(m.netId);
            w.str(m.name);
        }
        void writeBody(ByteWriter &w, const PlayerLeave &m) { w.u32(m.netId); }
        void writeBody(ByteWriter &w, const SpawnEntity &m)
        {
            w.u32(m.netId);
            w.u8(static_cast<uint8_t>(m.kind));
            w.vec3(m.scale);
            w.f32(m.color.r);
            w.f32(m.color.g);
            w.f32(m.color.b);
            w.f32(m.color.a);
        }
        void writeBody(ByteWriter &w, const DespawnEntity &m) { w.u32(m.netId); }
        void writeBody(ByteWriter &w, const Explosion &m)
        {
            w.vec3(m.pos);
            w.f32(m.radius);
            w.f32(m.speed);
            w.f32(m.upBias);
            w.f32(m.spin);
        }
    }

    std::vector<uint8_t> encode(const Message &m)
    {
        ByteWriter w;
        w.u8(static_cast<uint8_t>(typeOf(m)));
        std::visit([&](const auto &msg) { writeBody(w, msg); }, m);
        return std::move(w.buf);
    }

    std::optional<Message> decode(const uint8_t *data, size_t len)
    {
        if (data == nullptr || len == 0)
            return std::nullopt;
        ByteReader r(data, len);
        auto type = static_cast<MsgType>(r.u8());
        Message out;
        switch (type)
        {
        case MsgType::ClientHello:
        {
            ClientHello m;
            m.protocolVersion = r.u16();
            m.name = r.str();
            out = m;
            break;
        }
        case MsgType::ServerWelcome:
        {
            ServerWelcome m;
            m.playerNetId = r.u32();
            m.sceneId = r.u32();
            m.tickRate = r.u16();
            out = m;
            break;
        }
        case MsgType::InputCommand:
        {
            InputCommand m;
            m.sequence = r.u32();
            m.keys = r.u64();
            m.lookYaw = r.f32();
            m.lookPitch = r.f32();
            m.dt = r.f32();
            out = m;
            break;
        }
        case MsgType::Snapshot:
        {
            Snapshot m;
            m.serverTick = r.u32();
            m.ackSeq = r.u32();
            uint16_t count = r.u16();
            // Guard against a corrupt count claiming more entities than the
            // buffer could possibly hold (32 bytes each) before reserving.
            if (!r.ok)
                return std::nullopt;
            m.entities.reserve(count);
            for (uint16_t i = 0; i < count; ++i)
            {
                EntityState e;
                e.netId = r.u32();
                e.pos = r.vec3();
                e.rot = r.quat();
                e.vel = r.vec3();
                m.entities.push_back(e);
            }
            out = m;
            break;
        }
        case MsgType::PlayerJoin:
        {
            PlayerJoin m;
            m.netId = r.u32();
            m.name = r.str();
            out = m;
            break;
        }
        case MsgType::PlayerLeave:
        {
            PlayerLeave m;
            m.netId = r.u32();
            out = m;
            break;
        }
        case MsgType::SpawnEntity:
        {
            SpawnEntity m;
            m.netId = r.u32();
            m.kind = static_cast<SpawnKind>(r.u8());
            m.scale = r.vec3();
            m.color.r = r.f32();
            m.color.g = r.f32();
            m.color.b = r.f32();
            m.color.a = r.f32();
            out = m;
            break;
        }
        case MsgType::DespawnEntity:
        {
            DespawnEntity m;
            m.netId = r.u32();
            out = m;
            break;
        }
        case MsgType::Explosion:
        {
            Explosion m;
            m.pos = r.vec3();
            m.radius = r.f32();
            m.speed = r.f32();
            m.upBias = r.f32();
            m.spin = r.f32();
            out = m;
            break;
        }
        default:
            return std::nullopt; // unknown message type
        }
        if (!r.ok)
            return std::nullopt; // truncated
        return out;
    }
}
