/*
 * p8totic.c
 *
 * Copyright (C) 2022 bzt (bztsrc@gitlab) MIT license
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * @brief Small tool to convert PICO-8 cartriges to TIC-80 cartridges
 * https://gitlab.com/bztsrc/p8totic
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_JPEG
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_SIMD
#define STBI_NO_STDIO
#define STBI_ASSERT(x)
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_ONLY_PNG
#define STBI_WRITE_NO_FAILURE_STRINGS
#define STBI_WRITE_NO_SIMD
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include "lua_conv.h"   /* Lua converter and helper lib, PICO-8 wrapper by musurca */
#include "lua_infl.h"   /* PICO-8 compressed code section inflater by lexaloffle */
#define LUAMAX 524288   /* biggest Lua code we can handle */

/* stuff needed to decrypt/encrypt a TIC-80 png cartridge, from src/ext/png.c (see https://github.com/nesbox/TIC-80) */
typedef union {
    struct { uint32_t bits:8; uint32_t size:24; };
    uint8_t data[4];
} Header;
#define BITS_IN_BYTE 8
#define HEADER_BITS 4
#define HEADER_SIZE ((int)sizeof(Header) * BITS_IN_BYTE / HEADER_BITS)
#define MIN(a,b)            ((a) < (b) ? (a) : (b))
#define MAX(a,b)            ((a) > (b) ? (a) : (b))
#define CLAMP(v,a,b)        (MIN(MAX(v,a),b))
#define BITCHECK(a,b)       (!!((a) & (1ULL<<(b))))
#define _BITSET(a,b)        ((a) |= (1ULL<<(b)))
#define _BITCLEAR(a,b)      ((a) &= ~(1ULL<<(b)))
static inline void bitcpy(uint8_t* dst, uint32_t to, const uint8_t* src, uint32_t from, uint32_t size) {
    uint32_t i;
    for(i = 0; i < size; i++, to++, from++)
        BITCHECK(src[from >> 3], from & 7) ? _BITSET(dst[to >> 3], to & 7) : _BITCLEAR(dst[to >> 3], to & 7);
}
static inline int32_t ceildiv(int32_t a, int32_t b) { return (a + b - 1) / b; }
/* TIC-80 png stuff end */

#define HEX(a) (a>='0' && a<='9' ? a-'0' : (a>='a' && a<='f' ? a-'a'+10 : (a>='A' && a<='F' ? a-'A'+10 : 0)))
#define TICHDR(h,s) do{\
    if(ptr - out + s > maxlen) goto err;\
    *ptr++ = h; n = s; *ptr++ = n & 0xff; *ptr++ = (n >> 8) & 0xff; *ptr++ = (n >> 16) & 0xff;\
    }while(0);

/**
 * The default PICO-8 waveforms
 * To generate defaults, enable this define, then compile and run with `gcc p8totic.c -o p8totic -lm; ./p8totic`
 */
