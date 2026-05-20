/*
 * Zyra FT Dongle - FalbaTech Status Screen
 * GC9A01 240x240
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>

#include <zmk/keymap.h>
#include <zmk/ble.h>

#include "falbatech_logo.h"

LOG_MODULE_REGISTER(ft_dongle_screen, CONFIG_LOG_DEFAULT_LEVEL);

#define SPLASH_DURATION_MS 2500

#define COLOR_BG        0x000000
#define COLOR_TEXT      0xFFFFFF

#define COLOR_BAR_LOW   0xBBBBBB  /* dolne segmenty — jasny szary/srebrny */
#define COLOR_BAR_HIGH  0x00FF44  /* górne segmenty — żywy zielony */
#define COLOR_BAR_OFF   0x1A1A1A

#define COLOR_DOT_ON    0xE00039
#define COLOR_DOT_OFF   0x333333

#define COLOR_LINK_ON   0xE00039
#define COLOR_LINK_OFF  0x333333

#define COLOR_CHARGE    0xE00039

#define BAR_SEGMENTS 10
#define BAR_W 18
#define SEG_H 5
#define SEG_GAP 2

static bool splash_done = false;
static bool left_connected = false;
static bool right_connected = false;

static struct k_work_delayable splash_work;

static lv_obj_t *screen;
static lv_obj_t *splash_logo;
static lv_obj_t *top_logo;
static lv_obj_t *layer_label;

static lv_obj_t *left_percent;
static lv_obj_t *right_percent;

static lv_obj_t *left_icon;
static lv_obj_t *right_icon;

static lv_obj_t *left_link;
static lv_obj_t *right_link;

static lv_obj_t *left_segments[BAR_SEGMENTS];
static lv_obj_t *right_segments[BAR_SEGMENTS];

static lv_obj_t *bt_dots[5];

static int battery_left = 0;
static int battery_right = 0;

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
        case 0: return "Base";
        case 1: return "Nav";
        case 2: return "Num";
        case 3: return "Sym";
        case 4: return "Fn";
        case 5: return "Game";
        default: return "Layer";
    }
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
        if (!segments[i]) {
            continue;
        }

        if (i < filled) {
            uint32_t color;
            if (filled >= BAR_SEGMENTS || i >= BAR_SEGMENTS / 2) {
                color = COLOR_BAR_HIGH;
            } else {
                color = COLOR_BAR_LOW;
            }
            lv_obj_set_style_bg_color(
                segments[i],
                lv_color_hex(color),
                0
            );
        } else {
            lv_obj_set_style_bg_color(
                segments[i],
                lv_color_hex(COLOR_BAR_OFF),
                0
            );
        }
    }
}

static void update_link_status(void) {
    lv_label_set_text(left_link, "L");
    lv_label_set_text(right_link, "R");

    lv_obj_set_style_text_color(
        left_link,
        lv_color_hex(left_connected ? COLOR_LINK_ON : COLOR_LINK_OFF),
        0
    );

    lv_obj_set_style_text_color(
        right_link,
        lv_color_hex(right_connected ? COLOR_LINK_ON : COLOR_LINK_OFF),
        0
    );
}

static void update_side_battery(lv_obj_t *pct, lv_obj_t *icon,
                                lv_obj_t **segs, int percent, bool connected) {
    bool visible = splash_done && connected;

    set_hidden(pct,  !visible);
    set_hidden(icon, true);

    for (int i = 0; i < BAR_SEGMENTS; i++) {
        set_hidden(segs[i], !visible);
    }

    if (visible) {
        lv_label_set_text_fmt(pct, "%d%%", percent);
        update_segment_bar(segs, percent);
    }
}

static void update_battery_visuals(void) {
    update_side_battery(
        left_percent, left_icon, left_segments,
        battery_left, left_connected
    );

    update_side_battery(
        right_percent, right_icon, right_segments,
        battery_right, right_connected
    );
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

            lv_obj_set_size(bt_dots[i], 10, 10);

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
            -14
        );
    }
}

static void update_layer(void) {
    uint8_t layer = zmk_keymap_highest_layer_active();

    lv_label_set_text(
        layer_label,
        layer_name(layer)
    );
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

    lv_label_set_text(layer_label, "Base");

    style_text(
        layer_label,
        COLOR_TEXT,
        &lv_font_montserrat_28
    );

    lv_obj_align(
        layer_label,
        LV_ALIGN_CENTER,
        0,
        -42
    );

    set_hidden(layer_label, true);
}

