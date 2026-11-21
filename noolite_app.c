#include "noolite_app.h"

#include <furi_hal_random.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TAG              "NooLite"
#define REMOTES_FILETYPE "NooLite Remotes"
#define REMOTES_VERSION  1

bool noolite_remotes_load(NooLiteApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* str = furi_string_alloc();
    bool ok = false;
    app->remote_count = 0;

    do {
        if(!flipper_format_file_open_existing(ff, NOOLITE_REMOTES_FILE)) break;
        uint32_t version = 0;
        if(!flipper_format_read_header(ff, str, &version)) break;
        if(furi_string_cmp_str(str, REMOTES_FILETYPE) || version != REMOTES_VERSION) break;
        while(app->remote_count < NOOLITE_MAX_REMOTES) {
            uint32_t addr;
            if(!flipper_format_read_string(ff, "Name", str)) break;
            if(!flipper_format_read_uint32(ff, "Address", &addr, 1)) break;
            NooLiteRemote* r = &app->remotes[app->remote_count++];
            strlcpy(r->name, furi_string_get_cstr(str), sizeof(r->name));
            r->addr = addr & 0xFFFF;
        }
        ok = true;
    } while(false);

    FURI_LOG_I(TAG, "Loaded %u remotes", app->remote_count);
    furi_string_free(str);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool noolite_remotes_save(NooLiteApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, APP_DATA_PATH(""));
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;

    do {
        if(!flipper_format_file_open_always(ff, NOOLITE_REMOTES_FILE)) break;
        if(!flipper_format_write_header_cstr(ff, REMOTES_FILETYPE, REMOTES_VERSION)) break;
        bool fail = false;
        for(size_t i = 0; i < app->remote_count && !fail; i++) {
            uint32_t addr = app->remotes[i].addr;
            fail = !flipper_format_write_string_cstr(ff, "Name", app->remotes[i].name) ||
                   !flipper_format_write_uint32(ff, "Address", &addr, 1);
        }
        ok = !fail;
    } while(false);

    if(!ok) FURI_LOG_E(TAG, "Failed to save remotes");
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool noolite_app_send(NooLiteApp* app, NooLitePacket* packet) {
    packet->flip = app->flip;
    app->flip = !app->flip;
    bool ok = noolite_radio_tx(app->radio, packet);
    notification_message(
        app->notifications, ok ? &sequence_blink_magenta_10 : &sequence_blink_red_100);
    FURI_LOG_I(
        TAG,
        "TX addr=%04X cmd=%u fmt=%u flip=%u ok=%u",
        packet->addr,
        packet->cmd,
        packet->fmt,
        packet->flip,
        ok);
    return ok;
}

static bool custom_event_callback(void* context, uint32_t event) {
    NooLiteApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool back_event_callback(void* context) {
    NooLiteApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static NooLiteApp* noolite_app_alloc(void) {
    NooLiteApp* app = malloc(sizeof(NooLiteApp));
    memset(app, 0, sizeof(*app));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&noolite_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, back_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, NooLiteViewSubmenu, submenu_get_view(app->submenu));
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, NooLiteViewTextInput, text_input_get_view(app->text_input));
    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, NooLiteViewByteInput, byte_input_get_view(app->byte_input));
    app->var_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, NooLiteViewVarList, variable_item_list_get_view(app->var_list));
    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, NooLiteViewWidget, widget_get_view(app->widget));
    app->receive_view = noolite_receive_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        NooLiteViewReceive,
        noolite_receive_view_get_view(app->receive_view));
    app->remote_view = noolite_remote_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, NooLiteViewRemote, noolite_remote_view_get_view(app->remote_view));

    app->radio = noolite_radio_alloc();
    app->flip = furi_hal_random_get() & 1;
    app->current = -1;
    app->level = 128;
    app->rgb[0] = app->rgb[1] = app->rgb[2] = 255;
    app->timer_index = 3;

    noolite_remotes_load(app);
    return app;
}

static void noolite_app_free(NooLiteApp* app) {
    noolite_radio_free(app->radio);

    view_dispatcher_remove_view(app->view_dispatcher, NooLiteViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, NooLiteViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, NooLiteViewByteInput);
    view_dispatcher_remove_view(app->view_dispatcher, NooLiteViewVarList);
    view_dispatcher_remove_view(app->view_dispatcher, NooLiteViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, NooLiteViewReceive);
    view_dispatcher_remove_view(app->view_dispatcher, NooLiteViewRemote);

    submenu_free(app->submenu);
    text_input_free(app->text_input);
    byte_input_free(app->byte_input);
    variable_item_list_free(app->var_list);
    widget_free(app->widget);
    noolite_receive_view_free(app->receive_view);
    noolite_remote_view_free(app->remote_view);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t noolite_app(void* p) {
    UNUSED(p);
    NooLiteApp* app = noolite_app_alloc();
    scene_manager_next_scene(app->scene_manager, NooLiteSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    noolite_app_free(app);
    return 0;
}
