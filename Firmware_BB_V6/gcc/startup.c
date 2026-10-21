#include <stdint.h>
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern void SystemInit(void), __libc_init_array(void);
extern int main(void);
void Reset_Handler(void) {
  uint32_t *src=&_sidata,*dst;
  for(dst=&_sdata;dst<&_edata;) *dst++=*src++;
  for(dst=&_sbss;dst<&_ebss;) *dst++=0;
  SystemInit(); __libc_init_array(); (void)main(); for(;;){}
}
void Default_Handler(void) {
  /* Immediate gate shutdown, including unexpected interrupts. */
  *(volatile uint32_t *)0x40010814 = 1u<<10;
  *(volatile uint32_t *)0x40012C44 &= ~(1u<<15);
  for(;;){}
}
#define WEAK(name) void name(void) __attribute__((weak,alias("Default_Handler")))
WEAK(NMI_Handler);WEAK(HardFault_Handler);WEAK(MemManage_Handler);WEAK(BusFault_Handler);WEAK(UsageFault_Handler);
WEAK(SVC_Handler);WEAK(DebugMon_Handler);WEAK(PendSV_Handler);WEAK(SysTick_Handler);
WEAK(WWDG_IRQHandler);WEAK(PVD_IRQHandler);WEAK(TAMPER_IRQHandler);WEAK(RTC_IRQHandler);WEAK(FLASH_IRQHandler);WEAK(RCC_IRQHandler);
WEAK(EXTI0_IRQHandler);WEAK(EXTI1_IRQHandler);WEAK(EXTI2_IRQHandler);WEAK(EXTI3_IRQHandler);WEAK(EXTI4_IRQHandler);
WEAK(DMA1_Channel1_IRQHandler);WEAK(DMA1_Channel2_IRQHandler);WEAK(DMA1_Channel3_IRQHandler);WEAK(DMA1_Channel4_IRQHandler);WEAK(DMA1_Channel5_IRQHandler);WEAK(DMA1_Channel6_IRQHandler);WEAK(DMA1_Channel7_IRQHandler);
WEAK(ADC1_2_IRQHandler);WEAK(USB_HP_CAN1_TX_IRQHandler);WEAK(USB_LP_CAN1_RX0_IRQHandler);WEAK(CAN1_RX1_IRQHandler);WEAK(CAN1_SCE_IRQHandler);
WEAK(EXTI9_5_IRQHandler);WEAK(TIM1_BRK_IRQHandler);WEAK(TIM1_UP_IRQHandler);WEAK(TIM1_TRG_COM_IRQHandler);WEAK(TIM1_CC_IRQHandler);
WEAK(TIM2_IRQHandler);WEAK(TIM3_IRQHandler);WEAK(TIM4_IRQHandler);WEAK(I2C1_EV_IRQHandler);WEAK(I2C1_ER_IRQHandler);WEAK(I2C2_EV_IRQHandler);WEAK(I2C2_ER_IRQHandler);WEAK(SPI1_IRQHandler);WEAK(SPI2_IRQHandler);WEAK(USART1_IRQHandler);WEAK(USART2_IRQHandler);WEAK(USART3_IRQHandler);WEAK(EXTI15_10_IRQHandler);WEAK(RTC_Alarm_IRQHandler);WEAK(USBWakeUp_IRQHandler);
typedef void (*Handler)(void);
__attribute__((section(".isr_vector"),used)) const Handler vectors[] = {
 (Handler)&_estack,Reset_Handler,NMI_Handler,HardFault_Handler,MemManage_Handler,BusFault_Handler,UsageFault_Handler,0,0,0,0,SVC_Handler,DebugMon_Handler,0,PendSV_Handler,SysTick_Handler,
 WWDG_IRQHandler,PVD_IRQHandler,TAMPER_IRQHandler,RTC_IRQHandler,FLASH_IRQHandler,RCC_IRQHandler,EXTI0_IRQHandler,EXTI1_IRQHandler,EXTI2_IRQHandler,EXTI3_IRQHandler,EXTI4_IRQHandler,
 DMA1_Channel1_IRQHandler,DMA1_Channel2_IRQHandler,DMA1_Channel3_IRQHandler,DMA1_Channel4_IRQHandler,DMA1_Channel5_IRQHandler,DMA1_Channel6_IRQHandler,DMA1_Channel7_IRQHandler,
 ADC1_2_IRQHandler,USB_HP_CAN1_TX_IRQHandler,USB_LP_CAN1_RX0_IRQHandler,CAN1_RX1_IRQHandler,CAN1_SCE_IRQHandler,EXTI9_5_IRQHandler,TIM1_BRK_IRQHandler,TIM1_UP_IRQHandler,TIM1_TRG_COM_IRQHandler,TIM1_CC_IRQHandler,TIM2_IRQHandler,TIM3_IRQHandler,TIM4_IRQHandler,I2C1_EV_IRQHandler,I2C1_ER_IRQHandler,I2C2_EV_IRQHandler,I2C2_ER_IRQHandler,SPI1_IRQHandler,SPI2_IRQHandler,USART1_IRQHandler,USART2_IRQHandler,USART3_IRQHandler,EXTI15_10_IRQHandler,RTC_Alarm_IRQHandler,USBWakeUp_IRQHandler
};
