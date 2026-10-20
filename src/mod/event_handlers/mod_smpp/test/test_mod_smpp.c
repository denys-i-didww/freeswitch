/* SPDX-License-Identifier: MPL-1.1 */
#include <test/switch_test.h>
#include "smpp_test_transport.h"

/* Static callbacks and core registration must share one module state.
 * The actual production source is compiled here, with fake gateway functions. */
#include "../mod_smpp.c"

/* FCT integer comparisons evaluate operands twice; send exactly once. */
#define check_int(actual, expected) do { \
	int test_actual = (actual); \
	int test_expected = (expected); \
	fst_check_int_equals(test_actual, test_expected); \
} while (0)

static mod_smpp_gateway_t default_gateway = { .name = "default" };
static mod_smpp_gateway_t named_gateway = { .name = "named" };
static switch_event_node_t *event_binding;
static switch_mutex_t *event_lock;
static int delivery_seen, delivery_failure;
static char delivery_subject[80];

static void on_delivery(switch_event_t *event)
{
	const char *subject = switch_event_get_header(event, "subject");
	const char *failure = switch_event_get_header(event, "Delivery-Failure");
	switch_mutex_lock(event_lock);
	if (subject && failure && !strcmp(subject, delivery_subject)) {
		delivery_seen++;
		delivery_failure = !strcmp(failure, "true") ? 1 : !strcmp(failure, "false") ? 0 : -1;
	}
	switch_mutex_unlock(event_lock);
}

static switch_status_t send_interface(switch_event_t *event)
{
	switch_chat_interface_t *ci = switch_loadable_module_get_chat_interface("smpp");
	switch_status_t status;
	if (!ci) abort();
	status = ci->chat_send(event);
	UNPROTECT_INTERFACE(ci);
	return status;
}

/* These lower-layer failures all return GENERR. Test propagation, not encoding. */
static const char *failure_scenarios[] = { "construction", "packing", "socket", "short_write" };

