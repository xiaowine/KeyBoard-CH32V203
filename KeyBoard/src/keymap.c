#include "keymap.h"
#include "key.h"
#include "usb_endp.h"
#include "utils.h"
#include <string.h>
#include <stdbool.h>
#include "keymap_loader.h"

// 璁板綍涓婁竴娆″悎骞剁殑鎸夐敭浣嶆帺鐮侊紝鐢ㄤ簬妫€娴嬫壂鎻忛棿鐨勫彉鍖?
static uint32_t prev_keys = 0;
static uint16_t kb_heartbeat_counter = 0;
static uint32_t raw_zero_counter = 0;

/* 杩愯鏃跺崟灞傜紦鍐插尯锛歭oader 灏嗘妸閫変腑鐨?layer 鎷疯礉鍒版澶?*/
KeyMapping keymap_active[KEY_TOTAL_KEYS] __attribute__((section(".data"), used, aligned(4)));

/**
 * 鏍稿績鍙戦€侀€昏緫锛氭壂鎻?-> 鏀堕泦 -> 鍒嗙粍 -> 鍙戦€?
 */
void kb_send_snapshot(const uint8_t snapshot[HC165_COUNT])
{
    // 1. 鍚堝苟 24 浣嶆寜閿苟鍙栧弽锛?鍙?琛ㄧず鎸変笅锛?
    // 娉ㄦ剰锛氭牴鎹綘鐨?HC165 鎺ョ嚎椤哄簭锛屽彲鑳介渶瑕佽皟鏁翠綅绉婚『搴?
    const uint32_t raw = (uint32_t)snapshot[0] |
                         (uint32_t)snapshot[1] << 8 |
                         (uint32_t)snapshot[2] << 16;
    // 鏈変簺纭欢鎴栬鍙栨儏鍐典笅锛岀┖闂叉椂杩斿洖 0x000000锛堟墍鏈変綅涓?0锛夛紝
    // 鐩存帴鍙栧弽浼氬鑷村彉鎴愬叏 1锛堣鍒や负鎵€鏈夋寜閿鎸変笅锛夈€?
    // 鑻ュ師濮嬪€间负鍏?0锛屽垯璁や负娌℃湁鎸変笅浠讳綍閿紱鍚﹀垯鎸夊師閫昏緫鍙栧弽銆?
    const uint32_t keys = (raw == 0x000000) ? 0 : (~raw & 0x00FFFFFF);
    if (raw == 0x000000)
    {
        raw_zero_counter++;
        if ((raw_zero_counter % 500u) == 1u)
        {
            PRINT("Keymap trace: raw=0x000000 for %lu frames, keys forced to 0\r\n", (unsigned long)raw_zero_counter);
        }
    }

    // 2. 鍙樺寲妫€娴嬶細濡傛灉涓庝笂娆′綅鎺╃爜鐩稿悓锛岃烦杩囧鐞嗕互鑺傜渷 CPU/USB
    // 淇濆瓨鍘熷 keys 鍒?local 鍙橀噺锛屽悗缁綅鎵弿浼氫慨鏀逛复鏃跺彉閲?
    const uint32_t changed = keys ^ prev_keys;
    if (changed != 0)
    {
        PRINT("Keymap trace: snap=[%02X %02X %02X] raw=0x%06lX keys=0x%06lX changed=0x%06lX\r\n", snapshot[0], snapshot[1],
              snapshot[2], (unsigned long)raw, (unsigned long)keys, (unsigned long)changed);
    }
    if (changed == 0)
    {
        // 濡傛灉鍚敤浜嗗績璺充笖杈惧埌闂撮殧锛屽垯寮哄埗閲嶅彂涓€娆″畬鏁存姤鍛婏紱鍚﹀垯鐩存帴杩斿洖
        kb_heartbeat_counter++;
        if (kb_heartbeat_counter < KB_HEARTBEAT_INTERVAL)
        {
            return; // 鏃犲彉鍖栦笖鏈埌蹇冭烦鍛ㄦ湡锛岃烦杩囧悗缁敹闆嗕笌鍙戦€?
        }
        // 蹇冭烦瑙﹀彂锛氶噸缃鏁板苟缁х画鎵ц锛堝皢閲嶆柊鍙戦€佷笌涓婃鐩稿悓鐨勬姤鍛婏級
        kb_heartbeat_counter = 0;
    }
    else
    {
        // 鏈夊疄闄呭彉鍖栵紝娓呴浂蹇冭烦璁℃暟骞剁户缁鐞?
        kb_heartbeat_counter = 0;
    }

    // 3. 鏀堕泦鎵€鏈夋寜涓嬬殑 HID Usage ID锛堝尯鍒嗛敭鐩樹笌濯掍綋锛?
    /* 浼犵粺 boot 鎶ュ憡鍙渶鏈€澶?6 涓敭锛涚敤灏忔暟缁勫苟鍘婚噸浠ヨ妭鐪佺┖闂翠笌閬垮厤閲嶅 */
    static uint8_t kb_codes[BOOT_KEY_MAX] = {0};
    /* NKRO 浣嶅浘锛?20 bits -> 15 bytes锛夊湪鎵弿鏃剁洿鎺ユ瀯寤?*/
    uint8_t nkro_bitmap[15] = {0};
    static uint8_t prev_nkro[15] = {0};
    /* consumer usages 闄愬畾涓烘渶澶?MAX_CODE锛堟弿杩扮鏀寔 3 涓級 */
    static uint16_t consumer_usages[MAX_CODE] = {0};
    uint8_t kb_total = 0;
    uint8_t consumer_total = 0;
    uint8_t modifier_bits = 0;      // 鍚堝苟鎵€鏈夋寜涓嬮敭鐨勪慨楗颁綅锛堜粎閿洏鏈夋晥锛?
    uint8_t mouse_buttons_mask = 0; /* bits 0..4 */
    int16_t mouse_wheel_sum = 0;
    // 浣跨敤涓存椂鍙橀噺杩涜浣嶆壂鎻忥紝閬垮厤鐮村潖鍘熷 keys锛堢敤浜庢渶缁堟洿鏂?prev_keys锛?
    const KeyMapping *km = keymap_active;
    uint32_t scan = keys;
    while (scan)
    {
        const uint32_t idx = get_bit_index(scan);
        if (idx < KEY_TOTAL_KEYS)
        {
            const KeyMapping *m = &km[idx];
            if (changed != 0)
            {
                PRINT("  map idx=%lu mod=0x%02X type=%u codes=[0x%04X,0x%04X,0x%04X]\r\n", (unsigned long)idx,
                      (unsigned)m->modifiers, (unsigned)m->type, (unsigned)m->codes[0], (unsigned)m->codes[1],
                      (unsigned)m->codes[2]);
            }
            uint8_t count = keymap_get_count(m);
            if (count == 0 && m->modifiers == 0 && m->type == KEY_TYPE_KEYBOARD)
            {
                /* 鏃犳晥鏄犲皠锛岃烦杩?*/
                scan &= scan - 1;
                continue;
            }
            if (m->type == KEY_TYPE_CONSUMER)
            {
                for (uint8_t i = 0; i < count; i++)
                {
                    /* 鍙敹闆嗘渶澶?MAX_CODE锛堥€氬父涓?3锛変釜 consumer usages */
                    if (consumer_total < MAX_CODE)
                    {
                        consumer_usages[consumer_total++] = m->codes[i];
                    }
                }
            }
            else if (m->type == KEY_TYPE_MOUSE_BUTTON)
            {
                /* 榧犳爣鎸夐挳鏄犲皠锛氱疮绉寜閽帺鐮侊紙姣忎釜鏄犲皠鍙寘鍚涓寜閽帺鐮侊級 */
                for (uint8_t i = 0; i < count; i++)
                {
                    mouse_buttons_mask |= (m->codes[i] & 0x1F);
                }
            }
            else if (m->type == KEY_TYPE_MOUSE_WHEEL)
            {
                /* 榧犳爣婊氳疆鏄犲皠锛氱疮绉粴杞閲忥紙codes[0] 瀛樺偍婊氳疆澧為噺锛屾寜闇€杞负鏈夌鍙凤級 */
                mouse_wheel_sum += (int8_t)(m->codes[0] & 0xFF);
            }
            else
            {
                // 绱Н淇グ浣嶏紙鑻ヨ鐗╃悊閿甫淇グ锛?
                modifier_bits |= m->modifiers;
                for (uint8_t i = 0; i < count; i++)
                {
                    uint8_t code = (uint8_t)(m->codes[i] & 0xFF);
                    /* 鍦ㄦ敹闆嗛樁娈甸檺鍒朵负 BOOT_KEY_MAX 骞跺幓閲?*/
                    if (kb_total < BOOT_KEY_MAX)
                    {
                        bool found = false;
                        for (uint8_t j = 0; j < kb_total; j++)
                        {
                            if (kb_codes[j] == code)
                            {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            kb_codes[kb_total++] = code;
                        }
                    }
                    /* 鐩存帴鏋勫缓 NKRO 浣嶅浘锛堜粎澶勭悊 0..119锛?*/
                    if (code < 120)
                    {
                        const uint8_t byte_idx = code / 8;
                        const uint8_t bit = (uint8_t)(1u << (code & 7));
                        nkro_bitmap[byte_idx] |= bit;
                    }
                }
            }
        }
        scan &= scan - 1; // 娓呴櫎鏈€浣庝綅鐨?1
    }

    /* 灏嗕笂闈㈡敹闆嗗埌鐨?codes 鍒嗗埆鍙戦€佸嚭鍘伙紱鍒嗗埆浣跨敤閿洏涓庡獟浣撳彂閫佹帴鍙?*/
    /* 淇濈暀蹇冭烦琛屼负锛氫粎鍦ㄦ湁鍙樻洿鎴栧績璺宠Е鍙戞椂鍙戦€?*/
    // if (kb_total > 0 || modifier_bits != 0 || kb_heartbeat_counter == 0)
    // {
    //     USBD_SendKeyboardReports(modifier_bits, kb_codes, kb_total);
    // }

    /* 浠呭湪 NKRO 浣嶅浘涓庝笂娆′笉鍚屾垨蹇冭烦鏃跺彂閫?*/
    if (memcmp(nkro_bitmap, prev_nkro, sizeof(nkro_bitmap)) != 0 || kb_heartbeat_counter == 0)
    {
        USBD_SendNKROBitmap(modifier_bits, nkro_bitmap);
        memcpy(prev_nkro, nkro_bitmap, sizeof(nkro_bitmap));
    }

    if (consumer_total > 0 || kb_heartbeat_counter == 0)
    {
        /* 鍙戦€佸獟浣撴姤鍛婏紙鑻ユ病鏈夊獟浣撴寜閿篃浼氬彂閫佺┖鎶ュ憡浠ヤ究閲婃斁涔嬪墠鐨勭姸鎬侊級 */

        USBD_SendConsumerReport(consumer_usages, consumer_total);
    }
    if (mouse_buttons_mask != 0 || mouse_wheel_sum != 0 || kb_heartbeat_counter == 0)
    {
        /* 鍙戦€侀紶鏍囨姤鍛婏紙鑻ユ湁榧犳爣鎸夐敭鎴栨粴杞姩浣滐級 */
        USBD_SendMouseReport(mouse_buttons_mask, mouse_wheel_sum);
    }

    // 鏇存柊 prev_keys 涓烘湰娆″揩鐓х殑鐪熷疄閿綅锛堟敞鎰忎笂闈綅鎵弿浣跨敤浜嗗壇鏈?scan锛?
    if (changed != 0)
    {
        PRINT("  send mod=0x%02X kb_total=%u consumer_total=%u mouse_btn=0x%02X wheel=%d\r\n", (unsigned)modifier_bits,
              (unsigned)kb_total, (unsigned)consumer_total, (unsigned)mouse_buttons_mask, (int)mouse_wheel_sum);
    }

    prev_keys = keys;
}
