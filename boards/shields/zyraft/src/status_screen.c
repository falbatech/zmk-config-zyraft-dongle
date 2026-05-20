#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>

#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>

#include <zmk/keymap.h>
#include <zmk/ble.h>

#include "falbatech_logo.h"

LOG_MODULE_REGISTER(status_screen, LOG_LEVEL_INF);

#define BAR_SEGMENTS 10

#define COLOR_BG         0x000000
#define COLOR_TEXT       0xFFFFFF

#define COLOR_BAR_FULL   0x00FF00
#define COLOR_BAR_EMPTY  0x666666

#define COLOR_DOT_ON     0x00FF00
#define COLOR_DOT_OFF    0x666666

static lv_obj_t *label_left;
static lv_obj_t *label_right;

static lv_obj_t *dot_left;
static lv_obj_t *dot_right;

static lv_obj_t *bar_left[BAR_SEGMENTS];
static lv_obj_t *bar_right[BAR_SEGMENTS];

static int battery_left = 0;
static int battery_right = 0;

static bool left_connected = false;
static bool right_connected = false;

static void style_segment(lv_obj_t *obj) {
    lv_obj_set_size(obj, 8, 18);

    lv_obj_set_style_radius(obj, 2, 0);

    lv_obj_set_style_border_width(obj, 0, 0);

    lv_obj_set_style_bg_color(
        obj,
        lv_color_hex(COLOR_BAR_EMPTY),
        0
    );
}

static void update_segment_bar(lv_obj_t **segments, int percent) {
    if (percent < 0) {
        percent = 0;
    }

    if (percent > 100) {
        percent = 100;
    }

    int filled = (percent + 9) / 10;

    for (int i = 0; i < BAR_SEGMENTS; i++) {

        if (i < filled) {

            lv_obj_set_style_bg_color(
                segments[i],
                lv_color_hex(COLOR_BAR_FULL),
                0
            );

        } else {

            lv_obj_set_style_bg_color(
                segments[i],
                lv_color_hex(COLOR_BAR_EMPTY),
                0
            );
        }
    }
}

static void update_connection_dot(lv_obj_t *dot, bool connected) {

    lv_obj_set_style_bg_color(
        dot,
        lv_color_hex(
            connected ? COLOR_DOT_ON : COLOR_DOT_OFF
        ),
        0
    );
}

static void refresh_status(void) {

    char buf[32];

    snprintf(buf, sizeof(buf), "L %d%%", battery_left);
    lv_label_set_text(label_left, buf);

    snprintf(buf, sizeof(buf), "R %d%%", battery_right);
    lv_label_set_text(label_right, buf);

    update_segment_bar(bar_left, battery_left);
    update_segment_bar(bar_right, battery_right);

    update_connection_dot(dot_left, left_connected);
    update_connection_dot(dot_right, right_connected);
}

static void create_side(
    lv_obj_t *parent,
    int x,
    const char *title,
    lv_obj_t **label_out,
    lv_obj_t **dot_out,
    lv_obj_t **segments_out
) {

    lv_obj_t *cont = lv_obj_create(parent);

    lv_obj_set_size(cont, 145, 80);

    lv_obj_set_pos(cont, x, 80);

    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(cont, 0, 0);

    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(cont);

    lv_label_set_text(title_label, title);

    lv_obj_set_style_text_color(
        title_label,
        lv_color_hex(COLOR_TEXT),
        0
    );

    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *dot = lv_obj_create(cont);

    lv_obj_set_size(dot, 14, 14);

    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);

    lv_obj_set_style_border_width(dot, 0, 0);

    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, 0, 0);

    *dot_out = dot;

    lv_obj_t *label = lv_label_create(cont);

    lv_label_set_text(label, "0%");

    lv_obj_set_style_text_color(
        label,
        lv_color_hex(COLOR_TEXT),
        0
    );

    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 28);

    *label_out = label;

    lv_obj_t *bar_cont = lv_obj_create(cont);

    lv_obj_set_size(bar_cont, 130, 22);

    lv_obj_align(bar_cont, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_set_flex_flow(bar_cont, LV_FLEX_FLOW_ROW);

    lv_obj_set_style_pad_column(bar_cont, 2, 0);

    lv_obj_set_style_bg_opa(bar_cont, LV_OPA_TRANSP, 0);

    lv_obj_set_style_border_width(bar_cont, 0, 0);

    lv_obj_clear_flag(bar_cont, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < BAR_SEGMENTS; i++) {

        segments_out[i] = lv_obj_create(bar_cont);

        style_segment(segments_out[i]);
    }
}

static int battery_listener(const zmk_event_t *eh) {

    const struct zmk_battery_state_changed *ev =
        as_zmk_battery_state_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    battery_left = ev->state_of_charge;

    refresh_status();

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_screen_battery, battery_listener);
ZMK_SUBSCRIPTION(status_screen_battery, zmk_battery_state_changed);

static int peripheral_listener(const zmk_event_t *eh) {

    const struct zmk_split_peripheral_status_changed *ev =
        as_zmk_split_peripheral_status_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->source == 0) {

        right_connected = (ev->state_of_charge > 0);
        battery_right = ev->state_of_charge;

    } else if (ev->source == 1) {

        left_connected = (ev->state_of_charge > 0);
        battery_left = ev->state_of_charge;
    }

    refresh_status();

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_screen_peripheral, peripheral_listener);
ZMK_SUBSCRIPTION(
    status_screen_peripheral,
    zmk_split_peripheral_status_changed
);

lv_obj_t *zmk_display_status_screen(void) {

    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(
        screen,
        lv_color_hex(COLOR_BG),
        0
    );

    lv_obj_set_style_border_width(screen, 0, 0);

    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo = lv_img_create(screen);

    lv_img_set_src(logo, &falbatech_logo_large);

    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 10);

    create_side(
        screen,
        10,
        "LEFT",
        &label_left,
        &dot_left,
        bar_left
    );

    create_side(
        screen,
        165,
        "RIGHT",
        &label_right,
        &dot_right,
        bar_right
    );

    refresh_status();

    return screen;
}
