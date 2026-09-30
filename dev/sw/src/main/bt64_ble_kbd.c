//----------------------------------------------------------------------------//
// BT-64 BLE keyboard service - GATT peripheral
//
// Copyright 2026 Martin Oswald (do2mad, 1mhz.de)
//
// Lets a BLE central (e.g. the Blue-64 Keyboard iOS app) drive the C64 keyboard directly.
// Bluepad32's own BLE service stays disabled (its default), so this module
// owns the ATT server. Nothing in ext/bluepad32 is modified.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//----------------------------------------------------------------------------//

#include <string.h>

#include <btstack.h>
#include <uni.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "bt64_ble_kbd.h"
#include "bt64_parser.h"
#include "bt64_threadsafe.h"
#include "bt64_keyboard.h"

// the generated header defines a global "profile_data", which would clash
// with the one of Bluepad32's (disabled) BLE service
#define profile_data bt64_ble_kbd_profile_data
#include "bt64_ble_kbd.gatt.h"
#undef profile_data

#define HANDLE_KEY  ATT_CHARACTERISTIC_C64B0002_B1E6_4A64_9C64_6B7E3F1A2D00_01_VALUE_HANDLE
#define HANDLE_TEXT ATT_CHARACTERISTIC_C64B0003_B1E6_4A64_9C64_6B7E3F1A2D00_01_VALUE_HANDLE
#define HANDLE_INFO ATT_CHARACTERISTIC_C64B0004_B1E6_4A64_9C64_6B7E3F1A2D00_01_VALUE_HANDLE

#define TEXT_FLAG_FIRST 0x01
#define TEXT_FLAG_LAST  0x02
#define TEXT_BUF_LEN    1024

// ATT application error codes (0x80..0x9F)
#define ATT_APP_ERROR_BUSY     0x80
#define ATT_APP_ERROR_OVERFLOW 0x81

// Time every key state is held before the next one is applied. The KERNAL
// scans the keyboard every 1/60 s, so shorter taps could be missed.
#define KEY_HOLD_MS 30

#define KEY_QUEUE_LEN 32

#define ALL_FLAGS (BT64_BLE_KBD_FLAG_SHIFT | BT64_BLE_KBD_FLAG_CMDR | \
                   BT64_BLE_KBD_FLAG_CTRL  | BT64_BLE_KBD_FLAG_RESTORE)

// defined in bt64_keyboard.c (not exported in the header)
extern bool bt64_keyboard_set_mux(t_bt64_keyboard *h, unsigned int col, unsigned int row);
extern bool bt64_keyboard_clr_mux(t_bt64_keyboard *h);

typedef struct
{
	uint8_t flags;
	uint8_t key;
} t_ble_kbd_evt;

static QueueHandle_t   key_queue = NULL;
static hci_con_handle_t client   = HCI_CON_HANDLE_INVALID;

static char     text_buf[TEXT_BUF_LEN + 1];
static uint16_t text_len    = 0;
static bool     text_active = false;

static btstack_packet_callback_registration_t hci_cb_reg;

// General discoverable, BR/EDR not supported
#define ADV_FLAGS 0x06

// clang-format off
static const uint8_t adv_data[] = {
	2,  BLUETOOTH_DATA_TYPE_FLAGS, ADV_FLAGS,
	8,  BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'B','l','u','e','-','6','4',
	// C64B0001-B1E6-4A64-9C64-6B7E3F1A2D00 (little endian)
	17, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
	0x00, 0x2D, 0x1A, 0x3F, 0x7E, 0x6B, 0x64, 0x9C, 0x64, 0x4A, 0xE6, 0xB1, 0x01, 0x00, 0x4B, 0xC6,
};
// clang-format on
_Static_assert(sizeof(adv_data) <= 31, "adv_data too big");

//----------------------------------------------------------------------------//
// Key task: applies key states to the C64 keyboard lines

static void apply_state(uint8_t flags, uint8_t key)
{
	if(flags & BT64_BLE_KBD_FLAG_SHIFT)   bt64_keyboard_shft_psh(&keyboard);
	else                                  bt64_keyboard_shft_rel(&keyboard);

	if(flags & BT64_BLE_KBD_FLAG_CMDR)    bt64_keyboard_cmdr_psh(&keyboard);
	else                                  bt64_keyboard_cmdr_rel(&keyboard);

	if(flags & BT64_BLE_KBD_FLAG_CTRL)    bt64_keyboard_ctrl_psh(&keyboard);
	else                                  bt64_keyboard_ctrl_rel(&keyboard);

	if(flags & BT64_BLE_KBD_FLAG_RESTORE) bt64_keyboard_rest_psh(&keyboard);
	else                                  bt64_keyboard_rest_rel(&keyboard);

	if(key == BT64_BLE_KBD_KEY_NONE)
		bt64_keyboard_clr_mux(&keyboard);
	else
		bt64_keyboard_set_mux(&keyboard, (key >> 3) & 7, key & 7);
}

static void task_ble_kbd(void *arg)
{
	t_ble_kbd_evt evt;

	while(1)
	{
		if(xQueueReceive(key_queue, &evt, portMAX_DELAY) != pdTRUE)
			continue;

		bool active  = ((evt.flags & ALL_FLAGS) != 0) || (evt.key != BT64_BLE_KBD_KEY_NONE);
		bool applied = false;

		if(xSemaphoreTake(kbrd_sem_h, pdMS_TO_TICKS(50)) == pdTRUE)
		{
			if((kb_owner == KB_OWNER_BLE) || (kb_owner == KB_OWNER_NONE))
			{
				if(active)
				{
					kb_owner = KB_OWNER_BLE;
					apply_state(evt.flags, evt.key);
					applied = true;
				}
				else if(kb_owner == KB_OWNER_BLE)
				{
					apply_state(0, BT64_BLE_KBD_KEY_NONE);
					kb_owner = KB_OWNER_NONE;
					applied  = true;
				}
			}
			xSemaphoreGive(kbrd_sem_h);
		}

		if(applied)
			vTaskDelay(pdMS_TO_TICKS(KEY_HOLD_MS));
	}
}

