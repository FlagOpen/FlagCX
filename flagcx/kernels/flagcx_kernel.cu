#include "comm.h"
#include "flagcx.h"
#include "flagcx_kernel.h"

FLAGCX_DEVICE_INLINE_DECORATOR void spin_backoff(int iter) {
  int delay = 1 << (iter < 15 ? iter : 15);
#if __CUDA_ARCH__ >= 700
  __nanosleep(delay);
#else
  uint64_t start = clock64();
  while (clock64() - start < (uint64_t)delay) { /* spin */
  }
#endif
}

FLAGCX_DEVICE_DECORATOR size_t getFlagcxDataTypeSizeDevice(flagcxDataType_t dtype) {
  switch (dtype) {
    // case flagcxInt8:
    case flagcxChar:
      return sizeof(char); // 1 byte
    case flagcxUint8:
      return sizeof(unsigned char); // 1 byte
    // case flagcxInt32:
    case flagcxInt:
      return sizeof(int); // 4 bytes
    case flagcxUint32:
      return sizeof(unsigned int); // 4 bytes
    case flagcxInt64:
      return sizeof(long long); // 8 bytes
    case flagcxUint64:
      return sizeof(unsigned long long); // 8 bytes
    // case flagcxFloat16:
    case flagcxHalf:
      return 2; // Half precision float is 2 bytes
    // case flagcxFloat32:
    case flagcxFloat:
      return sizeof(float); // 4 bytes
    // case flagcxFloat64:
    case flagcxDouble:
      return sizeof(double); // 8 bytes
    case flagcxBfloat16:
      return 2; // BFloat16 is typically 2 bytes
    default:
      return 0;
  }
}

FLAGCX_DEVICE_DECORATOR flagcxResult_t flagcxDeviceSend(const void *sendbuff, size_t count,
                                           flagcxDataType_t datatype, int peer,
                                           void *fifoBuffer) {
  enqueue(fifoBuffer, (uint64_t)((uintptr_t)sendbuff), count, peer, datatype, flagcxDevicePrimSend);
  return flagcxSuccess;
}

FLAGCX_DEVICE_DECORATOR flagcxResult_t flagcxDeviceRecv(void *recvbuff, size_t count,
                                           flagcxDataType_t datatype, int peer,
                                           void *fifoBuffer) {
  enqueue(fifoBuffer, (uint64_t)((uintptr_t)recvbuff), count, peer, datatype, flagcxDevicePrimRecv);
  return flagcxSuccess;
}

FLAGCX_DEVICE_DECORATOR flagcxResult_t flagcxDeviceTerm(void *fifoBuffer) {
  enqueue(fifoBuffer, 0, 0, 0, 0, flagcxDevicePrimTerm);
  return flagcxSuccess;
}

FLAGCX_DEVICE_DECORATOR flagcxResult_t flagcxDeviceWait(void *fifoBuffer) {
  enqueue(fifoBuffer, 0, 0, 0, 0, flagcxDevicePrimWait);
  unsigned long long int *buffer = (unsigned long long int *)fifoBuffer;
  int distance = buffer[2] - buffer[1];
  int iter = 0;
  while (distance > 0) {
    spin_backoff(iter);
    iter++;
    FLAGCX_DEVICE_THREAD_FENCE();
    distance = buffer[2] - buffer[1];
  }
  return flagcxSuccess;
}

FLAGCX_DEVICE_DECORATOR flagcxResult_t enqueue(void *fifoBuffer, uint64_t addr, uint64_t count, uint64_t peerRank, uint64_t datatype, uint64_t type) {
  int idx = -1;
  unsigned long long int *buffer = (unsigned long long int *)fifoBuffer;
  int capacity = buffer[0];
  int distance = buffer[2] - buffer[1];
  int iter = 0;
  while (distance >= capacity) {
    spin_backoff(iter);
    iter++;
    FLAGCX_DEVICE_THREAD_FENCE();
    distance = buffer[2] - buffer[1];
  }
  idx = buffer[2] % capacity;
  buffer[2] = buffer[2] + 1;
  flagcxDeviceTrigger* trigger = ((flagcxDeviceTrigger*)(buffer + 3)) + idx;
  trigger->addr = addr;
  trigger->count = count;
  trigger->peerRank = peerRank;
  trigger->datatype = datatype;
  trigger->type = type;
  FLAGCX_DEVICE_THREAD_FENCE();
  return flagcxSuccess;
}

FLAGCX_HOST_DECORATOR flagcxResult_t dequeue(void *fifoBuffer, flagcxDeviceTrigger_t trigger) {
  int idx = -1;
  unsigned long long int *buffer = (unsigned long long int *)fifoBuffer;
  int capacity = buffer[0];
  int distance = buffer[2] - buffer[1];
  if (distance > 0) {
    idx = buffer[1] % capacity;
    buffer[1] = buffer[1] + 1;
    __sync_synchronize();
  }
  if (idx > -1) {
    memcpy((void *)trigger, (void *)(buffer + 3 + 5 * idx), sizeof(flagcxDeviceTrigger));
  } else {
    memset((void *)trigger, 0, sizeof(flagcxDeviceTrigger));
  }
  return flagcxSuccess;
}

// __host__ flagcxResult_t flagcxFifo::enqueue(flagcxReduceTrigger trigger) {
//   // to be implemented
//   return flagcxNotSupported;
// }

// __device__ flagcxResult_t flagcxFifo::dequeue(flagcxReduceTrigger_t trigger) {
//   // to be implemented
//   return flagcxNotSupported;
// }

