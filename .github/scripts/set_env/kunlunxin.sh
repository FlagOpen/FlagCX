#!/usr/bin/env bash

set -euo pipefail

MPI_HOME=${MPI_HOME:-/usr/local/mpich-3.3.2}
CCL_HOME=${CCL_HOME:-/usr/local/xccl_Linux_x86_64}
DEVICE_HOME=${DEVICE_HOME:-/usr/local/xpu}

export MPI_HOME CCL_HOME DEVICE_HOME
export JSON_INCLUDE_DIR="${JSON_INCLUDE_DIR:-/usr/include}"
export PATH="$MPI_HOME/bin:$DEVICE_HOME/bin:$PATH"
export LD_LIBRARY_PATH="$CCL_HOME/so:$MPI_HOME/lib:$DEVICE_HOME/so:$DEVICE_HOME/lib:${LD_LIBRARY_PATH:-}"
export FLAGCX_CI_MPIRUN="$(cd "$(dirname "${BASH_SOURCE[0]}")/../ci" && pwd)/run_mpich.sh"

FLAGCX_CI_PROJECT_MAKE_ARGS=(USE_KUNLUNXIN=1 MPI_HOME="$MPI_HOME" CCL_HOME="$CCL_HOME" DEVICE_HOME="$DEVICE_HOME" "HOST_CXXFLAGS=-include cstddef")
FLAGCX_CI_TEST_MAKE_ARGS=(USE_KUNLUNXIN=1 MPI_HOME="$MPI_HOME" CCL_HOME="$CCL_HOME" DEVICE_HOME="$DEVICE_HOME" "HOST_CXXFLAGS=-include cstddef")
FLAGCX_CI_ENABLE_IBUC=0
FLAGCX_CI_ENABLE_SHARED_P2P_ENGINE=0
FLAGCX_CI_INTRA_NP=4
FLAGCX_CI_NODE_NP=4
FLAGCX_CI_RUNNER_NP=4
export NP=4

FLAGCX_CI_NODE1_MPI_ARGS=(-x FLAGCX_HOSTID=node0)
FLAGCX_CI_NODE2_MPI_ARGS=(-x FLAGCX_HOSTID=node1)

flagcx_ci_prepare() {
  local suite=$1
  echo "Preparing KunlunXin environment for CI workload: $suite"
  command -v mpicxx
  command -v mpirun
  mpirun --version
  command -v xpu-smi
  xpu-smi -L
  test -d "$CCL_HOME/include"
  test -d "$CCL_HOME/so"
}
