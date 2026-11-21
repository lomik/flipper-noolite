/* Host unit tests for the nooLite codec: `make -C tests` */
#include "../noolite_protocol.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if(!(cond)) {                                                \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                              \
        }                                                            \
    } while(0)

/* Real packets captured by Wiren Board (contactless/rfm69-linux noolite.py) */
typedef struct {
    const char* name;
    const char* bits;
    int flip, cmd, addr, fmt, nargs;
    int args[4];
} Vector;

static const Vector vectors[] = {
    {"rgb 1,1,1",
     "110110 10000000 10000000 10000000 00000000 10011111 10100100 11000000 11001011",
     1,
     6,
     0x25F9,
     3,
     4,
     {1, 1, 1, 0}},
    {"rgb 1,1,2",
     "100110 10000000 10000000 01000000 00000000 10011111 10100100 11000000 11101101",
     0,
     6,
     0x25F9,
     3,
     4,
     {1, 1, 2, 0}},
    {"rgb 255",
     "110110 11111111 11111111 11111111 00000000 10011111 10100100 11000000 10110001",
     1,
     6,
     0x25F9,
     3,
     4,
     {255, 255, 255, 0}},
    {"rgb ch14",
     "110110 10000000 10000000 01000000 00000000 11111111 10100100 11000000 00110010",
     1,
     6,
     0x25FF,
     3,
     4,
     {1, 1, 2, 0}},
    {"rgb ch14 f0",
     "100110 10000000 10000000 01000000 00000000 11111111 10100100 11000000 01100110",
     0,
     6,
     0x25FF,
     3,
     4,
     {1, 1, 2, 0}},
    {"switch mode", "11 01001000 10011111 10100100 00100000 00010101", 1, 18, 0x25F9, 4, 0, {0}},
    {"switch color", "11 10001000 10011111 10100100 00100000 00000100", 1, 17, 0x25F9, 4, 0, {0}},
    {"level 46", "110110 01110100 10011111 10100100 10000000 10010100", 1, 6, 0x25F9, 1, 1, {46}},
    {"stop", "110101 10011111 10100100 00000000 00010001", 1, 10, 0x25F9, 0, 0, {0}},
    {"off", "110000 10011111 10100100 00000000 10000100", 1, 0, 0x25F9, 0, 0, {0}},
    {"shadow level",
     "11 00011000 00100110 10100000 01000100 10100000 00110110",
     1,
     24,
     0x2205,
     5,
     1,
     {100}},
    {"temporary on",
     "11 10011000 10000000 10011000 10100000 01000100 01100000 00100001",
     1,
     25,
     0x2205,
     6,
     2,
     {1, 25}},
    {"sensor",
     "10 10101000 11100000 10000101 00100110 11111111 11111001 00101000 11100000 00111000",
     0,
     21,
     0x149F,
     7,
     4,
     {7, 161, 100, 255}},
};

static size_t parse_bits(const char* s, uint8_t* bits) {
    size_t n = 0;
    for(; *s; s++) {
        if(*s == '0' || *s == '1') bits[n++] = *s - '0';
    }
    return n;
}

static void test_vector(const Vector* v) {
    printf("%s\n", v->name);
    uint8_t bits[NOOLITE_MAX_PACKET_BITS];
    size_t n = parse_bits(v->bits, bits);

    NooLitePacket p;
    bool ok = noolite_packet_from_bits(&p, bits, n);
    CHECK(ok);
    if(!ok) return;
    CHECK(p.flip == v->flip);
    CHECK(p.cmd == v->cmd);
    CHECK(p.addr == v->addr);
    CHECK(p.fmt == v->fmt);
    CHECK(p.args_count == v->nargs);
    for(int i = 0; i < v->nargs; i++)
        CHECK(p.args[i] == v->args[i]);

    /* serialise back, must be bit-identical */
    uint8_t out[NOOLITE_MAX_PACKET_BITS];
    size_t m = noolite_packet_to_bits(&p, out, sizeof(out));
    CHECK(m == n);
    CHECK(memcmp(out, bits, n) == 0);

    /* corrupt one bit -> CRC must reject */
    bits[n / 2] ^= 1;
    NooLitePacket bad;
    CHECK(!noolite_packet_from_bits(&bad, bits, n));

    /* through the air: pulses -> decoder */
    NooLitePulse pulses[512];
    size_t pc = noolite_encode_pulses(&p, 2, pulses, 512);
    CHECK(pc > 0);
    NooLiteDecoder d;
    noolite_decoder_reset(&d);
    int decoded = 0;
    for(size_t i = 0; i < pc; i++) {
        NooLitePacket r;
        /* +-10% jitter */
        uint32_t dur = pulses[i].duration;
        dur = (i % 3 == 0) ? dur * 9 / 10 : (i % 3 == 1) ? dur * 11 / 10 : dur;
        if(noolite_decoder_feed(&d, pulses[i].level, dur, &r)) {
            CHECK(noolite_packet_equal(&r, &p));
            CHECK(r.flip == p.flip);
            decoded++;
        }
    }
    CHECK(decoded == 2);
}

