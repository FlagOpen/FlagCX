#include "net.h"
#include "adaptor.h"
#include "device.h"
#include "proxy.h"


#include <string.h>
#include <errno.h>
#include <dlfcn.h>

static flagcxNet_v8_t flagcxNet_v5_as_v8;
static flagcxNet_v8_t flagcxNet_v6_as_v8;
static flagcxNet_v8_t flagcxNet_v7_as_v8;
static flagcxNet_v5_t *flagcxNet_v5;
static flagcxNet_v6_t *flagcxNet_v6;
static flagcxNet_v7_t *flagcxNet_v7;
static flagcxCollNet_v8_t flagcxCollNet_v5_as_v8;
static flagcxCollNet_v8_t flagcxCollNet_v6_as_v8;
static flagcxCollNet_v8_t flagcxCollNet_v7_as_v8;
static flagcxCollNet_v5_t *flagcxCollNet_v5;
static flagcxCollNet_v6_t *flagcxCollNet_v6;
static flagcxCollNet_v7_t *flagcxCollNet_v7;


static flagcxResult_t flagcxlNet_v7_as_v8_getProperties(int dev, flagcxNetProperties_v8_t* props) {
  flagcxNetProperties_v7_t p7;
  flagcxResult_t ans = flagcxNet_v7->getProperties(dev, &p7);
  if (ans != flagcxSuccess) return ans;
  props->name = p7.name;
  props->pciPath = p7.pciPath;
  props->guid = p7.guid;
  props->ptrSupport = p7.ptrSupport;
  props->regIsGlobal = 0;
  props->speed = p7.speed;
  props->port = p7.port;
  props->maxComms = p7.maxComms;
  props->maxRecvs = p7.maxRecvs;
  props->latency = p7.latency;
  props->netDeviceType = p7.netDeviceType;
  props->netDeviceVersion = p7.netDeviceVersion;
  return flagcxSuccess;
}

static flagcxResult_t flagcxNet_v7_as_v8_regMr(void* comm, void* data, size_t size, int type, void** mhandle) {
  if (size >= 1<<31) return flagcxInternalError;
  return flagcxNet_v7->regMr(comm, data, (int) size, type, mhandle);
}



static flagcxResult_t flagcxNet_v7_as_v8_init(flagcxDebugLogger_t logfn) {
  FLAGCXCHECK(flagcxNet_v7->init(logfn));
  flagcxNet_v7_as_v8.name = flagcxNet_v7->name;
  flagcxNet_v7_as_v8.devices = flagcxNet_v7->devices;
  flagcxNet_v7_as_v8.getProperties = flagcxlNet_v7_as_v8_getProperties; // flagcxNet_v5->getProperties;
  flagcxNet_v7_as_v8.listen = flagcxNet_v7->listen;
  flagcxNet_v7_as_v8.connect = flagcxNet_v7->connect;
  flagcxNet_v7_as_v8.accept =  flagcxNet_v7->accept;
  flagcxNet_v7_as_v8.regMr = flagcxNet_v7_as_v8_regMr;
  flagcxNet_v7_as_v8.regMrDmaBuf = flagcxNet_v7->regMrDmaBuf;
  flagcxNet_v7_as_v8.deregMr = flagcxNet_v7->deregMr;
  flagcxNet_v7_as_v8.isend = flagcxNet_v7->isend;
  flagcxNet_v7_as_v8.irecv = flagcxNet_v7->irecv;
  flagcxNet_v7_as_v8.iflush = flagcxNet_v7->iflush;
  flagcxNet_v7_as_v8.test = flagcxNet_v7->test;
  flagcxNet_v7_as_v8.closeSend = flagcxNet_v7->closeSend;
  flagcxNet_v7_as_v8.closeRecv = flagcxNet_v7->closeRecv;
  flagcxNet_v7_as_v8.closeListen = flagcxNet_v7->closeListen;
  flagcxNet_v7_as_v8.getDeviceMr = flagcxNet_v7->getDeviceMr;
  flagcxNet_v7_as_v8.irecvConsumed = flagcxNet_v7->irecvConsumed;
  return flagcxSuccess;
}



static flagcxResult_t flagcxNet_v6_as_v8_getProperties(int dev, flagcxNetProperties_v8_t* props) {
  flagcxNetProperties_v6_t p6;
  flagcxResult_t ans = flagcxNet_v6->getProperties(dev, &p6);
  if (ans != flagcxSuccess) return ans;
  props->name = p6.name;
  props->pciPath = p6.pciPath;
  props->guid = p6.guid;
  props->ptrSupport = p6.ptrSupport;
  props->regIsGlobal = 0;
  props->speed = p6.speed;
  props->port = p6.port;
  props->maxComms = p6.maxComms;
  props->maxRecvs = p6.maxRecvs;
  props->latency = p6.latency;
  props->netDeviceType = FLAGCX_NET_DEVICE_HOST;
  props->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  return flagcxSuccess;
}


static flagcxResult_t flagcxNet_v6_as_v8_regMr(void* comm, void* data, size_t size, int type, void** mhandle) {
  if (size >= 1<<31) return flagcxInternalError;
  return flagcxNet_v6->regMr(comm, data, (int) size, type, mhandle);
}

static flagcxResult_t flagcxNet_v6_as_v8_connect(int dev, void* handle, void** sendComm, flagcxNetDeviceHandle_t** /*sendDevComm*/) {
  return flagcxNet_v6->connect(dev, handle, sendComm);
}

