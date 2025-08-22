/*************************************************************************
 * SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES
 * Copyright (c) 2016-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE.txt for license information
 ************************************************************************/
// #include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>

#include "debug.h"
#include "p2p_plugin.h"
#include "utils.h"

#ifdef HAVE_UCX_PLUGIN
extern flagcxNet_v8_t ucxUctPlugin_v8;
extern flagcxNet_v7_t ucxUctPlugin_v7;
extern flagcxNet_v6_t ucxUctPlugin_v6;
extern flagcxNet_v5_t ucxUctPlugin_v5;
#endif

#ifdef HAVE_IB_PLUGIN
extern flagcxNet_v8_t ibPlugin_v8;
extern flagcxNet_v7_t ibPlugin_v7;
extern flagcxNet_v6_t ibPlugin_v6;
extern flagcxNet_v5_t ibPlugin_v5;
#endif
pthread_mutex_t flagcx_p2p_lock = PTHREAD_MUTEX_INITIALIZER;

flagcxDebugLogger_t pluginLogFunction;


const char* ibProviderName[] = {
  "None",
  "Mlx5",
};

#ifdef HAVE_SHARP_PLUGIN
extern int flagcxNSharpDevs;
#else
/* In case sharp plugin is not there just define this variable locally to make code cleaner */
int flagcxNSharpDevs;
#endif
#ifdef HAVE_IB_PLUGIN
extern int flagcxIbRelaxedOrderingEnabled;
#endif
FLAGCX_PARAM(SharpMaxComms, "SHARP_MAX_COMMS", 1);

// External function declarations for functions defined in net_ib.cc
extern int flagcxParamIbAdaptiveRouting();

FLAGCX_PARAM(IbDataDirect,"IB_DATA_DIRECT", 1);

flagcxResult_t pluginInit_v8(flagcxDebugLogger_t logFunction);
flagcxResult_t pluginInit_v7(flagcxDebugLogger_t logFunction);
flagcxResult_t pluginInit_v6(flagcxDebugLogger_t logFunction);
flagcxResult_t pluginInit_v5(flagcxDebugLogger_t logFunction);

flagcxNet_v8_t flagcxNetPlugin_v8 = {
  "FLAGCX RDMA Plugin v8",
  pluginInit_v8,
};

flagcxNet_v7_t flagcxNetPlugin_v7 = {
  "FLAGCX RDMA Plugin v7",
  pluginInit_v7,
};

flagcxNet_v6_t flagcxNetPlugin_v6 = {
  "FLAGCX RDMA Plugin v6",
  pluginInit_v6,
};


flagcxNet_v5_t flagcxNetPlugin_v5 = {
  "FLAGCX RDMA Plugin v5",
  pluginInit_v5,
};


static flagcx_p2p_plugin_t p2p_plugin = FLAGCX_P2P_LAST;

static int flagcx_p2p_is_uct_plugin(flagcx_p2p_plugin_t plugin) {
  return (plugin == FLAGCX_P2P_UCX_UCT);
}

static void pluginSetup()
{
  p2p_plugin = FLAGCX_P2P_IB;
  const char *plugin_path = get_plugin_lib_path();
  if (plugin_path != NULL) {
    INFO(FLAGCX_INIT|FLAGCX_NET, "Plugin Path : %s", plugin_path);;
  }

  const char *p2p_layer = getenv("FLAGCX_PLUGIN_P2P");
  if (p2p_layer != NULL) {
    if (!strcasecmp(p2p_layer, "ib")) p2p_plugin = FLAGCX_P2P_IB;
#ifdef HAVE_UCX_PLUGIN
    else if (!strcasecmp(p2p_layer, "ucx_uct")) p2p_plugin = FLAGCX_P2P_UCX_UCT;
#endif
    else {
      WARN("Invalid value %s for FLAGCX_PLUGIN_P2P, using default", p2p_layer);
    }
  }
  switch (p2p_plugin) {
#ifdef HAVE_UCX_PLUGIN
    case FLAGCX_P2P_UCX_UCT:
      flagcxNetPlugin_v8 = ucxUctPlugin_v8;
      flagcxNetPlugin_v7 = ucxUctPlugin_v7;
      flagcxNetPlugin_v6 = ucxUctPlugin_v6;
      flagcxNetPlugin_v5 = ucxUctPlugin_v5;
      break;
#endif
#ifdef HAVE_IB_PLUGIN
    default:
      flagcxNetPlugin_v8 = ibPlugin_v8;
      flagcxNetPlugin_v7 = ibPlugin_v7;
      flagcxNetPlugin_v6 = ibPlugin_v6;
      flagcxNetPlugin_v5 = ibPlugin_v5;
      break;
#endif
  }

}

