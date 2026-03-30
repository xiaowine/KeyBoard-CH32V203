#include "config_loader.h"
#include <string.h>
#include "keymap.h"

extern uint8_t _config_lma[]; // NOLINT(*-reserved-identifier)

KeymapBootConfig keymap_boot_config_ram = {0};
/* Only keep the currently active color path in RAM (single path of MAX_COLOR entries). */
Color color_ram[MAX_COLOR] = {0};
static uint32_t keymap_flash_shadow_words[CONFIG_FLASH_BYTES / sizeof(uint32_t)] = {0};

static const uint8_t *config_loader_layer_src(const uint8_t layer)
{
  const uint8_t *flash_base = &_config_lma[0];
  const uint8_t *layers_src = flash_base + KEYMAP_CONFIG_HEADER_BYTES + COLOR_PATHS_BYTES;
  return layers_src + (size_t)layer * KEYMAP_LAYER_SIZE;
}

void config_loader_read_color_path(uint8_t path_index)
{
  if (path_index >= MAX_COLOR_PATHS)
  {
    return;
  }

  const uint8_t *src = &_config_lma[KEYMAP_CONFIG_HEADER_BYTES] + (size_t)path_index * COLOR_PATH_SIZE;
  memcpy(color_ram, src, COLOR_PATH_SIZE);
}

