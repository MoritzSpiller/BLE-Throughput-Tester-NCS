#ifndef _ADC_H_
#define _ADC_H_

void adc_configure(void);
void adc_start_sampling(void);
void adc_stop_sampling(void);
uint32_t adc_get_bytes_acquired(void);

#endif /* _ADC_H_ */