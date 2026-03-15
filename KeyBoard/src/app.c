#include "app.h"

#include "encode.h"
#include "key.h"
#include "usb_lib.h"
#include "usb_desc.h"

#include "hid_comm.h"
#include "keymap.h"
#include "keymap_loader.h"

static volatile key_scan_state_t key_scan_state = KEY_STATE_IDLE;
static volatile uint16_t scan_tick_counter = 0;
static uint8_t last_snapshot[HC165_COUNT];
static uint8_t key_pressed_count = 0;
static uint8_t debug = 0;

/* 子采样槽位管理（0 -> 1 -> 2 -> 0） */
static uint8_t next_sample_slot = 0;
static volatile uint8_t active_sample_slot = 0;
/* 扫描超时保护（单位：TIM3 update tick） */
#define KEY_SCAN_TIMEOUT_TICKS 6U
static volatile uint8_t scan_timeout_ticks = 0;

void app_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* 按 FLASH 中的掩码（KEYMAP_LOAD_MASK）选择性加载 keymap 层到 RAM */
    keymap_loader_init();

    // 编码器初始化（TIM）
    encode_init();

    /* 键盘模块初始化：配置 SPI + DMA 以读取 74HC165 */
    key_init();

    // TIM 初始化
    {
        // TIM3

        RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

        // 设置定时器基准频率为 30kHz（用于分频得到 3kHz 扫描中断）
        const uint16_t tick_freq = 30000U;
        // 计算预分频值
        uint16_t prescaler = SystemCoreClock / tick_freq;
        // 预分频值至少为 1
        if (prescaler == 0)
            prescaler = 1;
        prescaler -= 1;

        // 计算周期：period_ticks = tick_freq / 3000 = 10
        uint16_t period_ticks = tick_freq / KEYBOARD_SCAN_FREQUENCY_HZ;
        // 周期至少为 1
        if (period_ticks == 0)
            period_ticks = 1;
        period_ticks -= 1;

        TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
        TIM_TimeBaseInitStructure.TIM_Prescaler = prescaler;
        TIM_TimeBaseInitStructure.TIM_Period = period_ticks;
        TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
        TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
        TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;

        TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);

        NVIC_InitTypeDef NVIC_InitStructure;
        NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);
        TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
        TIM_Cmd(TIM3, ENABLE);

        // TIM4

        RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

        TIM_TimeBaseInitStructure.TIM_Prescaler = 143;
        TIM_TimeBaseInitStructure.TIM_Period = 9999; // 100ms 定时器周期
        TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
        TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
        TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;

        TIM_TimeBaseInit(TIM4, &TIM_TimeBaseInitStructure);
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);

        NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);
        TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
        TIM_Cmd(TIM4, ENABLE);
    }

    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
    Set_USB_Clock();
    USB_Init();
    USB_Interrupts_Config();

    // 注册 HID 通信回调函数
    // hid_comm_set_callback(my_hid_comm_callback);

    PRINT("App Init OK!\r\n");
}

void app_run(void)
{
    hid_comm_process();
    switch (key_scan_state)
    {
    case KEY_STATE_SCANNING:
        /* 检查 DMA 传输是否完成 */
        if (key_transfer_complete())
        {
            /* 将本次采样存入指定槽位 */
            key_copy_snapshot(last_snapshot);
            key_store_sample(active_sample_slot, last_snapshot);

            /* 如果凑够 3 次采样（即当前收集的是第 3 个样本），执行多数投票 */
            if (active_sample_slot == (KEY_SAMPLE_WINDOW - 1))
            {
                key_do_filter_and_update();
            }

            next_sample_slot = (active_sample_slot + 1) % KEY_SAMPLE_WINDOW;
            key_scan_state = KEY_STATE_IDLE;
            scan_timeout_ticks = 0;
            TIM_Cmd(TIM3, ENABLE);
        }
        else if (scan_timeout_ticks >= KEY_SCAN_TIMEOUT_TICKS)
        {
            key_scan_state = KEY_STATE_IDLE;
            scan_timeout_ticks = 0;
            TIM_Cmd(TIM3, ENABLE);
            PRINT("Key scan timeout, recovery\r\n");
        }
        break;
    default:
        break;
    }

    /* 定期输出数据（每秒一次）*/
    if (scan_tick_counter >= KEYBOARD_SCAN_FREQUENCY_HZ)
    {
        if (key_get_debounce_state(2) == KEY_DEBOUNCE_PRESSED)
        {
            key_pressed_count = key_pressed_count + 1;
        }
        else
        {
            key_pressed_count = 0;
        }
        scan_tick_counter = 0;
        if (key_pressed_count >= 2)
        {
            if (debug)
            {
                debug = 0;
                GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
                PRINT("Debug mode disabled\r\n");
            }
            else
            {
                debug = 1;
                GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, DISABLE);
                PRINT("Debug mode enabled\r\n");
            }
            key_pressed_count = 0;
        }
    }
}

RAM INTF void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET)
    {
        scan_tick_counter++;

        /* 如果当前空闲，启动新的扫描 */
        if (key_scan_state == KEY_STATE_IDLE)
        {
            key_scan_state = KEY_STATE_SCANNING;
            active_sample_slot = next_sample_slot;
            scan_timeout_ticks = 0;
            TIM_Cmd(TIM3, DISABLE);
            key_start_scan();
        }
        else if (scan_timeout_ticks < 0xFF)
        {
            scan_timeout_ticks++;
        }
        // kb_send_snapshot(last_snapshot);
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    }
}

RAM INTF void TIM4_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) == SET)
    {
        // hid_comm_send(last_snapshot, HC165_COUNT);
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    }
}
