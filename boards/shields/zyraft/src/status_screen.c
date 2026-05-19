/*
 * Zyra FT Dongle - FalbaTech status screen
 * GC9A01 240x240 round display
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/keymap.h>
#include <zmk/ble.h>

#include "falbatech_logo.h"

LOG_MODULE_REGISTER(ft_dongle_screen, CONFIG_LOG_DEFAULT_LEVEL);

#define SPLASH_DURATION_MS 2500

#define COLOR_BG        0x000000
#define COLOR_TEXT      0xFFFFFF
#define COLOR_MUTED     0xB8B8B8
#define COLOR_BAR_BG    0x3A3A3A
#define COLOR_BAR_FILL  0x00C853
#define COLOR_DOT_OFF   0x5A5A5A
#define COLOR_DOT_ON    0x00C853

static bool splash_done = false;
static struct k_work_delayable splash_work;

static lv_obj_t *screen;
static lv_obj_t *splash_logo;
static lv_obj_t *top_logo;
static lv_obj_t *layer_label;

static lv_obj_t *left_label;
static lv_obj_t *right_label;

static lv_obj_t *left_bar_bg;
static lv_obj_t *left_bar_fill;
static lv_obj_t *right_bar_bg;
static lv_obj_t *right_bar_fill;

static lv_obj_t *left_percent;
static lv_obj_t *right_percent;

static lv_obj_t *bt_dots[5];

static void update_bt_profile(void);

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

static lv_obj_t *make_box(lv_obj_t *parent, int w, int h, uint32_t color, lv_opa_t opa, int radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void set_vertical_bar_value(lv_obj_t *bar_bg, lv_obj_t *fill, int value) {
    if (!bar_bg || !fill) return;

    if (value < 0) value = 0;
    if (value > 100) value = 100;

    int max_h = 68;
    int h = (max_h * value) / 100;

    if (h < 4 && value > 0) h = 4;

    lv_obj_set_size(fill, 14, h);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, -2);
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
        case 5: return "GAMING";
        default: return "LAYER";
    }
}

static void build_splash(void) {
    splash_logo = lv_image_create(screen);
    lv_image_set_src(splash_logo, &falbatech_logo_large);
    lv_obj_align(splash_logo, LV_ALIGN_CENTER, 0, 0);
}

static void build_top_logo(void) {
    top_logo = lv_image_create(screen);
    lv_image_set_src(top_logo, &falbatech_logo_small);
    lv_obj_align(top_logo, LV_ALIGN_TOP_MID, 0, 14);
    set_hidden(top_logo, true);
}

static void build_layer_label(void) {
    layer_label = lv_label_create(screen);
    lv_label_set_text(layer_label, "BASE");
    style_text(layer_label, COLOR_TEXT, &lv_font_montserrat_28);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, -30);
    set_hidden(layer_label, true);
}

static void build_battery_widgets(void) {
    left_percent = lv_label_create(screen);
    lv_label_set_text(left_percent, "87%");
    style_text(left_percent, COLOR_TEXT, &lv_font_montserrat_14);
    lv_obj_align(left_percent, LV_ALIGN_CENTER, -42, 22);
    set_hidden(left_percent, true);

    right_percent = lv_label_create(screen);
    lv_label_set_text(right_percent, "92%");
    style_text(right_percent, COLOR_TEXT, &lv_font_montserrat_14);
    lv_obj_align(right_percent, LV_ALIGN_CENTER, 42, 22);
    set_hidden(right_percent, true);

    left_bar_bg = make_box(screen, 18, 72, COLOR_BAR_BG, LV_OPA_COVER, 8);
    lv_obj_align(left_bar_bg, LV_ALIGN_CENTER, -42, 66);
    set_hidden(left_bar_bg, true);

    left_bar_fill = make_box(left_bar_bg, 14, 52, COLOR_BAR_FILL, LV_OPA_COVER, 7);
    lv_obj_align(left_bar_fill, LV_ALIGN_BOTTOM_MID, 0, -2);

    right_bar_bg = make_box(screen, 18, 72, COLOR_BAR_BG, LV_OPA_COVER, 8);
    lv_obj_align(right_bar_bg, LV_ALIGN_CENTER, 42, 66);
    set_hidden(right_bar_bg, true);

    right_bar_fill = make_box(right_bar_bg, 14, 58, COLOR_BAR_FILL, LV_OPA_COVER, 7);
    lv_obj_align(right_bar_fill, LV_ALIGN_BOTTOM_MID, 0, -2);

    left_label = lv_label_create(screen);
    lv_label_set_text(left_label, "L");
    style_text(left_label, COLOR_MUTED, &lv_font_montserrat_14);
    lv_obj_align(left_label, LV_ALIGN_CENTER, -42, 109);
    set_hidden(left_label, true);

    right_label = lv_label_create(screen);
    lv_label_set_text(right_label, "R");
    style_text(right_label, COLOR_MUTED, &lv_font_montserrat_14);
    lv_obj_align(right_label, LV_ALIGN_CENTER, 42, 109);
    set_hidden(right_label, true);

    set_vertical_bar_value(left_bar_bg, left_bar_fill, 87);
    set_vertical_bar_value(right_bar_bg, right_bar_fill, 92);
}

static void build_bt_dots(void) {
    for (int i = 0; i < 5; i++) {
        bt_dots[i] = make_box(screen, 9, 9, COLOR_DOT_OFF, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        lv_obj_align(bt_dots[i], LV_ALIGN_CENTER, -32 + (i * 16), 116);
        set_hidden(bt_dots[i], true);
    }
}

static void update_active_layer(void) {
    uint8_t layer = zmk_keymap_highest_layer_active();

    if (layer_label) {
        lv_label_set_text(layer_label, layer_name(layer));
        lv_obj_set_style_text_color(layer_label, lv_color_hex(COLOR_TEXT), 0);
    }

    update_bt_profile();
}

static void update_bt_profile(void) {
    uint8_t active = zmk_ble_active_profile_index();

    for (int i = 0; i < 5; i++) {
        if (!bt_dots[i]) continue;

        if (i == active) {
            lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(COLOR_DOT_ON), 0);
            lv_obj_set_size(bt_dots[i], 11, 11);
        } else {
            lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(COLOR_DOT_OFF), 0);
            lv_obj_set_size(bt_dots[i], 8, 8);
        }

        lv_obj_align(bt_dots[i], LV_ALIGN_CENTER, -32 + (i * 16), 116);
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

    set_hidden(left_bar_bg, false);
    set_hidden(right_bar_bg, false);

    set_hidden(left_label, false);
    set_hidden(right_label, false);

    for (int i = 0; i < 5; i++) {
        set_hidden(bt_dots[i], false);
    }

    update_active_layer();
    update_bt_profile();

    LOG_INF("FT Dongle status screen active");
}

static int ft_dongle_event_listener(const zmk_event_t *eh) {
    if (!splash_done) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_layer_state_changed(eh)) {
        update_active_layer();
    }

    if (as_zmk_ble_active_profile_changed(eh)) {
        update_bt_profile();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ft_dongle_screen, ft_dongle_event_listener);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_ble_active_profile_changed);

lv_obj_t *zmk_display_status_screen(void) {
    screen = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    build_splash();
    build_top_logo();
    build_layer_label();
    build_battery_widgets();
    build_bt_dots();

    update_active_layer();
    update_bt_profile();

    k_work_init_delayable(&splash_work, show_status);
    k_work_schedule(&splash_work, K_MSEC(SPLASH_DURATION_MS));

    LOG_INF("FT Dongle status screen initialized");

    return screen;
}
