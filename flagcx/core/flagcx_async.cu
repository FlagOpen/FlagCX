#include <cuda_runtime.h>
#include "flagcx_async.h"
#include "flagcx.h"
#include <cstdio>  

__global__ void asyncLaunchKernel(const volatile bool *__restrict__ flag) {
  while (!(*flag)) {
  }
}

flagcxResult_t launchAsyncKernel(flagcxStream_t stream, void *args_out_ptr) {
    cudaError_t err;

    // 已传入 pinned host 内存指针
    bool *h_flag = reinterpret_cast<bool *>(args_out_ptr);

    // 初始化 flag（可选）
    *h_flag = false;

    // 获取对应的 device pointer
    bool *d_flag = nullptr;
    err = cudaHostGetDevicePointer(reinterpret_cast<void **>(&d_flag), h_flag, 0);
    if (err != cudaSuccess) {
        return flagcxUnhandledDeviceError;
    }

    // 启动等待 flag 的 kernel
    asyncLaunchKernel<<<1, 1, 0, stream->base>>>(d_flag);

    return flagcxSuccess;
}