static flagcxResult_t flagcxNet_v6_as_v8_accept(void* listenComm, void** recvComm, flagcxNetDeviceHandle_t** /*recvDevComm*/) {
  return flagcxNet_v6->accept(listenComm, recvComm);
}



static flagcxResult_t flagcxNet_v6_as_v8_init(flagcxDebugLogger_t logfn) {
  FLAGCXCHECK(flagcxNet_v6->init(logfn));
  flagcxNet_v6_as_v8.name = flagcxNet_v6->name;
  flagcxNet_v6_as_v8.devices = flagcxNet_v6->devices;
  flagcxNet_v6_as_v8.getProperties = flagcxNet_v6_as_v8_getProperties; // flagcxNet_v5->getProperties;
  flagcxNet_v6_as_v8.listen = flagcxNet_v6->listen;
  flagcxNet_v6_as_v8.connect = flagcxNet_v6_as_v8_connect;
  flagcxNet_v6_as_v8.accept =  flagcxNet_v6_as_v8_accept;
  flagcxNet_v6_as_v8.regMr = flagcxNet_v6_as_v8_regMr;
  flagcxNet_v6_as_v8.regMrDmaBuf = flagcxNet_v6->regMrDmaBuf;
  flagcxNet_v6_as_v8.deregMr = flagcxNet_v6->deregMr;
  flagcxNet_v6_as_v8.isend = flagcxNet_v6->isend;
  flagcxNet_v6_as_v8.irecv = flagcxNet_v6->irecv;
  flagcxNet_v6_as_v8.iflush = flagcxNet_v6->iflush;
  flagcxNet_v6_as_v8.test = flagcxNet_v6->test;
  flagcxNet_v6_as_v8.closeSend = flagcxNet_v6->closeSend;
  flagcxNet_v6_as_v8.closeRecv = flagcxNet_v6->closeRecv;
  flagcxNet_v6_as_v8.closeListen = flagcxNet_v6->closeListen;
  flagcxNet_v6_as_v8.getDeviceMr = NULL;
  flagcxNet_v6_as_v8.irecvConsumed = NULL;
  return flagcxSuccess;
}

static flagcxResult_t flagcxNet_v5_as_v8_getProperties(int dev, flagcxNetProperties_v8_t* props) {
  flagcxNetProperties_v6_t p6;
  flagcxResult_t ans = flagcxNet_v5->getProperties(dev, &p6);
  if (ans != flagcxSuccess) return ans;
  props->name = p6.name;
  props->pciPath = p6.pciPath;
  props->guid = p6.guid;
  props->ptrSupport = p6.ptrSupport;
  props->regIsGlobal = 0;
  props->speed = p6.speed;
  props->port = p6.port;
  props->maxComms = p6.maxComms;
  props->maxRecvs = p6.maxRecvs;
  props->latency = p6.latency;
  props->netDeviceType    = FLAGCX_NET_DEVICE_HOST;
  props->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  return flagcxSuccess;
}



static flagcxResult_t flagcxNet_v5_as_v8_regMr(void* comm, void* data, size_t size, int type, void** mhandle) {
  if (size >= 1<<31) return flagcxInternalError;
  return flagcxNet_v5->regMr(comm, data, (int) size, type, mhandle);
}

static flagcxResult_t flagcxNet_v5_as_v8_connect(int dev, void* handle, void** sendComm, flagcxNetDeviceHandle_t** /*sendDevComm*/) {
  return flagcxNet_v5->connect(dev, handle, sendComm);
}

static flagcxResult_t flagcxNet_v5_as_v8_accept(void* listenComm, void** recvComm, flagcxNetDeviceHandle_t** /*recvDevComm*/) {
  return flagcxNet_v5->accept(listenComm, recvComm);
}

// We use a wrapper around the v5 init to copy over the struct contents
// post-init since they may not be initialized before hand.
static flagcxResult_t flagcxNet_v5_as_v8_init(flagcxDebugLogger_t logfn) {
  FLAGCXCHECK(flagcxNet_v5->init(logfn));
  flagcxNet_v5_as_v8.name = flagcxNet_v5->name;
  flagcxNet_v5_as_v8.devices = flagcxNet_v5->devices;
  flagcxNet_v5_as_v8.getProperties = flagcxNet_v5_as_v8_getProperties;
  flagcxNet_v5_as_v8.listen = flagcxNet_v5->listen;
  flagcxNet_v5_as_v8.connect = flagcxNet_v5_as_v8_connect;
  flagcxNet_v5_as_v8.accept =  flagcxNet_v5_as_v8_accept;
  flagcxNet_v5_as_v8.regMr = flagcxNet_v5_as_v8_regMr;
  flagcxNet_v5_as_v8.regMrDmaBuf = NULL;
  flagcxNet_v5_as_v8.deregMr = flagcxNet_v5->deregMr;
  flagcxNet_v5_as_v8.isend = flagcxNet_v5->isend;
  flagcxNet_v5_as_v8.irecv = flagcxNet_v5->irecv;
  flagcxNet_v5_as_v8.iflush = flagcxNet_v5->iflush;
  flagcxNet_v5_as_v8.test = flagcxNet_v5->test;
  flagcxNet_v5_as_v8.closeSend = flagcxNet_v5->closeSend;
  flagcxNet_v5_as_v8.closeRecv = flagcxNet_v5->closeRecv;
  flagcxNet_v5_as_v8.closeListen = flagcxNet_v5->closeListen;
  flagcxNet_v5_as_v8.getDeviceMr = NULL;
  flagcxNet_v5_as_v8.irecvConsumed = NULL;
  return flagcxSuccess;
}


