// protocol.h - LAN UDP 256x256 screen streaming protocol (ultra low latency)
// Rule: per-frame independent, packet loss = drop frame, NEVER wait for retransmission
#pragma once
#include <cstdint>
#include <cstring>

constexpr int      CAPTURE_W     = 256;      // default capture width
constexpr int      CAPTURE_H     = 256;      // default capture height
constexpr int      RAW_BGR_SIZE  = CAPTURE_W * CAPTURE_H * 4; // default BGRA = 256 KB

// Frame BGRA byte size for an arbitrary w x h region
inline constexpr size_t frame_bytes(int w, int h) { return static_cast<size_t>(w) * h * 4; }

constexpr uint16_t UDP_PORT      = 9000;
constexpr int      MTU_PAYLOAD   = 1400;     // UDP payload, avoid IP fragmentation
constexpr int      HEADER_SIZE   = 16;       // Magic(4)+FrameID(4)+TotalPieces(2)+PieceID(2)+Width(2)+Height(2)
constexpr int      DATA_PER_PKT  = MTU_PAYLOAD - HEADER_SIZE; // = 1384 bytes

constexpr uint32_t MAGIC_SC25 = 0x35324353;  // 'SC25' little-endian

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;
    uint32_t frame_id;
    uint16_t total_pieces;
    uint16_t piece_id;
    uint16_t width;
    uint16_t height;
};
#pragma pack(pop)
static_assert(sizeof(PacketHeader) == HEADER_SIZE, "Header size mismatch");

inline void write_header(uint8_t* buf, uint32_t frame_id,
                         uint16_t total_pieces, uint16_t piece_id,
                         uint16_t width = CAPTURE_W, uint16_t height = CAPTURE_H) {
    auto* h = reinterpret_cast<PacketHeader*>(buf);
    h->magic        = MAGIC_SC25;
    h->frame_id     = frame_id;
    h->total_pieces = total_pieces;
    h->piece_id     = piece_id;
    h->width        = width;
    h->height       = height;
}

inline size_t frame_bytes_from_header(const PacketHeader& hdr) {
    return static_cast<size_t>(hdr.width) * hdr.height * 4;
}

inline bool read_header(const uint8_t* buf, PacketHeader& out) {
    memcpy(&out, buf, sizeof(PacketHeader));
    return out.magic == MAGIC_SC25;
}

inline uint16_t calc_pieces(size_t compressed_size) {
    return static_cast<uint16_t>((compressed_size + DATA_PER_PKT - 1) / DATA_PER_PKT);
}
