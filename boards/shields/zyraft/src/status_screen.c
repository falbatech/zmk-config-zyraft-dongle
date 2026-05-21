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
#define SPLASH_DURATION_MS    2500
#define SLEEP_TIMEOUT_MS      30000
#define LAYER_FADE_OUT_MS     150
#define LAYER_FADE_IN_MS      200
#define BATTERY_FADE_MS       400
#define CHARGE_TICK_MS        140
#define SLEEP_FADE_MS         600
#define SLEEP_BREATHE_MS      1800

/* ── Colors ───────────────────────────────────────────────────── */
#define COLOR_BG      0x000000
#define COLOR_TEXT    0xFFFFFF
#define COLOR_ON      0xE00039   /* → ZIELONY */
#define COLOR_OFF     0x884890   /* → SZARY   */

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
static uint8_t charge_tick     = 0;

/* Layer animation */
static bool layer_anim_busy = false;
static char pending_layer[32] = "Base";

/* ── Work / timers ────────────────────────────────────────────── */
static struct k_work_delayable splash_work;
static lv_timer_t *sleep_timer  = NULL;
static lv_timer_t *charge_timer = NULL;

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

/* ═══════════════════ Animation core ════════════════════════════ */

static void opa_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void fade(lv_obj_t *obj, lv_opa_t from, lv_opa_t to,
                 uint32_t ms, uint32_t delay_ms,
                 lv_anim_ready_cb_t done_cb)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay_ms);
    if (done_cb) lv_anim_set_ready_cb(&a, done_cb);
    lv_anim_start(&a);
}

