/*************************************************************************
 * Copyright (c) 2025 by MetaX Integrated Circuits (Shanghai) Co., Ltd. All Rights Reserved.
 ************************************************************************/

#ifdef USE_KUNLUNXIN_ADAPTOR

#include <map>

#include <bkcl.h>
#include <cuda.h>
#include <cuda_runtime.h>

#include "adaptor.h"
#include "alloc.h"
#include "comm.h"
#include "flagcx.h"

struct flagcxInnerComm {
    // xcclComm_t base; // todo:这个看着是我们的BKCLContext？
};

struct flagcxStream {
    cudaStream_t base;
};

struct flagcxEvent {
    cudaEvent_t base;  // todo： 这个对应xpu是啥 init.h 文件里只搜索到了rdmaenvent的变量，我感觉明显不对。
};

#define DEVCHECK(func)                                                         \
  {                                                                            \
    int ret = func;                                                            \
    if (ret != cudaSuccess)                                                      \
      return flagcxUnhandledDeviceError;                                       \
  }

#endif // USE_KUNLUNXIN_ADAPTOR