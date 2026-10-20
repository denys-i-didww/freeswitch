# Outbound SMPP send status

The `smpp` chat interface and `smpp_send` chat application return the status of
the local gateway send. Message construction, packing, socket and short-write
failures return `SWITCH_STATUS_GENERR`. A missing gateway also fails; the chat
interface uses `default` when `smpp_gateway` is absent.

`switch_core_execute_chat_app` exposes this status. A blocking core chat send
also reports it through the existing `Delivery-Failure` event header. A
successful write is **not** provider acceptance (`submit_sm_resp`) or final SMS
delivery. Nonblocking chat still reports enqueue success. This change does not
alter `mod_sms` chatplan normalization or the void dialplan application.

## Isolated tests

Use a dedicated Linux checkout and the usual FreeSWITCH build dependencies,
including libsmpp34 >= 1.10. From its root:

With GCC 12, the unchanged gateway authentication code produces a
`stringop-truncation` warning under the project's `-Werror`. Keep that warning
visible without making it fatal by setting
`export CFLAGS="-g -O2 -Wno-error=stringop-truncation"` before the commands below.
Use the same environment for both before/after builds.

```sh
pkg-config --atleast-version=1.10 libsmpp34 &&
./bootstrap.sh -j &&
printf '%s\n' 'event_handlers/mod_smpp' > modules.conf &&
./configure --prefix="$PWD/.test-install" &&
make -j2 core &&
make -j2 -C src/mod/event_handlers/mod_smpp
make -C src/mod/event_handlers/mod_smpp check TESTS=test/test_mod_smpp
```

The FST executable compiles the actual module handlers with a linked fake
gateway boundary, then registers that same module state with the real core.
The production module contains no test controls. Empty gateway configuration
and synthetic gateways prevent real binds or sends. Four named lower-layer
failure scenarios return GENERR; they test propagation, not encoder or socket
internals. The CLI and missing-gateway controls and bounded blocking-core event
check cover the surrounding behavior.

## Before/after regression check

Commit the fix, tests and this documentation first. With Python 3 and Git
available, run from the prepared checkout root:

```sh
python3 src/mod/event_handlers/mod_smpp/test/check_status_regression.py \
  --baseline 12ac48ab9e692d645b91b83bab89a85abdc3af53 \
  --report-dir "$PWD/.test-results/smpp-before-after"
```

Allow space for one additional temporary checkout and full build. The driver
preserves the new tests in a detached temporary worktree at the implementation
commit and replaces only `mod_smpp.c` with the pinned original source. It
requires that variant to compile and then fail the named interface/chat-app
status assertions and related blocking-core assertions. All other controls
must pass. It runs the identical fixed tests and requires complete success.
FCT's JUnit logger supplies machine-readable per-case evidence in a separate
run, alongside `make check` logs.

The driver exits zero only for this expected before/after result. Compilation,
load errors, missing/skipped tests, unexpected assertions, timeouts and crashes
are not successful reproduction. Logs, exit codes and source hashes remain in
the report directory; only the driver-created temporary worktree is removed.
The original source checkout is never rewritten.

These tests do not establish DIDWW interoperability or handset delivery.
Actual setup verification requires an authorized trunk, eligible sender and
destination and reviewed transport settings. Provider rejection, DLRs, TLS,
reconnect/retry and Unicode/multipart handling are outside this fix. A DIDWW
setup guide should follow verification of the actual setup and its limitations.
