/*
 * Zyra FT Dongle - FalbaTech Status Screen
 * GC9A01 240x240
 *
 * Kolory lv_color_hex() — wartości nieintuicyjne (BGR + byte-swap SPI):
 *   0xE00039 → ZIELONY     0x884890 → SZARY
 *   0xFFFFFF → BIAŁY       0x000000 → CZARNY
 * Surowe dane obrazów (lv_image_dsc_t): RGB565 big-endian.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_central_status_changed.h>

#include <zmk/keymap.h>
#include <zmk/ble.h>

#include "falbatech_logo.h"

LV_IMG_DECLARE(zmk_studio_logo);

LOG_MODULE_REGISTER(ft_dongle_screen, CONFIG_LOG_DEFAULT_LEVEL);

/* ── Timing ───────────────────────────────────────────────────── */
#define SPLASH_DURATION_MS   2500
#define SLEEP_TIMEOUT_MS     30000

/* ── Colors ───────────────────────────────────────────────────── */
#define COLOR_BG     0x000000
#define COLOR_TEXT   0xFFFFFF
#define COLOR_ON     0xE00039   /* → ZIELONY */
#define COLOR_OFF    0x884890   /* → SZARY   */

/* ── Bar geometry ─────────────────────────────────────────────── */
#define BAR_SEGMENTS  10
#define BAR_W         18
#define SEG_H          5
#define SEG_GAP        2

/* ── State ────────────────────────────────────────────────────── */
static bool    splash_done     = false;
static bool    is_sleeping     = false;
static bool    left_connected  = false;
static bool    right_connected = false;
static int     battery_left    = 0;
static int     battery_right   = 0;
static int64_t last_activity   = 0;

/* ── Work / timers ────────────────────────────────────────────── */
static struct k_work_delayable splash_work;
static lv_timer_t *sleep_timer = NULL;

/* ── Widgets ──────────────────────────────────────────────────── */
static lv_obj_t *screen;
static lv_obj_t *splash_logo;
static lv_obj_t *sleep_logo   = NULL;
static lv_obj_t *top_logo;
static lv_obj_t *layer_label;
static lv_obj_t *left_percent;
static lv_obj_t *right_percent;
static lv_obj_t *left_link;
static lv_obj_t *right_link;
static lv_obj_t *left_segments[BAR_SEGMENTS];
static lv_obj_t *right_segments[BAR_SEGMENTS];
static lv_obj_t *bt_dots[5];

/* ═══════════════════ Helpers ═══════════════════════════════════ */

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (!obj) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void style_text(lv_obj_t *obj, uint32_t color, const lv_font_t *font)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(obj, font, 0);
}

