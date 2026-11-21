#include "noolite_app.h"

#include <furi_hal_random.h>
#include <stdio.h>

static NooLiteRemote* current_remote(NooLiteApp* app) {
    if(app->current < 0 || (size_t)app->current >= app->remote_count) return NULL;
    return &app->remotes[app->current];
}

static void set_status_sent(NooLiteApp* app, const char* what, bool ok) {
    char buf[24];
    snprintf(buf, sizeof(buf), ok ? "> %s" : "TX failed: %s", what);
    noolite_remote_view_set_status(app->remote_view, buf);
}

static bool send_cmd(NooLiteApp* app, uint8_t cmd) {
    NooLiteRemote* r = current_remote(app);
    if(!r) return false;
    NooLitePacket p;
    noolite_packet_cmd(&p, r->addr, cmd);
    bool ok = noolite_app_send(app, &p);
    set_status_sent(app, noolite_cmd_name(cmd), ok);
    return ok;
}

static bool send_level(NooLiteApp* app) {
    NooLiteRemote* r = current_remote(app);
    if(!r) return false;
    NooLitePacket p;
    noolite_packet_set_level(&p, r->addr, app->level);
    bool ok = noolite_app_send(app, &p);
    char buf[16];
    snprintf(buf, sizeof(buf), "Level %u", app->level);
    set_status_sent(app, buf, ok);
    noolite_remote_view_set_level(app->remote_view, app->level);
    return ok;
}

static void submenu_callback(void* context, uint32_t index) {
    NooLiteApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

/* ======================= Start ======================= */

enum {
    StartReceive,
    StartRemotes,
    StartAdd,
    StartAbout
};

static void scene_start_enter(void* context) {
    NooLiteApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "NooLite");
    submenu_add_item(app->submenu, "Receive", StartReceive, submenu_callback, app);
    submenu_add_item(app->submenu, "Remotes", StartRemotes, submenu_callback, app);
    submenu_add_item(app->submenu, "Add remote", StartAdd, submenu_callback, app);
    submenu_add_item(app->submenu, "About", StartAbout, submenu_callback, app);
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, NooLiteSceneStart));
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewSubmenu);
}

static bool scene_start_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, NooLiteSceneStart, event.event);
    switch(event.event) {
    case StartReceive:
        scene_manager_next_scene(app->scene_manager, NooLiteSceneReceive);
        return true;
    case StartRemotes:
        scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteList);
        return true;
    case StartAdd: {
        app->current = -1;
        uint16_t addr = furi_hal_random_get() & 0xFFFF;
        app->addr_buf[0] = addr >> 8;
        app->addr_buf[1] = addr & 0xFF;
        scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteAddr);
        return true;
    }
    case StartAbout:
        scene_manager_next_scene(app->scene_manager, NooLiteSceneAbout);
        return true;
    }
    return false;
}

static void scene_submenu_exit(void* context) {
    NooLiteApp* app = context;
    submenu_reset(app->submenu);
}

/* ======================= Receive ======================= */

static void receive_rx_callback(const NooLitePacket* packet, float rssi, void* context) {
    NooLiteApp* app = context;
    char args[16] = {0};
    for(uint8_t i = 0; i < packet->args_count; i++) {
        snprintf(args + i * 3, sizeof(args) - i * 3, "%02X ", packet->args[i]);
    }
    FURI_LOG_I(
        "NooLite",
        "RX addr=%04X cmd=%u fmt=%u flip=%u args=%s rssi=%d",
        packet->addr,
        packet->cmd,
        packet->fmt,
        packet->flip,
        args,
        (int)rssi);
    if(noolite_receive_view_add(app->receive_view, packet, rssi)) {
        notification_message(app->notifications, &sequence_blink_green_10);
    }
}

static void receive_select_callback(uint16_t addr, void* context) {
    NooLiteApp* app = context;
    app->addr_buf[0] = addr >> 8;
    app->addr_buf[1] = addr & 0xFF;
    view_dispatcher_send_custom_event(app->view_dispatcher, NooLiteEventReceiveSave);
}

static void scene_receive_enter(void* context) {
    NooLiteApp* app = context;
    noolite_receive_view_set_callback(app->receive_view, receive_select_callback, app);
    noolite_radio_rx_start(app->radio, receive_rx_callback, app);
    notification_message(app->notifications, &sequence_blink_start_cyan);
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewReceive);
}

static bool scene_receive_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == NooLiteEventReceiveSave) {
        app->current = -1;
        snprintf(
            app->name_buf,
            sizeof(app->name_buf),
            "Remote %02X%02X",
            app->addr_buf[0],
            app->addr_buf[1]);
        scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteName);
        return true;
    }
    return false;
}

