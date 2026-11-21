#pragma once

#include "noolite_protocol.h"

typedef struct NooLiteRadio NooLiteRadio;

/** Called from the radio worker thread for every decoded packet. */
typedef void (*NooLiteRadioRxCallback)(const NooLitePacket* packet, float rssi, void* context);

NooLiteRadio* noolite_radio_alloc(void);
void noolite_radio_free(NooLiteRadio* radio);

void noolite_radio_rx_start(NooLiteRadio* radio, NooLiteRadioRxCallback callback, void* context);
void noolite_radio_rx_stop(NooLiteRadio* radio);
bool noolite_radio_is_rx(NooLiteRadio* radio);

/**
 * Transmit a packet (blocking, ~0.3 s). The flip bit is taken from the packet.
 * RX, if running, is paused and resumed. Returns false if TX is not allowed.
 */
bool noolite_radio_tx(NooLiteRadio* radio, const NooLitePacket* packet);
