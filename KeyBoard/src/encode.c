#include "encode.h"
#include "utils.h"

volatile int circle = 0;

INTF void TIM2_IRQHandler(void)
{
    volatile uint16_t tempcnt = TIM_GetCounter(TIM2), temparr = TIM2->ATRLR;
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