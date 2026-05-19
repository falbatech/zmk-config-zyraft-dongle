/*
 * FT Dongle - status screen z logo FalbaTech
 *
 * Tryby:
 *  - Splash (pierwsze 3 sekundy po boot): pełnoekranowe logo na czarnym tle
 *  - Status (potem): mini-FT u góry + aktywna warstwa + baterie L/R + 5 BT profili
 *
 * UWAGA: Przy boot włączamy backlight PWM (100%) i wyłączamy display blanking
 * - bez tego ekran GC9A01 pozostanie czarny mimo że firmware działa.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/device.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>
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
static lv_obj_t *bat_left_arc;
static lv_obj_t *bat_left_label;
static lv_obj_t *bat_right_arc;
static lv_obj_t *bat_right_label;
static lv_obj_t *bt_dots[5];

/* PWM backlight - referencja do node disp_bl z devicetree */
static const struct pwm_dt_spec backlight_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(disp_bl));

/* Display device - referencja przez chosen zephyr,display */
static const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static void init_display_and_backlight(struct k_work *work) {
    /* Wyłącz display blanking - bez tego GC9A01 może zostać czarny */
    if (device_is_ready(display_dev)) {
        int ret = display_blanking_off(display_dev);
        if (ret) {
            LOG_ERR("Failed to disable display blanking: %d", ret);
        } else {
            LOG_INF("Display blanking OFF");
        }
    } else {
        LOG_ERR("Display device not ready");
    }

    /* Włącz backlight na 100% */
    if (device_is_ready(backlight_pwm.dev)) {
        int ret = pwm_set_pulse_dt(&backlight_pwm, backlight_pwm.period);
        if (ret) {
            LOG_ERR("Failed to set backlight PWM: %d", ret);
        } else {
            LOG_INF("Backlight ON (PWM 100%%)");
        }
    } else {
        LOG_ERR("Backlight PWM device not ready");
    }
}

static void show_splash(void) {
    splash_logo = lv_image_create(screen);
    lv_image_set_src(splash_logo, &falbatech_logo_large);
    lv_obj_align(splash_logo, LV_ALIGN_CENTER, 0, 0);
}

