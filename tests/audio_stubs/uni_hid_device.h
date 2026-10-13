#pragma once
#include <stdint.h>
#include <stdbool.h>
#define CONTROLLER_TYPE_PS5Controller 1
#define ERROR_CODE_SUCCESS 0
typedef struct uni_hid_device_s {int controller_type;struct {uint16_t interrupt_cid;} conn;} uni_hid_device_t;
bool l2cap_can_send_packet_now(uint16_t);
int l2cap_send(uint16_t,uint8_t *,uint16_t);

uint8_t l2cap_request_can_send_now_event(uint16_t);
