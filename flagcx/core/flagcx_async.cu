#include <cuda_runtime.h>
#include "flagcx_async.h"
#include "flagcx.h"
#include <cstdio>  

__global__ void asyncLaunchKernel(const volatile bool *__restrict__ flag) {
  while (!(*flag)) {
  }
}


flagcxResult_t launchAsyncKernel(flagcxStream_t stream, void *args_out_ptr) {

    bool *d_flag = reinterpret_cast<bool *>(args_out_ptr);

    asyncLaunchKernel<<<1, 1, 0, stream->base>>>(d_flag);

    return flagcxSuccess;
}
