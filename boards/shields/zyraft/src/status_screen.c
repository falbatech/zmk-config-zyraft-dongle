/*
 * Add near other includes
 */
#include <zmk/events/split_peripheral_status_changed.h>

/*
 * Add near other colors
 */
#define COLOR_LINK_ON   0x00FF66
#define COLOR_LINK_OFF  0x333333

/*
 * Add near other globals
 */
static bool peripheral_connected = false;

static lv_obj_t *left_link;
static lv_obj_t *right_link;

/*
 * ADD THIS FUNCTION
 */
static void update_link_icons(void) {
    if (!left_link || !right_link) {
        return;
    }

    lv_label_set_text(left_link, LV_SYMBOL_WIFI);
    lv_label_set_text(right_link, LV_SYMBOL_WIFI);

    /* left side = dongle side = always connected */
    lv_obj_set_style_text_color(
        left_link,
        lv_color_hex(COLOR_LINK_ON),
        0
    );

    /* right side depends on split connection */
    lv_obj_set_style_text_color(
        right_link,
        lv_color_hex(
            peripheral_connected
                ? COLOR_LINK_ON
                : COLOR_LINK_OFF
        ),
        0
    );
}

/*
 * IN build_battery_widgets()
 * ADD AFTER right_icon
 */

left_link = lv_label_create(screen);
style_text(left_link, COLOR_LINK_ON, &lv_font_montserrat_14);
lv_obj_align(left_link, LV_ALIGN_CENTER, -38, 18);
set_hidden(left_link, true);

right_link = lv_label_create(screen);
style_text(right_link, COLOR_LINK_OFF, &lv_font_montserrat_14);
lv_obj_align(right_link, LV_ALIGN_CENTER, 38, 18);
set_hidden(right_link, true);

/*
 * IN update_battery_visuals()
 * ADD AT END
 */

update_link_icons();

/*
 * IN show_status()
 * ADD
 */

set_hidden(left_link, false);
set_hidden(right_link, false);

/*
 * IN ft_dongle_listener()
 * ADD
 */

if (as_zmk_split_peripheral_status_changed(eh)) {
    const struct zmk_split_peripheral_status_changed *ev =
        as_zmk_split_peripheral_status_changed(eh);

    peripheral_connected = ev->connected;

    update_link_icons();
}

/*
 * ADD SUBSCRIPTION
 */

ZMK_SUBSCRIPTION(
    ft_dongle_screen,
    zmk_split_peripheral_status_changed
);
