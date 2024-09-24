#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include "adc.h"
#include "rbuf.h"

/* STEP 2 - Include header for nrfx drivers */
#include <nrfx_saadc.h>
#include <nrfx_timer.h>
#include <helpers/nrfx_gppi.h>
#if defined(DPPI_PRESENT)
#include <nrfx_dppi.h>
#else
#include <nrfx_ppi.h>
#endif

//!: ADC docs: https://docs.nordicsemi.com/bundle/ncs-latest/page/nrfx/drivers/saadc/index.html

#define LOG_MODULE_NAME adc
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

/* STEP 3.1 - Define the SAADC sample interval in microseconds */
#define SAADC_SAMPLE_INTERVAL_US 62

/* STEP 4.1 - Define the buffer size for the SAADC - was 8000 (M.S.) */
#define SAADC_BUFFER_SIZE 122

/* STEP 3.2 - Declaring an instance of nrfx_timer for TIMER1. */
const nrfx_timer_t timer_instance = NRFX_TIMER_INSTANCE(3);

/* STEP 4.2 - Declare the buffers for the SAADC */
static int16_t saadc_sample_buffer[2][SAADC_BUFFER_SIZE];

/* STEP 4.3 - Declare variable used to keep track of which buffer was last assigned to the SAADC driver */
static uint32_t saadc_current_buffer = 0;

// static uint16_t CC_VALUE = 1000;
static uint32_t ADC_EVT = 0;

static bool adc_ready = false;

static void adc_event_handler(nrfx_saadc_evt_t const *p_event)
{
    nrfx_err_t err;
    switch (p_event->type)
    {
    case NRFX_SAADC_EVT_READY:
        adc_ready = true;
        break;

    case NRFX_SAADC_EVT_BUF_REQ:

        /* STEP 5.2 - Set up the next available buffer. Alternate between buffer 0 and 1 */
        err = nrfx_saadc_buffer_set(saadc_sample_buffer[(saadc_current_buffer++) % 2], SAADC_BUFFER_SIZE);
        // err = nrfx_saadc_buffer_set(saadc_sample_buffer[((saadc_current_buffer == 0 )? saadc_current_buffer++ : 0)], SAADC_BUFFER_SIZE);
        if (err != NRFX_SUCCESS)
        {
            LOG_ERR("nrfx_saadc_buffer_set error: %08x", err);
            return;
        }
        break;

    case NRFX_SAADC_EVT_DONE:

        ADC_EVT += 1;
        if ((ADC_EVT % 1000) == 0) {
            LOG_INF("New ADC DONE event: %d", ADC_EVT);
        }
        
        // Write int16_t array to the ring buffer
        rbuf_put_data((uint8_t *)p_event->data.done.p_buffer, p_event->data.done.size*2);
        break;

    case NRFX_SAADC_EVT_FINISHED:
        LOG_INF("All buffers filled.");
        break;

    default:
        LOG_INF("Unhandled SAADC evt %d", p_event->type);
        break;
    }
}

static void adc_configure_timer(void)
{
    nrfx_err_t err;

    /* STEP 3.3 - Declaring timer config and intialize nrfx_timer instance. */
    nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG(1000000);
    err = nrfx_timer_init(&timer_instance, &timer_config, NULL);
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("nrfx_timer_init error: %08x", err);
        return;
    }

    /* STEP 3.4 - Set compare channel 0 to generate event every SAADC_SAMPLE_INTERVAL_US. */
    uint32_t timer_ticks = nrfx_timer_us_to_ticks(&timer_instance, SAADC_SAMPLE_INTERVAL_US);
    nrfx_timer_extended_compare(&timer_instance, NRF_TIMER_CC_CHANNEL0, timer_ticks, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, false);
}

