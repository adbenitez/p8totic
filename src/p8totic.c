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
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_JPEG
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_SIMD
#define STBI_NO_STDIO
#define STBI_ASSERT(x)
#include "stb_image.h"

#include "lua_lib.h"    /* Lua helper, PICO-8 wrapper by musurca */
#include "lua_inf.h"    /* PICO-8 compressed code section inflater by lexaloffle */

#define HEX(a) (a>='0' && a<='9' ? a-'0' : (a>='a' && a<='f' ? a-'a'+10 : (a>='A' && a<='F' ? a-'A'+10 : 0)))
#define TICHDR(h,s) do{\
    if(ptr - out + s > maxlen) goto err;\
    *ptr++ = h; n = s; *ptr++ = n & 0xff; *ptr++ = (n >> 8) & 0xff; *ptr++ = (n >> 16) & 0xff;\
    }while(0);

#define LUAMAX 524288   /* biggest Lua code we can handle */

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
    uint8_t i;
    /* we need to mask the lower 2 bits in each channel before comparing, as those are used to store cartridge data */
    r &= ~3; g &= ~3; b &= ~3;
    for(i = 0; i < 16; i++)
        if((picopal[i * 3] & ~3) == r && (picopal[i * 3 + 1] & ~3) == g && (picopal[i * 3 + 2] & ~3) == b)
            return i;
    return 0;
}

/**
 * Lua syntax converter
 *   src is zero terminated (but you can also use srclen)
 *   dst is at least 512k (but use maxlen)
 */
static int pico_lua_to_tic_lua(char *dst, int maxlen, char *src, int srclen)
{
    int len;

    /* FIXME: if there's any syntax difference between PICO-8 and TIC-80, replace strings here */
    memcpy(dst, src, srclen);

    len = srclen;
    dst[len] = 0;
    return len;
}

/**
 * Public API function to convert cartridges
 */
