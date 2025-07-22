#ifndef FLAGCX_LAUNCH_KERNEL_H_
typedef struct flagcxStream *flagcxStream_t;
typedef void (*flagcxLaunchFunc_t)(flagcxStream_t, void *);
#define FLAGCX_LAUNCH_KERNEL_H_
#pragma once
#include "adaptor.h"
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

flagcxResult_t loadAsyncKernelSymbol(const char *path, flagcxLaunchFunc_t *fn);

#ifdef __cplusplus
}
#endif

struct hostLaunchArgs {
  volatile bool stopLaunch;
  volatile bool retLaunch;
};

extern flagcxLaunchFunc_t deviceKernel;

void cpuAsyncLaunch(void *_args);
void cpuStreamWait(void *_args);

/*
Reference CUDA implementation for async kernel launch (for future adaptor implementations)
1. 核函数：写 dEvent = true，等待 flag == true
__global__ void asyncLaunchKernel(const volatile bool* __restrict__ flag,
                                  bool* __restrict__ dEvent)
{
    *dEvent = 1;   // 设置事件（host 可轮询）
    printf("kernel enter\n");

    while (!(*flag)) { // 可插入 __nanosleep(100); }

    __threadfence_system();  // 保证写入对主机可见
    printf("kernel done\n");
}

// 2. host launcher，暴露成 C 接口（唯一版本）
flagcxResult_t launchAsyncKernel(flagcxStream_t stream, FuncArgs* args)
{

    bool* d_flag  = static_cast<bool*>(args->dargs);
    bool* d_event = static_cast<bool*>(args->dEvent);

    asyncLaunchKernel<<<1, 1, 0, stream->base>>>(d_flag, d_event);
    return flagcxSuccess;
}
*/

#endif