static void scene_receive_exit(void* context) {
    NooLiteApp* app = context;
    noolite_radio_rx_stop(app->radio);
    notification_message(app->notifications, &sequence_blink_stop);
}

/* ======================= Remote list ======================= */

#define LIST_ADD 0xFFFF

static void scene_remote_list_enter(void* context) {
    NooLiteApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Remotes");
    for(size_t i = 0; i < app->remote_count; i++) {
        submenu_add_item(app->submenu, app->remotes[i].name, i, submenu_callback, app);
    }
    if(app->remote_count < NOOLITE_MAX_REMOTES) {
        submenu_add_item(app->submenu, "+ Add remote", LIST_ADD, submenu_callback, app);
    }
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, NooLiteSceneRemoteList));
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewSubmenu);
}

static bool scene_remote_list_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, NooLiteSceneRemoteList, event.event);
    if(event.event == LIST_ADD) {
        app->current = -1;
        uint16_t addr = furi_hal_random_get() & 0xFFFF;
        app->addr_buf[0] = addr >> 8;
        app->addr_buf[1] = addr & 0xFF;
        scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteAddr);
    } else {
        app->current = event.event;
        scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteControl);
    }
    return true;
}

/* ======================= Address input ======================= */

static void byte_input_done(void* context) {
    NooLiteApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, NooLiteEventByteDone);
}

static void scene_remote_addr_enter(void* context) {
    NooLiteApp* app = context;
    byte_input_set_header_text(app->byte_input, "Remote address (hex)");
    byte_input_set_result_callback(
        app->byte_input, byte_input_done, NULL, app, app->addr_buf, sizeof(app->addr_buf));
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewByteInput);
}

static bool scene_remote_addr_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == NooLiteEventByteDone) {
        snprintf(
            app->name_buf,
            sizeof(app->name_buf),
            "Remote %02X%02X",
            app->addr_buf[0],
            app->addr_buf[1]);
        scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteName);
        return true;
    }
    return false;
}

static void scene_remote_addr_exit(void* context) {
    NooLiteApp* app = context;
    byte_input_set_result_callback(app->byte_input, NULL, NULL, NULL, NULL, 0);
}

/* ======================= Name input ======================= */

static void text_input_done(void* context) {
    NooLiteApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, NooLiteEventTextDone);
}

static void scene_remote_name_enter(void* context) {
    NooLiteApp* app = context;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Remote name");
    text_input_set_result_callback(
        app->text_input, text_input_done, app, app->name_buf, sizeof(app->name_buf), false);
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewTextInput);
}

static bool scene_remote_name_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != NooLiteEventTextDone)
        return false;

    NooLiteRemote* r = current_remote(app);
    if(r) {
        /* rename */
        strlcpy(r->name, app->name_buf, sizeof(r->name));
        noolite_remotes_save(app);
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, NooLiteSceneRemoteControl);
        return true;
    }

    if(app->remote_count < NOOLITE_MAX_REMOTES) {
        r = &app->remotes[app->remote_count];
        strlcpy(r->name, app->name_buf, sizeof(r->name));
        r->addr = ((uint16_t)app->addr_buf[0] << 8) | app->addr_buf[1];
        app->current = app->remote_count;
        app->remote_count++;
        noolite_remotes_save(app);
    }
    scene_manager_search_and_switch_to_previous_scene(app->scene_manager, NooLiteSceneStart);
    scene_manager_set_scene_state(app->scene_manager, NooLiteSceneRemoteList, app->current);
    scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteList);
    scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteControl);
    return true;
}

static void scene_remote_name_exit(void* context) {
    NooLiteApp* app = context;
    text_input_reset(app->text_input);
}

/* ======================= Remote control ======================= */

static void remote_action_callback(NooLiteRemoteAction action, void* context) {
    NooLiteApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, NooLiteEventSendBase + action);
}

static void scene_remote_control_enter(void* context) {
    NooLiteApp* app = context;
    NooLiteRemote* r = current_remote(app);
    if(!r) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }
    noolite_remote_view_set_callback(app->remote_view, remote_action_callback, app);
    noolite_remote_view_set_remote(app->remote_view, r->name, r->addr);
    noolite_remote_view_set_level(app->remote_view, app->level);
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewRemote);
}

