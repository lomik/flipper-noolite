#include "noolite_protocol.h"

#include <string.h>

#define PREAMBLE_PERIODS 41
#define COPY_GAP_US      1500U

/* ------------------------------------------------------------------------ */

uint8_t noolite_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(uint8_t b = 0; b < 8; b++) {
            crc = (crc & 1) ? (uint8_t)((crc >> 1) ^ 0x8C) : (uint8_t)(crc >> 1);
        }
    }
    return crc;
}

int noolite_fmt_args_count(uint8_t fmt) {
    switch(fmt) {
    case 0:
    case 4:
        return 0;
    case 1:
    case 5:
        return 1;
    case 6:
        return 2;
    case 3:
    case 7:
        return 4;
    default:
        return -1;
    }
}

static uint8_t cmd_width(uint8_t fmt) {
    return fmt < 4 ? 4 : 8;
}

static void put_bits(uint8_t* bits, size_t* pos, uint32_t value, uint8_t width) {
    for(uint8_t i = 0; i < width; i++) {
        bits[(*pos)++] = (value >> i) & 1;
    }
}

static uint32_t get_bits(const uint8_t* bits, size_t pos, uint8_t width) {
    uint32_t value = 0;
    for(uint8_t i = 0; i < width; i++) {
        value |= (uint32_t)(bits[pos + i] & 1) << i;
    }
    return value;
}

/* Serialise everything except CRC (start bit included). Returns bit count. */
static size_t packet_body_bits(const NooLitePacket* p, uint8_t* bits) {
    size_t pos = 0;
    bits[pos++] = 1;
    bits[pos++] = p->flip ? 1 : 0;
    put_bits(bits, &pos, p->cmd, cmd_width(p->fmt));
    for(uint8_t i = 0; i < p->args_count; i++) {
        put_bits(bits, &pos, p->args[i], 8);
    }
    put_bits(bits, &pos, p->addr & 0xFF, 8);
    put_bits(bits, &pos, p->addr >> 8, 8);
    put_bits(bits, &pos, p->fmt, 8);
    return pos;
}

/* CRC over bits[1..count-1], front-padded to a byte boundary, packed LSB first. */
static uint8_t bits_crc(const uint8_t* bits, size_t count) {
    uint8_t bytes[(NOOLITE_MAX_PACKET_BITS + 7) / 8] = {0};
    size_t len = count - 1;
    size_t pad = (8 - (len % 8)) % 8;
    for(size_t i = 0; i < len; i++) {
        size_t s = pad + i;
        bytes[s / 8] |= (uint8_t)((bits[1 + i] & 1) << (s % 8));
    }
    return noolite_crc8(bytes, (pad + len) / 8);
}

uint8_t noolite_packet_crc(const NooLitePacket* p) {
    uint8_t bits[NOOLITE_MAX_PACKET_BITS];
    size_t n = packet_body_bits(p, bits);
    return bits_crc(bits, n);
}

size_t noolite_packet_to_bits(const NooLitePacket* p, uint8_t* bits, size_t max_bits) {
    int args = noolite_fmt_args_count(p->fmt);
    if(args < 0 || args != p->args_count) return 0;
    uint8_t tmp[NOOLITE_MAX_PACKET_BITS];
    size_t n = packet_body_bits(p, tmp);
    if(n + 8 > max_bits) return 0;
    uint8_t crc = bits_crc(tmp, n);
    memcpy(bits, tmp, n);
    put_bits(bits, &n, crc, 8);
    return n;
}

bool noolite_packet_from_bits(NooLitePacket* p, const uint8_t* bits, size_t count) {
    if(count < 38 || count > NOOLITE_MAX_PACKET_BITS) return false;
    if(bits[0] != 1) return false;

    uint8_t crc = get_bits(bits, count - 8, 8);
    uint8_t fmt = get_bits(bits, count - 16, 8);
    int args = noolite_fmt_args_count(fmt);
    if(args < 0) return false;
    uint8_t w = cmd_width(fmt);
    if(count != (size_t)(2 + w + args * 8 + 32)) return false;
    if(bits_crc(bits, count - 8) != crc) return false;

    memset(p, 0, sizeof(*p));
    p->flip = bits[1];
    p->cmd = get_bits(bits, 2, w);
    p->fmt = fmt;
    p->args_count = args;
    for(int i = 0; i < args; i++) {
        p->args[i] = get_bits(bits, 2 + w + i * 8, 8);
    }
    uint8_t lo = get_bits(bits, count - 32, 8);
    uint8_t hi = get_bits(bits, count - 24, 8);
    p->addr = ((uint16_t)hi << 8) | lo;
    return true;
}