/*#define GENWAVEFORM*/
static uint8_t picowave[256] = {
    0xef, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x22, 0x21, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xee, /* 0 - sine */
    0x32, 0x43, 0x44, 0x55, 0x66, 0x77, 0x88, 0x88, 0x98, 0xa9, 0xba, 0xcb, 0xcc, 0xdd, 0xbe, 0x58, /* 1 - triangle */
    0x88, 0x98, 0xa9, 0xba, 0xbb, 0xcc, 0xdd, 0xee, 0x21, 0x32, 0x43, 0x54, 0x55, 0x66, 0x77, 0x88, /* 2 - sawtooth */
    0xbb, 0xbb, 0xbb, 0xbb, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xbb, 0xbb, 0xbb, 0xbb, /* 3 - square */
    0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, /* 4 - short square / pulse */
    0xbc, 0x9a, 0x88, 0x56, 0x54, 0x66, 0x87, 0x88, 0x89, 0x88, 0x67, 0x56, 0x54, 0x86, 0x98, 0xba, /* 5 - ringing / organ */
    0x35, 0x59, 0x7d, 0x69, 0x83, 0xc6, 0x35, 0xda, 0x72, 0x42, 0xd3, 0x5c, 0x42, 0x8e, 0xcb, 0x2b, /* 6 - noise */
    0xab, 0x9a, 0x88, 0x78, 0x67, 0x55, 0x34, 0x23, 0x22, 0x33, 0x54, 0x65, 0x77, 0x88, 0x98, 0xaa, /* 7 - ringing sine / phaser */

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 8 to 15 custom generated */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/**
 * Generate custom PICO-8 waveforms
 */
void pico_genwave(uint8_t *out, uint16_t *in, uint8_t flags, uint8_t speed, uint8_t start, uint8_t end)
{
    /* FIXME: calculate waveform */

    /* out: 16 bytes as in picowave, each byte contains 2 values so 32 samples in total, one is 0 to 15, on 4 bits */
    (void)out;
    /* in: 32 times:
     *  bit 0..5: pitch,
     *  bit 6..8: waveform lower 3 bits (0 to 7 one of the default waveforms, 8 to 15 one of the already generated custom waves),
     *  bit 9..11: volume,
     *  bit 12..14: effect (0 none, 1 slide, 2 vibrato, 3 drop, 4 fade in, 5 fade out, 6 arp fast, 7 arp slow),
     *  bit 15: waveform most significant 4th bit
     *
     *  source wave samples: picowave + 16 * (((bit 15) << 3) | (bit 6..8)), apply volume, effects etc. and store into out
     *  difficulty: can't use math.h, you're limited to integer arithmetic */
    (void)in;
    /* flags:
     *  bit 0: editor mode
     *  bit 1: noiz
     *  bit 2: buzz
     *  bit 3..4: detune
     *  bit ?..?: reverb
     *  bit ?..?: dampen */
    (void)flags;
    /* speed: in 183 ticks, assuming 22050 ticks per second */
    (void)speed;
    /* start, end: loop positions */
    (void)start; (void)end;
}

/**
 * The default PICO-8 palette
 */
static uint8_t picopal[48] = {
    0x00, 0x00, 0x00, 0x1D, 0x2B, 0x53, 0x7E, 0x25, 0x53, 0x00, 0x87, 0x51, 0xAB, 0x52, 0x36, 0x5F,
    0x57, 0x4F, 0xC2, 0xC3, 0xC7, 0xFF, 0xF1, 0xE8, 0xFF, 0x00, 0x4D, 0xFF, 0xA3, 0x00, 0xFF, 0xEC,
    0x27, 0x00, 0xE4, 0x36, 0x29, 0xAD, 0xFF, 0x83, 0x76, 0x9C, 0xFF, 0x77, 0xA8, 0xFF, 0xCC, 0xAA
};

/**
 * Match pico palette and return index
 */
uint8_t picopal_idx(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t i, m = 0, dr, dg, db;
    uint32_t d, dm = -1U;

    /* we need to mask the lower 2 bits in each channel before comparing, as those are used to store cartridge data */
    r &= ~3; g &= ~3; b &= ~3;
    for(i = 0; i < 16; i++) {
        /* exact match */
        if((picopal[i * 3] & ~3) == r && (picopal[i * 3 + 1] & ~3) == g && (picopal[i * 3 + 2] & ~3) == b)
            return i;
        /* remember closest match */
        dr = r > picopal[i * 3 + 0] ? r - picopal[i * 3 + 0] : picopal[i * 3 + 0] - r;
        dg = g > picopal[i * 3 + 1] ? g - picopal[i * 3 + 1] : picopal[i * 3 + 1] - g;
        db = b > picopal[i * 3 + 2] ? b - picopal[i * 3 + 2] : picopal[i * 3 + 2] - b;
        /* no need to calculate sqrt(), we don't need the exact distance, we just care about which one is the smallest */
        d = (uint32_t)dr*(uint32_t)dr + (uint32_t)dg*(uint32_t)dg + (uint32_t)db*(uint32_t)db;
        if(d < dm) { dm = d; m = i; }
    }
    return m;
}

/**
 * Public API function to convert cartridges
 */
int p8totic(uint8_t *buf, int size, uint8_t *out, int maxlen)
{
    Header header;
    int w = 0, h = 0, f, i, j, d, s, e, n;
    uint8_t *ptr, *pixels = NULL, *raw = NULL, *lua = NULL, *lu2 = NULL, *lbl = NULL;
    uint8_t *gfx = NULL, *gff = NULL, *map = NULL, *mus = NULL, *snd = NULL, *S, *D;
    uint16_t *sn, *dn;

    if(!buf || size < 1 || !out || maxlen < LUAMAX) return 0;
    memset(out, 0, maxlen);

    /****************** parse PICO-8 cartridge ******************/
    if(!memcmp(buf, "pico-8 cartridge", 16)) {
        /****** decode textual format ******/
        /* skip over header */
        for(; *buf && (buf[0] != '_' || buf[1] != '_'); buf++);
        if(!buf[0]) return -1;

        while(*buf) {

            /*** lua script ***/
            if(!memcmp(buf, "__lua__", 7)) {
                for(buf += 7; *buf == '\r' || *buf == '\n'; buf++);
                for(ptr = buf; *ptr && memcmp(ptr - 1, "\n__", 3); ptr++);
                if(!lua) {
                    i = strlen(p8totic_lua);
                    lua = (uint8_t*)malloc(LUAMAX + i + 1);
                    if(!lua) goto err;
                    j = *ptr; *ptr = 0;
                    /* no need for pico_lua_to_utf8(), this is already utf-8 */
                    /* add the Lua helper library */
                    memcpy(lua, p8totic_lua, i);
                    /* add the converted Lua code */
                    pico_lua_to_tic_lua((char*)lua + i, LUAMAX, (char*)buf, ptr - buf);
                    *ptr = j;
                }
                buf = ptr;
            } else

            /*** sprites ***/
            if(!memcmp(buf, "__gfx__", 7)) {
                for(buf += 7; *buf == '\r' || *buf == '\n'; buf++);
                if(!gfx) {
                    gfx = (uint8_t*)malloc(8192);
                    if(!gfx) goto err;
                    memset(gfx, 0, 8192);
                    /* one large 128 x 128 x 4 bit sheet, with 8 x 8 pixel sprites */
                    for(i = 0; i < 8192 && *buf && *buf != '_';) {
                        while(*buf == '\r' || *buf == '\n') buf++;
                        if(*buf == '_' || buf[1] == '_') break;
                        /* we just load them here, we convert later when TIC-80 chunk generated
                         * this is little endian! */
                        gfx[i++] = HEX(buf[0]) | (HEX(buf[1]) << 4);
                        buf += 2;
                    }
                    /* the lower part of the map shared with the upper sprites */
                    if(map) memcpy(map + 4096, gfx + 4096, 4096);
                }
            } else

            /*** sprite flags ***/
            if(!memcmp(buf, "__gff__", 7)) {
                for(buf += 7; *buf == '\r' || *buf == '\n'; buf++);
                if(!gff) {
                    gff = (uint8_t*)malloc(256);
                    if(!gff) goto err;
                    memset(gff, 0, 256);
                    for(i = 0; i < 256 && *buf && *buf != '_';) {
                        while(*buf == '\r' || *buf == '\n') buf++;
                        if(*buf == '_' || buf[1] == '_') break;
                        gff[i++] = (HEX(buf[0]) << 4) | HEX(buf[1]);
                        buf += 2;
                    }
                }
            } else

            /*** label (cover image) ***/
            if(!memcmp(buf, "__label__", 9)) {
                for(buf += 9; *buf == '\r' || *buf == '\n'; buf++);
                if(!lbl) {
                    /* screen size is 240 x 136 x 4 bit */
                    lbl = (uint8_t*)malloc(16320);
                    if(!lbl) goto err;
                    memset(lbl, 0, 16320);
                    /* read in 128 x 128 tetrad (64 bytes) and center on screen */
                    for(j = 0; j < 128 && *buf && *buf != '_';)
                        for(i = 0; i < 64 && *buf && *buf != '_';) {
                            while(*buf == '\r' || *buf == '\n') buf++;
                            if(*buf == '_' || buf[1] == '_') break;
                            /* this might also encode g .. v, but we can't store that. Also, little endian */
                            lbl[(j + 4) * 120 + 28 + i] = HEX(buf[0]) | (HEX(buf[1]) << 4);
                            buf += 2;
                        }
                }
            } else

            /*** map ***/
            if(!memcmp(buf, "__map__", 7)) {
                for(buf += 7; *buf == '\r' || *buf == '\n'; buf++);
                if(!map) {
                    map = (uint8_t*)malloc(8192);
                    if(!map) goto err;
                    memset(map, 0, 8192);
                    for(i = 0; i < 4096 && *buf && *buf != '_';) {
                        while(*buf == '\r' || *buf == '\n') buf++;
                        if(*buf == '_' || buf[1] == '_') break;
                        /* 8 bit per map entry, each a sprite id, big endian */
                        map[i++] = (HEX(buf[0]) << 4) | HEX(buf[1]);
                        buf += 2;
                    }
                    /* the lower part of the map shared with the upper sprites */
                    if(gfx) memcpy(map + 4096, gfx + 4096, 4096);
                }
            } else

            /*** music ***/
            if(!memcmp(buf, "__music__", 9)) {
                for(buf += 9; *buf == '\r' || *buf == '\n'; buf++);
                if(!mus) {
                    mus = (uint8_t*)malloc(256);
                    if(!mus) goto err;
                    memset(mus, 0, 256);
                    for(i = 0; i < 256 && *buf && *buf != '_';) {
                        while(*buf == '\r' || *buf == '\n') buf++;
                        if(*buf == '_' || buf[1] == '_') break;
                        /* flags. These are loaded in MSB in memory */
                        f = (HEX(buf[0]) << 4) | HEX(buf[1]);
                        for(buf += 2; *buf == ' '; buf++);
                        for(j = 0; j < 4; j++) {
                            if(*buf == '_' || buf[1] == '_') break;
                            /* big endian data and the MSB flags */
                            mus[i++] = ((HEX(buf[0]) & 7) << 4) | HEX(buf[1]) | (((f >> j) & 1) << 7);
                            buf += 2;
                        }
                    }
                }
            } else

            /*** sound effects ***/
            if(!memcmp(buf, "__sfx__", 7)) {
                for(buf += 7; *buf == '\r' || *buf == '\n'; buf++);
                if(!snd) {
                    snd = (uint8_t*)malloc(4352);
                    if(!snd) goto err;
                    memset(snd, 0, 4352);
                    for(i = 0; i < 4352 && *buf && *buf != '_';) {
                        while(*buf == '\r' || *buf == '\n') buf++;
                        if(*buf == '_' || buf[1] == '_') break;
                        f = (HEX(buf[0]) << 4) | HEX(buf[1]); buf += 2; if(*buf == '_' || buf[1] == '_') break;
                        d = (HEX(buf[0]) << 4) | HEX(buf[1]); buf += 2; if(*buf == '_' || buf[1] == '_') break;
                        s = (HEX(buf[0]) << 4) | HEX(buf[1]); buf += 2; if(*buf == '_' || buf[1] == '_') break;
                        e = (HEX(buf[0]) << 4) | HEX(buf[1]); buf += 2;
                        for(j = 0; j < 32; j++) {
                            if(*buf == '_' || buf[1] == '_' || buf[2] == '_' || buf[3] == '_' || buf[4] == '_') break;
                            /* tetrad 0..1: pitch, tetrad 2: waveform, tetrad 3: volume, tetrad 4: effect */
                            *((uint16_t*)&snd[i]) =
                                ((HEX(buf[1]) << 4) | (HEX(buf[0]) & 0x3f)) |   /* pitch 0..63 */
                                ((HEX(buf[2]) & 7) << 6) |                      /* waveform 0..7, MSB see below */
                                ((HEX(buf[3]) & 7) << 9) |                      /* volume 0..7 */
                                ((HEX(buf[4]) & 7) << 12) |                     /* effect 0..7 */
                                (((HEX(buf[2]) >> 3) & 1) << 15);               /* waveform 4th bit, custom SFX id */
                            i += 2;
                            buf += 5;
                        }
                        snd[i++] = f;   /* flags */
                        snd[i++] = d;   /* we have duration here, but according to the doc this should be speed? */
                        snd[i++] = s;   /* loop start */
                        snd[i++] = e;   /* loop end */
                    }
                }
            } else {
                /* unknown chunk */
                for(ptr = buf; *buf && *buf != '\r' && *buf != '\n'; buf++);
                *buf++ = 0; fprintf(stderr, "p8totic: unknown chunk '%s'\r\n", ptr);
            }
            while(*buf && *buf != '_') buf++;
        }
    } else
    if(!memcmp(buf, "\x89PNG", 4) && (pixels = stbi_load_from_memory((const stbi_uc*)buf, size, &w, &h, &f, 4)) && w > 0 && h > 0) {
        /*** Ooops, this must be a TIC-80 png cartridge. ***/
        if(w == 256 && h == 256) {
            /* first, let's see if it has a cartridge chunk */
            for(raw = buf + 8; raw < buf + size - 12; raw += n + 12) {
                n = ((raw[0] << 24) | (raw[1] << 16) | (raw[2] << 8) | raw[3]);
                if(!memcmp(raw + 4, "caRt", 4)) { raw += 8; goto uncomp; }
            }
            /* nope, fallback to steganography. This code is (mostly) from png_decode() in TIC-80/src/ext/png.c */
            for (i = 0; i < HEADER_SIZE; i++)
                bitcpy(header.data, i * HEADER_BITS, pixels, i << 3, HEADER_BITS);
            if (header.bits > 0 && header.bits <= BITS_IN_BYTE && header.size > 0
              && header.size <= w * h * 4 * header.bits / BITS_IN_BYTE - HEADER_SIZE) {
                n = header.size + ceildiv(header.size * BITS_IN_BYTE % header.bits, BITS_IN_BYTE);
                raw = (uint8_t*)malloc(n);
                if(!raw) goto err;
                for (i = 0, e = ceildiv(header.size * BITS_IN_BYTE, header.bits); i < e; i++)
                    bitcpy(raw, i * header.bits, pixels + HEADER_SIZE, i << 3, header.bits);
uncomp:         free(pixels);
                s = 0; ptr = (uint8_t*)stbi_zlib_decode_malloc_guesssize((const char *)raw, n, 8192, &s);
                if(raw < buf || raw > buf + size) free(raw);
                if(ptr) { if(s > maxlen) { s = maxlen; } memcpy(out, ptr, s); free(ptr); return s; }
            }
            return -1;
        }
        /****** decode binary format ******/
        if(w != 160 || h != 205) {
            free(pixels);
            return -1;
        }
        raw = (uint8_t*)malloc(w * h);
        if(!raw) goto err;
        for(f = 0; f < w * h; f++)
            raw[f] = ((pixels[f * 4 + 0] & 3) << 4) | ((pixels[f * 4 + 1] & 3) << 2) |
                     ((pixels[f * 4 + 2] & 3) << 0) | ((pixels[f * 4 + 3] & 3) << 6);

        /*** label (cover image) ***/
        /* screen size is 240 x 136 x 4 bit */
        lbl = (uint8_t*)malloc(16320);
        if(!lbl) goto err;
        memset(lbl, 0, 16320);
        /* in lack of a saved label, we parse a 128 x 128 area at (16,24) on the png image with true color pixels, where
         * the screenshot should be on the cartridge's picture, trying to match with pico palette to make it a screen */
        for(j = 0; j < 128; j++)
            for(i = 0; i < 64; i++)
                lbl[(j + 4) * 120 + 28 + i] =
                    /* left pixel in lower tetrad */
                    (picopal_idx(pixels[(j + 24) * w * 4 + (i * 2 + 17) * 4 + 0],
                                 pixels[(j + 24) * w * 4 + (i * 2 + 17) * 4 + 1],
                                 pixels[(j + 24) * w * 4 + (i * 2 + 17) * 4 + 2]) << 4) |   /* upper tetrad's pixel */
                     picopal_idx(pixels[(j + 24) * w * 4 + (i * 2 + 16) * 4 + 0],
                                 pixels[(j + 24) * w * 4 + (i * 2 + 16) * 4 + 1],
                                 pixels[(j + 24) * w * 4 + (i * 2 + 16) * 4 + 2]);          /* lower tetrad's pixel */

        /*** sprites ***/
        gfx = (uint8_t*)malloc(8192);
        if(!gfx) goto err;
        /* one large 128 x 128 x 4 bit sheet, with 8 x 8 pixel sprites */
        memcpy(gfx, raw, 8192);

        /*** map ***/
        map = (uint8_t*)malloc(8192);
        if(!map) goto err;
        memcpy(map,        raw + 0x2000, 4096);
        /* the lower part of the map shared with the upper sprites */
        memcpy(map + 4096, raw + 0x1000, 4096);

        /*** sprite flags ***/
        gff = (uint8_t*)malloc(256);
        if(!gff) goto err;
        memcpy(gff, raw + 0x3000, 256);

        /*** music ***/
        mus = (uint8_t*)malloc(256);
        if(!mus) goto err;
        memcpy(mus, raw + 0x3100, 256);

        /*** sound effects ***/
        snd = (uint8_t*)malloc(4352);
        if(!snd) goto err;
        memcpy(snd, raw + 0x3200, 4352);

        /*** lua script ***/
        i = strlen(p8totic_lua);
        lua = (uint8_t*)malloc(LUAMAX + i + 1);
        if(!lua) goto err;
        memset(lua, 0, LUAMAX + i + 1);
        lu2 = (uint8_t*)malloc(LUAMAX);
        if(!lu2) goto err;
        memset(lu2, 0, LUAMAX);
        pico8_code_section_decompress(raw + 0x4300, lua, LUAMAX);
        if(!lua[0]) {
            fprintf(stderr, "p8totic: unable to decompress Lua\r\n");
            free(lua); lua = NULL;
        } else {
            /* convert to utf-8 */
            j = pico_lua_to_utf8(lu2, LUAMAX, lua, strlen((char*)lua));
            memset(lua, 0, LUAMAX + i + 1);
            /* add the Lua helper library */
            memcpy(lua, p8totic_lua, i);
            /* add the inflated, converted Lua code */
            pico_lua_to_tic_lua((char*)lua + i, LUAMAX, (char*)lu2, j);
        }
        free(lu2);
        free(raw);
        free(pixels);
    } else
        return -1;

    /****************** construct TIC-80 cartridge ******************/
    ptr = out;

    /*** CHUNK_SCREEN, cover image in bank 0 ***/
    if(lbl) {
        /* 240 x 136 x 4 bit, we already have copied the 128 x 128 x 4 bit PICO-8 image at the centre */
        TICHDR(18, 16320);
        memcpy(ptr, lbl, 16320);
        ptr += n;
        free(lbl);
    }

    /*** CHUNK_DEFAULT, needed otherwise palette and waveforms not loaded ***/
    TICHDR(17, 0);

    /*** CHUNK_PALETTE, add a fixed PICO-8 palette ***/
    TICHDR(12, 96);
    memcpy(ptr, picopal, 48);       /* SCN palette */
    memcpy(ptr + 48, picopal, 48);  /* OVR palette */
    ptr += n;

    /** CHUNK_WAVEFORM, add fixed PICO-8 waveforms, and generate the rest ***/
    TICHDR(10, 256);
    memcpy(ptr, picowave, 128);
    if(snd) {
        for(i = 0, S = snd; i < 7; i++, S += 68)
            pico_genwave(picowave + 128 + i * 16, (uint16_t*)S, S[64], S[65], S[66], S[67]);
        memcpy(ptr + 128, picowave + 128, 128);
    }
    ptr += n;

    /*** CHUNK_TILES / sprites 0 - 255 ***/
    if(gfx) {
        TICHDR(1, 256 * 32);
        raw = ptr;
        /* unlike PICO-8, the TIC-8 stores the sprites as an array, each 32 bytes, separate 8 x 8 x 4 bit images */
        for(e = 0; e < 256; e++) {                  /* foreach sprite */
            s = 512 * (e >> 4) + 4 * (e & 15);      /* top left pixel on sprite sheet */
            for(j = 0; j < 8; j++, s += 64, raw += 4)/* foreach row 8 */
                memcpy(raw, gfx + s, 4);
        }
        ptr += n;
        free(gfx);
    }

    /*** CHUNK_MAP ***/
    if(map) {
        TICHDR(4, 240 * 136);
        /* PICO-8 map is 128 x 64 x 8 bit, TIC-80 map size is 240 x 136 x 8 bit. Copy to the top left corner */
        for(j = 0; j < 64; j++)
            memcpy(ptr + j * 240, map + j * 128, 128);
        ptr += n;
        free(map);
    }

    /*** CHUNK_FLAGS ***/
    if(gff) {
        /* PICO-8 format: 1 byte per sprite, bit 0: red, bit 1: orange, yellow, green, blue, purple, pink, bit 7: peach */
        TICHDR(6, 512);
        /* FIXME: should we convert these flags? If so, how? https://github.com/nesbox/TIC-80/wiki/fset does not tell */
        memcpy(ptr, gff, 256);
        ptr += n;
        free(gff);
    }

    /*** CHUNK_SAMPLES, sound effects ***/
    if(snd) {
        /* PICO-8 format: 64 samples, each 68 bytes: 32 x 2 byte notes, 1 byte flags, 1 byte speed, 1 byte start, 1 byte end */
        /* for details, see comments in pico_genwave() function above */
        TICHDR(9, 4224);
        /* 64 samples, each 66 bytes */
        /* FIXME: not sure how to store these in TIC-80 */
        for(j = 0; j < 64; j++) {
            S = snd + j * 68; sn = (uint16_t*)S;
            D = ptr + j * 66; dn = (uint16_t*)D;
            for(i = 0; i < 30; i++) {
                dn[i] |= (7 - ((sn[i] >> 9) & 7)) << 1;                     /* volume*2 FIXME: is this really reversed? */
                dn[i] |= (((sn[i] >> 15) << 3) | ((sn[i] >> 6) & 7)) << 4;  /* wave */
                dn[i] |= ((sn[i] >> 0) & 7) << 13;                          /* pitch*2 */
                /* FIXME: what about effect? */
            }
            D[60] |= (S[65] & 7) << 4;              /* speed */
            /* makes no sense. this can store max 15, but we have 30 notes. Let's assume they address every even note */
            e = (S[67] > 30 ? 30 : S[67]) >> 1;     /* loop end */
            s = (S[66] > 30 ? 30 : S[66]) >> 1;     /* loop start */
            d = ((e - s) << 4) | s;                 /* we need start and size */
            D[62] = D[63] = D[64] = D[65] = d;      /* loop for wave, volume, arpeggio, pitch */
        }
        ptr += n;
        free(snd);
    }

    /*** CHUNK_MUSIC ***/
    if(mus) {
        /* PICO-8 format: 64 tracks, each 4 bytes */
        /* one track:
         *  byte 0: bit 7: begin loop
         *          bit 6: channel enabled
         *          bit 0..5: sound id
         *  byte 1: bit 7: end loop
         *          bit 6: channel enabled
         *          bit 0..5: sound id
         *  byte 2: bit 7: stop at end
         *          bit 6: channel enabled
         *          bit 0..5: sound id
         *  byte 3: bit 7: ???
         *          bit 6: channel enabled
         *          bit 0..5: sound id */
        TICHDR(14, 408);
        /* 8 tracks, each 51 bytes */
        /* FIXME: not sure how to store these in TIC-80, do we need an additional CHUNK_PATTERNS (15) too? */
        ptr += n;
        free(mus);
    }

    /*** CHUNK_CODE, this chunk should be the last in the cartridge ***/
    if(lua) {
        s = strlen((const char*)lua) + 1;
        i = 0; j = s / 65535;
        /* write out into 64k banks */
        while(s > 65535) {
            TICHDR((j << 5) | 5, 65535);
            memcpy(ptr, lua + i * 65535, n);
            ptr += n;
            s -= n; i++; j--;
            if(i > 7) {
                fprintf(stderr, "p8totic: too many code banks, only 8 supported\r\n");
                goto err;
            }
        }
        /* remaining */
        if(s > 0) {
            TICHDR(5, s);
            memcpy(ptr, lua + i * 65535, n);
            ptr += n;
        }
        free(lua);
    }

    return ptr - out;
err:
    if(lbl) free(lbl);
    if(lua) free(lua);
    if(gfx) free(gfx);
    if(gff) free(gff);
    if(map) free(map);
    if(mus) free(mus);
    if(snd) free(snd);
    if(raw) free(raw);
    if(pixels) free(pixels);
    return 0;
}

/* things needed for creating a PNG cartridge */
const stbi_uc cartpng[] = {
#include "cart.png.dat"
};
static uint8_t cartfnt[] = {
#include "font.inl"
};
static uint8_t Sweetie16[] = { 0x1a, 0x1c, 0x2c, 0x5d, 0x27, 0x5d, 0xb1, 0x3e, 0x53, 0xef, 0x7d, 0x57, 0xff, 0xcd, 0x75, 0xa7, 0xf0,
 0x70, 0x38, 0xb7, 0x64, 0x25, 0x71, 0x79, 0x29, 0x36, 0x6f, 0x3b, 0x5d, 0xc9, 0x41, 0xa6, 0xf6, 0x73, 0xef, 0xf7, 0xf4, 0xf4, 0xf4,
 0x94, 0xb0, 0xc2, 0x56, 0x6c, 0x86, 0x33, 0x3c, 0x57};
void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);
void drawtext(uint8_t *dst, int dw, int dh, uint32_t c, int x, int y, int w, uint8_t *str)
{
    int i, j, k, p = dw * 4, p2 = 2 * p, s, e;
    uint8_t *fnt, *pix = dst + (y * dw + x) * 4, *row;

    if(!dst || dw < 1 || dh < 1 || x < 0 || y < 0 || w < 1 || !str) return;
    for(; *str >= ' ' && *str < 128 && x < w; str++, x += (k + 1) * 2, pix += (k + 1) * 8) {
        if(*str == ' ') { k = 3; continue; }
        fnt = cartfnt + *str * 8;
        for(i = e = 0, s = 7; i < 8; i++)
            for(j = 0; j < 8; j++) if(fnt[j] & (1 << i)) { if(i < s) { s = i; } if(i > e) { e = i; } }
        k = e - s + 1;
        for(j = 0; j < 8; j++, fnt++)
            for(row = pix + j * p2, i = 0; i < k && x + i + i < w; i++, row += 8)
                if(*fnt & (1 << (s + i))) {
                    *((uint32_t*)row) = *((uint32_t*)(row + 4)) = *((uint32_t*)(row + p)) = *((uint32_t*)(row + p + 4)) = c;
                    *((uint32_t*)(row + p2)) = *((uint32_t*)(row + p2 + 4)) =
                        *((uint32_t*)(row + p2 + p)) = *((uint32_t*)(row + p2 + p + 4)) = 0xff2c1c1a;
                }
    }
}

