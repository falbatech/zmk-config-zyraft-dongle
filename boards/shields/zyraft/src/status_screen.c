/*
 * Zyra FT Dongle - FalbaTech Status Screen
 * GC9A01 240x240
 *
 * Kolory lv_color_hex() — wartości nieintuicyjne (BGR + byte-swap SPI):
 *   0xE00039 → ZIELONY     0x884890 → SZARY
 *   0xFFFFFF → BIAŁY       0x000000 → CZARNY
 * Surowe dane obrazów (lv_image_dsc_t): RGB565 big-endian.
 *
 * Stany ekranu:
 *   ACTIVE  — pełne UI, overlay 0%
 *   IDLE    — ZMK logo + layer + % baterii, overlay 80% (20% jasności)
 *   SLEEP   — tylko ZMK logo, overlay 92% (~8% jasności)
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
#include <zmk/events/hid_indicators_changed.h>

#include <zmk/keymap.h>
#include <zmk/ble.h>
#include <zmk/hid_indicators.h>
#include <zmk/hid.h>

#include "falbatech_logo.h"

LV_IMG_DECLARE(zmk_studio_logo);

LOG_MODULE_REGISTER(ft_dongle_screen, CONFIG_LOG_DEFAULT_LEVEL);

/* ── Timing ───────────────────────────────────────────────────── */
#define SPLASH_DURATION_MS   2500
#define IDLE_TIMEOUT_MS      25000   /* 25s bez aktywności → IDLE  */
#define SLEEP_TIMEOUT_MS     90000   /* 90s bez aktywności → SLEEP */

/* ── Dim overlay opacity ──────────────────────────────────────── */
#define DIM_ACTIVE   LV_OPA_0    /*   0% czerni — pełna jasność   */
#define DIM_IDLE     LV_OPA_80   /*  80% czerni — 20% jasności    */
#define DIM_SLEEP    234         /*  92% czerni — ~8% jasności    */

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

/* ── Display state ────────────────────────────────────────────── */
typedef enum { DISP_ACTIVE, DISP_IDLE, DISP_SLEEP } disp_state_t;
static disp_state_t disp_state = DISP_ACTIVE;

/* ── State ────────────────────────────────────────────────────── */
static bool                 splash_done     = false;
static bool                 left_connected  = false;
static bool                 right_connected = false;
static int                  battery_left    = 0;
static int                  battery_right   = 0;
static int64_t              last_activity   = 0;
static zmk_hid_indicators_t hid_indicators  = 0;

/* ── Work / timers ────────────────────────────────────────────── */
static struct k_work_delayable splash_work;
static lv_timer_t *sleep_timer = NULL;

/* ── Widgets ──────────────────────────────────────────────────── */
static lv_obj_t *screen;
static lv_obj_t *dim_overlay;      /* czarna nakładka regulująca jasność */
static lv_obj_t *splash_logo;
static lv_obj_t *top_logo;         /* ZMK Studio — widoczne w każdym stanie */
static lv_obj_t *layer_label;      /* ACTIVE + IDLE */
static lv_obj_t *left_percent;     /* ACTIVE + IDLE (jeśli połączony) */
static lv_obj_t *right_percent;
static lv_obj_t *left_link;        /* ACTIVE only */
static lv_obj_t *right_link;
static lv_obj_t *left_segments[BAR_SEGMENTS];   /* ACTIVE only */
static lv_obj_t *right_segments[BAR_SEGMENTS];
static lv_obj_t *bt_dots[5];       /* ACTIVE only — 5 profili BT hosta */
static lv_obj_t *caps_indicator;   /* ACTIVE only — zielona pigułka CAPS */
static lv_obj_t *num_indicator;    /* ACTIVE only — zielona pigułka NUM  */

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

static void set_overlay(lv_opa_t opa)
{
    lv_obj_set_style_bg_opa(dim_overlay, opa, 0);
}

/* ═══════════════════ Update functions ══════════════════════════ */

/* Widoczność zależy od stanu — wywoływane po każdej zmianie stanu/danych */

