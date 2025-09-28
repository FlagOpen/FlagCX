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

FLAGCX_PARAM(FuncNloops, "FUNC_NLOOPS", 1);
static int64_t funcNloops = flagcxParamFuncNloops();

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

  // only for device func, to be deprecated
  // host func: {eventRecorded, hlArgs, NULL};
  // device func: {eventRecorded, hlArgs, dlArgs};
  std::queue<flagcxFuncArgs> funcQueue;
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

  // Each groupLaunch we create a semaphore to track the p2p ops
  // and a stream to launch host or device func
  struct flagcxHostSemaphore *semaphore;
  FLAGCXCHECK(flagcxCalloc(&semaphore, 1));
  semaphore->flag = 0;
  semaphore->counter = 0;
  flagcxStream_t launchStream = nullptr;

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
    // post all send/recv tasks
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
          // launch proxyRegister op if not yet registered
          flagcxConnector *peerConns[] = {
              comm->channels[op->channelId].peers[peer]->send};
          FLAGCXCHECK(flagcxNetRegisterBuffer(
              comm, p2p->buff, p2p->bytes, peerConns, 1, &op->args.regBufFlag,
              &op->args.regHandle));
          FLAGCXCHECK(deviceAdaptor->eventCreate(&op->event));
          FLAGCXCHECK(deviceAdaptor->eventRecord(op->event, op->stream));
          // we don't use semaphore tracking for device func for the moment
          if (deviceAsyncLoad && deviceAsyncStore) {
            std::vector<void *> argList;
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
            argsQueue.push(std::move(argList));
          } else {
            op->args.semaphore = semaphore;
            semaphore->counter++;
            if (semaphore->counter == 1) {
              launchStream = op->stream;
            }
            FLAGCXCHECK(
                deviceAdaptor->streamWaitEvent(launchStream, op->event));
          }
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
          // launch proxyRegister op if not yet registered
          flagcxConnector *peerConns[] = {
              comm->channels[op->channelId].peers[peer]->recv};
          FLAGCXCHECK(flagcxNetRegisterBuffer(
              comm, p2p->buff, p2p->bytes, peerConns, 1, &op->args.regBufFlag,
              &op->args.regHandle));
          FLAGCXCHECK(deviceAdaptor->eventCreate(&op->event));
          FLAGCXCHECK(deviceAdaptor->eventRecord(op->event, op->stream));
          // we don't use semaphore tracking for device func for the moment
          if (deviceAsyncLoad && deviceAsyncStore) {
            std::vector<void *> argList;
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
            argsQueue.push(std::move(argList));
          } else {
            op->args.semaphore = semaphore;
            semaphore->counter++;
            if (semaphore->counter == 1) {
              launchStream = op->stream;
            }
            FLAGCXCHECK(
                deviceAdaptor->streamWaitEvent(launchStream, op->event));
          }
          FLAGCXCHECK(flagcxProxySaveOp(comm, op));
          free(p2p);
        }
      }
      comm->tasks.p2pOrderSteps = 0;
      comm = comm->groupNext;
    } while (comm != nullptr);
  }

  if (deviceAsyncLoad && deviceAsyncStore) {
    while (!funcQueue.empty()) {
      // get corresponding func args
      flagcxFuncArgs args = funcQueue.front();

      // launch device func
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

      // record func op
      FLAGCXCHECK(deviceAdaptor->eventRecord(args.event, args.stream));
      bool *volatile recorded = (bool *)args.argList[0];
      *recorded = true;

      // pop item
      funcQueue.pop();
      argsQueue.pop();
    }
  } else {
    FLAGCXCHECK(deviceAdaptor->launchHostFunc(launchStream, cpuAsyncKernel,
                                              (void *)semaphore));
  }
  // deprecated code path for host func, since the previous
  // hang issue may be walked around by using zero copy
  // else {
  //   for (int64_t i = 0; i < funcNloops; i++) {
  //     std::queue<flagcxFuncArgs> funcQueue_;
  //     std::queue<std::vector<void *>> argsQueue_;

  //     while (!funcQueue.empty()) {
  //       // get corresponding args
  //       flagcxFuncArgs args = funcQueue.front();
  //       auto argList = argsQueue.front();

  //       // launch host func
  //       if (i == funcNloops - 1) {
  //         FLAGCXCHECK(deviceAdaptor->launchHostFunc(args.stream,
  //         cpuAsyncLoad,
  //                                                   args.argList[1]));
  //         // record func op
  //         FLAGCXCHECK(deviceAdaptor->eventRecord(args.event, args.stream));
  //         bool *volatile recorded = (bool *)args.argList[0];
  //         *recorded = true;
  //       } else {
  //         FLAGCXCHECK(deviceAdaptor->launchHostFunc(
  //             args.stream, cpuAsyncLoadWithMaxSpinCount, args.argList[1]));
  //         // push item
  //         funcQueue_.push(std::move(args));
  //         argsQueue_.push(std::move(argList));
  //       }

  //       // pop item
  //       funcQueue.pop();
  //       argsQueue.pop();
  //     }

  //     // reset queues
  //     funcQueue = std::move(funcQueue_);
  //     argsQueue = std::move(argsQueue_);
  //   }
  // }

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
