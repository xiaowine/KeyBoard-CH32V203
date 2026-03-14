#include "keymap_loader.h"

#include <string.h>

#include "keymap.h"

// 外部符号：FLASH 中 `.keymap` 段起始地址（LMA）
extern uint8_t _keymap_lma[];

/* 运行时记录当前加载的层索引：-1 表示未加载 */
static uint8_t loaded_layer = -1;

/* 初始化：读取 FLASH 起始的 1 字节层索引并加载该单层（单层设计，兼容运行时的 loaded_mask） */
void keymap_loader_init(void)
{
    /* 先清零运行时活动层缓冲区（只需清一个层的大小） */
    const size_t layer_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS;
    memset(keymap_active, 0, layer_size);

    const uint8_t *flash_base = &_keymap_lma[0];
    const uint8_t layer_byte = flash_base[0];

    int active_layer = -1;
    /* 如果首字节是合法层索引则尝试加载该层；否则回退加载 layer0 */
    if (layer_byte < (uint8_t)KEYMAP_LAYERS)
    {
        keymap_loader_load_layer(layer_byte);
    }
    else
    {
        keymap_loader_load_layer(0);
    }

    /* 根据 loaded_layer 确定实际生效层（-1 表示未加载） */
    if (loaded_layer >= 0)
    {
        active_layer = (int)loaded_layer;
    }
    PRINT("Keymap loader: image_layer=0x%02X, active_layer=%d, max_layers=%d\r\n", (unsigned)layer_byte, active_layer,
          (int)KEYMAP_LAYERS);
}

uint8_t keymap_loader_loaded_layer(void)
{
    return loaded_layer;
}

/* 动态加载单层：从 flash 紧凑布局中拷贝指定 layer 到 keymap_active */
void keymap_loader_load_layer(uint8_t layer)
{
    /* 若请求越界则回退到 layer0（不立即返回） */
    if (layer >= KEYMAP_LAYERS)
    {
        PRINT("Keymap loader: requested layer %u out of range, fallback to layer 0\r\n", (unsigned)layer);
        layer = 0;
    }

    const size_t layer_size = sizeof(KeyMapping) * KEY_TOTAL_KEYS;
    /* FLASH 布局：首字节为单字节层索引（image header），后续的 KEY_MAP 按 4 字节对齐放置。
      因此需要将索引字节后的地址向上对齐到 4 字节边界再计算层偏移。 */
    const uint8_t *flash_base = (const uint8_t *)&_keymap_lma[0];
    uintptr_t p = (uintptr_t)flash_base + 1; /* 跳过索引字节 */
    /* 向上对齐到 4 字节边界 */
    p = (p + 3u) & ~((uintptr_t)3u);
    const uint8_t *layers_src = (const uint8_t *)p;
    const uint8_t *src = layers_src + ((size_t)layer * layer_size);
    /* 检测该层是否有数据：若整层全为 0x00 或全为 0xFF，则视为未写入/空层。
       若这是非 0 层的空数据，则尝试回退加载 layer0；若 layer0 也空则最终跳过加载。 */
    {
        int all_zero = 1;
        int all_ff = 1;
        for (size_t i = 0; i < layer_size; ++i)
        {
            uint8_t b = src[i];
            if (b != 0x00)
                all_zero = 0;
            if (b != 0xFF)
                all_ff = 0;
            if (!all_zero && !all_ff)
                break;
        }
        if (all_zero || all_ff)
        {
            PRINT("Keymap loader: layer %u empty (all %s)\r\n", (unsigned)layer, all_zero ? "0x00" : "0xFF");
            if (layer != 0)
            {
                PRINT("Keymap loader: fallback to layer 0\r\n");
                /* 尝试加载 layer0（避免无限递归：如果 layer==0 则不再回退） */
                keymap_loader_load_layer(0);
                return;
            }
            else
            {
                PRINT("Keymap loader: layer 0 empty, skip loading\r\n");
                return;
            }
        }
    }
    void *dst = (void *)keymap_active;
    memcpy(dst, src, layer_size);
    /* 标记当前唯一加载的层索引 */
    loaded_layer = (int8_t)layer;

#ifdef DEBUG
    /* 调试：打印该层的所有键位映射 */
    PRINT("Loaded keymap layer %u:\r\n", (unsigned)layer);
    for (uint16_t i = 0; i < KEY_TOTAL_KEYS; ++i)
    {
        const KeyMapping *e = &keymap_active[i];
        PRINT(" Entry %03u: count=%u mods=0x%02X type=%u codes:", (unsigned)i, (unsigned)e->count,
              (unsigned)e->modifiers, (unsigned)e->type);
        if (e->type == KEY_TYPE_KEYBOARD)
        {
            PRINT(" kcodes=%02X %02X %02X\r\n", (unsigned)e->codes[0], (unsigned)e->codes[1],
                  (unsigned)e->codes[2]);
        }
        else if (e->type == KEY_TYPE_CONSUMER)
        {
            PRINT(" ccodes=%04X %04X %04X\r\n", (unsigned)e->codes[0], (unsigned)e->codes[1],
                  (unsigned)e->codes[2]);
        }
        else if (e->type == KEY_TYPE_MOUSE_BUTTON)
        {
            PRINT(" mbuttons=%02X\r\n", (unsigned)e->codes[0]);
        }
        else if (e->type == KEY_TYPE_MOUSE_WHEEL)
        {
            PRINT(" mwheel=%d\r\n", (int)e->codes[0]);
        }
        else
        {
            PRINT("\r\n");
        }
    }
#endif
}
