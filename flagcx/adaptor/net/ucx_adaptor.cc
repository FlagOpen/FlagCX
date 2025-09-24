/*************************************************************************
 * SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES
 * Copyright (c) 2016-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifdef USE_UCX

#include "adaptor.h"
#include "comm.h"
#include "core.h"
#include "net.h"
#include "param.h"
#include "socket.h"
#include "utils.h"
#include "flagcx.h"
#include "debug.h"
#include "ibvwrap.h"
#include "ib_common.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ucp/api/ucp.h>

// Additional macro definitions
#define MAX_REQUESTS 16
// Parameter function declarations (now in ib_common.h)

// Type definitions
// Structures now defined in ib_common.h
#define PTHREADCHECKGOTO(statement, name, RES, label) do { \
  int retval = (statement); \
  if (retval != 0) { \
    WARN("Call to " name " failed: %s", strerror(retval)); \
    RES = flagcxSystemError; \
    goto label; \
  } \
} while (0)
// Enums and constants (now defined in ib_common.h)

#define FLAGCX_IB_LLSTR(link_layer) ((link_layer) == IBV_LINK_LAYER_INFINIBAND ? "IB" : "ETH")
#define FLAGCX_NET_IB_MAX_RECVS 16
#define FLAGCX_STATIC_ASSERT(condition, message) static_assert(condition, message)
#define FLAGCX_NET_HANDLE_MAXSIZE 128
FLAGCX_PARAM(SharpMaxComms, "SHARP_MAX_COMMS", 1);
// Global variables (now defined in ib_common.h)

// Additional global variables
static int flagcxNIbDevs = -1;
static int flagcxNMergedIbDevs = -1;
static int flagcxNSharpDevs = 0;
static pthread_mutex_t flagcx_p2p_lock = PTHREAD_MUTEX_INITIALIZER;
static int flagcxIbGdrModuleLoaded = 0;
static struct {
  pthread_once_t once;
} onces[MAX_IB_DEVS];

flagcxResult_t flagcxIbMakeVDeviceInternal(int* d, flagcxNetVDeviceProps_t* props, int flagcxNIbDevs, int *flagcxNMergedIbDevs) {
  if ((flagcxParamIbMergeNics() == 0) && props->ndevs > 1) {
    INFO(FLAGCX_NET, "NET/IB : Skipping makeVDevice, flagcx_IB_MERGE_NICS=0");
    return flagcxInvalidUsage;
  }

  if (props->ndevs == 0) {
   WARN("NET/IB : Can't make virtual NIC with 0 devices");
   return flagcxInvalidUsage;
  }

  if (*flagcxNMergedIbDevs == MAX_IB_DEVS) {
    WARN("NET/IB : Cannot allocate any more virtual devices (%d)", MAX_IB_DEVS);
    return flagcxInvalidUsage;
  }

  // Always count up number of merged devices
  flagcxIbMergedDev* mDev = flagcxIbMergedDevs + *flagcxNMergedIbDevs;
  mDev->vProps.ndevs = 0;
  mDev->speed = 0;

  for (int i = 0; i < props->ndevs; i++) {
    flagcxIbDev* dev = flagcxIbDevs + props->devs[i];
    if (mDev->vProps.ndevs == 2) return flagcxInvalidUsage; // FLAGCX_IB_MAX_DEVS_PER_NIC
    mDev->vProps.devs[mDev->vProps.ndevs++] = props->devs[i];
    mDev->speed += dev->speed;
    // Each successive time, copy the name '+' new name
    if (mDev->vProps.ndevs > 1) {
      snprintf(mDev->devName + strlen(mDev->devName), sizeof(mDev->devName) - strlen(mDev->devName), "+%s", dev->devName);
    // First time, copy the plain name
    } else {
      strncpy(mDev->devName, dev->devName, MAXNAMESIZE);
     }
   }

  // Check link layers
  flagcxIbDev* dev0 = flagcxIbDevs + props->devs[0];
  for (int i = 1; i < props->ndevs; i++) {
    if (props->devs[i] >= flagcxNIbDevs) {
      WARN("NET/IB : Cannot use physical device %d, max %d", props->devs[i], flagcxNIbDevs);
      return flagcxInvalidUsage;
    }
    flagcxIbDev* dev = flagcxIbDevs + props->devs[i];
    if (dev->link != dev0->link) {
      WARN("NET/IB : Attempted to merge incompatible devices: [%d]%s:%d/%s and [%d]%s:%d/%s. Try selecting NICs of only one link type using flagcx_IB_HCA",
        props->devs[0], dev0->devName, dev0->portNum, "IB", props->devs[i], dev->devName, dev->portNum, "IB"); 
      return flagcxInvalidUsage;
    }
  }

  *d = *flagcxNMergedIbDevs;
  (*flagcxNMergedIbDevs)++;

  INFO(FLAGCX_NET, "NET/IB : Made virtual device [%d] name=%s speed=%d ndevs=%d", *d, mDev->devName, mDev->speed, mDev->vProps.ndevs);
  return flagcxSuccess;
}
static int flagcxIbMatchVfPath(char* path1, char* path2) {
  // Merge multi-port NICs into the same PCI device
  if (flagcxParamIbMergeVfs()) {
    return strncmp(path1, path2, strlen(path1)-4) == 0;
  } else {
    return strncmp(path1, path2, strlen(path1)-1) == 0;
  }
}
static void flagcxIbStatsFatalError(struct flagcxIbStats* stat){
  __atomic_fetch_add(&stat->fatalErrorCount, 1, __ATOMIC_RELAXED);
}
static void flagcxIbQpFatalError(struct ibv_qp* qp) {
  flagcxIbStatsFatalError((struct flagcxIbStats*)qp->qp_context);
}
static void flagcxIbCqFatalError(struct ibv_cq* cq) {
  flagcxIbStatsFatalError((struct flagcxIbStats*)cq->cq_context);
}
static void flagcxIbDevFatalError(struct flagcxIbDev* dev) {
  flagcxIbStatsFatalError(&dev->stats);
}
#define KNL_MODULE_LOADED(a) ((access(a, F_OK) == -1) ? 0 : 1)
static void ibGdrSupportInitOnce() {
  // Check for the nv_peer_mem module being loaded
  flagcxIbGdrModuleLoaded = KNL_MODULE_LOADED("/sys/kernel/mm/memory_peers/nv_mem/version") ||
                          KNL_MODULE_LOADED("/sys/kernel/mm/memory_peers/nv_mem_nc/version") ||
                          KNL_MODULE_LOADED("/sys/module/nvidia_peermem/version");
}

/* for data direct nic, the device name is ends with suffix '_dma`.
 * remove this suffix before passing name to libsharp */
 void plugin_get_device_name(const char *input, char *output, size_t output_size) {
  const char *suffix = "_dma";
  size_t input_len = strlen(input);
  size_t suffix_len = strlen(suffix);

  if (input_len >= suffix_len && strcmp(input + input_len - suffix_len, suffix) == 0) {
    size_t new_len = input_len - suffix_len;
    if (new_len >= output_size) {
      new_len = output_size - 1;
    }
    memcpy(output, input, new_len);
    output[new_len] = '\0';
  } else {
    strncpy(output, input, output_size - 1);
    output[output_size - 1] = '\0';
  }
}
// Missing arrays and constants
static int ibv_widths[] = {1, 2, 4, 8, 12};
static int ibv_speeds[] = {2500, 5000, 10000, 14000, 25000, 50000, 100000, 200000};

// Helper function
static int first_bit_set(int value, int max_bits) {
  for (int i = 0; i <= max_bits; i++) {
    if (value & (1 << i)) return i;
  }
  return max_bits;
}

// Function implementations from ucx_adaptor.cc
int flagcx_p2p_ib_width(int width)
{
  return ibv_widths[first_bit_set(width, sizeof(ibv_widths)/sizeof(int)-1)];
}

int flagcx_p2p_ib_speed(int speed)
{
  return ibv_speeds[first_bit_set(speed, sizeof(ibv_speeds)/sizeof(int)-1)];
}

flagcxResult_t flagcxIbStatsInit(struct flagcxIbStats* stat) {
  __atomic_store_n(&stat->fatalErrorCount, 0, __ATOMIC_RELAXED);
  return flagcxSuccess;
}

