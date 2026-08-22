// udp_sender.h - Sender: split frame into UDP datagrams, push immediately.
// No ACK, no retransmission, no buffering beyond the current frame.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <cstring>
#pragma comment(lib, "ws2_32.lib")
#include "protocol.h"

class UdpSender {
public:
    UdpSender() {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        int buf_size = 2 * 1024 * 1024;
        setsockopt(sock_, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&buf_size), sizeof(buf_size));
    }

    ~UdpSender() {
        closesocket(sock_);
        WSACleanup();
    }

    // Set receiver IP:port. Returns false on invalid IP format.
    bool set_target(const char* ip, uint16_t port = UDP_PORT) {
        memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_port   = htons(port);
        int r = inet_pton(AF_INET, ip, &addr_.sin_addr);
        if (r <= 0) {
            addr_.sin_addr.s_addr = inet_addr(ip);
            if (addr_.sin_addr.s_addr == INADDR_NONE) return false;
        }
        return true;
    }

    // Split frame into MTU-sized pieces and send all.
    // Returns false on sendto() error.
    bool send_frame(uint32_t frame_id, const uint8_t* comp_data, size_t comp_size,
                    uint16_t width = CAPTURE_W, uint16_t height = CAPTURE_H) {
        const uint16_t total = calc_pieces(comp_size);
        size_t  offset = 0;
        for (uint16_t i = 0; i < total; ++i) {
            const size_t chunk = (i == total - 1)
                                     ? comp_size - offset
                                     : static_cast<size_t>(DATA_PER_PKT);
            uint8_t pkt[MTU_PAYLOAD];
            write_header(pkt, frame_id, total, i, width, height);
            memcpy(pkt + HEADER_SIZE, comp_data + offset, chunk);
            const int to_send = static_cast<int>(HEADER_SIZE + chunk);
            int sent = sendto(sock_, reinterpret_cast<const char*>(pkt),
                              to_send, 0,
                              reinterpret_cast<sockaddr*>(&addr_), sizeof(addr_));
            if (sent != to_send) return false;
            offset += chunk;
        }
        return true;
    }

private:
    SOCKET      sock_ = INVALID_SOCKET;
    sockaddr_in addr_{};
};
