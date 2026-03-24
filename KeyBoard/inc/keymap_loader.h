#ifndef KEYMAP_LOADER_H
#define KEYMAP_LOADER_H

#include <stdint.h>

typedef union
{
  uint32_t raw;
  struct
  {
    uint32_t boot_layer : 4;
    uint32_t ver : 3;
    uint32_t reserved : 25;
  } bits;
} KeymapBootConfig;

typedef char keymap_boot_config_size_must_be_4[(sizeof(KeymapBootConfig) == 4) ? 1 : -1];

#define KEYMAP_CONFIG_HEADER_BYTES ((uint32_t)sizeof(KeymapBootConfig))
#define KEYMAP_LAYERS ((2048 - KEYMAP_CONFIG_HEADER_BYTES) / (sizeof(KeyMapping) * KEY_TOTAL_KEYS))

/* 初始化：从 FLASH 读取配置头中的 boot_layer 并加载对应单层 */
void keymap_loader_init(void);

/* 返回当前已加载的层索引（0xFF 表示未加载） */
uint8_t keymap_loader_loaded_layer(void);

/* 运行时动态加载单个 layer（会覆盖已存在的层数据） */
void keymap_loader_load_layer(uint8_t layer);
#endif