flagcxResult_t flagcx_p2p_ib_pci_path(flagcxIbDev *devs, int num_devs, char* dev_name, char** path, int* real_port)
{
  char device_path[PATH_MAX];
  snprintf(device_path, PATH_MAX, "/sys/class/infiniband/%s/device", dev_name);
  char* p = realpath(device_path, NULL);
  if (p == NULL) {
    WARN("Could not find real path of %s", device_path);
  } else {
    // Merge multi-port NICs into the same PCI device
    p[strlen(p)-1] = '0';
    // Also merge virtual functions (VF) into the same device
    if (flagcxParamIbMergeVfs()) p[strlen(p)-3] = p[strlen(p)-4] = '0';
    // Keep the real port aside (the ibv port is always 1 on recent cards)
    *real_port = 0;
    for (int d=0; d<num_devs; d++) {
      if (flagcxIbMatchVfPath(p, flagcxIbDevs[d].pciPath)) (*real_port)++;
    }
    *path = p;
  }
  return flagcxSuccess;
}

static void* flagcxIbAsyncThreadMain(void* args) {
  struct flagcxIbDev* dev = (struct flagcxIbDev*)args;
  while (1) {
    struct ibv_async_event event;
    if (flagcxSuccess != flagcxWrapIbvGetAsyncEvent(dev->context, &event)) { break; }
    char *str;
    struct ibv_cq* cq __attribute__((unused)) = event.element.cq;    // only valid if CQ error
    struct ibv_qp* qp __attribute__((unused)) = event.element.qp;    // only valid if QP error
    struct ibv_srq* srq __attribute__((unused)) = event.element.srq; // only valid if SRQ error
    if (flagcxSuccess != flagcxWrapIbvEventTypeStr(&str, event.event_type)) { break; }
    switch (event.event_type) {
    case IBV_EVENT_DEVICE_FATAL:
      // the above is device fatal error
      WARN("NET/IB : %s:%d async fatal event: %s", dev->devName, dev->portNum, str);
      flagcxIbDevFatalError(dev);
      break;
    case IBV_EVENT_PORT_ACTIVE:
    case IBV_EVENT_PORT_ERR:
      WARN("NET/IB : %s:%d port event: %s", dev->devName, dev->portNum, str);
      break;
    case IBV_EVENT_CQ_ERR:
      WARN("NET/IB : %s:%d CQ event: %s", dev->devName, dev->portNum, str);
      break;
    case IBV_EVENT_QP_FATAL:
    case IBV_EVENT_QP_ACCESS_ERR:
      WARN("NET/IB : %s:%d QP event: %s", dev->devName, dev->portNum, str);
      break;
    case IBV_EVENT_SRQ_ERR:
      WARN("NET/IB : %s:%d SRQ event: %s", dev->devName, dev->portNum, str);
      break;
    default:
      WARN("NET/IB : %s:%d unknown event: %s", dev->devName, dev->portNum, str);
    }
    if (flagcxSuccess != flagcxWrapIbvAckAsyncEvent(&event)) { break; }
  }
  return NULL;
}





#define UCXCHECK(cmd) do {                           \
  ucs_status_t e = cmd;                              \
  if( UCS_OK != e ) {                                \
    WARN("Failed: UCX error %s:%d '%s'\n",           \
        __FILE__,__LINE__, ucs_status_string(e));    \
    return flagcxInternalError;                      \
  }                                                  \
} while(0)

#define UCXCHECK_VOID(cmd) do {                      \
  ucs_status_t e = cmd;                              \
  if( UCS_OK != e ) {                                \
    WARN("Failed: UCX error %s:%d '%s'\n",           \
        __FILE__,__LINE__, ucs_status_string(e));    \
  }                                                  \
} while(0)

// With flagcxNet_v11_t the FlagCX core initializes the network plugin per-communicator
// rather than once for all communicators. However, the internal plugin implementation
// still assumes the plugin is initialized only once across all communicators. The ref
// counter makes sure the plugin internally initializes only once. When per communicator
// context support is added to the plugin the ref counter can be removed.
static int flagcxUcxRefCount = 0;


FLAGCX_PARAM(UCXDisable, "UCX_DISABLE", 0);
/* Exclude cuda-related UCX transports */
FLAGCX_PARAM(UCXCudaDisable, "UCX_CUDA_DISABLE", 1);

flagcxDebugLogger_t flagcxUcxPluginLogFunction;
static const ucp_tag_t tag      = 0x8a000000;
static const ucp_tag_t tagMask = (uint64_t)(-1);


enum flagcxUCXCommState {
  flagcxUCXCommStateStart = 0,
  flagcxUCXCommStateConnect = 1,
  flagcxUCXCommStateAccept = 3,
};

struct flagcxUCXCommStage {
  enum flagcxUCXCommState state;
  uint8_t iteration;
  void* sock;
  void* comm;
};

typedef struct flagcxUcxMhandle {
  ucp_mem_h  ucp_memh;
  ucp_rkey_h rkey;
  int        mem_type;
} flagcxUcxMhandle_t;

flagcxResult_t flagcxUcxDevices(int* ndev) {
  *ndev = flagcxNIbDevs;
  return flagcxSuccess;
}

static __thread int flagcxUcxDmaSupportInitDev; // which device to init, must be thread local
static void flagcxUcxDmaBufSupportInitOnce(){
  flagcxResult_t res;
  int dev_fail = 0;

  // This is a physical device, not a virtual one, so select from ibDevs
  flagcxIbMergedDev* mergedDev = flagcxIbMergedDevs + flagcxUcxDmaSupportInitDev;
  flagcxIbDev* ibDev = flagcxIbDevs + mergedDev->vProps.devs[0];
  struct ibv_pd* pd;
  struct ibv_context* ctx = ibDev->context;
  FLAGCXCHECKGOTO(flagcxWrapIbvAllocPd(&pd, ctx), res, failure);
  // Test kernel DMA-BUF support with a dummy call (fd=-1)
  (void)flagcxWrapDirectIbvRegMr(pd, 0ULL /*addr*/, 0ULL /*len*/, 0 /*access*/);
  // ibv_reg_dmabuf_mr() will fail with EOPNOTSUPP/EPROTONOSUPPORT if not supported (EBADF otherwise)
  dev_fail |= (errno == EOPNOTSUPP) || (errno == EPROTONOSUPPORT);
  FLAGCXCHECKGOTO(flagcxWrapIbvDeallocPd(pd), res, failure);
  // stop the search and goto failure
  if (dev_fail) goto failure;
  ibDev->dmaBufSupported = 1;
  return;
failure:
  ibDev->dmaBufSupported = -1;
  return;
}
flagcxResult_t flagcx_p2p_dmabuf_support(int dev) {
  // init the device only once
  flagcxUcxDmaSupportInitDev = dev;
  pthread_once(&onces[dev].once, flagcxUcxDmaBufSupportInitOnce);
  flagcxIbMergedDev* mergedDev = flagcxIbMergedDevs + flagcxUcxDmaSupportInitDev;
  flagcxIbDev* ibDev = flagcxIbDevs + mergedDev->vProps.devs[0];
  int dmaBufSupported = ibDev->dmaBufSupported;
  if (dmaBufSupported == 1) return flagcxSuccess;
  return flagcxSystemError;
}
flagcxResult_t flagcx_p2p_gdr_support()
{
  static pthread_once_t once = PTHREAD_ONCE_INIT;
  pthread_once(&once, ibGdrSupportInitOnce);
  if (!flagcxIbGdrModuleLoaded)
    return flagcxSystemError;
  return flagcxSuccess;
}

