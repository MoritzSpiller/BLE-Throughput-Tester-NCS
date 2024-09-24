#include <zephyr/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
#include <nrfx_timer.h>
#include "cts.h"
#include "adc.h"
#include "rbuf.h"

#define DELAY_US 1000000
#define COUNTER_MAX_US 1000000
#define ALARM_CHANNEL_ID 0

#define LOG_MODULE_NAME app
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define RUN_STATUS_LED DK_LED1
#define CONN_STATUS_LED DK_LED2
#define RUN_LED_BLINK_INTERVAL 1000

#define INTERVAL_MIN 12 /* 6 units, 7.5 ms, only used to setup connection */
#define INTERVAL_MAX 16 /* 16 units, 20 ms, only used to setup connection */

static struct bt_le_conn_param *conn_param = BT_LE_CONN_PARAM(INTERVAL_MIN, INTERVAL_MAX, 0, 400);
static struct bt_conn_le_phy_param *phy = BT_CONN_LE_PHY_PARAM_2M;
static struct bt_conn_le_data_len_param *data_len = BT_LE_DATA_LEN_PARAM_MAX;
static struct bt_conn *current_conn;
static struct bt_gatt_exchange_params exchange_params;

static uint8_t SEND_DATA = 0;
static uint8_t counter = 0;
static uint8_t blink_status = 0;

// Get a reference to the TIMER1 instance
static const nrfx_timer_t my_timer = NRFX_TIMER_INSTANCE(4);

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
    if (err)
    {
        LOG_INF("PHY update failed: %d.", err);
    }

    err = bt_conn_le_param_update(conn, conn_param);
    if (err)
    {
        LOG_INF("Connection parameters update failed: %d.", err);
    }

    err = bt_conn_le_data_len_update(conn, data_len);
    if (err)
    {
        LOG_INF("LE data length update failed: %d", err);
    }

    LOG_INF("Connection parameters successfully changed.");

    struct bt_conn_info info = {0};
    err = bt_conn_get_info(conn, &info);
    if (err)
    {
        LOG_INF("Failed to get connection info %d.", err);
        return;
    }

    LOG_INF("Connection Interval: %d.", info.le.interval);
    LOG_INF("Slave Latency: %d.", info.le.latency);
    LOG_INF("RX PHY: %d.", info.le.phy->rx_phy);
    LOG_INF("TX PHY: %d.", info.le.phy->tx_phy);
    LOG_INF("RX Data Length: %d.", info.le.data_len->rx_max_len);
    LOG_INF("TX Data Length: %d.", info.le.data_len->tx_max_len);
    LOG_INF("RX Data Time: %d.", info.le.data_len->rx_max_time);
    LOG_INF("TX Data Time: %d.", info.le.data_len->tx_max_time);
}

/* MTU Exchange Function */
static void exchange_func(struct bt_conn *conn, uint8_t att_err, struct bt_gatt_exchange_params *params)
{
    struct bt_conn_info info = {0};
    int err;

    LOG_INF("MTU exchange %s", att_err == 0 ? "successful" : "failed");

    err = bt_conn_get_info(conn, &info);
    if (err)
    {
        LOG_INF("Failed to get connection info %d.", err);
        return;
    }

    update_conn_params(current_conn);
}

/* Bluetooth Connection Callbacks */
void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("Connection error: %d", err);
        return;
    }
    LOG_INF("Connected.");
    current_conn = bt_conn_ref(conn);

    exchange_params.func = exchange_func;
    err = bt_gatt_exchange_mtu(conn, &exchange_params);
    if (err)
    {
        LOG_INF("MTU exchange failed (err %d).", err);
    }
    else
    {
        LOG_INF("MTU exchange pending.");
    }

    dk_set_led_on(CONN_STATUS_LED);
}

void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason: %d)", reason);
    dk_set_led_off(CONN_STATUS_LED);
    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
}

