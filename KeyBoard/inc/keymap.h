//
// Created by xiaow on 2026/3/3.
//

#ifndef KEYBOARD_KEYMAP_H
#define KEYBOARD_KEYMAP_H
#include "key.h"
#include "app.h" /* provides USBD_ENDPx_DataUp() prototype */


void kb_send_snapshot(const u8 snapshot[HC165_COUNT]);

#endif //KEYBOARD_KEYMAP_H