flagcxResult_t flagcx_p2p_ib_init(int *nDevs, int *nmDevs, flagcxIbDev *flagcxIbDevs, char *flagcxIbIfName, union flagcxSocketAddress *flagcxIbIfAddr, pthread_t *flagcxIbAsyncThread, flagcxDebugLogger_t logFunction)
{
    printf("liuheliuhe \n");
  flagcxResult_t ret = flagcxSuccess;
  int flagcxNIbDevs = *nDevs;
  int flagcxNMergedIbDevs = *nmDevs;
  flagcxUcxPluginLogFunction = logFunction;
  if (flagcxNIbDevs == -1) {
    for (int i=0; i< MAX_IB_DEVS; i++)
      onces[i].once = PTHREAD_ONCE_INIT;
    pthread_mutex_lock(&flagcx_p2p_lock);
    flagcxWrapIbvForkInit();
    if (flagcxNIbDevs == -1) {
      int nIpIfs = 0;
      flagcxNIbDevs = 0;
      flagcxNMergedIbDevs = 0;
      flagcxNSharpDevs = 0;
      nIpIfs = flagcxFindInterfaces(flagcxIbIfName, flagcxIbIfAddr, MAX_IF_NAME_SIZE, 1);
      if (nIpIfs != 1) {
        WARN("NET/IB : No IP interface found.");
        ret = flagcxInternalError;
        goto fail;
      }

      // Detect IB cards
      int nIbDevs;
      struct ibv_device** devices;
      // Check if user defined which IB device:port to use
      const char* userIbEnv = flagcxGetEnv("FLAGCX_IB_HCA");
      struct netIf userIfs[MAX_IB_DEVS];
      int searchNot = userIbEnv && userIbEnv[0] == '^';
      if (searchNot) userIbEnv++;
      int searchExact = userIbEnv && userIbEnv[0] == '=';
      if (searchExact) userIbEnv++;
      int nUserIfs = parseStringList(userIbEnv, userIfs, MAX_IB_DEVS);

      if (flagcxSuccess != flagcxWrapIbvGetDeviceList(&devices, &nIbDevs)) { ret = flagcxInternalError; goto fail; }
      for (int d=0; d<nIbDevs && flagcxNIbDevs<MAX_IB_DEVS; d++) {
        struct ibv_context * context;
        if (flagcxSuccess != flagcxWrapIbvOpenDevice(&context, devices[d]) || context == NULL) {
          WARN("NET/IB : Unable to open device %s", devices[d]->name);
          continue;
        }
        enum flagcxIbProvider ibProvider = IB_PROVIDER_NONE;
        char dataDirectDevicePath[PATH_MAX];
        int dataDirectSupported = 0;
        int skipNetDevForDataDirect = 0;
        int nPorts = 0;
        struct ibv_device_attr devAttr;
        if (flagcxSuccess != flagcxWrapIbvQueryDevice(context, &devAttr)) {
          WARN("NET/IB : Unable to query device %s", devices[d]->name);
          if (flagcxSuccess != flagcxWrapIbvCloseDevice(context)) { ret = flagcxInternalError; goto fail; }
          continue;
        }
        for (int port_num = 1; port_num <= devAttr.phys_port_cnt; port_num++) {
          for (int dataDirect = skipNetDevForDataDirect; dataDirect < 1 + dataDirectSupported; ++dataDirect) {
            struct ibv_port_attr portAttr;
            uint32_t portSpeed;
            if (flagcxSuccess != flagcxWrapIbvQueryPort(context, port_num, &portAttr)) {
              WARN("NET/IB : Unable to query port_num %d", port_num);
              continue;
            }
            if (portAttr.state != IBV_PORT_ACTIVE) continue;
            if (portAttr.link_layer != IBV_LINK_LAYER_INFINIBAND
                && portAttr.link_layer != IBV_LINK_LAYER_ETHERNET) continue;

            // check against user specified HCAs/ports
            if (! (matchIfList(devices[d]->name, port_num, userIfs, nUserIfs, searchExact) ^ searchNot)) {
              continue;
            }
            pthread_mutex_init(&flagcxIbDevs[flagcxNIbDevs].lock, NULL);
            flagcxIbDevs[flagcxNIbDevs].device = d;
            flagcxIbDevs[flagcxNIbDevs].ibProvider = ibProvider;
            flagcxIbDevs[flagcxNIbDevs].guid = devAttr.sys_image_guid;
            flagcxIbDevs[flagcxNIbDevs].portAttr = portAttr;
            flagcxIbDevs[flagcxNIbDevs].portNum = port_num;
            flagcxIbDevs[flagcxNIbDevs].link = portAttr.link_layer;
            #if HAVE_STRUCT_IBV_PORT_ATTR_ACTIVE_SPEED_EX
            portSpeed = portAttr.active_speed_ex ? portAttr.active_speed_ex : portAttr.active_speed;
            #else
            portSpeed = portAttr.active_speed;
            #endif
            flagcxIbDevs[flagcxNIbDevs].speed = flagcx_p2p_ib_speed(portSpeed) * flagcx_p2p_ib_width(portAttr.active_width);
            flagcxIbDevs[flagcxNIbDevs].context = context;
            flagcxIbDevs[flagcxNIbDevs].pdRefs = 0;
            flagcxIbDevs[flagcxNIbDevs].pd = NULL;
            if (!dataDirect) {
              strncpy(flagcxIbDevs[flagcxNIbDevs].devName, devices[d]->name, MAXNAMESIZE);
              FLAGCXCHECKGOTO(flagcx_p2p_ib_pci_path(flagcxIbDevs, flagcxNIbDevs, flagcxIbDevs[flagcxNIbDevs].devName, &flagcxIbDevs[flagcxNIbDevs].pciPath, &flagcxIbDevs[flagcxNIbDevs].realPort), ret, fail);
            } else {
              snprintf(flagcxIbDevs[flagcxNIbDevs].devName, MAXNAMESIZE, "%.*s_dma", 
                       (int)(MAXNAMESIZE - 5), devices[d]->name);
              flagcxIbDevs[flagcxNIbDevs].pciPath = (char*)malloc(PATH_MAX);
              strncpy(flagcxIbDevs[flagcxNIbDevs].pciPath, dataDirectDevicePath, PATH_MAX);
              flagcxIbDevs[flagcxNIbDevs].capsProvider.mlx5.dataDirect = 1;
            }
            flagcxIbDevs[flagcxNIbDevs].maxQp = devAttr.max_qp;
            flagcxIbDevs[flagcxNIbDevs].mrCache.capacity = 0;
            flagcxIbDevs[flagcxNIbDevs].mrCache.population = 0;
            flagcxIbDevs[flagcxNIbDevs].mrCache.slots = NULL;
            FLAGCXCHECK(flagcxIbStatsInit(&flagcxIbDevs[flagcxNIbDevs].stats));

          // Enable ADAPTIVE_ROUTING by default on IB networks
            // But allow it to be overloaded by an env parameter
            flagcxIbDevs[flagcxNIbDevs].ar = (portAttr.link_layer == IBV_LINK_LAYER_INFINIBAND) ? 1 : 0;
            if (flagcxParamIbAdaptiveRouting() != -2) flagcxIbDevs[flagcxNIbDevs].ar = flagcxParamIbAdaptiveRouting();

            flagcxIbDevs[flagcxNIbDevs].isSharpDev = 0;
            if (portAttr.link_layer == IBV_LINK_LAYER_INFINIBAND)
            {
              flagcxIbDevs[flagcxNIbDevs].isSharpDev = 1;
              flagcxIbDevs[flagcxNIbDevs].maxQp = flagcxParamSharpMaxComms();
              flagcxNSharpDevs++;
            }
            TRACE(FLAGCX_NET,"NET/IB: [%d] %s:%s:%d/%s provider=%s speed=%d context=%p pciPath=%s ar=%d", d, devices[d]->name, devices[d]->dev_name, flagcxIbDevs[flagcxNIbDevs].portNum,
              FLAGCX_IB_LLSTR(portAttr.link_layer), ibProviderName[flagcxIbDevs[flagcxNIbDevs].ibProvider], flagcxIbDevs[flagcxNIbDevs].speed, context, flagcxIbDevs[flagcxNIbDevs].pciPath, flagcxIbDevs[flagcxNIbDevs].ar);
            if (flagcxIbAsyncThread != NULL) {
              PTHREADCHECKGOTO(pthread_create(flagcxIbAsyncThread, NULL, flagcxIbAsyncThreadMain, flagcxIbDevs + flagcxNIbDevs), "pthread_create", ret, fail);
              flagcxSetThreadName(*flagcxIbAsyncThread, "flagcx IbAsync %2d", flagcxNIbDevs);
              PTHREADCHECKGOTO(pthread_detach(*flagcxIbAsyncThread), "pthread_detach", ret, fail); // will not be pthread_join()'d
            }

            // Add this plain physical device to the list of virtual devices
            int vDev;
            flagcxNetVDeviceProps_t vProps = {0};
            vProps.ndevs = 1;
            vProps.devs[0] = flagcxNIbDevs;
            FLAGCXCHECK(flagcxIbMakeVDeviceInternal(&vDev, &vProps, flagcxNIbDevs, &flagcxNMergedIbDevs));

            flagcxNIbDevs++;
            nPorts++;
          }
        }
        if (nPorts == 0 && flagcxSuccess != flagcxWrapIbvCloseDevice(context))  { ret = flagcxInternalError; goto fail; }
      }
      
      if (nIbDevs && (flagcxSuccess != flagcxWrapIbvFreeDeviceList(devices))) { ret = flagcxInternalError; goto fail; };
    }
    if (flagcxNIbDevs == 0) {
      INFO(FLAGCX_INIT|FLAGCX_NET, "NET/IB : No device found.");
    }

    // Print out all net devices to the user (in the same format as before)
    char line[2048];
    line[0] = '\0';
    // Determine whether RELAXED_ORDERING is enabled and possible
#ifdef HAVE_IB_PLUGIN
    flagcxIbRelaxedOrderingEnabled = flagcxIbRelaxedOrderingCapable();
#endif
    for (int d = 0; d < flagcxNIbDevs; d++) {
#ifdef HAVE_SHARP_PLUGIN
            snprintf(line+strlen(line), sizeof(line)-strlen(line), " [%d]%s:%d/%s%s", d, flagcxIbDevs[d].devName,
              flagcxIbDevs[d].portNum, FLAGCX_IB_LLSTR(flagcxIbDevs[d].link),
              flagcxIbDevs[d].isSharpDev ? "/SHARP" : "");
#else
      snprintf(line+strlen(line), sizeof(line)-strlen(line), " [%d]%s:%d/%s", d, flagcxIbDevs[d].devName,
        flagcxIbDevs[d].portNum, FLAGCX_IB_LLSTR(flagcxIbDevs[d].link));
#endif
    }
    char addrline[SOCKET_NAME_MAXLEN+1];
    INFO(FLAGCX_INIT|FLAGCX_NET, "NET/IB : Using%s %s; OOB %s:%s", line, 
#ifdef HAVE_IB_PLUGIN
      flagcxIbRelaxedOrderingEnabled ? "[RO]" : ""
#else
      ""
#endif
      ,
      flagcxIbIfName, flagcxSocketToString(flagcxIbIfAddr, addrline, 1));
    *nDevs = flagcxNIbDevs;
    *nmDevs = flagcxNMergedIbDevs;
    pthread_mutex_unlock(&flagcx_p2p_lock);
  }
exit:
  return ret;
fail:
  pthread_mutex_unlock(&flagcx_p2p_lock);
  goto exit;
}
flagcxResult_t flagcxIbGetPhysProperties(int dev, flagcxNetProperties_v8_t* props) {
  struct flagcxIbDev* ibDev = flagcxIbDevs + dev;
  pthread_mutex_lock(&ibDev->lock);
  props->name = ibDev->devName;
  props->speed = ibDev->speed;
  props->pciPath = ibDev->pciPath;
  props->guid = ibDev->guid;
  props->ptrSupport   = FLAGCX_PTR_HOST;
  if (flagcx_p2p_gdr_support() == flagcxSuccess) {
    props->ptrSupport |= FLAGCX_PTR_CUDA; // GDR support via nv_peermem
    INFO(FLAGCX_NET,"NET/IB : GPU Direct RDMA (nvidia-peermem) enabled for HCA %d '%s", dev, ibDev->devName);
  }
  props->regIsGlobal = 1;
  if (flagcx_p2p_dmabuf_support(dev) == flagcxSuccess) {
    props->ptrSupport |= FLAGCX_PTR_DMABUF; // GDR support via DMA-BUF
    INFO(FLAGCX_NET,"NET/IB : GPU Direct RDMA (DMABUF) enabled for HCA %d '%s", dev, ibDev->devName);
  }

  props->latency      = 0; // Not set
  props->port = ibDev->portNum + ibDev->realPort;
  props->maxComms = ibDev->maxQp;
  props->maxRecvs = FLAGCX_NET_IB_MAX_RECVS;
  props->netDeviceType = FLAGCX_NET_DEVICE_HOST;
  props->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  pthread_mutex_unlock(&ibDev->lock);
  return flagcxSuccess;
}
flagcxResult_t flagcx_p2p_ib_get_properties(flagcxIbDev *devs, int flagcxNMergedIbDevs, int dev, flagcxNetProperties_v8_t* props)
{
  if (dev >= flagcxNMergedIbDevs) {
    WARN("NET/IB : Requested properties for vNic %d, only %d vNics have been created", dev, flagcxNMergedIbDevs);
    return flagcxInvalidUsage;
  }
  struct flagcxIbMergedDev* mergedDev = flagcxIbMergedDevs + dev;
  // Take the rest of the properties from an arbitrary sub-device (should be the same)
  FLAGCXCHECK(flagcxIbGetPhysProperties(mergedDev->vProps.devs[0], props));
  props->name = mergedDev->devName;
  props->speed = mergedDev->speed;
//   memcpy(&props->vProps, &mergedDev->vProps, sizeof(flagcxNetVDeviceProps_v8_t));
  return flagcxSuccess;
}
flagcxResult_t flagcxUcxGetProperties(int dev, void* props)
{
  return flagcx_p2p_ib_get_properties(flagcxIbDevs, flagcxNMergedIbDevs, dev, (flagcxNetProperties_v8_t*)props);
}