void on_notif_changed(enum bt_button_notifications_enabled status)
{
    if (status == BT_BUTTON_NOTIFICATIONS_ENABLED)
    {
        LOG_INF("Notifications enabled");
    }
    else
    {
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

static void start_acq(void) {
    adc_start_sampling();
    SEND_DATA = 1;
    nrfx_timer_enable(&my_timer);          
}

static void stop_acq(void) {
    adc_stop_sampling();
    nrfx_timer_disable(&my_timer);
    counter = 0;
    SEND_DATA = 0;
    reset_bytes_sent();
}

/* Button Handler */
void button_handler(uint32_t button_state, uint32_t has_changed)
{
    if (has_changed & button_state)
    {
        switch (has_changed)
        {
        case DK_BTN1_MSK:
            LOG_INF("Button 1 pressed.");
            if (SEND_DATA == 0)
            {
                start_acq();
            }
            else
            {
                stop_acq();
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
    if (err)
    {
        LOG_ERR("Couldn't init LEDs (err %d)", err);
    }
    err = dk_buttons_init(button_handler);
    if (err)
    {
        LOG_ERR("Couldn't init buttons (err %d)", err);
    }
}

/* Calculate Throughput */
int calc_throughput(int secs)
{
    return get_bytes_sent() / secs;
}

// Interrupt handler for the timer
// NOTE: This callback is triggered by an interrupt. Many drivers or modules in Zephyr can not be accessed directly from interrupts, 
//		 and if you need to access one of these from the timer callback it is necessary to use something like a k_work item to move execution out of the interrupt context. 
void timer1_event_handler(nrf_timer_event_t event_type, void * p_context)
{
    // Blink the status LED
    dk_set_led(RUN_STATUS_LED, (blink_status++) % 2);

	switch(event_type) {
		case NRF_TIMER_EVENT_COMPARE1:
            counter++;

			// Do your work here
			LOG_INF("Timer 1 callback. Counter = %d.", counter);

            int current_throughput = calc_throughput(counter);

            if (counter >= 30)
            {
                LOG_INF("%u seconds sent.", counter);
                LOG_INF("Stopping timer.");
                LOG_INF("Current throughput: %d bytes per second.", current_throughput);
                stop_acq();
                return;
            }
            else
            {
                LOG_INF("%u seconds sent.", counter);
                LOG_INF("Current throughput: %d bytes per second.", current_throughput);
                uint32_t bytes_acquired = adc_get_bytes_acquired();
                uint32_t bps = bytes_acquired / counter;
                LOG_INF("ADC: Bytes acquired so far: %d, this equals to %d bytes per second.", bytes_acquired, bps);
                LOG_INF("BUFFER: contains %d bytes, put_head at: %d, get_head at: %d.", rbuf_get_size(), rbuf_get_put_head(), rbuf_get_get_head());
                LOG_INF("BUFFER: total bytes read: %d, total bytes written: %d", rbuf_get_bytes_read(), rbuf_get_bytes_written());
                printk("\n");
            }

			break;
		
		default:
			break;
	}
}

// Function for initializing the TIMER1 peripheral using the nrfx driver
static void timer1_init(void)
{
	nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG(1000000);
	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;

	int err = nrfx_timer_init(&my_timer, &timer_config, timer1_event_handler);
	if (err != NRFX_SUCCESS) {
		LOG_INF("Error initializing timer: %x.", err);
	}

	IRQ_DIRECT_CONNECT(TIMER4_IRQn, 0, nrfx_timer_4_irq_handler, 0);
	irq_enable(TIMER4_IRQn);

    nrfx_timer_extended_compare(&my_timer, NRF_TIMER_CC_CHANNEL1, DELAY_US, 
                                NRF_TIMER_SHORT_COMPARE1_CLEAR_MASK, true);
}

void print_int16_array(int16_t *array, size_t length) {
    for (size_t i = 0; i < length; i++) {
        LOG_INF("data read from buffer: %d", array[i]);
    }
    LOG_DBG("\n");
}

// Example callback function
void my_overwrite_callback(uint32_t num) {
    if ((num%10) == 0) {
        LOG_INF("------------> This was overwrite: %d.", num);
    } 
}

/* Main Function */
int main(void)
{
    int err;
    LOG_INF("Hello World! %s", CONFIG_BOARD);

    // Configure buttons and LEDs
    configure_dk_buttons_leds();

    // Initialize Bluetooth
    err = bluetooth_init(&bluetooth_callbacks, &remote_callbacks);
    if (err)
    {
        LOG_INF("Couldn't initialize Bluetooth. err: %d", err);
    }

    // Initialize TIMER1
	timer1_init();

    adc_configure();

    rbuf_init(my_overwrite_callback);

    LOG_INF("Running...");
    for (;;)
    {
        // Send data while SEND_DATA is set
        while (SEND_DATA == 1)
        {
            uint8_t data_retrieved[244];
            uint8_t bytes_read = rbuf_get_data(data_retrieved, 244);
            if (bytes_read > 0) {
                err = send_notification(current_conn, data_retrieved);
                if (err) {
                    LOG_ERR("Failed to send notification (err %d)", err);
                }
            }     
        }

        //TODO: "sleep" mode should be added here, to save power when SEND_DATA == 0
    }
}
