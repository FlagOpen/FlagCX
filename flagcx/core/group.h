/*************************************************************************
 * Copyright (c) 2015-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef FLAGCX_GROUP_H_
#define FLAGCX_GROUP_H_

#include "assert.h"
#include "comm.h"

flagcxResult_t flagcxGroupErrCheck(flagcxResult_t ret);
void flagcxGroupCommJoin(struct flagcxHeteroComm *comm);
void flagcxGroupCommPreconnect(struct flagcxHeteroComm *comm);
flagcxResult_t flagcxGroupCommLeave(struct flagcxHeteroComm *comm);
flagcxResult_t flagcxGroupJobAbort(struct flagcxGroupJob *groupJob);
flagcxResult_t flagcxGroupJobComplete(struct flagcxGroupJob *groupJob);

typedef flagcxResult_t (*flagcxInitFunc_t)(flagcxHeteroComm_t *newcomm,
                                           int ndev, flagcxUniqueId commId,
                                           int myrank, int cudaDev);

flagcxResult_t flagcxAsyncInit(flagcxInitFunc_t func,
                               flagcxHeteroComm_t *newcomm, int ndev,
                               flagcxUniqueId commId, int myrank, int cudaDev);

typedef enum flagcxGroupJobState {
  flagcxGroupJobRunning = 0,
  flagcxGroupJobDone = 1,
  flagcxGroupJobJoined = 2,
} flagcxGroupJobState_t;

struct flagcxAsyncJob {
  struct flagcxAsyncJob *next;
  pthread_t thread;
  flagcxResult_t result;
  flagcxResult_t (*func)(struct flagcxAsyncJob *);
  void (*undo)(struct flagcxAsyncJob *);
  void (*destructor)(void *);
  flagcxGroupJobState_t state;
  volatile uint32_t *abortFlag;      /* point to comm abortFlag */
  volatile uint32_t *childAbortFlag; /* point to child abortFlag */
  flagcxHeteroComm_t comm;
};

flagcxResult_t
flagcxAsyncLaunch(struct flagcxAsyncJob *job,
                  flagcxResult_t (*func)(struct flagcxAsyncJob *),
                  void (*undo)(struct flagcxAsyncJob *),
                  void (*destructor)(void *), flagcxHeteroComm_t comm);

struct flagcxGroupJob {
  struct flagcxAsyncJob base;
  struct flagcxHeteroComm **groupCommHeadPtr;
  struct flagcxHeteroComm **groupCommPreconnectHeadPtr;
  flagcxResult_t *groupErrorPtr;
  volatile bool *abortFlagPtr;
  int *groupBlockingPtr;
  struct flagcxIntruQueue<struct flagcxAsyncJob, &flagcxAsyncJob::next>
      *asyncJobsPtr;
  bool initialized;
};

flagcxResult_t flagcxGroupStartInternal();
flagcxResult_t flagcxGroupEndInternal();
flagcxResult_t flagcxAsyncJobComplete(struct flagcxAsyncJob *job);

////////////////////////////////////////////////////////////////////////////////

extern __thread int flagcxGroupDepth; // depth of flagcxGroupStart nesting
extern __thread flagcxResult_t flagcxGroupError;
extern __thread struct flagcxHeteroComm *flagcxGroupCommHead;
extern __thread struct flagcxHeteroComm *flagcxGroupCommPreconnectHead;
extern __thread int flagcxGroupBlocking;
extern __thread struct flagcxGroupJob *flagcxGroupJobMainPtr;
extern __thread struct flagcxGroupJob flagcxGroupJobMain;
extern __thread struct flagcxIntruQueue<struct flagcxAsyncJob,
                                        &flagcxAsyncJob::next>
    flagcxAsyncJobs;

static flagcxResult_t groupLaunch(struct flagcxAsyncJob *job_);
flagcxResult_t flagcxHeteroGroupStart();
flagcxResult_t flagcxHeteroGroupEnd();

static inline void groupResetJobState() {
  flagcxGroupBlocking = 0;
  flagcxGroupJobMainPtr = NULL;
  flagcxGroupCommPreconnectHead = nullptr;
  flagcxGroupCommHead = nullptr;
  memset(&flagcxGroupJobMain, 0, sizeof(struct flagcxGroupJob));
  return;
}

static inline flagcxResult_t groupJobComplete(struct flagcxGroupJob *job) {
  flagcxResult_t ret = flagcxSuccess;
  if (job) {
    ret = flagcxAsyncJobComplete(&job->base);
    groupResetJobState();
  }
  return ret;
}

inline flagcxResult_t flagcxGroupStartInternal() {
  flagcxGroupDepth++;
  return flagcxSuccess;
}

inline flagcxResult_t flagcxGroupEndInternal() {
  flagcxResult_t ret = flagcxSuccess;
  flagcxGroupDepth--;
  if (flagcxGroupDepth < 0)
    return flagcxSystemError;
  if (flagcxGroupDepth == 0) {

    /**
     * TODO: do all jobs at Groups
     **/
    if (flagcxGroupCommPreconnectHead || flagcxGroupCommHead) {

      flagcxGroupJobMain.groupCommHeadPtr = &flagcxGroupCommHead;
      flagcxGroupJobMain.groupCommPreconnectHeadPtr =
          &flagcxGroupCommPreconnectHead;
      flagcxGroupJobMain.asyncJobsPtr = &flagcxAsyncJobs;
      flagcxGroupJobMain.initialized = true;
      flagcxGroupJobMainPtr = &flagcxGroupJobMain;

      FLAGCXCHECKGOTO(groupLaunch(&flagcxGroupJobMainPtr->base), ret, fail);
      groupResetJobState();
    }
  }

exit:
  return ret;
fail:
  /**
   * TODO: add groupCleanup()
   **/
  // groupCleanup(&flagcxGroupCommHead, &flagcxGroupCommPreconnectHead,
  // &flagcxAsyncJobs, &flagcxGroupError, &flagcxGroupBlocking,
  // &flagcxGroupJobAbortFlag, ret);
  goto exit;
}

inline flagcxResult_t flagcxGroupErrCheck(flagcxResult_t ret) {
  if (flagcxGroupDepth > 0) {
    if (ret != flagcxSuccess && ret != flagcxInProgress)
      flagcxGroupError = ret;
  }
  return ret;
}

// Add comm to this thread's group
inline void flagcxGroupCommJoin(struct flagcxHeteroComm *comm) {
  if (comm->groupNext == reinterpret_cast<struct flagcxHeteroComm *>(0x1)) {
    comm->groupNext = flagcxGroupCommHead;
    flagcxGroupCommHead = comm;
  }
}

// Add comm to this thread's group needing preconnect
inline void flagcxGroupCommPreconnect(struct flagcxHeteroComm *comm) {
  if (comm->preconnectNext ==
      reinterpret_cast<struct flagcxHeteroComm *>(0x1)) {
    comm->preconnectNext = flagcxGroupCommPreconnectHead;
    flagcxGroupCommPreconnectHead = comm;
  }
}

// Comm has left group
inline flagcxResult_t flagcxGroupCommLeave(struct flagcxHeteroComm *comm) {
  comm->groupNext = reinterpret_cast<struct flagcxHeteroComm *>(0x1);
  return flagcxSuccess;
}

#endif