static uint8_t config_loader_write_flash_range(const uint32_t data_offset, const uint8_t *data, const uint32_t data_len,
                                               const char *log_tag)
{
  uint8_t *const shadow_bytes = (uint8_t *)keymap_flash_shadow_words;

  if (data == NULL || data_len == 0u)
  {
    PRINT("%s: invalid data\r\n", log_tag);
    return 0;
  }

  if (data_offset + data_len > CONFIG_FLASH_BYTES)
  {
    PRINT("%s: out of range, offset=%u len=%u\r\n", log_tag, (unsigned)data_offset, (unsigned)data_len);
    return 0;
  }

  const uint32_t aligned_start = data_offset & ~(CONFIG_PAGE_BYTES - 1u);
  const uint32_t aligned_end = (data_offset + data_len + (CONFIG_PAGE_BYTES - 1u)) & ~(CONFIG_PAGE_BYTES -
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

  status = FLASH_ROM_WRITE(target_addr, keymap_flash_shadow_words, aligned_len);
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

void config_loader_read_boot_config(KeymapBootConfig *config)
{
  if (config == NULL)
  {
    return;
  }

  memcpy(&config->raw, &_config_lma[0], sizeof(config->raw));
}

void config_loader_init(void)
{
  const size_t layer_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS;

  memset(keymap_active, 0, layer_size);
  config_loader_read_boot_config(&keymap_boot_config_ram);

  const uint8_t requested_layer = (uint8_t)keymap_boot_config_ram.bits.boot_layer;
  const uint8_t requested_color = (uint8_t)keymap_boot_config_ram.bits.color_layer;

  keymap_boot_config_ram.bits.boot_layer = -1;
  keymap_boot_config_ram.bits.color_layer = (uint32_t)-1;

  if (requested_layer < (uint8_t)KEYMAP_LAYERS)
  {
    config_loader_load_layer(requested_layer);
  }
  else
  {
    PRINT("Keymap loader: boot layer %u out of range, skip loading\r\n", (unsigned)requested_layer);
  }

  const int active_layer = keymap_boot_config_ram.bits.boot_layer;
  const int active_color = keymap_boot_config_ram.bits.color_layer;

  /* Load only the selected color path */
  if (requested_color < (uint8_t)MAX_COLOR_PATHS)
  {
    config_loader_read_color_path(requested_color);
  }

  PRINT("Keymap loader: boot_layer=%u, color_layer=%d, active_layer=%d, max_layers=%d\r\n",
        (unsigned)requested_layer, active_color, active_layer, (int)KEYMAP_LAYERS);
}

void config_loader_load_layer(const uint8_t layer)
{
  if (layer >= KEYMAP_LAYERS)
  {
    PRINT("Keymap loader: requested layer %u out of range, skip loading\r\n", (unsigned)layer);
    return;
  }

  const uint8_t *src = config_loader_layer_src(layer);

  memcpy(keymap_active, src, KEYMAP_LAYER_SIZE);
  keymap_boot_config_ram.bits.boot_layer = layer;
}

uint8_t config_loader_write_boot_config(const KeymapBootConfig *config)
{
  uint32_t page_words[CONFIG_PAGE_BYTES / sizeof(uint32_t)] = {0};
  const uint32_t config_addr = FLASH_BASE + (uint32_t)&_config_lma[0];
  KeymapBootConfig flash_config = {0};

  if (config == NULL)
  {
    return 0;
  }

  memcpy(page_words, &_config_lma[0], CONFIG_PAGE_BYTES);
  memcpy(page_words, &config->raw, sizeof(config->raw));

  FLASH_Status status = FLASH_ROM_ERASE(config_addr, CONFIG_PAGE_BYTES);
  if (status != FLASH_COMPLETE)
  {
    PRINT("Set boot layer: flash erase failed, status=%d\r\n", status);
    return 0;
  }

  status = FLASH_ROM_WRITE(config_addr, page_words, CONFIG_PAGE_BYTES);
  if (status != FLASH_COMPLETE)
  {
    PRINT("Set boot layer: flash write failed, status=%d\r\n", status);
    return 0;
  }

  config_loader_read_boot_config(&flash_config);
  if (flash_config.raw != config->raw)
  {
    PRINT("Set boot layer: flash verify failed\r\n");
    return 0;
  }

  return 1;
}

uint8_t config_loader_write_layer(const uint8_t layer, const uint8_t *layer_data, const uint16_t layer_data_len)
{
  const int active_layer = keymap_boot_config_ram.bits.boot_layer;
  const uint32_t layer_offset = KEYMAP_CONFIG_HEADER_BYTES + COLOR_PATHS_BYTES + ((uint32_t)layer * (uint32_t)KEYMAP_LAYER_SIZE);

  if (layer >= KEYMAP_LAYERS)
  {
    PRINT("Set layer keymap: layer %u out of range\r\n", (unsigned)layer);
    return 0;
  }

  if ((uint32_t)layer_data_len != (uint32_t)KEYMAP_LAYER_SIZE)
  {
    PRINT("Set layer keymap: invalid payload length %u, expected %u\r\n",
          (unsigned)layer_data_len, (unsigned)KEYMAP_LAYER_SIZE);
    return 0;
  }

  if (!config_loader_write_flash_range(layer_offset, layer_data, KEYMAP_LAYER_SIZE, "Set layer keymap"))
  {
    return 0;
  }

  if (active_layer == (int)layer)
  {
    memcpy(keymap_active, layer_data, KEYMAP_LAYER_SIZE);
  }

  return 1;
}

uint8_t config_loader_write_all_layers(const uint8_t *layers_data, const uint16_t layers_data_len)
{
  const uint32_t all_layers_size = (uint32_t)KEYMAP_LAYER_SIZE * (KEYMAP_LAYERS);
  const int active_layer = keymap_boot_config_ram.bits.boot_layer;

  if ((uint32_t)layers_data_len != all_layers_size)
  {
    PRINT("Set all layer keymap: invalid payload length %u, expected %u\r\n",
          (unsigned)layers_data_len, (unsigned)all_layers_size);
    return 0;
  }

  if (!config_loader_write_flash_range(KEYMAP_CONFIG_HEADER_BYTES + COLOR_PATHS_BYTES, layers_data, all_layers_size,
                                       "Set all layer keymap"))
  {
    return 0;
  }

  if ((active_layer >= 0) && (active_layer < KEYMAP_LAYERS))
  {
    memcpy(keymap_active, layers_data + ((uint32_t)active_layer * (uint32_t)KEYMAP_LAYER_SIZE), KEYMAP_LAYER_SIZE);
  }

  return 1;
}
