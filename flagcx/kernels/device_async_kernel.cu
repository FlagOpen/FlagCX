// include CUDA-related headers
#include "flagcx.h"

__global__ void asyncStore(bool *__restrict__ value) {
  *value = true; // set value to true
  // __threadfence_system();  // Ensure that the write is visible to the CPU.
}

__global__ void asyncLoad(const volatile bool *__restrict__ value) {
  while (!(*value)) { // no-op; }
  }

  extern "C" void deviceAsyncStore(flagcxStream_t stream, void *args) {
    bool *value = static_cast<bool *>(args);
    asyncStore<<<1, 1, 0, *(cudaStream_t *)stream>>>(value);
    return;
  }

  extern "C" void deviceAsyncLoad(flagcxStream_t stream, void *args) {
    bool *value = static_cast<bool *>(args);
    asyncLoad<<<1, 1, 0, *(cudaStream_t *)stream>>>(value);
    return;
  }