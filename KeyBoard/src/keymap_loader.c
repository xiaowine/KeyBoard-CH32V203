#include "keymap_loader.h"
#include <string.h>
#include "keymap.h"

extern uint8_t _config_lma[]; // NOLINT(*-reserved-identifier)

KeymapBootConfig keymap_boot_config_ram = {0};

static const uint8_t* keymap_loader_layer_src(uint8_t layer)
{
    const uint8_t* flash_base = &_config_lma[0];
    const uint8_t* layers_src = flash_base + sizeof(KeymapBootConfig);
    return layers_src + (size_t)layer * LAYER_SIZE;
}

static int keymap_loader_layer_is_empty(const uint8_t* src)
{
    int all_zero = 1;
    int all_ff = 1;

    for (size_t i = 0; i < LAYER_SIZE; ++i)
    {
        const uint8_t b = src[i];
        if (b != 0x00)
            all_zero = 0;
        if (b != 0xFF)
            all_ff = 0;
        if (!all_zero && !all_ff)
            return 0;
    }
    return 1;
}

void keymap_loader_init(void)
{
    const size_t layer_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS;

    memset(keymap_active, 0, layer_size);
    memcpy(&keymap_boot_config_ram.raw, &_config_lma[0], sizeof(keymap_boot_config_ram.raw));

    const uint8_t requested_layer = (uint8_t)keymap_boot_config_ram.bits.boot_layer;
    const uint8_t ver_cfg = (uint8_t)keymap_boot_config_ram.bits.ver;

    keymap_boot_config_ram.bits.boot_layer = -1;

    if (requested_layer < (uint8_t)KEYMAP_LAYERS)
    {
        keymap_loader_load_layer(requested_layer);
    }
    else
    {
        PRINT("Keymap loader: boot layer %u out of range, skip loading\r\n", (unsigned)requested_layer);
    }

    const int active_layer = keymap_boot_config_ram.bits.boot_layer;

    PRINT("Keymap loader: boot_layer=%u, ver_cfg=0x%X, active_layer=%d, max_layers=%d\r\n",
          (unsigned)requested_layer, (unsigned)ver_cfg, active_layer, (int)KEYMAP_LAYERS);
}

void keymap_loader_load_layer(const uint8_t layer)
{
    if (layer >= KEYMAP_LAYERS)
    {
        PRINT("Keymap loader: requested layer %u out of range, skip loading\r\n", (unsigned)layer);
        return;
    }

    const uint8_t* src = keymap_loader_layer_src(layer);

    if (keymap_loader_layer_is_empty(src))
    {
        PRINT("Keymap loader: layer %u empty, skip loading\r\n", (unsigned)layer);
        return;
    }

    memcpy(keymap_active, src, LAYER_SIZE);
    keymap_boot_config_ram.bits.boot_layer = layer;
}
