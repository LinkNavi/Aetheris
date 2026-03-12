#pragma once
#include <enet/enet.h>
#include <unordered_map>
#include <string>
#include <glm/vec3.hpp>
#include "mp_packets.h"
#include "http_client.h"
#include "net_common.h"
#include "log.h"

struct ConnectedPlayer {
    uint32_t    id = 0;
    ENetPeer*   peer = nullptr;
    std::string username;
    std::string uid; // from auth server
    bool        authenticated = false;
    glm::vec3   pos{0.f};
    float       yaw   = 0.f;
    float       pitch = 0.f;
};

class MultiplayerManager {
public:
    std::string authHost = "127.0.0.1";
    int         authPort = 8080;

    void onPeerConnect(ENetPeer* peer) {
        // Don't assign player ID yet - wait for auth
        _pending[peer] = {};
    }

    void onPeerDisconnect(ENetPeer* peer, ENetHost* host) {
        _pending.erase(peer);
        auto it = _peerToId.find(peer);
        if (it == _peerToId.end()) return;
        uint32_t pid = it->second;

        // Broadcast despawn to all other players
        PlayerDespawnPacket dp{pid};
        auto bytes = dp.serialize();
        for (auto& [otherId, other] : _players) {
            if (otherId == pid || !other.authenticated) continue;
            Net::sendReliable(other.peer, bytes);
        }

        Log::info("Player left: " + _players[pid].username + " (id=" + std::to_string(pid) + ")");
        _players.erase(pid);
        _peerToId.erase(it);
    }

    // Returns true if auth was handled (whether accepted or rejected)
    bool onAuthRequest(ENetPeer* peer, const AuthRequestPacket& req, ENetHost* host) {
        // Verify token with auth server
        std::string path = "/api/verify?token=" + req.token;
        auto resp = HttpClient::get(authHost.c_str(), authPort, path.c_str());

        bool valid = false;
        std::string serverUid;
        std::string serverUsername;

        if (resp.ok()) {
            // Minimal JSON parse - look for "valid":true
            valid = resp.body.find("\"valid\":true") != std::string::npos ||
                    resp.body.find("\"valid\": true") != std::string::npos;
            if (valid) {
                // Extract username from response
                auto upos = resp.body.find("\"username\":\"");
                if (upos == std::string::npos) upos = resp.body.find("\"username\": \"");
                if (upos != std::string::npos) {
                    auto start = resp.body.find('"', upos + 10);
                    if (start != std::string::npos) {
                        start++;
                        auto end = resp.body.find('"', start);
                        if (end != std::string::npos)
                            serverUsername = resp.body.substr(start, end - start);
                    }
                }
                auto uidpos = resp.body.find("\"uid\":\"");
                if (uidpos == std::string::npos) uidpos = resp.body.find("\"uid\": \"");
                if (uidpos != std::string::npos) {
                    auto start = resp.body.find('"', uidpos + 5);
                    if (start != std::string::npos) {
                        start++;
                        auto end = resp.body.find('"', start);
                        if (end != std::string::npos)
                            serverUid = resp.body.substr(start, end - start);
                    }
                }
            }
        }

        // Allow guest connections (no auth server or token empty)
        if (!valid && req.token.empty()) {
            valid = true;
            serverUsername = req.username.empty() ? "Guest" : req.username;
            serverUid = "guest_" + std::to_string((uintptr_t)peer);
        }

        // Also allow if auth server is down (fallback to guest)
        if (!valid && resp.status == 0) {
            Log::warn("Auth server unreachable, allowing as guest: " + req.username);
            valid = true;
            serverUsername = req.username.empty() ? "Guest" : req.username;
            serverUid = "guest_" + std::to_string((uintptr_t)peer);
        }

        if (!valid) {
            AuthResponsePacket arp{0, 0, "Authentication failed."};
            Net::sendReliable(peer, arp.serialize());
            enet_host_flush(host);
            return true;
        }

        // Assign player ID
        uint32_t pid = _nextId++;
        ConnectedPlayer cp;
        cp.id = pid;
        cp.peer = peer;
        cp.username = serverUsername.empty() ? req.username : serverUsername;
        cp.uid = serverUid;
        cp.authenticated = true;

        _players[pid] = cp;
        _peerToId[peer] = pid;
        _pending.erase(peer);

        // Send auth accepted
        AuthResponsePacket arp{1, pid, "Welcome, " + cp.username + "!"};
        Net::sendReliable(peer, arp.serialize());

        Log::info("Player authenticated: " + cp.username + " (id=" + std::to_string(pid) + ")");

        // Tell new player about all existing players
        for (auto& [otherId, other] : _players) {
            if (otherId == pid || !other.authenticated) continue;
            PlayerSpawnPacket sp{other.id, other.username,
                                other.pos.x, other.pos.y, other.pos.z, other.yaw};
            Net::sendReliable(peer, sp.serialize());
        }

        // Tell all existing players about new player
        PlayerSpawnPacket sp{pid, cp.username, 0, 0, 0, 0};
        auto spBytes = sp.serialize();
        for (auto& [otherId, other] : _players) {
            if (otherId == pid || !other.authenticated) continue;
            Net::sendReliable(other.peer, spBytes);
        }

        enet_host_flush(host);
        return true;
    }

    void onPlayerMove(ENetPeer* peer, float x, float y, float z, float yaw, float pitch) {
        auto it = _peerToId.find(peer);
        if (it == _peerToId.end()) return;
        auto pit = _players.find(it->second);
        if (pit == _players.end()) return;
        pit->second.pos = {x, y, z};
        pit->second.yaw = yaw;
        pit->second.pitch = pitch;
    }

    // Call at ~20Hz to broadcast all positions
    void broadcastPositions(ENetHost* host) {
        if (_players.size() < 2) return;

        PlayerPosSyncPacket pkt;
        for (auto& [id, p] : _players) {
            if (!p.authenticated) continue;
            pkt.players.push_back({p.id, p.pos.x, p.pos.y, p.pos.z, p.yaw, p.pitch});
        }

        auto bytes = pkt.serialize();
        for (auto& [id, p] : _players) {
            if (!p.authenticated) continue;
            Net::sendReliable(p.peer, bytes);
        }
    }

    bool isAuthenticated(ENetPeer* peer) const {
        auto it = _peerToId.find(peer);
        if (it == _peerToId.end()) return false;
        auto pit = _players.find(it->second);
        return pit != _players.end() && pit->second.authenticated;
    }

    uint32_t getPlayerId(ENetPeer* peer) const {
        auto it = _peerToId.find(peer);
        return it != _peerToId.end() ? it->second : 0;
    }

    ConnectedPlayer* getPlayer(ENetPeer* peer) {
        auto it = _peerToId.find(peer);
        if (it == _peerToId.end()) return nullptr;
        auto pit = _players.find(it->second);
        return pit != _players.end() ? &pit->second : nullptr;
    }

private:
    uint32_t _nextId = 1;
    std::unordered_map<uint32_t, ConnectedPlayer> _players;
    std::unordered_map<ENetPeer*, uint32_t>       _peerToId;
    std::unordered_map<ENetPeer*, int>             _pending; // awaiting auth
};