/* Breathing: oscillates opacity forever (for sleep screen) */
static void start_breathe(lv_obj_t *obj)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_duration(&a, SLEEP_BREATHE_MS);
    lv_anim_set_playback_duration(&a, SLEEP_BREATHE_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* ═══════════════════ Layer transition ══════════════════════════ */

static void layer_fade_in_done(lv_anim_t *a)
{
    (void)a;
    layer_anim_busy = false;
}

static void layer_fade_out_done(lv_anim_t *a)
{
    (void)a;
    lv_label_set_text(layer_label, pending_layer);
    fade(layer_label, 0, LV_OPA_COVER, LAYER_FADE_IN_MS, 0, layer_fade_in_done);
}

static void update_layer(void)
{
    uint8_t idx = zmk_keymap_highest_layer_active();
    strncpy(pending_layer, layer_name(idx), sizeof(pending_layer) - 1);
    pending_layer[sizeof(pending_layer) - 1] = '\0';

    if (!splash_done || is_sleeping) return;

    /* Already animating — the ready cb will pick up pending_layer */
    if (layer_anim_busy) return;

    layer_anim_busy = true;
    fade(layer_label, LV_OPA_COVER, 0,
         LAYER_FADE_OUT_MS, 0, layer_fade_out_done);
}

/* ═══════════════════ Battery bars ══════════════════════════════ */

static void draw_segment_bar(lv_obj_t **segs, int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    int filled = (percent + 9) / 10;

    for (int i = 0; i < BAR_SEGMENTS; i++) {
        if (!segs[i]) continue;
        uint32_t color;
        if (i < filled) {
            /* Shimmer: one segment lights up white, cycles bottom→top */
            int shimmer = (filled > 0) ? (charge_tick % filled) : 0;
            color = (i == shimmer) ? COLOR_TEXT : COLOR_ON;
        } else {
            color = COLOR_OFF;
        }
        lv_obj_set_style_bg_color(segs[i], lv_color_hex(color), 0);
    }
}

static void update_side_battery(lv_obj_t *pct, lv_obj_t **segs,
                                int percent, bool connected)
{
    bool visible    = splash_done && connected && !is_sleeping;
    bool was_hidden = lv_obj_has_flag(pct, LV_OBJ_FLAG_HIDDEN);

    if (!visible) {
        set_hidden(pct, true);
        for (int i = 0; i < BAR_SEGMENTS; i++) set_hidden(segs[i], true);
        return;
    }

    /* Unhide */
    set_hidden(pct, false);
    for (int i = 0; i < BAR_SEGMENTS; i++) set_hidden(segs[i], false);

    if (was_hidden) {
        /* Fade in — stagger segments for cascading effect */
        lv_obj_set_style_opa(pct, 0, 0);
        fade(pct, 0, LV_OPA_COVER, BATTERY_FADE_MS, 0, NULL);

        for (int i = 0; i < BAR_SEGMENTS; i++) {
            lv_obj_set_style_opa(segs[i], 0, 0);
            fade(segs[i], 0, LV_OPA_COVER,
                 BATTERY_FADE_MS, (uint32_t)i * 25, NULL);
        }
    }

    lv_label_set_text_fmt(pct, "%d%%", percent);
    draw_segment_bar(segs, percent);
}

static void update_battery_visuals(void)
{
    update_side_battery(left_percent,  left_segments,  battery_left,  left_connected);
    update_side_battery(right_percent, right_segments, battery_right, right_connected);
}

/* ═══════════════════ BT dots ═══════════════════════════════════ */

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

/* ═══════════════════ Link status ═══════════════════════════════ */

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

static void set_status_ui_hidden(bool hidden)
{
    set_hidden(top_logo,     hidden);
    set_hidden(layer_label,  hidden);
    set_hidden(left_link,    hidden);
    set_hidden(right_link,   hidden);
    for (int i = 0; i < 5; i++) set_hidden(bt_dots[i], hidden);
}

static void enter_sleep(void)
{
    if (is_sleeping || !splash_done) return;
    is_sleeping = true;

    /* Hide main UI */
    set_status_ui_hidden(true);
    set_hidden(left_percent,  true);
    set_hidden(right_percent, true);
    for (int i = 0; i < BAR_SEGMENTS; i++) {
        set_hidden(left_segments[i],  true);
        set_hidden(right_segments[i], true);
    }

    /* Build sleep logo once */
    if (!sleep_logo) {
        sleep_logo = lv_image_create(screen);
        lv_image_set_src(sleep_logo, &falbatech_logo_large);
        lv_obj_align(sleep_logo, LV_ALIGN_CENTER, 0, 0);
    }

    /* Fade in, then breathe */
    lv_anim_delete(sleep_logo, opa_anim_cb);
    lv_obj_set_style_opa(sleep_logo, 0, 0);
    set_hidden(sleep_logo, false);
    fade(sleep_logo, 0, LV_OPA_COVER, SLEEP_FADE_MS, 0, NULL);

    /* Start breathing after fade-in completes */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_set_var(&a, sleep_logo);
    lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_duration(&a, SLEEP_BREATHE_MS);
    lv_anim_set_playback_duration(&a, SLEEP_BREATHE_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_delay(&a, SLEEP_FADE_MS);   /* wait for fade-in */
    lv_anim_start(&a);
}

static void wake_from_sleep(void)
{
    if (!is_sleeping) return;
    is_sleeping = false;

    /* Stop breathing, hide sleep logo */
    if (sleep_logo) {
        lv_anim_delete(sleep_logo, opa_anim_cb);
        set_hidden(sleep_logo, true);
    }

    /* Restore main UI */
    set_status_ui_hidden(false);

    /* Layer: restore immediately (no fade-out animation on wake) */
    layer_anim_busy = false;
    lv_obj_set_style_opa(layer_label, LV_OPA_COVER, 0);
    lv_label_set_text(layer_label, pending_layer);

    update_bt_profile();
    update_link_status();
    update_battery_visuals();   /* battery fades in if connected */

    mark_activity();
}

/* ═══════════════════ LVGL timer callbacks ══════════════════════ */

static void sleep_check_cb(lv_timer_t *t)
{
    (void)t;
    if (!splash_done || is_sleeping) return;
    if ((k_uptime_get() - last_activity) >= SLEEP_TIMEOUT_MS) {
        enter_sleep();
    }
}

static void charge_anim_cb(lv_timer_t *t)
{
    (void)t;
    if (!splash_done || is_sleeping) return;
    charge_tick++;
    if (left_connected  && battery_left  > 0)
        draw_segment_bar(left_segments,  battery_left);
    if (right_connected && battery_right > 0)
        draw_segment_bar(right_segments, battery_right);
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

    /* Set initial layer text directly (no fade-out needed from blank) */
    uint8_t idx = zmk_keymap_highest_layer_active();
    strncpy(pending_layer, layer_name(idx), sizeof(pending_layer) - 1);
    pending_layer[sizeof(pending_layer) - 1] = '\0';
    lv_label_set_text(layer_label, pending_layer);

    /* Show main UI elements */
    set_status_ui_hidden(false);

    /* Fade everything in from transparent */
    lv_obj_set_style_opa(top_logo,    0, 0);
    lv_obj_set_style_opa(layer_label, 0, 0);
    fade(top_logo,    0, LV_OPA_COVER, BATTERY_FADE_MS, 0,   NULL);
    fade(layer_label, 0, LV_OPA_COVER, BATTERY_FADE_MS, 100, NULL);

    /* L/R labels fade in */
    lv_obj_set_style_opa(left_link,  0, 0);
    lv_obj_set_style_opa(right_link, 0, 0);
    fade(left_link,  0, LV_OPA_COVER, BATTERY_FADE_MS, 200, NULL);
    fade(right_link, 0, LV_OPA_COVER, BATTERY_FADE_MS, 200, NULL);

    /* BT dots fade in */
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_opa(bt_dots[i], 0, 0);
        fade(bt_dots[i], 0, LV_OPA_COVER,
             BATTERY_FADE_MS, (uint32_t)(250 + i * 40), NULL);
    }

    update_bt_profile();
    update_link_status();
    update_battery_visuals();
}

/* ═══════════════════ Event listener ═══════════════════════════ */

static int ft_dongle_listener(const zmk_event_t *eh)
{
    /* Split central status — handled before splash gate */
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

    /* Any event wakes the display */
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

    /* Continuous LVGL timers (safe — created in display thread context) */
    sleep_timer  = lv_timer_create(sleep_check_cb, 1000,        NULL);
    charge_timer = lv_timer_create(charge_anim_cb, CHARGE_TICK_MS, NULL);

    k_work_init_delayable(&splash_work, show_status);
    k_work_schedule(&splash_work, K_MSEC(SPLASH_DURATION_MS));

    return screen;
}