pthread_mutex_t flagcxUcxLock = PTHREAD_MUTEX_INITIALIZER;

struct flagcxUcxEpList {
  struct flagcxSocket *sock;
  struct flagcxUcxEpList *next;
};

/**
 * Connection descriptor. Used to store all opened connections.
 */
typedef struct ucx_worker {
  ucp_worker_h   worker;   /* ucp worker associated with ctx */
  ucp_context_h  ctx;      /* ucp_context bounded to specific device */
  struct flagcxUcxEpList *eps;     /* oob conection to all endpoints that were opened on this worker */

  int            count;    /* number of connections that uses this worker */
  int            dev;      /* Managed device */
  pthread_t      thread;   /* Owner thread */

  struct ucx_worker *next;
} ucx_worker_t;

/**
 * Listen handle that is sent from receiver to sender through OOB connection
 */
typedef struct flagcxUcxListenHandle {
  union flagcxSocketAddress connectAddr; /* reciever socket address */
  uint64_t magic;                      /* random number to help debugging */
  ucp_tag_t               tag;         /* tag that is used to distiguish data that was sent to
                                          this reciever. Required when shared worker is used. */
  struct flagcxUCXCommStage stage;
} flagcxUcxListenHandle_t;

/**
 * Listen commincator for UCX plugin.
 */
typedef struct flagcxUcxListenComm {
  int           dev;    /* device number in flagcxIbDevs which will
                         * be used to recieve data */
  struct flagcxSocket sock;/* socket for OOB connection */
  ucp_context_h ctx;    /* ucp_context associated with specific device dev */
  ucx_worker_t *ucx_worker; /* ucx_worker created on ctx, worker can be shared between
                           multiple connections */
  ucp_tag_t     tag;    /* tag that is used to distiguish data that was sent to 
                           this reciever. Required when shared worker is used.*/
  struct flagcxUCXCommStage stage;
} flagcxUcxListenComm_t;

typedef struct flagcxUcxConnectMsg {
  size_t addr_len;
} flagcxUcxConnectMsg_t;

struct flagcxUcxComm;

/**
 * Batch of UCX Requests from FlagCX perspective
 */
typedef struct flagcxUcxRequest {
  struct flagcxUcxRequest *next;    /* Next request in the free list */
  struct flagcxUcxComm    *comm;    /* Owning communicator */
  ucp_worker_h        worker;  /* Worker for all requests */
  int                 pending; /* How many requests are still pending */
  int                 count;   /* How many requests are contained */
  int                 size[FLAGCX_NET_IB_MAX_RECVS];
} flagcxUcxRequest_t;

static ucp_tag_t              flagcxUcxWorkerTags[MAX_IB_DEVS];
static ucp_context_h          flagcxUcxCtx[MAX_IB_DEVS];
static struct ucx_worker *flagcxUcxWorkers[MAX_IB_DEVS];
static int ucx_workerCount = 0;

typedef struct flagcxUcxGpuFlush {
  int      enabled;
  int      hostMem;
  ucp_ep_h flush_ep;
} flagcxUcxGpuFlush_t;

/**
 * Common data member for ucx_comm for send and receive
 * Used to map/unmap memory in flagcxUcxRegMr/flagcxUcxDeregMr
 */
typedef struct flagcxUcxCtx {
  ucp_context_h   flagcxUcxCtx;
  flagcxUcxGpuFlush_t gpuFlush;
} flagcxUcxCtx_t;

/**
 * Sender and Receiver communicator
 */
typedef struct flagcxUcxComm {
  ucp_context_h   ctx;           /* ucp_context bounded to specific device */
  flagcxUcxGpuFlush_t gpuFlush;      /* flushing handle */
  ucx_worker_t *ucx_worker; /* ucp worker associated with ctx */
  ucp_ep_h        ep;            /* ucp endpoint created on worker */
  ucp_tag_t       tag;           /* datapath tag to filter out message that are not
                                    belong to this connnection */
  ucp_tag_t       ctag;          /* controlpath tag to filter out message that are not
                                    belong to this connnection */
  struct flagcxSocket sock;        /* socket for OOB connection */
  int             ready;         /* indicates that receive communicator is fully initialized */
  flagcxUcxRequest_t   reqs[MAX_REQUESTS]; /* max inflight requests */
  flagcxUcxRequest_t   *free_req;     /* first request available */
  flagcxUcxConnectMsg_t   *msg;          /* message to establish reverse connection */
  void            *connect_req;  /* msg request */
} flagcxUcxComm_t;

