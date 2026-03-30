#include "common.h"
#include "keymap.h"

#include "config.h"
#include "rgb.h"

extern uint8_t _config_lma[]; // NOLINT(*-reserved-identifier)


// De Bruijn 序列表 (长度 32)
//
// 说明：
// 该表与一个 De Bruijn 常数 0x077CB531U 配合使用，用来在 O(1) 时间内
// 通过将仅包含最低置位的值乘以该常数并右移得到的索引，快速映射到最低置位的位位置。
// 实现步骤：
//  1. v & -v -> 仅保留最低的 1 位（LSB）。
//  2. 将结果乘以 De Bruijn 常数 0x077CB531U。
//  3. 右移 27 位得到一个 0..31 的索引，用于查表。
//
// 注意：
//  - 本表与所选常数和位宽（32 位）配套使用，适用于 32 位无符号整数。
//  - 当输入 v == 0 时，本方法并不表示任何置位；当前实现会返回表的第 0 项（即 0），
//    调用者如需区分 v==0 的情况，应在调用前进行检查。
static const uint8_t DeBruijnTable[32] = {
    0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
    31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
};

/**
 * @brief 获取最低置位（least significant 1 bit）的位索引（0-31）。
 *
 * 该函数使用 De Bruijn 技术在常数时间内计算出最低置位的索引。
 * 实现细节见上方 De BruijnTable 注释。
 *
 * @param v 输入值（uint32_t）。函数内部会执行 v & -v，仅保留最低 1 位。
 * @return uint32_t 返回最低置位的索引（0-31）。当输入 v==0 时，返回 0（调用者若需特别处理应先检查）。
 */
uint32_t get_bit_index(uint32_t v)
{
    // 只保留最低的 1 位
    v &= -v;

    // 与 De Bruijn 常数相乘后右移得到索引
    const uint32_t hash = v * 0x077CB531U;

    const uint32_t index = hash >> 27;

    return DeBruijnTable[index];
}

// 将 value 按照 scale（0-255）进行缩放，结果也在 0-255 范围内。
uint8_t scale8_by_255(const uint8_t value, const uint8_t scale)
{
    const uint16_t t = (uint16_t)value * (uint16_t)scale;
    return (uint8_t)((t + (t >> 8) + 1U) >> 8);
}

// 获取 KeyMap_t 结构中有效 codes 的数量
uint8_t km_get_code_count(const KeyMap_t* m)
{
    for (uint8_t i = 0; i < MAX_CODE; ++i)
    {
        if (m->codes[i] == 0)
            return i;
    }
    return MAX_CODE;
}

const uint8_t* config_keymap_layer_address(const uint8_t index)
{
    const uint8_t* flash_base = &_config_lma[0];
    const uint8_t* layers_src = flash_base + sizeof(ConfigHeader_t) + CONFIG_RGB_COLOR_TOTAL_LAYERS_BYTE;
    return layers_src + index * CONFIG_KEYMAP_LAYER_BYTE;
}

const uint8_t* config_rgb_color_layer_address(const uint8_t index)
{
    const uint8_t* flash_base = &_config_lma[0];
    const uint8_t* layers_src = flash_base + sizeof(ConfigHeader_t);
    return layers_src + index * CONFIG_RGB_COLOR_LAYER_BYTE;
}