flagcxResult_t pluginInit_v8(flagcxDebugLogger_t logFunction) {
  pluginLogFunction = logFunction;
  pluginSetup();
  INFO(FLAGCX_INIT|FLAGCX_NET, "P2P plugin v8 %s", flagcxNetPlugin_v8.name);
  return flagcxNetPlugin_v8.init(logFunction);
}

flagcxResult_t pluginInit_v7(flagcxDebugLogger_t logFunction) {
  pluginLogFunction = logFunction;
  pluginSetup();
  INFO(FLAGCX_INIT|FLAGCX_NET, "P2P plugin %s", flagcxNetPlugin_v7.name);
  return flagcxNetPlugin_v7.init(logFunction);
}

flagcxResult_t pluginInit_v6(flagcxDebugLogger_t logFunction) {
  pluginLogFunction = logFunction;
  pluginSetup();
  INFO(FLAGCX_INIT|FLAGCX_NET, "P2P plugin %s", flagcxNetPlugin_v6.name);
  return flagcxNetPlugin_v6.init(logFunction);
}

flagcxResult_t pluginInit_v5(flagcxDebugLogger_t logFunction) {
  pluginLogFunction = logFunction;
  pluginSetup();
  INFO(FLAGCX_INIT|FLAGCX_NET, "P2P plugin %s", flagcxNetPlugin_v5.name);
  return flagcxNetPlugin_v5.init(logFunction);
}


#define KNL_MODULE_LOADED(a) ((access(a, F_OK) == -1) ? 0 : 1)
static int flagcxIbGdrModuleLoaded = 0; // 1 = true, 0 = false
static void ibGdrSupportInitOnce() {
  flagcxIbGdrModuleLoaded = KNL_MODULE_LOADED("/sys/kernel/mm/memory_peers/nv_mem/version") ||
                          KNL_MODULE_LOADED("/sys/kernel/mm/memory_peers/nv_mem_nc/version") ||
                          KNL_MODULE_LOADED("/sys/module/nvidia_peermem/version");
}

flagcxResult_t flagcx_p2p_gdr_support()
{
  static pthread_once_t once = PTHREAD_ONCE_INIT;
  pthread_once(&once, ibGdrSupportInitOnce);
  if (!flagcxIbGdrModuleLoaded)
    return flagcxSystemError;
  return flagcxSuccess;
}

static __thread int ibDmaSupportInitDev; // which device to init, must be thread local
static void ibDmaBufSupportInitOnce(){
  flagcxResult_t res;
  int dev_fail = 0;

  // This is a physical device, not a virtual one, so select from ibDevs
  flagcxIbMergedDev* mergedDev = flagcxIbMergedDevs + ibDmaSupportInitDev;
  flagcxIbDev* ibDev = flagcxIbDevs + mergedDev->vProps.devs[0];
  struct ibv_pd* pd;
  struct ibv_context* ctx = ibDev->context;
  FLAGCXCHECKGOTO(wrap_ibv_alloc_pd(&pd, ctx), res, failure);
  // Test kernel DMA-BUF support with a dummy call (fd=-1)
  (void)wrap_direct_ibv_reg_dmabuf_mr(pd, 0ULL /*offset*/, 0ULL /*len*/, 0ULL /*iova*/, -1 /*fd*/, 0 /*flags*/);
  // ibv_reg_dmabuf_mr() will fail with EOPNOTSUPP/EPROTONOSUPPORT if not supported (EBADF otherwise)
  dev_fail |= (errno == EOPNOTSUPP) || (errno == EPROTONOSUPPORT);
  FLAGCXCHECKGOTO(wrap_ibv_dealloc_pd(pd), res, failure);
  // stop the search and goto failure
  if (dev_fail) goto failure;
  ibDev->dmaBufSupported = 1;
  return;
failure:
  ibDev->dmaBufSupported = -1;
  return;
}


struct oncewrap {
  pthread_once_t once;
};
static struct oncewrap onces[MAX_IB_DEVS];
// Detect whether DMA-BUF support is present in the kernel
// Returns :
// flagcxSuccess : DMA-BUF support is available
// flagcxSystemError : DMA-BUF is not supported by the kernel
flagcxResult_t flagcx_p2p_dmabuf_support(int dev) {
  // init the device only once
  ibDmaSupportInitDev = dev;
  pthread_once(&onces[dev].once, ibDmaBufSupportInitOnce);
  flagcxIbMergedDev* mergedDev = flagcxIbMergedDevs + ibDmaSupportInitDev;
  flagcxIbDev* ibDev = flagcxIbDevs + mergedDev->vProps.devs[0];
  int dmaBufSupported = ibDev->dmaBufSupported;
  if (dmaBufSupported == 1) return flagcxSuccess;
  return flagcxSystemError;
}