static void send_handler_nbx(void *request, ucs_status_t status,
                             void *user_data) {
  int *pending = (int*)user_data;

  assert(status == UCS_OK);
  assert(*pending > 0);
  (*pending)--;
  ucp_request_free(request);
}

static void recv_handler_nbx(void *request, ucs_status_t status,
                             const ucp_tag_recv_info_t *tag_info,
                             void *user_data) {
  send_handler_nbx(request, status, user_data);
}

static union flagcxSocketAddress flagcxUcxIfAddr;
static char if_name[MAX_IF_NAME_SIZE];

static flagcxResult_t flagcxUcxConfigNoCuda(ucp_config_t *config) {
  char tmp[PATH_MAX];
  const char *flagcxUcxTls;
  ssize_t n;

  flagcxUcxTls = getenv("FLAGCX_UCX_TLS");
  if (flagcxUcxTls == NULL) {
    flagcxUcxTls = getenv("UCX_TLS");
  }

  if (flagcxUcxTls == NULL) {
    flagcxUcxTls = "^cuda";
  } else if (flagcxUcxTls[0] == '^') {
    /* Negative expression, make sure to keep cuda excluded */
    n = snprintf(tmp, sizeof(tmp), "^cuda,%s", &flagcxUcxTls[1]);
    if (n >= sizeof(tmp)) {
      return flagcxInternalError;
    }

    flagcxUcxTls = tmp;
  } else {
    /* Positive expression cannot allow cuda-like transports */
    if ((strstr(flagcxUcxTls, "cuda") != NULL) || (strstr(flagcxUcxTls, "gdr") != NULL)) {
      WARN("Cannot use cuda/gdr transports as part of specified UCX_TLS");
      return flagcxInternalError;
    }
  }

  UCXCHECK(ucp_config_modify(config, "TLS", flagcxUcxTls));
  UCXCHECK(ucp_config_modify(config, "RNDV_THRESH", "0"));
  UCXCHECK(ucp_config_modify(config, "RNDV_SCHEME", "get_zcopy"));
  UCXCHECK(
      ucp_config_modify(config, "MEMTYPE_REG_WHOLE_ALLOC_TYPES", "unknown"));
  return flagcxSuccess;
}

static flagcxResult_t flagcxUcxInitContext(ucp_context_h *ctx, int dev) {
  ucp_params_t ucp_params;
  ucp_config_t *config;
  char         flagcxUcxDevName[PATH_MAX];
  flagcxResult_t result;

  if (flagcxUcxCtx[dev] == NULL) {
    plugin_get_device_name(flagcxIbDevs[dev].devName, flagcxUcxDevName, 64);
    snprintf(flagcxUcxDevName + strlen(flagcxUcxDevName), PATH_MAX - strlen(flagcxUcxDevName), ":%d",
             flagcxIbDevs[dev].portNum);

    UCXCHECK(ucp_config_read("FLAGCX", NULL, &config));
    UCXCHECK(ucp_config_modify(config, "NET_DEVICES", flagcxUcxDevName));

    if (flagcxParamUCXCudaDisable()) {
      result = flagcxUcxConfigNoCuda(config);
      if (result != flagcxSuccess) {
        return result;
      }
    }

    memset(&ucp_params, 0, sizeof(ucp_params));
    ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES;
    ucp_params.features   = UCP_FEATURE_TAG | UCP_FEATURE_RMA;

    UCXCHECK(ucp_init(&ucp_params, config, &flagcxUcxCtx[dev]));
    ucp_config_release(config);
  }

  *ctx = flagcxUcxCtx[dev];

  return flagcxSuccess;
}

static flagcxResult_t flagcxUcxInitWorker(ucp_context_h ctx, ucp_worker_h *worker) {
  ucp_worker_params_t worker_params;
  ucp_worker_attr_t   worker_attr;

  memset(&worker_params, 0, sizeof(worker_params));
  worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
  worker_params.thread_mode = UCS_THREAD_MODE_MULTI;

  UCXCHECK(ucp_worker_create(ctx, &worker_params, worker));

  worker_attr.field_mask = UCP_WORKER_ATTR_FIELD_THREAD_MODE;
  ucp_worker_query(*worker, &worker_attr);
  if (worker_attr.thread_mode != UCS_THREAD_MODE_MULTI) {
    INFO(FLAGCX_NET, "Thread mode multi is not supported");
  }

  return flagcxSuccess;
}

static flagcxResult_t flagcxUcxWorkerGetNetaddress(ucp_worker_h worker,
                                              ucp_address_t **address,
                                              size_t *address_length) {
  ucp_worker_attr_t attr;

  attr.field_mask    = UCP_WORKER_ATTR_FIELD_ADDRESS |
                       UCP_WORKER_ATTR_FIELD_ADDRESS_FLAGS;
  attr.address_flags = UCP_WORKER_ADDRESS_FLAG_NET_ONLY;

  UCXCHECK(ucp_worker_query(worker, &attr));
  *address = (ucp_address_t*)malloc(attr.address_length);
  if (address == NULL) {
    return flagcxSystemError;
  }

  memcpy(*address, attr.address, attr.address_length);
  *address_length = attr.address_length;
  free(attr.address);

  return flagcxSuccess;
}

static flagcxResult_t flagcxUcxGetCtxAndWorker(int dev, ucp_context_h *ctx,
                                           ucx_worker_t **ucx_worker,
                                           ucp_tag_t *newtag) {
  pthread_mutex_lock(&flagcxUcxLock);
  flagcxResult_t result;

  if (flagcxNIbDevs <= dev) {
    WARN("Device index is too large");
    goto err;
  }

  ucx_worker_t *w;
  for (w = flagcxUcxWorkers[dev]; w != NULL; w = w->next) {
    assert(w->dev == dev);
    if (w->thread == pthread_self()) {
      break;
    }
  }

  if (w == NULL) {
    w = (ucx_worker_t*)calloc(1, sizeof(*w));
    if (w == NULL) {
      WARN("Worker allocation failure");
      goto err;
    }

    w->dev    = dev;
    w->thread = pthread_self();
    w->count  = 0;

    result = flagcxUcxInitContext(&w->ctx, dev);
    if (result != flagcxSuccess) {
        return result;
    }
    flagcxUcxInitWorker(w->ctx, &w->worker);
    ucx_workerCount++;

    w->next = flagcxUcxWorkers[dev];
    flagcxUcxWorkers[dev] = w;
  }

  *ctx        = w->ctx;
  *ucx_worker = w;
  if (newtag != NULL) {
    *newtag = ++flagcxUcxWorkerTags[dev];
  }

  ucp_worker_progress(w->worker);
  w->count++;
  pthread_mutex_unlock(&flagcxUcxLock);
  return flagcxSuccess;

err:
  pthread_mutex_unlock(&flagcxUcxLock);
  return flagcxSystemError;
}

static flagcxResult_t flagcxUcxFreeWorker(ucx_worker_t *ucx_worker) {
  int dev, dummy, done = 0;
  struct flagcxUcxEpList *ep, *cur;
  struct ucx_worker *next;
  flagcxResult_t result;

  pthread_mutex_lock(&flagcxUcxLock);
  ucx_worker->count--;
  if (ucx_worker->count == 0) {
      ucx_workerCount--;
      done = ucx_workerCount == 0;
  }
  pthread_mutex_unlock(&flagcxUcxLock);

  if (!done) {
    return flagcxSuccess;
  }

  for (dev = 0; dev < sizeof(flagcxUcxWorkers) / sizeof(*flagcxUcxWorkers); dev++) {
    for (ucx_worker = flagcxUcxWorkers[dev]; ucx_worker != NULL; ucx_worker = next) {
      next = ucx_worker->next;
      assert(ucx_worker->count == 0);

      ep = ucx_worker->eps;
      while (ep) {
        cur    = ep;
        result = flagcxSocketRecv(ep->sock, &dummy, sizeof(int));
        if (result != flagcxSuccess) {
          WARN("Failed to receive close for worker cleanup (res:%d)", result);
        }

        ep = ep->next;
        close(cur->sock->fd);
        free(cur);
      }
      ucp_worker_destroy(ucx_worker->worker);
      free(ucx_worker);
    }

    flagcxUcxWorkers[dev] = NULL;
    if (flagcxUcxCtx[dev]) {
      ucp_cleanup(flagcxUcxCtx[dev]);
      flagcxUcxCtx[dev] = NULL;
    }
  }

  return flagcxSuccess;
}

static flagcxResult_t flagcxUcxAddEp(ucx_worker_t *ucx_worker,
                                    struct flagcxSocket *sock) {
  struct flagcxUcxEpList *new_ep = (struct flagcxUcxEpList*)malloc(sizeof(struct flagcxUcxEpList));
  if (new_ep == NULL) {
    return flagcxSystemError;
  }

  new_ep->sock    = sock;
  new_ep->next    = ucx_worker->eps;
  ucx_worker->eps = new_ep;
  return flagcxSuccess;
}



