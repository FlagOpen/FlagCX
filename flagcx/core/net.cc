#include "net.h"
#include "adaptor.h"
#include "device.h"
#include "proxy.h"
#include "reg_pool.h"

#include <errno.h>
#include <string.h>

static pthread_mutex_t netLock = PTHREAD_MUTEX_INITIALIZER;
// Use adaptor system for all network types
struct flagcxNetAdaptor *flagcxNetAdaptors[3] = {
    nullptr, getUnifiedNetAdaptor(IBRC), getUnifiedNetAdaptor(SOCKET)};
enum flagcxNetState {
  flagcxNetStateInit = 0,
  flagcxNetStateEnabled = 1,
  flagcxNetStateDisabled = 2
};
enum flagcxNetState flagcxNetStates[3] = {
    flagcxNetStateInit, flagcxNetStateInit, flagcxNetStateInit};

flagcxResult_t flagcxNetCheckDeviceVersion(struct flagcxHeteroComm *comm,
                                           struct flagcxNetAdaptor *net,
                                           int dev) {
  flagcxNetProperties_v8_t props;

  FLAGCXCHECK(net->getProperties(dev, (void *)&props));
  flagcxNetDeviceType type = props.netDeviceType;
  if (type)
    switch (type) {
      case FLAGCX_NET_DEVICE_UNPACK:
        if (props.netDeviceVersion == FLAGCX_NET_DEVICE_UNPACK_VERSION) {
          INFO(FLAGCX_INIT,
               "Using FLAGCX_NET_DEVICE_UNPACK net plugin version %d",
               props.netDeviceVersion);
          return flagcxSuccess;
        } else {
          WARN("FLAGCX_DEVICE_UNPACK plugin has incompatible version %d, this "
               "flagcx build is compatible with %d, not using it",
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

static flagcxResult_t netGetState(int i, enum flagcxNetState *state) {
  pthread_mutex_lock(&netLock);
  if (flagcxNetStates[i] == flagcxNetStateInit) {
    int ndev;
    if (flagcxNetAdaptors[i] == nullptr) {
      flagcxNetStates[i] = flagcxNetStateDisabled;
    } else if (flagcxNetAdaptors[i]->init() != flagcxSuccess) {
      flagcxNetStates[i] = flagcxNetStateDisabled;
    } else if (flagcxNetAdaptors[i]->devices(&ndev) != flagcxSuccess ||
               ndev <= 0) {
      flagcxNetStates[i] = flagcxNetStateDisabled;
    } else {
      flagcxNetStates[i] = flagcxNetStateEnabled;
    }
  }
  *state = flagcxNetStates[i];
  pthread_mutex_unlock(&netLock);
  return flagcxSuccess;
}

flagcxResult_t flagcxNetInit(struct flagcxHeteroComm *comm) {
  // Initialize main communication network
  const char *netName;
  bool ok = false;

  const char *forceSocketEnv = getenv("FLAGCX_FORCE_NET_SOCKET");
  bool forceSocket = (forceSocketEnv && atoi(forceSocketEnv) == 1);

  netName = comm->config.netName;

  if (forceSocket) {
    // Force socket network usage
    for (int i = 2; i >= 0; i--) {
      if (flagcxNetAdaptors[i] == nullptr)
        continue;
      if (flagcxNetAdaptors[i] != getUnifiedNetAdaptor(SOCKET))
        continue;
      enum flagcxNetState state;
      FLAGCXCHECK(netGetState(i, &state));
      if (state != flagcxNetStateEnabled)
        continue;
      if (netName && strcasecmp(netName, flagcxNetAdaptors[i]->name) != 0)
        continue;
      if (flagcxSuccess !=
          flagcxNetCheckDeviceVersion(comm, flagcxNetAdaptors[i], 0)) {
        continue;
      }

      comm->netAdaptor = flagcxNetAdaptors[i];
      ok = true;

      break;
    }
  } else {
    // Normal network selection order (IBRC/UCX first, then socket)
    for (int i = 0; i < 3; i++) {
      if (flagcxNetAdaptors[i] == nullptr)
        continue;
      enum flagcxNetState state;
      FLAGCXCHECK(netGetState(i, &state));
      if (state != flagcxNetStateEnabled)
        continue;
      if (netName && strcasecmp(netName, flagcxNetAdaptors[i]->name) != 0)
        continue;
      if (flagcxSuccess !=
          flagcxNetCheckDeviceVersion(comm, flagcxNetAdaptors[i], 0)) {
        continue;
      }

      comm->netAdaptor = flagcxNetAdaptors[i];
      ok = true;

      break;
    }
  }

  if (!ok) {
    WARN("Error: network %s not found.", netName ? netName : "");
    return flagcxInvalidUsage;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxProxySend(sendNetResources *resources, void *data,
                               size_t size, flagcxProxyArgs *args) {
  if (!__atomic_load_n(&args->hEventReady, __ATOMIC_RELAXED))
    return flagcxSuccess;

  // check if data is registered
  if (args->reg == NULL) {
    // globalRegPool.dump();
    args->reg = globalRegPool.getItem((uintptr_t)data);
    if (args->reg != NULL) {
      // INFO(FLAGCX_REG, "Data buffer found in reg pool: %p, size %zu", data,
      // size); if (reg->status == 0 && resources->netAdaptor ==
      // getUnifiedNetAdaptor(IBRC)) {
      if (args->reg->sendMrHandle == NULL &&
          resources->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
        // INFO(FLAGCX_REG, "send regMr with beginAddr=%lx, endAddr=%lx\n",
        // beginAddr, reg.endAddr);
        FLAGCXCHECK(resources->netAdaptor->regMr(
            resources->netSendComm, (void *)args->reg->beginAddr,
            (size_t)(args->reg->endAddr - args->reg->beginAddr), 2,
            &args->reg->sendMrHandle));
        // resources->netSendComm, data,
        // size, 2, &reg.sendMrHandle));
        // size, 2, &resources->mhandles[0]));
        // reg->status = 1;
        // } else if (reg.type == flagcxDeviceRdmaMemNative) {
        //   args->reg = &reg;
      }
    }
  }

  if (args->transmitted < args->chunkSteps) {
    if (args->reg == NULL) {
      int stepMask = args->sendStepMask;

      if (args->waitCopy < args->chunkSteps &&
          args->waitCopy - args->transmitted < MAXSTEPS) {
        int step = args->waitCopy & stepMask;
        args->subs[step].stepSize =
            std::min(args->chunkSize, size - args->totalCopySize);
        args->subs[step].stepBuff = resources->buffers[0] + (CHUNKSIZE * step);
        if (resources->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
          FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
              args->subs[step].stepBuff, (char *)data + args->totalCopySize,
              args->subs[step].stepSize, flagcxMemcpyDeviceToDevice,
              resources->cpStream, args->subs[step].copyArgs));
        } else if (resources->netAdaptor == getUnifiedNetAdaptor(SOCKET)) {
          FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
              args->subs[step].stepBuff, (char *)data + args->totalCopySize,
              args->subs[step].stepSize, flagcxMemcpyDeviceToHost,
              resources->cpStream, args->subs[step].copyArgs));
        }
        FLAGCXCHECK(deviceAdaptor->eventRecord(resources->cpEvents[step],
                                               resources->cpStream));
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
        resources->netAdaptor->isend(
            resources->netSendComm,
            args->subs[args->posted & stepMask].stepBuff,
            args->subs[args->posted & stepMask].stepSize, 0,
            resources->mhandles[0], NULL, &req);
        if (req) {
          args->subs[args->posted++ & stepMask].requests[0] = req;
        }
      }

      if (args->transmitted < args->posted) {
        void *req = args->subs[args->transmitted & stepMask].requests[0];
        int done = 0, sizes;
        resources->netAdaptor->test(req, &done, &sizes);
        if (done) {
          args->transmitted++;
        }
      }
    } else {
      // zero-copy send
      // INFO(FLAGCX_REG, "isend args->reg: %p", args->reg);
      if (args->posted < args->chunkSteps) {
        void *req = NULL;
        resources->netAdaptor->isend(resources->netSendComm, data, size, 0,
                                     args->reg->sendMrHandle, NULL, &req);
        if (req) {
          args->subs[0].requests[0] = req;
          args->posted = args->chunkSteps;
        }
      }

      if (args->posted == args->chunkSteps) {
        void *req = args->subs[0].requests[0];
        int done = 0, sizes;
        resources->netAdaptor->test(req, &done, &sizes);
        if (done) {
          args->transmitted = args->chunkSteps;
        }
      }
    }
  } else {
    if (args->done != 1) {
      __atomic_store_n(&args->hlArgs, 1, __ATOMIC_RELAXED);
      if (deviceAsyncLoad && deviceAsyncStore) {
        if (args->deviceFuncRelaxedOrdering == 1) {
          FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
              args->dlArgs, (void *)&args->hlArgs, sizeof(bool),
              flagcxMemcpyHostToDevice, resources->cpStream, NULL));
        }
      }
      args->done = 1;
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxProxyRecv(recvNetResources *resources, void *data,
                               size_t size, flagcxProxyArgs *args) {
  if (!__atomic_load_n(&args->hEventReady, __ATOMIC_RELAXED))
    return flagcxSuccess;

  // check if data is registered
  if (args->reg == NULL) {
    // globalRegPool.dump();
    args->reg = globalRegPool.getItem((uintptr_t)data);
    if (args->reg != NULL) {
      // INFO(FLAGCX_REG, "Data buffer found in reg pool: %p, size %zu", data,
      // size);
      // if (reg.type == flagcxDeviceRdmaMemRegistered && reg.status == 0 &&
      // resources->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
      // if (reg->status == 0 && resources->netAdaptor ==
      // getUnifiedNetAdaptor(IBRC)) {
      if (args->reg->recvMrHandle == NULL &&
          resources->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
        // INFO(FLAGCX_REG, "recv regMr with beginAddr=%lx, endAddr=%lx\n",
        // beginAddr, reg.endAddr);
        FLAGCXCHECK(resources->netAdaptor->regMr(
            resources->netRecvComm, (void *)args->reg->beginAddr,
            (size_t)(args->reg->endAddr - args->reg->beginAddr), 2,
            &args->reg->recvMrHandle));
        // resources->netRecvComm, data,
        // size, 2, &reg.recvMrHandle));
        // size, 2, &resources->mhandles[0]));
        // reg->status = 1;
        // } else if (reg.type == flagcxDeviceRdmaMemNative) {
        //   args->reg = &reg;
      }
    }
  }

  if (args->copied < args->chunkSteps) {
    if (args->reg == NULL) {
      int stepMask = args->sendStepMask;
      if (args->posted < args->chunkSteps &&
          args->posted - args->copied < MAXSTEPS) {
        int tags[8] = {0};
        void *req = NULL;
        args->subs[args->posted & stepMask].stepSize =
            std::min(args->chunkSize, size - args->totalPostSize);
        args->subs[args->posted & stepMask].stepBuff =
            resources->buffers[0] + CHUNKSIZE * (args->posted & stepMask);
        size_t stepSize = args->subs[args->posted & stepMask].stepSize;
        resources->netAdaptor->irecv(
            resources->netRecvComm, 1,
            &args->subs[args->posted & stepMask].stepBuff, &stepSize, tags,
            resources->mhandles, NULL, &req);
        if (req) {
          args->subs[args->posted & stepMask].requests[0] = req;
          args->totalPostSize += args->subs[args->posted++ & stepMask].stepSize;
        }
      }

      if (args->transmitted < args->posted) {
        void *req = args->subs[args->transmitted & stepMask].requests[0];
        int done = 0, sizes;
        resources->netAdaptor->test(req, &done, &sizes);
        if (done) {
          args->transmitted++;
        }
      }

      if (args->postFlush < args->transmitted) {
        if (resources->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
          void *req = NULL;
          void *allData[] = {args->subs[args->postFlush & stepMask].stepBuff};
          resources->netAdaptor->iflush(
              resources->netRecvComm, 1, allData,
              &args->subs[args->postFlush & stepMask].stepSize,
              resources->mhandles, &req);
          if (req) {
            args->subs[args->postFlush++ & stepMask].requests[0] = req;
          }
        } else if (resources->netAdaptor == getUnifiedNetAdaptor(SOCKET)) {
          args->subs[args->postFlush & stepMask].requests[0] = (void *)0x1;
          args->postFlush++;
        }
      }

      if (args->flushed < args->postFlush) {
        void *req = args->subs[args->flushed & stepMask].requests[0];
        int done = 0, sizes;
        if (resources->netAdaptor == getUnifiedNetAdaptor(SOCKET) &&
            req == (void *)0x1) {
          done = 1;
          sizes = 0;
        } else {
          resources->netAdaptor->test(req, &done, &sizes);
        }
        if (done) {
          args->flushed++;
        }
      }

      if (args->waitCopy < args->flushed) {
        int step = args->waitCopy & stepMask;
        if (resources->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
          FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
              (char *)data + args->totalCopySize, args->subs[step].stepBuff,
              args->subs[step].stepSize, flagcxMemcpyDeviceToDevice,
              resources->cpStream, args->subs[step].copyArgs));
        } else if (resources->netAdaptor == getUnifiedNetAdaptor(SOCKET)) {
          FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
              (char *)data + args->totalCopySize, args->subs[step].stepBuff,
              args->subs[step].stepSize, flagcxMemcpyHostToDevice,
              resources->cpStream, args->subs[step].copyArgs));
        }
        FLAGCXCHECK(deviceAdaptor->eventRecord(resources->cpEvents[step],
                                               resources->cpStream));
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
      // zero-copy recv
      // INFO(FLAGCX_REG, "irecv args->reg: %p", args->reg);
      if (args->posted < args->chunkSteps) {
        int tags[8] = {0};
        void *req = NULL;
        resources->netAdaptor->irecv(resources->netRecvComm, 1, &data, &size,
                                     tags, &args->reg->recvMrHandle, NULL,
                                     &req);
        if (req) {
          args->subs[0].requests[0] = req;
          args->posted = args->chunkSteps;
        }
      }

      if (args->transmitted < args->chunkSteps) {
        void *req = args->subs[0].requests[0];
        int done = 0, sizes;
        resources->netAdaptor->test(req, &done, &sizes);
        if (done) {
          args->transmitted = args->chunkSteps;
        }
      }

      // INFO(FLAGCX_REG, "iflush args->reg: %p", args->reg);
      if (args->postFlush < args->chunkSteps) {
        void *req = NULL;
        resources->netAdaptor->iflush(resources->netRecvComm, 1, &data,
                                      (int *)&size, &args->reg->recvMrHandle,
                                      &req);
        if (req) {
          args->subs[0].requests[0] = req;
          args->postFlush = args->chunkSteps;
        }
      }

      if (args->flushed < args->chunkSteps) {
        void *req = args->subs[0].requests[0];
        int done = 0, sizes;
        resources->netAdaptor->test(req, &done, &sizes);
        if (done) {
          args->flushed = args->chunkSteps;
          args->copied = args->chunkSteps;
        }
      }
    }
  } else {
    if (args->done != 1) {
      __atomic_store_n(&args->hlArgs, 1, __ATOMIC_RELAXED);
      if (deviceAsyncLoad && deviceAsyncStore) {
        if (args->deviceFuncRelaxedOrdering == 1) {
          FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
              args->dlArgs, (void *)&args->hlArgs, sizeof(bool),
              flagcxMemcpyHostToDevice, resources->cpStream, NULL));
        }
      }
      args->done = 1;
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxSendProxyFree(sendNetResources *resources) {
  for (int s = 0; s < MAXSTEPS; s++) {
    FLAGCXCHECK(deviceAdaptor->eventDestroy(resources->cpEvents[s]));
  }
  FLAGCXCHECK(deviceAdaptor->streamDestroy(resources->cpStream));
  // globalRegPool.dump();
  auto &regMap = globalRegPool.getMap();
  for (auto &reg : regMap) {
    // if (reg.second.type == flagcxDeviceRdmaMemRegistered &&
    // reg.second.sendMrHandle != NULL && resources->netAdaptor ==
    // getUnifiedNetAdaptor(IBRC)) {

    if (reg.second->sendMrHandle != NULL &&
        resources->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
      // globalRegPool.deRegisterBuffer((void *)reg.first);
      if (reg.second->refCount > 0)
        reg.second->refCount--;
      if (reg.second->refCount == 0) {
        FLAGCXCHECK(resources->netAdaptor->deregMr(resources->netSendComm,
                                                   reg.second->sendMrHandle));
        reg.second->sendMrHandle = NULL;
      }
      // reg.second.status = 0;
    }
  }
  // globalRegPool.dump();
  FLAGCXCHECK(resources->netAdaptor->deregMr(resources->netSendComm,
                                             resources->mhandles[0]));
  FLAGCXCHECK(resources->netAdaptor->closeSend(resources->netSendComm));
  FLAGCXCHECK(deviceAdaptor->gdrMemFree(resources->buffers[0], NULL));
  return flagcxSuccess;
}

flagcxResult_t flagcxRecvProxyFree(recvNetResources *resources) {
  for (int s = 0; s < MAXSTEPS; s++) {
    FLAGCXCHECK(deviceAdaptor->eventDestroy(resources->cpEvents[s]));
  }
  FLAGCXCHECK(deviceAdaptor->streamDestroy(resources->cpStream));
  auto &regMap = globalRegPool.getMap();
  for (auto &reg : regMap) {
    // if (reg.second.type == flagcxDeviceRdmaMemRegistered &&
    // reg.second.recvMrHandle != NULL && resources->netAdaptor ==
    // getUnifiedNetAdaptor(IBRC)) {
    if (reg.second->recvMrHandle != NULL &&
        resources->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
      // globalRegPool.deRegisterBuffer((void *)reg.first);
      if (reg.second->refCount > 0)
        reg.second->refCount--;
      if (reg.second->refCount == 0) {
        FLAGCXCHECK(resources->netAdaptor->deregMr(resources->netRecvComm,
                                                   reg.second->recvMrHandle));
        reg.second->recvMrHandle = NULL;
      }
      // reg.second.status = 0;
    }
  }
  FLAGCXCHECK(resources->netAdaptor->deregMr(resources->netRecvComm,
                                             resources->mhandles[0]));
  FLAGCXCHECK(resources->netAdaptor->closeRecv(resources->netRecvComm));
  FLAGCXCHECK(resources->netAdaptor->closeListen(resources->netListenComm));
  FLAGCXCHECK(deviceAdaptor->gdrMemFree(resources->buffers[0], NULL));
  return flagcxSuccess;
}