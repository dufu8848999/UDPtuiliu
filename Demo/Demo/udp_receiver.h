// udp_receiver.h - Receiver: piece reassembly with frame-skip policy.
// Low-latency policy:
//   1. As soon as all pieces of the current frame arrive -> emit to decoder.
//   2. If a NEWER frame arrives before the current is complete -> DROP current.
//   3. No retransmission request. No blocking timeout per frame.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <cstdint>
#include <cstring>
#include <vector>
#include "protocol.h"

class UdpReceiver {
public:
    explicit UdpReceiver(uint16_t port = UDP_PORT) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        int buf_size = 4 * 1024 * 1024;
        setsockopt(sock_, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&buf_size), sizeof(buf_size));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        DWORD ms = 1;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&ms), sizeof(ms));

        piece_buf_.resize(MTU_PAYLOAD);
    }

    ~UdpReceiver() {
        closesocket(sock_);
        WSACleanup();
    }

    // Non-blocking attempt to assemble a full frame.
    // Returns true when a complete frame is ready:
    //   - out_frame_data: reassembled compressed payload
    //   - out_frame_id  : monotonic frame id
    //   - out_w / out_h : frame dimensions (BGRA)
    // Returns false if no complete frame available yet (call again).
    bool receive(std::vector<uint8_t>& out_frame_data, uint32_t& out_frame_id,
                 uint16_t* out_w = nullptr, uint16_t* out_h = nullptr) {
        sockaddr_in from{};
        int fromlen = static_cast<int>(sizeof(from));
        int got = recvfrom(sock_,
                           reinterpret_cast<char*>(piece_buf_.data()),
                           static_cast<int>(piece_buf_.size()), 0,
                           reinterpret_cast<sockaddr*>(&from), &fromlen);
        if (got <= HEADER_SIZE) return false;

        PacketHeader hdr;
        if (!read_header(piece_buf_.data(), hdr)) return false;
        if (out_w) *out_w = hdr.width;
        if (out_h) *out_h = hdr.height;
        const int data_len = got - HEADER_SIZE;

        // ---- Frame jump: newer frame id means the old partial frame is obsolete ----
        if (hdr.frame_id != assembling_id_) {
            if (assembling_id_ != UINT32_MAX && hdr.frame_id < assembling_id_) {
                return false;  // late packet from older frame: drop silently
            }
            // Switch to new frame immediately (old partial frame is DISCARDED)
            assembling_id_   = hdr.frame_id;
            total_pieces_    = hdr.total_pieces;
            received_mask_.assign(total_pieces_, false);
            assembled_size_  = 0;
            frame_buffer_.resize(static_cast<size_t>(total_pieces_) * DATA_PER_PKT);
            received_count_  = 0;
        }

        if (hdr.piece_id >= total_pieces_) return false;

        // ---- Write piece into frame buffer ----
        if (!received_mask_[hdr.piece_id]) {
            const size_t offset = static_cast<size_t>(hdr.piece_id) * DATA_PER_PKT;
            memcpy(frame_buffer_.data() + offset,
                   piece_buf_.data() + HEADER_SIZE,
                   static_cast<size_t>(data_len));
            received_mask_[hdr.piece_id] = true;
            ++received_count_;
            assembled_size_ += static_cast<size_t>(data_len);
        }

        // ---- Complete? ----
        if (received_count_ == total_pieces_ && total_pieces_ > 0) {
            out_frame_data.assign(frame_buffer_.begin(),
                                  frame_buffer_.begin() + assembled_size_);
            out_frame_id = assembling_id_;
            assembling_id_ = UINT32_MAX;
            return true;
        }
        return false;
    }

private:
    SOCKET                 sock_ = INVALID_SOCKET;
    std::vector<uint8_t>   piece_buf_;

    uint32_t               assembling_id_  = UINT32_MAX;
    uint16_t               total_pieces_   = 0;
    uint16_t               received_count_ = 0;
    size_t                 assembled_size_ = 0;
    std::vector<uint8_t>   frame_buffer_;
    std::vector<bool>      received_mask_;
};
