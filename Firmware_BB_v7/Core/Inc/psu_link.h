#ifndef PSU_LINK_H
#define PSU_LINK_H
#include <stdint.h>
/* USB CDC virtual COM; Receive callback (IRQ) only enqueues bytes. */
typedef uint8_t (*PSU_LinkWrite)(const uint8_t *data,uint16_t len);
void PSU_Link_Init(PSU_LinkWrite write_fn);
void PSU_Link_RxBytes(const uint8_t *data,uint32_t length);
void PSU_Link_Poll(void); /* in main, after PowerStage_Handle, never TIM3 ISR */
#endif
