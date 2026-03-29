#include "app.h"

#include <string.h>
#include "comm_controller.h"
#include "encode.h"
#include "key.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "keymap.h"
#include "keymap_loader.h"
#include "rgb_led.h"

volatile key_scan_state_t key_scan_state = KEY_STATE_IDLE;
uint8_t last_snapshot[HC165_COUNT];
uint8_t debug_key_pressed_count = 0;
uint8_t is_debug_mode = 0;

/* 子采样槽位管理（0 -> 1 -> 2 -> 0） */
uint8_t next_sample_slot = 0;
volatile uint8_t active_sample_slot = 0;
volatile uint8_t scan_timeout_ticks = 0;
volatile uint8_t time1ms_tick = 0;
volatile uint8_t time5ms_tick = 0;

void app_comm_rx_callback(uint8_t payload_type, const uint8_t* payload, uint16_t payload_len);

void app_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_CRC, ENABLE);

    /* 按 FLASH 中的掩码（KEYMAP_BOOT_CONFIG）选择性加载 keymap 层到 RAM */
    keymap_loader_init();

    // 编码器初始化（TIM）
    encode_init();

    /* 键盘模块初始化：配置 SPI + DMA 以读取 74HC165 */
    key_init();

    // RGB 初始化
    rgb_led_init();
    // TIM 初始化
    {
        RCC_APB2PeriphClockCmd(KEYSCAN_TIM_RCC, ENABLE);

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

        TIM_TimeBaseInit(KEYSCAN_TIM, &TIM_TimeBaseInitStructure);
        TIM_ClearITPendingBit(KEYSCAN_TIM, TIM_IT_Update);

        NVIC_InitTypeDef NVIC_InitStructure;
        NVIC_InitStructure.NVIC_IRQChannel = KEYSCAN_TIM_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);
        TIM_ITConfig(KEYSCAN_TIM, TIM_IT_Update, ENABLE);
        TIM_Cmd(KEYSCAN_TIM, ENABLE);

        // TIM2 已移除：改用 TIM1 的中断来产生额外的 5ms 定时标志
    }
    Set_USB_Clock();
    USB_Init();
    USB_Interrupts_Config();
    comm_register_rx_callback(app_comm_rx_callback);
    PRINT("App Init OK!\r\n");
}

void app_run(void)
{
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
            TIM_Cmd(KEYSCAN_TIM, ENABLE);
        }
        else if (scan_timeout_ticks >= KEY_SCAN_TIMEOUT_TICKS)
        {
            key_scan_state = KEY_STATE_IDLE;
            scan_timeout_ticks = 0;
            TIM_Cmd(KEYSCAN_TIM, ENABLE);
            PRINT("Key scan timeout, recovery\r\n");
        }
        break;
    default:
        break;
    }

    if (time1ms_tick)
    {
        kb_send_snapshot(last_snapshot);
        time1ms_tick = 0;
    }
    if (time5ms_tick)
    {
        comm_controller_process();
        time5ms_tick = 0;
    }
}

RAM INTF void KEYSCAN_TIM_IRQHANDLER(void)
{
    if (TIM_GetITStatus(KEYSCAN_TIM, TIM_IT_Update) == SET)
    {
        time1ms_tick = 1;
        // 由 TIM1 额外产生 5ms 标志（每 5 个 update 触发一次）
        static uint8_t tick5ms_counter = 0;
        tick5ms_counter++;
        if (tick5ms_counter >= 5)
        {
            time5ms_tick = 1;
            tick5ms_counter = 0;
        }
        /* 如果当前空闲，启动新的扫描 */
        if (key_scan_state == KEY_STATE_IDLE)
        {
            key_scan_state = KEY_STATE_SCANNING;
            active_sample_slot = next_sample_slot;
            scan_timeout_ticks = 0;
            TIM_Cmd(KEYSCAN_TIM, DISABLE);
            key_start_scan();
        }
        else if (scan_timeout_ticks < 0xFF)
        {
            scan_timeout_ticks++;
        }
        TIM_ClearITPendingBit(KEYSCAN_TIM, TIM_IT_Update);
    }
}