static lv_obj_t *make_box(lv_obj_t *parent, int w, int h,
                          uint32_t color, int radius)
{
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

static const char *layer_name(uint8_t layer)
{
    const char *name = zmk_keymap_layer_name(layer);
    if (name) return name;
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

static void mark_activity(void)
{
    last_activity = k_uptime_get();
}

/* ═══════════════════ Update functions ══════════════════════════ */

static void update_layer(void)
{
    uint8_t idx = zmk_keymap_highest_layer_active();
    lv_label_set_text(layer_label, layer_name(idx));
}

static void draw_segment_bar(lv_obj_t **segs, int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    int filled = (percent + 9) / 10;

    for (int i = 0; i < BAR_SEGMENTS; i++) {
        if (!segs[i]) continue;
        uint32_t color = (i < filled) ? COLOR_ON : COLOR_OFF;
        lv_obj_set_style_bg_color(segs[i], lv_color_hex(color), 0);
    }
}

static void update_side_battery(lv_obj_t *pct, lv_obj_t **segs,
                                int percent, bool connected)
{
    bool visible = splash_done && connected && !is_sleeping;

    set_hidden(pct, !visible);
    for (int i = 0; i < BAR_SEGMENTS; i++) set_hidden(segs[i], !visible);

    if (visible) {
        lv_label_set_text_fmt(pct, "%d%%", percent);
        draw_segment_bar(segs, percent);
    }
}

static void update_battery_visuals(void)
{
    update_side_battery(left_percent,  left_segments,  battery_left,  left_connected);
    update_side_battery(right_percent, right_segments, battery_right, right_connected);
}

static void update_bt_profile(void)
{
    uint8_t active = zmk_ble_active_profile_index();
    for (int i = 0; i < 5; i++) {
        if (!bt_dots[i]) continue;
        if (i == active) {
            lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(COLOR_ON), 0);
            lv_obj_set_size(bt_dots[i], 10, 10);
        } else {
            lv_obj_set_style_bg_color(bt_dots[i], lv_color_hex(COLOR_OFF), 0);
            lv_obj_set_size(bt_dots[i], 8, 8);
        }
        lv_obj_align(bt_dots[i], LV_ALIGN_BOTTOM_MID, -32 + (i * 16), -14);
    }
}

static void update_link_status(void)
{
    lv_label_set_text(left_link,  "L");
    lv_label_set_text(right_link, "R");
    lv_obj_set_style_text_color(left_link,
        lv_color_hex(left_connected  ? COLOR_ON : COLOR_OFF), 0);
    lv_obj_set_style_text_color(right_link,
        lv_color_hex(right_connected ? COLOR_ON : COLOR_OFF), 0);
}

/* ═══════════════════ Sleep ═════════════════════════════════════ */

static void set_main_ui_hidden(bool hidden)
{
    set_hidden(top_logo,      hidden);
    set_hidden(layer_label,   hidden);
    set_hidden(left_link,     hidden);
    set_hidden(right_link,    hidden);
    set_hidden(left_percent,  hidden);
    set_hidden(right_percent, hidden);
    for (int i = 0; i < BAR_SEGMENTS; i++) {
        set_hidden(left_segments[i],  hidden);
        set_hidden(right_segments[i], hidden);
    }
    for (int i = 0; i < 5; i++) set_hidden(bt_dots[i], hidden);
}

static void enter_sleep(void)
{
    if (is_sleeping || !splash_done) return;
    is_sleeping = true;

    set_main_ui_hidden(true);

    /* Zbuduj sleep logo tylko raz */
    if (!sleep_logo) {
        sleep_logo = lv_image_create(screen);
        lv_image_set_src(sleep_logo, &falbatech_logo_small);
        lv_obj_align(sleep_logo, LV_ALIGN_CENTER, 0, 0);

        /* Szary odcień — całkowita zmiana koloru na szary */
        lv_obj_set_style_image_recolor(sleep_logo,
            lv_color_hex(0x888888), 0);
        lv_obj_set_style_image_recolor_opa(sleep_logo, LV_OPA_COVER, 0);

        /* Przyciemnione do ~40% */
        lv_obj_set_style_opa(sleep_logo, LV_OPA_40, 0);
    }

    set_hidden(sleep_logo, false);
}

static void wake_from_sleep(void)
{
    if (!is_sleeping) return;
    is_sleeping = false;

    set_hidden(sleep_logo, true);
    set_main_ui_hidden(false);

    update_layer();
    update_bt_profile();
    update_link_status();
    update_battery_visuals();

    mark_activity();
}

static void sleep_check_cb(lv_timer_t *t)
{
    (void)t;
    if (!splash_done || is_sleeping) return;
    if ((k_uptime_get() - last_activity) >= SLEEP_TIMEOUT_MS) {
        enter_sleep();
    }
}

/* ═══════════════════ Build UI ══════════════════════════════════ */

static void build_splash(void)
{
    splash_logo = lv_image_create(screen);
    lv_image_set_src(splash_logo, &falbatech_logo_large);
    lv_obj_align(splash_logo, LV_ALIGN_CENTER, 0, 0);
}

static void build_top_logo(void)
{
    top_logo = lv_image_create(screen);
    lv_image_set_src(top_logo, &zmk_studio_logo);
    lv_obj_align(top_logo, LV_ALIGN_TOP_MID, 20, 10);
    set_hidden(top_logo, true);
}

static void build_layer_label(void)
{
    layer_label = lv_label_create(screen);
    lv_label_set_text(layer_label, "Base");
    style_text(layer_label, COLOR_TEXT, &lv_font_montserrat_28);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, -42);
    set_hidden(layer_label, true);
}

static void build_segment_bar(lv_obj_t **segs, int x)
{
    int total_h = (BAR_SEGMENTS * SEG_H) + ((BAR_SEGMENTS - 1) * SEG_GAP);
    int start_y = 40 + (total_h / 2);

    for (int i = 0; i < BAR_SEGMENTS; i++) {
        segs[i] = make_box(screen, BAR_W, SEG_H, COLOR_OFF, 2);
        int y = start_y - (i * (SEG_H + SEG_GAP));
        lv_obj_align(segs[i], LV_ALIGN_CENTER, x, y);
        set_hidden(segs[i], true);
    }
}