/**
 * Public API to create a TIC-80 PNG cartridge from a .tic file
 */
int tictopng(uint8_t *buf, int size, uint8_t *out, int maxlen)
{
    Header header = { 0 };
    int w = 0, h = 0, l = 0, f, i, j, s, n;
    uint8_t *ptr, *comp, *pixels = NULL, *raw = NULL, *pal = Sweetie16, *lbl = NULL, *tit = NULL, *ath = NULL;

    if(!buf || size < 1 || !out || maxlen < 1) return 0;
    memset(out, 0, maxlen);

    /* compress .tic */
    comp = stbi_zlib_compress(buf, size, &s, 9);
    if(!comp) return 0;
    comp = (uint8_t*)realloc(comp, s + HEADER_SIZE);
    if(!comp) return 0;

    /* get the cover image background */
    pixels = stbi_load_from_memory(cartpng, sizeof(cartpng), &w, &h, &f, 4);
    header.bits = CLAMP(ceildiv(s * BITS_IN_BYTE, w * h * 4 - HEADER_SIZE), 1, BITS_IN_BYTE); header.size = s;

    /* parse the .tic, look for cover image, palette and cartridge labels */
    tit = memmem(buf, size, " title:", 7); if(tit) for(tit += 7; *tit == ' '; tit++);
    ath = memmem(buf, size, " author:", 8); if(ath) for(ath += 8; *ath == ' '; ath++);
    for(ptr = buf, raw = NULL; ptr < buf + size - 4; ptr += (ptr[1] | (ptr[2] << 8)) + 4)
        switch(ptr[0] & 0x1F) {
            case 12: pal = ptr + 4; break;
            case 18: if(!(ptr[0] >> 5) && !lbl) { lbl = ptr + 4; l = ptr[1] | (ptr[2] << 8); } break;
            case 3:
                raw = stbi_load_from_memory(ptr + 4, ptr[1] | (ptr[2] << 8), &s, &n, &f, 4);
                if(raw) {
                    for(j = 0; j < n; j++)
                        memcpy(pixels + ((j + 8) * w + 8) * 4, raw + j * s * 4, s * 4);
                    free(raw);
                }
            break;
        }
    /* if there was no cover image, but we have a screen chunk, use that */
    if(!raw && lbl)
        for(j = n = 0; j < 136; j++) {
            ptr = pixels + ((j + 8) * w + 8) * 4;
            for(i = 0; i < 120 && n < l; i++, n++, ptr += 8) {
                memcpy(ptr, &pal[(lbl[n] & 0xf) * 3], 3);
                memcpy(ptr + 4, &pal[((lbl[n] >> 4) & 0xf) * 3], 3);
            }
        }

    /* add title and author */
    if(tit) drawtext(pixels, w, h, 0xfff5f4f4, 16, 162, 240, tit);
    if(ath) {
        drawtext(pixels, w, h, 0xff876d56, 16, 186, 240, (uint8_t*)"by");
        drawtext(pixels, w, h, 0xff876d56, 48, 186, 240, ath);
    }

    /* do the steganography. This code is (mostly) from png_encode() in TIC-80/src/ext/png.c */
    for (i = 0; i < HEADER_SIZE; i++)
        bitcpy(pixels, i << 3, header.data, i * HEADER_BITS, HEADER_BITS);
    for(n = ceildiv(header.size * BITS_IN_BYTE, header.bits), i = 0; i < n; i++)
        bitcpy(pixels + HEADER_SIZE, i << 3, comp, i * header.bits, header.bits);

    /* write out png */
    stbi_write_png_compression_level = 9;
    raw = stbi_write_png_to_mem((unsigned char*)pixels, w * 4, w, h, 4, &f, comp, header.size);
    free(pixels);
    if(raw) { if(f > maxlen) { f = maxlen; } memcpy(out, raw, f); free(raw); return f; }
    return 0;
}

