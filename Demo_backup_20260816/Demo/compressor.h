// compressor.h - LZ4 fast compression wrapper (sub-ms for 256KB frames)
// Integration note: add lz4.c / lz4.h directly to the project.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "protocol.h"

extern "C" {
    int LZ4_compress_fast(const char* src, char* dst, int srcSize, int dstCapacity, int acceleration);
    int LZ4_decompress_safe(const char* src, char* dst, int compressedSize, int dstCapacity);
    int LZ4_compressBound(int inputSize);
}

class CompressorLZ4 {
public:
    CompressorLZ4() {
        out_buf_.resize(static_cast<size_t>(LZ4_compressBound(RAW_BGR_SIZE)) + 16);
    }

    // Returns compressed size in bytes. 0 on failure.
    // acceleration: 1 (default) ~ 10 (faster / slightly worse ratio). LAN recommend 3.
    size_t compress(const uint8_t* raw_bgra, int accel = 3) {
        const int compressed = LZ4_compress_fast(
            reinterpret_cast<const char*>(raw_bgra),
            reinterpret_cast<char*>(out_buf_.data()),
            RAW_BGR_SIZE, static_cast<int>(out_buf_.size()), accel);
        if (compressed <= 0) return 0;
        return static_cast<size_t>(compressed);
    }

    const uint8_t* data() const { return out_buf_.data(); }

private:
    std::vector<uint8_t> out_buf_;
};

class DecompressorLZ4 {
public:
    DecompressorLZ4() {
        out_buf_.resize(RAW_BGR_SIZE);
    }

    // Returns true if decompression produced exactly RAW_BGR_SIZE bytes.
    bool decompress(const uint8_t* comp_data, size_t comp_size) {
        const int got = LZ4_decompress_safe(
            reinterpret_cast<const char*>(comp_data),
            reinterpret_cast<char*>(out_buf_.data()),
            static_cast<int>(comp_size), RAW_BGR_SIZE);
        return got == RAW_BGR_SIZE;
    }

    const uint8_t* bgra() const { return out_buf_.data(); }

private:
    std::vector<uint8_t> out_buf_;
};