static void post_key(uint8_t flags, uint8_t key)
{
	t_ble_kbd_evt evt = { .flags = flags & ALL_FLAGS, .key = key };

	if(key != BT64_BLE_KBD_KEY_NONE)
		evt.key = key & 0x3F;

	if(xQueueSend(key_queue, &evt, 0) != pdTRUE)
		loge("ble_kbd: key queue full\n");
}

//----------------------------------------------------------------------------//
// Text feed (uses the existing macro feed thread)

static int handle_text(const uint8_t* buffer, uint16_t size)
{
	if(size < 1)
		return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;

	uint8_t flags = buffer[0];

	if(flags & TEXT_FLAG_FIRST)
	{
		text_len    = 0;
		text_active = true;
	}

	if(!text_active)
		return ATT_ERROR_REQUEST_NOT_SUPPORTED;

	if(text_len + (size - 1) > TEXT_BUF_LEN)
	{
		text_active = false;
		return ATT_APP_ERROR_OVERFLOW;
	}

	if(flags & TEXT_FLAG_LAST)
	{
		// text_buf must not change while the feed thread reads it,
		// mcro_sem_h is given back by the feed thread when done
		if(xSemaphoreTake(mcro_sem_h, 0) != pdTRUE)
			return ATT_APP_ERROR_BUSY; // app retries the last chunk
	}

	memcpy(&text_buf[text_len], &buffer[1], size - 1);
	text_len += size - 1;

	if(flags & TEXT_FLAG_LAST)
	{
		text_buf[text_len] = 0;
		text_active = false;
		logi("ble_kbd: feeding %d chars\n", text_len);
		keyboard_macro_feed(text_buf);
	}

	return ATT_ERROR_SUCCESS;
}

//----------------------------------------------------------------------------//
// ATT callbacks (BTstack task)

static int att_write_cb(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t transaction_mode,
                        uint16_t offset, uint8_t* buffer, uint16_t buffer_size)
{
	ARG_UNUSED(con_handle);

	// no long / prepared writes
	if(transaction_mode != ATT_TRANSACTION_MODE_NONE || offset != 0)
		return ATT_ERROR_REQUEST_NOT_SUPPORTED;

	switch(att_handle)
	{
		case HANDLE_KEY:
			if(buffer_size != 2)
				return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
			post_key(buffer[0], buffer[1]);
			return ATT_ERROR_SUCCESS;

		case HANDLE_TEXT:
			return handle_text(buffer, buffer_size);

		default:
			return ATT_ERROR_SUCCESS;
	}
}

static uint16_t att_read_cb(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t offset,
                            uint8_t* buffer, uint16_t buffer_size)
{
	ARG_UNUSED(con_handle);

	if(att_handle == HANDLE_INFO)
	{
		uint8_t info[2];
		info[0] = BT64_BLE_KBD_PROTO_VERSION;
		info[1] = (uxSemaphoreGetCount(mcro_sem_h) == 0) ? 0x01 : 0x00;
		return att_read_callback_handle_blob(info, sizeof(info), offset, buffer, buffer_size);
	}
	return 0;
}

static void att_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(size);

	if(packet_type != HCI_EVENT_PACKET)
		return;

	switch(hci_event_packet_get_type(packet))
	{
		case ATT_EVENT_CONNECTED:
			client = att_event_connected_get_handle(packet);
			logi("ble_kbd: client connected, handle %#x\n", client);
			break;

		case ATT_EVENT_DISCONNECTED:
			logi("ble_kbd: client disconnected\n");
			client      = HCI_CON_HANDLE_INVALID;
			text_active = false;
			// never leave a key stuck
			post_key(0, BT64_BLE_KBD_KEY_NONE);
			break;

		default:
			break;
	}
}

// re-enable advertising after a central disconnected
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(size);

	if(packet_type != HCI_EVENT_PACKET)
		return;

	if(hci_event_packet_get_type(packet) == HCI_EVENT_DISCONNECTION_COMPLETE)
	{
		if(hci_event_disconnection_complete_get_connection_handle(packet) == client)
			client = HCI_CON_HANDLE_INVALID;
		if(client == HCI_CON_HANDLE_INVALID)
			gap_advertisements_enable(1);
	}
}

//----------------------------------------------------------------------------//
// Public

void bt64_ble_kbd_init(void)
{
	key_queue = xQueueCreate(KEY_QUEUE_LEN, sizeof(t_ble_kbd_evt));

	xTaskCreatePinnedToCore(task_ble_kbd,
	                        "ble_kbd",
	                        1024*4,
	                        NULL,
	                        TASK_PRIO_PARSE,
	                        NULL,
	                        CORE_AFFINITY);
}

void bt64_ble_kbd_start(void)
{
	logi("ble_kbd: starting BLE keyboard service\n");

	att_server_init(bt64_ble_kbd_profile_data, att_read_cb, att_write_cb);
	att_server_register_packet_handler(att_packet_handler);

	hci_cb_reg.callback = &hci_packet_handler;
	hci_add_event_handler(&hci_cb_reg);

	bd_addr_t null_addr = {0};
	// 100 ms advertising interval (units of 0.625 ms)
	gap_advertisements_set_params(0x00A0, 0x00A0, 0 /* ADV_IND */, 0, null_addr, 0x07, 0x00);
	gap_advertisements_set_data(sizeof(adv_data), (uint8_t*)adv_data);
	gap_advertisements_enable(1);
}
