Wklej cały plik boards/shields/zyraft/src/status_screen.c:

/*
 * FalbaTech FT Dongle status screen
 * GC9A01 240x240 round display
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <zephyr/device.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/keymap.h>
#include <zmk/ble.h>

#include "falbatech_logo.h"

LOG_MODULE_REGISTER(ft_dongle_screen, CONFIG_LOG_DEFAULT_LEVEL);

#define SPLASH_DURATION_MS 3000

static bool splash_done = false;

static struct k_work_delayable splash_to_status_work;
static struct k_work_delayable display_init_work;

static lv_obj_t *screen;
static lv_obj_t *splash_logo;
static lv_obj_t *mini_logo;
static lv_obj_t *layer_label;

static lv_obj_t *battery_box;
static lv_obj_t *left_bar;
static lv_obj_t *right_bar;
static lv_obj_t *left_label;
static lv_obj_t *right_label;
static lv_obj_t *bt_dots[5];

static const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static uint32_t layer_color(uint8_t layer)
{
    switch (layer) {
    case 0: return 0xE6C27A;
    case 1: return 0x00D9FF;
    case 2: return 0xFF9D00;
    case 3: return 0xB65CFF;
    case 4: return 0x00FF90;
    default: return 0xFF3B3B;
    }
}

static void init_display(struct k_work *work)
{
    ARG_UNUSED(work);

    if (device_is_ready(display_dev)) {
        display_blanking_off(display_dev);
        LOG_INF("Display ON");
    } else {
        LOG_ERR("Display not ready");
    }
}

static void show_splash(void)
{
    splash_logo = lv_image_create(screen);
    lv_image_set_src(splash_logo, &falbatech_logo_large);
    lv_obj_align(splash_logo, LV_ALIGN_CENTER, 0, 0);
}

static void hide_splash(struct k_work *work)
{
    ARG_UNUSED(work);

    if (splash_logo) {
        lv_obj_del(splash_logo);
        splash_logo = NULL;
    }

    splash_done = true;

    lv_obj_clear_flag(mini_logo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(battery_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(left_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(right_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(left_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(right_label, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 5; i++) {
        lv_obj_clear_flag(bt_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_logo(void)
{
    mini_logo = lv_image_create(screen);
    lv_image_set_src(mini_logo, &falbatech_logo_small);
    lv_obj_align(mini_logo, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_add_flag(mini_logo, LV_OBJ_FLAG_HIDDEN);
}

static void build_layer_label(void)
{
    layer_label = lv_label_create(screen);
    lv_label_set_text(layer_label, "BASE");
    lv_obj_set_style_text_color(layer_label, lv_color_hex(0xE6C27A), 0);
    lv_obj_set_style_text_font(layer_label, &lv_font_montserrat_28, 0);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, -18);
    lv_obj_add_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
}

static void build_battery_bars(void)
{
    battery_box = lv_obj_create(screen);
    lv_obj_set_size(battery_box, 132, 54);
    lv_obj_align(battery_box, LV_ALIGN_CENTER, 0, 44);
    lv_obj_set_style_bg_opa(battery_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(battery_box, lv_color_hex(0x454545), 0);
    lv_obj_set_style_border_width(battery_box, 1, 0);
    lv_obj_set_style_radius(battery_box, 10, 0);
    lv_obj_clear_flag(battery_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(battery_box, LV_OBJ_FLAG_HIDDEN);

    left_label = lv_label_create(screen);
    lv_label_set_text(left_label, "L 87%");
    lv_obj_set_style_text_color(left_label, lv_color_hex(0xD8D8D8), 0);
    lv_obj_set_style_text_font(left_label, &lv_font_montserrat_12, 0);
    lv_obj_align(left_label, LV_ALIGN_CENTER, -44, 32);
    lv_obj_add_flag(left_label, LV_OBJ_FLAG_HIDDEN);

    left_bar = lv_bar_create(screen);
    lv_obj_set_size(left_bar, 48, 6);
    lv_obj_align(left_bar, LV_ALIGN_CENTER, -35, 52);
    lv_bar_set_range(left_bar, 0, 100);
    lv_bar_set_value(left_bar, 87, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(left_bar, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_bg_color(left_bar, lv_color_hex(0x00FF90), LV_PART_INDICATOR);
    lv_obj_set_style_radius(left_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(left_bar, 3, LV_PART_INDICATOR);
    lv_obj_add_flag(left_bar, LV_OBJ_FLAG_HIDDEN);

    right_label = lv_label_create(screen);
    lv_label_set_text(right_label, "R 92%");
    lv_obj_set_style_text_color(right_label, lv_color_hex(0xD8D8D8), 0);
    lv_obj_set_style_text_font(right_label, &lv_font_montserrat_12, 0);
    lv_obj_align(right_label, LV_ALIGN_CENTER, 44, 32);
    lv_obj_add_flag(right_label, LV_OBJ_FLAG_HIDDEN);

    right_bar = lv_bar_create(screen);
    lv_obj_set_size(right_bar, 48, 6);
    lv_obj_align(right_bar, LV_ALIGN_CENTER, 35, 52);
    lv_bar_set_range(right_bar, 0, 100);
    lv_bar_set_value(right_bar, 92, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(right_bar, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_bg_color(right_bar, lv_color_hex(0x00D9FF), LV_PART_INDICATOR);
    lv_obj_set_style_radius(right_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(right_bar, 3, LV_PART_INDICATOR);
    lv_obj_add_flag(right_bar, LV_OBJ_FLAG_HIDDEN);
}

static void build_bt_dots(void)
{
    for (int i = 0; i < 5; i++) {
        bt_dots[i] = lv_obj_create(screen);
        lv_obj_set_size(bt_dots[i], 8, 8);
        lv_obj_set_style_radius(bt_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(0x303030), 0);
        lv_obj_set_style_border_width(bt_dots[i], 0, 0);
        lv_obj_align(bt_dots[i], LV_ALIGN_BOTTOM_MID, -32 + (i * 16), -22);
        lv_obj_add_flag(bt_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_active_layer(void)
{
    uint8_t layer = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(layer);
    uint32_t color = layer_color(layer);

    if (layer_label) {
        lv_label_set_text(layer_label, name ? name : "BASE");
        lv_obj_set_style_text_color(layer_label, lv_color_hex(color), 0);
    }

    if (left_bar) {
        lv_obj_set_style_bg_color(left_bar, lv_color_hex(color), LV_PART_INDICATOR);
    }
}

static void update_bt_profile(void)
{
    uint8_t active = zmk_ble_active_profile_index();

    for (int i = 0; i < 5; i++) {
        if (bt_dots[i]) {
            uint32_t color = (i == active) ? 0x00FF90 : 0x303030;
            lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(color), 0);
        }
    }
}

static int ft_dongle_event_listener(const zmk_event_t *eh)
{
    if (!splash_done) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_layer_state_changed(eh)) {
        update_active_layer();
    } else if (as_zmk_ble_active_profile_changed(eh)) {
        update_bt_profile();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ft_dongle_screen, ft_dongle_event_listener);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_ble_active_profile_changed);

lv_obj_t *zmk_display_status_screen(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    show_splash();

    build_logo();
    build_layer_label();
    build_battery_bars();
    build_bt_dots();

    update_active_layer();
    update_bt_profile();

    k_work_init_delayable(&display_init_work, init_display);
    k_work_schedule(&display_init_work, K_MSEC(200));

    k_work_init_delayable(&splash_to_status_work, hide_splash);
    k_work_schedule(&splash_to_status_work, K_MSEC(SPLASH_DURATION_MS));

    LOG_INF("FalbaTech dongle screen initialized");
    return screen;
}
