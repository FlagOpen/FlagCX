#ifndef FLAGCX_LAUNCH_KERNEL_H_
#define FLAGCX_LAUNCH_KERNEL_H_
#pragma once
#include "adaptor.h"
#include "check.h"
#include "debug.h"
#include "flagcx.h"
#include "param.h"
#include "topo.h"
#include "utils.h"
#include <dlfcn.h>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

flagcxResult_t loadKernelSymbol(const char *path, const char *name,
                                flagcxLaunchFunc_t *fn);

#ifdef __cplusplus
}
#endif

struct flagcxSemaphore {
  flagcxSemaphore() = default;
  virtual ~flagcxSemaphore() = default;

  virtual flagcxEvent_t getEvent() = 0;
  virtual void signalStart() = 0;
  virtual void signalEnd() = 0;
  virtual void *getSignals() = 0;
  virtual void subCounter(int value) = 0;
  virtual void addCounter(int value) = 0;
  virtual int getCounter() = 0;
  virtual int pollStart() = 0;
  virtual int pollEnd() = 0;
  virtual void wait() = 0;
};

// Host semaphore derived class
struct flagcxHostSemaphore : public flagcxSemaphore {
  int start;   // started or not
  int end;     // ended or not
  int counter; // total operations to wait for inside the group
  std::vector<flagcxEvent_t> events;

  flagcxHostSemaphore() : start(0), end(0), counter(0) {}
  ~flagcxHostSemaphore() override {
    for (auto event : events) {
      deviceAdaptor->eventDestroy(event);
    }
  }
  flagcxEvent_t getEvent() override {
    events.push_back(nullptr);
    auto &event = events.back();
    deviceAdaptor->eventCreate(&event, flagcxEventDisableTiming);
    return event;
  }
  void signalStart() override { __atomic_store_n(&start, 1, __ATOMIC_RELEASE); }
  void signalEnd() override { __atomic_store_n(&end, 1, __ATOMIC_RELEASE); }
  void *getSignals() override { return nullptr; }
  void subCounter(int value) override {
    __atomic_fetch_sub(&counter, value, __ATOMIC_RELEASE);
  }
  void addCounter(int value) override {
    __atomic_fetch_add(&counter, value, __ATOMIC_RELEASE);
  }
  int getCounter() override { return counter; }
  int pollStart() override { return __atomic_load_n(&start, __ATOMIC_ACQUIRE); }
  int pollEnd() override { return __atomic_load_n(&end, __ATOMIC_ACQUIRE); }
  void wait() override {
    while (__atomic_load_n(&counter, __ATOMIC_ACQUIRE) > 0) {
      sched_yield();
    }
  }
};

// Used for flagcxDeviceSemaphore to manage a buffer pool
#define FLAGCX_MAX_SEMAPHORES 32
struct flagcxDeviceSemaphoreBufferPool {
  int slotId;         // slot index in the pool
  int *signalsPool;   // Host-mapped memory region
  void *dSignalsPool; // Device alias
  flagcxEvent_t
      events[FLAGCX_MAX_SEMAPHORES]; // store first event of each semaphore

  flagcxDeviceSemaphoreBufferPool() {
    slotId = 0;
    // Allocate host-pinned memory for all semaphores (3 ints each)
    deviceAdaptor->deviceMalloc((void **)&signalsPool,
                                3 * FLAGCX_MAX_SEMAPHORES * sizeof(int),
                                flagcxMemHost, nullptr);
    // Get device pointer alias
    deviceAdaptor->hostGetDevicePointer(&dSignalsPool, (void *)signalsPool);
    // Init events to nullptr
    for (int i = 0; i < FLAGCX_MAX_SEMAPHORES; i++) {
      events[i] = nullptr;
    }
  }
  ~flagcxDeviceSemaphoreBufferPool() {
    for (auto event : events) {
      if (event != nullptr) {
        deviceAdaptor->eventDestroy(event);
      }
    }
    deviceAdaptor->deviceFree((void *)signalsPool, flagcxMemHost, NULL);
  }
  int getSlotId() {
    if (events[slotId] != nullptr) {
      // wait for the previous event to complete
      while (deviceAdaptor->eventQuery(events[slotId]) != flagcxSuccess) {
        sched_yield();
      }
      // destroy previous event
      deviceAdaptor->eventDestroy(events[slotId]);
    }
    // set this slot signals to zero
    int offset = 3 * slotId;
    signalsPool[offset] = 0;     // started or not
    signalsPool[offset + 1] = 0; // ended or not
    signalsPool[offset + 2] =
        0; // total operations to wait for inside the group
    int ret = slotId;
    // Move to next slot
    slotId = (slotId + 1) % FLAGCX_MAX_SEMAPHORES;
    return ret;
  }
  void addEvent(int id, flagcxEvent_t event) {
    // Store the event for this semaphore slot
    events[id] = event;
  }
  // Return pointer to the start of a semaphore’s signals (host/device)
  int *getHostPtr(int id) { return signalsPool + 3 * id; }
  void *getDevicePtr(int id) {
    return static_cast<void *>(
        (static_cast<char *>(dSignalsPool) + 3 * id * sizeof(int)));
  }
};
static flagcxDeviceSemaphoreBufferPool deviceSemaphoreBufferPool;

// Device semaphore derived class
struct flagcxDeviceSemaphore : public flagcxSemaphore {
  int id;       // slot index in the pool
  int *signals; // [start, end, counter]
  void *dSignals;
  std::vector<flagcxEvent_t> events;

  flagcxDeviceSemaphore() {
    id = deviceSemaphoreBufferPool.getSlotId();
    signals = deviceSemaphoreBufferPool.getHostPtr(id);
    dSignals = deviceSemaphoreBufferPool.getDevicePtr(id);
    deviceAdaptor->hostGetDevicePointer((void **)&dSignals, (void *)signals);
  }
  ~flagcxDeviceSemaphore() override {
    for (size_t i = 0; i < events.size(); i++) {
      if (i == 0) {
        deviceSemaphoreBufferPool.addEvent(id, events[i]);
      } else {
        deviceAdaptor->eventDestroy(events[i]);
      }
    }
  }
  flagcxEvent_t getEvent() override {
    events.push_back(nullptr);
    auto &event = events.back();
    deviceAdaptor->eventCreate(&event, flagcxEventDisableTiming);
    return event;
  }
  // In future, we may implement device-side signal/wait APIs here,
  // for now, we implement them outside
  void signalStart() override {}
  void signalEnd() override {}
  void *getSignals() override { return dSignals; }
  void subCounter(int value) override {
    __atomic_fetch_sub(signals + 2, value, __ATOMIC_RELEASE);
  }
  void addCounter(int value) override {
    __atomic_fetch_add(signals + 2, value, __ATOMIC_RELEASE);
  }
  int getCounter() override { return signals[2]; }
  int pollStart() override {
    return __atomic_load_n(signals, __ATOMIC_ACQUIRE);
  }
  int pollEnd() override {
    return __atomic_load_n(signals + 1, __ATOMIC_ACQUIRE);
  }
  void wait() override {}
};

void cpuAsyncKernel(void *args);
extern flagcxLaunchFunc_t deviceAsyncKernel;

#endif