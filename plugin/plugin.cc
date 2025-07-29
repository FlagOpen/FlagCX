/*************************************************************************
 * Copyright (c) 2015-2019, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "net.h"

#define __hidden __attribute__ ((visibility("hidden")))

int max_requests = flagcx_NET_MAX_REQUESTS;

__hidden flagcxResult_t pluginInit(flagcxDebugLogger_t logFunction) { return flagcxSuccess; }
__hidden flagcxResult_t pluginDevices(int* ndev) { *ndev = 0; return flagcxSuccess; }

__hidden flagcxResult_t pluginPciPath(int dev, char** path) { return flagcxInternalError; }
__hidden flagcxResult_t pluginPtrSupport(int dev, int* supportedTypes) { return flagcxInternalError; }
__hidden flagcxResult_t pluginGetProperties(int dev, flagcxNetProperties_v8_t* props) {
  // Below are default values, if unsure don't change.

  props->name = "Example";
  // Fill for proper topology detection, e.g. /sys/devices/pci0000:00/0000:00:10.0/0000:0b:00.0
  props->pciPath = NULL;
  // Only used to detect NICs with multiple PCI attachments.
  props->guid = 0;
  // Add flagcx_PTR_CUDA if GPU Direct RDMA is supported and regMr can take CUDA pointers.
  props->ptrSupport = FLAGCX_PTR_HOST;
  // If you regMr has a fast registration cache, set to 1. If set to 0, user buffer registration may be disabled.
  props->regIsGlobal = 0;
  // Speed in *Mbps*. 100000 means 100G
  props->speed = 100000;
  // Port number, used in conjunction with guid
  props->port = 0;
  // Custom latency (used to help tuning if latency is high. If set to 0, use default flagcx values.
  props->latency = 0;
  // Maximum number of comm objects we can create.
  props->maxComms = 1024*1024;
  // Maximum number of receive operations taken by irecv().
  props->maxRecvs = 1;
  // Coupling with flagcx network device-side code.
  props->netDeviceType = 0;
  props->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  return flagcxInternalError;
}
__hidden flagcxResult_t pluginListen(int dev, void* handle, void** listenComm) { return flagcxInternalError; }
__hidden flagcxResult_t pluginConnect(int dev, void* handle, void** sendComm, flagcxNetDeviceHandle_v8_t** sendDevComm) { return flagcxInternalError; }
__hidden flagcxResult_t pluginAccept(void* listenComm, void** recvComm, flagcxNetDeviceHandle_v8_t** recvDevComm) { return flagcxInternalError; }
__hidden flagcxResult_t pluginRegMr(void* collComm, void* data, size_t size, int type, void** mhandle) { return flagcxInternalError; }
__hidden flagcxResult_t pluginRegMrDmaBuf(void* collComm, void* data, size_t size, int type, uint64_t offset, int fd, void** mhandle) { return flagcxInternalError; }
__hidden flagcxResult_t pluginDeregMr(void* collComm, void* mhandle) { return flagcxInternalError;}
__hidden flagcxResult_t pluginIsend(void* sendComm, void* data, int size, int tag, void* mhandle, void** request) { return flagcxInternalError; }
__hidden flagcxResult_t pluginIrecv(void* recvComm, int n, void** data, int* sizes, int* tags, void** mhandles, void** request) { return flagcxInternalError; }
__hidden flagcxResult_t pluginIflush(void* recvComm, int n, void** data, int* sizes, void** mhandles, void** request) { return flagcxInternalError; }
__hidden flagcxResult_t pluginTest(void* request, int* done, int* size) { return flagcxInternalError; }
__hidden flagcxResult_t pluginCloseSend(void* sendComm) { return flagcxInternalError; }
__hidden flagcxResult_t pluginCloseRecv(void* recvComm) { return flagcxInternalError; }
__hidden flagcxResult_t pluginCloseListen(void* listenComm) { return flagcxInternalError; }
__hidden flagcxResult_t pluginIrecvConsumed(void* recvComm, int n, void* request) { return flagcxInternalError; }
__hidden flagcxResult_t pluginGetDeviceMr(void* comm, void* mhandle, void** dptr_mhandle) { return flagcxInternalError; }

#define PLUGIN_NAME "Plugin"

const flagcxNet_v8_t flagcxNetPlugin_v8 = {
  .name = PLUGIN_NAME,
  .init = pluginInit,
  .devices = pluginDevices,
  .getProperties = pluginGetProperties,
  .listen = pluginListen,
  .connect = pluginConnect,
  .accept = pluginAccept,
  .regMr = pluginRegMr,
  .regMrDmaBuf = pluginRegMrDmaBuf,
  .deregMr = pluginDeregMr,
  .isend = pluginIsend,
  .irecv = pluginIrecv,
  .iflush = pluginIflush,
  .test = pluginTest,
  .closeSend = pluginCloseSend,
  .closeRecv = pluginCloseRecv,
  .closeListen = pluginCloseListen,
  .getDeviceMr = pluginGetDeviceMr,
  .irecvConsumed = pluginIrecvConsumed,
};

__hidden flagcxResult_t pluginGetProperties_v7(int dev, flagcxNetProperties_v7_t* props_v7) {
  flagcxNetProperties_t props;
  flagcxResult_t ret = pluginGetProperties(dev, &props);
  if (ret != flagcxSuccess) return ret;
  props_v7->name = props.name;
  props_v7->pciPath = props.pciPath;
  props_v7->guid = props.guid;
  props_v7->ptrSupport = props.ptrSupport;
  props_v7->speed = props.speed;
  props_v7->port = props.port;
  props_v7->maxComms = props.maxComms;
  props_v7->maxRecvs = props.maxRecvs;
  props_v7->netDeviceType = props.netDeviceType;
  props_v7->netDeviceVersion = props.netDeviceVersion;
  return flagcxSuccess;
}

__hidden flagcxResult_t pluginRegMr_v7(void* collComm, void* data, int size, int type, void** mhandle) {
  return pluginRegMr(collComm, data, size, type, mhandle);
}

const flagcxNet_v7_t flagcxNetPlugin_v7 = {
  .name = PLUGIN_NAME,
  .init = pluginInit,
  .devices = pluginDevices,
  .getProperties = pluginGetProperties_v7,
  .listen = pluginListen,
  .connect = pluginConnect,
  .accept = pluginAccept,
  .regMr = pluginRegMr_v7,
  .regMrDmaBuf = pluginRegMrDmaBuf,
  .deregMr = pluginDeregMr,
  .isend = pluginIsend,
  .irecv = pluginIrecv,
  .iflush = pluginIflush,
  .test = pluginTest,
  .closeSend = pluginCloseSend,
  .closeRecv = pluginCloseRecv,
  .closeListen = pluginCloseListen,
  .getDeviceMr = pluginGetDeviceMr,
  .irecvConsumed = pluginIrecvConsumed,
};

__hidden flagcxResult_t pluginGetProperties_v6(int dev, flagcxNetProperties_v6_t* props_v6) {
  flagcxNetProperties_t props;
  flagcxResult_t ret = pluginGetProperties(dev, &props);
  if (ret != flagcxSuccess) return ret;
  props_v6->name = props.name;
  props_v6->pciPath = props.pciPath;
  props_v6->guid = props.guid;
  props_v6->ptrSupport = props.ptrSupport;
  props_v6->speed = props.speed;
  props_v6->port = props.port;
  props_v6->maxComms = props.maxComms;
  props_v6->maxRecvs = props.maxRecvs;
  return flagcxSuccess;
}

__hidden flagcxResult_t pluginConnect_v6(int dev, void* handle, void** sendComm) { return flagcxInternalError; }
__hidden flagcxResult_t pluginAccept_v6(void* listenComm, void** recvComm) { return flagcxInternalError; }

const flagcxNet_v6_t flagcxNetPlugin_v6 = {
  .name = PLUGIN_NAME,
  .init = pluginInit,
  .devices = pluginDevices,
  .getProperties = pluginGetProperties_v6,
  .listen = pluginListen,
  .connect = pluginConnect_v6,
  .accept = pluginAccept_v6,
  .regMr = pluginRegMr_v7,
  .regMrDmaBuf = pluginRegMrDmaBuf,
  .deregMr = pluginDeregMr,
  .isend = pluginIsend,
  .irecv = pluginIrecv,
  .iflush = pluginIflush,
  .test = pluginTest,
  .closeSend = pluginCloseSend,
  .closeRecv = pluginCloseRecv,
  .closeListen = pluginCloseListen
};

/* v5 Compat */
const flagcxNet_v5_t flagcxNetPlugin_v5 = {
  .name = PLUGIN_NAME,
  .init = pluginInit,
  .devices = pluginDevices,
  .getProperties = pluginGetProperties_v6,
  .listen = pluginListen,
  .connect = pluginConnect_v6,
  .accept = pluginAccept_v6,
  .regMr = pluginRegMr_v7,
  .deregMr = pluginDeregMr,
  .isend = pluginIsend,
  .irecv = pluginIrecv,
  .iflush = pluginIflush,
  .test = pluginTest,
  .closeSend = pluginCloseSend,
  .closeRecv = pluginCloseRecv,
  .closeListen = pluginCloseListen,
};

