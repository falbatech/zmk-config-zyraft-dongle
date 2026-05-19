/*
 * Zyra FT Dongle - FalbaTech Premium Status Screen
 * GC9A01 240x240 round display
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>

#include <zmk/keymap.h>
#include <zmk/ble.h>
#include <zmk/battery.h>

#include "falbatech_logo.h"

LOG_MODULE_REGISTER(ft_dongle_screen, CONFIG_LOG_DEFAULT_LEVEL);

#define SPLASH_DURATION_MS 2500

#define COLOR_BG         0x000000
#define COLOR_TEXT       0xFFFFFF
#define COLOR_MUTED      0xA0A0A0

#define COLOR_BAR_BG     0xFFFFFF
#define COLOR_BAR_FILL   0x00D95F

#define COLOR_DOT_OFF    0x555555
#define COLOR_DOT_ON     0x00D95F

#define COLOR_CHARGING   0x3FA9FF

static bool splash_done = false;
static bool usb_connected = false;

static struct k_work_delayable splash_work;

static lv_obj_t *screen;
static lv_obj_t *splash_logo;
static lv_obj_t *top_logo;
static lv_obj_t *layer_label;

static lv_obj_t *left_bar_bg;
static lv_obj_t *left_bar_fill;

static lv_obj_t *right_bar_bg;
static lv_obj_t *right_bar_fill;

static lv_obj_t *left_percent;
static lv_obj_t *right_percent;

static lv_obj_t *left_icon;
static lv_obj_t *right_icon;

static lv_obj_t *bt_dots[5];

static int left_battery = 100;
static int right_battery = 100;

static void set_hidden(lv_obj_t *obj, bool hidden) {
    if (!obj) return;

    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void style_text(lv_obj_t *obj, uint32_t color, const lv_font_t *font) {
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(obj, font, 0);
}

static lv_obj_t *make_box(lv_obj_t *parent, int w, int h, uint32_t color, int radius) {
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_size(obj, w, h);

    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);

    lv_obj_set_style_pad_all(obj, 0, 0);

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

static const char *layer_name(uint8_t layer) {
    const char *name = zmk_keymap_layer_name(layer);

    if (name) {
        return name;
    }

    switch (layer) {
        case 0: return "BASE";
        case 1: return "NAV";
        case 2: return "NUM";
        case 3: return "SYM";
        case 4: return "FN";
        case 5: return "GAME";
        default: return "LAYER";
    }
}

static void update_vertical_bar(lv_obj_t *fill, int value) {
    if (!fill) return;

    if (value < 0) value = 0;
    if (value > 100) value = 100;

    int max_h = 90;
    int h = (max_h * value) / 100;

    if (h < 4 && value > 0) {
        h = 4;
    }

    lv_obj_set_size(fill, 16, h);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, -2);
}

static void update_battery_visuals(void) {

    lv_label_set_text_fmt(left_percent, "%d%%", left_battery);
    lv_label_set_text_fmt(right_percent, "%d%%", right_battery);

    update_vertical_bar(left_bar_fill, left_battery);
    update_vertical_bar(right_bar_fill, right_battery);

    if (usb_connected) {

        lv_obj_set_style_bg_color(left_bar_fill, lv_color_hex(COLOR_CHARGING), 0);
        lv_obj_set_style_bg_color(right_bar_fill, lv_color_hex(COLOR_CHARGING), 0);

        lv_label_set_text(left_icon, LV_SYMBOL_CHARGE);
        lv_label_set_text(right_icon, LV_SYMBOL_CHARGE);

        lv_obj_set_style_text_color(left_icon, lv_color_hex(COLOR_CHARGING), 0);
        lv_obj_set_style_text_color(right_icon, lv_color_hex(COLOR_CHARGING), 0);

    } else {

        lv_obj_set_style_bg_color(left_bar_fill, lv_color_hex(COLOR_BAR_FILL), 0);
        lv_obj_set_style_bg_color(right_bar_fill, lv_color_hex(COLOR_BAR_FILL), 0);

        lv_label_set_text(left_icon, LV_SYMBOL_BATTERY_FULL);
        lv_label_set_text(right_icon, LV_SYMBOL_BATTERY_FULL);

        lv_obj_set_style_text_color(left_icon, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_color(right_icon, lv_color_hex(COLOR_TEXT), 0);
    }
}

static void update_bt_profile(void) {

    uint8_t active = zmk_ble_active_profile_index();

    for (int i = 0; i < 5; i++) {

        if (!bt_dots[i]) {
            continue;
        }

        if (i == active) {

            lv_obj_set_style_bg_color(
                bt_dots[i],
                lv_color_hex(COLOR_DOT_ON),
                0
            );

            lv_obj_set_size(bt_dots[i], 11, 11);

        } else {

            lv_obj_set_style_bg_color(
                bt_dots[i],
                lv_color_hex(COLOR_DOT_OFF),
                0
            );

            lv_obj_set_size(bt_dots[i], 8, 8);
        }

        lv_obj_align(
            bt_dots[i],
            LV_ALIGN_BOTTOM_MID,
            -32 + (i * 16),
            -12
        );
    }
}

static void update_layer(void) {

    uint8_t layer = zmk_keymap_highest_layer_active();

    lv_label_set_text(layer_label, layer_name(layer));
}

static void build_splash(void) {

    splash_logo = lv_image_create(screen);

    lv_image_set_src(
        splash_logo,
        &falbatech_logo_large
    );

    lv_obj_align(
        splash_logo,
        LV_ALIGN_CENTER,
        0,
        0
    );
}

static void build_top_logo(void) {

    top_logo = lv_image_create(screen);

    lv_image_set_src(
        top_logo,
        &falbatech_logo_small
    );

    lv_obj_align(
        top_logo,
        LV_ALIGN_TOP_MID,
        0,
        10
    );

    set_hidden(top_logo, true);
}

static void build_layer_label(void) {

    layer_label = lv_label_create(screen);

    lv_label_set_text(layer_label, "BASE");

    style_text(
        layer_label,
        COLOR_TEXT,
        &lv_font_montserrat_28
    );

    lv_obj_align(
        layer_label,
        LV_ALIGN_CENTER,
        0,
        -48
    );

    set_hidden(layer_label, true);
}

static void build_battery_widgets(void) {

    left_percent = lv_label_create(screen);
    style_text(left_percent, COLOR_TEXT, &lv_font_montserrat_14);

    lv_obj_align(
        left_percent,
        LV_ALIGN_CENTER,
        -78,
        -12
    );

    set_hidden(left_percent, true);

    right_percent = lv_label_create(screen);
    style_text(right_percent, COLOR_TEXT, &lv_font_montserrat_14);

    lv_obj_align(
        right_percent,
        LV_ALIGN_CENTER,
        78,
        -12
    );

    set_hidden(right_percent, true);

    left_icon = lv_label_create(screen);
    style_text(left_icon, COLOR_TEXT, &lv_font_montserrat_16);

    lv_obj_align(
        left_icon,
        LV_ALIGN_CENTER,
        -78,
        -34
    );

    set_hidden(left_icon, true);

    right_icon = lv_label_create(screen);
    style_text(right_icon, COLOR_TEXT, &lv_font_montserrat_16);

    lv_obj_align(
        right_icon,
        LV_ALIGN_CENTER,
        78,
        -34
    );

    set_hidden(right_icon, true);

    left_bar_bg = make_box(
        screen,
        20,
        94,
        COLOR_BAR_BG,
        10
    );

    lv_obj_align(
        left_bar_bg,
        LV_ALIGN_CENTER,
        -78,
        44
    );

    set_hidden(left_bar_bg, true);

    left_bar_fill = make_box(
        left_bar_bg,
        16,
        80,
        COLOR_BAR_FILL,
        8
    );

    right_bar_bg = make_box(
        screen,
        20,
        94,
        COLOR_BAR_BG,
        10
    );

    lv_obj_align(
        right_bar_bg,
        LV_ALIGN_CENTER,
        78,
        44
    );

    set_hidden(right_bar_bg, true);

    right_bar_fill = make_box(
        right_bar_bg,
        16,
        80,
        COLOR_BAR_FILL,
        8
    );

    update_battery_visuals();
}

static void build_bt_dots(void) {

    for (int i = 0; i < 5; i++) {

        bt_dots[i] = make_box(
            screen,
            8,
            8,
            COLOR_DOT_OFF,
            LV_RADIUS_CIRCLE
        );

        lv_obj_align(
            bt_dots[i],
            LV_ALIGN_BOTTOM_MID,
            -32 + (i * 16),
            -12
        );

        set_hidden(bt_dots[i], true);
    }
}

static void show_status(struct k_work *work) {

    if (splash_logo) {
        lv_obj_del(splash_logo);
        splash_logo = NULL;
    }

    splash_done = true;

    set_hidden(top_logo, false);
    set_hidden(layer_label, false);

    set_hidden(left_percent, false);
    set_hidden(right_percent, false);

    set_hidden(left_icon, false);
    set_hidden(right_icon, false);

    set_hidden(left_bar_bg, false);
    set_hidden(right_bar_bg, false);

    for (int i = 0; i < 5; i++) {
        set_hidden(bt_dots[i], false);
    }

    update_layer();
    update_bt_profile();
    update_battery_visuals();
}

static int ft_dongle_listener(const zmk_event_t *eh) {

    if (!splash_done) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_layer_state_changed(eh)) {
        update_layer();
    }

    if (as_zmk_ble_active_profile_changed(eh)) {
        update_bt_profile();
    }

    if (as_zmk_usb_conn_state_changed(eh)) {

        const struct zmk_usb_conn_state_changed *ev =
            as_zmk_usb_conn_state_changed(eh);

        usb_connected = ev->connected;

        update_battery_visuals();
    }

    if (as_zmk_battery_state_changed(eh)) {

        const struct zmk_battery_state_changed *ev =
            as_zmk_battery_state_changed(eh);

        left_battery = ev->state_of_charge;
        right_battery = ev->state_of_charge;

        update_battery_visuals();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ft_dongle_screen, ft_dongle_listener);

ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_usb_conn_state_changed);

lv_obj_t *zmk_display_status_screen(void) {

    screen = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(
        screen,
        lv_color_hex(COLOR_BG),
        0
    );

    lv_obj_set_style_bg_opa(
        screen,
        LV_OPA_COVER,
        0
    );

    lv_obj_set_style_border_width(
        screen,
        0,
        0
    );

    lv_obj_set_style_pad_all(
        screen,
        0,
        0
    );

    lv_obj_clear_flag(
        screen,
        LV_OBJ_FLAG_SCROLLABLE
    );

    build_splash();
    build_top_logo();
    build_layer_label();
    build_battery_widgets();
    build_bt_dots();

    update_layer();
    update_bt_profile();
    update_battery_visuals();

    k_work_init_delayable(
        &splash_work,
        show_status
    );

    k_work_schedule(
        &splash_work,
        K_MSEC(SPLASH_DURATION_MS)
    );

    return screen;
}
