#pragma once
#include "packets.h"
#include <string>

enum class ChatPacketID : uint8_t {
    ChatMessage = 0x50, // client -> server: raw message text
    ChatBroadcast = 0x51, // server -> client: username + message
};

struct ChatMessagePacket {
    std::string text; // raw text from client, max 256 chars

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)ChatPacketID::ChatMessage);
        uint32_t len = (uint32_t)std::min(text.size(), (size_t)256);
        writeU32(b, len);
        b.insert(b.end(), text.begin(), text.begin() + len);
        return b;
    }
    static ChatMessagePacket deserialize(const uint8_t* d, size_t len) {
        ChatMessagePacket p; size_t o = 1;
        uint32_t tlen = readU32(d, o);
        tlen = (uint32_t)std::min((size_t)tlen, len - o);
        p.text.assign((const char*)d + o, tlen);
        return p;
    }
};

struct ChatBroadcastPacket {
    std::string username;
    std::string text;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)ChatPacketID::ChatBroadcast);
        writeU32(b, (uint32_t)username.size());
        b.insert(b.end(), username.begin(), username.end());
        writeU32(b, (uint32_t)text.size());
        b.insert(b.end(), text.begin(), text.end());
        return b;
    }
    static ChatBroadcastPacket deserialize(const uint8_t* d, size_t len) {
        ChatBroadcastPacket p; size_t o = 1;
        uint32_t ulen = readU32(d, o);
        ulen = (uint32_t)std::min((size_t)ulen, len - o);
        p.username.assign((const char*)d + o, ulen); o += ulen;
        uint32_t tlen = readU32(d, o);
        tlen = (uint32_t)std::min((size_t)tlen, len - o);
        p.text.assign((const char*)d + o, tlen);
        return p;
    }
};