flagcxResult_t flagcxIbGetPhysProperties(int dev, flagcxUcxProperties_t* props) {
  struct flagcxIbDev* ibDev = flagcxIbDevs + dev;
  pthread_mutex_lock(&ibDev->lock);
  props->name = ibDev->devName;
  props->speed = ibDev->speed;
  props->pciPath = ibDev->pciPath;
  props->guid = ibDev->guid;
  props->ptrSupport   = FLAGCX_PTR_HOST;
  if (flagcx_p2p_gdr_support() == flagcxSuccess) {
    props->ptrSupport |= FLAGCX_ALLOC; // GDR support via nv_peermem
    INFO(FLAGCX_NET,"NET/IB : GPU Direct RDMA (nvidia-peermem) enabled for HCA %d '%s", dev, ibDev->devName);
  }
  props->regIsGlobal = 1;
  props->forceFlush = 0;
  if (ibDev->capsProvider.mlx5.dataDirect) {
    props->forceFlush = 1;
  }
  if ((flagcx_p2p_is_uct_plugin(p2p_plugin) || (p2p_plugin == FLAGCX_P2P_IB)) &&
      flagcx_p2p_dmabuf_support(dev) == flagcxSuccess) {
    props->ptrSupport |= FLAGCX_PTR_DMABUF; // GDR support via DMA-BUF
    INFO(FLAGCX_NET,"NET/IB : GPU Direct RDMA (DMABUF) enabled for HCA %d '%s", dev, ibDev->devName);
  }

  props->latency      = 0; // Not set
  props->port = ibDev->portNum + ibDev->realPort;
  props->maxComms = ibDev->maxQp;

  if (p2p_plugin == FLAGCX_P2P_IB ||
      flagcx_p2p_is_uct_plugin(p2p_plugin)) {
    props->maxRecvs = FLAGCX_NET_IB_MAX_RECVS;
  } else {
    props->maxRecvs = 1;
  }
  props->netDeviceType = FLAGCX_NET_DEVICE_HOST;
  props->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  props->maxP2pBytes = FLAGCX_MAX_NET_SIZE_BYTES;
  pthread_mutex_unlock(&ibDev->lock);
  return flagcxSuccess;
}

flagcxResult_t flagcx_p2p_ib_get_properties(flagcxIbDev *devs, int flagcxNMergedIbDevs, int dev, flagcxUcxProperties_t* props)
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
  memcpy(&props->vProps, &mergedDev->vProps, sizeof(flagcxNetVDeviceProps_t));
  return flagcxSuccess;
}