static bool scene_remote_control_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event < NooLiteEventSendBase) return false;

    switch((NooLiteRemoteAction)(event.event - NooLiteEventSendBase)) {
    case NooLiteRemoteActionOn:
        send_cmd(app, NooLiteCmdOn);
        break;
    case NooLiteRemoteActionOff:
        send_cmd(app, NooLiteCmdOff);
        break;
    case NooLiteRemoteActionSwitch:
        send_cmd(app, NooLiteCmdSwitch);
        break;
    case NooLiteRemoteActionDimUp:
        send_cmd(app, NooLiteCmdSlowUp);
        break;
    case NooLiteRemoteActionDimDown:
        send_cmd(app, NooLiteCmdSlowDown);
        break;
    case NooLiteRemoteActionDimStop:
        send_cmd(app, NooLiteCmdStop);
        break;
    case NooLiteRemoteActionLevelUp:
        app->level = app->level > 255 - 16 ? 255 : app->level + 16;
        send_level(app);
        break;
    case NooLiteRemoteActionLevelDown:
        app->level = app->level < 16 ? 0 : app->level - 16;
        send_level(app);
        break;
    case NooLiteRemoteActionMenu:
        scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteMenu);
        break;
    }
    return true;
}

static void scene_noop_exit(void* context) {
    UNUSED(context);
}

/* ======================= Remote menu ======================= */

enum {
    MenuBind,
    MenuUnbind,
    MenuSetLevel,
    MenuRgb,
    MenuRollColor,
    MenuSwitchColor,
    MenuSwitchMode,
    MenuSwitchSpeed,
    MenuLoadPreset,
    MenuSavePreset,
    MenuTimer,
    MenuRename,
    MenuDelete,
};

static void scene_remote_menu_enter(void* context) {
    NooLiteApp* app = context;
    NooLiteRemote* r = current_remote(app);
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, r ? r->name : "Remote");
    submenu_add_item(app->submenu, "Bind (pair)", MenuBind, submenu_callback, app);
    submenu_add_item(app->submenu, "Unbind", MenuUnbind, submenu_callback, app);
    submenu_add_item(app->submenu, "Set level...", MenuSetLevel, submenu_callback, app);
    submenu_add_item(app->submenu, "RGB color...", MenuRgb, submenu_callback, app);
    submenu_add_item(app->submenu, "Roll color", MenuRollColor, submenu_callback, app);
    submenu_add_item(app->submenu, "Switch color", MenuSwitchColor, submenu_callback, app);
    submenu_add_item(app->submenu, "Switch mode", MenuSwitchMode, submenu_callback, app);
    submenu_add_item(app->submenu, "Switch speed", MenuSwitchSpeed, submenu_callback, app);
    submenu_add_item(app->submenu, "Load preset", MenuLoadPreset, submenu_callback, app);
    submenu_add_item(app->submenu, "Save preset", MenuSavePreset, submenu_callback, app);
    submenu_add_item(app->submenu, "Temporary on...", MenuTimer, submenu_callback, app);
    submenu_add_item(app->submenu, "Rename", MenuRename, submenu_callback, app);
    submenu_add_item(app->submenu, "Delete", MenuDelete, submenu_callback, app);
    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, NooLiteSceneRemoteMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewSubmenu);
}

static bool scene_remote_menu_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    scene_manager_set_scene_state(app->scene_manager, NooLiteSceneRemoteMenu, event.event);
    NooLiteRemote* r = current_remote(app);
    if(!r) return false;

    switch(event.event) {
    case MenuBind:
        send_cmd(app, NooLiteCmdBind);
        break;
    case MenuUnbind:
        send_cmd(app, NooLiteCmdUnbind);
        break;
    case MenuRollColor:
        send_cmd(app, NooLiteCmdRollColor);
        break;
    case MenuSwitchColor:
        send_cmd(app, NooLiteCmdSwitchColor);
        break;
    case MenuSwitchMode:
        send_cmd(app, NooLiteCmdSwitchMode);
        break;
    case MenuSwitchSpeed:
        send_cmd(app, NooLiteCmdSwitchSpeed);
        break;
    case MenuLoadPreset:
        send_cmd(app, NooLiteCmdLoadPreset);
        break;
    case MenuSavePreset:
        send_cmd(app, NooLiteCmdSavePreset);
        break;
    case MenuSetLevel:
        scene_manager_next_scene(app->scene_manager, NooLiteSceneSetLevel);
        break;
    case MenuRgb:
        scene_manager_next_scene(app->scene_manager, NooLiteSceneRgb);
        break;
    case MenuTimer:
        scene_manager_next_scene(app->scene_manager, NooLiteSceneTimer);
        break;
    case MenuRename:
        strlcpy(app->name_buf, r->name, sizeof(app->name_buf));
        scene_manager_next_scene(app->scene_manager, NooLiteSceneRemoteName);
        break;
    case MenuDelete:
        memmove(
            &app->remotes[app->current],
            &app->remotes[app->current + 1],
            sizeof(NooLiteRemote) * (app->remote_count - app->current - 1));
        app->remote_count--;
        app->current = -1;
        noolite_remotes_save(app);
        scene_manager_set_scene_state(app->scene_manager, NooLiteSceneRemoteMenu, 0);
        scene_manager_set_scene_state(app->scene_manager, NooLiteSceneRemoteList, 0);
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, NooLiteSceneRemoteList);
        break;
    }
    return true;
}

