#include "utils.h"
#include "app.h"
#include "usb_lib.h"
#include "utils.h"
/* Global typedef */

/* Global define */

/* Global Variable */

extern uint8_t APP_ENTRY_ADDR[];

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
RAM int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

#if (DEBUG)
    USART_Printf_Init(921600);
    PRINT("SystemClk:%lu\r\n", SystemCoreClock);
#endif
    app_init();
    // ReSharper disable once CppDFAEndlessLoop
    while (1)
    {
        app_run();
    }
}