static void test_builders(void) {
    printf("builders\n");
    NooLitePacket p, q;
    uint8_t bits[NOOLITE_MAX_PACKET_BITS];

    noolite_packet_cmd(&p, 0x25F9, NooLiteCmdOff);
    p.flip = 1;
    size_t n = noolite_packet_to_bits(&p, bits, sizeof(bits));
    uint8_t ref[NOOLITE_MAX_PACKET_BITS];
    size_t rn = parse_bits(vectors[9].bits, ref);
    CHECK(n == rn && memcmp(bits, ref, n) == 0);

    noolite_packet_set_level(&p, 0x25F9, 46);
    p.flip = 1;
    n = noolite_packet_to_bits(&p, bits, sizeof(bits));
    rn = parse_bits(vectors[7].bits, ref);
    CHECK(n == rn && memcmp(bits, ref, n) == 0);

    noolite_packet_temporary_on(&p, 0x2205, 25 * 256 + 1);
    p.flip = 1;
    n = noolite_packet_to_bits(&p, bits, sizeof(bits));
    rn = parse_bits(vectors[11].bits, ref);
    CHECK(n == rn && memcmp(bits, ref, n) == 0);

    noolite_packet_cmd(&p, 0x25F9, NooLiteCmdSwitchMode);
    p.flip = 1;
    n = noolite_packet_to_bits(&p, bits, sizeof(bits));
    rn = parse_bits(vectors[5].bits, ref);
    CHECK(n == rn && memcmp(bits, ref, n) == 0);

    noolite_packet_set_rgb(&p, 0x25F9, 1, 1, 1);
    p.flip = 1;
    n = noolite_packet_to_bits(&p, bits, sizeof(bits));
    CHECK(noolite_packet_from_bits(&q, bits, n));
    CHECK(noolite_packet_equal(&p, &q));
}

static void test_sensor(void) {
    printf("sensor data\n");
    uint8_t bits[NOOLITE_MAX_PACKET_BITS];
    size_t n = parse_bits(vectors[12].bits, bits);
    NooLitePacket p;
    CHECK(noolite_packet_from_bits(&p, bits, n));
    NooLiteSensorData s;
    CHECK(noolite_packet_get_sensor(&p, &s));
    /* raw temp = 0x107 = 263 -> 26.3 C, type 0b010, humidity 100 */
    CHECK(s.temperature > 26.25f && s.temperature < 26.35f);
    CHECK(s.sensor_type == 2);
    CHECK(s.humidity == 100);
    CHECK(s.battery_low); /* bit 7 of args[1] is set in this capture */

    p.args[0] = 0xF6;
    p.args[1] = 0x2F; /* raw 0xFF6 = -10 -> -1.0 C */
    CHECK(noolite_packet_get_sensor(&p, &s));
    CHECK(s.temperature < -0.95f && s.temperature > -1.05f);
}

static void test_noise(void) {
    printf("noise rejection\n");
    NooLiteDecoder d;
    noolite_decoder_reset(&d);
    uint32_t seed = 12345;
    int false_hits = 0;
    for(int i = 0; i < 200000; i++) {
        seed = seed * 1103515245 + 12345;
        uint32_t dur = 200 + (seed >> 16) % 2000;
        NooLitePacket r;
        if(noolite_decoder_feed(&d, i & 1, dur, &r)) false_hits++;
    }
    CHECK(false_hits == 0);
}

int main(void) {
    for(size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++)
        test_vector(&vectors[i]);
    test_builders();
    test_sensor();
    test_noise();
    printf(failures ? "\n%d FAILURES\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}