static void update_layer(void)
{
    /* ACTIVE + IDLE */
    bool show = splash_done && (disp_state != DISP_SLEEP);
    set_hidden(layer_label, !show);
    if (show) {
        uint8_t idx = zmk_keymap_highest_layer_active();
        lv_label_set_text(layer_label, layer_name(idx));
    }
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
    /* Procent: ACTIVE + IDLE | Segmenty: tylko ACTIVE */
    bool show_pct  = splash_done && connected && (disp_state != DISP_SLEEP);
    bool show_segs = splash_done && connected && (disp_state == DISP_ACTIVE);

    set_hidden(pct, !show_pct);
    for (int i = 0; i < BAR_SEGMENTS; i++) set_hidden(segs[i], !show_segs);

    if (show_pct)  lv_label_set_text_fmt(pct, "%d%%", percent);
    if (show_segs) draw_segment_bar(segs, percent);
}

static void update_battery_visuals(void)
{
    update_side_battery(left_percent,  left_segments,  battery_left,  left_connected);
    update_side_battery(right_percent, right_segments, battery_right, right_connected);
}

static void update_link_status(void)
{
    /* Tylko ACTIVE */
    bool show = splash_done && (disp_state == DISP_ACTIVE);
    set_hidden(left_link,  !show);
    set_hidden(right_link, !show);
    if (!show) return;

    lv_label_set_text(left_link,  "L");
    lv_label_set_text(right_link, "R");
    lv_obj_set_style_text_color(left_link,
        lv_color_hex(left_connected  ? COLOR_ON : COLOR_OFF), 0);
    lv_obj_set_style_text_color(right_link,
        lv_color_hex(right_connected ? COLOR_ON : COLOR_OFF), 0);
}