/* v4 Compat */
static flagcxResult_t pluginGetProperties_v4(int dev, flagcxNetProperties_v4_t* props_v4) {
  flagcxNetProperties_t props;
  flagcxResult_t ret = pluginGetProperties(dev, &props);
  if (ret != flagcxSuccess) return ret;
  props_v4->name = props.name;
  props_v4->pciPath = props.pciPath;
  props_v4->guid = props.guid;
  props_v4->ptrSupport = props.ptrSupport;
  props_v4->speed = props.speed;
  props_v4->port = props.port;
  props_v4->maxComms = props.maxComms;
  return flagcxSuccess;
}
static flagcxResult_t pluginIsend_v4(void *sendComm, void* data, int size, void *mhandle, void** request) {
  return pluginIsend(sendComm, data, size, 0, mhandle, request);
}
static flagcxResult_t pluginIrecv_v4(void* recvComm, void* data, int size, void* mhandle, void** request) {
  int tag = 0;
  return pluginIrecv(recvComm, 1, &data, &size, &tag, &mhandle, request);
}
static flagcxResult_t pluginIflush_v4(void* recvComm, void* data, int size, void* mhandle, void** request) {
  return pluginIflush(recvComm, 1, &data, &size, &mhandle, request);
}
static flagcxResult_t pluginConnect_v4(int dev, void* handle, void** sendComm) {
  flagcxResult_t ret;
  do {
    flagcxNetDeviceHandle_v7_t* handle = NULL;
    ret = pluginConnect(dev, handle, sendComm, &handle);
  } while (ret == flagcxSuccess && *sendComm == NULL);
  return ret;
}
static flagcxResult_t pluginAccept_v4(void* listenComm, void** recvComm) {
  flagcxResult_t ret;
  do {
    flagcxNetDeviceHandle_v7_t* handle = NULL;
    ret = pluginAccept(listenComm, recvComm, &handle);
  } while (ret == flagcxSuccess && *recvComm == NULL);
  return ret;
}
const flagcxNet_v4_t flagcxNetPlugin_v4 = {
  .name = PLUGIN_NAME,
  .init = pluginInit,
  .devices = pluginDevices,
  .getProperties = pluginGetProperties_v4,
  .listen = pluginListen,
  .connect = pluginConnect_v4,
  .accept = pluginAccept_v4,
  .regMr = pluginRegMr_v7,
  .deregMr = pluginDeregMr,
  .isend = pluginIsend_v4,
  .irecv = pluginIrecv_v4,
  .iflush = pluginIflush_v4,
  .test = pluginTest,
  .closeSend = pluginCloseSend,
  .closeRecv = pluginCloseRecv,
  .closeListen = pluginCloseListen,
};