static void adc_configure_ppi(void)
{
    nrfx_err_t err;
    /* STEP 6.1 - Declare variables used to hold the (D)PPI channel number */
    uint8_t m_saadc_sample_ppi_channel;
    uint8_t m_saadc_start_ppi_channel;

    /* STEP 6.2 - Trigger task sample from timer */
    err = nrfx_gppi_channel_alloc(&m_saadc_sample_ppi_channel);
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("nrfx_gppi_channel_alloc error: %08x", err);
        return;
    }

    err = nrfx_gppi_channel_alloc(&m_saadc_start_ppi_channel);
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("nrfx_gppi_channel_alloc error: %08x", err);
        return;
    }

    /* STEP 6.3 - Trigger task sample from timer */
    nrfx_gppi_channel_endpoints_setup(m_saadc_sample_ppi_channel,
                                      nrfx_timer_compare_event_address_get(&timer_instance, NRF_TIMER_CC_CHANNEL0),
                                      nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_SAMPLE));

    /* STEP 6.4 - Trigger task start from end event */
    nrfx_gppi_channel_endpoints_setup(m_saadc_start_ppi_channel,
                                      nrf_saadc_event_address_get(NRF_SAADC, NRF_SAADC_EVENT_END),
                                      nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_START));

    /* STEP 6.5 - Enable both (D)PPI channels */
    nrfx_gppi_channels_enable(BIT(m_saadc_sample_ppi_channel));
    nrfx_gppi_channels_enable(BIT(m_saadc_start_ppi_channel));
}

void adc_configure(void)
{
    adc_configure_timer();
    adc_configure_ppi();

    nrfx_err_t err;

    /* STEP 4.4 - Connect ADC interrupt to nrfx interrupt handler */
    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(adc)),
                DT_IRQ(DT_NODELABEL(adc), priority),
                nrfx_isr, nrfx_saadc_irq_handler, 0);

    /* STEP 4.5 - Initialize the nrfx_SAADC driver */
    err = nrfx_saadc_init(DT_IRQ(DT_NODELABEL(adc), priority));
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("nrfx_saadc_init error: %08x", err);
        return;
    }

    /* STEP 4.6 - Declare the struct to hold the configuration for the SAADC channel used to sample the battery voltage */
    nrfx_saadc_channel_t channel = NRFX_SAADC_DEFAULT_CHANNEL_SE(NRF_SAADC_INPUT_AIN0, 0);

    /* STEP 4.7 - Change gain config in default config and apply channel configuration */
    channel.channel_config.gain = NRF_SAADC_GAIN1_6;
    err = nrfx_saadc_channels_config(&channel, 1);
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("nrfx_saadc_channels_config error: %08x", err);
        return;
    }

    /* STEP 4.8 - Configure channel 0 in advanced mode with event handler (non-blocking mode) */
    nrfx_saadc_adv_config_t saadc_adv_config = NRFX_SAADC_DEFAULT_ADV_CONFIG;
    err = nrfx_saadc_advanced_mode_set(BIT(0),
                                       NRF_SAADC_RESOLUTION_12BIT,
                                       &saadc_adv_config,
                                       adc_event_handler);
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("nrfx_saadc_advanced_mode_set error: %08x", err);
        return;
    }

    /* STEP 4.9 - Configure two buffers to make use of double-buffering feature of SAADC */
    err = nrfx_saadc_buffer_set(saadc_sample_buffer[0], SAADC_BUFFER_SIZE);
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("nrfx_saadc_buffer_set error: %08x", err);
        return;
    }
    err = nrfx_saadc_buffer_set(saadc_sample_buffer[1], SAADC_BUFFER_SIZE);
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("nrfx_saadc_buffer_set error: %08x", err);
        return;
    }

    /* STEP 4.10 - Trigger the SAADC. This will not start sampling, but will prepare buffer for sampling triggered through PPI */
    err = nrfx_saadc_mode_trigger();
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("nrfx_saadc_mode_trigger error: %08x", err);
        return;
    }
}

void adc_start_sampling(void)
{
    if (adc_ready) {
        nrfx_timer_enable(&timer_instance);
        LOG_INF("Sampling started.");
    } else {
        LOG_INF("adc not ready to start sampling.");
    }
    
}

void adc_stop_sampling(void)
{
    nrfx_timer_disable(&timer_instance);
    LOG_INF("Sampling stopped.");
}

uint32_t adc_get_bytes_acquired(void) {
    return (ADC_EVT * 244);
}