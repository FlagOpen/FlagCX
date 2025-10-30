#ifndef FLAGCX_ADAPTOR_DEVICE_UTILS_H_
#define FLAGCX_ADAPTOR_DEVICE_UTILS_H_

#ifdef USE_NVIDIA_ADAPTOR
#define FLAGCX_HOST_DECORATOR __host__
#define FLAGCX_DEVICE_DECORATOR __device__
#define FLAGCX_GLOBAL_DECORATOR __global__
#define FLAGCX_DEVICE_INLINE_DECORATOR __forceinline__ __device__
#define FLAGCX_DEVICE_STREAM_PTR cudaStream_t *
#define FLAGCX_DEVICE_THREAD_FENCE __threadfence_system
#define FLAGCX_DEVICE_LAUNCH_KERNEL cudaLaunchKernel
#include <cuda.h>
#include <cuda_runtime.h>
#else
#define FLAGCX_HOST_DECORATOR __host__
#define FLAGCX_DEVICE_DECORATOR __device__
#define FLAGCX_GLOBAL_DECORATOR __global__
#endif

#endif // FLAGCX_ADAPTOR_DEVICE_UTILS_H_