flagcxResult_t flagcxUcxInit() {
  if (flagcxUcxRefCount++) return flagcxSuccess;
  if (flagcxParamUCXDisable()) return flagcxInternalError;

  for (int i = 0; i < sizeof(flagcxUcxWorkerTags) / sizeof(*flagcxUcxWorkerTags); i++) {
    flagcxUcxWorkerTags[i] = tag;
  }

  // Initialize InfiniBand symbols first
  if (flagcxWrapIbvSymbols() != flagcxSuccess) {
    WARN("NET/UCX: Failed to initialize InfiniBand symbols");
    return flagcxInternalError;
  }

  return flagcx_p2p_ib_init(&flagcxNIbDevs, &flagcxNMergedIbDevs, flagcxIbDevs, if_name,
                          &flagcxUcxIfAddr, NULL, NULL);
}

flagcxResult_t flagcxUcxListen(int dev, void *handle, void **listen_comm) {
  flagcxUcxListenHandle_t *my_handle = (flagcxUcxListenHandle_t*)handle;
  flagcxUcxListenComm_t   *comm      = (flagcxUcxListenComm_t*)calloc(1, sizeof(*comm));

  FLAGCX_STATIC_ASSERT(sizeof(flagcxUcxListenHandle_t) < FLAGCX_NET_HANDLE_MAXSIZE,
                     "UCX listen handle size too large");
  my_handle->magic = FLAGCX_SOCKET_MAGIC;
  FLAGCXCHECK(flagcxSocketInit(&comm->sock, &flagcxUcxIfAddr, my_handle->magic, flagcxSocketTypeNetIb, NULL, 1));
  FLAGCXCHECK(flagcxSocketListen(&comm->sock));
  FLAGCXCHECK(flagcxSocketGetAddr(&comm->sock, &my_handle->connectAddr));
  FLAGCXCHECK(flagcxUcxGetCtxAndWorker(dev, &comm->ctx, &comm->ucx_worker, &comm->tag));

  comm->dev = dev;
  my_handle->tag = comm->tag;
  *listen_comm = comm;
 
  return flagcxSuccess;
}

static void flagcxUcxRequestInit(flagcxUcxComm_t *comm) {
  static const int entries = sizeof(comm->reqs) / sizeof(*comm->reqs);

  comm->free_req = NULL;
  for (int i = entries - 1; i >= 0; i--) {
      comm->reqs[i].comm = comm;
      comm->reqs[i].next = comm->free_req;
      comm->free_req = &comm->reqs[i];
  }
}

flagcxResult_t flagcxUcxConnect(int dev, void *handle, void **send_comm) {
  flagcxUcxListenHandle_t     *recv_handle = (flagcxUcxListenHandle_t*)handle;
  struct flagcxUCXCommStage *stage       = &recv_handle->stage;
  flagcxUcxComm_t              *comm        = (flagcxUcxComm_t*)stage->comm;
  ucp_address_t           *my_addr;
  size_t                  local_addr_len;
  int                     ready;

  *send_comm = NULL;

  if (stage->state == flagcxUCXCommStateConnect) goto flagcxUcxConnectCheck;

  FLAGCXCHECK(flagcxIbMalloc((void**)&comm, sizeof(flagcxUcxComm_t)));
  FLAGCXCHECK(flagcxSocketInit(&comm->sock, &recv_handle->connectAddr, recv_handle->magic, flagcxSocketTypeNetIb, NULL, 1));
  stage->comm = comm;
  stage->state = flagcxUCXCommStateConnect;
  FLAGCXCHECK(flagcxSocketConnect(&comm->sock));
  flagcxUcxRequestInit(comm);

flagcxUcxConnectCheck:
  /* since flagcxSocketConnect is async, we must check if connection is complete */
  FLAGCXCHECK(flagcxSocketReady(&comm->sock, &ready));
  if (!ready) return flagcxSuccess;

  FLAGCXCHECK(flagcxUcxGetCtxAndWorker(dev, &comm->ctx, &comm->ucx_worker, &comm->ctag));
  comm->tag              = recv_handle->tag;
  comm->gpuFlush.enabled = 0;
  FLAGCXCHECK(flagcxUcxWorkerGetNetaddress(comm->ucx_worker->worker, &my_addr, &local_addr_len));
  FLAGCXCHECK(flagcxUcxAddEp(comm->ucx_worker, &comm->sock));
  TRACE(FLAGCX_NET, "NET/UCX: Worker address length: %zu", local_addr_len);

  FLAGCXCHECK(flagcxSocketSend(&comm->sock, &local_addr_len, sizeof(size_t)));
  FLAGCXCHECK(flagcxSocketSend(&comm->sock, my_addr, local_addr_len));
  FLAGCXCHECK(flagcxSocketSend(&comm->sock, &comm->ctag, sizeof(ucp_tag_t)));

  *send_comm = comm;
  free(my_addr);
  return flagcxSuccess;
}

flagcxResult_t flagcxUcxAccept(void *listen_comm, void **recv_comm)
{
  flagcxUcxListenComm_t  *l_comm = (flagcxUcxListenComm_t *)listen_comm;
  struct flagcxUCXCommStage* stage = &l_comm->stage;
  flagcxUcxComm_t         *r_comm = (flagcxUcxComm_t *)stage->comm;
  size_t             peer_addr_len;
  ucp_address_t      *peer_addr;
  ucp_ep_params_t    ep_params;
  int                ready;

  *recv_comm = NULL;
  if (stage->state == flagcxUCXCommStateAccept) goto flagcxUcxAcceptCheck;

  FLAGCXCHECK(flagcxIbMalloc((void**)&r_comm, sizeof(flagcxUcxComm_t)));
  stage->comm = r_comm;
  stage->state = flagcxUCXCommStateAccept;
  l_comm->sock.asyncFlag = 1;
  r_comm->sock.asyncFlag = 1;

  FLAGCXCHECK(flagcxSocketInit(&r_comm->sock, NULL, FLAGCX_SOCKET_MAGIC, flagcxSocketTypeUnknown, NULL, 0));
  FLAGCXCHECK(flagcxSocketAccept(&r_comm->sock, &l_comm->sock));
flagcxUcxAcceptCheck:
  FLAGCXCHECK(flagcxSocketReady(&r_comm->sock, &ready));
  if (!ready) return flagcxSuccess;

  r_comm->ctx        = l_comm->ctx;
  r_comm->ucx_worker = l_comm->ucx_worker;
  r_comm->tag        = l_comm->tag;

  flagcxUcxRequestInit(r_comm);

  FLAGCXCHECK(flagcxSocketRecv(&r_comm->sock, &peer_addr_len, sizeof(size_t)));
  peer_addr = (ucp_address_t*)malloc(peer_addr_len);
  if (peer_addr == NULL) {
    return flagcxSystemError;
  }

  FLAGCXCHECK(flagcxSocketRecv(&r_comm->sock, peer_addr, peer_addr_len));
  ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
  ep_params.address    = peer_addr;
  UCXCHECK(ucp_ep_create(r_comm->ucx_worker->worker, &ep_params, &r_comm->ep));
  FLAGCXCHECK(flagcxSocketRecv(&r_comm->sock, &r_comm->ctag, sizeof(ucp_tag_t)));

  r_comm->gpuFlush.enabled = (flagcx_p2p_gdr_support() == flagcxSuccess);  
  if (r_comm->gpuFlush.enabled) {
    ucp_address_t *my_addr;
    size_t        local_addr_len;

    FLAGCXCHECK(flagcxUcxWorkerGetNetaddress(r_comm->ucx_worker->worker, &my_addr, &local_addr_len));
    ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
    ep_params.address    = my_addr;
    UCXCHECK(ucp_ep_create(r_comm->ucx_worker->worker, &ep_params, &r_comm->gpuFlush.flush_ep));
    free(my_addr);
  }

  free(peer_addr);
  *recv_comm = r_comm;

  return flagcxSuccess;
}