/* ======================= Set level ======================= */

#define EVENT_VARLIST_SEND 3000

static void varlist_enter_callback(void* context, uint32_t index) {
    UNUSED(index);
    NooLiteApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_VARLIST_SEND);
}

static void level_show(VariableItem* item, uint8_t level) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u (%u%%)", level, (level * 100 + 127) / 255);
    variable_item_set_current_value_text(item, buf);
}

static void level_changed(VariableItem* item) {
    NooLiteApp* app = variable_item_get_context(item);
    app->level = variable_item_get_current_value_index(item) * 5;
    level_show(item, app->level);
}

static void scene_set_level_enter(void* context) {
    NooLiteApp* app = context;
    variable_item_list_reset(app->var_list);
    VariableItem* item = variable_item_list_add(app->var_list, "Level", 52, level_changed, app);
    variable_item_set_current_value_index(item, app->level / 5);
    level_show(item, app->level); /* keep exact level until the user changes it */
    variable_item_list_add(app->var_list, "Press OK to send", 0, NULL, NULL);
    variable_item_list_set_enter_callback(app->var_list, varlist_enter_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewVarList);
}

static bool scene_set_level_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == EVENT_VARLIST_SEND) {
        send_level(app);
        notification_message(app->notifications, &sequence_blink_magenta_10);
        return true;
    }
    return false;
}

static void scene_varlist_exit(void* context) {
    NooLiteApp* app = context;
    variable_item_list_reset(app->var_list);
}

/* ======================= RGB ======================= */

static void rgb_changed(VariableItem* item) {
    NooLiteApp* app = variable_item_get_context(item);
    uint8_t ch = variable_item_list_get_selected_item_index(app->var_list);
    if(ch > 2) return;
    app->rgb[ch] = variable_item_get_current_value_index(item) * 15;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", app->rgb[ch]);
    variable_item_set_current_value_text(item, buf);
}

static void scene_rgb_enter(void* context) {
    NooLiteApp* app = context;
    variable_item_list_reset(app->var_list);
    const char* names[3] = {"Red", "Green", "Blue"};
    for(uint8_t i = 0; i < 3; i++) {
        VariableItem* item = variable_item_list_add(app->var_list, names[i], 18, rgb_changed, app);
        variable_item_set_current_value_index(item, app->rgb[i] / 15);
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", app->rgb[i]);
        variable_item_set_current_value_text(item, buf);
    }
    variable_item_list_add(app->var_list, "Press OK to send", 0, NULL, NULL);
    variable_item_list_set_enter_callback(app->var_list, varlist_enter_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewVarList);
}

static bool scene_rgb_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == EVENT_VARLIST_SEND) {
        NooLiteRemote* r = current_remote(app);
        if(!r) return true;
        NooLitePacket p;
        noolite_packet_set_rgb(&p, r->addr, app->rgb[0], app->rgb[1], app->rgb[2]);
        bool ok = noolite_app_send(app, &p);
        set_status_sent(app, "RGB", ok);
        notification_message(app->notifications, &sequence_blink_magenta_10);
        return true;
    }
    return false;
}

/* ======================= Temporary on ======================= */

static const uint16_t timer_seconds[] = {5, 10, 30, 60, 300, 600, 1800, 3600, 7200, 14400};
static const char* timer_names[] =
    {"5 s", "10 s", "30 s", "1 min", "5 min", "10 min", "30 min", "1 h", "2 h", "4 h"};
#define TIMER_COUNT (sizeof(timer_seconds) / sizeof(timer_seconds[0]))

static void timer_changed(VariableItem* item) {
    NooLiteApp* app = variable_item_get_context(item);
    app->timer_index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, timer_names[app->timer_index]);
}

static void scene_timer_enter(void* context) {
    NooLiteApp* app = context;
    variable_item_list_reset(app->var_list);
    VariableItem* item =
        variable_item_list_add(app->var_list, "Duration", TIMER_COUNT, timer_changed, app);
    variable_item_set_current_value_index(item, app->timer_index);
    variable_item_set_current_value_text(item, timer_names[app->timer_index]);
    variable_item_list_add(app->var_list, "Press OK to send", 0, NULL, NULL);
    variable_item_list_set_enter_callback(app->var_list, varlist_enter_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewVarList);
}

