/*
 * Redox FT Dongle — obserwator połączeń split central
 * SPDX-License-Identifier: MIT
 *
 * Rejestruje BT conn callbacks i emituje zmk_split_central_status_changed
 * gdy peryferyjna połówka łączy się lub rozłącza.
 */

#include <zephyr/types.h>
#include <zephyr/init.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/ble.h>
#include <zmk/events/split_central_status_changed.h>

#ifndef ZMK_SPLIT_BLE_PERIPHERAL_COUNT
#define ZMK_SPLIT_BLE_PERIPHERAL_COUNT CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS
#endif

enum ft_peripheral_slot_state {
    FT_SLOT_STATE_OPEN,
    FT_SLOT_STATE_CONNECTING,
    FT_SLOT_STATE_CONNECTED,
};

struct ft_peripheral_slot {
    enum ft_peripheral_slot_state state;
    struct bt_conn *conn;
};

static struct ft_peripheral_slot peripherals[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];

static int ft_slot_index_for_conn(struct bt_conn *conn)
{
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        if (peripherals[i].conn == conn) {
            return i;
        }
    }
    return -EINVAL;
}

static struct ft_peripheral_slot *ft_slot_for_conn(struct bt_conn *conn)
{
    int idx = ft_slot_index_for_conn(conn);
    return (idx < 0) ? NULL : &peripherals[idx];
}

static int ft_release_slot(int index)
{
    if (index < 0 || index >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
        return -EINVAL;
    }
    struct ft_peripheral_slot *slot = &peripherals[index];
    if (slot->state == FT_SLOT_STATE_OPEN) {
        return -EINVAL;
    }
    LOG_DBG("Releasing peripheral slot %d", index);
    slot->conn  = NULL;
    slot->state = FT_SLOT_STATE_OPEN;
    return 0;
}

static int ft_reserve_slot_for_conn(struct bt_conn *conn)
{
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_PREF_WEAK_BOND)
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        if (peripherals[i].state == FT_SLOT_STATE_OPEN) {
            ft_release_slot(i);
            peripherals[i].conn  = conn;
            peripherals[i].state = FT_SLOT_STATE_CONNECTED;
            return i;
        }
    }
#else
    int i = zmk_ble_put_peripheral_addr(bt_conn_get_dst(conn));
    if (i >= 0 && peripherals[i].state == FT_SLOT_STATE_OPEN) {
        ft_release_slot(i);
        peripherals[i].conn  = conn;
        peripherals[i].state = FT_SLOT_STATE_CONNECTED;
        return i;
    }
#endif
    return -ENOMEM;
}

static int ft_release_slot_for_conn(struct bt_conn *conn)
{
    int idx = ft_slot_index_for_conn(conn);
    return (idx < 0) ? idx : ft_release_slot(idx);
}

static void ft_process_connection(struct bt_conn *conn)
{
    struct ft_peripheral_slot *slot = ft_slot_for_conn(conn);
    if (!slot) {
        LOG_ERR("No peripheral slot for connection");
        return;
    }

    struct bt_conn_info info;
    bt_conn_get_info(conn, &info);
    LOG_DBG("Connected: interval=%d latency=%d phy=%d",
            info.le.interval, info.le.latency, info.le.phy->rx_phy);

    int idx = ft_slot_index_for_conn(conn);
    raise_zmk_split_central_status_changed(
        (struct zmk_split_central_status_changed){ .slot = (uint8_t)idx, .connected = true });
}

static void ft_connected(struct bt_conn *conn, uint8_t conn_err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    struct bt_conn_info info;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    bt_conn_get_info(conn, &info);

    if (info.role != BT_CONN_ROLE_CENTRAL) {
        LOG_DBG("Skipping non-central role %d", info.role);
        return;
    }
    if (conn_err) {
        LOG_ERR("Connect error to %s (%u)", addr, conn_err);
        ft_release_slot_for_conn(conn);
        return;
    }

    LOG_DBG("Connected: %s", addr);

    int idx = ft_reserve_slot_for_conn(conn);
    if (idx < 0) {
        LOG_ERR("No free peripheral slot (err %d)", idx);
        return;
    }
    ft_process_connection(conn);
}

static void ft_disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_DBG("Disconnected: %s (reason %d)", addr, reason);

    int idx = ft_slot_index_for_conn(conn);
    raise_zmk_split_central_status_changed(
        (struct zmk_split_central_status_changed){
            .slot      = (uint8_t)(idx >= 0 ? idx : 0),
            .connected = false,
        });

    ft_release_slot_for_conn(conn);
}

static struct bt_conn_cb ft_conn_callbacks = {
    .connected    = ft_connected,
    .disconnected = ft_disconnected,
};

static int ft_split_central_status_init(void)
{
    bt_conn_cb_register(&ft_conn_callbacks);
    return 0;
}

SYS_INIT(ft_split_central_status_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);