static flagcxResult_t flagcxCollNet_v5_as_v8_getProperties(int dev, flagcxNetProperties_v8_t* props) {
  flagcxNetProperties_v6_t p6;
  flagcxResult_t ans = flagcxCollNet_v5->getProperties(dev, &p6);
  if (ans != flagcxSuccess) return ans;
  props->name = p6.name;
  props->pciPath = p6.pciPath;
  props->guid = p6.guid;
  props->ptrSupport = p6.ptrSupport;
  props->regIsGlobal = 0;
  props->speed = p6.speed;
  props->port = p6.port;
  props->maxComms = p6.maxComms;
  props->maxRecvs = p6.maxRecvs;
  props->latency = p6.latency;
  props->netDeviceType    = FLAGCX_NET_DEVICE_HOST;
  props->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  return flagcxSuccess;
}

static flagcxResult_t flagcxCollNet_v5_as_v8_regMr(void* comm, void* data, size_t size, int type, void** mhandle) {
  if (size >= 1<<31) return flagcxInternalError;
  return flagcxCollNet_v5->regMr(comm, data, (int) size, type, mhandle);
}


// We use a wrapper around the v5 init to copy over the struct contents
// post-init since they may not be initialized before hand.
static flagcxResult_t flagcxCollNet_v5_as_v8_init(flagcxDebugLogger_t logfn) {
  FLAGCXCHECK(flagcxCollNet_v5->init(logfn));
  flagcxCollNet_v5_as_v8.name = flagcxCollNet_v5->name;
  flagcxCollNet_v5_as_v8.devices = flagcxCollNet_v5->devices;
  flagcxCollNet_v5_as_v8.getProperties = flagcxCollNet_v5_as_v8_getProperties;
  flagcxCollNet_v5_as_v8.listen = flagcxCollNet_v5->listen;
  flagcxCollNet_v5_as_v8.connect = flagcxCollNet_v5->connect;
  flagcxCollNet_v5_as_v8.reduceSupport = flagcxCollNet_v5->reduceSupport;
  flagcxCollNet_v5_as_v8.regMr = flagcxCollNet_v5_as_v8_regMr;
  flagcxCollNet_v5_as_v8.regMrDmaBuf = NULL;
  flagcxCollNet_v5_as_v8.deregMr = flagcxCollNet_v5->deregMr;
  flagcxCollNet_v5_as_v8.iallreduce = flagcxCollNet_v5->iallreduce;
  flagcxCollNet_v5_as_v8.iallgather = nullptr;
  flagcxCollNet_v5_as_v8.ireducescatter = nullptr;
  flagcxCollNet_v5_as_v8.iflush = flagcxCollNet_v5->iflush;
  flagcxCollNet_v5_as_v8.test = flagcxCollNet_v5->test;
  flagcxCollNet_v5_as_v8.closeColl = flagcxCollNet_v5->closeColl;
  flagcxCollNet_v5_as_v8.closeListen = flagcxCollNet_v5->closeListen;
  return flagcxSuccess;
}

static flagcxResult_t flagcxCollNet_v6_as_v8_getProperties(int dev, flagcxNetProperties_v8_t* props) {
  flagcxNetProperties_v6_t p6;
  flagcxResult_t ans = flagcxCollNet_v6->getProperties(dev, &p6);
  if (ans != flagcxSuccess) return ans;
  props->name = p6.name;
  props->pciPath = p6.pciPath;
  props->guid = p6.guid;
  props->ptrSupport = p6.ptrSupport;
  props->regIsGlobal = 0;
  props->speed = p6.speed;
  props->port = p6.port;
  props->maxComms = p6.maxComms;
  props->maxRecvs = p6.maxRecvs;
  props->latency = p6.latency;
  props->netDeviceType    = FLAGCX_NET_DEVICE_HOST;
  props->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  return flagcxSuccess;
}



static flagcxResult_t flagcxCollNet_v6_as_v8_regMr(void* comm, void* data, size_t size, int type, void** mhandle) {
  if (size >= 1<<31) return flagcxInternalError;
  return flagcxCollNet_v6->regMr(comm, data, (int) size, type, mhandle);
}

// We use a wrapper around the v6 init to copy over the struct contents
// post-init since they may not be initialized before hand.
static flagcxResult_t flagcxCollNet_v6_as_v8_init(flagcxDebugLogger_t logfn) {
  FLAGCXCHECK(flagcxCollNet_v6->init(logfn));
  flagcxCollNet_v6_as_v8.name = flagcxCollNet_v6->name;
  flagcxCollNet_v6_as_v8.devices = flagcxCollNet_v6->devices;
  flagcxCollNet_v6_as_v8.getProperties = flagcxCollNet_v6_as_v8_getProperties;
  flagcxCollNet_v6_as_v8.listen = flagcxCollNet_v6->listen;
  flagcxCollNet_v6_as_v8.connect = flagcxCollNet_v6->connect;
  flagcxCollNet_v6_as_v8.reduceSupport = flagcxCollNet_v6->reduceSupport;
  flagcxCollNet_v6_as_v8.regMr = flagcxCollNet_v6_as_v8_regMr;
  flagcxCollNet_v6_as_v8.regMrDmaBuf = flagcxCollNet_v6->regMrDmaBuf;
  flagcxCollNet_v6_as_v8.deregMr = flagcxCollNet_v6->deregMr;
  flagcxCollNet_v6_as_v8.iallreduce = flagcxCollNet_v6->iallreduce;
  flagcxCollNet_v6_as_v8.iallgather = nullptr;
  flagcxCollNet_v6_as_v8.ireducescatter = nullptr;
  flagcxCollNet_v6_as_v8.iflush = flagcxCollNet_v6->iflush;
  flagcxCollNet_v6_as_v8.test = flagcxCollNet_v6->test;
  flagcxCollNet_v6_as_v8.closeColl = flagcxCollNet_v6->closeColl;
  flagcxCollNet_v6_as_v8.closeListen = flagcxCollNet_v6->closeListen;
  return flagcxSuccess;
}

