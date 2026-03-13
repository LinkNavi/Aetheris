// tools/bot_client.cpp
// Headless multiplayer test bot. No Vulkan, no window — just ENet + packets.
// Connects, authenticates (guest), walks in a circle, prints other players.
//
// Build:
//   g++ -std=c++17 -Ishared/include -o bot_client tools/bot_client.cpp \
//       -lenet -lm -pthread
//   (or add to meson as a separate target)
//
// Usage:
//   ./bot_client                          # connect to 127.0.0.1:7777 as "Bot1"
//   ./bot_client 127.0.0.1 7777 Bot2     # custom ip/port/name
//   ./bot_client 127.0.0.1 7777 Bot3 mytoken  # with auth token

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include <string>
#include <enet/enet.h>

// ── Inline the minimal packet serialization we need ───────────────────────────
#include <vector>
#include <cstdint>

static void writeU8 (std::vector<uint8_t>& b, uint8_t  v) { b.push_back(v); }
static void writeU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((v>>24)&0xFF); b.push_back((v>>16)&0xFF);
    b.push_back((v>> 8)&0xFF); b.push_back( v     &0xFF);
}
static void writeF32(std::vector<uint8_t>& b, float v) {
    uint32_t tmp; memcpy(&tmp, &v, 4); writeU32(b, tmp);
}
static uint8_t  readU8 (const uint8_t* d, size_t& o) { return d[o++]; }
static uint32_t readU32(const uint8_t* d, size_t& o) {
    uint32_t v = ((uint32_t)d[o]<<24)|((uint32_t)d[o+1]<<16)|
                 ((uint32_t)d[o+2]<< 8)|((uint32_t)d[o+3]);
    o+=4; return v;
}
static float readF32(const uint8_t* d, size_t& o) {
    uint32_t tmp = readU32(d,o); float v; memcpy(&v,&tmp,4); return v;
}

// Packet IDs we care about
enum : uint8_t {
    PID_ChunkData     = 0x01,
    PID_PlayerMove     = 0x02,
    PID_SpawnPosition  = 0x05,
    PID_AuthRequest    = 0x30,
    PID_AuthResponse   = 0x31,
    PID_PlayerSpawn    = 0x32,
    PID_PlayerDespawn  = 0x33,
    PID_PlayerPosSync  = 0x34,
};

static void sendReliable(ENetPeer* peer, const std::vector<uint8_t>& data) {
    ENetPacket* pkt = enet_packet_create(data.data(), data.size(),
                                         ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, pkt);
}