flagcxResult_t flagcxIbStatsInit(struct flagcxIbStats* stat) {
  __atomic_store_n(&stat->fatalErrorCount, 0, __ATOMIC_RELAXED);
  return flagcxSuccess;
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

static void* flagcxIbAsyncThreadMain(void* args) {
  struct flagcxIbDev* dev = (struct flagcxIbDev*)args;
  while (1) {
    struct ibv_async_event event;
    if (flagcxSuccess != wrap_ibv_get_async_event(dev->context, &event)) { break; }
    char *str;
    struct ibv_cq* cq = event.element.cq;    // only valid if CQ error
    struct ibv_qp* qp = event.element.qp;    // only valid if QP error
    struct ibv_srq* srq = event.element.srq; // only valid if SRQ error
    if (flagcxSuccess != wrap_ibv_event_type_str(&str, event.event_type)) { break; }
    switch (event.event_type) {
    case IBV_EVENT_DEVICE_FATAL:
      // the above is device fatal error
      WARN("NET/IB : %s:%d async fatal event: %s", dev->devName, dev->portNum, str);
      flagcxIbDevFatalError(dev);
      break;
    case IBV_EVENT_CQ_ERR:
      // the above is a CQ fatal error
      WARN("NET/IB : %s:%d async fatal event on CQ (%p): %s", dev->devName, dev->portNum, cq, str);
      flagcxIbCqFatalError(cq);
      break;
    case IBV_EVENT_QP_FATAL:
    case IBV_EVENT_QP_REQ_ERR:
    case IBV_EVENT_QP_ACCESS_ERR:
      // the above are QP fatal errors
      WARN("NET/IB : %s:%d async fatal event on QP (%p): %s", dev->devName, dev->portNum, qp, str);
      flagcxIbQpFatalError(qp);
      break;
    case IBV_EVENT_SRQ_ERR:
      // SRQ are not used in flagcx
      WARN("NET/IB : %s:%d async fatal event on SRQ, unused for now (%p): %s", dev->devName, dev->portNum, srq, str);
      break;
    case IBV_EVENT_PATH_MIG_ERR:
    case IBV_EVENT_PORT_ERR:
    case IBV_EVENT_PATH_MIG:
    case IBV_EVENT_PORT_ACTIVE:
    case IBV_EVENT_SQ_DRAINED:
    case IBV_EVENT_LID_CHANGE:
    case IBV_EVENT_PKEY_CHANGE:
    case IBV_EVENT_SM_CHANGE:
    case IBV_EVENT_QP_LAST_WQE_REACHED:
    case IBV_EVENT_CLIENT_REREGISTER:
    case IBV_EVENT_SRQ_LIMIT_REACHED:
      // the above are non-fatal
      WARN("NET/IB : %s:%d Got async error event: %s", dev->devName, dev->portNum, str);
      break;
    case IBV_EVENT_COMM_EST:
      break;
    default:
      WARN("NET/IB : %s:%d unknown event type (%d)", dev->devName, dev->portNum, event.event_type);
      break;
    }
    // acknowledgment needs to happen last to avoid user-after-free
    if (flagcxSuccess != wrap_ibv_ack_async_event(&event)) { break; }
  }
  return NULL;
}

int devSharpCompare(const void *a, const void *b)
{
  const struct flagcxIbDev *d1 = (const struct flagcxIbDev *)a;
  const struct flagcxIbDev *d2 = (const struct flagcxIbDev *)b;

  if (d1->isSharpDev == d2->isSharpDev) { return 0; }
  else if (d1->isSharpDev > d2->isSharpDev) { return -1; }
  else { return 1; }
}


static bool flagcxMlx5dvDmaBufCapable(struct ibv_context *context){
  flagcxResult_t res;
  int dev_fail = 0;

  struct ibv_pd* pd;
  FLAGCXCHECKGOTO(wrap_ibv_alloc_pd(&pd, context), res, failure);
  // Test kernel DMA-BUF support with a dummy call (fd=-1)
  (void)wrap_direct_ibv_reg_dmabuf_mr(pd, 0ULL /*offset*/, 0ULL /*len*/, 0ULL /*iova*/, -1 /*fd*/, 0 /*flags*/);  
  dev_fail |= (errno == EOPNOTSUPP) || (errno == EPROTONOSUPPORT);
  FLAGCXCHECKGOTO(wrap_ibv_dealloc_pd(pd), res, failure);
  // stop the search and goto failure
  if (dev_fail) goto failure;
  return true;
failure:
  return false;
}

flagcxResult_t flagcxIbMakeVDeviceInternal(int* d, flagcxNetVDeviceProps_t* props, int flagcxNIbDevs, int *flagcxNMergedIbDevs) {
  if ((flagcxParamIbMergeNics() == 0) && props->ndevs > 1) {
    INFO(FLAGCX_NET, "NET/IB : Skipping makeVDevice, flagcx_IB_MERGE_NICS=0");
    return flagcxInvalidUsage;
  }

  if (props->ndevs == 0) {
   WARN("NET/IB : Can't make virtual NIC with 0 devices");
   return flagcxInvalidUsage;
  }

  if (*flagcxNMergedIbDevs == MAX_IB_VDEVS) {
    WARN("NET/IB : Cannot allocate any more virtual devices (%d)", MAX_IB_VDEVS);
    return flagcxInvalidUsage;
  }

  // Always count up number of merged devices
  flagcxIbMergedDev* mDev = flagcxIbMergedDevs + *flagcxNMergedIbDevs;
  mDev->vProps.ndevs = 0;
  mDev->speed = 0;

  for (int i = 0; i < props->ndevs; i++) {
    flagcxIbDev* dev = flagcxIbDevs + props->devs[i];
    if (mDev->vProps.ndevs == FLAGCX_IB_MAX_DEVS_PER_NIC) return flagcxInvalidUsage;
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
        props->devs[0], dev0->devName, dev0->portNum, FLAGCX_IB_LLSTR(dev0->link), props->devs[i], dev->devName, dev->portNum, FLAGCX_IB_LLSTR(dev->link)); 
      return flagcxInvalidUsage;
    }
  }

  *d = *flagcxNMergedIbDevs;
  (*flagcxNMergedIbDevs)++;

  INFO(FLAGCX_NET, "NET/IB : Made virtual device [%d] name=%s speed=%d ndevs=%d", *d, mDev->devName, mDev->speed, mDev->vProps.ndevs);
  return flagcxSuccess;
}