static flagcxResult_t flagcxCollNet_v7_as_v8_getProperties(int dev, flagcxNetProperties_v8_t* props) {
  flagcxNetProperties_v7_t p7;
  flagcxResult_t ans = flagcxCollNet_v7->getProperties(dev, &p7);
  if (ans != flagcxSuccess) return ans;
  props->name = p7.name;
  props->pciPath = p7.pciPath;
  props->guid = p7.guid;
  props->ptrSupport = p7.ptrSupport;
  props->regIsGlobal = 0;
  props->speed = p7.speed;
  props->port = p7.port;
  props->maxComms = p7.maxComms;
  props->maxRecvs = p7.maxRecvs;
  props->latency = p7.latency;
  props->netDeviceType    = FLAGCX_NET_DEVICE_HOST;
  props->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  return flagcxSuccess;
}


static flagcxResult_t flagcxCollNet_v7_as_v8_regMr(void* comm, void* data, size_t size, int type, void** mhandle) {
  if (size >= 1<<31) return flagcxInternalError;
  return flagcxCollNet_v7->regMr(comm, data, (int) size, type, mhandle);
}

// We use a wrapper around the v7 init to copy over the struct contents
// post-init since they may not be initialized before hand.
static flagcxResult_t flagcxCollNet_v7_as_v8_init(flagcxDebugLogger_t logfn) {
  FLAGCXCHECK(flagcxCollNet_v7->init(logfn));
  flagcxCollNet_v7_as_v8.name = flagcxCollNet_v7->name;
  flagcxCollNet_v7_as_v8.devices = flagcxCollNet_v7->devices;
  flagcxCollNet_v7_as_v8.getProperties = flagcxCollNet_v7_as_v8_getProperties;
  flagcxCollNet_v7_as_v8.listen = flagcxCollNet_v7->listen;
  flagcxCollNet_v7_as_v8.connect = flagcxCollNet_v7->connect;
  flagcxCollNet_v7_as_v8.reduceSupport = flagcxCollNet_v7->reduceSupport;
  flagcxCollNet_v7_as_v8.regMr = flagcxCollNet_v7_as_v8_regMr;
  flagcxCollNet_v7_as_v8.regMrDmaBuf = flagcxCollNet_v7->regMrDmaBuf;
  flagcxCollNet_v7_as_v8.deregMr = flagcxCollNet_v7->deregMr;
  flagcxCollNet_v7_as_v8.iallreduce = flagcxCollNet_v7->iallreduce;
  flagcxCollNet_v7_as_v8.iallgather = nullptr;
  flagcxCollNet_v7_as_v8.ireducescatter = nullptr;
  flagcxCollNet_v7_as_v8.iflush = flagcxCollNet_v7->iflush;
  flagcxCollNet_v7_as_v8.test = flagcxCollNet_v7->test;
  flagcxCollNet_v7_as_v8.closeColl = flagcxCollNet_v7->closeColl;
  flagcxCollNet_v7_as_v8.closeListen = flagcxCollNet_v7->closeListen;
  return flagcxSuccess;
}


static pthread_mutex_t netLock = PTHREAD_MUTEX_INITIALIZER;
flagcxNet_t* flagcxNets[3] = { nullptr, &flagcxNetIb, &flagcxNetSocket };
flagcxCollNet_t* flagcxCollNets[3] = { nullptr, nullptr, nullptr };
enum flagcxNetState {
  flagcxNetStateInit = 0,
  flagcxNetStateEnabled = 1,
  flagcxNetStateDisabled = 2
};
enum flagcxNetState flagcxNetStates[3] = { flagcxNetStateInit, flagcxNetStateInit, flagcxNetStateInit };
enum flagcxNetState flagcxCollNetStates[3] = { flagcxNetStateInit, flagcxNetStateInit, flagcxNetStateInit };

static void* tryOpenDynamicLib(char* name) {
  if (nullptr == name || strlen(name) == 0) {
    return nullptr;
  }
  void *handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
  if (nullptr == handle) {
    if (ENOENT == errno) {
      INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: No plugin found (%s)", name);
    } else {
      INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Plugin load returned %d : %s when loading %s", errno, dlerror(), name);
    }
  }
  return handle;
}

static void summarizeOpenNetPluginErrors(char* pluginNames) {
  const char *separator = " ";
  int len = strlen(pluginNames);
  // remove tail separator
  pluginNames[len - 1] = '\0';

  // remove last plugin name
  while (len > 0 && pluginNames[--len] != *separator);
  if (len > 0) {
    pluginNames[len] = '\0';
  }

  // distinguish between one load attempt and multiple attempts
  if (strstr(pluginNames, separator)) {
    INFO(FLAGCX_ENV|FLAGCX_TUNING, "NET/Plugin: Most recent plugin load returned %d : %s. All attempts to load '%s' also failed.", errno, dlerror(), pluginNames);
  } else {
    INFO(FLAGCX_ENV|FLAGCX_TUNING, "NET/Plugin: Plugin load returned %d : %s : when loading %s", errno, dlerror(), pluginNames);
  }
}


