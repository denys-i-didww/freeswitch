/* SPDX-License-Identifier: MPL-1.1 */
#include "smpp_test_transport.h"
switch_status_t smpp_test_status;
unsigned smpp_test_calls;
mod_smpp_gateway_t *smpp_test_gateway;
switch_event_t *smpp_test_message;

void smpp_test_reset(switch_status_t status)
{
	smpp_test_status = status;
	smpp_test_calls = 0;
	smpp_test_gateway = NULL;
	smpp_test_message = NULL;
}

switch_status_t mod_smpp_gateway_send_message(mod_smpp_gateway_t *gateway, switch_event_t *message)
{
	smpp_test_calls++;
	smpp_test_gateway = gateway;
	smpp_test_message = message;
	return smpp_test_status;
}

switch_status_t mod_smpp_gateway_create(mod_smpp_gateway_t **gw, char *name, char *host,
	int port, int debug, char *system_id, char *password, char *system_type, char *profile)
{
	(void)gw; (void)name; (void)host; (void)port; (void)debug;
	(void)system_id; (void)password; (void)system_type; (void)profile;
	/* A real gateway in the config is a fixture error. Never connect. */
	abort();
}

switch_status_t mod_smpp_gateway_destroy(mod_smpp_gateway_t **gateway)
{
	switch_core_hash_delete(mod_smpp_globals.gateways, (*gateway)->name);
	*gateway = NULL;
	return SWITCH_STATUS_SUCCESS;
}
