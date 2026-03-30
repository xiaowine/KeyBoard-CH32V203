#include "config_manager.h"
#include <string.h>
#include "keymap.h"
#include "config.h"
#include "debug.h"
#include "utils.h"

extern uint8_t _config_lma[]; // NOLINT(*-reserved-identifier)
extern KeyMap_t active_keymap[TOTAL_KEYS];
extern RGB_Color_t active_rgb_color[CONFIG_RGB_COLOR_PATH_NUM];

ConfigHeader_t config_boot_config_ram = {0};

uint32_t config_flash_shadow_words[CONFIG_FLASH_BYTE / sizeof(uint32_t)] = {0};

void config_manager_init(void)
{
    const size_t layer_size = CONFIG_KEYMAP_LAYER_BYTE;

    memset(active_keymap, 0, layer_size);
    config_read_header(&config_boot_config_ram);

    const uint8_t requested_keymap_layer = config_boot_config_ram.bits.boot_layer;
    const uint8_t requested_rgb_color_layer = config_boot_config_ram.bits.rgb_color_layer;
    const uint8_t open_rgb_led = config_boot_config_ram.bits.open_rgb_led;
    config_boot_config_ram.bits.boot_layer = -1;
    config_boot_config_ram.bits.rgb_color_layer = -1;

    if (config_load_keymap_layer(requested_keymap_layer))
    {
        config_boot_config_ram.bits.boot_layer = requested_keymap_layer;
    }

    if (open_rgb_led)
    {
        if (config_load_rgb_color_layer(requested_rgb_color_layer))
        {
            config_boot_config_ram.bits.rgb_color_layer = requested_rgb_color_layer;
        }
    }


#ifdef DEBUG_H

    PRINT("boot_layer=%u max_layers=%d,rgb_color_layer=%u,open_rgb_led=%u,normal_mode=%d\r\n",
          (unsigned)requested_keymap_layer, CONFIG_KEYMAP_LAYERS_NUM, requested_rgb_color_layer, open_rgb_led,
          config_boot_config_ram.bits.normal_mode);
    for (uint8_t i = 0; i < TOTAL_KEYS; i++)
    {
        PRINT("modifiers=%d codes=%x %x %x  type=%u \r\n",
              active_keymap[i].modifiers,
              active_keymap[i].codes[0], active_keymap[i].codes[1], active_keymap[i].codes[2],
              active_keymap[i].type);
    }
    for (uint8_t i = 0; i < CONFIG_RGB_COLOR_NUM; i++)
    {
        PRINT("RGB %d %d %d\r\n", active_rgb_color[i].r, active_rgb_color[i].g, active_rgb_color[i].b);
    }
#endif
}

// 以 page 为单位写入 flash，先读出整页数据到 RAM，修改对应范围后擦除并写回整页，最后验证写入结果。
uint8_t config_write_flash_range(const uint8_t* lma_addr, const uint8_t* data, const uint32_t data_len,
                                 const char* log_tag)
{
    uint8_t* const shadow_bytes = (uint8_t*)config_flash_shadow_words;

    if (data == NULL || data_len == 0u || lma_addr == NULL)
    {
        PRINT("%s: invalid data\r\n", log_tag);
        return 0;
    }

    const uint8_t* const flash_base = &_config_lma[0];
    const uint32_t data_offset = (uint32_t)(lma_addr - flash_base);

    if (data_offset + data_len > CONFIG_FLASH_BYTE)
    {
        PRINT("%s: out of range, offset=%u len=%u\r\n", log_tag, (unsigned)data_offset, (unsigned)data_len);
        return 0;
    }
    const uint32_t aligned_start = data_offset & ~(CONFIG_FLASH_PAGE_BYTE - 1u);
    const uint32_t aligned_end = (data_offset + data_len + (CONFIG_FLASH_PAGE_BYTE - 1u)) & ~(CONFIG_FLASH_PAGE_BYTE -
        1u);
    const uint32_t aligned_len = aligned_end - aligned_start;
    const uint32_t patch_offset = data_offset - aligned_start;

    memcpy(shadow_bytes, &_config_lma[aligned_start], aligned_len);
    memcpy(shadow_bytes + patch_offset, data, data_len);

    const uint32_t target_addr = FLASH_BASE + (uint32_t)&_config_lma[aligned_start];

    FLASH_Status status = FLASH_ROM_ERASE(target_addr, aligned_len);
    if (status != FLASH_COMPLETE)
    {
        PRINT("%s: flash erase failed, status=%d\r\n", log_tag, status);
        return 0;
    }

    status = FLASH_ROM_WRITE(target_addr, config_flash_shadow_words, aligned_len);
    if (status != FLASH_COMPLETE)
    {
        PRINT("%s: flash write failed, status=%d\r\n", log_tag, status);
        return 0;
    }

    if (memcmp(&_config_lma[data_offset], data, data_len) != 0)
    {
        PRINT("%s: flash verify failed\r\n", log_tag);
        return 0;
    }

    return 1;
}

void config_read_header(ConfigHeader_t* config)
{
    if (config == NULL)
    {
        return;
    }

    memcpy(&config->raw, &_config_lma[0], sizeof(config->raw));
}