static void* openNetPluginLib(void) {
  void *pluginLib;

#define MAX_PLUGIN_LOAD 2

  int len;
  char netPluginLibNameTried[MAX_PLUGIN_LOAD * PATH_MAX] = { 0 };
  char *ptr = netPluginLibNameTried;
  char netPluginLibName[PATH_MAX];
  const char *envNetPluginName = getenv("FLAGCX_NET_PLUGIN");
  if (envNetPluginName && strlen(envNetPluginName)) {
    snprintf(netPluginLibName, PATH_MAX, "%s", envNetPluginName);
    pluginLib = tryOpenDynamicLib(netPluginLibName);
    if (pluginLib) {
      INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Plugin name set by env to %s", netPluginLibName);
      return pluginLib;
    }
    len = PATH_MAX - strlen(ptr);
    snprintf(ptr + strlen(ptr), len + 1, "%s ", netPluginLibName);

    snprintf(netPluginLibName, PATH_MAX, "libflagcx-net-%s.so", envNetPluginName);
    pluginLib = tryOpenDynamicLib(netPluginLibName);
    if (pluginLib) {
      INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Plugin name set by env to %s", netPluginLibName);
      return pluginLib;
    }
    len = PATH_MAX - strlen(ptr);
    snprintf(ptr + strlen(ptr), len + 1, "%s ", netPluginLibName);
  } else {
    snprintf(netPluginLibName, PATH_MAX, "libflagcx-net.so");
    pluginLib = tryOpenDynamicLib(netPluginLibName);
    if (pluginLib) {
      return pluginLib;
    }
    len = PATH_MAX - strlen(ptr);
    snprintf(ptr + strlen(ptr), len + 1, "%s ", netPluginLibName);
  }
  summarizeOpenNetPluginErrors(ptr);

  return nullptr;
}


flagcxResult_t flagcxNetPluginInit() {
  void* netPluginLib = openNetPluginLib();
  if (netPluginLib == nullptr) {
    INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Using internal network plugin.");
    return flagcxSuccess;
  }
  
  flagcxNets[0] = (flagcxNet_v8_t*)dlsym(netPluginLib, "flagcxNetPlugin_v8");
  if (flagcxNets[0] == nullptr) {
    INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Failed to find flagcxNetPlugin_v8 symbol.");
    // Try v7 plugin
    flagcxNet_v7 = (flagcxNet_v7_t*)dlsym(netPluginLib, "flagcxNetPlugin_v7");
    if (flagcxNet_v7 == nullptr) {
      // Try v6 plugin
      flagcxNet_v6 = (flagcxNet_v6_t*)dlsym(netPluginLib, "flagcxNetPlugin_v6");
      if (flagcxNet_v6 == nullptr) {
        // Try v5 plugin
        flagcxNet_v5 = (flagcxNet_v5_t*)dlsym(netPluginLib, "flagcxNetPlugin_v5");
        if (flagcxNet_v5 == nullptr) {
          INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Failed to find flagcxNetPlugin symbol (>= v5). flagcxNetPlugin symbols v4 and lower are not supported.");
          if (netPluginLib != nullptr) dlclose(netPluginLib);
          return flagcxSuccess;
        } else {
          flagcxNets[0] = &flagcxNet_v5_as_v8;
          flagcxNet_v5_as_v8.init = flagcxNet_v5_as_v8_init;
          // Set the name right away to allow for flagcx_NET=... to work
          flagcxNet_v5_as_v8.name = flagcxNet_v5->name;
          INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Loaded net plugin %s (v5)", flagcxNets[0]->name);
        }
      } else {
        flagcxNets[0] = &flagcxNet_v6_as_v8;
        flagcxNet_v6_as_v8.init = flagcxNet_v6_as_v8_init;
        // Set the name right away to allow for flagcx_NET=... to work
        flagcxNet_v6_as_v8.name = flagcxNet_v6->name;
        INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Loaded net plugin %s (v6)", flagcxNets[0]->name);
      }
    } else {
      flagcxNets[0] = &flagcxNet_v7_as_v8;
      flagcxNet_v7_as_v8.init = flagcxNet_v7_as_v8_init;
      // Set the name right away to allow for flagcx_NET=... to work
      flagcxNet_v7_as_v8.name = flagcxNet_v7->name;
      INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Loaded net plugin %s (v7)", flagcxNets[0]->name);
    }
  }

  // Check for CollNet
  flagcxCollNets[0] = (flagcxCollNet_v8_t*)dlsym(netPluginLib, "flagcxCollNetPlugin_v8");
  if (flagcxCollNets[0] == nullptr) {
    INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Failed to find flagcxCollNetPlugin_v8 symbol.");
    flagcxCollNet_v7 = (flagcxCollNet_v7_t*)dlsym(netPluginLib, "flagcxCollNetPlugin_v7");
    if (flagcxCollNet_v7 == nullptr) {
      flagcxCollNet_v6 = (flagcxCollNet_v6_t*)dlsym(netPluginLib, "flagcxCollNetPlugin_v6");
      if (flagcxCollNet_v6 == nullptr) {
        flagcxCollNet_v5 = (flagcxCollNet_v5_t*)dlsym(netPluginLib, "flagcxCollNetPlugin_v5");
        if (flagcxCollNet_v5 == nullptr) {
          INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Failed to find flagcxCollNetPlugin symbol (>= v5). flagcxCollNetPlugin symbols v4 and lower are not supported.");
        } else {
          flagcxCollNets[0] = &flagcxCollNet_v5_as_v8;
          flagcxCollNet_v5_as_v8.init = flagcxCollNet_v5_as_v8_init;
          flagcxCollNet_v5_as_v8.name = flagcxCollNet_v5->name;
          INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Loaded coll plugin %s (v5)", flagcxCollNets[0]->name);
        }
      } else {
        flagcxCollNets[0] = &flagcxCollNet_v6_as_v8;
        flagcxCollNet_v6_as_v8.init = flagcxCollNet_v6_as_v8_init;
        flagcxCollNet_v6_as_v8.name = flagcxCollNet_v6->name;
        INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Loaded coll plugin %s (v6)", flagcxCollNets[0]->name);
      }
    } else {
      flagcxCollNets[0] = &flagcxCollNet_v7_as_v8;
      flagcxCollNet_v7_as_v8.init = flagcxCollNet_v7_as_v8_init;
      flagcxCollNet_v7_as_v8.name = flagcxCollNet_v7->name;
      INFO(FLAGCX_INIT|FLAGCX_NET, "NET/Plugin: Loaded coll plugin %s (v7)", flagcxCollNets[0]->name);
    }
  }
  return flagcxSuccess;
}



