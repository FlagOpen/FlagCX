/*************************************************************************
 * Copyright (c) 2025 BAAI. All rights reserved.
 *
 * Custom AllReduce kernels using FlagCX Device API.
 * Communication infrastructure uses FlagCX Device API. DU uses the local
 * peer-pointer allreduce path; multicast is not supported by this adaptor.
 ************************************************************************/

#include "device_api/flagcx_device.h"
#include "dev_comm_state.h"
#include "flagcx_kernel.h"
#include "global_comm.h"
#include "du_adaptor.h" // for struct flagcxStream (stream->base)
#include <hip/hip_runtime.h>
#include <hip/hip_fp16.h>
#include <hip/hip_bfloat16.h>
#include <type_traits>

// HIP arithmetic types used by the DU kernels.
typedef __half half;
typedef hip_bfloat16 bfloat16;

// ============================================================
// Algorithm-specific helpers: array_t, packed_t, pack/unpack,
// lsa_st, reduceOp, lsa_reduce — local to this file, not part of Device API
// ============================================================

// Aligned array for vectorized operations
template <typename T, int N>
struct __align__(alignof(T) * N) array_t {
  T data[N];
  using type = T;
  static constexpr int size = N;
};

// Storage type based on byte size (4, 8, or 16 bytes)
template <int ByteSize> struct storage_type;
template <> struct storage_type<4>  { using type = uint32_t; };
template <> struct storage_type<8>  { using type = uint2; };
template <> struct storage_type<16> { using type = uint4; };

// Packed type: N is byte size (4, 8, or 16 bytes = 32, 64, or 128 bits)
template <typename T, int ByteSize = 4>
struct packed_t {
  static_assert(ByteSize == 4 || ByteSize == 8 || ByteSize == 16,
                "ByteSize must be 4, 8, or 16");
  static_assert(ByteSize >= sizeof(T), "ByteSize must be >= sizeof(T)");

  static constexpr int num_elems = ByteSize / sizeof(T);
  using elem_t = T;
  using array_type = array_t<T, num_elems>;
  using storage_t = typename storage_type<ByteSize>::type;
};

// Pack elements into storage type
template <typename T, int ByteSize = 4>
FLAGCX_DEVICE_INLINE_DECORATOR typename packed_t<T, ByteSize>::storage_t
pack(const T* data) {
  using P = packed_t<T, ByteSize>;
  if constexpr (ByteSize == 4) {
    if constexpr (sizeof(T) == 2) {
      uint16_t lo = *reinterpret_cast<const uint16_t*>(&data[0]);
      uint16_t hi = *reinterpret_cast<const uint16_t*>(&data[1]);
      return uint32_t(lo) | (uint32_t(hi) << 16);
    } else {
      return *reinterpret_cast<const uint32_t*>(&data[0]);
    }
  } else if constexpr (ByteSize == 8) {
    uint2 ret;
    if constexpr (P::num_elems == 1) {
      // Single element (e.g. double): reinterpret directly
      ret = *reinterpret_cast<const uint2*>(&data[0]);
    } else {
      ret.x = pack<T, 4>(&data[0]);
      ret.y = pack<T, 4>(&data[P::num_elems / 2]);
    }
    return ret;
  } else if constexpr (ByteSize == 16) {
    uint4 ret;
    constexpr int quarter = P::num_elems / 4;
    ret.x = pack<T, 4>(&data[0]);
    ret.y = pack<T, 4>(&data[quarter]);
    ret.z = pack<T, 4>(&data[quarter * 2]);
    ret.w = pack<T, 4>(&data[quarter * 3]);
    return ret;
  }
}

// Unpack storage type into elements
template <typename T, int ByteSize = 4>
FLAGCX_DEVICE_INLINE_DECORATOR void
unpack(typename packed_t<T, ByteSize>::storage_t v, T* data) {
  using P = packed_t<T, ByteSize>;
  if constexpr (ByteSize == 4) {
    if constexpr (sizeof(T) == 2) {
      uint16_t lo = v & 0xffff;
      uint16_t hi = v >> 16;
      data[0] = *reinterpret_cast<T*>(&lo);
      data[1] = *reinterpret_cast<T*>(&hi);
    } else {
      data[0] = *reinterpret_cast<T*>(&v);
    }
  } else if constexpr (ByteSize == 8) {
    if constexpr (P::num_elems == 1) {
      // Single element (e.g. double): reinterpret directly
      data[0] = *reinterpret_cast<T*>(&v);
    } else {
      unpack<T, 4>(v.x, &data[0]);
      unpack<T, 4>(v.y, &data[P::num_elems / 2]);
    }
  } else if constexpr (ByteSize == 16) {
    constexpr int quarter = P::num_elems / 4;
    unpack<T, 4>(v.x, &data[0]);
    unpack<T, 4>(v.y, &data[quarter]);
    unpack<T, 4>(v.z, &data[quarter * 2]);
    unpack<T, 4>(v.w, &data[quarter * 3]);
  }
}

