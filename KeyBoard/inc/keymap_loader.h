#ifndef KEYMAP_LOADER_H
#define KEYMAP_LOADER_H

#include <stdint.h>
#include <stdbool.h>

#define KEYMAP_LAYERS ((2048 - 4) / (sizeof(KeyMapping) * KEY_TOTAL_KEYS))

/* 初始化：从 FLASH 读取首字节层索引并加载对应单层（紧凑镜像布局） */
void keymap_loader_init(void);

/* 返回当前已加载的层索引（-1 表示未加载） */
uint8_t keymap_loader_loaded_layer(void);

/* 运行时动态加载单个 layer（会覆盖已存在的层数据） */
void keymap_loader_load_layer(uint8_t layer);
#endif