/* v3 Compat */
static flagcxResult_t pluginFlush(void* recvComm, void* data, int size, void* mhandle) {
  void* req;
  flagcxResult_t ret = pluginIflush_v4(recvComm, data, size, mhandle, &req);
  int done = 0;
  while (ret == flagcxSuccess && done == 0) {
    ret = pluginTest(req, &done, NULL);
  }
  return ret;
}
static flagcxResult_t pluginInit_v3(flagcxDebugLogger_t logFunction) {
  max_requests = flagcx_NET_MAX_REQUESTS_V3;
  return pluginInit(logFunction);
}
#include <string.h>
static flagcxResult_t pluginListen_v3(int dev, void* handle, void** listenComm) {
  char pluginHandle[flagcx_NET_HANDLE_MAXSIZE];
  flagcxResult_t ret = pluginListen(dev, &pluginHandle, listenComm);
  memcpy(handle, &pluginHandle, flagcx_NET_HANDLE_MAXSIZE_V4);
  return ret;
}
static flagcxResult_t pluginConnect_v3(int dev, void* handle, void** sendComm) {
  char pluginHandle[flagcx_NET_HANDLE_MAXSIZE];
  memcpy(&pluginHandle, handle, flagcx_NET_HANDLE_MAXSIZE_V4);
  return pluginConnect_v4(dev, &pluginHandle, sendComm);
}
const flagcxNet_v3_t flagcxNetPlugin_v3 = {
  .name = PLUGIN_NAME,
  .init = pluginInit_v3,
  .devices = pluginDevices,
  .getProperties = pluginGetProperties_v4,
  .listen = pluginListen_v3,
  .connect = pluginConnect_v3,
  .accept = pluginAccept_v4,
  .regMr = pluginRegMr_v7,
  .deregMr = pluginDeregMr,
  .isend = pluginIsend_v4,
  .irecv = pluginIrecv_v4,
  .flush = pluginFlush,
  .test = pluginTest,
  .closeSend = pluginCloseSend,
  .closeRecv = pluginCloseRecv,
  .closeListen = pluginCloseListen,
};

/* v2 Compat */
const flagcxNet_v2_t flagcxNetPlugin_v2 = {
  .name = PLUGIN_NAME,
  .init = pluginInit_v3,
  .devices = pluginDevices,
  .pciPath = pluginPciPath,
  .ptrSupport = pluginPtrSupport,
  .listen = pluginListen,
  .connect = pluginConnect_v4,
  .accept = pluginAccept_v4,
  .regMr = pluginRegMr_v7,
  .deregMr = pluginDeregMr,
  .isend = pluginIsend_v4,
  .irecv = pluginIrecv_v4,
  .flush = pluginFlush,
  .test = pluginTest,
  .closeSend = pluginCloseSend,
  .closeRecv = pluginCloseRecv,
  .closeListen = pluginCloseListen,
};