static void build_battery_widgets(void)
{
    left_percent = lv_label_create(screen);
    style_text(left_percent, COLOR_TEXT, &lv_font_montserrat_14);
    lv_obj_align(left_percent, LV_ALIGN_CENTER, -76, -6);
    set_hidden(left_percent, true);

    right_percent = lv_label_create(screen);
    style_text(right_percent, COLOR_TEXT, &lv_font_montserrat_14);
    lv_obj_align(right_percent, LV_ALIGN_CENTER, 76, -6);
    set_hidden(right_percent, true);

    left_link = lv_label_create(screen);
    style_text(left_link, COLOR_OFF, &lv_font_montserrat_14);
    lv_obj_align(left_link, LV_ALIGN_CENTER, -48, 2);
    set_hidden(left_link, true);

    right_link = lv_label_create(screen);
    style_text(right_link, COLOR_OFF, &lv_font_montserrat_14);
    lv_obj_align(right_link, LV_ALIGN_CENTER, 48, 2);
    set_hidden(right_link, true);

    build_segment_bar(left_segments,  -76);
    build_segment_bar(right_segments,  76);
}

static void build_bt_dots(void)
{
    for (int i = 0; i < 5; i++) {
        bt_dots[i] = make_box(screen, 8, 8, COLOR_OFF, LV_RADIUS_CIRCLE);
        lv_obj_align(bt_dots[i], LV_ALIGN_BOTTOM_MID, -32 + (i * 16), -14);
        set_hidden(bt_dots[i], true);
    }
}

/* ═══════════════════ Splash → main ════════════════════════════ */

static void show_status(struct k_work *work)
{
    if (splash_logo) {
        lv_obj_del(splash_logo);
        splash_logo = NULL;
    }

    splash_done = true;
    mark_activity();

    set_hidden(top_logo,    false);
    set_hidden(layer_label, false);
    set_hidden(left_link,   false);
    set_hidden(right_link,  false);
    for (int i = 0; i < 5; i++) set_hidden(bt_dots[i], false);

    update_layer();
    update_bt_profile();
    update_link_status();
    update_battery_visuals();
}

/* ═══════════════════ Event listener ═══════════════════════════ */

static int ft_dongle_listener(const zmk_event_t *eh)
{
    if (as_zmk_split_central_status_changed(eh)) {
        const struct zmk_split_central_status_changed *ev =
            as_zmk_split_central_status_changed(eh);

        if (ev->slot == 0) {
            left_connected = ev->connected;
            if (!ev->connected) battery_left = 0;
        } else if (ev->slot == 1) {
            right_connected = ev->connected;
            if (!ev->connected) battery_right = 0;
        }

        if (is_sleeping) wake_from_sleep();
        else             mark_activity();

        if (splash_done) {
            update_link_status();
            update_battery_visuals();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!splash_done) return ZMK_EV_EVENT_BUBBLE;

    if (is_sleeping) {
        wake_from_sleep();
        return ZMK_EV_EVENT_BUBBLE;
    }
    mark_activity();

    if (as_zmk_layer_state_changed(eh)) {
        update_layer();
    }

    if (as_zmk_ble_active_profile_changed(eh)) {
        update_bt_profile();
    }

    if (as_zmk_peripheral_battery_state_changed(eh)) {
        const struct zmk_peripheral_battery_state_changed *ev =
            as_zmk_peripheral_battery_state_changed(eh);

        if (ev->source == 0)      battery_left  = ev->state_of_charge;
        else if (ev->source == 1) battery_right = ev->state_of_charge;

        update_battery_visuals();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ft_dongle_screen, ft_dongle_listener);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_split_central_status_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_peripheral_battery_state_changed);

/* ═══════════════════ Init ══════════════════════════════════════ */

lv_obj_t *zmk_display_status_screen(void)
{
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

    update_bt_profile();

    sleep_timer = lv_timer_create(sleep_check_cb, 1000, NULL);

    k_work_init_delayable(&splash_work, show_status);
    k_work_schedule(&splash_work, K_MSEC(SPLASH_DURATION_MS));

    return screen;
}
