#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include "cts.h"
#include "adc.h"

#define DELAY_US 1000000
#define COUNTER_MAX_US 1000000
#define ALARM_CHANNEL_ID 0
#define TIMER DT_NODELABEL(timer2)

#define LOG_MODULE_NAME app
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define RUN_STATUS_LED DK_LED1
#define CONN_STATUS_LED DK_LED2
#define RUN_LED_BLINK_INTERVAL 1000

#define INTERVAL_MIN 12  /* 6 units, 7.5 ms, only used to setup connection */
#define INTERVAL_MAX 16 /* 16 units, 20 ms, only used to setup connection */

static struct bt_le_conn_param *conn_param = BT_LE_CONN_PARAM(INTERVAL_MIN, INTERVAL_MAX, 0, 400);
static struct bt_conn_le_phy_param *phy = BT_CONN_LE_PHY_PARAM_2M;
static struct bt_conn_le_data_len_param *data_len = BT_LE_DATA_LEN_PARAM_MAX;
static struct bt_conn *current_conn;
static struct bt_gatt_exchange_params exchange_params;

static uint8_t SEND_DATA = 0;
static uint8_t m_array[244] = {0};
static uint8_t m_array_size = 244;

struct counter_alarm_cfg alarm_cfg;
const struct device *const counter_dev = DEVICE_DT_GET(TIMER);

/* Bluetooth Callbacks */
void on_connected(struct bt_conn *conn, uint8_t err);
void on_disconnected(struct bt_conn *conn, uint8_t reason);
void on_notif_changed(enum bt_button_notifications_enabled status);
void on_data_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len);

struct bt_conn_cb bluetooth_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

struct bt_remote_service_cb remote_callbacks = {
    .notif_changed = on_notif_changed,
    .data_received = on_data_received,
};

/* Update Connection Parameters */
static void update_conn_params(struct bt_conn *conn)
{
    uint8_t err;

    err = bt_conn_le_phy_update(conn, phy);
    if (err) {
        LOG_INF("PHY update failed: %d\n", err);
    }

    err = bt_conn_le_param_update(conn, conn_param);
    if (err) {
        LOG_INF("Connection parameters update failed: %d", err);
    }

    err = bt_conn_le_data_len_update(conn, data_len);
    if (err) {
        LOG_INF("LE data length update failed: %d", err);
    }

    LOG_INF("Connection parameters successfully changed.");

    struct bt_conn_info info = {0};
    err = bt_conn_get_info(conn, &info);
    if (err) {
        LOG_INF("Failed to get connection info %d\n", err);
        return;
    }

    LOG_INF("Connection Interval: %d\n", info.le.interval);
    LOG_INF("Slave Latency: %d\n", info.le.latency);
    LOG_INF("RX PHY: %d\n", info.le.phy->rx_phy);
    LOG_INF("TX PHY: %d\n", info.le.phy->tx_phy);
    LOG_INF("RX Data Length: %d\n", info.le.data_len->rx_max_len);
    LOG_INF("TX Data Length: %d\n", info.le.data_len->tx_max_len);
    LOG_INF("RX Data Time: %d\n", info.le.data_len->rx_max_time);
    LOG_INF("TX Data Time: %d\n", info.le.data_len->tx_max_time);
}

/* MTU Exchange Function */
static void exchange_func(struct bt_conn *conn, uint8_t att_err, struct bt_gatt_exchange_params *params)
{
    struct bt_conn_info info = {0};
    int err;

    LOG_INF("MTU exchange %s\n", att_err == 0 ? "successful" : "failed");

    err = bt_conn_get_info(conn, &info);
    if (err) {
        LOG_INF("Failed to get connection info %d\n", err);
        return;
    }

    update_conn_params(current_conn);
}

/* Bluetooth Connection Callbacks */
void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection error: %d", err);
        return;
    }
    LOG_INF("Connected.");
    current_conn = bt_conn_ref(conn);

    exchange_params.func = exchange_func;
    err = bt_gatt_exchange_mtu(conn, &exchange_params);
    if (err) {
        LOG_INF("MTU exchange failed (err %d)\n", err);
    } else {
        LOG_INF("MTU exchange pending\n");
    }

    dk_set_led_on(CONN_STATUS_LED);
}

void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason: %d)", reason);
    dk_set_led_off(CONN_STATUS_LED);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
}

void on_notif_changed(enum bt_button_notifications_enabled status)
{
    if (status == BT_BUTTON_NOTIFICATIONS_ENABLED) {
        LOG_INF("Notifications enabled");
    } else {
        LOG_INF("Notifications disabled");
    }
}

