#!/usr/bin/env bash

set -euo pipefail

project_root=${GITHUB_WORKSPACE:-$(git rev-parse --show-toplevel)}
export PATH="$MPI_HOME/bin:$PATH"
export LD_LIBRARY_PATH="$project_root/build/lib:$MPI_HOME/lib:${LD_LIBRARY_PATH:-}"
perf_bin="$project_root/test/perf/host_api/build/bin"
device_api_bin="$project_root/test/perf/device_api/build/bin"

make -C "$project_root" --jobs="$(nproc)" \
  USE_NVIDIA=1 COMPILE_KERNEL=1 FORCE_DEFAULT_PATH=1
make -C "$project_root/test/perf" --jobs="$(nproc)" \
  USE_NVIDIA=1 COMPILE_KERNEL=1 FORCE_DEFAULT_PATH=1

source "$project_root/test/script/_gpu_check.sh"
wait_for_gpu

# Preserve the original extended perf-test coverage.
export FLAGCX_CI_PERF_PLATFORM='CUDA extended'
"$project_root/.github/scripts/ci/run_host_perf_suite.sh" homogeneous "$perf_bin"

mpirun -np 4 --allow-run-as-root "$perf_bin/perf_allreduce" \
  -b 128M -e 1G -f 2 -p 1 -R 1

for operation in sendrecv alltoall; do
  mpirun -np 4 --allow-run-as-root \
    -x FLAGCX_MEM_ENABLE=1 -x FLAGCX_VMM_ENABLE=0 \
    -x FLAGCX_USE_HETERO_COMM=1 \
    "$perf_bin/perf_$operation" -b 128M -e 1G -f 2 -p 1 -R 1

  mpirun -np 4 --allow-run-as-root \
    -x FLAGCX_MEM_ENABLE=1 -x FLAGCX_VMM_ENABLE=0 \
    -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_P2P_DISABLE=1 \
    "$perf_bin/perf_$operation" -b 128M -e 1G -f 2 -p 1 -R 1
done

for operation in put get; do
  mpirun -np 2 --allow-run-as-root \
    -x FLAGCX_MEM_ENABLE=1 -x FLAGCX_VMM_ENABLE=0 \
    -x FLAGCX_USE_HETERO_COMM=1 \
    "$perf_bin/perf_$operation" -b 8 -e 1M -f 2 -R 2
done

mpirun -np 4 --allow-run-as-root \
  -x FLAGCX_USE_HETERO_COMM=1 -x FLAGCX_MEM_ENABLE=1 \
  -x FLAGCX_VMM_ENABLE=0 -x FLAGCX_P2P_DISABLE=1 \
  -x FLAGCX_DEBUG=INFO -x FLAGCX_DEBUG_SUBSYS=ALL -x LD_LIBRARY_PATH \
  "$device_api_bin/perf_allreduce_intranode" -b 1M -e 64M -f 2 -R 1

common_env=(
  -x FLAGCX_USE_HETERO_COMM=1
  -x FLAGCX_MEM_ENABLE=1
  -x FLAGCX_VMM_ENABLE=0
  -x FLAGCX_P2P_DISABLE=1
  -x LD_LIBRARY_PATH
)
mpirun --allow-run-as-root \
  -np 2 "${common_env[@]}" "${FLAGCX_CI_NODE1_MPI_ARGS[@]}" \
  "$device_api_bin/perf_internode_onesided" -b 1M -e 64M -f 2 -R 1 \
  : -np 2 "${common_env[@]}" "${FLAGCX_CI_NODE2_MPI_ARGS[@]}" \
  "$device_api_bin/perf_internode_onesided" -b 1M -e 64M -f 2 -R 1

FLAGCX_P2P_PERF_OP=write \
  mpirun -np 2 --allow-run-as-root "$perf_bin/perf_p2p_engine" \
    -b 4K -e 64M -f 2 -n 10

shared_project_build="$project_root/build-p2p-shared"
shared_test_build="$project_root/test/perf/host_api/build-p2p-shared"
make -C "$project_root" --jobs="$(nproc)" \
  USE_NVIDIA=1 COMPILE_KERNEL=1 FORCE_DEFAULT_PATH=1 \
  USE_SHARED_P2P_ENGINE=1 BUILDDIR="$shared_project_build"
make -C "$project_root/test/perf/host_api" \
  USE_NVIDIA=1 COMPILE_KERNEL=1 FORCE_DEFAULT_PATH=1 \
  USE_SHARED_P2P_ENGINE=1 BUILDDIR="$shared_test_build" \
  FLAGCX_LIB="$shared_project_build/lib" \
  "$shared_test_build/bin/perf_p2p_engine"

LD_LIBRARY_PATH="$shared_project_build/lib:$LD_LIBRARY_PATH" \
  FLAGCX_P2P_PERF_OP=read \
  mpirun -np 2 --allow-run-as-root \
    "$shared_test_build/bin/perf_p2p_engine" \
    -b 4K -e 64M -f 2 -n 10
