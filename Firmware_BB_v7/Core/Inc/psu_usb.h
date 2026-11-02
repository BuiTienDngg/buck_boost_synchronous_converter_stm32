#ifndef PSU_USB_H
#define PSU_USB_H
#include <stdint.h>
/* USB CDC transport writes from a stable buffer while class TxState is active. */
uint8_t PSU_USB_TryWrite(const uint8_t *data,uint16_t len);
#endif
