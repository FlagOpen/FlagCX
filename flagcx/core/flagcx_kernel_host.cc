#include "comm.h"
#include "flagcx.h"
#include "flagcx_kernel.h"

FLAGCX_HOST_DECORATOR flagcxResult_t dequeue(void *fifoBuffer,
                                             flagcxDeviceTrigger_t trigger) {
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
    memcpy((void *)trigger,
           (void *)(buffer + 3 +
                    sizeof(flagcxDeviceTrigger) /
                        sizeof(unsigned long long int) * idx),
           sizeof(flagcxDeviceTrigger));
  } else {
    memset((void *)trigger, 0, sizeof(flagcxDeviceTrigger));
  }
  return flagcxSuccess;
}

// __host__ flagcxResult_t flagcxFifo::enqueue(flagcxReduceTrigger trigger) {
//   // to be implemented
//   return flagcxNotSupported;
// }
