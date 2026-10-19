#!/usr/bin/env bash
if [[ -f /usr/local/Ascend/ascend-toolkit/set_env.sh ]]; then
  # shellcheck source=/dev/null
  source /usr/local/Ascend/ascend-toolkit/set_env.sh
fi

export DEVICE_HOME=${DEVICE_HOME:-/usr/local/Ascend/ascend-toolkit/latest}
export CCL_HOME=${CCL_HOME:-$DEVICE_HOME}
export MPI_HOME=${MPI_HOME:-/usr/local/mpi}
export PATH="$DEVICE_HOME/bin:${PATH:-}"
export LD_LIBRARY_PATH="$DEVICE_HOME/lib64:/usr/local/Ascend/driver/lib64:${LD_LIBRARY_PATH:-}"

FLAGCX_CI_PROJECT_MAKE_ARGS=(USE_ASCEND=1)
FLAGCX_CI_TEST_MAKE_ARGS=(USE_ASCEND=1)
FLAGCX_CI_ENABLE_IBUC=0
FLAGCX_CI_ENABLE_SHARED_P2P_ENGINE=0

flagcx_ci_configure_suite() {
  case "$1" in
    core|adaptor|service) ;;
    *) echo "Ascend CI does not enable suite: $1" >&2; return 1 ;;
  esac
}

flagcx_ci_build_suite_override() {
  local suite=$1
  local suite_dir=$2
  shift 2
  # Ascend currently enables single-process suites; MPI coverage is separate.
  FLAGCX_CI_BUILD_SUITE_OVERRIDE_HANDLED=1
  make -C "$suite_dir" --jobs="$(nproc)" \
    "$suite_dir/build/bin/${suite}_unit_tests" "$@"
}

flagcx_ci_run_suite_override() {
  local suite=$1
  local suite_dir=$2
  shift 2
  FLAGCX_CI_RUN_SUITE_OVERRIDE_HANDLED=1
  FLAGCX_CI_TEST_LABEL="Ascend $suite unit tests" \
    "$TEST_RUNNER" make -C "$suite_dir" run-unit "$@"
}

flagcx_ci_prepare() {
  local suite=$1
  echo "Preparing Ascend environment for CI workload: $suite"
  test -d "$DEVICE_HOME/include"
  test -e /dev/davinci_manager
  test -e /dev/devmm_svm
  command -v g++
}
