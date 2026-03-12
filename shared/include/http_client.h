#pragma once
#include <string>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32")
  typedef SOCKET sock_t;
  #define SOCK_INVALID INVALID_SOCKET
  static void sock_init()  { WSADATA w; WSAStartup(MAKEWORD(2,2),&w); }
  static void sock_close(sock_t s) { closesocket(s); }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  typedef int sock_t;
  #define SOCK_INVALID (-1)
  static void sock_init()  {}
  static void sock_close(sock_t s) { close(s); }
#endif

struct HttpResponse {
    int status = 0;
    std::string body;
    bool ok() const { return status >= 200 && status < 300; }
};

// Minimal blocking HTTP POST/GET. Good enough for auth calls.
// host: e.g. "127.0.0.1", port: e.g. 8080
namespace HttpClient {

inline HttpResponse request(const char* method, const char* host, int port,
                            const char* path, const std::string& jsonBody = "") {
    HttpResponse resp;
    sock_init();

    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_INVALID) return resp;

    // Set 3s timeout
    struct timeval tv;
    tv.tv_sec = 3; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    // Try numeric first, then DNS
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        struct hostent* he = gethostbyname(host);
        if (!he) { sock_close(fd); return resp; }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        sock_close(fd); return resp;
    }

    // Build HTTP request
    char reqBuf[4096];
    int reqLen;
    if (jsonBody.empty()) {
        reqLen = snprintf(reqBuf, sizeof(reqBuf),
            "%s %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, path, host, port);
    } else {
        reqLen = snprintf(reqBuf, sizeof(reqBuf),
            "%s %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            method, path, host, port, (int)jsonBody.size(), jsonBody.c_str());
    }

    send(fd, reqBuf, reqLen, 0);

    // Read response
    std::string raw;
    char buf[4096];
    while (true) {
        int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, n);
    }
    sock_close(fd);

    // Parse status
    if (raw.size() > 12 && raw.substr(0, 4) == "HTTP") {
        resp.status = atoi(raw.c_str() + 9);
    }

    // Find body after \r\n\r\n
    auto bodyStart = raw.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        resp.body = raw.substr(bodyStart + 4);
    }

    return resp;
}

inline HttpResponse post(const char* host, int port, const char* path,
                         const std::string& json) {
    return request("POST", host, port, path, json);
}

inline HttpResponse get(const char* host, int port, const char* path) {
    return request("GET", host, port, path);
}

} // namespace HttpClient