/* ------------------------------------------------------------------------ */

void noolite_packet_cmd(NooLitePacket* p, uint16_t addr, uint8_t cmd) {
    memset(p, 0, sizeof(*p));
    p->addr = addr;
    p->cmd = cmd;
    p->fmt = cmd >= 16 ? 4 : 0;
}

void noolite_packet_set_level(NooLitePacket* p, uint16_t addr, uint8_t level) {
    memset(p, 0, sizeof(*p));
    p->addr = addr;
    p->cmd = NooLiteCmdSetLevel;
    p->fmt = 1;
    p->args_count = 1;
    p->args[0] = level;
}

void noolite_packet_set_rgb(NooLitePacket* p, uint16_t addr, uint8_t r, uint8_t g, uint8_t b) {
    memset(p, 0, sizeof(*p));
    p->addr = addr;
    p->cmd = NooLiteCmdSetLevel;
    p->fmt = 3;
    p->args_count = 4;
    p->args[0] = r;
    p->args[1] = g;
    p->args[2] = b;
    p->args[3] = 0;
}

void noolite_packet_temporary_on(NooLitePacket* p, uint16_t addr, uint16_t quanta) {
    memset(p, 0, sizeof(*p));
    p->addr = addr;
    p->cmd = NooLiteCmdTemporaryOn;
    if(quanta <= 0xFF) {
        p->fmt = 5;
        p->args_count = 1;
        p->args[0] = quanta;
    } else {
        p->fmt = 6;
        p->args_count = 2;
        p->args[0] = quanta & 0xFF;
        p->args[1] = quanta >> 8;
    }
}

/* ------------------------------------------------------------------------ */

typedef struct {
    NooLitePulse* out;
    size_t max;
    size_t count;
    bool overflow;
} PulseWriter;

static void pw_add(PulseWriter* w, bool level, uint32_t duration) {
    if(w->count && w->out[w->count - 1].level == level) {
        w->out[w->count - 1].duration += duration;
        return;
    }
    if(w->count >= w->max) {
        w->overflow = true;
        return;
    }
    w->out[w->count].level = level;
    w->out[w->count].duration = duration;
    w->count++;
}

size_t noolite_encode_pulses(
    const NooLitePacket* p,
    uint8_t copies,
    NooLitePulse* out,
    size_t max_pulses) {
    uint8_t bits[NOOLITE_MAX_PACKET_BITS];
    size_t n = noolite_packet_to_bits(p, bits, sizeof(bits));
    if(!n || !copies) return 0;

    PulseWriter w = {.out = out, .max = max_pulses};
    for(int i = 0; i < PREAMBLE_PERIODS; i++) {
        pw_add(&w, true, NOOLITE_TE_SHORT);
        pw_add(&w, false, NOOLITE_TE_SHORT);
    }
    pw_add(&w, false, NOOLITE_TE_LONG);

    for(uint8_t c = 0; c < copies; c++) {
        for(size_t i = 0; i < n; i++) {
            /* 1 = low,high ; 0 = high,low */
            pw_add(&w, !bits[i], NOOLITE_TE_SHORT);
            pw_add(&w, bits[i], NOOLITE_TE_SHORT);
        }
        pw_add(&w, false, COPY_GAP_US);
    }
    /* trailing silence so the receiver sees the end of the last copy */
    pw_add(&w, false, COPY_GAP_US);
    return w.overflow ? 0 : w.count;
}

/* ------------------------------------------------------------------------ */

void noolite_decoder_reset(NooLiteDecoder* d) {
    memset(d, 0, sizeof(*d));
}

static void decoder_start(NooLiteDecoder* d) {
    d->active = true;
    d->half_count = 0;
    /* low half of the start bit is hidden inside the gap */
    d->half[d->half_count++] = 0;
}

