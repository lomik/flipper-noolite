#include "noolite_view_receive.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>

#define LOG_SIZE      40
#define ROWS          5
#define ROW_H         10
#define REPEAT_WINDOW 1500 /* ms: same packet with same flip = same press */

typedef struct {
    NooLitePacket packet;
    uint32_t tick;
    uint16_t copies;
    int8_t rssi;
} Entry;

typedef struct {
    Entry entries[LOG_SIZE]; /* [0] = newest */
    uint8_t count;
    uint8_t selected;
    uint8_t offset;
    uint32_t total;
} Model;

struct NooLiteReceiveView {
    View* view;
    NooLiteReceiveSelectCallback callback;
    void* context;
};

static void format_entry(const Entry* e, char* buf, size_t size) {
    const NooLitePacket* p = &e->packet;
    NooLiteSensorData s;
    if(noolite_packet_get_sensor(p, &s)) {
        if(s.sensor_type == 2) {
            snprintf(
                buf,
                size,
                "%04X %.1fC %u%%%s",
                p->addr,
                (double)s.temperature,
                s.humidity,
                s.battery_low ? " BAT" : "");
        } else {
            snprintf(
                buf,
                size,
                "%04X %.1fC%s",
                p->addr,
                (double)s.temperature,
                s.battery_low ? " BAT" : "");
        }
        return;
    }
    int n = snprintf(buf, size, "%04X %s", p->addr, noolite_cmd_name(p->cmd));
    if(n < 0 || (size_t)n >= size) return;
    if(p->cmd == NooLiteCmdSetLevel && p->fmt == 1) {
        snprintf(buf + n, size - n, " %u", p->args[0]);
    } else if(p->cmd == NooLiteCmdSetLevel && p->fmt == 3) {
        snprintf(buf + n, size - n, " %u,%u,%u", p->args[0], p->args[1], p->args[2]);
    } else if(p->cmd == NooLiteCmdTemporaryOn) {
        uint32_t q = p->args[0] | (p->args_count > 1 ? p->args[1] << 8 : 0);
        snprintf(buf + n, size - n, " %lus", (unsigned long)(q * 5));
    } else if(p->cmd == NooLiteCmdShadowSetLevel && p->args_count) {
        snprintf(buf + n, size - n, " %u", p->args[0]);
    } else if(!strcmp(noolite_cmd_name(p->cmd), "Unknown")) {
        snprintf(buf + n, size - n, " %u/%u", p->cmd, p->fmt);
    }
}

static void draw_callback(Canvas* canvas, void* _model) {
    Model* m = _model;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 9, "NooLite RX 433.92");
    canvas_set_font(canvas, FontSecondary);
    char buf[40];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)m->total);
    canvas_draw_str_aligned(canvas, 128, 9, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, 11, 127, 11);

    if(!m->count) {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Waiting for signal...");
        canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignCenter, "Press remote button");
        return;
    }

    for(uint8_t row = 0; row < ROWS; row++) {
        uint8_t i = m->offset + row;
        if(i >= m->count) break;
        const Entry* e = &m->entries[i];
        int y = 13 + row * ROW_H;
        format_entry(e, buf, sizeof(buf));
        if(i == m->selected) {
            canvas_draw_box(canvas, 0, y, 123, ROW_H);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 2, y + 8, buf);
        char right[12];
        snprintf(right, sizeof(right), "f%u x%u", e->packet.flip, e->copies);
        canvas_draw_str_aligned(canvas, 121, y + 8, AlignRight, AlignBottom, right);
        canvas_set_color(canvas, ColorBlack);
    }
    elements_scrollbar_pos(canvas, 128, 13, 50, m->selected, m->count);
}

static bool input_callback(InputEvent* event, void* context) {
    NooLiteReceiveView* rv = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool consumed = false;
    uint16_t addr = 0;
    bool select = false;
    with_view_model(
        rv->view,
        Model * m,
        {
            if(event->key == InputKeyUp && m->selected > 0) {
                m->selected--;
                consumed = true;
            } else if(event->key == InputKeyDown && m->selected + 1 < m->count) {
                m->selected++;
                consumed = true;
            } else if(event->key == InputKeyOk && m->count) {
                addr = m->entries[m->selected].packet.addr;
                select = true;
                consumed = true;
            }
            if(m->selected < m->offset) m->offset = m->selected;
            if(m->selected >= m->offset + ROWS) m->offset = m->selected - ROWS + 1;
        },
        true);
    if(select && rv->callback) rv->callback(addr, rv->context);
    return consumed;
}

NooLiteReceiveView* noolite_receive_view_alloc(void) {
    NooLiteReceiveView* rv = malloc(sizeof(NooLiteReceiveView));
    rv->callback = NULL;
    rv->context = NULL;
    rv->view = view_alloc();
    view_allocate_model(rv->view, ViewModelTypeLocking, sizeof(Model));
    view_set_context(rv->view, rv);
    view_set_draw_callback(rv->view, draw_callback);
    view_set_input_callback(rv->view, input_callback);
    noolite_receive_view_clear(rv);
    return rv;
}

void noolite_receive_view_free(NooLiteReceiveView* rv) {
    view_free(rv->view);
    free(rv);
}

View* noolite_receive_view_get_view(NooLiteReceiveView* rv) {
    return rv->view;
}

void noolite_receive_view_set_callback(
    NooLiteReceiveView* rv,
    NooLiteReceiveSelectCallback callback,
    void* context) {
    rv->callback = callback;
    rv->context = context;
}

bool noolite_receive_view_add(NooLiteReceiveView* rv, const NooLitePacket* packet, float rssi) {
    bool fresh = true;
    uint32_t now = furi_get_tick();
    with_view_model(
        rv->view,
        Model * m,
        {
            Entry* last = m->count ? &m->entries[0] : NULL;
            if(last && last->packet.flip == packet->flip &&
               noolite_packet_equal(&last->packet, packet) &&
               now - last->tick < furi_ms_to_ticks(REPEAT_WINDOW)) {
                last->copies++;
                last->tick = now;
                fresh = false;
            } else {
                if(m->count < LOG_SIZE) m->count++;
                memmove(&m->entries[1], &m->entries[0], sizeof(Entry) * (m->count - 1));
                m->entries[0].packet = *packet;
                m->entries[0].tick = now;
                m->entries[0].copies = 1;
                m->entries[0].rssi = (int8_t)rssi;
                m->total++;
                /* keep the cursor on the same entry unless it is at the top */
                if(m->selected && m->selected + 1 < m->count) m->selected++;
                if(m->selected >= m->offset + ROWS) m->offset = m->selected - ROWS + 1;
            }
        },
        true);
    return fresh;
}

void noolite_receive_view_clear(NooLiteReceiveView* rv) {
    with_view_model(rv->view, Model * m, { memset(m, 0, sizeof(*m)); }, true);
}
