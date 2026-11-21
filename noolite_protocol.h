#pragma once

/*
 * nooLite (classic, 433.92 MHz OOK) protocol codec.
 *
 * Pure C, no Flipper dependencies, so it can be unit-tested on the host.
 *
 * Over the air (all fields LSB first):
 *   start(1)=1 | flip(1) | cmd(4 if fmt<4 else 8) | args(0..4 bytes) |
 *   addr_lo(8) | addr_hi(8) | fmt(8) | crc8(8)
 *
 * Manchester, half-bit 500 us: data 1 = low,high; data 0 = high,low.
 * CRC-8/MAXIM (poly 0x31 reflected, init 0) over every bit after the start bit
 * and before the CRC, zero-padded at the front to a byte boundary and packed
 * LSB first.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOOLITE_FREQUENCY       433920000UL
#define NOOLITE_TE_SHORT        500U /* half-bit, us */
#define NOOLITE_TE_LONG         1000U
#define NOOLITE_GAP_MIN         1300U /* low longer than this ends a packet */
#define NOOLITE_MAX_ARGS        4
#define NOOLITE_MAX_PACKET_BITS (2 + 8 + NOOLITE_MAX_ARGS * 8 + 32)

typedef enum {
    NooLiteCmdOff = 0,
    NooLiteCmdSlowDown = 1,
    NooLiteCmdOn = 2,
    NooLiteCmdSlowUp = 3,
    NooLiteCmdSwitch = 4,
    NooLiteCmdSlowSwitch = 5,
    NooLiteCmdSetLevel = 6,
    NooLiteCmdLoadPreset = 7,
    NooLiteCmdSavePreset = 8,
    NooLiteCmdUnbind = 9,
    NooLiteCmdStop = 10,
    NooLiteCmdStepDown = 11,
    NooLiteCmdStepUp = 12,
    NooLiteCmdBrightReg = 13,
    NooLiteCmdBind = 15,
    NooLiteCmdRollColor = 16,
    NooLiteCmdSwitchColor = 17,
    NooLiteCmdSwitchMode = 18,
    NooLiteCmdSwitchSpeed = 19,
    NooLiteCmdBatteryLow = 20,
    NooLiteCmdSensTempHumi = 21,
    NooLiteCmdShadowLoadPreset = 23,
    NooLiteCmdShadowSetLevel = 24,
    NooLiteCmdTemporaryOn = 25,
} NooLiteCmd;

typedef struct {
    bool flip;
    uint8_t cmd;
    uint16_t addr;
    uint8_t fmt;
    uint8_t args_count;
    uint8_t args[NOOLITE_MAX_ARGS];
} NooLitePacket;

typedef struct {
    float temperature; /* deg C */
    uint8_t humidity; /* %, 0 if sensor has none */
    bool battery_low;
    uint8_t sensor_type; /* 3-bit field from the packet */
} NooLiteSensorData;

/* --- helpers to build packets ------------------------------------------ */

/** Simple command without arguments (fmt 0, or fmt 4 for cmd >= 16). */
void noolite_packet_cmd(NooLitePacket* p, uint16_t addr, uint8_t cmd);
/** Brightness 0..255 (fmt 1). */
void noolite_packet_set_level(NooLitePacket* p, uint16_t addr, uint8_t level);
/** RGB colour (fmt 3). */
void noolite_packet_set_rgb(NooLitePacket* p, uint16_t addr, uint8_t r, uint8_t g, uint8_t b);
/** Temporary on, timeout in 5 s units (fmt 5 or 6). */
void noolite_packet_temporary_on(NooLitePacket* p, uint16_t addr, uint16_t quanta);

/** Number of argument bytes for a format, or -1 if the format is unknown. */
int noolite_fmt_args_count(uint8_t fmt);

/* --- bit level ----------------------------------------------------------- */

uint8_t noolite_crc8(const uint8_t* data, size_t len);
uint8_t noolite_packet_crc(const NooLitePacket* p);

/** Serialise packet to data bits (one bit per byte, 0/1). Returns bit count or 0. */
size_t noolite_packet_to_bits(const NooLitePacket* p, uint8_t* bits, size_t max_bits);

/** Parse data bits; checks length, format and CRC. */
bool noolite_packet_from_bits(NooLitePacket* p, const uint8_t* bits, size_t count);

/* --- radio level --------------------------------------------------------- */

typedef struct {
    bool level;
    uint32_t duration;
} NooLitePulse;

/**
 * Build the complete transmission: preamble, packet, gap, packet ... (copies times).
 * Returns pulse count or 0 if buffer is too small.
 */
size_t noolite_encode_pulses(
    const NooLitePacket* p,
    uint8_t copies,
    NooLitePulse* out,
    size_t max_pulses);

typedef struct {
    uint8_t half[2 * NOOLITE_MAX_PACKET_BITS + 4];
    size_t half_count;
    bool active;
    bool prev_level;
} NooLiteDecoder;

void noolite_decoder_reset(NooLiteDecoder* d);

/** Feed one level/duration pair. Returns true when a valid packet was decoded into out. */
bool noolite_decoder_feed(NooLiteDecoder* d, bool level, uint32_t duration, NooLitePacket* out);

/* --- presentation -------------------------------------------------------- */

const char* noolite_cmd_name(uint8_t cmd);
const char* noolite_sensor_type_name(uint8_t sensor_type);
bool noolite_packet_get_sensor(const NooLitePacket* p, NooLiteSensorData* data);
/** Equal ignoring flip (used for repeat suppression with flip compare separately). */
bool noolite_packet_equal(const NooLitePacket* a, const NooLitePacket* b);