#define REG_ALIGN (4096)
flagcxResult_t flagcxUcxRegMr(void* comm, void* data, size_t size, int type, void** mhandle) {
  flagcxUcxCtx_t *ctx = (flagcxUcxCtx_t*)comm;
  uint64_t  addr = (uint64_t)  data;
  ucp_mem_map_params_t mmap_params;
  flagcxUcxMhandle_t        *mh;
  uint64_t             reg_addr, reg_size;
  size_t               rkey_buf_size;
  void                 *rkey_buf;
  
  FLAGCXCHECK(flagcxIbMalloc((void**)&mh, sizeof(flagcxUcxMhandle_t)));
  reg_addr = addr & (~(REG_ALIGN - 1));
  reg_size = addr + size - reg_addr;
  reg_size = ROUNDUP(reg_size, REG_ALIGN);

  mmap_params.field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
                           UCP_MEM_MAP_PARAM_FIELD_LENGTH; 
  mmap_params.address    = (void*)reg_addr;
  mmap_params.length     = reg_size;  
  mh->mem_type = (type == FLAGCX_PTR_HOST)? UCS_MEMORY_TYPE_HOST: UCS_MEMORY_TYPE_CUDA;
  mmap_params.field_mask  |= UCP_MEM_MAP_PARAM_FIELD_MEMORY_TYPE; 
  mmap_params.memory_type = (ucs_memory_type_t)mh->mem_type;

  UCXCHECK(ucp_mem_map(ctx->flagcxUcxCtx, &mmap_params, &mh->ucp_memh));
  if (ctx->gpuFlush.enabled) {
    UCXCHECK(ucp_rkey_pack(ctx->flagcxUcxCtx, mh->ucp_memh, &rkey_buf, &rkey_buf_size));
    UCXCHECK(ucp_ep_rkey_unpack(ctx->gpuFlush.flush_ep, rkey_buf, &mh->rkey));
    ucp_rkey_buffer_release(rkey_buf);
  }
  
  *mhandle = mh;
  return flagcxSuccess;
}

flagcxResult_t flagcxUcxDeregMr(void* comm, void* mhandle) {
  flagcxUcxCtx_t     *ctx = (flagcxUcxCtx_t*)comm;
  flagcxUcxMhandle_t *mh  = (flagcxUcxMhandle_t*)mhandle;

  if (ctx->gpuFlush.enabled) {
      ucp_rkey_destroy(mh->rkey);
  }

  ucp_mem_unmap(ctx->flagcxUcxCtx, mh->ucp_memh);
  free(mhandle);

  return flagcxSuccess;
}

flagcxResult_t flagcxUcxRegMrDmaBuf(void* comm, void* data, size_t size, int type, uint64_t offset, int fd, void** mhandle) {
	return flagcxUcxRegMr(comm, data, size, type, mhandle);
}

static flagcxUcxRequest_t *flagcxUcxRequestGet(flagcxUcxComm_t *comm) {
  flagcxUcxRequest_t *req = comm->free_req;

  if (req == NULL) {
    WARN("NET/UCX: unable to allocate FlagCX request");
    return NULL;
  }

  comm->free_req = req->next;
  req->worker  = comm->ucx_worker->worker;
  req->pending = 0;
  req->count   = 0;
  return req;
}

static void flagcxUcxRequestRelease(flagcxUcxRequest_t *req) {
    req->next = req->comm->free_req;
    req->comm->free_req = req;
}

static void flagcxUcxRequestAdd(flagcxUcxRequest_t *req, int size) {
  req->size[req->count] = size;
  req->pending++;
  req->count++;
}

static flagcxResult_t flagcxUcxSendCheck(flagcxUcxComm_t *comm) {
  ucp_request_param_t params;
  ucp_tag_message_h   msg_tag;
  ucp_tag_recv_info_t info_tag;
  ucp_ep_params_t     ep_params;
  void                *ucp_req;
  ucs_status_t        status;

  ucp_worker_progress(comm->ucx_worker->worker);

  if (comm->connect_req != NULL) {
    goto out_check_status;
  }

  msg_tag = ucp_tag_probe_nb(comm->ucx_worker->worker, comm->ctag, tagMask, 1,
                             &info_tag);
  if (msg_tag == NULL) {
    return flagcxSuccess;
  }

  comm->msg = (flagcxUcxConnectMsg_t*)malloc(info_tag.length);
  if (comm->msg == NULL) {
    return flagcxSystemError;
  }

  params.op_attr_mask = 0;
  ucp_req = ucp_tag_msg_recv_nbx(comm->ucx_worker->worker, comm->msg,
                                 info_tag.length, msg_tag, &params);
  if (UCS_PTR_IS_ERR(ucp_req)) {
    WARN("Unable to receive connect msg (%s)",
         ucs_status_string(UCS_PTR_STATUS(ucp_req)));
    free(comm->msg);
    comm->msg = NULL;
    return flagcxSystemError;
  } else if (ucp_req == NULL) {
    goto out_set_ready;
  }

  assert(comm->connect_req == NULL);
  comm->connect_req = ucp_req;

out_check_status:
  status = ucp_request_check_status(comm->connect_req);
  if (status == UCS_INPROGRESS) {
    return flagcxSuccess;
  }

  if (status != UCS_OK) {
    free(comm->msg);
    comm->msg = NULL;
    WARN("Send check requested returned error (%s)", ucs_status_string(status));
    return flagcxSystemError;
  }

  ucp_request_free(comm->connect_req);
  comm->connect_req = NULL;

out_set_ready:
  ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
  ep_params.address    = (ucp_address_t*)(comm->msg + 1);
  UCXCHECK(ucp_ep_create(comm->ucx_worker->worker, &ep_params, &comm->ep));
  comm->ready = 1;
  free(comm->msg);
  comm->msg = NULL;

  return flagcxSuccess;
}

static void flagcxUcxRecvSetReady(flagcxUcxComm_t *comm) {
  free(comm->msg);
  comm->msg   = NULL;
  comm->ready = 1;
}

static void check_handler(void *request, ucs_status_t status, void *user_data) {
  assert(status == UCS_OK);
  flagcxUcxRecvSetReady((flagcxUcxComm_t*)user_data);
  ucp_request_free(request);
}

flagcxResult_t flagcxUcxRecvCheck(flagcxUcxComm_t *comm) {
  ucp_request_param_t params;
  ucp_address_t       *my_addr;
  size_t              local_addr_len;
  size_t              msg_len;

  if (comm->connect_req != NULL) {
    goto done;
  }

  FLAGCXCHECK(flagcxUcxWorkerGetNetaddress(comm->ucx_worker->worker, &my_addr,
                                      &local_addr_len));
  flagcxUcxAddEp(comm->ucx_worker, &comm->sock);

  msg_len             = sizeof(flagcxUcxConnectMsg_t) + local_addr_len;
  comm->msg           = (flagcxUcxConnectMsg_t*)calloc(1, msg_len);
  comm->msg->addr_len = local_addr_len;
  memcpy(comm->msg + 1, my_addr, local_addr_len);

  params.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK |
                        UCP_OP_ATTR_FIELD_USER_DATA;
  params.cb.send      = check_handler;
  params.user_data    = comm;

  assert(comm->connect_req == NULL);
  comm->connect_req   = ucp_tag_send_nbx(comm->ep, comm->msg, msg_len,
                                         comm->ctag, &params);
  if (UCS_PTR_IS_ERR(comm->connect_req)) {
    WARN("Unable to send connect message");
    free(comm->msg);
    return flagcxSystemError;
  } else if (comm->connect_req == NULL) {
    flagcxUcxRecvSetReady(comm);
    return flagcxSuccess;
  }

done:
  ucp_worker_progress(comm->ucx_worker->worker);
  return flagcxSuccess;
}

static ucp_tag_t flagcxUcxUcpTag(ucp_tag_t comm_tag, uint64_t tag)
{
  assert(tag <= UINT32_MAX);
  assert(comm_tag <= UINT32_MAX);
  return comm_tag + (tag << 32);
}

flagcxResult_t flagcxUcxIsend(void *send_comm, void *data, size_t size,
                                   int tag, void *mhandle, void* phandle, void **request)
{
  flagcxUcxComm_t         *comm = (flagcxUcxComm_t *)send_comm;
  flagcxUcxMhandle_t      *mh   = (flagcxUcxMhandle_t*)mhandle;
  flagcxUcxRequest_t      *req;
  void               *ucp_req;
  ucp_request_param_t params;

  if (comm->ready == 0) {
    FLAGCXCHECK(flagcxUcxSendCheck(comm));
    if (comm->ready == 0) {
      *request = NULL;
      return flagcxSuccess;
    }
  }

  req = flagcxUcxRequestGet(comm);
  if (req == NULL) {
    return flagcxInternalError;
  }

  flagcxUcxRequestAdd(req, size);

  params.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK |
                        UCP_OP_ATTR_FIELD_USER_DATA;
  params.cb.send      = send_handler_nbx;
  params.user_data    = &req->pending;
  if (mh) {
    params.op_attr_mask |= UCP_OP_ATTR_FIELD_MEMH;
    params.memh          = mh->ucp_memh;
  }


  ucp_req = ucp_tag_send_nbx(comm->ep, data, size,
                             flagcxUcxUcpTag(comm->tag, tag), &params);
  if (UCS_PTR_IS_ERR(ucp_req)) {
    WARN("ucx_isend: unable to send message (%s)",
         ucs_status_string(UCS_PTR_STATUS(ucp_req)));
    return flagcxSystemError;
  } else if (ucp_req == NULL) {
    req->pending--;
  }

  *request = req;
  return flagcxSuccess;
}

