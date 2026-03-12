#pragma once
#include "packets.h"
#include <string>
#include <vector>
#include <cstdint>

// ── Multiplayer Packet IDs ────────────────────────────────────────────────────

enum class MPPacketID : uint8_t {
    AuthRequest     = 0x30, // client -> server: token + username
    AuthResponse    = 0x31, // server -> client: accepted / rejected
    PlayerSpawn     = 0x32, // server -> client: a new player appeared
    PlayerDespawn   = 0x33, // server -> client: a player left
    PlayerPosSync   = 0x34, // server -> client: batch position update
};

// ── Auth ──────────────────────────────────────────────────────────────────────

struct AuthRequestPacket {
    std::string username;
    std::string token;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)MPPacketID::AuthRequest);
        writeU32(b, (uint32_t)username.size());
        b.insert(b.end(), username.begin(), username.end());
        writeU32(b, (uint32_t)token.size());
        b.insert(b.end(), token.begin(), token.end());
        return b;
    }

    static AuthRequestPacket deserialize(const uint8_t* d, size_t len) {
        AuthRequestPacket p; size_t o = 1;
        uint32_t uLen = readU32(d, o);
        p.username.assign((const char*)d + o, uLen); o += uLen;
        uint32_t tLen = readU32(d, o);
        p.token.assign((const char*)d + o, tLen); o += tLen;
        return p;
    }
};

struct AuthResponsePacket {
    uint8_t accepted; // 1 = ok, 0 = rejected
    uint32_t playerId; // server-assigned ID
    std::string message;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)MPPacketID::AuthResponse);
        writeU8(b, accepted);
        writeU32(b, playerId);
        writeU32(b, (uint32_t)message.size());
        b.insert(b.end(), message.begin(), message.end());
        return b;
    }

    static AuthResponsePacket deserialize(const uint8_t* d, size_t len) {
        AuthResponsePacket p; size_t o = 1;
        p.accepted = readU8(d, o);
        p.playerId = readU32(d, o);
        uint32_t mLen = readU32(d, o);
        p.message.assign((const char*)d + o, mLen);
        return p;
    }
};

// ── Player spawn/despawn ──────────────────────────────────────────────────────

struct PlayerSpawnPacket {
    uint32_t playerId;
    std::string username;
    float x, y, z;
    float yaw;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)MPPacketID::PlayerSpawn);
        writeU32(b, playerId);
        writeU32(b, (uint32_t)username.size());
        b.insert(b.end(), username.begin(), username.end());
        writeF32(b, x); writeF32(b, y); writeF32(b, z);
        writeF32(b, yaw);
        return b;
    }

    static PlayerSpawnPacket deserialize(const uint8_t* d, size_t len) {
        PlayerSpawnPacket p; size_t o = 1;
        p.playerId = readU32(d, o);
        uint32_t nLen = readU32(d, o);
        p.username.assign((const char*)d + o, nLen); o += nLen;
        p.x = readF32(d, o); p.y = readF32(d, o); p.z = readF32(d, o);
        p.yaw = readF32(d, o);
        return p;
    }
};

struct PlayerDespawnPacket {
    uint32_t playerId;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)MPPacketID::PlayerDespawn);
        writeU32(b, playerId);
        return b;
    }

    static PlayerDespawnPacket deserialize(const uint8_t* d, size_t len) {
        PlayerDespawnPacket p; size_t o = 1;
        p.playerId = readU32(d, o);
        return p;
    }
};

// ── Position batch sync (server -> all clients, 20 Hz) ───────────────────────
// Contains all connected players' positions in one packet for efficiency.

struct PlayerPosEntry {
    uint32_t playerId;
    float x, y, z;
    float yaw, pitch;
};

struct PlayerPosSyncPacket {
    std::vector<PlayerPosEntry> players;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)MPPacketID::PlayerPosSync);
        writeU32(b, (uint32_t)players.size());
        for (const auto& p : players) {
            writeU32(b, p.playerId);
            writeF32(b, p.x); writeF32(b, p.y); writeF32(b, p.z);
            writeF32(b, p.yaw); writeF32(b, p.pitch);
        }
        return b;
    }

    static PlayerPosSyncPacket deserialize(const uint8_t* d, size_t len) {
        PlayerPosSyncPacket pkt; size_t o = 1;
        uint32_t count = readU32(d, o);
        pkt.players.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            auto& p = pkt.players[i];
            p.playerId = readU32(d, o);
            p.x = readF32(d, o); p.y = readF32(d, o); p.z = readF32(d, o);
            p.yaw = readF32(d, o); p.pitch = readF32(d, o);
        }
        return pkt;
    }
};
