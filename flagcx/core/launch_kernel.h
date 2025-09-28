#ifndef FLAGCX_LAUNCH_KERNEL_H_
#define FLAGCX_LAUNCH_KERNEL_H_
#pragma once
#include "flagcx.h"
typedef void (*flagcxLaunchFunc_t)(flagcxStream_t, void *);
#include "adaptor.h"
#include "debug.h"
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

extern flagcxLaunchFunc_t deviceAsyncStore;
extern flagcxLaunchFunc_t deviceAsyncLoad;

void cpuAsyncStore(void *args);
void cpuAsyncLoad(void *args);
void cpuAsyncLoadWithMaxSpinCount(void *args);

struct flagcxHostSemaphore {
  int flag = 0;    // if ready to be triggered
  int counter = 0; // total operations to wait for inside the group
  void signalFlag() { __atomic_store_n(&flag, 1, __ATOMIC_RELEASE); }
  void signalCounter(int value) {
    __atomic_fetch_sub(&counter, value, __ATOMIC_RELAXED);
  }
  int poll() { return __atomic_load_n(&flag, __ATOMIC_RELAXED); }
  void wait() {
    while (__atomic_load_n(&counter, __ATOMIC_ACQUIRE) > 0) {
      sched_yield();
    }
  }
};

void cpuAsyncKernel(void *args);

#endif