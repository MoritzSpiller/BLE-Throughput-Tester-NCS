#ifndef _RBUF_H_
#define _RBUF_H_

#include <stdint.h>
#include <stddef.h>

// Define a function pointer type for the callback
typedef void (*overwrite_callback_t)(uint32_t);

void rbuf_init(overwrite_callback_t callback);

int rbuf_put_data(uint8_t *data, size_t length);

int rbuf_get_data(uint8_t *data, size_t length);

uint32_t rbuf_get_size(void);
uint32_t rbuf_get_put_head(void);
uint32_t rbuf_get_get_head(void);
uint32_t rbuf_get_bytes_read(void);
uint32_t rbuf_get_bytes_written(void);

#endif /* _ADC_H_ */