FST_CORE_BEGIN("conf")
{
	if (switch_loadable_module_build_dynamic("mod_smpp", mod_smpp_load, NULL,
			mod_smpp_shutdown, SWITCH_FALSE) != SWITCH_STATUS_SUCCESS) {
		fprintf(stderr, "SMPP test module registration failed\n");
		exit(2);
	}
	switch_core_hash_insert(mod_smpp_globals.gateways, "default", &default_gateway);
	switch_core_hash_insert(mod_smpp_globals.gateways, "named", &named_gateway);
	switch_mutex_init(&event_lock, SWITCH_MUTEX_NESTED, mod_smpp_globals.pool);
	if (switch_event_bind_removable("smpp-status-test", SWITCH_EVENT_MESSAGE, NULL,
			on_delivery, NULL, &event_binding) != SWITCH_STATUS_SUCCESS) exit(2);

	{
	FST_SUITE_BEGIN(smpp_status)
	{
		FST_SETUP_BEGIN()
		{
			smpp_test_reset(SWITCH_STATUS_SUCCESS);
		}
		FST_SETUP_END()

		FST_TEST_BEGIN(interface_send_failure)
		{
			size_t i;
			for (i = 0; i < sizeof(failure_scenarios) / sizeof(*failure_scenarios); i++) {
				switch_event_t *message = NULL;
				fst_requires(switch_event_create(&message, SWITCH_EVENT_MESSAGE) == SWITCH_STATUS_SUCCESS);
				switch_event_add_header_string(message, SWITCH_STACK_BOTTOM, "smpp_gateway", "named");
				switch_event_set_body(message, failure_scenarios[i]);
				smpp_test_reset(SWITCH_STATUS_GENERR);
				check_int(send_interface(message), SWITCH_STATUS_GENERR);
				check_int(smpp_test_calls, 1);
				fst_check(smpp_test_gateway == &named_gateway);
				fst_check(smpp_test_message == message);
				switch_event_destroy(&message);
			}
		}
		FST_TEST_END()

		FST_TEST_BEGIN(chat_app_send_failure)
		{
			size_t i;
			for (i = 0; i < sizeof(failure_scenarios) / sizeof(*failure_scenarios); i++) {
				switch_event_t *message = NULL;
				fst_requires(switch_event_create(&message, SWITCH_EVENT_MESSAGE) == SWITCH_STATUS_SUCCESS);
				switch_event_set_body(message, failure_scenarios[i]);
				smpp_test_reset(SWITCH_STATUS_GENERR);
				check_int(switch_core_execute_chat_app(message, "smpp_send", "named"), SWITCH_STATUS_GENERR);
				check_int(smpp_test_calls, 1);
				fst_check(smpp_test_gateway == &named_gateway);
				fst_check(smpp_test_message == message);
				switch_event_destroy(&message);
			}
		}
		FST_TEST_END()

		FST_TEST_BEGIN(success_and_default_gateway)
		{
			switch_event_t *message = NULL;
			fst_requires(switch_event_create(&message, SWITCH_EVENT_MESSAGE) == SWITCH_STATUS_SUCCESS);
			check_int(send_interface(message), SWITCH_STATUS_SUCCESS);
			fst_check(smpp_test_gateway == &default_gateway);
			fst_check(smpp_test_message == message);
			check_int(smpp_test_calls, 1);
			switch_event_add_header_string(message, SWITCH_STACK_BOTTOM, "smpp_gateway", "named");
			smpp_test_reset(SWITCH_STATUS_SUCCESS);
			check_int(send_interface(message), SWITCH_STATUS_SUCCESS);
			fst_check(smpp_test_gateway == &named_gateway);
			check_int(smpp_test_calls, 1);
			smpp_test_reset(SWITCH_STATUS_SUCCESS);
			check_int(switch_core_execute_chat_app(message, "smpp_send", "named"), SWITCH_STATUS_SUCCESS);
			fst_check(smpp_test_gateway == &named_gateway);
			fst_check(smpp_test_message == message);
			check_int(smpp_test_calls, 1);
			smpp_test_reset(SWITCH_STATUS_SUCCESS);
			check_int(switch_core_execute_chat_app(message, "smpp_send", "default"), SWITCH_STATUS_SUCCESS);
			fst_check(smpp_test_gateway == &default_gateway);
			fst_check(smpp_test_message == message);
			check_int(smpp_test_calls, 1);
			switch_event_destroy(&message);
		}
		FST_TEST_END()

		FST_TEST_BEGIN(missing_gateway)
		{
			switch_event_t *message = NULL;
			fst_requires(switch_event_create(&message, SWITCH_EVENT_MESSAGE) == SWITCH_STATUS_SUCCESS);
			switch_event_add_header_string(message, SWITCH_STACK_BOTTOM, "smpp_gateway", "absent");
			check_int(send_interface(message), SWITCH_STATUS_GENERR);
			check_int(switch_core_execute_chat_app(message, "smpp_send", "absent"), SWITCH_STATUS_GENERR);
			check_int(smpp_test_calls, 0);
			switch_event_destroy(&message);
		}
		FST_TEST_END()

		FST_TEST_BEGIN(cli_status)
		{
			switch_stream_handle_t stream = { 0 };
			SWITCH_STANDARD_STREAM(stream);
			check_int(mod_smpp_send_api("named|123|456|test", NULL, &stream), SWITCH_STATUS_SUCCESS);
			check_int(smpp_test_calls, 1);
			fst_check(smpp_test_gateway == &named_gateway);
			/* Clean up the existing CLI's generated event in the fake transport. */
			switch_event_destroy(&smpp_test_message);
			smpp_test_reset(SWITCH_STATUS_GENERR);
			check_int(mod_smpp_send_api("named|123|456|test", NULL, &stream), SWITCH_STATUS_GENERR);
			check_int(smpp_test_calls, 1);
			switch_event_destroy(&smpp_test_message);
			smpp_test_reset(SWITCH_STATUS_SUCCESS);
			check_int(mod_smpp_send_api("absent|123|456|test", NULL, &stream), SWITCH_STATUS_GENERR);
			check_int(smpp_test_calls, 0);
			switch_safe_free(stream.data);
		}
		FST_TEST_END()

		FST_TEST_BEGIN(blocking_core_status)
		{
			int failure;
			for (failure = 0; failure <= 1; failure++) {
				int attempts, seen, actual_failure;
				switch_status_t actual;
				switch_mutex_lock(event_lock);
				switch_snprintf(delivery_subject, sizeof(delivery_subject), "smpp-status-%d", failure);
				delivery_seen = 0;
				delivery_failure = -1;
				switch_mutex_unlock(event_lock);
				smpp_test_reset(failure ? SWITCH_STATUS_GENERR : SWITCH_STATUS_SUCCESS);
				actual = switch_core_chat_send_args("smpp", "test", "456", "123",
					delivery_subject, "local only", "text/plain", NULL, SWITCH_TRUE);
				check_int(actual, failure ? SWITCH_STATUS_GENERR : SWITCH_STATUS_SUCCESS);
				for (attempts = 0; attempts < 200; attempts++) {
					switch_mutex_lock(event_lock);
					seen = delivery_seen;
					actual_failure = delivery_failure;
					switch_mutex_unlock(event_lock);
					if (seen) break;
					switch_yield(10000);
				}
				check_int(seen, 1);
				check_int(actual_failure, failure);
				check_int(smpp_test_calls, 1);
				fst_check(smpp_test_gateway == &default_gateway);
			}
		}
		FST_TEST_END()

		FST_TEARDOWN_BEGIN()
		{
		}
		FST_TEARDOWN_END()
	}
	FST_SUITE_END()
	}
	switch_event_unbind(&event_binding);
	switch_core_hash_delete(mod_smpp_globals.gateways, "default");
	switch_core_hash_delete(mod_smpp_globals.gateways, "named");
}
FST_CORE_END()
