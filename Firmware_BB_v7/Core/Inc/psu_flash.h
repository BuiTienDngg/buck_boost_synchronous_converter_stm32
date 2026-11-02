#ifndef PSU_FLASH_H
#define PSU_FLASH_H
#include "power_stage.h"
#include <stdint.h>
/* STM32F103C8: 64KB flash, 1KB pages; reserve 0x0800F800-0x0800FFFF.
 * Keil scatter MUST restrict the code image to 0x08000000-0x0800F7FF.
 * Only call Save while output is OFF, outside all IRQs. */
uint8_t PSU_Flash_Load(PSU_Tuning *cfg,uint32_t *vset_mv,uint32_t *iset_ma);
uint8_t PSU_Flash_Save(const PSU_Tuning *cfg,uint32_t vset_mv,uint32_t iset_ma);
#endif