// Convenience overloads for array_t
template <typename T, int ByteSize = 4>
FLAGCX_DEVICE_INLINE_DECORATOR typename packed_t<T, ByteSize>::storage_t
pack(const typename packed_t<T, ByteSize>::array_type& arr) {
  return pack<T, ByteSize>(arr.data);
}

template <typename T, int ByteSize = 4>
FLAGCX_DEVICE_INLINE_DECORATOR typename packed_t<T, ByteSize>::array_type
unpack(typename packed_t<T, ByteSize>::storage_t v) {
  typename packed_t<T, ByteSize>::array_type ret;
  unpack<T, ByteSize>(v, ret.data);
  return ret;
}

// Default ByteSize: 4 bytes for types <= 4 bytes, 8 bytes for 8-byte types
template <typename T>
constexpr int defaultByteSize() {
  return (sizeof(T) <= 4) ? 4 : 8;
}

// Elements per pack for given ByteSize
template <typename T, int ByteSize = defaultByteSize<T>()>
FLAGCX_DEVICE_INLINE_DECORATOR constexpr size_t elemsPerPack() {
  return packed_t<T, ByteSize>::num_elems;
}

// ============================================================
// Store to local memory using alias-safe vectorized store
template <typename T, int ByteSize = defaultByteSize<T>()>
FLAGCX_DEVICE_INLINE_DECORATOR void
lsa_st(T* addr, typename packed_t<T, ByteSize>::array_type val) {
  constexpr int N = packed_t<T, ByteSize>::num_elems;
  if constexpr (N == 1) {
    addr[0] = val.data[0];
  } else {
    using storage_t = typename packed_t<T, ByteSize>::storage_t;
    storage_t packed = pack<T, ByteSize>(val.data);
    __builtin_memcpy(addr, &packed, sizeof(storage_t));
  }
}

// Element-wise reduce operation
template <typename T>
FLAGCX_DEVICE_INLINE_DECORATOR T reduceOp(T a, T b, flagcxRedOp_t op) {
  switch (op) {
    case flagcxSum: return a + b;
    case flagcxMin: return a < b ? a : b;
    case flagcxMax: return a > b ? a : b;
    default: return a + b;
  }
}

// LSA reduce: load from each peer via intra pointers, reduce in registers
template <typename T, int ByteSize = defaultByteSize<T>()>
FLAGCX_DEVICE_INLINE_DECORATOR typename packed_t<T, ByteSize>::array_type
lsa_reduce(const flagcxDevMem &mem, size_t byteOffset, int nRanks, flagcxRedOp_t op) {
  using P = packed_t<T, ByteSize>;
  using arr_t = typename P::array_type;
  using storage_t = typename P::storage_t;
  constexpr int N = P::num_elems;

  T* p0 = (T*)flagcxGetIntraPointer(mem, byteOffset, 0);
  arr_t acc;
  storage_t s0;
  __builtin_memcpy(&s0, p0, sizeof(storage_t));
  unpack<T, ByteSize>(s0, acc.data);

  for (int peer = 1; peer < nRanks; peer++) {
    T* pp = (T*)flagcxGetIntraPointer(mem, byteOffset, peer);
    arr_t tmp;
    storage_t sp;
    __builtin_memcpy(&sp, pp, sizeof(storage_t));
    unpack<T, ByteSize>(sp, tmp.data);
    for (int i = 0; i < N; i++)
      acc.data[i] = reduceOp(acc.data[i], tmp.data[i], op);
  }
  return acc;
}

// ============================================================
// Kernels — communication via FlagCX Device API,
// algorithm via local HIP helpers above
// ============================================================

// LSA AllReduce: reduce from peer pointers, store to local buffer
// Works on any arch — no multicast dependency
template <typename T, int ByteSize = defaultByteSize<T>()>
__global__ void __launch_bounds__(FLAGCX_DEVICE_THREADS_PER_CTA)
flagcxLsaAllReduceKernel(flagcxDevMem sendmem, size_t sendoffset,
                         void* recvbuffer, size_t count,
                         flagcxRedOp_t op, flagcxDevComm devComm) {
  flagcxTeam intra = flagcxTeamIntra(devComm);
  flagcxDevBarrier<flagcxTeamTagIntra, flagcxCoopBlock> bar{
      flagcxCoopBlock(), devComm, intra, FLAGCX_BLOCK_IDX_X, true};
  bar.sync(flagcxDeviceMemoryOrderAcquire);

  const int nRanks = devComm.getIntraSize();
  const int globalTid = FLAGCX_THREAD_IDX_X + FLAGCX_BLOCK_DIM_X * FLAGCX_BLOCK_IDX_X;
  const int globalNthreads = FLAGCX_BLOCK_DIM_X * FLAGCX_GRID_DIM_X;
  constexpr size_t pSize = elemsPerPack<T, ByteSize>();
  const size_t packCount = count / pSize;

  T* lsaRecvPtr = (T*)recvbuffer;

  for (size_t offset = globalTid; offset < packCount; offset += globalNthreads) {
    auto v = lsa_reduce<T, ByteSize>(sendmem, sendoffset + offset * ByteSize, nRanks, op);
    lsa_st<T, ByteSize>(lsaRecvPtr + pSize * offset, v);
  }

  bar.sync(flagcxDeviceMemoryOrderRelease);
}

