// Author: Kunduru Shiva Tusshar
#pragma once

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
  #pragma comment(lib, "ws2_32.lib")
  #endif
  typedef int socklen_t;
  #define close_socket closesocket
#else
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #include <sys/time.h>
  #define SOCKET int
  #define INVALID_SOCKET -1
  #define SOCKET_ERROR -1
  #define close_socket close
#endif

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

using namespace std;

enum PacketType : uint8_t {
    PKT_DATA = 1,
    PKT_ACK  = 2,
    PKT_NACK = 3,
    PKT_END  = 4
};

#pragma pack(push, 1)
struct Header {
    uint8_t type;
    uint8_t seq_num;
    uint16_t length;
};
#pragma pack(pop)

inline void init_networking() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

inline void cleanup_networking() {
#ifdef _WIN32
    WSACleanup();
#endif
}

inline bool send_all(SOCKET sock, const void* data, size_t len) {
    const char* ptr = (const char*)data;
    while (len > 0) {
        int sent = send(sock, ptr, (int)len, 0);
        if (sent <= 0) return false;
        ptr += sent;
        len -= sent;
    }
    return true;
}

inline bool recv_all(SOCKET sock, void* data, size_t len) {
    char* ptr = (char*)data;
    while (len > 0) {
        int received = recv(sock, ptr, (int)len, 0);
        if (received <= 0) return false;
        ptr += received;
        len -= received;
    }
    return true;
}

inline int wait_for_read(SOCKET sock, int timeout_sec) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    return select((int)sock + 1, &fds, nullptr, nullptr, &tv);
}
