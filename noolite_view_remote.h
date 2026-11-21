#pragma once

#include <gui/view.h>
#include <stdint.h>

typedef enum {
    NooLiteRemoteActionOn,
    NooLiteRemoteActionOff,
    NooLiteRemoteActionSwitch,
    NooLiteRemoteActionDimUp,
    NooLiteRemoteActionDimDown,
    NooLiteRemoteActionDimStop,
    NooLiteRemoteActionLevelUp,
    NooLiteRemoteActionLevelDown,
    NooLiteRemoteActionMenu,
} NooLiteRemoteAction;

typedef struct NooLiteRemoteView NooLiteRemoteView;
typedef void (*NooLiteRemoteActionCallback)(NooLiteRemoteAction action, void* context);

NooLiteRemoteView* noolite_remote_view_alloc(void);
void noolite_remote_view_free(NooLiteRemoteView* rv);
View* noolite_remote_view_get_view(NooLiteRemoteView* rv);
void noolite_remote_view_set_callback(
    NooLiteRemoteView* rv,
    NooLiteRemoteActionCallback callback,
    void* context);

void noolite_remote_view_set_remote(NooLiteRemoteView* rv, const char* name, uint16_t addr);
void noolite_remote_view_set_level(NooLiteRemoteView* rv, uint8_t level);
void noolite_remote_view_set_status(NooLiteRemoteView* rv, const char* status);