static void build_segment_bar(lv_obj_t **segments, int x) {

    int total_h =
        (BAR_SEGMENTS * SEG_H) +
        ((BAR_SEGMENTS - 1) * SEG_GAP);

    int start_y = 40 + (total_h / 2);

    for (int i = 0; i < BAR_SEGMENTS; i++) {

        segments[i] = make_box(
            screen,
            BAR_W,
            SEG_H,
            COLOR_BAR_OFF,
            2
        );

        int y =
            start_y -
            (i * (SEG_H + SEG_GAP));

        lv_obj_align(
            segments[i],
            LV_ALIGN_CENTER,
            x,
            y
        );

        set_hidden(segments[i], true);
    }
}

static void build_battery_widgets(void) {

    left_percent = lv_label_create(screen);

    style_text(
        left_percent,
        COLOR_TEXT,
        &lv_font_montserrat_14
    );

    lv_obj_align(
        left_percent,
        LV_ALIGN_CENTER,
        -76,
        -6
    );

    set_hidden(left_percent, true);

    right_percent = lv_label_create(screen);

    style_text(
        right_percent,
        COLOR_TEXT,
        &lv_font_montserrat_14
    );

    lv_obj_align(
        right_percent,
        LV_ALIGN_CENTER,
        76,
        -6
    );

    set_hidden(right_percent, true);

    left_icon = lv_label_create(screen);

    style_text(
        left_icon,
        COLOR_CHARGE,
        &lv_font_montserrat_14
    );

    lv_obj_align(
        left_icon,
        LV_ALIGN_CENTER,
        -76,
        -30
    );

    set_hidden(left_icon, true);

    right_icon = lv_label_create(screen);

    style_text(
        right_icon,
        COLOR_CHARGE,
        &lv_font_montserrat_14
    );

    lv_obj_align(
        right_icon,
        LV_ALIGN_CENTER,
        76,
        -30
    );

    set_hidden(right_icon, true);

    left_link = lv_label_create(screen);

    style_text(
        left_link,
        COLOR_LINK_OFF,
        &lv_font_montserrat_14
    );

    lv_obj_align(
        left_link,
        LV_ALIGN_CENTER,
        -48,
        2
    );

    set_hidden(left_link, true);

    right_link = lv_label_create(screen);

    style_text(
        right_link,
        COLOR_LINK_OFF,
        &lv_font_montserrat_14
    );

    lv_obj_align(
        right_link,
        LV_ALIGN_CENTER,
        48,
        2
    );

    set_hidden(right_link, true);

    build_segment_bar(left_segments, -76);
    build_segment_bar(right_segments, 76);
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
            -14
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

    set_hidden(top_logo,    false);
    set_hidden(layer_label, false);
    set_hidden(left_link,   false);
    set_hidden(right_link,  false);

    for (int i = 0; i < 5; i++) {
        set_hidden(bt_dots[i], false);
    }

    update_layer();
    update_bt_profile();
    update_link_status();
    update_battery_visuals();
}

static int ft_dongle_listener(const zmk_event_t *eh) {

    if (as_zmk_split_peripheral_status_changed(eh)) {
        if (splash_done) {
            update_link_status();
        }
    }

    if (!splash_done) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_layer_state_changed(eh)) {
        update_layer();
    }

    if (as_zmk_ble_active_profile_changed(eh)) {
        update_bt_profile();
    }

    if (as_zmk_peripheral_battery_state_changed(eh)) {

        const struct zmk_peripheral_battery_state_changed *ev =
            as_zmk_peripheral_battery_state_changed(eh);

        /*
         * ZMK wysyła level=0 przy rozłączeniu peryferiów.
         * source 0 = lewa połówka, source 1 = prawa połówka.
         */
        if (ev->source == 0) {
            left_connected  = (ev->state_of_charge > 0);
            battery_left    = ev->state_of_charge;
        } else if (ev->source == 1) {
            right_connected  = (ev->state_of_charge > 0);
            battery_right    = ev->state_of_charge;
        }

        update_link_status();
        update_battery_visuals();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ft_dongle_screen, ft_dongle_listener);

ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_split_peripheral_status_changed);

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
