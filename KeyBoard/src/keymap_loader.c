#include "keymap_loader.h"

#include <string.h>

#include "keymap.h"

// 链接脚本导出的符号：FLASH 中 .config 段起始地址（LMA）
extern uint8_t _config_lma[]; // NOLINT(*-reserved-identifier)

// 运行时记录当前已加载层索引；0xFF 表示未加载
static uint8_t loaded_layer = (uint8_t)-1;

// 启动初始化：从 FLASH 读取 32bit 配置头，并按 boot_layer 尝试加载
void keymap_loader_init(void)
{
    const size_t layer_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS;
    memset(keymap_active, 0, layer_size);

    const KeymapBootConfig boot_cfg = {.raw = *(const uint32_t *)&_config_lma[0]};
    const uint8_t layer_byte = (uint8_t)boot_cfg.bits.boot_layer;
    const uint8_t ver_cfg = (uint8_t)boot_cfg.bits.ver;

    int active_layer = -1;
    if (layer_byte < (uint8_t)KEYMAP_LAYERS)
    {
        keymap_loader_load_layer(layer_byte);
    }
    else
    {
        PRINT("Keymap loader: boot layer %u out of range, skip loading\r\n", (unsigned)layer_byte);
    }

    if (loaded_layer != (uint8_t)-1)
    {
        active_layer = (int)loaded_layer;
    }

    PRINT("Keymap loader: boot_layer=%u, ver_cfg=0x%X, active_layer=%d, max_layers=%d\r\n", (unsigned)layer_byte,
          (unsigned)ver_cfg, active_layer, (int)KEYMAP_LAYERS);
}

uint8_t keymap_loader_loaded_layer(void)
{
    return loaded_layer;
}

// 将指定层从 FLASH 镜像加载到 keymap_active
void keymap_loader_load_layer(uint8_t layer)
{
    if (layer >= KEYMAP_LAYERS)
    {
        PRINT("Keymap loader: requested layer %u out of range, skip loading\r\n", (unsigned)layer);
        return;
    }

    const size_t layer_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS;

    // FLASH 布局：前 4 字节为配置头，后续是层数据
    const uint8_t *flash_base = (const uint8_t *)&_config_lma[0];
    const uint8_t *layers_src = flash_base + sizeof(KeymapBootConfig);
    const uint8_t *src = layers_src + (size_t)layer * layer_size;

    /* 空层检测：整层全 0x00 或全 0xFF 视为未写入 */
    int all_zero = 1;
    int all_ff = 1;
    for (size_t i = 0; i < layer_size; ++i)
    {
        const uint8_t b = src[i];
        if (b != 0x00)
            all_zero = 0;
        if (b != 0xFF)
            all_ff = 0;
        if (!all_zero && !all_ff)
            break;
    }

    if (all_zero || all_ff)
    {
        const char *reason = all_zero ? "0x00" : "0xFF";
        PRINT("Keymap loader: layer %u empty (all %s), skip loading\r\n", (unsigned)layer, reason);
        return;
    }

    memcpy((void *)keymap_active, src, layer_size);
    loaded_layer = layer;
}
