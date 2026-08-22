/*
 * lz4.c - Self-contained, minimal LZ4 legacy block compressor/decompressor.
 * Targets small frames (~256 KB) for ultra-low-latency screen streaming.
 * Conforms to LZ4 Block Format:
 *   https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md
 *
 * - CompressBound / compress_fast / decompress_safe only.
 * - No heap usage (hash table lives on the stack, 16K entries).
 * - Compression ratio is intentionally traded for speed via acceleration param.
 *
 * Copyright (c) 2024, Minimal implementation provided AS-IS under 2-clause BSD.
 */
#include "lz4.h"
#include <stdint.h>
#include <string.h>

typedef  uint8_t   BYTE;
typedef  uint16_t  U16;
typedef  uint32_t  U32;

static inline U16 read16(const void* p) {
    U16 v; memcpy(&v, p, 2); return v;
}
static inline void write16(void* p, U16 v) {
    memcpy(p, &v, 2);
}
static inline U32 read32(const void* p) {
    U32 v; memcpy(&v, p, 4); return v;
}

#define MINMATCH        4
#define MFLIMIT         12
#define LASTLITERALS    5
#define HASHTABLE_SZ    (1u << 14)   /* 16384 slots, 32KB stack memory */

/* ======================= LZ4_compressBound ======================= */
int LZ4_compressBound(int isize) {
    if (isize <= 0) return 0;
    return isize + (isize / 255) + 16;
}

/* ======================= LZ4_compress_fast ======================= */
int LZ4_compress_fast(const char* src, char* dst,
                      int srcSize, int dstCapacity, int acceleration)
{
    const BYTE* const base        = (const BYTE*)src;
    const BYTE*       ip          = base;
    const BYTE* const iend        = base + srcSize;
    const BYTE*       anchor      = base;
    const BYTE* const mflimit     = iend - MFLIMIT;
    const BYTE* const matchlimit  = iend - LASTLITERALS;

    BYTE*       op   = (BYTE*)dst;
    BYTE* const oend = op + dstCapacity;

    if (acceleration < 1) acceleration = 1;

    /* Hash table: stores (offset-from-base) as U16.
     * For offsets > 64KB the hash is still useful but a stale hit
     * is naturally discarded by the offset bound check below. */
    U16 hashTable[HASHTABLE_SZ];
    memset(hashTable, 0, sizeof(hashTable));

#define HASH(v)  ((((U32)(v) * 2654435761u) >> (32 - 14)) & (HASHTABLE_SZ - 1))

    if (srcSize < MINMATCH + 1) goto _last_literals;

    while (ip < mflimit) {
        const BYTE* ref;
        U32 h = HASH(read32(ip));
        ref = base + hashTable[h];
        hashTable[h] = (U16)(ip - base);

        /* Match: same 4-byte word AND offset within 1..65535 bytes. */
        if ((ip - ref) > 0 && (ip - ref) < 65536 &&
            read32(ref) == read32(ip)) goto _found_match;

        ip += acceleration;
        continue;

_found_match:
        /* ---- Emit literals token + length + bytes ---- */
        BYTE* token_byte;   /* saved pointer to the 1-byte token for this sequence */
        {
            size_t litLen = (size_t)(ip - anchor);
            if (op + litLen + 2 + 1 + LASTLITERALS + (litLen + 254) / 255 > oend)
                return 0;
            token_byte = op++;
            if (litLen >= 15) {
                size_t len = litLen - 15;
                *token_byte = 0xF0;
                while (len >= 255) { *op++ = 255; len -= 255; }
                *op++ = (BYTE)len;
            } else {
                *token_byte = (BYTE)(litLen << 4);
            }
            if (litLen) { memcpy(op, anchor, litLen); op += litLen; }
        }

        /* ---- Extend match beyond the initial 4 bytes ---- */
        {
            const BYTE* s = ref  + MINMATCH;
            const BYTE* t = ip   + MINMATCH;
            size_t matchLen;
            while (t < matchlimit && *s == *t) { ++s; ++t; }
            matchLen = (size_t)(t - ip);
            ip = t;

            /* 2-byte little-endian offset */
            {
                /* Match reference is relative to CURRENT output position
                   (position AFTER the literals were just written). In source
                   terms this is `t - matchLen`, i.e. where the 4-byte match
                   began before we extended it forward.
                   ip has been set to `t`, so `ip - matchLen` equals match start. */
                size_t offset = (size_t)(ip - matchLen) - (size_t)ref;
                write16(op, (U16)offset);
                op += 2;
            }

            /* match length encoded as (matchLen - MINMATCH) into low nibble
               of the SAME token byte we wrote at the start of this sequence.
               Do NOT use op-3 (that assumes literal ext=0 + litLen=0 which is
               almost never true and corrupts literals data). */
            {
                size_t lenCode = matchLen - MINMATCH;
                if (lenCode >= 15) {
                    *token_byte |= 0x0F;
                    lenCode -= 15;
                    while (lenCode >= 255) { *op++ = 255; lenCode -= 255; }
                    *op++ = (BYTE)lenCode;
                } else {
                    *token_byte |= (BYTE)lenCode;
                }
            }

            /* Fill hash positions we skipped while searching (so future
               searches at those positions can find matches). */
            while (anchor < ip - 2) {
                U32 h2 = HASH(read32(anchor));
                hashTable[h2] = (U16)(anchor - base);
                ++anchor;
            }
            anchor = ip;
        }
    }

_last_literals:
    {
        size_t litLen = (size_t)(iend - anchor);
        if (op + 1 + litLen + (litLen + 254) / 255 > oend) return 0;
        if (litLen >= 15) {
            size_t len = litLen - 15;
            *op++ = 0xF0;
            while (len >= 255) { *op++ = 255; len -= 255; }
            *op++ = (BYTE)len;
        } else {
            *op++ = (BYTE)(litLen << 4);
        }
        memcpy(op, anchor, litLen); op += litLen;
    }
#undef HASH
    return (int)(op - (BYTE*)dst);
}

