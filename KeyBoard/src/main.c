#include "debug.h"
#include "app.h"
#include "comm_controller.h"
#include "usb_lib.h"
#include "utils.h"
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
RAM int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

#if (DEBUG)
    USART_Printf_Init(2000000);
#endif
    app_init();
    // ReSharper disable once CppDFAEndlessLoop
    while (1)
    {
        app_run();
    }
}