uint8_t config_write_header(const ConfigHeader_t* config)
{
    uint32_t page_words[CONFIG_FLASH_PAGE_BYTE / sizeof(uint32_t)] = {0};
    const uint32_t config_addr = FLASH_BASE + (uint32_t)&_config_lma[0];
    ConfigHeader_t flash_config = {0};

    if (config == NULL)
    {
        return 0;
    }

    memcpy(page_words, &_config_lma[0], CONFIG_FLASH_PAGE_BYTE);
    memcpy(page_words, &config->raw, sizeof(config->raw));

    FLASH_Status status = FLASH_ROM_ERASE(config_addr, CONFIG_FLASH_PAGE_BYTE);
    if (status != FLASH_COMPLETE)
    {
        PRINT("Set boot layer: flash erase failed, status=%d\r\n", status);
        return 0;
    }

    status = FLASH_ROM_WRITE(config_addr, page_words, CONFIG_FLASH_PAGE_BYTE);
    if (status != FLASH_COMPLETE)
    {
        PRINT("Set boot layer: flash write failed, status=%d\r\n", status);
        return 0;
    }

    config_read_header(&flash_config);
    if (flash_config.raw != config->raw)
    {
        PRINT("Set boot layer: flash verify failed\r\n");
        return 0;
    }

    return 1;
}

uint8_t config_load_keymap_layer(const uint8_t layer_index)
{
    if (layer_index >= CONFIG_KEYMAP_LAYERS_NUM)
    {
        PRINT("KeyMap Config loader: requested layer %u out of range, skip loading\r\n", (unsigned)layer_index);
        return 0;
    }

    const uint8_t* src = config_keymap_layer_address(layer_index);

    memcpy(active_keymap, src, CONFIG_KEYMAP_LAYER_BYTE);
    config_boot_config_ram.bits.boot_layer = layer_index;
    return 1;
}

uint8_t config_write_keymap_layer(const uint8_t layer, const uint8_t* layer_data, const uint16_t layer_data_len)
{
    if (layer >= CONFIG_KEYMAP_LAYERS_NUM)
    {
        PRINT("Set layer config: layer %u out of range\r\n", (unsigned)layer);
        return 0;
    }

    if (layer_data_len != CONFIG_KEYMAP_LAYER_BYTE)
    {
        PRINT("Set layer config: invalid payload length %u, expected %u\r\n",
              (unsigned)layer_data_len, (unsigned)CONFIG_KEYMAP_LAYER_BYTE);
        return 0;
    }
    const uint8_t* addr = config_keymap_layer_address(layer);
    if (!config_write_flash_range(addr, layer_data, CONFIG_KEYMAP_LAYER_BYTE, "Set layer config"))
    {
        return 0;
    }

    const int active_layer = config_boot_config_ram.bits.boot_layer;
    if (active_layer == (int)layer)
    {
        memcpy(active_keymap, layer_data, CONFIG_KEYMAP_LAYER_BYTE);
    }

    return 1;
}

uint8_t config_write_all_keymap_layer(const uint8_t* layers_data, const uint16_t layers_data_len)
{
    const uint32_t all_layers_size = (uint32_t)CONFIG_KEYMAP_LAYER_BYTE * (CONFIG_KEYMAP_LAYERS_NUM);
    const int active_layer = config_boot_config_ram.bits.boot_layer;

    if ((uint32_t)layers_data_len != all_layers_size)
    {
        PRINT("Set all layer config: invalid payload length %u, expected %u\r\n",
              (unsigned)layers_data_len, (unsigned)all_layers_size);
        return 0;
    }

    const uint8_t* addr = config_keymap_layer_address(0);
    if (!config_write_flash_range(addr, layers_data, all_layers_size,
                                  "Set all layer config"))
    {
        return 0;
    }

    if (active_layer >= 0 && active_layer < CONFIG_KEYMAP_LAYERS_NUM)
    {
        memcpy(active_keymap, layers_data + ((uint32_t)active_layer * (uint32_t)CONFIG_KEYMAP_LAYER_BYTE),
               CONFIG_KEYMAP_LAYER_BYTE);
    }

    return 1;
}

uint8_t config_load_rgb_color_layer(const uint8_t layer_index)
{
    if (layer_index >= CONFIG_RGB_COLOR_NUM)
    {
        PRINT("Color Config loader: requested layer %u out of range, skip loading\r\n", (unsigned)layer_index);
        return 0;
    }

    const uint8_t* src = config_rgb_color_layer_address(layer_index);

    memcpy(active_rgb_color, src, CONFIG_RGB_COLOR_LAYER_BYTE);
    config_boot_config_ram.bits.rgb_color_layer = layer_index;
    return 1;
}

uint8_t config_write_rgb_color_layer(const uint8_t layer, const uint8_t* color_data, const uint16_t color_data_len)
{
    const int active_rgb_color_layer = config_boot_config_ram.bits.rgb_color_layer;
    if (active_rgb_color_layer >= CONFIG_RGB_COLOR_NUM)
    {
        PRINT("Set RGB color layer: layer %u out of range\r\n", (unsigned)layer);
        return 0;
    }
    if (color_data_len != CONFIG_RGB_COLOR_LAYER_BYTE)
    {
        PRINT("Set color config: invalid payload length %u, expected %u\r\n",
              (unsigned)color_data_len, (unsigned)CONFIG_RGB_COLOR_LAYER_BYTE);
        return 0;
    }
    const uint8_t* addr = config_rgb_color_layer_address(layer);
    if (!config_write_flash_range(addr, color_data, CONFIG_RGB_COLOR_LAYER_BYTE, "Set RGB color layer"))
    {
        return 0;
    }
    if (active_rgb_color_layer == layer)
    {
        memcpy(active_rgb_color, color_data, CONFIG_RGB_COLOR_LAYER_BYTE);
    }
    return 1;
}