flagcxResult_t flagcxNetCheckDeviceVersion(struct flagcxComm* comm, flagcxNet_t* net, int dev) {
  flagcxNetProperties_t props;

  flagcxCHECK(net->getProperties(dev, &props));
  flagcxNetDeviceType type = props.netDeviceType;
  if (type) switch (type) {
    case FLAGCX_NET_DEVICE_UNPACK:
      if (props.netDeviceVersion == FLAGCX_NET_DEVICE_UNPACK_VERSION) {
        INFO(FLAGCX_INIT, "Using FLAGCX_NET_DEVICE_UNPACK net plugin version %d",
          props.netDeviceVersion);
        return flagcxSuccess;
      } else {
        WARN("FLAGCX_DEVICE_UNPACK plugin has incompatible version %d, this flagcx build is compatible with %d, not using it",
          props.netDeviceVersion, FLAGCX_NET_DEVICE_UNPACK_VERSION);
        return flagcxInternalError;
      }
    default:
      WARN("Unknown device code index");
      return flagcxInternalError;
  }

  INFO(FLAGCX_INIT, "Using non-device net plugin version %d",
    props.netDeviceVersion);
  return flagcxSuccess;
}

static flagcxResult_t netGetState(int i, enum flagcxNetState* state) {
  pthread_mutex_lock(&netLock);
  if (flagcxNetStates[i] == flagcxNetStateInit) {
    int ndev;
    if (flagcxNets[i]->init(flagcxDebugLog) != flagcxSuccess) flagcxNetStates[i] = flagcxNetStateDisabled;
    else if (flagcxNets[i]->devices(&ndev) != flagcxSuccess || ndev <= 0) flagcxNetStates[i] = flagcxNetStateDisabled;
    else flagcxNetStates[i] = flagcxNetStateEnabled;
  }
  *state = flagcxNetStates[i];
  pthread_mutex_unlock(&netLock);
  return flagcxSuccess;
}

static flagcxResult_t collNetGetState(int i, enum flagcxNetState* state) {
  pthread_mutex_lock(&netLock);
  if (flagcxCollNetStates[i] == flagcxNetStateInit) {
    int ndev;
    if (flagcxCollNets[i]->init(flagcxDebugLog) != flagcxSuccess) flagcxCollNetStates[i] = flagcxNetStateDisabled;
    else if (flagcxCollNets[i]->devices(&ndev) != flagcxSuccess || ndev <= 0) flagcxCollNetStates[i] = flagcxNetStateDisabled;
    else flagcxCollNetStates[i] = flagcxNetStateEnabled;
  }
  *state = flagcxCollNetStates[i];
  pthread_mutex_unlock(&netLock);
  return flagcxSuccess;
}



flagcxResult_t flagcxNetInit(struct flagcxComm* comm) {
  // Initialize main communication network
  const char* netName;
  bool ok = false;

  netName = comm->config.netName;
  for (int i=0; i<3; i++) {
    if (flagcxNets[i] == nullptr) continue;
    enum flagcxNetState state;
    FLAGCXCHECK(netGetState(i, &state));
    if (state != flagcxNetStateEnabled) continue;
    if (netName && strcasecmp(netName, flagcxNets[i]->name) != 0) continue;
    if (flagcxSuccess != flagcxNetCheckDeviceVersion(comm, flagcxNets[i], 0)) {
      // Mismatched device plugin version
      continue;
    }

    comm->flagcxNet = flagcxNets[i];
    ok = true;

    if (flagcxCollNets[i]) {
      FLAGCXCHECK(collNetGetState(i, &state));
      if (state == flagcxNetStateEnabled) {
        comm->flagcxCollNet = flagcxCollNets[i];
      }
    }
    break;
  }

  if (!ok) {
    WARN("Error: network %s not found.", netName ? netName : "");
    return flagcxInvalidUsage;
  }
  return flagcxSuccess;
}


