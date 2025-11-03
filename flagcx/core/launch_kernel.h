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

// Device semaphore derived class
struct flagcxDeviceSemaphore : public flagcxSemaphore {
  int *signals; // [start, end, counter]
  void *dSignals;
  std::vector<flagcxEvent_t> events;

  flagcxDeviceSemaphore() {
    deviceAdaptor->deviceMalloc((void **)&signals, 3 * sizeof(int),
                                flagcxMemHost, nullptr);
    signals[0] = 0; // started or not
    signals[1] = 0; // ended or not
    signals[2] = 0; // total operations to wait for inside the group
    deviceAdaptor->hostGetDevicePointer((void **)&dSignals, (void *)signals);
  }
  ~flagcxDeviceSemaphore() override {
    for (auto event : events) {
      deviceAdaptor->eventDestroy(event);
    }
    deviceAdaptor->deviceFree((void *)signals, flagcxMemHost, NULL);
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