static bool scene_timer_event(void* context, SceneManagerEvent event) {
    NooLiteApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == EVENT_VARLIST_SEND) {
        NooLiteRemote* r = current_remote(app);
        if(!r) return true;
        NooLitePacket p;
        noolite_packet_temporary_on(&p, r->addr, timer_seconds[app->timer_index] / 5);
        bool ok = noolite_app_send(app, &p);
        set_status_sent(app, "Temp. on", ok);
        notification_message(app->notifications, &sequence_blink_magenta_10);
        return true;
    }
    return false;
}

/* ======================= About ======================= */

static void scene_about_enter(void* context) {
    NooLiteApp* app = context;
    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget,
        0,
        0,
        128,
        64,
        "\e#NooLite\n"
        "Classic nooLite 433.92 MHz\n"
        "remote and receiver.\n\n"
        "\e#Receive\n"
        "Shows packets from remotes\n"
        "and PT111/PT112 sensors.\n"
        "OK saves address as remote.\n\n"
        "\e#Remote\n"
        "Up/Down: on/off\n"
        "OK: toggle\n"
        "Hold Up/Down: dim\n"
        "Left/Right: level -/+\n"
        "Hold OK: menu (bind,\n"
        "RGB, timer, presets)\n\n"
        "\e#Pairing\n"
        "Put the power block into\n"
        "bind mode, then choose\n"
        "Bind in the remote menu.\n\n"
        "nooLite-F is not supported.");
    view_dispatcher_switch_to_view(app->view_dispatcher, NooLiteViewWidget);
}

static bool scene_about_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static void scene_about_exit(void* context) {
    NooLiteApp* app = context;
    widget_reset(app->widget);
}

/* ======================= Handlers ======================= */

static void (*const scene_on_enter[])(void*) = {
    [NooLiteSceneStart] = scene_start_enter,
    [NooLiteSceneReceive] = scene_receive_enter,
    [NooLiteSceneRemoteList] = scene_remote_list_enter,
    [NooLiteSceneRemoteAddr] = scene_remote_addr_enter,
    [NooLiteSceneRemoteName] = scene_remote_name_enter,
    [NooLiteSceneRemoteControl] = scene_remote_control_enter,
    [NooLiteSceneRemoteMenu] = scene_remote_menu_enter,
    [NooLiteSceneSetLevel] = scene_set_level_enter,
    [NooLiteSceneRgb] = scene_rgb_enter,
    [NooLiteSceneTimer] = scene_timer_enter,
    [NooLiteSceneAbout] = scene_about_enter,
};

static bool (*const scene_on_event[])(void*, SceneManagerEvent) = {
    [NooLiteSceneStart] = scene_start_event,
    [NooLiteSceneReceive] = scene_receive_event,
    [NooLiteSceneRemoteList] = scene_remote_list_event,
    [NooLiteSceneRemoteAddr] = scene_remote_addr_event,
    [NooLiteSceneRemoteName] = scene_remote_name_event,
    [NooLiteSceneRemoteControl] = scene_remote_control_event,
    [NooLiteSceneRemoteMenu] = scene_remote_menu_event,
    [NooLiteSceneSetLevel] = scene_set_level_event,
    [NooLiteSceneRgb] = scene_rgb_event,
    [NooLiteSceneTimer] = scene_timer_event,
    [NooLiteSceneAbout] = scene_about_event,
};

static void (*const scene_on_exit[])(void*) = {
    [NooLiteSceneStart] = scene_submenu_exit,
    [NooLiteSceneReceive] = scene_receive_exit,
    [NooLiteSceneRemoteList] = scene_submenu_exit,
    [NooLiteSceneRemoteAddr] = scene_remote_addr_exit,
    [NooLiteSceneRemoteName] = scene_remote_name_exit,
    [NooLiteSceneRemoteControl] = scene_noop_exit,
    [NooLiteSceneRemoteMenu] = scene_submenu_exit,
    [NooLiteSceneSetLevel] = scene_varlist_exit,
    [NooLiteSceneRgb] = scene_varlist_exit,
    [NooLiteSceneTimer] = scene_varlist_exit,
    [NooLiteSceneAbout] = scene_about_exit,
};

const SceneManagerHandlers noolite_scene_handlers = {
    .on_enter_handlers = scene_on_enter,
    .on_event_handlers = scene_on_event,
    .on_exit_handlers = scene_on_exit,
    .scene_num = NooLiteSceneNum,
};
