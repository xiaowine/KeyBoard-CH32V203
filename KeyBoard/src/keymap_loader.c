#include "keymap_loader.h"

#include <string.h>

#include "keymap.h"

// 链接脚本导出的符号：FLASH 中 .config 段起始地址（LMA）
extern uint8_t _config_lma[]; // NOLINT(*-reserved-identifier)

// 运行时记录当前已加载层索引；0xFF 表示未加载
static uint8_t loaded_layer = (uint8_t)-1;

static void keymap_loader_dump_loaded_layer(uint8_t layer)
{
    PRINT("Keymap loader: dump loaded layer %u\r\n", (unsigned)layer);
    for (uint8_t i = 0; i < (uint8_t)KEY_TOTAL_KEYS; ++i)
    {
        const KeyMapping *m = &keymap_active[i];
        PRINT("  key[%02u] mod=0x%02X type=%u codes=[0x%04X,0x%04X,0x%04X]\r\n", (unsigned)i, (unsigned)m->modifiers,
              (unsigned)m->type, (unsigned)m->codes[0], (unsigned)m->codes[1], (unsigned)m->codes[2]);
    }
}

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

    if (loaded_layer != (uint8_t)-1)
    {
        keymap_loader_dump_loaded_layer(loaded_layer);
    }
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

    /* 空层检测：
     * 1) 整层全 0x00
     * 2) 整层全 0xFF
     * 3) 满足 0xE339 图样：字读 0xE339E339，半字读 0xE339，
     *    字节读按地址奇偶分别为 0x39 / 0xE3
     */
    int all_zero = 1;
    int all_ff = 1;
    int all_e339 = 1;
    for (size_t i = 0; i < layer_size; ++i)
    {
        const uint8_t b = src[i];
        if (b != 0x00)
            all_zero = 0;
        if (b != 0xFF)
            all_ff = 0;

        const uintptr_t addr = (uintptr_t)(src + i);
        const uint8_t expected = (addr & 1u) == 0u ? 0x39u : 0xE3u;
        if (b != expected)
            all_e339 = 0;

        if (!all_zero && !all_ff && !all_e339)
            break;
    }

    if (all_zero || all_ff || all_e339)
    {
        const char *reason = all_zero ? "0x00" : (all_ff ? "0xFF" : "0xE339 pattern");
        PRINT("Keymap loader: layer %u empty (all %s), skip loading\r\n", (unsigned)layer, reason);
        return;
    }

    memcpy((void *)keymap_active, src, layer_size);
    loaded_layer = layer;
}