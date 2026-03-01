#include "key.h"
#include <string.h>

u8 *gekey(void)
{
    static u8 data[HC165_COUNT];
    /*
     * 74HC165 时序：
     * - 保持 CE 为高（时钟禁止）同时通过 PL (/CS) 加载并行输入。
     * - 将 PL 脉冲从低变高来锁存输入。
     * - 将 CE 拉低以启用时钟并执行 SPI 时钟读取。
     */
    KEY_DISABLE_CLOCK(); /* 禁止时钟 */
    KEY_LOAD_PL();       /* 加载并行输入 */
    KEY_ENABLE_CLOCK();  /* 启用时钟 */

    for (u8 i = 0; i < HC165_COUNT; i++)
    {
        // 1. 必须先发送一个字节（空数据），以产生 8 个 SCK 时钟
        while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)
            ;
        SPI_I2S_SendData(SPI1, 0xFF);

        // 2. 等待接收完成（165的数据被“挤”进来了）
        while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET)
            ;
        data[i] = SPI_I2S_ReceiveData(SPI1);
    }

    KEY_DISABLE_CLOCK(); /* 禁止时钟 */
    return data;
}

#pragma region
// /*
//  * 对 keyboard_gekey 多次采样进行简单去抖：
//  * - attempts: 最多采样次数（例如 3-10）
//  * - delay_ms: 采样间隔（毫秒，简单忙等待）
//  * 返回指向内部稳定数据的指针。
//  * 实现注意：该函数使用内部静态缓冲，返回的指针在下一次调用时会被覆盖。
//  */
// u8 *gekey_filtered(u8 attempts, unsigned int delay_ms)
// {
//     static u8 stable[HC165_COUNT];
//     static u8 last[HC165_COUNT];
//     static u8 cur[HC165_COUNT];

//     if (attempts == 0)
//         attempts = 1;

//     /* 第一次读取 */
//     u8 *p = gekey();
//     memcpy(last, p, HC165_COUNT * sizeof(u8));

//     for (u8 a = 1; a < attempts; ++a)
//     {
//         Delay_Ms(delay_ms);

//         p = gekey();
//         memcpy(cur, p, HC165_COUNT * sizeof(u8));

//         if (memcmp(last, cur, HC165_COUNT * sizeof(u8)) == 0)
//         {
//             memcpy(stable, cur, HC165_COUNT * sizeof(u8));
//             return stable;
//         }

//         memcpy(last, cur, HC165_COUNT * sizeof(u8));
//     }

//     /* 未达到完全稳定，返回最后一次采样结果 */
//     memcpy(stable, last, HC165_COUNT * sizeof(u8));
//     return stable;
// }
#pragma endregion

void output_data(const u8 *data)
{
    char buffer[256];
    int offset = 0;
    for (u8 i = 0; i < HC165_COUNT; ++i)
    {
        // header（使用 snprintf 确保不溢出）
        int n = snprintf(buffer + offset, sizeof(buffer) - offset, "line%u: ", (unsigned)i);
        if (n < 0 || n >= (int)(sizeof(buffer) - offset))
            break;
        offset += n;

        // 直接写位字符，避免 sprintf/printf 在内循环中
        for (int bit = 7; bit >= 0; --bit)
        {
            if (offset >= (int)sizeof(buffer) - 1)
                break;
            buffer[offset++] = ((data[i] >> bit) & 1) ? '1' : '0';
        }

        if (offset >= (int)sizeof(buffer) - 2)
            break;
        buffer[offset++] = '\r';
        buffer[offset++] = '\n';
    }
    buffer[offset] = '\0';
    PRINT("%s", buffer);
}