/* ======================= LZ4_decompress_safe ======================= */
int LZ4_decompress_safe(const char* src, char* dst,
                        int compressedSize, int dstCapacity)
{
    const BYTE* ip   = (const BYTE*)src;
    const BYTE* iend = ip + compressedSize;
    BYTE*       op   = (BYTE*)dst;
    BYTE* const oend = op + dstCapacity;
    BYTE* const oexit = oend - LASTLITERALS;

    while (ip < iend) {
        unsigned token;
        size_t length;

        token = *ip++;

        /* ---------- Literals ---------- */
        length = token >> 4;
        if (length == 15) {
            unsigned c;
            do {
                if (ip >= iend) return -1;
                c = *ip++;
                length += c;
            } while (c == 255);
        }
        {
            BYTE* const cpy = op + length;
            if ((cpy > oexit) || ((const BYTE*)(ip + length) > iend)) {
                if ((cpy > oend) || ((const BYTE*)(ip + length) > iend))
                    return -1;
            }
            if (length) {
                memcpy(op, ip, length);
                op  = cpy;
                ip += length;
            }
        }
        if (ip >= iend) break;

        /* ---------- Match copy ---------- */
        {
            const BYTE* match;
            size_t offset = (size_t)read16(ip);
            ip += 2;
            if (offset == 0) return -1;
            match = op - offset;
            if (match < (BYTE*)dst) return -1;

            length = token & 0x0F;
            if (length == 15) {
                unsigned c;
                do {
                    if (ip >= iend) return -1;
                    c = *ip++;
                    length += c;
                } while (c == 255);
            }
            length += MINMATCH;
            {
                BYTE* const cpy = op + length;
                if (cpy > oexit) {
                    if (cpy > oend) return -1;
                }
                /* Overlap-aware copy (for small offsets such as RLE of 1 byte) */
                if ((size_t)(op - match) >= 8) {
                    memcpy(op, match, length);
                } else {
                    BYTE* d = op;
                    const BYTE* s = match;
                    size_t n = length;
                    while (n--) *d++ = *s++;
                }
                op = cpy;
            }
        }
    }
    return (int)(op - (BYTE*)dst);
}