static bool decoder_finish(NooLiteDecoder* d, NooLitePacket* out) {
    if(d->half_count % 2) d->half[d->half_count++] = 0;
    size_t nbits = d->half_count / 2;
    if(nbits < 38 || nbits > NOOLITE_MAX_PACKET_BITS) return false;
    uint8_t bits[NOOLITE_MAX_PACKET_BITS];
    for(size_t i = 0; i < nbits; i++) {
        uint8_t a = d->half[2 * i], b = d->half[2 * i + 1];
        if(a == 0 && b == 1)
            bits[i] = 1;
        else if(a == 1 && b == 0)
            bits[i] = 0;
        else
            return false;
    }
    return noolite_packet_from_bits(out, bits, nbits);
}

bool noolite_decoder_feed(NooLiteDecoder* d, bool level, uint32_t duration, NooLitePacket* out) {
    bool result = false;

    if(!level && duration > NOOLITE_GAP_MIN) {
        if(d->active) result = decoder_finish(d, out);
        decoder_start(d);
        return result;
    }
    if(!d->active) return false;

    uint8_t n;
    if(duration >= NOOLITE_TE_SHORT / 2 && duration < (NOOLITE_TE_SHORT + NOOLITE_TE_LONG) / 2) {
        n = 1;
    } else if(duration >= (NOOLITE_TE_SHORT + NOOLITE_TE_LONG) / 2 && duration <= NOOLITE_GAP_MIN) {
        n = 2;
    } else {
        d->active = false;
        return false;
    }
    if(d->half_count + n > sizeof(d->half)) {
        d->active = false;
        return false;
    }
    for(uint8_t i = 0; i < n; i++) {
        d->half[d->half_count++] = level ? 1 : 0;
    }
    return false;
}

/* ------------------------------------------------------------------------ */

const char* noolite_cmd_name(uint8_t cmd) {
    switch(cmd) {
    case NooLiteCmdOff:
        return "Off";
    case NooLiteCmdSlowDown:
        return "Dim down";
    case NooLiteCmdOn:
        return "On";
    case NooLiteCmdSlowUp:
        return "Dim up";
    case NooLiteCmdSwitch:
        return "Switch";
    case NooLiteCmdSlowSwitch:
        return "Dim switch";
    case NooLiteCmdSetLevel:
        return "Set level";
    case NooLiteCmdLoadPreset:
        return "Load preset";
    case NooLiteCmdSavePreset:
        return "Save preset";
    case NooLiteCmdUnbind:
        return "Unbind";
    case NooLiteCmdStop:
        return "Stop";
    case NooLiteCmdStepDown:
        return "Step down";
    case NooLiteCmdStepUp:
        return "Step up";
    case NooLiteCmdBrightReg:
        return "Bright reg";
    case NooLiteCmdBind:
        return "Bind";
    case NooLiteCmdRollColor:
        return "Roll color";
    case NooLiteCmdSwitchColor:
        return "Switch color";
    case NooLiteCmdSwitchMode:
        return "Switch mode";
    case NooLiteCmdSwitchSpeed:
        return "Switch speed";
    case NooLiteCmdBatteryLow:
        return "Battery low";
    case NooLiteCmdSensTempHumi:
        return "Sensor";
    case NooLiteCmdShadowLoadPreset:
        return "Shadow preset";
    case NooLiteCmdShadowSetLevel:
        return "Shadow level";
    case NooLiteCmdTemporaryOn:
        return "Temp. on";
    default:
        return "Unknown";
    }
}

const char* noolite_sensor_type_name(uint8_t sensor_type) {
    switch(sensor_type) {
    case 1:
        return "PT112";
    case 2:
        return "PT111";
    default:
        return "Sensor";
    }
}

bool noolite_packet_get_sensor(const NooLitePacket* p, NooLiteSensorData* data) {
    if(p->cmd != NooLiteCmdSensTempHumi || p->fmt != 7 || p->args_count != 4) return false;
    int32_t raw = ((int32_t)(p->args[1] & 0x0F) << 8) | p->args[0];
    if(raw > 0x7FF) raw -= 0x1000;
    data->temperature = raw / 10.0f;
    data->sensor_type = (p->args[1] >> 4) & 0x07;
    data->battery_low = (p->args[1] & 0x80) != 0;
    data->humidity = p->args[2];
    return true;
}

bool noolite_packet_equal(const NooLitePacket* a, const NooLitePacket* b) {
    return a->cmd == b->cmd && a->addr == b->addr && a->fmt == b->fmt &&
           a->args_count == b->args_count && memcmp(a->args, b->args, a->args_count) == 0;
}
