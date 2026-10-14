#!/usr/bin/env bash

set -euo pipefail

test_timeout="${FLAGCX_CI_TEST_TIMEOUT:-20m}"
test_kill_after="${FLAGCX_CI_TEST_KILL_AFTER:-30s}"
test_label="${FLAGCX_CI_TEST_LABEL:-test invocation}"

if [[ $# -eq 0 ]]; then
  echo "Usage: $0 <command> [arguments...]" >&2
  exit 2
fi
if ! command -v timeout >/dev/null 2>&1; then
  echo "GNU timeout is required to run CI tests" >&2
  exit 1
fi
attempt=1
max_attempts=2
while ((attempt <= max_attempts)); do
  echo "Starting $test_label (attempt $attempt/$max_attempts, timeout=$test_timeout, kill-after=$test_kill_after)"
  if timeout --signal=TERM --kill-after="$test_kill_after" \
    "$test_timeout" "$@"; then
    if ((attempt > 1)); then
      echo "::warning title=Flaky CI test::$test_label passed on retry $((attempt - 1))"
      echo "$test_label passed on retry $((attempt - 1)); classify as flaky"
    fi
    exit 0
  else
    status=$?
  fi
  if [[ "$status" == 124 || "$status" == 137 ]]; then
    echo "$test_label exceeded its $test_timeout timeout on attempt $attempt" >&2
  fi
  if ((attempt == max_attempts)); then
    exit "$status"
  fi
  echo "$test_label failed with exit=$status; retrying once" >&2
  attempt=$((attempt + 1))
done
