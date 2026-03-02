#include "app.h"
#include "key.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include <string.h>

#include "hid_comm.h"

static volatile key_scan_state_t key_state = KEY_STATE_IDLE;
static u16 scan_tick_counter = 0;
static u8 last_snapshot[HC165_COUNT];
static u8 key_pressed_count = 0;
static u8 debug = 0;

// uint8_t KB_Data_Pack[DEF_ENDP_SIZE_KB] = {0x00}; // Keyboard IN Data Packet
// uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len);

void my_hid_comm_callback(uint8_t *data, uint16_t len);

void app_init(void)
{

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    // IO 初始化
    {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

        GPIO_InitTypeDef GPIO_InitStructure = {0};
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

        GPIO_InitStructure.GPIO_Pin = ENCODE_A | ENCODE_B;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(ENCODE_PORT, &GPIO_InitStructure);

        GPIO_InitStructure.GPIO_Pin = KEY_CE | KEY_PL;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(KEY_PORT, &GPIO_InitStructure);
        GPIO_SetBits(KEY_PORT, KEY_CE | KEY_PL);

        GPIO_InitStructure.GPIO_Pin = KEY_MISO;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
        GPIO_Init(KEY_PORT, &GPIO_InitStructure);

        GPIO_InitStructure.GPIO_Pin = KEY_CP;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
        GPIO_Init(KEY_PORT, &GPIO_InitStructure);
    }

#pragma region
// 编码器初始化（TIM）
// {
//     RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

//  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
//  TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
//  TIM_TimeBaseStructure.TIM_Prescaler = 0x0;
//  TIM_TimeBaseStructure.TIM_Period = 0xFFFF;
//  TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
//  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
//  TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

//  TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

//  TIM_ICInitTypeDef TIM_ICInitStructure;
//  TIM_ICStructInit(&TIM_ICInitStructure);
//  TIM_ICInitStructure.TIM_ICFilter = 10;
//  TIM_ICInit(TIM2, &TIM_ICInitStructure);

//  NVIC_InitTypeDef NVIC_InitStructure;
//  NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
//  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
//  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
//  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
//  NVIC_Init(&NVIC_InitStructure);

//  TIM_ClearFlag(TIM2, TIM_FLAG_Update);
//  TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
//  TIM_SetCounter(TIM2, (TIM_TimeBaseStructure.TIM_Period + 1) / 2);
//  TIM_Cmd(TIM2, ENABLE);
// }
#pragma endregion

    /* 键盘模块初始化：配置 SPI + DMA 以读取 74HC165 */
    key_init();

    // TIM 初始化
    {
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

        TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

        // 设置定时器频率为 10 kHz（即 0.1 ms 的分辨率）
        const u16 tick_freq = 10000U;
        // 计算预分频值
        u16 prescaler = SystemCoreClock / tick_freq;
        // 预分频值至少为 1
        if (prescaler == 0)
            prescaler = 1;
        prescaler -= 1;

        // 计算周期：100 ms = 0.1 s -> ticks = tick_freq * 0.1 = tick_freq / 10
        u16 period_ticks = tick_freq / KEYBOARD_SCAN_FREQUENCY_HZ;
        // 周期至少为 1
        if (period_ticks == 0)
            period_ticks = 1;

        TIM_TimeBaseInitStructure.TIM_Prescaler = (u16)prescaler;
        TIM_TimeBaseInitStructure.TIM_Period = (u16)(period_ticks - 1);
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
    }

    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();

    // 注册 HID 通信回调函数
    hid_comm_set_callback(my_hid_comm_callback);

    PRINT("App Init OK!\r\n");
}

RAM void app_run(void)
{
    hid_comm_process();
    switch (key_state)
    {
    case KEY_STATE_IDLE:
        /* 空闲状态，无操作 */
        break;

    case KEY_STATE_SCANNING:
        /* 检查 DMA 传输是否完成 */
        if (key_transfer_complete())
        {
            /* 将模块快照复制到应用层的 last_snapshot */
            key_copy_snapshot(last_snapshot);
            key_state = KEY_STATE_DATA_READY;
        }
        break;

    case KEY_STATE_DATA_READY:
        /* 数据处理完毕，回到空闲状态 */
        key_state = KEY_STATE_IDLE;
        break;
    }

    /* 定期输出数据（每秒一次）*/
    if (scan_tick_counter >= KEYBOARD_SCAN_FREQUENCY_HZ)
    {
        if (key_is_pressed(2))
        {
            key_pressed_count = key_pressed_count + 1;
        }
        else
        {
            key_pressed_count = 0;
        }
        scan_tick_counter = 0;
        output_data((const u8 *)last_snapshot);
        hid_comm_send((u8 *)last_snapshot, HC165_COUNT);
    }
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

INTF void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET)
    {
        scan_tick_counter++;

        /* 如果当前空闲，启动新的扫描 */
        if (key_state == KEY_STATE_IDLE)
        {
            key_state = KEY_STATE_SCANNING;
            key_start_scan();
        }
        // if (key_is_pressed(16))
        // {
        //     KB_Data_Pack[2] = 0x1A;
        //     KB_Data_Pack[3] = 0x1B;
        // }
        // else
        // {
        //     KB_Data_Pack[2] = 0x00;
        //     KB_Data_Pack[3] = 0x00;
        // }

        // uint8_t status = USBD_ENDPx_DataUp(ENDP1, KB_Data_Pack, DEF_ENDP_SIZE_KB);
    }
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
}

void my_hid_comm_callback(uint8_t *data, uint16_t len)
{
    PRINT("App: Received %d bytes from HID comm\r\n", len);
    // 这里可以添加对接收到的数据的处理逻辑
    for (uint16_t i = 0; i < len; i++)
    {
        PRINT("  Byte %d: 0x%02X\r\n", i, data[i]);
    }
}