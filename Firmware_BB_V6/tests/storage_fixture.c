#include "psu_app.h"
HAL_StatusTypeDef HAL_FLASH_Unlock(void) {return HAL_OK;}
HAL_StatusTypeDef HAL_FLASH_Lock(void) {return HAL_OK;}
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *p,uint32_t *error) {(void)p;*error=0xFFFFFFFFu;return HAL_OK;}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type,uint32_t address,uint64_t data) {(void)type;(void)address;(void)data;return HAL_OK;}
/* Keep load reachable as the emulator calls both public functions. */
int fixture(PsuPreset p[3]) {return PsuStorage_Save(p)+PsuStorage_Load(p);}
