#include "noolite_radio.h"

#include <furi.h>
#include <stdio.h>
#include <furi_hal.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/subghz_worker.h>

#define TAG           "NooLiteRadio"
#define TX_COPIES     3
#define TX_MAX_PULSES 640
#define TX_TIMEOUT_MS 1500

/* build with -DNOOLITE_DEBUG_PULSES to dump undecoded bursts at debug log level */
#define DEBUG_PULSES 160

struct NooLiteRadio {
    const SubGhzDevice* device;
    uint16_t dbg[DEBUG_PULSES];
    uint16_t dbg_count;
    uint32_t dbg_total;
    SubGhzWorker* worker;
    NooLiteDecoder decoder;
    bool rx;

    NooLiteRadioRxCallback rx_callback;
    void* rx_context;

    NooLitePulse* pulses;
    size_t pulse_count;
    size_t pulse_pos;
};

static void radio_pair_callback(void* context, bool level, uint32_t duration) {
    NooLiteRadio* radio = context;
    NooLitePacket packet;
#ifdef NOOLITE_DEBUG_PULSES
    radio->dbg_total++;
    bool gap = !level && duration > NOOLITE_GAP_MIN;
    if(gap) {
        if(radio->dbg_count >= 40) {
            char line[DEBUG_PULSES * 3 + 1];
            size_t n = 0;
            for(size_t i = 0; i < radio->dbg_count && n + 4 < sizeof(line); i++) {
                n += snprintf(line + n, sizeof(line) - n, "%u ", radio->dbg[i]);
            }
            FURI_LOG_D(TAG, "burst %u pulses (x100us, from high): %s", radio->dbg_count, line);
        }
        radio->dbg_count = 0;
    } else if(radio->dbg_count < DEBUG_PULSES) {
        radio->dbg[radio->dbg_count++] = (duration + 50) / 100;
    }
#endif
    if(noolite_decoder_feed(&radio->decoder, level, duration, &packet)) {
        if(radio->rx_callback) {
            radio->rx_callback(&packet, subghz_devices_get_rssi(radio->device), radio->rx_context);
        }
    }
}

static void radio_overrun_callback(void* context) {
    NooLiteRadio* radio = context;
    noolite_decoder_reset(&radio->decoder);
}

static void radio_configure(NooLiteRadio* radio) {
    subghz_devices_reset(radio->device);
    subghz_devices_idle(radio->device);
    subghz_devices_load_preset(radio->device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_set_frequency(radio->device, NOOLITE_FREQUENCY);
}

NooLiteRadio* noolite_radio_alloc(void) {
    NooLiteRadio* radio = malloc(sizeof(NooLiteRadio));
    memset(radio, 0, sizeof(*radio));

    subghz_devices_init();
    radio->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    furi_check(radio->device);
    subghz_devices_begin(radio->device);
    radio_configure(radio);

    radio->worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(radio->worker, radio_overrun_callback);
    subghz_worker_set_pair_callback(radio->worker, radio_pair_callback);
    subghz_worker_set_context(radio->worker, radio);

    radio->pulses = malloc(sizeof(NooLitePulse) * TX_MAX_PULSES);
    return radio;
}

void noolite_radio_free(NooLiteRadio* radio) {
    noolite_radio_rx_stop(radio);
    subghz_worker_free(radio->worker);
    subghz_devices_sleep(radio->device);
    subghz_devices_end(radio->device);
    subghz_devices_deinit();
    free(radio->pulses);
    free(radio);
}

static void radio_rx_begin(NooLiteRadio* radio) {
    noolite_decoder_reset(&radio->decoder);
    subghz_devices_idle(radio->device);
    subghz_devices_set_frequency(radio->device, NOOLITE_FREQUENCY);
    subghz_devices_flush_rx(radio->device);
    subghz_devices_start_async_rx(radio->device, subghz_worker_rx_callback, radio->worker);
    subghz_worker_start(radio->worker);
}

static void radio_rx_end(NooLiteRadio* radio) {
    if(subghz_worker_is_running(radio->worker)) {
        subghz_worker_stop(radio->worker);
        subghz_devices_stop_async_rx(radio->device);
    }
    subghz_devices_idle(radio->device);
}

void noolite_radio_rx_start(NooLiteRadio* radio, NooLiteRadioRxCallback callback, void* context) {
    if(radio->rx) return;
    radio->rx_callback = callback;
    radio->rx_context = context;
    radio_rx_begin(radio);
    radio->rx = true;
}

void noolite_radio_rx_stop(NooLiteRadio* radio) {
    if(!radio->rx) return;
    radio_rx_end(radio);
    radio->rx = false;
}

bool noolite_radio_is_rx(NooLiteRadio* radio) {
    return radio->rx;
}

static LevelDuration radio_tx_callback(void* context) {
    NooLiteRadio* radio = context;
    if(radio->pulse_pos >= radio->pulse_count) return level_duration_reset();
    NooLitePulse* p = &radio->pulses[radio->pulse_pos++];
    return level_duration_make(p->level, p->duration);
}

bool noolite_radio_tx(NooLiteRadio* radio, const NooLitePacket* packet) {
    radio->pulse_count = noolite_encode_pulses(packet, TX_COPIES, radio->pulses, TX_MAX_PULSES);
    radio->pulse_pos = 0;
    if(!radio->pulse_count) {
        FURI_LOG_E(TAG, "Encode failed");
        return false;
    }

    bool was_rx = radio->rx;
    if(was_rx) radio_rx_end(radio);

    bool ok = false;
    subghz_devices_idle(radio->device);
    subghz_devices_set_frequency(radio->device, NOOLITE_FREQUENCY);
    if(subghz_devices_set_tx(radio->device) &&
       subghz_devices_start_async_tx(radio->device, radio_tx_callback, radio)) {
        uint32_t start = furi_get_tick();
        while(!subghz_devices_is_async_complete_tx(radio->device)) {
            if(furi_get_tick() - start > TX_TIMEOUT_MS) {
                FURI_LOG_E(TAG, "TX timeout");
                break;
            }
            furi_delay_ms(5);
        }
        subghz_devices_stop_async_tx(radio->device);
        ok = true;
    } else {
        FURI_LOG_E(TAG, "TX not allowed");
    }
    subghz_devices_idle(radio->device);

    if(was_rx) radio_rx_begin(radio);
    return ok;
}
