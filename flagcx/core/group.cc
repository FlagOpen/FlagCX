/*************************************************************************
 * Copyright (c) 2015-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "group.h"
#include "adaptor.h"
#include "assert.h"
#include "collectives.h"
#include "debug.h"
#include "launch_kernel.h"
#include "net.h"
#include "transport.h"
#include "type.h"
#include <pthread.h>
#include <queue>
#include <stdio.h>

__thread int flagcxGroupDepth = 0;
__thread bool flagcxGroupJobAbortFlag = false;
__thread struct flagcxHeteroComm *flagcxGroupCommHead = nullptr;
__thread struct flagcxHeteroComm *flagcxGroupCommPreconnectHead = nullptr;
__thread flagcxResult_t flagcxGroupError = flagcxSuccess;
__thread struct flagcxGroupJob *flagcxGroupJobMainPtr = NULL;
__thread struct flagcxGroupJob flagcxGroupJobMain;
__thread int flagcxGroupBlocking = 1; /* default mode */
__thread struct flagcxIntruQueue<struct flagcxAsyncJob, &flagcxAsyncJob::next>
    flagcxAsyncJobs;

flagcxResult_t flagcxHeteroGroupStart() {
  flagcxResult_t ret = flagcxSuccess;
  FLAGCXCHECK(flagcxGroupStartInternal());
  return ret;
}

flagcxResult_t flagcxHeteroGroupEnd() {
  flagcxResult_t ret = flagcxSuccess;
  FLAGCXCHECKGOTO(flagcxGroupEndInternal(), ret, exit);
exit:
  return ret;
}

struct flagcxPreconnectJob {
  struct flagcxAsyncJob base;
  struct flagcxHeteroComm *comm;
};

flagcxResult_t flagcxPreconnectFunc(struct flagcxAsyncJob *job_) {
  struct flagcxPreconnectJob *job = (struct flagcxPreconnectJob *)job_;
  struct flagcxHeteroComm *comm = job->comm;
  if (comm->proxyState->initialized == 0) {
    FLAGCXCHECK(flagcxProxyInit(comm));
  }
  FLAGCXCHECK(flagcxTransportP2pSetup(comm, NULL, 0));
  return flagcxSuccess;
}

/**
 * TODO: add proxy block to make sure the connect is commplite
 **/

void *flagcxAsyncJobMain(void *arg) {
  struct flagcxAsyncJob *job = (struct flagcxAsyncJob *)arg;
  // flagcxSetDevice(job->comm->cudaDev);
  deviceAdaptor->setDevice(job->comm->cudaDev);
  job->result = job->func(job);
  if (job->result != flagcxSuccess) {
    INFO(FLAGCX_INIT, "%s:%d -> %d [Async thread]", __FILE__, __LINE__,
         job->result);
  }
  __atomic_store_n(&job->state, flagcxGroupJobDone, __ATOMIC_RELEASE);
  return arg;
}