flagcxResult_t flagcx_p2p_ib_init(int *nDevs, int *nmDevs, flagcxIbDev *flagcxIbDevs, char *flagcxIbIfName, union flagcxSocketAddress *flagcxIbIfAddr, pthread_t *flagcxIbAsyncThread, flagcxDebugLogger_t logFunction)
{
  flagcxResult_t ret = flagcxSuccess;
  int flagcxNIbDevs = *nDevs;
  int flagcxNMergedIbDevs = *nmDevs;
  pluginLogFunction = logFunction;
  if (flagcxNIbDevs == -1) {
    for (int i=0; i< MAX_IB_DEVS; i++)
      onces[i].once = PTHREAD_ONCE_INIT;
    pthread_mutex_lock(&flagcx_p2p_lock);
    wrap_ibv_fork_init();
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

      if (flagcxSuccess != wrap_ibv_get_device_list(&devices, &nIbDevs)) { ret = flagcxInternalError; goto fail; }
      for (int d=0; d<nIbDevs && flagcxNIbDevs<MAX_IB_DEVS; d++) {
        struct ibv_context * context;
        if (flagcxSuccess != wrap_ibv_open_device(&context, devices[d]) || context == NULL) {
          WARN("NET/IB : Unable to open device %s", devices[d]->name);
          continue;
        }
        enum flagcxIbProvider ibProvider = IB_PROVIDER_NONE;
        char dataDirectDevicePath[PATH_MAX];
        int dataDirectSupported = 0;
        int skipNetDevForDataDirect = 0;
        int nPorts = 0;
        struct ibv_device_attr devAttr;
        if (flagcxSuccess != wrap_ibv_query_device(context, &devAttr)) {
          WARN("NET/IB : Unable to query device %s", devices[d]->name);
          if (flagcxSuccess != wrap_ibv_close_device(context)) { ret = flagcxInternalError; goto fail; }
          continue;
        }
        for (int port_num = 1; port_num <= devAttr.phys_port_cnt; port_num++) {
          for (int dataDirect = skipNetDevForDataDirect; dataDirect < 1 + dataDirectSupported; ++dataDirect) {
            struct ibv_port_attr portAttr;
            uint32_t portSpeed;
            if (flagcxSuccess != wrap_ibv_query_port(context, port_num, &portAttr)) {
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
              snprintf(flagcxIbDevs[flagcxNIbDevs].devName, MAXNAMESIZE, "%s_dma", devices[d]->name);
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
        if (nPorts == 0 && flagcxSuccess != wrap_ibv_close_device(context))  { ret = flagcxInternalError; goto fail; }
      }
      
      if (nIbDevs && (flagcxSuccess != wrap_ibv_free_device_list(devices))) { ret = flagcxInternalError; goto fail; };
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

// Returns 0 if this is the path of two VFs of the same physical device
static int flagcxIbMatchVfPath(char* path1, char* path2) {
  // Merge multi-port NICs into the same PCI device
  if (flagcxParamIbMergeVfs()) {
    return strncmp(path1, path2, strlen(path1)-4) == 0;
  } else {
    return strncmp(path1, path2, strlen(path1)-1) == 0;
  }
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
  }
  *path = p;
  return flagcxSuccess;
}

static int ibv_widths[] = { 1, 4, 8, 12, 2};
static int ibv_speeds[] = {
  2500,  /* SDR */
  5000,  /* DDR */
  10000, /* QDR */
  10000, /* QDR */
  14000, /* FDR */
  25000, /* EDR */
  50000, /* HDR */
  100000, /* NDR */
  200000  /* XDR */
};


static int first_bit_set(int val, int max) {
  int i = 0;
  while (i<max && ((val & (1<<i)) == 0)) i++;
  return i;
}

int flagcx_p2p_ib_width(int width)
{
  return ibv_widths[first_bit_set(width, sizeof(ibv_widths)/sizeof(int)-1)];
}

int flagcx_p2p_ib_speed(int speed)
{
  return ibv_speeds[first_bit_set(speed, sizeof(ibv_speeds)/sizeof(int)-1)];
}

flagcx_p2p_plugin_t flagcx_p2p_get_plugin_type()
{
  return p2p_plugin;
}

extern struct flagcxIbDev flagcxIbDevs[MAX_IB_DEVS];
extern struct flagcxIbDev userIbDevs[MAX_IB_DEVS];
extern struct flagcxIbMergedDev flagcxIbMergedDevs[MAX_IB_VDEVS];
