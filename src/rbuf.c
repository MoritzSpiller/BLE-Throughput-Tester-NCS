#include <zephyr/sys/ring_buffer.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "rbuf.h"


//?: perform operations in memory?
//?: would this increase performance and/ or reduce memory usage
//!: Ring Buffer docs: https://docs.nordicsemi.com/bundle/ncs-latest/page/zephyr/kernel/data_structures/ring_buffers.html

#define LOG_MODULE_NAME ring_buffer
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define RING_BUF_SIZE 10000

uint8_t ring_buffer_data[RING_BUF_SIZE];
struct ring_buf ring_buffer;
static uint32_t overwrites = 0;
static uint32_t counter = 0;
static uint32_t bytes_read = 0;
static uint32_t bytes_written = 0;

// Global variable to store the callback function pointer
overwrite_callback_t overwrite_callback = NULL;

void buffer_overwrite_callback(void) {

    overwrites++;

    if (overwrite_callback) {
        overwrite_callback(overwrites);
    } else {
        LOG_DBG("data overwritten.");
    }
}

void rbuf_init(overwrite_callback_t callback) {
    ring_buf_init(&ring_buffer, sizeof(ring_buffer_data), ring_buffer_data);
    overwrite_callback = callback;
}

int rbuf_put_data(uint8_t *data, size_t length) {
    int ret = ring_buf_put(&ring_buffer, data, length);
    counter++;
    bytes_written += ret;

    if (ret < length) {
        //?: this works, so data is not overwritten but also not added to the buffer --> data is lost
        buffer_overwrite_callback();
        // LOG_INF("put_head: %d", ring_buffer.put_head);
        // LOG_INF("put_tail: %d", ring_buffer.put_tail);
        // LOG_INF("put_base: %d", ring_buffer.put_base);
        // LOG_INF("get_head: %d", ring_buffer.get_head);
        // LOG_INF("get_tail: %d", ring_buffer.get_tail);
        // LOG_INF("get_base: %d", ring_buffer.get_base);
        // LOG_INF("size: %d", ring_buffer.size);
    }
    return ret;
}

int rbuf_get_data(uint8_t *data, size_t length) {
    int ret = ring_buf_get(&ring_buffer, data, length);
    bytes_read += ret;
    return ret;
}

uint32_t rbuf_get_size(void) {
    return (RING_BUF_SIZE - ring_buf_space_get(&ring_buffer));
}

uint32_t rbuf_get_put_head(void) {
    return ring_buffer.put_head;
}

uint32_t rbuf_get_get_head(void) {
    return ring_buffer.get_head;
}

uint32_t rbuf_get_bytes_read(void) {
    return bytes_read;
}

uint32_t rbuf_get_bytes_written(void) {
    return bytes_written;
}