static flagcxResult_t groupLaunch(struct flagcxAsyncJob *job_) {
  flagcxResult_t ret = flagcxSuccess;
  // bool errorJobAbortFlag = false;
  struct flagcxGroupJob *gjob = (struct flagcxGroupJob *)job_;
  struct flagcxHeteroComm *groupCommHeadMain = *gjob->groupCommHeadPtr;

  struct flagcxHeteroComm *groupCommPreconnectHeadMain =
      *gjob->groupCommPreconnectHeadPtr;

  struct flagcxIntruQueue<struct flagcxAsyncJob, &flagcxAsyncJob::next>
      *asyncJobsMain = gjob->asyncJobsPtr;
  // volatile bool *groupAbortFlag = gjob->abortFlagPtr;

  std::queue<flagcxFuncArgs> funcQueue;
  // host func: {eventRecorded, hlArgs, NULL};
  // device func: {eventRecorded, hlArgs, dlArgs};
  std::queue<std::vector<void *>> argsQueue;

  // When relaxed ordering is enabled, the H2D copy is issued on cpStream
  // Otherwise, it shares commStream with the device function to guarantee
  // execution order
  int deviceFuncRelaxedOrdering = 0;
  const char *dfroStr = std::getenv("FLAGCX_DEVICE_FUNC_RELAXED_ORDERING");
  if (dfroStr) {
    if (std::stoi(dfroStr) == 1) {
      deviceFuncRelaxedOrdering = 1;
    } else {
      deviceFuncRelaxedOrdering = 0;
    }
  }

  if (groupCommPreconnectHeadMain != nullptr) {
    struct flagcxHeteroComm *comm = groupCommPreconnectHeadMain;
    do {
      struct flagcxPreconnectJob *job;
      FLAGCXCHECKGOTO(flagcxCalloc(&job, 1), ret, fail);
      job->base.func = flagcxPreconnectFunc;
      job->base.undo = nullptr;
      job->base.destructor = free;
      job->base.state = flagcxGroupJobRunning;
      job->base.abortFlag = comm->abortFlag;
      job->comm = job->base.comm = comm;
      flagcxIntruQueueEnqueue(asyncJobsMain, &job->base);

      struct flagcxHeteroComm *next = comm->preconnectNext;
      comm->preconnectNext = reinterpret_cast<struct flagcxHeteroComm *>(0x1);
      comm = next;
    } while (comm != nullptr);
  }

  if (!flagcxIntruQueueEmpty(asyncJobsMain)) {
    struct flagcxAsyncJob *job = flagcxIntruQueueHead(asyncJobsMain);
    do {
      SYSCHECKGOTO(
          pthread_create(&job->thread, nullptr, flagcxAsyncJobMain, job), ret,
          fail);
      job = job->next;
    } while (job != nullptr);

    job = flagcxIntruQueueHead(asyncJobsMain);
    do {
      pthread_join(job->thread, nullptr);
      job = job->next;
    } while (job != nullptr);

    if (ret != flagcxSuccess)
      goto fail;
  }

  if (groupCommHeadMain != nullptr) {
    struct flagcxHeteroComm *comm = groupCommHeadMain;
    do {
      flagcxTasks *tasks = &comm->tasks;
      for (int i = 0; i < tasks->p2pOrderSteps; i++) {
        int peer = tasks->p2pOrder[i];
        while (!flagcxIntruQueueEmpty(&tasks->peers[peer].sendQueue)) {
          flagcxTaskP2p *p2p =
              flagcxIntruQueueDequeue(&tasks->peers[peer].sendQueue);
          flagcxProxyOp *op;
          FLAGCXCHECK(flagcxCalloc(&op, 1));
          op->pattern = flagcxPatternSend;
          op->nbytes = p2p->bytes;
          op->recvbuff = (uint8_t *)p2p->buff;
          op->channelId = 0;
          op->root = peer;
          op->connection = comm->channels[op->channelId]
                               .peers[peer]
                               ->send[0]
                               .proxyConn.connection;
          op->args.chunkSize = CHUNKSIZE;
          op->args.chunkSteps = (p2p->bytes + CHUNKSIZE - 1) / (CHUNKSIZE);
          op->args.sendStepMask = MAXSTEPS - 1;
          op->args.deviceFuncRelaxedOrdering = deviceFuncRelaxedOrdering;
          op->stream = p2p->stream;
          FLAGCXCHECK(deviceAdaptor->eventCreate(&op->event));
          std::vector<void *> argList;
          if (deviceAsyncLoad && deviceAsyncStore) {
            FLAGCXCHECK(deviceAdaptor->deviceMalloc(
                (void **)&op->args.dlArgs, sizeof(bool), flagcxMemDevice,
                op->stream));
            FLAGCXCHECK(deviceAdaptor->deviceMalloc(
                (void **)&op->args.dEventReady, sizeof(bool), flagcxMemDevice,
                op->stream));
            FLAGCXCHECK(deviceAdaptor->launchDeviceFunc(
                op->stream, deviceAsyncStore, op->args.dEventReady));
            FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
                (void *)&op->args.hEventReady, (void *)op->args.dEventReady,
                sizeof(bool), flagcxMemcpyDeviceToHost, op->stream, NULL));
            argList = {(void *)&op->args.eventRecorded,
                       (void *)&op->args.hlArgs, (void *)op->args.dlArgs};
            funcQueue.push({op->stream, op->event, argList.data()});
          } else {
            FLAGCXCHECK(deviceAdaptor->launchHostFunc(
                op->stream, cpuAsyncStore, (void *)&op->args.hEventReady));
            argList = {(void *)&op->args.eventRecorded,
                       (void *)&op->args.hlArgs, NULL};
            funcQueue.push({op->stream, op->event, argList.data()});
          }
          argsQueue.push(std::move(argList));
          FLAGCXCHECK(flagcxProxySaveOp(comm, op));
          free(p2p);
        }
        while (!flagcxIntruQueueEmpty(&tasks->peers[peer].recvQueue)) {
          flagcxTaskP2p *p2p =
              flagcxIntruQueueDequeue(&tasks->peers[peer].recvQueue);
          flagcxProxyOp *op;
          FLAGCXCHECK(flagcxCalloc(&op, 1));
          op->pattern = flagcxPatternRecv;
          op->nbytes = p2p->bytes;
          op->recvbuff = (uint8_t *)p2p->buff;
          op->channelId = 0;
          op->root = peer;
          op->connection = comm->channels[op->channelId]
                               .peers[peer]
                               ->recv[0]
                               .proxyConn.connection;
          op->args.chunkSize = CHUNKSIZE;
          op->args.chunkSteps = (p2p->bytes + CHUNKSIZE - 1) / (CHUNKSIZE);
          op->args.sendStepMask = MAXSTEPS - 1;
          op->args.deviceFuncRelaxedOrdering = deviceFuncRelaxedOrdering;
          op->stream = p2p->stream;
          FLAGCXCHECK(deviceAdaptor->eventCreate(&op->event));
          std::vector<void *> argList;
          if (deviceAsyncLoad && deviceAsyncStore) {
            FLAGCXCHECK(deviceAdaptor->deviceMalloc(
                (void **)&op->args.dlArgs, sizeof(bool), flagcxMemDevice,
                op->stream));
            FLAGCXCHECK(deviceAdaptor->deviceMalloc(
                (void **)&op->args.dEventReady, sizeof(bool), flagcxMemDevice,
                op->stream));
            FLAGCXCHECK(deviceAdaptor->launchDeviceFunc(
                op->stream, deviceAsyncStore, op->args.dEventReady));
            FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
                (void *)&op->args.hEventReady, (void *)op->args.dEventReady,
                sizeof(bool), flagcxMemcpyDeviceToHost, op->stream, NULL));
            argList = {(void *)&op->args.eventRecorded,
                       (void *)&op->args.hlArgs, (void *)op->args.dlArgs};
            funcQueue.push({op->stream, op->event, argList.data()});
          } else {
            FLAGCXCHECK(deviceAdaptor->launchHostFunc(
                op->stream, cpuAsyncStore, (void *)&op->args.hEventReady));
            argList = {(void *)&op->args.eventRecorded,
                       (void *)&op->args.hlArgs, NULL};
            funcQueue.push({op->stream, op->event, argList.data()});
          }
          argsQueue.push(std::move(argList));
          FLAGCXCHECK(flagcxProxySaveOp(comm, op));
          free(p2p);
        }
      }
      comm->tasks.p2pOrderSteps = 0;
      comm = comm->groupNext;
    } while (comm != nullptr);
  }

  while (!funcQueue.empty()) {
    // get corresponding func args
    flagcxFuncArgs args = funcQueue.front();

    // launch host or device func
    if (deviceAsyncLoad && deviceAsyncStore) {
      if (deviceFuncRelaxedOrdering == 0) {
        bool *volatile hlArgs = (bool *)args.argList[1];
        while (!__atomic_load_n(hlArgs, __ATOMIC_RELAXED)) {
        }
        FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
            args.argList[2], args.argList[1], sizeof(bool),
            flagcxMemcpyHostToDevice, args.stream, NULL));
      }
      FLAGCXCHECK(deviceAdaptor->launchDeviceFunc(args.stream, deviceAsyncLoad,
                                                  args.argList[2]));
    } else {
      FLAGCXCHECK(deviceAdaptor->launchHostFunc(args.stream, cpuAsyncLoad,
                                                args.argList[1]));
    }

    // record func op
    FLAGCXCHECK(deviceAdaptor->eventRecord(args.event, args.stream));
    bool *volatile recorded = (bool *)args.argList[0];
    *recorded = true;

    funcQueue.pop();
    argsQueue.pop();
  }

  while (!flagcxIntruQueueEmpty(asyncJobsMain)) {
    struct flagcxAsyncJob *job = flagcxIntruQueueDequeue(asyncJobsMain);
    free(job);
  }

  while (groupCommHeadMain != nullptr) {
    struct flagcxHeteroComm *comm = groupCommHeadMain;
    struct flagcxHeteroComm *next = comm->groupNext;
    (void)flagcxGroupCommLeave(comm);
    groupCommHeadMain = next;
  }
exit:
  return ret;
fail:
  goto exit;
}
