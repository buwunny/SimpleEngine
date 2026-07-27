// Headless authoritative server entry point.
//
// Wire framing on the server<->sidecar UDP hop (the sidecar owns the browser's
// WebTransport/WebSocket connection and multiplexes them onto this one socket):
//
//   [u32 session][u8 kind][payload...]
//     kind 0 = connect      (no payload)
//     kind 1 = disconnect   (no payload)
//     kind 2 = message on the unreliable channel (payload = encoded net::Message)
//     kind 3 = message on the reliable channel   (payload = encoded net::Message)
//
// The server replies with the same framing; the sidecar routes each reply back
// to the browser identified by `session`.
//
// Two further kinds serve the control plane rather than a player, so they are
// deliberately *not* recorded in the session table:
//
//     kind 4 = status request (no payload)
//     kind 5 = status reply   [u32 sessions][u32 players][u32 uptimeSec]
//
// One request/reply pair answers both questions the orchestrator has: "is this
// room up yet?" (a reply at all) and "is anyone still in it?" (players). Doing
// it on the existing socket beats scraping stdout, which is block-buffered under
// Docker and would need the log format to stay stable forever.

#include "server/GameServer.hpp"
#include "net/UdpLink.hpp"
#include "net/Protocol.hpp"
#include "net/ByteIO.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
    enum FrameKind : uint8_t
    {
        FrameConnect = 0,
        FrameDisconnect = 1,
        FrameUnreliable = 2,
        FrameReliable = 3,
        FrameStatus = 4,
        FrameStatusReply = 5,
    };

    void usage(const char *argv0)
    {
        std::cerr << "usage: " << argv0 << " [port] [scenePath] [--idle-exit <secs>]\n"
                  << "  port       udp port to bind (default 4433)\n"
                  << "  scenePath  scene to load    (default scenes/scene.json)\n"
                  << "  --idle-exit  exit once no session has been connected for\n"
                  << "               <secs> seconds; 0 (default) means never exit\n";
    }
}

int main(int argc, char **argv)
{
    // Under Docker/systemd stdout is a pipe, so it is block-buffered and log
    // lines sit in the buffer indefinitely on a long-running process — `docker
    // logs` would show nothing. Flush every insertion; this server logs rarely.
    std::cout << std::unitbuf;

    uint16_t port = 4433;
    std::string scenePath = "scenes/scene.json";
    double idleExitSecs = 0.0;

    // Positional args stay exactly as they were (deploy/Dockerfile.server's CMD
    // passes "4433 scenes/scene.json"); --idle-exit is accepted anywhere.
    {
        std::vector<std::string> positional;
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "--idle-exit")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "--idle-exit needs a value\n";
                    usage(argv[0]);
                    return 2;
                }
                idleExitSecs = std::atof(argv[++i]);
            }
            else if (a == "-h" || a == "--help")
            {
                usage(argv[0]);
                return 0;
            }
            else
            {
                positional.push_back(a);
            }
        }
        if (positional.size() > 0)
            port = static_cast<uint16_t>(std::atoi(positional[0].c_str()));
        if (positional.size() > 1)
            scenePath = positional[1];
        if (positional.size() > 2)
        {
            std::cerr << "unexpected argument: " << positional[2] << "\n";
            usage(argv[0]);
            return 2;
        }
    }

    net::UdpLink udp;
    if (!udp.bind(port))
        return 1;

    // sockaddr for each live session, refreshed on every inbound datagram so
    // replies always go to the sidecar's current source address.
    std::unordered_map<uint32_t, sockaddr_in> sessionAddr;

    GameServer server;
    if (!server.init(scenePath))
        return 1;

    server.setSend([&](uint32_t session, const net::Message &m) {
        auto it = sessionAddr.find(session);
        if (it == sessionAddr.end())
            return;
        std::vector<uint8_t> payload = net::encode(m);
        net::ByteWriter w;
        w.u32(session);
        w.u8(net::channelFor(net::typeOf(m)) == net::Channel::Unreliable
                 ? FrameUnreliable
                 : FrameReliable);
        w.buf.insert(w.buf.end(), payload.begin(), payload.end());
        udp.sendTo(w.buf.data(), w.buf.size(), it->second);
    });

    std::cout << "CowEngine server listening on udp/" << port
              << " (scene: " << scenePath << ")";
    if (idleExitSecs > 0.0)
        std::cout << " (idle-exit: " << idleExitSecs << "s)";
    std::cout << "\n";

    using clock = std::chrono::steady_clock;
    const double kFixedDt = 1.0 / 60.0;
    const auto startedAt = clock::now();
    auto prev = clock::now();
    double accumulator = 0.0;
    // When the room last had anyone in it. Starts at boot, so a room nobody ever
    // joins exits on the same timer as one everybody left.
    auto lastOccupied = clock::now();

    for (;;)
    {
        // 1) Drain all pending datagrams.
        std::vector<uint8_t> data;
        sockaddr_in from{};
        while (udp.recv(data, from))
        {
            net::ByteReader r(data.data(), data.size());
            uint32_t session = r.u32();
            uint8_t kind = r.u8();
            if (!r.ok)
                continue;

            // The control plane probes over the same socket but is not a player.
            // Answer straight to the sender and never touch sessionAddr — an
            // entry here would make the next snapshot fan out to the orchestrator.
            if (kind == FrameStatus)
            {
                const double up = std::chrono::duration<double>(clock::now() - startedAt).count();
                net::ByteWriter w;
                w.u32(session);
                w.u8(FrameStatusReply);
                w.u32(static_cast<uint32_t>(server.playerCount()));
                w.u32(static_cast<uint32_t>(server.spawnedCount()));
                w.u32(static_cast<uint32_t>(up));
                udp.sendTo(w.buf.data(), w.buf.size(), from);
                continue;
            }

            sessionAddr[session] = from;

            switch (kind)
            {
            case FrameConnect:
                server.onConnect(session);
                break;
            case FrameDisconnect:
                server.onDisconnect(session);
                sessionAddr.erase(session);
                break;
            case FrameUnreliable:
            case FrameReliable:
            {
                // Payload begins after the 5-byte header.
                if (data.size() > 5)
                {
                    if (auto msg = net::decode(data.data() + 5, data.size() - 5))
                        server.onMessage(session, *msg);
                }
                break;
            }
            default:
                break;
            }
        }

        // 2) Advance the simulation in fixed steps.
        auto now = clock::now();
        accumulator += std::chrono::duration<double>(now - prev).count();
        prev = now;
        if (accumulator > 0.25)
            accumulator = 0.25;
        while (accumulator >= kFixedDt)
        {
            server.tick(static_cast<float>(kFixedDt));
            accumulator -= kFixedDt;
        }

        // 3) Exit once the room has stood empty long enough. The control plane
        //    also reaps rooms, but it can restart or lose track of a child; this
        //    guarantees an orphan cleans itself up and frees its port.
        if (server.playerCount() > 0)
            lastOccupied = now;
        else if (idleExitSecs > 0.0 &&
                 std::chrono::duration<double>(now - lastOccupied).count() >= idleExitSecs)
        {
            std::cout << "CowEngine server: idle for " << idleExitSecs
                      << "s with no sessions, exiting\n";
            return 0;
        }

        // 4) Yield so we don't spin a core at 100%.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