static void hide_splash(struct k_work *work) {
    if (splash_logo) {
        lv_obj_del(splash_logo);
        splash_logo = NULL;
    }
    splash_done = true;
    LOG_INF("Splash done, status screen active");

    if (mini_logo) lv_obj_clear_flag(mini_logo, LV_OBJ_FLAG_HIDDEN);
    if (layer_label) lv_obj_clear_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
    if (bat_left_arc) lv_obj_clear_flag(bat_left_arc, LV_OBJ_FLAG_HIDDEN);
    if (bat_left_label) lv_obj_clear_flag(bat_left_label, LV_OBJ_FLAG_HIDDEN);
    if (bat_right_arc) lv_obj_clear_flag(bat_right_arc, LV_OBJ_FLAG_HIDDEN);
    if (bat_right_label) lv_obj_clear_flag(bat_right_label, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 5; i++) {
        if (bt_dots[i]) lv_obj_clear_flag(bt_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_mini_logo(void) {
    mini_logo = lv_image_create(screen);
    lv_image_set_src(mini_logo, &falbatech_logo_small);
    lv_obj_align(mini_logo, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_add_flag(mini_logo, LV_OBJ_FLAG_HIDDEN);
}

static void build_layer_label(void) {
    layer_label = lv_label_create(screen);
    lv_label_set_text(layer_label, "BASE");
    lv_obj_set_style_text_color(layer_label, lv_color_hex(0x00FF90), 0);
    lv_obj_set_style_text_font(layer_label, &lv_font_montserrat_28, 0);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
}

static void build_battery_widgets(void) {
    bat_left_arc = lv_arc_create(screen);
    lv_obj_set_size(bat_left_arc, 50, 50);
    lv_obj_align(bat_left_arc, LV_ALIGN_CENTER, -55, 50);
    lv_arc_set_rotation(bat_left_arc, 270);
    lv_arc_set_bg_angles(bat_left_arc, 0, 360);
    lv_arc_set_value(bat_left_arc, 0);
    lv_obj_set_style_arc_color(bat_left_arc, lv_color_hex(0x3B5C68), LV_PART_MAIN);
    lv_obj_set_style_arc_color(bat_left_arc, lv_color_hex(0x00FF90), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(bat_left_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bat_left_arc, 4, LV_PART_INDICATOR);
    lv_obj_remove_style(bat_left_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(bat_left_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bat_left_arc, LV_OBJ_FLAG_HIDDEN);

    bat_left_label = lv_label_create(screen);
    lv_label_set_text(bat_left_label, "--%");
    lv_obj_set_style_text_color(bat_left_label, lv_color_hex(0xA2B5B8), 0);
    lv_obj_set_style_text_font(bat_left_label, &lv_font_montserrat_14, 0);
    lv_obj_align(bat_left_label, LV_ALIGN_CENTER, -55, 50);
    lv_obj_add_flag(bat_left_label, LV_OBJ_FLAG_HIDDEN);

    bat_right_arc = lv_arc_create(screen);
    lv_obj_set_size(bat_right_arc, 50, 50);
    lv_obj_align(bat_right_arc, LV_ALIGN_CENTER, 55, 50);
    lv_arc_set_rotation(bat_right_arc, 270);
    lv_arc_set_bg_angles(bat_right_arc, 0, 360);
    lv_arc_set_value(bat_right_arc, 0);
    lv_obj_set_style_arc_color(bat_right_arc, lv_color_hex(0x3B5C68), LV_PART_MAIN);
    lv_obj_set_style_arc_color(bat_right_arc, lv_color_hex(0x00FF90), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(bat_right_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bat_right_arc, 4, LV_PART_INDICATOR);
    lv_obj_remove_style(bat_right_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(bat_right_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bat_right_arc, LV_OBJ_FLAG_HIDDEN);

    bat_right_label = lv_label_create(screen);
    lv_label_set_text(bat_right_label, "--%");
    lv_obj_set_style_text_color(bat_right_label, lv_color_hex(0xA2B5B8), 0);
    lv_obj_set_style_text_font(bat_right_label, &lv_font_montserrat_14, 0);
    lv_obj_align(bat_right_label, LV_ALIGN_CENTER, 55, 50);
    lv_obj_add_flag(bat_right_label, LV_OBJ_FLAG_HIDDEN);
}

static void build_bt_dots(void) {
    for (int i = 0; i < 5; i++) {
        bt_dots[i] = lv_obj_create(screen);
        lv_obj_set_size(bt_dots[i], 8, 8);
        lv_obj_set_style_radius(bt_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(0x3B5C68), 0);
        lv_obj_set_style_border_width(bt_dots[i], 0, 0);
        lv_obj_align(bt_dots[i], LV_ALIGN_CENTER, -30 + (i * 15), 95);
        lv_obj_add_flag(bt_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_active_layer(void) {
    if (!layer_label) return;
    uint8_t layer = zmk_keymap_highest_layer_active();
    const char *layer_name = zmk_keymap_layer_name(layer);
    lv_label_set_text(layer_label, layer_name ? layer_name : "BASE");
}

static void update_bt_profile(void) {
    uint8_t active = zmk_ble_active_profile_index();
    for (int i = 0; i < 5; i++) {
        if (bt_dots[i]) {
            uint32_t color = (i == active) ? 0x00FF90 : 0x3B5C68;
            lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(color), 0);
        }
    }
}

static int ft_dongle_event_listener(const zmk_event_t *eh) {
    if (!splash_done) return ZMK_EV_EVENT_BUBBLE;

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

lv_obj_t *zmk_display_status_screen(void) {
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    show_splash();

    build_mini_logo();
    build_layer_label();
    build_battery_widgets();
    build_bt_dots();

    update_active_layer();
    update_bt_profile();

    /* KLUCZOWE: włącz backlight + wyłącz display blanking
     * Bez tego ekran GC9A01 zostaje czarny mimo że LVGL rysuje
     * Delayed work żeby LVGL miało czas narysować logo zanim ekran się włączy */
    k_work_init_delayable(&display_init_work, init_display_and_backlight);
    k_work_schedule(&display_init_work, K_MSEC(200));

    /* Po 3 sek splash znika, pokazujemy status */
    k_work_init_delayable(&splash_to_status_work, hide_splash);
    k_work_schedule(&splash_to_status_work, K_MSEC(SPLASH_DURATION_MS));

    LOG_INF("FT Dongle screen initialized");
    return screen;
}
