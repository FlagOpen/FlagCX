#!/usr/bin/env bash

set -euo pipefail

test_timeout="${FLAGCX_CI_TEST_TIMEOUT:-20m}"
test_kill_after="${FLAGCX_CI_TEST_KILL_AFTER:-30s}"
test_label="${FLAGCX_CI_TEST_LABEL:-test invocation}"
log_root="${FLAGCX_CI_LOG_DIR:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/flagcx-ci-attempts}"
safe_label=$(printf '%s' "$test_label" | tr -c 'A-Za-z0-9_.-' '_')
log_dir="$log_root/$safe_label"

if [[ $# -eq 0 ]]; then
  echo "Usage: $0 <command> [arguments...]" >&2
  exit 2
fi
if ! command -v timeout >/dev/null 2>&1; then
  echo "GNU timeout is required to run CI tests" >&2
  exit 1
fi
mkdir -p "$log_dir"
attempt=1
max_attempts=2
while ((attempt <= max_attempts)); do
  attempt_log="$log_dir/attempt-$attempt.log"
  echo "Starting $test_label (attempt $attempt/$max_attempts, timeout=$test_timeout, kill-after=$test_kill_after)"
  set +e
  timeout --signal=TERM --kill-after="$test_kill_after" \
    "$test_timeout" "$@" 2>&1 | tee "$attempt_log"
  status=${PIPESTATUS[0]}
  set -e
  if [[ "$status" == 0 ]]; then
    if ((attempt > 1)); then
      echo "::warning title=Flaky CI test::$test_label passed on retry $((attempt - 1))"
      echo "$test_label passed on retry $((attempt - 1)); classify as flaky"
      if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
        {
          echo "### Flaky CI test"
          echo "- **Test:** $test_label"
          echo "- **Result:** passed on retry $((attempt - 1))"
          printf '%s\n' "- **First attempt log:** $log_dir/attempt-1.log"
        } >> "$GITHUB_STEP_SUMMARY"
      fi
    fi
    exit 0
  fi
  if [[ "$status" == 124 || "$status" == 137 ]]; then
    echo "$test_label exceeded its $test_timeout timeout on attempt $attempt" >&2
  fi
  echo "$test_label attempt $attempt failed with exit=$status; log saved to $attempt_log" >&2
  if ((attempt == max_attempts)); then
    exit "$status"
  fi
  echo "$test_label failed with exit=$status; retrying once" >&2
  attempt=$((attempt + 1))
done
