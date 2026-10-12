#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef enum { HID_REPORT_TYPE_INVALID, HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_OUTPUT, HID_REPORT_TYPE_FEATURE } hid_report_type_t;
void tusb_init(void);
void tud_task(void);
bool tud_hid_ready(void);
bool tud_hid_report(uint8_t id, const void *buffer, uint16_t len);
#define CONTROL_STAGE_SETUP 0
typedef struct {uint8_t bmRequestType,bRequest;uint16_t wValue,wIndex,wLength;} tusb_control_request_t;
uint32_t tud_vendor_available(void);
uint32_t tud_vendor_read(void *,uint32_t);
uint32_t tud_vendor_write(const void *,uint32_t);
uint32_t tud_vendor_write_flush(void);
bool tud_control_xfer(uint8_t,const tusb_control_request_t *,void *,uint16_t);
bool tud_control_status(uint8_t,const tusb_control_request_t *);
