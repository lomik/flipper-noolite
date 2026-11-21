#include "noolite_view_remote.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>

typedef struct {
    char name[24];
    uint16_t addr;
    uint8_t level;
    char status[24];
    InputKey pressed;
    bool dimming;
} Model;

struct NooLiteRemoteView {
    View* view;
    NooLiteRemoteActionCallback callback;
    void* context;
};

static void draw_key(Canvas* canvas, int x, int y, const char* label, bool pressed) {
    if(pressed) {
        canvas_draw_rbox(canvas, x - 18, y - 9, 36, 12, 2);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x - 18, y - 9, 36, 12, 2);
    }
    canvas_draw_str_aligned(canvas, x, y, AlignCenter, AlignBottom, label);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_callback(Canvas* canvas, void* _model) {
    Model* m = _model;
    char buf[32];
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 9, m->name);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%04X", m->addr);
    canvas_draw_str_aligned(canvas, 128, 9, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, 11, 127, 11);

    /* d-pad */
    const int cx = 40, cy = 37;
    draw_key(
        canvas,
        cx,
        cy - 12,
        m->dimming && m->pressed == InputKeyUp ? "Dim+" : "On",
        m->pressed == InputKeyUp);
    draw_key(canvas, cx, cy + 2, "Toggle", m->pressed == InputKeyOk);
    draw_key(
        canvas,
        cx,
        cy + 16,
        m->dimming && m->pressed == InputKeyDown ? "Dim-" : "Off",
        m->pressed == InputKeyDown);
    canvas_draw_str_aligned(
        canvas, cx - 21, cy, AlignRight, AlignBottom, m->pressed == InputKeyLeft ? "[-]" : "-");
    canvas_draw_str_aligned(
        canvas, cx + 21, cy, AlignLeft, AlignBottom, m->pressed == InputKeyRight ? "[+]" : "+");

    /* level bar */
    canvas_draw_str(canvas, 82, 22, "Level");
    canvas_draw_frame(canvas, 82, 25, 44, 7);
    canvas_draw_box(canvas, 84, 27, (m->level * 40) / 255, 3);
    snprintf(buf, sizeof(buf), "%u", m->level);
    canvas_draw_str(canvas, 82, 42, buf);

    canvas_draw_str(canvas, 82, 53, "Long OK:");
    canvas_draw_str(canvas, 82, 63, "  menu");

    canvas_draw_str(canvas, 0, 64, m->status);
}

static void emit(NooLiteRemoteView* rv, NooLiteRemoteAction action) {
    if(rv->callback) rv->callback(action, rv->context);
}

static bool input_callback(InputEvent* event, void* context) {
    NooLiteRemoteView* rv = context;
    if(event->key == InputKeyBack) return false;

    bool dimming = false;
    with_view_model(
        rv->view,
        Model * m,
        {
            if(event->type == InputTypePress) m->pressed = event->key;
            if(event->type == InputTypeRelease) m->pressed = InputKeyMAX;
            dimming = m->dimming;
            if(event->type == InputTypeLong &&
               (event->key == InputKeyUp || event->key == InputKeyDown)) {
                m->dimming = true;
            }
            if(event->type == InputTypeRelease) m->dimming = false;
        },
        true);

    switch(event->type) {
    case InputTypeShort:
        if(event->key == InputKeyUp) emit(rv, NooLiteRemoteActionOn);
        if(event->key == InputKeyDown) emit(rv, NooLiteRemoteActionOff);
        if(event->key == InputKeyOk) emit(rv, NooLiteRemoteActionSwitch);
        if(event->key == InputKeyLeft) emit(rv, NooLiteRemoteActionLevelDown);
        if(event->key == InputKeyRight) emit(rv, NooLiteRemoteActionLevelUp);
        break;
    case InputTypeRepeat:
        if(event->key == InputKeyLeft) emit(rv, NooLiteRemoteActionLevelDown);
        if(event->key == InputKeyRight) emit(rv, NooLiteRemoteActionLevelUp);
        break;
    case InputTypeLong:
        if(event->key == InputKeyUp) emit(rv, NooLiteRemoteActionDimUp);
        if(event->key == InputKeyDown) emit(rv, NooLiteRemoteActionDimDown);
        if(event->key == InputKeyOk) emit(rv, NooLiteRemoteActionMenu);
        break;
    case InputTypeRelease:
        if(dimming) emit(rv, NooLiteRemoteActionDimStop);
        break;
    default:
        break;
    }
    return true;
}

NooLiteRemoteView* noolite_remote_view_alloc(void) {
    NooLiteRemoteView* rv = malloc(sizeof(NooLiteRemoteView));
    rv->callback = NULL;
    rv->context = NULL;
    rv->view = view_alloc();
    view_allocate_model(rv->view, ViewModelTypeLocking, sizeof(Model));
    view_set_context(rv->view, rv);
    view_set_draw_callback(rv->view, draw_callback);
    view_set_input_callback(rv->view, input_callback);
    with_view_model(
        rv->view,
        Model * m,
        {
            memset(m, 0, sizeof(*m));
            m->pressed = InputKeyMAX;
        },
        false);
    return rv;
}

void noolite_remote_view_free(NooLiteRemoteView* rv) {
    view_free(rv->view);
    free(rv);
}

View* noolite_remote_view_get_view(NooLiteRemoteView* rv) {
    return rv->view;
}

void noolite_remote_view_set_callback(
    NooLiteRemoteView* rv,
    NooLiteRemoteActionCallback callback,
    void* context) {
    rv->callback = callback;
    rv->context = context;
}

void noolite_remote_view_set_remote(NooLiteRemoteView* rv, const char* name, uint16_t addr) {
    with_view_model(
        rv->view,
        Model * m,
        {
            strlcpy(m->name, name, sizeof(m->name));
            m->addr = addr;
            m->status[0] = 0;
            m->pressed = InputKeyMAX;
            m->dimming = false;
        },
        true);
}

void noolite_remote_view_set_level(NooLiteRemoteView* rv, uint8_t level) {
    with_view_model(rv->view, Model * m, { m->level = level; }, true);
}

void noolite_remote_view_set_status(NooLiteRemoteView* rv, const char* status) {
    with_view_model(rv->view, Model * m, { strlcpy(m->status, status, sizeof(m->status)); }, true);
}
