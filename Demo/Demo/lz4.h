/*
 * lz4.h - Minimal LZ4 header (3 functions needed by streamer)
 * From LZ4 - Fast LZ compression algorithm, BSD 2-Clause, Yann Collet.
 */
#ifndef LZ4_H
#define LZ4_H
#if defined (__cplusplus)
extern "C" {
#endif

int LZ4_compressBound(int inputSize);

/*
 * LZ4_compress_fast()
 *   src / srcSize        : raw input data
 *   dst / dstCapacity    : output buffer (use LZ4_compressBound to size it)
 *   acceleration         : 1 = default fast; larger = faster but less ratio.
 *                          Recommend 2~5 for real-time LAN streaming.
 *   Returns number of bytes written into dst, or 0 on failure.
 */
int LZ4_compress_fast(const char* src, char* dst,
                      int srcSize, int dstCapacity, int acceleration);

/*
 * LZ4_decompress_safe()
 *   src / compressedSize : LZ4 block produced by the compressor.
 *   dst / dstCapacity    : output buffer (must be the EXACT original size).
 *   Returns number of bytes decompressed, or negative value on corruption.
 */
int LZ4_decompress_safe(const char* src, char* dst,
                        int compressedSize, int dstCapacity);

#if defined (__cplusplus)
}
#endif
#endif /* LZ4_H */
