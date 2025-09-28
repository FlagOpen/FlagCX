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

struct flagcxHostFuncSignal {
  volatile int groupOpCount = 0;
  volatile int readyToTrigger = 0;
};

void cpuAsyncStore(void *args);
void cpuAsyncLoad(void *args);
void cpuAsyncLoadWithMaxSpinCount(void *args);

#endif