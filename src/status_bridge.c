// SPDX-License-Identifier: MIT
#include "status_messages.h"
// 2wiCC's serial status output is unnecessary for a standalone USB adapter.
void status_msg_init(void) { }
void status_msg_send(status_msg_type_t type) { (void)type; }
void status_msg_send_with_data(status_msg_type_t type, uint32_t data) {
    (void)type; (void)data;
}
void status_msg_process_queue(void) { }