flagcxResult_t flagcxGpuGdrSupport(struct flagcxComm* comm, int* gdrSupport) {
  constexpr int GPU_BUF_SIZE = 2*1024*1024;
#if CUDART_VERSION >= 11030
  // In CUDA 11.3 and later we can now query the cudaDevAttrGPUDirectRDMASupported attribute
  int driverVersion;
  CUDACHECK(cudaDriverGetVersion(&driverVersion));
  if (driverVersion >= 11030) {
    int cudaDev, attr = 0;
    CUDACHECK(cudaGetDevice(&cudaDev));
    CUDACHECK(cudaDeviceGetAttribute(&attr, cudaDevAttrGPUDirectRDMASupported, cudaDev));
    *gdrSupport = attr;
    return flagcxSuccess;
  }
#endif
  static int gdrSupportMatrix[32] = {
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
  if (gdrSupportMatrix[comm->cudaDev] == -1) {
    int netDevs;
    FLAGCXCHECK(comm->flagcxNet->devices(&netDevs));
    gdrSupportMatrix[comm->cudaDev] = 0;
    for (int dev=0; dev<netDevs; dev++) {
      // Find a net device which is GDR-capable
      flagcxNetProperties_t props;
      FLAGCXCHECK(comm->flagcxNet->getProperties(dev, &props));
      if ((props.ptrSupport & flagcx_PTR_CUDA) == 0) continue;

    // Allocate memory on the GPU and try to register it on the NIC.
    void *lComm = NULL, *sComm = NULL, *rComm = NULL;
    flagcxNetHandle_t handle;
    char* gpuPtr = NULL;
    void* mHandle = NULL;
    flagcxResult_t ret;
    flagcxDebugNoWarn = flagcx_NET;
    FLAGCXCHECKGOTO(comm->flagcxNet->listen(dev, &handle, &lComm), ret, cleanup1);

    bool connected;
    connected = false;
    while (!connected) {

      // If we're aborting now, skip to cleanup
      if (__atomic_load_n(comm->abortFlag, __ATOMIC_RELAXED)) {
        goto cleanup2;
      }

      if (sComm == NULL)
        FLAGCXCHECKGOTO(comm->flagcxNet->connect(dev, &handle, &sComm, NULL), ret, cleanup2);

      if (rComm == NULL)
        FLAGCXCHECKGOTO(comm->flagcxNet->accept(lComm, &rComm, NULL), ret, cleanup2);

      connected = (rComm != NULL) && (sComm != NULL);
    }

    FLAGCXCHECKGOTO(flagcxCudaMalloc(&gpuPtr, GPU_BUF_SIZE), ret, cleanup2);
    if (comm->flagcxNet->regMr(sComm, gpuPtr, GPU_BUF_SIZE, flagcx_PTR_CUDA, &mHandle) == flagcxSuccess) {
      FLAGCXCHECK(comm->flagcxNet->deregMr(sComm, mHandle));
      FLAGCXCHECK(comm->flagcxNet->regMr(rComm, gpuPtr, GPU_BUF_SIZE, flagcx_PTR_CUDA, &mHandle));
      FLAGCXCHECK(comm->flagcxNet->deregMr(rComm, mHandle));
      gdrSupportMatrix[comm->cudaDev] = 1;
    }
    flagcxDebugNoWarn = 0;
    FLAGCXCHECK(flagcxCudaFree(gpuPtr));
cleanup2:
    if (rComm != NULL)
      FLAGCXCHECK(comm->flagcxNet->closeRecv(rComm));
    if (sComm != NULL)
      FLAGCXCHECK(comm->flagcxNet->closeSend(sComm));
    FLAGCXCHECK(comm->flagcxNet->closeListen(lComm));
cleanup1:
      break;
    }
  }
  *gdrSupport = gdrSupportMatrix[comm->cudaDev];
  return flagcxSuccess;
}

int flagcxNetVersion(struct flagcxComm* comm) {
  return
    (comm->flagcxNet == &flagcxNet_v5_as_v8) ? 5 :
    (comm->flagcxNet == &flagcxNet_v6_as_v8) ? 6 :
    (comm->flagcxNet == &flagcxNet_v7_as_v8) ? 7 :
    8;
}



flagcxResult_t flagcxProxySend(sendNetResources *resources, void *data,
                               size_t size, flagcxProxyArgs *args) {
  if (deviceKernel) {
    deviceAdaptor->deviceMemcpy(args->hEventReady,args->dEventReady, 1,
                                flagcxMemcpyDeviceToHost, resources->cpStream,
                                NULL);
  }
  if (!__atomic_load_n(&args->hEventReady, __ATOMIC_RELAXED))
    return flagcxSuccess;
  if (args->transmitted < args->chunkSteps) {
    int stepMask = args->sendStepMask;

    if (args->waitCopy < args->chunkSteps &&
        args->waitCopy - args->transmitted < MAXSTEPS) {
      int step = args->waitCopy & stepMask;
      args->subs[step].stepSize =
          std::min(args->chunkSize, size - args->totalCopySize);
      args->subs[step].stepBuff = resources->buffers[0] + (CHUNKSIZE * step);
      deviceAdaptor->deviceMemcpy(
          args->subs[step].stepBuff, (char *)data + args->totalCopySize,
          args->subs[step].stepSize, flagcxMemcpyDeviceToDevice,
          resources->cpStream, args->subs[step].copyArgs);
      deviceAdaptor->eventRecord(resources->cpEvents[step],
                                 resources->cpStream);
      args->totalCopySize += args->subs[step].stepSize;
      args->waitCopy++;
    }

    if (args->copied < args->waitCopy) {
      int step = args->copied & stepMask;
      if (deviceAdaptor->eventQuery(resources->cpEvents[step]) ==
          flagcxSuccess) {
        args->copied++;
      }
    }

    if (args->posted < args->copied) {
      void *req = NULL;
      flagcxNetIb.isend(resources->netSendComm,
                        args->subs[args->posted & stepMask].stepBuff,
                        args->subs[args->posted & stepMask].stepSize, 0,
                        resources->mhandles[0], &req);
      if (req) {
        args->subs[args->posted++ & stepMask].requests[0] = req;
      }
    }

    if (args->transmitted < args->posted) {
      void *req = args->subs[args->transmitted & stepMask].requests[0];
      int done = 0, sizes;
      flagcxNetIb.test(req, &done, &sizes);
      if (done) {
        args->transmitted++;
      }
    }
  } else {
    __atomic_store_n(args->hlArgs, 1, __ATOMIC_RELAXED);
    args->done = true;
    if (deviceKernel) {
      deviceAdaptor->deviceMemcpy(args->dlArgs, args->hlArgs, 1,
                                  flagcxMemcpyHostToDevice, resources->cpStream,
                                  NULL);
    }
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxProxyRecv(recvNetResources *resources, void *data,
                               size_t size, flagcxProxyArgs *args) {
  if (deviceKernel) {
    deviceAdaptor->deviceMemcpy(args->hEventReady,args->dEventReady, 1,
                                flagcxMemcpyDeviceToHost, resources->cpStream,
                                NULL);
  }
  if (!__atomic_load_n(&args->hEventReady, __ATOMIC_RELAXED))
    return flagcxSuccess;
  if (args->copied < args->chunkSteps) {
    int stepMask = args->sendStepMask;
    if (args->posted < args->chunkSteps &&
        args->posted - args->copied < MAXSTEPS) {
      int tags[8] = {0};
      void *req = NULL;
      args->subs[args->posted & stepMask].stepSize =
          std::min(args->chunkSize, size - args->totalPostSize);
      args->subs[args->posted & stepMask].stepBuff =
          resources->buffers[0] + CHUNKSIZE * (args->posted & stepMask);
      flagcxNetIb.irecv(resources->netRecvComm, 1,
                        &args->subs[args->posted & stepMask].stepBuff,
                        (int *)&args->subs[args->posted & stepMask].stepSize,
                        tags, resources->mhandles, &req);
      if (req) {
        args->subs[args->posted & stepMask].requests[0] = req;
        args->totalPostSize += args->subs[args->posted++ & stepMask].stepSize;
      }
    }

    if (args->transmitted < args->posted) {
      void *req = args->subs[args->transmitted & stepMask].requests[0];
      int done = 0, sizes;
      flagcxNetIb.test(req, &done, &sizes);
      if (done) {
        args->transmitted++;
      }
    }

    if (args->postFlush < args->transmitted) {
      void *req = NULL;
      void *allData[] = {args->subs[args->postFlush & stepMask].stepBuff};
      flagcxNetIb.iflush(resources->netRecvComm, 1, allData,
                         &args->subs[args->postFlush & stepMask].stepSize,
                         resources->mhandles, &req);
      if (req) {
        args->subs[args->postFlush++ & stepMask].requests[0] = req;
      }
    }

    if (args->flushed < args->postFlush) {
      void *req = args->subs[args->flushed & stepMask].requests[0];
      int done = 0, sizes;
      flagcxNetIb.test(req, &done, &sizes);
      if (done) {
        args->flushed++;
      }
    }

    if (args->waitCopy < args->flushed) {
      int step = args->waitCopy & stepMask;
      deviceAdaptor->deviceMemcpy(
          (char *)data + args->totalCopySize, args->subs[step].stepBuff,
          args->subs[step].stepSize, flagcxMemcpyDeviceToDevice,
          resources->cpStream, args->subs[step].copyArgs);
      deviceAdaptor->eventRecord(resources->cpEvents[step],
                                 resources->cpStream);
      args->totalCopySize += args->subs[step].stepSize;
      args->waitCopy++;
    }

    if (args->copied < args->waitCopy) {
      int step = args->copied & stepMask;
      if (deviceAdaptor->eventQuery(resources->cpEvents[step]) ==
          flagcxSuccess) {
        args->copied++;
      }
    }

  } else {
    __atomic_store_n(args->hlArgs, 1, __ATOMIC_RELAXED);
    args->done = true;
    if (deviceKernel) {
      deviceAdaptor->deviceMemcpy(args->dlArgs, args->hlArgs, 1,
                                  flagcxMemcpyHostToDevice, resources->cpStream,
                                  NULL);
    }
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxSendProxyFree(sendNetResources *resources) {
  flagcxNetIb.deregMr(resources->netSendComm, resources->mhandles[0]);
  flagcxNetIb.closeSend(resources->netSendComm);
  deviceAdaptor->gdrMemFree(resources->buffers[0], NULL);
  for (int s = 0; s < MAXSTEPS; s++) {
    deviceAdaptor->eventDestroy(resources->cpEvents[s]);
  }
  deviceAdaptor->streamDestroy(resources->cpStream);
  return flagcxSuccess;
}

flagcxResult_t flagcxRecvProxyFree(recvNetResources *resources) {
  flagcxNetIb.deregMr(resources->netRecvComm, resources->mhandles[0]);
  flagcxNetIb.closeRecv(resources->netRecvComm);
  flagcxNetIb.closeListen(resources->netListenComm);
  deviceAdaptor->gdrMemFree(resources->buffers[0], NULL);
  for (int s = 0; s < MAXSTEPS; s++) {
    deviceAdaptor->eventDestroy(resources->cpEvents[s]);
  }
  deviceAdaptor->streamDestroy(resources->cpStream);
  return flagcxSuccess;
}
