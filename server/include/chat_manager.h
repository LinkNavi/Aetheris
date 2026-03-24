#pragma once
#include <enet/enet.h>
#include <string>
#include <vector>
#include <sstream>
#include "chat_packets.h"
#include "multiplayer_manager.h"
#include "inventory_manager.h"
#include "net_common.h"
#include "log.h"
#include "item.h"

class ChatManager {
public:
    ChatManager(MultiplayerManager& mp, InventoryManager& inv)
        : _mp(mp), _inv(inv) {}

    // Call from server packet loop when ChatMessage arrives
    void onMessage(ENetPeer* peer, const ChatMessagePacket& pkt,
                   ENetHost* host) {
        if (pkt.text.empty()) return;

        const std::string& text = pkt.text;

        if (text[0] == '/') {
            handleCommand(peer, text, host);
            return;
        }

        // Normal message — get username and broadcast
        std::string username = getUsername(peer);
        Log::info("[CHAT] " + username + ": " + text);
        broadcast(username, text, host, nullptr); // nullptr = send to everyone
    }

private:
    MultiplayerManager& _mp;
    InventoryManager&   _inv;

    std::string getUsername(ENetPeer* peer) {
        auto* player = _mp.getPlayer(peer);
        return player ? player->username : "Unknown";
    }

    // Broadcast to all authenticated players.
    // exclude: if non-null, skip that peer (used for commands that echo back
    // a system message to sender only — pass nullptr for normal chat)
    void broadcast(const std::string& username, const std::string& text,
                   ENetHost* host, ENetPeer* exclude) {
        ChatBroadcastPacket pkt{username, text};
        auto bytes = pkt.serialize();
        // MultiplayerManager::broadcastToAll broadcasts to all authenticated peers
        // We need to do it manually here to support the exclude param
        // Walk peers directly via ENet host
        for (size_t i = 0; i < host->peerCount; i++) {
            ENetPeer* p = &host->peers[i];
            if (p->state != ENET_PEER_STATE_CONNECTED) continue;
            if (p == exclude) continue;
            if (!_mp.isAuthenticated(p)) continue;
            Net::sendReliable(p, bytes);
        }
    }

    // Send a system message only to one peer
    void sendSystem(ENetPeer* peer, const std::string& text) {
        ChatBroadcastPacket pkt{"", text};
        Net::sendReliable(peer, pkt.serialize());
    }

    // ── Command dispatcher ────────────────────────────────────────────────────
    void handleCommand(ENetPeer* peer, const std::string& raw, ENetHost* host) {
        // Tokenise
        std::vector<std::string> args;
        std::istringstream ss(raw.substr(1)); // strip leading /
        std::string tok;
        while (ss >> tok) args.push_back(tok);
        if (args.empty()) return;

        std::string cmd = args[0];
        // lowercase
        for (auto& c : cmd) c = (char)tolower((unsigned char)c);

        std::string username = getUsername(peer);
        Log::info("[CMD] " + username + " ran: " + raw);

        if (cmd == "give")         cmdGive(peer, args);
        else if (cmd == "clear")   cmdClear(peer, args);
        else if (cmd == "say")     cmdSay(peer, args, host);
        else if (cmd == "help")    cmdHelp(peer);
        else
            sendSystem(peer, "Unknown command: /" + cmd + "  — try /help");
    }

    // /give <itemname> [count]
    // e.g.  /give grimoire   /give healpotion 5   /give sword
    void cmdGive(ENetPeer* peer, const std::vector<std::string>& args) {
        if (args.size() < 2) {
            sendSystem(peer, "Usage: /give <itemname> [count]");
            return;
        }

        int count = 1;
        if (args.size() >= 3) {
            try { count = std::stoi(args[2]); }
            catch (...) { count = 1; }
            count = std::max(1, std::min(count, 999));
        }

        // Fuzzy match item name against ITEM_DEFS
        std::string query = args[1];
        for (auto& c : query) c = (char)tolower((unsigned char)c);

        ItemID found = ItemID::None;
        for (int i = 1; i < ITEM_COUNT; i++) {
            std::string defName(ITEM_DEFS[i].name);
            // Strip spaces for matching: "healpotion" matches "Heal Potion"
            std::string defLower;
            for (auto c : defName)
                if (c != ' ') defLower += (char)tolower((unsigned char)c);

            if (defLower.find(query) != std::string::npos ||
                query.find(defLower) != std::string::npos) {
                found = (ItemID)i;
                break;
            }
        }

        if (found == ItemID::None) {
            sendSystem(peer, "Unknown item: " + args[1]);
            return;
        }

        // Add to player inventory
        const ItemDef& def = getItemDef(found);
        // Get mutable inventory via inventory manager
        // We need to reach into the manager — expose a giveItem helper
        bool ok = giveItem(peer, found, count);
        if (ok)
            sendSystem(peer, "Gave " + std::to_string(count) + "x " +
                             std::string(def.name));
        else
            sendSystem(peer, "Inventory full.");
    }

    // /clear [itemname]  — clears whole inventory or specific item
    void cmdClear(ENetPeer* peer, const std::vector<std::string>& args) {
        // For now just notify — full impl needs inventory manager access
        sendSystem(peer, "/clear not yet implemented.");
    }

    // /say <message>  — broadcast as server/system message
    void cmdSay(ENetPeer* peer, const std::vector<std::string>& args,
                ENetHost* host) {
        if (args.size() < 2) { sendSystem(peer, "Usage: /say <message>"); return; }
        std::string msg;
        for (size_t i = 1; i < args.size(); i++) {
            if (i > 1) msg += ' ';
            msg += args[i];
        }
        broadcast("", "[Server] " + msg, host, nullptr);
    }

    // /help
    void cmdHelp(ENetPeer* peer) {
        sendSystem(peer, "Commands:");
        sendSystem(peer, "  /give <item> [count]  — give yourself an item");
        sendSystem(peer, "  /say <message>        — broadcast as server");
        sendSystem(peer, "  /help                 — show this list");
    }

    // ── Inventory integration ─────────────────────────────────────────────────
    bool giveItem(ENetPeer* peer, ItemID id, int count) {
        // InventoryManager doesn't expose a mutable getPlayerInv publicly,
        // so we use the existing onInventoryMoveReq path indirectly.
        // Instead, we directly call a helper we'll add to InventoryManager.
        // For now, use the public API: sendInventoryState after modifying.
        // This requires InventoryManager to expose giveItem — see note below.
        return _inv.giveItem(peer, id, count);
    }
};
