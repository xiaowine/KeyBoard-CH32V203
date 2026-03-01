#include "encode.h"

volatile int circle = 0;

__attribute__((interrupt("WCH-Interrupt-fast"))) void TIM2_IRQHandler(void)
{
  volatile u16 tempcnt = TIM_GetCounter(TIM2), temparr = TIM2->ATRLR;
  if (TIM_GetITStatus(TIM2, TIM_IT_Update))
  {
    if (tempcnt < temparr / 2)
    {
      circle += 1;
    }
    else
    {
      circle -= 1;
    }
  }
  TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
}
