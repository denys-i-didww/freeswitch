#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-1.1
"""Prove the same SMPP tests reject original handlers and accept the fix."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET

MODULE = Path("src/mod/event_handlers/mod_smpp")
SOURCE = MODULE / "mod_smpp.c"
CASES = {
    "interface_send_failure", "chat_app_send_failure",
    "success_and_default_gateway", "missing_gateway", "cli_status",
    "blocking_core_status",
}
EXPECTED_ERRORS = {
    "interface_send_failure": ["chq_eq_int: 0 != 9"] * 4,
    "chat_app_send_failure": ["chq_eq_int: 0 != 9"] * 4,
    "blocking_core_status": ["chq_eq_int: 0 != 9", "chq_eq_int: 0 != 1"],
}


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(root, command, report, name, required=True):
    start = time.time()
    with (report / (name + ".log")).open("wb") as output:
        process = subprocess.Popen(command, cwd=root, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=3600)
        except BaseException:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise
    record = {"command": command, "cwd": str(root), "started": start,
              "finished": time.time(), "exitCode": code}
    (report / (name + ".json")).write_text(json.dumps(record, indent=2))
    if required and code:
        raise RuntimeError(f"{name} failed ({code}); see {report}")
    return code


def results(root, report, label):
    # Compilation is a separate gate; make/check failure alone is not evidence.
    run(root, ["make", "-C", str(MODULE), "test/test_mod_smpp"],
        report, label + "-test-build")
    check_code = run(root, ["make", "-C", str(MODULE), "check",
                           "TESTS=test/test_mod_smpp"], report,
                     label + "-check", required=False)
    # FCT's built-in JUnit logger supplies actual per-case/assertion results.
    junit_code = run(root / MODULE, ["test/test_mod_smpp", "--logger", "junit"],
                     report, label + "-junit", required=False)
    text = (report / (label + "-junit.log")).read_text()
    first, last = text.find("<testsuites>"), text.rfind("</testsuites>")
    if first < 0 or last < 0:
        raise RuntimeError(f"{label}: missing complete JUnit report")
    xml = ET.fromstring(text[first:last + len("</testsuites>")])
    tests = xml.findall(".//testcase")
    if len(tests) != len(CASES) or {t.get("name") for t in tests} != CASES:
        raise RuntimeError(f"{label}: missing, duplicate or unexpected test cases")
    if xml.findall(".//skipped") or xml.findall(".//failure"):
        raise RuntimeError(f"{label}: skipped/unrecognized test result")
    errors = {t.get("name"): [e.get("message") for e in t.findall("error")]
              for t in tests if t.findall("error")}
    (report / (label + "-cases.json")).write_text(json.dumps(errors, indent=2))
    return check_code, junit_code, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel").decode().strip())
    report = args.report_dir.resolve()
    report.mkdir(parents=True, exist_ok=True)
    if git(root, "status", "--porcelain", "--untracked-files=no").strip():
        raise RuntimeError("Commit all implementation changes before this check")
    if git(root, "status", "--porcelain", "--untracked-files=normal", "--", str(MODULE)).strip():
        raise RuntimeError("Commit all new module/test files before this check")
    head = git(root, "rev-parse", "HEAD").decode().strip()
    baseline = git(root, "rev-parse", args.baseline + "^{commit}").decode().strip()
    git(root, "merge-base", "--is-ancestor", baseline, head)
    tracked = git(root, "ls-files", "-z", str(MODULE / "test")).decode().split("\0")
    hashes = {p: digest((root / p).read_bytes()) for p in tracked if p}
    original = git(root, "show", f"{baseline}:{SOURCE}")
    fixed = (root / SOURCE).read_bytes()
    # Pin exactly the approved two-handler change, with no test controls.
    expected = original.replace(
        b"mod_smpp_gateway_send_message(gateway, event);\n\n\treturn SWITCH_STATUS_SUCCESS;",
        b"return mod_smpp_gateway_send_message(gateway, event);", 1).replace(
        b"mod_smpp_gateway_send_message(gateway, message);\n\treturn SWITCH_STATUS_SUCCESS;",
        b"return mod_smpp_gateway_send_message(gateway, message);", 1)
    if expected == original or fixed != expected:
        raise RuntimeError("Source is not exactly the pinned two-handler fix")
    evidence = {"baseline": baseline, "head": head, "testHashes": hashes,
                "CFLAGS": os.environ.get("CFLAGS", ""),
                "originalSourceSha256": digest(original),
                "fixedSourceSha256": digest(fixed), "passed": False}
    (report / "summary.json").write_text(json.dumps(evidence, indent=2))
    with tempfile.TemporaryDirectory(prefix="smpp-before-after-") as temp:
        worktree = Path(temp) / "original"
        added = False
        try:
            git(root, "worktree", "add", "--detach", str(worktree), head)
            added = True
            (worktree / SOURCE).write_bytes(original)
            changed = git(worktree, "diff", "--name-only").decode().splitlines()
            if changed != [str(SOURCE)]:
                raise RuntimeError("Unexpected original-variant source changes")
            for path, sha in hashes.items():
                if digest((worktree / path).read_bytes()) != sha:
                    raise RuntimeError("Test infrastructure changed")
            build = ('pkg-config --atleast-version=1.10 libsmpp34 && ./bootstrap.sh -j'
                     " && printf '%s\\n' 'event_handlers/mod_smpp' > modules.conf"
                     ' && ./configure --prefix="$PWD/.test-install"'
                     ' && make -j2 core && make -j2 -C ' + str(MODULE))
            run(worktree, ["sh", "-c", build], report, "original-build")
            red_check, red_junit, red_errors = results(worktree, report, "original")
            if red_check != 2 or red_junit != len(EXPECTED_ERRORS) or red_errors != EXPECTED_ERRORS:
                raise RuntimeError("Original code did not produce exactly the expected assertion failures")
            green_check, green_junit, green_errors = results(root, report, "fixed")
            if green_check or green_junit or green_errors:
                raise RuntimeError("Fixed code did not pass the identical complete suite")
            if (root / SOURCE).read_bytes() != fixed:
                raise RuntimeError("Original checkout was unexpectedly modified")
            evidence.update(passed=True, original=red_errors, fixed=green_errors,
                            exitCodes={"originalCheck": red_check, "originalJUnit": red_junit,
                                       "fixedCheck": green_check, "fixedJUnit": green_junit})
            (report / "summary.json").write_text(json.dumps(evidence, indent=2))
            print("PASS: original status assertions fail; identical fixed tests pass")
        finally:
            if added:
                git(root, "worktree", "remove", "--force", str(worktree))


def interrupt(signum, frame):
    raise KeyboardInterrupt(f"Interrupted by signal {signum}")


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupt)
    main()
