#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>

#include "noolite_protocol.h"
#include "noolite_radio.h"
#include "noolite_view_receive.h"
#include "noolite_view_remote.h"

#define NOOLITE_MAX_REMOTES  32
#define NOOLITE_NAME_LEN     24
#define NOOLITE_REMOTES_FILE APP_DATA_PATH("remotes.txt")

typedef struct {
    char name[NOOLITE_NAME_LEN];
    uint16_t addr;
} NooLiteRemote;

typedef enum {
    NooLiteViewSubmenu,
    NooLiteViewTextInput,
    NooLiteViewByteInput,
    NooLiteViewVarList,
    NooLiteViewWidget,
    NooLiteViewReceive,
    NooLiteViewRemote,
} NooLiteView;

typedef enum {
    NooLiteSceneStart,
    NooLiteSceneReceive,
    NooLiteSceneRemoteList,
    NooLiteSceneRemoteAddr,
    NooLiteSceneRemoteName,
    NooLiteSceneRemoteControl,
    NooLiteSceneRemoteMenu,
    NooLiteSceneSetLevel,
    NooLiteSceneRgb,
    NooLiteSceneTimer,
    NooLiteSceneAbout,
    NooLiteSceneNum,
} NooLiteScene;

/* custom events */
enum {
    NooLiteEventTextDone = 1000,
    NooLiteEventByteDone,
    NooLiteEventReceiveSave,
    NooLiteEventSendBase = 2000, /* + NooLiteRemoteAction */
};

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    TextInput* text_input;
    ByteInput* byte_input;
    VariableItemList* var_list;
    Widget* widget;
    NooLiteReceiveView* receive_view;
    NooLiteRemoteView* remote_view;

    NooLiteRadio* radio;
    bool flip;

    NooLiteRemote remotes[NOOLITE_MAX_REMOTES];
    size_t remote_count;
    int32_t current; /* index in remotes, -1 = new */

    /* edit buffers */
    char name_buf[NOOLITE_NAME_LEN];
    uint8_t addr_buf[2];
    uint8_t level;
    uint8_t rgb[3];
    uint8_t timer_index;
} NooLiteApp;

extern const SceneManagerHandlers noolite_scene_handlers;

bool noolite_remotes_load(NooLiteApp* app);
bool noolite_remotes_save(NooLiteApp* app);

/** Send a packet for the current remote (flip handled here). */
bool noolite_app_send(NooLiteApp* app, NooLitePacket* packet);