int p8totic(uint8_t *buf, int size, uint8_t *out, int maxlen)
{
    int w = 0, h = 0, f, i, j, d, s, e, n;
    unsigned long dstSize;
    uint8_t *ptr, *pixels = NULL, *raw = NULL, *lua = NULL, *lu2 = NULL, *lbl = NULL;
    uint8_t *gfx = NULL, *gff = NULL, *map = NULL, *mus = NULL, *snd = NULL;

    if(!buf || size < 1 || !out || maxlen < LUAMAX) return 0;
    memset(out, 0, maxlen);

    /*** parse PICO-8 cartridge ***/
    if(!memcmp(buf, "pico-8", 6)) {
        /* decode textual format */
        for(; *buf && (buf[0] != '_' || buf[1] != '_'); buf++);
        if(!buf[0]) return -1;
        while(*buf) {
            /* lua script */
            if(!memcmp(buf, "__lua__", 7)) {
                for(buf += 7; *buf == '\r' || *buf == '\n'; buf++);
                for(ptr = buf; *ptr && memcmp(ptr - 1, "\n__", 3); ptr++);
                if(!lua) {
                    i = strlen(p8totic_lua);
                    lua = (uint8_t*)malloc(LUAMAX + i + 1);
                    if(!lua) goto err;
                    j = *ptr; *ptr = 0;
                    memcpy(lua, p8totic_lua, i);
                    pico_lua_to_tic_lua((char*)lua + i, LUAMAX, (char*)buf, ptr - buf);
                    *ptr = j;
                }
                buf = ptr;
            } else
            /* sprites */
            if(!memcmp(buf, "__gfx__", 7)) {
                for(buf += 7; *buf == '\r' || *buf == '\n'; buf++);
                if(!gfx) {
                    gfx = (uint8_t*)malloc(8192);
                    if(!gfx) goto err;
                    memset(gfx, 0, 8192);
                    for(i = 0; i < 8192 && *buf && *buf != '_';) {
                        while(*buf == '\r' || *buf == '\n') buf++;
                        if(*buf == '_' || buf[1] == '_') break;
                        gfx[i++] = HEX(buf[0]) | (HEX(buf[1]) << 4);
                        buf += 2;
                    }
                    if(map) memcpy(map + 4096, gfx + 4096, 4096);
                }
            } else
            /* sprite flags */
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
            /* label (cover image) */
            if(!memcmp(buf, "__label__", 9)) {
                for(buf += 9; *buf == '\r' || *buf == '\n'; buf++);
                if(!lbl) {
                    lbl = (uint8_t*)malloc(16320);
                    if(!lbl) goto err;
                    memset(lbl, 0, 16320);
                    for(j = 0; j < 128 && *buf && *buf != '_';)
                        for(i = 0; i < 64 && *buf && *buf != '_';) {
                            while(*buf == '\r' || *buf == '\n') buf++;
                            if(*buf == '_' || buf[1] == '_') break;
                            /* this might also encode g .. v, but we can't store that */
                            lbl[(j + 4) * 120 + 28 + i] = HEX(buf[0]) | (HEX(buf[1]) << 4);
                            buf += 2;
                        }
                }
            } else
            /* map */
            if(!memcmp(buf, "__map__", 7)) {
                for(buf += 7; *buf == '\r' || *buf == '\n'; buf++);
                if(!map) {
                    map = (uint8_t*)malloc(8192);
                    if(!map) goto err;
                    memset(map, 0, 8192);
                    for(i = 0; i < 4096 && *buf && *buf != '_';) {
                        while(*buf == '\r' || *buf == '\n') buf++;
                        if(*buf == '_' || buf[1] == '_') break;
                        map[i++] = (HEX(buf[0]) << 4) | HEX(buf[1]);
                        buf += 2;
                    }
                    if(gfx) memcpy(map + 4096, gfx + 4096, 4096);
                }
            } else
            /* music */
            if(!memcmp(buf, "__music__", 9)) {
                for(buf += 9; *buf == '\r' || *buf == '\n'; buf++);
                if(!mus) {
                    mus = (uint8_t*)malloc(256);
                    if(!mus) goto err;
                    memset(mus, 0, 256);
                    for(i = 0; i < 256 && *buf && *buf != '_';) {
                        while(*buf == '\r' || *buf == '\n') buf++;
                        if(*buf == '_' || buf[1] == '_') break;
                        f = (HEX(buf[0]) << 4) | HEX(buf[1]);
                        for(buf += 2; *buf == ' '; buf++);
                        for(j = 0; j < 4; j++) {
                            if(*buf == '_' || buf[1] == '_') break;
                            mus[i++] = (HEX(buf[0]) << 4) | HEX(buf[1]) | (((f >> j) & 1) << 7);
                            buf += 2;
                        }
                    }
                }
            } else
            /* sound effects */
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
                            /* FIXME: according to the doc, https://pico-8.fandom.com/wiki/Memory#Sound_effects,
                             * these 20 bit blocks should be stored in 16 bits somehow... */
                            n = (HEX(buf[0]) << 16) | (HEX(buf[1]) << 12) | (HEX(buf[2]) << 8) | (HEX(buf[3]) << 4) | HEX(buf[4]);
                            mus[i++] = n & 0xff;
                            mus[i++] = (n >> 8) & 0xff;
                            buf += 5;
                        }
                        mus[i++] = f;   /* flags */
                        mus[i++] = d;   /* we have duration here, but according to the doc this should be speed? */
                        mus[i++] = s;   /* loop start */
                        mus[i++] = e;   /* loop end */
                    }
                }
            } else {
                /* unknown chunk */
                for(ptr = buf; *buf && *buf != '\r' && *buf != '\n'; buf++);
                *buf = 0; fprintf(stderr, "p8totic: unknown chunk '%s'\r\n", ptr);
            }
            while(*buf && *buf != '_') buf++;
        }
    } else
    if(!memcmp(buf, "\x89PNG", 4) && (pixels = stbi_load_from_memory((const stbi_uc*)buf, size, &w, &h, &f, 4)) && w > 0 && h > 0) {
        /* decode binary format */
        raw = (uint8_t*)malloc(w * h);
        if(!raw) goto err;
        for(f = 0; f < w * h; f++)
            raw[f] = ((pixels[f * 4 + 0] & 3) << 4) | ((pixels[f * 4 + 1] & 3) << 2) |
                     ((pixels[f * 4 + 2] & 3) << 0) | ((pixels[f * 4 + 3] & 3) << 6);
        /* label (cover image) */
        lbl = (uint8_t*)malloc(16320);
        if(!lbl) goto err;
        memset(lbl, 0, 16320);
        /* in lack of a true label, we parse a 128 x 128 area at (16,24) on the png image, where the screenshot
         * should be on the cartridge image, trying to match truecolor pixels with pico palette */
        for(j = 0; j < 128; j++)
            for(i = 0; i < 64; i++)
                lbl[(j + 4) * 120 + 28 + i] =
                    (picopal_idx(pixels[(j + 24) * w * 4 + (i * 2 + 17) * 4 + 0],
                                 pixels[(j + 24) * w * 4 + (i * 2 + 17) * 4 + 1],
                                 pixels[(j + 24) * w * 4 + (i * 2 + 17) * 4 + 2]) << 4) |
                     picopal_idx(pixels[(j + 24) * w * 4 + (i * 2 + 16) * 4 + 0],
                                 pixels[(j + 24) * w * 4 + (i * 2 + 16) * 4 + 1],
                                 pixels[(j + 24) * w * 4 + (i * 2 + 16) * 4 + 2]);
        /* sprites */
        gfx = (uint8_t*)malloc(8192);
        if(!gfx) goto err;
        memcpy(gfx, raw, 8192);
        /* map */
        map = (uint8_t*)malloc(8192);
        if(!map) goto err;
        memcpy(map,        raw + 0x2000, 4096);
        memcpy(map + 4096, raw + 0x1000, 4096);
        /* sprite flags */
        gff = (uint8_t*)malloc(256);
        if(!gff) goto err;
        memcpy(gff, raw + 0x3000, 256);
        /* music */
        mus = (uint8_t*)malloc(256);
        if(!mus) goto err;
        memcpy(mus, raw + 0x3100, 256);
        /* sound effects */
        snd = (uint8_t*)malloc(4352);
        if(!snd) goto err;
        memcpy(snd, raw + 0x3200, 4352);
        /* lua script */
        i = strlen(p8totic_lua);
        lua = (uint8_t*)malloc(LUAMAX + i + 1);
        if(!lua) goto err;
        memset(lua, 0, LUAMAX + i + 1);
        lu2 = (uint8_t*)malloc(LUAMAX);
        if(!lu2) goto err;
        memset(lu2, 0, LUAMAX);
        if(!pico8_code_section_decompress(raw + 0x4300, lu2, LUAMAX)) { free(lua); free(raw); free(pixels); return -1; }
        memcpy(lua, p8totic_lua, i);
        pico_lua_to_tic_lua((char*)lua + i, LUAMAX, (char*)lu2, strlen((char*)lu2));
        free(lu2);
        free(raw);
        free(pixels);
    } else
        return -1;

    /*** construct TIC-80 cartridge ***/
    ptr = out;
    /* CHUNK_SCREEN, cover image in bank 0 */
    if(lbl) {
        TICHDR(18, 16320);
        memcpy(ptr, lbl, 16320);
        ptr += n;
        free(lbl);
    }
    /* CHUNK_TILES / sprites 0 - 255 */
    if(gfx) {
        TICHDR(1, 256 * 32);
        raw = ptr;
        for(e = 0; e < 256; e++) {
            s = 512 * (e >> 4) + 4 * (e & 15);
            for(j = 0; j < 8; j++, s += 64)
                for(i = 0; i < 4; i++, raw++)
                    *raw = (gfx[s + i] >> 4) | ((gfx[s + i] & 0xf) << 4);
        }
        ptr += n;
        free(gfx);
    }
    /* CHUNK_MAP */
    if(map) {
        TICHDR(4, 240 * 136);
        for(j = 0; j < 64; j++)
            memcpy(ptr + j * 240, map + j * 128, 128);
        ptr += n;
        free(map);
    }
    /* CHUNK_CODE */
    if(lua) {
        s = strlen((const char*)lua) + 1;
        i = 0;
        /* write out into banks */
        while(s > 65535) {
            TICHDR((i << 5) | 5, 65535);
            memcpy(ptr, lua + i * 65536, n);
            ptr += n;
            s -= n; i++;
            if(i > 7) goto err;
        }
        /* remaining */
        if(s > 0) {
            TICHDR((i << 5) | 5, s);
            memcpy(ptr, lua + i * 65536, n);
            ptr += n;
        }
        free(lua);
    }
    /* CHUNK_FLAGS */
    if(gff) {
        /* FIXME: should we convert these flags? https://github.com/nesbox/TIC-80/wiki/fset does not tell */
        TICHDR(6, 512);
        memcpy(ptr, gff, 256);
        ptr += n;
        free(gff);
    }
    /* CHUNK_SAMPLES, sound effects */
    if(snd) {
        /* FIXME: not sure how to store these in TIC-80, plus text pico-8 import isn't working yet, just the binary */
        free(snd);
    }
    /* CHUNK_PALETTE, add a fixed PICO-8 palette */
    TICHDR(12, 96);
    memcpy(ptr, picopal, 48);       /* SCN palette */
    memcpy(ptr + 48, picopal, 48);  /* OVR palette */
    ptr += n;
    /* CHUNK_MUSIC */
    if(mus) {
        /* FIXME: not sure how to store these in TIC-80, do we need an additional CHUNK_PATTERNS too? */
        free(mus);
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

#ifndef __EMSCRIPTEN__
/**
 * Command line interface
 */
int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *buf = NULL, *out;
    int i, size = 0;
    char *fn = NULL, *c;

    /* parse command line */
    if(argc < 2) {
        printf("p8totic by bzt MIT\r\n\r\n%s <p8|p8.png input> [tic output]\r\n\r\n", argv[0]);
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
        i = fread(buf, 1, size, f);
        fclose(f);
    }
    if(!buf || size < 1 || (memcmp(buf, "pico-8", 6) && memcmp(buf, "\x89PNG", 4))) {
        fprintf(stderr, "p8topic: unable to read '%s' or it is not a PICO-8 cartridge\r\n", argv[1]);
        exit(1);
    }
    out = (uint8_t*)malloc(1024*1024);
    if(!buf) { fprintf(stderr, "p8totic: unable to allocate memory\r\n"); exit(1); }

    /* do the thing */
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