#ifndef __EMSCRIPTEN__

/* PICO-8 default waveform generation. */
#ifdef GENWAVEFORM
#include <math.h>
/* Waves from https://github.com/egordorichev/pemsa/blob/master/src/pemsa/audio/pemsa_wave_functions.cpp */
float wave_sine(float t)     { return sinf(t * 2.0 * M_PI + M_PI / 2.0); }
/*float wave_sine(float t)     { return (fabs(fmod(t, 1.0) * 2.0 - 1.0) * 2 - 1.0); }*/
float wave_triangle(float t) { t = fmod(t, 1); return (((t < 0.875) ? (t * 16 / 7) : ((1 - t) * 16)) - 1) * 0.9; }
float wave_sawtooth(float t) { return 2 * (t - (int) (t + 0.5)); }
float wave_square(float t)   { return (wave_sine(t) >= 0 ? 1.0 : -1.0) * 0.5; }
float wave_pulse(float t)    { return (fmod(t, 1) < 0.3125 ? 1 : -1) * 0.7; }
float wave_organ(float t)    { t *= 4; return (fabs(fmod(t, 2) - 1) - 0.5 + (fabs(fmod(t * 0.5, 2) - 1) - 0.5) / 2.0 - 0.1); }
float wave_noise(float t)    { return (float)((((int)t + rand()) & 0xffff) - 32768) / (float)32768.0; }
float wave_phaser(float t)   { t *= 2; return (fabs(fmod(t, 2) - 1) - 0.5f + (fabs(fmod((t * 127 / 128), 2) - 1) - 0.5) / 2) - 0.25; }