// ============================================================
// LSA kernel launcher (available on all paths)
// ============================================================

template <typename T>
flagcxResult_t launchLsaAllReduceKernel(flagcxDevMem sendmem, void* recvbuffer,
                                        size_t count, flagcxRedOp_t op,
                                        flagcxDevComm devComm, hipStream_t stream) {
  flagcxLsaAllReduceKernel<T><<<FLAGCX_DEVICE_CTA_COUNT,
                                FLAGCX_DEVICE_THREADS_PER_CTA, 0, stream>>>(
      sendmem, 0, recvbuffer, count, op, devComm);
  hipError_t err = hipGetLastError();
  return (err == hipSuccess) ? flagcxSuccess : flagcxUnhandledDeviceError;
}

static inline size_t getAlignmentRequirement(flagcxDataType_t datatype) {
  switch (datatype) {
    case flagcxFloat16:
    case flagcxBfloat16:
      return 2;
    default:
      return 1;
  }
}

static flagcxResult_t flagcxLsaAllReduceDispatch(
    flagcxDevMem sendmem, void* recvbuff, size_t count,
    flagcxDataType_t datatype, flagcxRedOp_t op,
    flagcxDevComm devComm, hipStream_t stream) {
  if (op != flagcxSum && op != flagcxMin && op != flagcxMax)
    return flagcxNotSupported;
  if ((op == flagcxMin || op == flagcxMax) &&
      (datatype != flagcxFloat16 && datatype != flagcxBfloat16))
    return flagcxNotSupported;
  if (count % getAlignmentRequirement(datatype) != 0)
    return flagcxNotSupported;

  switch (datatype) {
    case flagcxFloat16:
      return launchLsaAllReduceKernel<half>(sendmem, recvbuff, count, op, devComm, stream);
    case flagcxFloat32:
      return launchLsaAllReduceKernel<float>(sendmem, recvbuff, count, op, devComm, stream);
    case flagcxFloat64:
      return launchLsaAllReduceKernel<double>(sendmem, recvbuff, count, op, devComm, stream);
    case flagcxBfloat16:
      return launchLsaAllReduceKernel<bfloat16>(sendmem, recvbuff, count, op, devComm, stream);
    default:
      return flagcxNotSupported;
  }
}

// ============================================================
// Custom AllReduce entry point — registered as custom op
// ============================================================

extern "C" flagcxResult_t flagcxCustomAllReduceImpl(
    const void *sendbuff, void *recvbuff, size_t count,
    flagcxDataType_t datatype, flagcxRedOp_t op,
    flagcxComm_t comm, flagcxStream_t stream) {
  if (comm->devCommState == nullptr || !comm->devCommState->initialized)
    return flagcxNotSupported;

  auto *state = comm->devCommState;

  // Compute data size
  size_t elemSize = 0;
  switch (datatype) {
    case flagcxFloat16: case flagcxBfloat16: elemSize = 2; break;
    case flagcxFloat32: elemSize = 4; break;
    case flagcxFloat64: elemSize = 8; break;
    default: return flagcxNotSupported;
  }
  size_t size = count * elemSize;

  // Validate op/datatype/alignment before staging memcpy so we don't
  // waste a D2D copy when the dispatch will return flagcxNotSupported.
  if (op != flagcxSum && op != flagcxMin && op != flagcxMax)
    return flagcxNotSupported;
  if ((op == flagcxMin || op == flagcxMax) &&
      (datatype != flagcxFloat16 && datatype != flagcxBfloat16))
    return flagcxNotSupported;
  if (count % getAlignmentRequirement(datatype) != 0)
    return flagcxNotSupported;
  // Reject misaligned recvbuff — vectorized stores require ByteSize alignment.
  size_t storeAlign = (elemSize <= 4) ? 4 : 8;
  if (reinterpret_cast<uintptr_t>(recvbuff) % storeAlign != 0)
    return flagcxNotSupported;
  if (size > state->stagedBuffSize)
    return flagcxNotSupported;

  // Copy sendbuff to staged buffer
  hipStream_t hipStream = stream->base;
  hipError_t cerr = hipMemcpyAsync(state->sendStagedBuff, sendbuff, size,
                                      hipMemcpyDeviceToDevice, hipStream);
  if (cerr != hipSuccess)
    return flagcxUnhandledDeviceError;

  // Construct device-side objects from host handles
  flagcxDevComm dc(*state->devComm);
  flagcxDevMem sm(*state->sendStagedMem);
  return flagcxLsaAllReduceDispatch(sm, recvbuff, count, datatype, op,
                                    dc, hipStream);
}