void app_comm_rx_callback(const uint8_t payload_type, const uint8_t* payload, const uint16_t payload_len)
{
    switch (payload_type)
    {
    case DATA_TYPE_GET_KEY:
        {
            comm_queue_reply(DATA_TYPE_GET_KEY, last_snapshot, HC165_COUNT);
            break;
        }
    case DATA_TYPE_GET_LAYER_KEYMAP:
        {
            const uint16_t layer_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS;
            /* 发送当前运行时已加载的一层（位于 keymap_active） */
            comm_queue_reply(DATA_TYPE_GET_LAYER_KEYMAP, (uint8_t*)keymap_active, layer_size);
            break;
        }
    case DATA_TYPE_GET_ALL_LAYER_KEYMAP:
        {
            /* 从 FLASH 镜像读取所有层的连续数据（紧凑镜像布局） */
            // NOLINTNEXTLINE(*-reserved-identifier)
            extern uint8_t _config_lma[]; /* LMA 起始符号，定义在 keymap_image.o 中 */
            const uint16_t layer_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS;
            const uint8_t* flash_base = &_config_lma[0];
            const uint8_t* layers_src = flash_base + sizeof(KeymapBootConfig);
            comm_queue_reply(DATA_TYPE_GET_ALL_LAYER_KEYMAP, layers_src, layer_size * (KEYMAP_LAYERS));
            break;
        }
    case DATA_TYPE_SET_LAYER:
        {
            uint8_t layer = 0;
            if (payload_len < sizeof(layer))
            {
                return;
            }
            memcpy(&layer, payload, sizeof(layer));
            if (layer >= KEYMAP_LAYERS)
            {
                return;
            }
            keymap_loader_load_layer(layer);
            break;
        }
    case DATA_TYPE_SET_BOOT_LAYER:
        {
            uint8_t layer = 0;
            if (payload_len < sizeof(layer))
            {
                return;
            }
            memcpy(&layer, payload, sizeof(layer));
            if (layer >= KEYMAP_LAYERS)
            {
                return;
            }
            KeymapBootConfig keymap_boot_config = {0};

            keymap_loader_read_boot_config(&keymap_boot_config);
            keymap_boot_config.bits.boot_layer = layer;
            if (keymap_loader_write_boot_config(&keymap_boot_config))
            {
                PRINT("Set boot layer: %u\r\n", (unsigned)layer);
            }
            break;
        }
    case DATA_TYPE_SET_LAYER_KEYMAP:
        {
            const int active_layer = keymap_boot_config_ram.bits.boot_layer;
            const uint16_t layer_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS;

            if ((active_layer < 0) || (active_layer >= KEYMAP_LAYERS))
            {
                PRINT("Set layer keymap: no active layer\r\n");
                return;
            }

            if (payload_len != layer_size)
            {
                PRINT("Set layer keymap: invalid payload length %u, expected %u\r\n",
                      (unsigned)payload_len, (unsigned)layer_size);
                return;
            }

            if (keymap_loader_write_layer((uint8_t)active_layer, payload, payload_len))
            {
                PRINT("Set layer keymap: layer=%u\r\n", (unsigned)active_layer);
            }
            break;
        }
    case DATA_TYPE_SET_ALL_LAYER_KEYMAP:
        {
            const uint16_t all_layers_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS * KEYMAP_LAYERS;

            if (payload_len != all_layers_size)
            {
                PRINT("Set all layer keymap: invalid payload length %u, expected %u\r\n",
                      (unsigned)payload_len, (unsigned)all_layers_size);
                return;
            }

            if (keymap_loader_write_all_layers(payload, payload_len))
            {
                PRINT("Set all layer keymap: layers=%u\r\n", (unsigned)KEYMAP_LAYERS);
            }
            break;
        }
    default:
        break;
    }
}
