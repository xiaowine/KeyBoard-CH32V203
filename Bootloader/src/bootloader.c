#include <utils.h>
#include "usb_lib.h"
#include "usb_desc.h"

extern uint8_t APP_ENTRY_ADDR[];

int main(void)
{
    // NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    // SystemCoreClockUpdate();
    // Delay_Init();
    // RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    // RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    // GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
    // Set_USB_Clock();
    // USB_Init();
    // USB_Interrupts_Config();
    __disable_irq();
    void (*app_entry)(void) = (void(*))APP_ENTRY_ADDR;
    app_entry();

    while (1)
    {
        // __asm__ volatile("wfi");
    }

    return 0;
}
