Poniżej cały kompletny plik status_screen.c do skopiowania.

/*
 * Zyra FT Dongle - FalbaTech premium status screen
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

#define COLOR_WHITE 0xFFFFFF
#define COLOR_GREEN 0x55E46D
#define COLOR_BG    0x050505
#define COLOR_DARK  0x1B1B1B
#define COLOR_DOT   0x2A2A2A

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
    if (!obj) {
        return;
    }

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
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

static void set_vertical_bar_value(lv_obj_t *fill, int value) {
    if (!fill) {
        return;
    }

    if (value < 0) {
        value = 0;
    }

    if (value > 100) {
        value = 100;
    }

    int max_height = 46;
    int height = (max_height * value) / 100;

    if (height < 4 && value > 0) {
        height = 4;
    }

    lv_obj_set_height(fill, height);
}

static void build_splash(void) {
    splash_logo = lv_image_create(screen);
    lv_image_set_src(splash_logo, &falbatech_logo_large);
    lv_obj_align(splash_logo, LV_ALIGN_CENTER, 0, 0);
}

static void build_top_logo(void) {
    top_logo = lv_image_create(screen);
    lv_image_set_src(top_logo, &falbatech_logo_small);
    lv_obj_align(top_logo, LV_ALIGN_TOP_MID, 0, 8);
    set_hidden(top_logo, true);
}

static void build_layer_label(void) {
    layer_label = lv_label_create(screen);
    lv_label_set_text(layer_label, "Base");
    style_text(layer_label, COLOR_WHITE, &lv_font_montserrat_28);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, -8);
    set_hidden(layer_label, true);
}

static void build_battery_widgets(void) {
    left_label = lv_label_create(screen);
    lv_label_set_text(left_label, "L");
    style_text(left_label, COLOR_WHITE, &lv_font_montserrat_12);
    lv_obj_set_style_text_opa(left_label, LV_OPA_80, 0);
    lv_obj_align(left_label, LV_ALIGN_CENTER, -78, 66);
    set_hidden(left_label, true);

    right_label = lv_label_create(screen);
    lv_label_set_text(right_label, "R");
    style_text(right_label, COLOR_WHITE, &lv_font_montserrat_12);
    lv_obj_set_style_text_opa(right_label, LV_OPA_80, 0);
    lv_obj_align(right_label, LV_ALIGN_CENTER, 78, 66);
    set_hidden(right_label, true);

    left_percent = lv_label_create(screen);
    lv_label_set_text(left_percent, "87%");
    style_text(left_percent, COLOR_WHITE, &lv_font_montserrat_12);
    lv_obj_set_style_text_opa(left_percent, LV_OPA_80, 0);
    lv_obj_align(left_percent, LV_ALIGN_CENTER, -58, 28);
    set_hidden(left_percent, true);

    right_percent = lv_label_create(screen);
    lv_label_set_text(right_percent, "92%");
    style_text(right_percent, COLOR_WHITE, &lv_font_montserrat_12);
    lv_obj_set_style_text_opa(right_percent, LV_OPA_80, 0);
    lv_obj_align(right_percent, LV_ALIGN_CENTER, 58, 28);
    set_hidden(right_percent, true);

    left_bar_bg = make_box(screen, 13, 54, COLOR_DARK, LV_OPA_COVER, 7);
    lv_obj_align(left_bar_bg, LV_ALIGN_CENTER, -58, 66);
    set_hidden(left_bar_bg, true);

    left_bar_fill = make_box(left_bar_bg, 9, 40, COLOR_GREEN, LV_OPA_COVER, 5);
    lv_obj_align(left_bar_fill, LV_ALIGN_BOTTOM_MID, 0, -2);

    right_bar_bg = make_box(screen, 13, 54, COLOR_DARK, LV_OPA_COVER, 7);
    lv_obj_align(right_bar_bg, LV_ALIGN_CENTER, 58, 66);
    set_hidden(right_bar_bg, true);

    right_bar_fill = make_box(right_bar_bg, 9, 42, COLOR_GREEN, LV_OPA_COVER, 5);
    lv_obj_align(right_bar_fill, LV_ALIGN_BOTTOM_MID, 0, -2);

    set_vertical_bar_value(left_bar_fill, 87);
    set_vertical_bar_value(right_bar_fill, 92);
}

static void build_bt_dots(void) {
    for (int i = 0; i < 5; i++) {
        bt_dots[i] = make_box(screen, 8, 8, COLOR_DOT, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        lv_obj_align(bt_dots[i], LV_ALIGN_CENTER, -24 + (i * 12), 92);
        set_hidden(bt_dots[i], true);
    }
}

static void update_active_layer(void) {
    uint8_t layer = zmk_keymap_highest_layer_active();

    const char *name = zmk_keymap_layer_name(layer);

    if (!name) {
        switch (layer) {
            case 0:
                name = "Base";
                break;
            case 1:
                name = "Nav";
                break;
            case 2:
                name = "Num";
                break;
            case 3:
                name = "Sym";
                break;
            case 4:
                name = "Fn";
                break;
            case 5:
                name = "Gaming";
                break;
            default:
                name = "Layer";
                break;
        }
    }

    if (layer_label) {
        lv_label_set_text(layer_label, name);
        lv_obj_set_style_text_color(layer_label, lv_color_hex(COLOR_WHITE), 0);
    }

    update_bt_profile();
}

static void update_bt_profile(void) {
    uint8_t active = zmk_ble_active_profile_index();

    for (int i = 0; i < 5; i++) {
        if (!bt_dots[i]) {
            continue;
        }

        if (i == active) {
            lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(COLOR_GREEN), 0);
            lv_obj_set_size(bt_dots[i], 10, 10);
        } else {
            lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(COLOR_DOT), 0);
            lv_obj_set_size(bt_dots[i], 7, 7);
        }

        lv_obj_align(bt_dots[i], LV_ALIGN_CENTER, -24 + (i * 12), 92);
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

    set_hidden(left_label, false);
    set_hidden(right_label, false);

    set_hidden(left_bar_bg, false);
    set_hidden(right_bar_bg, false);

    set_hidden(left_percent, false);
    set_hidden(right_percent, false);

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

    LOG_INF("FT Dongle premium screen initialized");

    return screen;
}
