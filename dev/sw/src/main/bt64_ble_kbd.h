//----------------------------------------------------------------------------//
// BT-64 BLE keyboard service - GATT peripheral
//
// Copyright 2026 Martin Oswald (do2mad, 1mhz.de)
//
// Licensed under the Apache License, Version 2.0
//----------------------------------------------------------------------------//

#ifndef BT64_BLE_KBD_H
#define BT64_BLE_KBD_H

#include <stdint.h>
#include <stdbool.h>

#define BT64_BLE_KBD_PROTO_VERSION 1

// flags byte of the key characteristic
#define BT64_BLE_KBD_FLAG_SHIFT   0x01
#define BT64_BLE_KBD_FLAG_CMDR    0x02
#define BT64_BLE_KBD_FLAG_CTRL    0x04
#define BT64_BLE_KBD_FLAG_RESTORE 0x08
// Together with SHIFT: it is the RIGHT shift key. The BT-64 has one SHIFT line
// (left SHIFT); right SHIFT alone is pressed via the matrix position PA6/PB4.
#define BT64_BLE_KBD_FLAG_RSHIFT  0x10

#define BT64_BLE_KBD_KEY_NONE     0xFF

// Creates the queue/task. Call once from the platform init (any task).
void bt64_ble_kbd_init(void);

// Sets up the ATT server and starts advertising.
// Must be called from the BTstack task (e.g. platform on_init_complete).
void bt64_ble_kbd_start(void);

#endif