typedef float (*wavefunc_t)(float);
void print_wave(wavefunc_t wavefunc, char *comment) {
    int i, n; uint8_t tmp[16] = { 0 };
    printf("    /* check:");
    for(i = 0; i < 32; i++) {
        n = (int)((*wavefunc)((float)i/(float)32) * (float)7.0) + 8;
        tmp[i >> 1] |= (n & 0xF) << ((i & 1) * 4); printf(" %d",n);
    }
    printf(" */\r\n   "); for(i = 0; i < 16; i++) { printf(" 0x%02x,", tmp[i]); } printf(" /* %s */\r\n\r\n", comment);
}
#endif

/**
 * Command line interface
 */
int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *buf = NULL, *out;
    size_t size = 0;
    char *fn = NULL, *c;

    /* parse command line */
    if(argc < 2) {
        printf("p8totic by bzt MIT\r\n\r\n%s <p8|p8.png|tic.png|tic input> [tic|tic.png output]\r\n\r\n", argv[0]);
#ifdef GENWAVEFORM
        print_wave(wave_sine,     "0 - sine");
        print_wave(wave_triangle, "1 - triangle");
        print_wave(wave_sawtooth, "2 - sawtooth");
        print_wave(wave_square,   "3 - square");
        print_wave(wave_pulse,    "4 - short square / pulse");
        print_wave(wave_organ,    "5 - ringing / organ");
        print_wave(wave_noise,    "6 - noise");
        print_wave(wave_phaser,   "7 - ringing sine / phaser");
#endif
        return 1;
    }
    if(argc > 2 && argv[2])
        fn = argv[2];
    else {
        fn = malloc(strlen(argv[1]) + 8);
        if(!fn) { fprintf(stderr, "p8totic: unable to allocate memory\r\n"); exit(1); }
        strcpy(fn, argv[1]);
        c = strrchr(fn, '.'); if(c && !strcmp(c, ".png")) *c = 0;
        c = strrchr(fn, '.'); if(c && !strcmp(c, ".p8")) *c = 0;
        c = strrchr(fn, '.'); if(c && !strcmp(c, ".tic")) *c = 0;
        if(!c) c = fn + strlen(fn);
        strcpy(c, ".tic");
    }

    /* get the image data */
    f = fopen(argv[1], "rb");
    if(f) {
        fseek(f, 0L, SEEK_END);
        size = (int)ftell(f);
        fseek(f, 0L, SEEK_SET);
        buf = (uint8_t*)malloc(size + 1);
        if(!buf) { fprintf(stderr, "p8totic: unable to allocate memory\r\n"); exit(1); }
        memset(buf, 0, size + 1);
        if(fread(buf, 1, size, f) != size) size = 0;
        fclose(f);
    }
    if(!buf || size < 1) {
        fprintf(stderr, "p8topic: unable to read '%s'\r\n", argv[1]);
        exit(1);
    }
    out = (uint8_t*)malloc(1024*1024);
    if(!buf) { fprintf(stderr, "p8totic: unable to allocate memory\r\n"); exit(1); }

    /* do the thing */
    c = strrchr(argv[1], '.');
    if(c && !strcmp(c, ".tic")) {
        if(fn != argv[2]) strcat(fn, ".png");
        size = tictopng(buf, size, out, 1024*1024);
    } else
        size = p8totic(buf, size, out, 1024*1024);
    if(size < 1) {
        fprintf(stderr, "p8topic: unable to generate TIC-80 cartridge\r\n");
        exit(1);
    }
    f = fopen(fn, "wb");
    if(f) {
        fwrite(out, 1, size, f);
        fclose(f);
    } else {
        fprintf(stderr, "p8totic: unable to write '%s'.\r\n", fn);
        exit(1);
    }
    if(fn != argv[2]) free(fn);
    free(out);
    free(buf);
    return 0;
}
#endif