static void update_bt_profile(void)
{
    /* Tylko ACTIVE */
    bool show = splash_done && (disp_state == DISP_ACTIVE);
    for (int i = 0; i < 5; i++) set_hidden(bt_dots[i], !show);
    if (!show) return;

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

static void update_hid_indicators(void)
{
    /* Tylko ACTIVE — pigułki CAPS / NUM */
    bool show = splash_done && (disp_state == DISP_ACTIVE);
    set_hidden(caps_indicator, !(show && (hid_indicators & HID_KBD_LED_CAPS_LOCK)));
    set_hidden(num_indicator,  !(show && (hid_indicators & HID_KBD_LED_NUM_LOCK)));
}

static void refresh_all(void)
{
    update_layer();
    update_bt_profile();
    update_link_status();
    update_battery_visuals();
    update_hid_indicators();
}

/* ═══════════════════ State transitions ═════════════════════════ */

static void enter_active(void)
{
    disp_state = DISP_ACTIVE;
    set_overlay(DIM_ACTIVE);
    lv_timer_set_period(sleep_timer, 1000);  /* normalny polling */

    set_hidden(top_logo, false);
    refresh_all();
    mark_activity();
}

static void enter_idle(void)
{
    disp_state = DISP_IDLE;
    set_overlay(DIM_IDLE);
    /* Zawartość: ZMK logo + layer + % baterii — reszta ukryta przez update fns */
    refresh_all();
}

static void enter_sleep(void)
{
    disp_state = DISP_SLEEP;
    set_overlay(DIM_SLEEP);
    lv_timer_set_period(sleep_timer, 5000);  /* polling co 5s — minimalne CPU */
    /* Zawartość: tylko ZMK logo (widoczne ~8%) — reszta ukryta przez update fns */
    refresh_all();
}

/* ── Sleep check (LVGL timer, co 1s / 5s) ────────────────────── */

static void sleep_check_cb(lv_timer_t *t)
{
    if (!splash_done) return;

    int64_t elapsed = k_uptime_get() - last_activity;

    switch (disp_state) {
    case DISP_ACTIVE:
        if (elapsed >= IDLE_TIMEOUT_MS) enter_idle();
        break;
    case DISP_IDLE:
        if (elapsed >= SLEEP_TIMEOUT_MS) enter_sleep();
        break;
    case DISP_SLEEP:
        break;  /* budzi zdarzenie, nie timer */
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

static lv_obj_t *build_indicator_pill(const char *text, int x_ofs)
{
    lv_obj_t *obj = lv_label_create(screen);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_color(obj, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(COLOR_ON), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 4, 0);
    lv_obj_set_style_pad_hor(obj, 5, 0);
    lv_obj_set_style_pad_ver(obj, 2, 0);
    lv_obj_align(obj, LV_ALIGN_CENTER, x_ofs, -20);
    set_hidden(obj, true);
    return obj;
}

static void build_indicators(void)
{
    caps_indicator = build_indicator_pill("CAPS", -24);
    num_indicator  = build_indicator_pill("NUM",   24);
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


static void build_dim_overlay(void)
{
    /* Czarna nakładka na wierzchu wszystkich widgetów */
    dim_overlay = lv_obj_create(screen);
    lv_obj_set_size(dim_overlay, 240, 240);
    lv_obj_set_pos(dim_overlay, 0, 0);
    lv_obj_set_style_bg_color(dim_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(dim_overlay, LV_OPA_0, 0);   /* transparentna na start */
    lv_obj_set_style_border_width(dim_overlay, 0, 0);
    lv_obj_set_style_pad_all(dim_overlay, 0, 0);
    lv_obj_set_style_radius(dim_overlay, 0, 0);
    lv_obj_clear_flag(dim_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dim_overlay, LV_OBJ_FLAG_CLICKABLE);
}

/* ═══════════════════ Splash → main ════════════════════════════ */

static void show_status(struct k_work *work)
{
    if (splash_logo) {
        lv_obj_del(splash_logo);
        splash_logo = NULL;
    }

    splash_done = true;
    disp_state = DISP_ACTIVE;
    set_overlay(DIM_ACTIVE);
    mark_activity();

    set_hidden(top_logo, false);
    refresh_all();
}

/* ═══════════════════ Event listener ═══════════════════════════ */

static int ft_dongle_listener(const zmk_event_t *eh)
{
    /* Split central — obsługiwane zawsze (też przed splash) */
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

        if (splash_done) {
            if (disp_state != DISP_ACTIVE) enter_active();
            else mark_activity();
            update_link_status();
            update_battery_visuals();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!splash_done) return ZMK_EV_EVENT_BUBBLE;

    if (as_zmk_layer_state_changed(eh)) {
        /* Layer = aktywność użytkownika → zawsze budzi */
        if (disp_state != DISP_ACTIVE) enter_active();
        else mark_activity();
        update_layer();
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_peripheral_battery_state_changed(eh)) {
        const struct zmk_peripheral_battery_state_changed *ev =
            as_zmk_peripheral_battery_state_changed(eh);

        if (ev->source == 0)      battery_left  = ev->state_of_charge;
        else if (ev->source == 1) battery_right = ev->state_of_charge;

        /* Bateria: aktualizuj dane, ale nie budź ze SLEEP —
           tylko odśwież wyświetlane info w bieżącym stanie */
        if (disp_state == DISP_ACTIVE) mark_activity();
        update_battery_visuals();
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_hid_indicators_changed(eh)) {
        const struct zmk_hid_indicators_changed *ev =
            as_zmk_hid_indicators_changed(eh);
        hid_indicators = ev->indicators;

        /* Zmiana wskaźnika = aktywność → budź ekran */
        if (disp_state != DISP_ACTIVE) enter_active();
        else mark_activity();
        update_hid_indicators();
        return ZMK_EV_EVENT_BUBBLE;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ft_dongle_screen, ft_dongle_listener);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_split_central_status_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(ft_dongle_screen, zmk_hid_indicators_changed); 

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
    build_indicators();
    build_battery_widgets();
    /* Nakładka musi być ostatnia — na wierzchu całej hierarchii */
    build_dim_overlay();

    sleep_timer = lv_timer_create(sleep_check_cb, 1000, NULL);

    k_work_init_delayable(&splash_work, show_status);
    k_work_schedule(&splash_work, K_MSEC(SPLASH_DURATION_MS));

    return screen;
}
