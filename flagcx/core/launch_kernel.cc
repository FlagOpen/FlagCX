#include "launch_kernel.h"
#include "group.h"
#include <stdio.h>

flagcxLaunchFunc_t deviceAsyncKernel = NULL;

flagcxResult_t loadKernelSymbol(const char *path, const char *name,
                                flagcxLaunchFunc_t *fn) {
  void *handle = flagcxOpenLib(
      path, RTLD_LAZY, [](const char *p, int err, const char *msg) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
      });

  if (!handle)
    return flagcxSystemError;

  void *sym = dlsym(handle, name);
  if (!sym) {
    fprintf(stderr, "dlsym failed: %s\n", dlerror());
    return flagcxSystemError;
  }

  *fn = (flagcxLaunchFunc_t)sym;
  return flagcxSuccess;
}

#define FLAGCX_SIGNALS_PER_SEMAPHORE 3
#define FLAGCX_SIGNAL_START 0
#define FLAGCX_SIGNAL_END 1
#define FLAGCX_SIGNAL_COUNTER 2
FLAGCX_PARAM(SemaphoreBufferPoolCapacity, "SEMAPHORE_BUFFER_POOL_CAPACITY", 32);

flagcxDeviceSemaphoreBufferPool::flagcxDeviceSemaphoreBufferPool() {
  capacity = flagcxParamSemaphoreBufferPoolCapacity();
  slotId = 0;
  // Allocate host-pinned memory for all semaphores (3 ints each)
  deviceAdaptor->deviceMalloc((void **)&signalsPool,
                              capacity * FLAGCX_SIGNALS_PER_SEMAPHORE *
                                  sizeof(int),
                              flagcxMemHost, nullptr);
  // Get device pointer alias
  deviceAdaptor->hostGetDevicePointer(&dSignalsPool, (void *)signalsPool);
  // Init events to nullptr
  events =
      static_cast<flagcxEvent_t *>(malloc(capacity * sizeof(flagcxEvent_t)));
  for (int i = 0; i < capacity; i++) {
    events[i] = nullptr;
  }
}

flagcxDeviceSemaphoreBufferPool::~flagcxDeviceSemaphoreBufferPool() {
  for (int i = 0; i < capacity; i++) {
    if (events[i] != nullptr) {
      deviceAdaptor->eventDestroy(events[i]);
    }
  }
  free(events);
  deviceAdaptor->deviceFree((void *)signalsPool, flagcxMemHost, NULL);
}

int flagcxDeviceSemaphoreBufferPool::getSlotId() {
  if (events[slotId] != nullptr) {
    // wait for the previous event to complete
    while (deviceAdaptor->eventQuery(events[slotId]) != flagcxSuccess) {
      sched_yield();
    }
    // destroy previous event
    deviceAdaptor->eventDestroy(events[slotId]);
  }
  // set this slot signals to zero
  int offset = FLAGCX_SIGNALS_PER_SEMAPHORE * slotId;
  signalsPool[offset + FLAGCX_SIGNAL_START] = 0;   // started or not
  signalsPool[offset + FLAGCX_SIGNAL_END] = 0;     // ended or not
  signalsPool[offset + FLAGCX_SIGNAL_COUNTER] = 0; // total operations
  int ret = slotId;
  // Move to next slot
  slotId = (slotId + 1) % capacity;
  return ret;
}

void flagcxDeviceSemaphoreBufferPool::addEvent(int id, flagcxEvent_t event) {
  // Store the event for this semaphore slot
  events[id] = event;
}

// Return pointer to the start of a semaphore’s signals (host/device)
int *flagcxDeviceSemaphoreBufferPool::getHostPtr(int id) {
  return signalsPool + FLAGCX_SIGNALS_PER_SEMAPHORE * id;
}
void *flagcxDeviceSemaphoreBufferPool::getDevicePtr(int id) {
  return static_cast<void *>((static_cast<char *>(dSignalsPool) +
                              FLAGCX_SIGNALS_PER_SEMAPHORE * id * sizeof(int)));
}

void cpuAsyncKernel(void *args) {
  flagcxHostSemaphore *semaphore = (flagcxHostSemaphore *)args;
  semaphore->signalStart();
  semaphore->wait();
  semaphore->signalEnd();
}