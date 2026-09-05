/* tools — bake a tiny deterministic Ogg Opus asset for tests.
 *
 * Encodes a fixed 440 Hz stereo sine with libopus and hand-muxes the
 * Ogg container (OpusHead + OpusTags + audio pages, one packet per
 * page, Ogg CRC32). Build-time tool only - the ENGINE never parses
 * this format at build time beyond decoding via opusfile in tests.
 *
 * Output: a C header `const unsigned char baked_sine_opus[]` + length,
 * written to stdout or the file given as argv[1]. */
#include <opus/opus.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000
#define FRAME 960          /* 20 ms @ 48 kHz */
#define PRESKIP 312        /* standard 48 kHz Opus preskip */
#define TONES 100          /* 100 frames = 2 s of sine + skip padding */

static uint32_t crc_table[256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t r = i << 24;
        for (int k = 0; k < 8; k++)
            r = (r & 0x80000000u) ? (r << 1) ^ 0x04c11db7u : (r << 1);
        crc_table[i] = r;
    }
}
static uint32_t ogg_crc(const uint8_t *d, size_t n) {
    uint32_t c = 0;
    for (size_t i = 0; i < n; i++)
        c = (c << 8) ^ crc_table[(c >> 24) ^ d[i]];
    return c;
}

typedef struct {
    uint8_t *d;
    size_t n, cap;
} buf;
static void bput(buf *b, const void *p, size_t n) {
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->d = realloc(b->d, b->cap);
    }
    memcpy(b->d + b->n, p, n);
    b->n += n;
}

/* one Ogg page, one packet */
static void put_page(buf *o, uint8_t htype, int64_t granule, uint32_t serial,
                     uint32_t seq, const uint8_t *pkt, size_t len) {
    uint8_t hdr[27 + 255];
    size_t segs = (len + 254) / 255;
    if (segs == 0)
        segs = 1;
    memcpy(hdr, "OggS", 4);
    hdr[4] = 0;
    hdr[5] = htype;
    for (int b = 0; b < 8; b++)
        hdr[6 + b] = (uint8_t)((uint64_t)granule >> (8 * b));
    for (int b = 0; b < 4; b++)
        hdr[14 + b] = (uint8_t)(serial >> (8 * b));
    for (int b = 0; b < 4; b++)
        hdr[18 + b] = (uint8_t)(seq >> (8 * b));
    hdr[22] = 0; /* crc placeholder */
    hdr[23] = 0;
    hdr[24] = 0;
    hdr[25] = 0;
    hdr[26] = (uint8_t)segs;
    size_t rem = len, at = 27;
    for (size_t s = 0; s < segs; s++) {
        hdr[at++] = (uint8_t)(rem >= 255 ? 255 : rem);
        rem = rem >= 255 ? rem - 255 : 0;
    }
    size_t body = 27 + (size_t)segs + len;
    uint8_t *page = (uint8_t *)malloc(body);
    memcpy(page, hdr, at);
    memcpy(page + at, pkt, len);
    uint32_t crc = ogg_crc(page, body);
    page[22] = (uint8_t)crc;
    page[23] = (uint8_t)(crc >> 8);
    page[24] = (uint8_t)(crc >> 16);
    page[25] = (uint8_t)(crc >> 24);
    bput(o, page, body);
    free(page);
}

int main(int argc, char **argv) {
    if (argc < 2)
        return 1;
    buf ogg = { 0 };
    crc_init();

    int err = 0;
    OpusEncoder *enc = opus_encoder_create(RATE, 2,
                                           OPUS_APPLICATION_AUDIO, &err);
    if (!enc)
        return 2;
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(32000));

    /* header pages */
    uint8_t head[19];
    memcpy(head, "OpusHead", 8);
    head[8] = 1;                 /* version */
    head[9] = 2;                 /* channels */
    head[10] = PRESKIP & 0xFF;   /* preskip LE */
    head[11] = PRESKIP >> 8;
    head[12] = RATE & 0xFF;      /* original rate (informational) */
    head[13] = (RATE >> 8) & 0xFF;
    head[14] = (RATE >> 16) & 0xFF;
    head[15] = (RATE >> 24) & 0xFF;
    head[16] = head[17] = head[18] = 0; /* gain, mapping family */
    put_page(&ogg, 0x02, 0, 0x414D45u, 0, head, sizeof head);

    uint8_t tags[16];
    memcpy(tags, "OpusTags", 8);
    memset(tags + 8, 0, 8);      /* zero vendor + comment counts */
    put_page(&ogg, 0x00, 0, 0x414D45u, 1, tags, sizeof tags);

    /* audio: preskip silence + TONES sine frames, then preskip tail */
    /* opus_encode only accepts standard durations: pad the tail so
     * every frame is a full 20 ms (extra silence is harmless) */
    int total = PRESKIP + TONES * FRAME + PRESKIP;
    total = (total + FRAME - 1) / FRAME * FRAME;
    int64_t granule = 0;
    uint32_t seq = 2;
    uint8_t pkt[4000];
    int16_t pcm[FRAME * 2];
    for (int pos = 0; pos < total; pos += FRAME) {
        int n = total - pos < FRAME ? total - pos : FRAME;
        for (int i = 0; i < n; i++) {
            double t = (pos + i - PRESKIP) / (double)RATE;
            double s = (pos + i) >= PRESKIP ? 0.5 * sin(2.0 * 3.14159265358979 * 440.0 * t) : 0.0;
            int32_t v = (int32_t)(s * 32767.0);
            pcm[i * 2] = (int16_t)v;
            pcm[i * 2 + 1] = (int16_t)v;
        }
        int len = opus_encode(enc, pcm, n, pkt, sizeof pkt);
        if (len < 0)
            return 3;
        granule += n;
        uint8_t htype = pos + n >= total ? 0x04 : 0x00;
        put_page(&ogg, htype, granule, 0x414D45u, seq++, pkt, (size_t)len);
    }
    opus_encoder_destroy(enc);

    /* emit the C byte-array header */
    FILE *out = fopen(argv[1], "w");
    if (!out)
        return 4;
    fprintf(out, "/* generated by tools/mkopus_sine.c - do not edit.\n"
                 " * 4 s 440 Hz stereo sine @ 48 kHz, 96 kbps, preskip %d. */\n"
                 "const unsigned char baked_sine_opus[] = {\n", PRESKIP);
    for (size_t i = 0; i < ogg.n; i++) {
        fprintf(out, "0x%02x,%s", ogg.d[i], (i % 16 == 15 || i + 1 == ogg.n) ? "\n" : " ");
    }
    fprintf(out, "};\nconst unsigned int baked_sine_opus_len =\n"
                 "    (const unsigned int)sizeof baked_sine_opus;\n");
    fclose(out);
    free(ogg.d);
    return 0;
}
