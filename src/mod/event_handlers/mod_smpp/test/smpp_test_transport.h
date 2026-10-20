/* SPDX-License-Identifier: MPL-1.1 */
#ifndef SMPP_TEST_TRANSPORT_H
#define SMPP_TEST_TRANSPORT_H
#include "../mod_smpp.h"
extern switch_status_t smpp_test_status;
extern unsigned smpp_test_calls;
extern mod_smpp_gateway_t *smpp_test_gateway;
extern switch_event_t *smpp_test_message;
void smpp_test_reset(switch_status_t status);
#endif
