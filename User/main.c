#include "debug.h"
#include "app.h"
#include "usb_lib.h"

/* Global typedef */

/* Global define */

/* Global Variable */

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

#if (DEBUG)
    USART_Printf_Init(921600);
    PRINT("SystemClk:%d\r\n", SystemCoreClock);
#endif
    app_init();
    while (1)
    {
        app_run();
    }
}
