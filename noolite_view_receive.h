#pragma once

#include <gui/view.h>
#include "noolite_protocol.h"

typedef struct NooLiteReceiveView NooLiteReceiveView;

/** OK pressed on an entry: address of that entry. */
typedef void (*NooLiteReceiveSelectCallback)(uint16_t addr, void* context);

NooLiteReceiveView* noolite_receive_view_alloc(void);
void noolite_receive_view_free(NooLiteReceiveView* rv);
View* noolite_receive_view_get_view(NooLiteReceiveView* rv);

void noolite_receive_view_set_callback(
    NooLiteReceiveView* rv,
    NooLiteReceiveSelectCallback callback,
    void* context);

/** Thread safe; returns true if this is a new press (not a repeated copy). */
bool noolite_receive_view_add(NooLiteReceiveView* rv, const NooLitePacket* packet, float rssi);
void noolite_receive_view_clear(NooLiteReceiveView* rv);