int main(int argc, char** argv) {
    const char* ip   = argc > 1 ? argv[1] : "127.0.0.1";
    int         port = argc > 2 ? atoi(argv[2]) : 7777;
    const char* name = argc > 3 ? argv[3] : "Bot1";
    const char* token = argc > 4 ? argv[4] : "";

    printf("[Bot] %s connecting to %s:%d\n", name, ip, port);

    if (enet_initialize() != 0) { fprintf(stderr, "enet init failed\n"); return 1; }

    ENetHost* host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!host) { fprintf(stderr, "host create failed\n"); return 1; }

    ENetAddress addr{};
    enet_address_set_host(&addr, ip);
    addr.port = (uint16_t)port;

    ENetPeer* server = enet_host_connect(host, &addr, 2, 0);
    if (!server) { fprintf(stderr, "connect failed\n"); return 1; }

    // Wait for connection
    ENetEvent ev;
    if (enet_host_service(host, &ev, 5000) <= 0 || ev.type != ENET_EVENT_TYPE_CONNECT) {
        fprintf(stderr, "[Bot] Connection failed\n");
        enet_peer_reset(server);
        enet_host_destroy(host);
        enet_deinitialize();
        return 1;
    }

    printf("[Bot] Connected! Sending auth...\n");

    // Send auth request
    {
        std::vector<uint8_t> b;
        writeU8(b, PID_AuthRequest);
        std::string sName(name), sToken(token);
        writeU32(b, (uint32_t)sName.size());
        b.insert(b.end(), sName.begin(), sName.end());
        writeU32(b, (uint32_t)sToken.size());
        b.insert(b.end(), sToken.begin(), sToken.end());
        sendReliable(server, b);
        enet_host_flush(host);
    }

    // State
    float posX = 0.f, posY = 80.f, posZ = 0.f;
    float yaw = 0.f;
    float circleAngle = 0.f;
    float circleRadius = 8.f;
    float circleSpeed = 0.5f; // radians/sec
    bool spawned = false;
    bool authed = false;
    uint32_t myId = 0;

    using Clock = std::chrono::steady_clock;
    auto lastTime = Clock::now();
    float sendAccum = 0.f;

    printf("[Bot] Running. Ctrl+C to stop.\n");

    while (true) {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        if (dt > 0.1f) dt = 0.1f;

        // Poll network
        while (enet_host_service(host, &ev, 0) > 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                const uint8_t* d = ev.packet->data;
                size_t len = ev.packet->dataLength;
                if (len > 0) {
                    uint8_t pid = d[0];

                    if (pid == PID_AuthResponse) {
                        size_t o = 1;
                        uint8_t accepted = readU8(d, o);
                        uint32_t playerId = readU32(d, o);
                        uint32_t msgLen = readU32(d, o);
                        std::string msg((const char*)d + o, msgLen);
                        if (accepted) {
                            authed = true;
                            myId = playerId;
                            printf("[Bot] Auth accepted: %s (id=%u)\n", msg.c_str(), playerId);
                        } else {
                            printf("[Bot] Auth rejected: %s\n", msg.c_str());
                            goto done;
                        }
                    }
                    else if (pid == PID_SpawnPosition) {
                        size_t o = 1;
                        posX = readF32(d, o);
                        posY = readF32(d, o);
                        posZ = readF32(d, o);
                        spawned = true;
                        printf("[Bot] Spawn at (%.1f, %.1f, %.1f)\n", posX, posY, posZ);
                    }
                    else if (pid == PID_PlayerSpawn) {
                        size_t o = 1;
                        uint32_t pid2 = readU32(d, o);
                        uint32_t nLen = readU32(d, o);
                        std::string pname((const char*)d + o, nLen); o += nLen;
                        float px = readF32(d, o), py = readF32(d, o), pz = readF32(d, o);
                        printf("[Bot] Player spawned: %s (id=%u) at (%.1f,%.1f,%.1f)\n",
                               pname.c_str(), pid2, px, py, pz);
                    }
                    else if (pid == PID_PlayerDespawn) {
                        size_t o = 1;
                        uint32_t pid2 = readU32(d, o);
                        printf("[Bot] Player left (id=%u)\n", pid2);
                    }
                    else if (pid == PID_PlayerPosSync) {
                        // Silent — too spammy to print
                    }
                    else if (pid == PID_ChunkData) {
                        // Ignore chunk data — we're headless
                    }
                    // Ignore all other packets (inventory, stats, etc.)
                }
                enet_packet_destroy(ev.packet);
            }
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                printf("[Bot] Disconnected from server\n");
                goto done;
            }
        }

        // Walk in circle around spawn point
        if (spawned && authed) {
            circleAngle += circleSpeed * dt;
            float cx = posX + cosf(circleAngle) * circleRadius;
            float cz = posZ + sinf(circleAngle) * circleRadius;
            yaw = -circleAngle * (180.f / 3.14159f) + 90.f;

            // Send position at 20Hz
            sendAccum += dt;
            if (sendAccum >= 0.05f) {
                sendAccum = 0.f;
                std::vector<uint8_t> b;
                writeU8(b, PID_PlayerMove);
                writeF32(b, cx);
                writeF32(b, posY);
                writeF32(b, cz);
                writeF32(b, yaw);
                writeF32(b, 0.f); // pitch
                sendReliable(server, b);
                enet_host_flush(host);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

done:
    enet_peer_disconnect(server, 0);
    enet_host_flush(host);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    enet_host_destroy(host);
    enet_deinitialize();
    printf("[Bot] Bye.\n");
    return 0;
}