flagcxResult_t flagcxUcxIrecv(void *recv_comm, int n, void **data,
                                   size_t *sizes, int *tags, void **mhandle,
                                   void** phandles, void **request)
{
  flagcxUcxComm_t         *comm = (flagcxUcxComm_t*)recv_comm;
  flagcxUcxMhandle_t      **mh  = (flagcxUcxMhandle_t**)mhandle;
  void               *ucp_req;
  flagcxUcxRequest_t      *req;
  ucp_request_param_t params;

  if (comm->ready == 0) {
    FLAGCXCHECK(flagcxUcxRecvCheck(comm));
    if (comm->ready == 0) {
      *request = NULL;
      return flagcxSuccess;
    }
  }

  if (n > FLAGCX_NET_IB_MAX_RECVS) {
    WARN("ucx_irecv: posting %d but max is %d", n, FLAGCX_NET_IB_MAX_RECVS);
    return flagcxInternalError;
  }

  req = flagcxUcxRequestGet(comm);
  if (req == NULL) {
    return flagcxInternalError;
  }

  params.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK |
                        UCP_OP_ATTR_FIELD_USER_DATA;
  params.cb.recv      = recv_handler_nbx;
  params.user_data    = &req->pending;

  for (int i = 0; i < n; i++) {
    flagcxUcxRequestAdd(req, sizes[i]);

    if (mh[i]) {
      params.op_attr_mask |= UCP_OP_ATTR_FIELD_MEMH;
      params.memh          = mh[i]->ucp_memh;
    } else {
      params.op_attr_mask &= ~UCP_OP_ATTR_FIELD_MEMH;
    }

    ucp_req = ucp_tag_recv_nbx(comm->ucx_worker->worker, data[i], sizes[i],
                               flagcxUcxUcpTag(comm->tag, tags[i]), tagMask,
                               &params);
    if (UCS_PTR_IS_ERR(ucp_req)) {
      WARN("ucx_irecv: unable to post receive %d/%d (%s)", i, n,
           ucs_status_string(UCS_PTR_STATUS(ucp_req)));
      return flagcxSystemError;
    } else if (ucp_req == NULL) {
      req->pending--;
    }
  }

  *request = req;
  return flagcxSuccess;
}

flagcxResult_t flagcxUcxIflush(void *recv_comm, int n, void **data, int *sizes,
                             void **mhandle, void **request) {
  int last            = -1;
  int size            = 1;
  flagcxUcxComm_t    *comm = (flagcxUcxComm_t *)recv_comm;
  flagcxUcxMhandle_t **mh  = (flagcxUcxMhandle_t**)mhandle;
  flagcxUcxRequest_t *req;
  void *ucp_req;
  ucp_request_param_t params;

  *request = NULL;
  for (int i=0; i<n; i++) if (sizes[i]) last = i;
  if (comm->gpuFlush.enabled == 0 || last == -1) return flagcxSuccess;

  req = flagcxUcxRequestGet(comm);
  if (req == NULL) {
    return flagcxInternalError;
  }

  flagcxUcxRequestAdd(req, size);

  params.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK |
                        UCP_OP_ATTR_FIELD_USER_DATA;
  params.cb.send      = send_handler_nbx;
  params.user_data    = &req->pending;
  ucp_req = ucp_get_nbx(comm->gpuFlush.flush_ep, &comm->gpuFlush.hostMem, size,
                        (uint64_t)data[last], mh[last]->rkey, &params);
  if (UCS_PTR_IS_ERR(ucp_req)) {
    WARN("ucx_iflush: unable to read data (%s)",
         ucs_status_string(UCS_PTR_STATUS(ucp_req)));
    return flagcxSystemError;
  } else if (ucp_req == NULL) {
    req->pending--;
  }

  *request = req;
  return flagcxSuccess;
}

flagcxResult_t flagcxUcxTest(void *request, int *done, int *size) {
  flagcxUcxRequest_t *req = (flagcxUcxRequest_t*)request;
  unsigned p;

  while (req->pending > 0) {
    p = ucp_worker_progress(req->worker);
    if (!p) {
      *done = 0;
      return flagcxSuccess;
    }
  }

  *done = 1;
  if (size != NULL) {
    /* Posted receives have completed */
    memcpy(size, req->size, sizeof(*size) * req->count);
  }

  flagcxUcxRequestRelease(req);
  return flagcxSuccess;
}

static void wait_close(ucp_worker_h worker, void *ucp_req) {
  ucs_status_t status;

  if (UCS_PTR_IS_PTR(ucp_req)) {
    do {
      ucp_worker_progress(worker);
      status = ucp_request_check_status(ucp_req);
    } while(status == UCS_INPROGRESS);
    ucp_request_free(ucp_req);
  } else if (ucp_req != NULL) {
    WARN("Failed to close UCX endpoint");
  }
}

flagcxResult_t flagcxUcxCloseSend(void *send_comm) {
  flagcxUcxComm_t *comm = (flagcxUcxComm_t*)send_comm;
  void *close_req;

  if (comm) {
    if (comm->ep) {
      close_req = ucp_ep_close_nb(comm->ep, UCP_EP_CLOSE_MODE_FLUSH);
      wait_close(comm->ucx_worker->worker, close_req);
      int close = 1;
      FLAGCXCHECK(flagcxSocketSend(&comm->sock, &close, sizeof(int)));
    }
    flagcxUcxFreeWorker(comm->ucx_worker);
    free(comm);
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxUcxCloseRecv(void *recv_comm) {
  flagcxUcxComm_t *comm = (flagcxUcxComm_t*)recv_comm;
  void *close_req;

  if (comm) {
    if (comm->gpuFlush.enabled) {
      close_req = ucp_ep_close_nb(comm->gpuFlush.flush_ep, UCP_EP_CLOSE_MODE_FLUSH);
      wait_close(comm->ucx_worker->worker, close_req);
    }
    if (comm->ep) {
      close_req = ucp_ep_close_nb(comm->ep, UCP_EP_CLOSE_MODE_FLUSH);
      wait_close(comm->ucx_worker->worker, close_req);
      int close=1;
      FLAGCXCHECK(flagcxSocketSend(&comm->sock, &close, sizeof(int)));
    }
    flagcxUcxFreeWorker(comm->ucx_worker);
    free(comm);
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxUcxCloseListen(void *listen_comm) {
  flagcxUcxListenComm_t *comm = (flagcxUcxListenComm_t *)listen_comm;

  if (comm) {
    close(comm->sock.fd);
    free(comm);
  }
  
  return flagcxSuccess;
}

flagcxResult_t flagcxUcxFinalize(void* ctx) {
  flagcxUcxRefCount--;
  return flagcxSuccess;
}

// Additional functions needed for the adaptor interface
flagcxResult_t flagcxUcxGetDevFromName(char *name, int *dev) {
  // Simple implementation - find device by name
  for (int i = 0; i < flagcxNIbDevs; i++) {
    if (strcmp(flagcxIbDevs[i].devName, name) == 0) {
      *dev = i;
      return flagcxSuccess;
    }
  }
  return flagcxSystemError;
}
// UCX network adaptor structure
struct flagcxNetAdaptor flagcxNetUcx = {
    // Basic functions
    "UCX", flagcxUcxInit, flagcxUcxDevices, flagcxUcxGetProperties,
    NULL, // reduceSupport
    NULL, // getDeviceMr
    NULL, // irecvConsumed

    // Setup functions
    flagcxUcxListen, // listen
    flagcxUcxConnect, // connect
    flagcxUcxAccept, // accept
    flagcxUcxCloseSend, // closeSend
    flagcxUcxCloseRecv, // closeRecv
    flagcxUcxCloseListen, // closeListen

    // Memory region functions
    flagcxUcxRegMr, // regMr
    flagcxUcxRegMrDmaBuf, // regMrDmaBuf
    flagcxUcxDeregMr, // deregMr

    // Two-sided functions
    flagcxUcxIsend, // isend
    flagcxUcxIrecv, // irecv
    flagcxUcxIflush, // iflush
    flagcxUcxTest, // test

    // One-sided functions
    NULL, // write - TODO: Implement
    NULL, // read - TODO: Implement
    NULL, // signal - TODO: Implement

    // Device name lookup
    flagcxUcxGetDevFromName  // getDevFromName
};

#endif // USE_UCX