void on_data_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
    uint8_t temp_str[len + 1];
    memcpy(temp_str, data, len);
    temp_str[len] = 0x00;

    LOG_INF("Received data on conn %p. Len: %d", (void *)conn, len);
    LOG_INF("Data: %s", temp_str);
}

/* Button Handler */
void button_handler(uint32_t button_state, uint32_t has_changed)
{
    if (has_changed & button_state) {
        switch (has_changed) {
        case DK_BTN1_MSK:
            if (SEND_DATA == 0) {
                SEND_DATA = 1;
                counter_start(counter_dev);
            } else {
                SEND_DATA = 0;
                counter_stop(counter_dev);
            }
            break;
        case DK_BTN2_MSK:
            LOG_DBG("Button 2 not implemented.");
        case DK_BTN3_MSK:
            LOG_DBG("Button 3 not implemented.");
        case DK_BTN4_MSK:
            LOG_DBG("Button 4 not implemented.");
        default:
            break;
        }
    }
}

/* Configure Buttons and LEDs */
static void configure_dk_buttons_leds(void)
{
    int err;
    err = dk_leds_init();
    if (err) {
        LOG_ERR("Couldn't init LEDs (err %d)", err);
    }
    err = dk_buttons_init(button_handler);
    if (err) {
        LOG_ERR("Couldn't init buttons (err %d)", err);
    }
}

/* Calculate Throughput */
int calc_throughput(int secs)
{
    return get_bytes_sent() / secs;
}

/* Counter Timeout Handler */
static void counter_timeout_handler(const struct device *counter_dev, uint8_t chan_id, uint32_t ticks, void *user_data)
{
    // struct counter_alarm_cfg *config = user_data;
    uint32_t now_ticks;
    uint64_t now_usec;
    int now_sec;
    int err;

    err = counter_get_value(counter_dev, &now_ticks);
    if (err) {
        LOG_INF("Failed to read counter value (err %d)", err);
        return;
    }

    now_usec = counter_ticks_to_us(counter_dev, now_ticks);
    now_sec = (int)(now_usec / USEC_PER_SEC);

    int current_throughput = calc_throughput(now_sec);

    if (now_sec >= 30) {
        LOG_INF("%u seconds sent.\n", now_sec);
        LOG_INF("Stopping timer\n");
        LOG_INF("Current throughput: %d bytes per second.\n", current_throughput);
        SEND_DATA = 0;
        counter_stop(counter_dev);
        return;
    } else {
        LOG_INF("%u seconds sent.\n", now_sec);
        LOG_INF("Current throughput: %d bytes per second.\n", current_throughput);
    }
}

/* Configure Timer */
int configure_timer(void)
{
    int err;

    LOG_INF("Counter alarm sample\n\n");

    if (!device_is_ready(counter_dev)) {
        LOG_INF("Device not ready.\n");
        return 0;
    }

    // Initialize alarm configuration
    alarm_cfg.flags = 0;
    alarm_cfg.ticks = counter_us_to_ticks(counter_dev, DELAY_US);
    alarm_cfg.callback = counter_timeout_handler;
    alarm_cfg.user_data = &alarm_cfg;

    // Set the counter alarm
    err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID, &alarm_cfg);
    LOG_INF("Set alarm in %u sec (%u ticks)\n",
           (uint32_t)(counter_ticks_to_us(counter_dev, alarm_cfg.ticks) / USEC_PER_SEC),
           alarm_cfg.ticks);

    // Handle potential errors
    if (err == -EINVAL) {
        LOG_ERR("Alarm settings invalid\n");
    } else if (err == -ENOTSUP) {
        LOG_ERR("Alarm setting request not supported\n");
    } else if (err != 0) {
        LOG_ERR("Error\n");
    }

    return err;
}

/* Main Function */
int main(void)
{
    int err;
    int blink_status = 0;
    LOG_INF("Hello World! %s\n", CONFIG_BOARD);

    // Configure buttons and LEDs
    configure_dk_buttons_leds();

    // Configure the timer
    err = configure_timer();
    if (err) {
        LOG_INF("Couldn't configure timer, err: %d", err);
    }

    // Initialize Bluetooth
    err = bluetooth_init(&bluetooth_callbacks, &remote_callbacks);
    if (err) {
        LOG_INF("Couldn't initialize Bluetooth. err: %d", err);
    }

    configure_saadc();

    LOG_INF("Running...");
    for (;;) {
        // Blink the status LED
        dk_set_led(RUN_STATUS_LED, (blink_status++) % 2);
        k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));

        // Send data while SEND_DATA is set
        while (SEND_DATA == 1) {
            send_notification(current_conn, m_array);
            for (int i = 0; i < m_array_size; i++) {
                m_array[i]++;
            }
        }
    }
}

