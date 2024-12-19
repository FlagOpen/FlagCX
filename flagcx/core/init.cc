/*************************************************************************
 * Copyright (c) 2015-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/
#include <string.h>
#include "flagcx.h"
#include "type.h"
#include "check.h"
#include "bootstrap.h"
#include "collectives.h"
#include "transport.h"
#include "group.h"
#include "net.h"
#include "topo.h"
#include "adaptor.h"

static bool initialized = false;
pthread_mutex_t initLock = PTHREAD_MUTEX_INITIALIZER;

struct flagcxCommInitRankAsyncJob {
  struct flagcxAsyncJob base;
  struct flagcxHeteroComm* comm;
  struct flagcxHeteroComm** newcomm;
  int cudaDev;
  // For flagcxCommInitRank
  int nranks, myrank;
  flagcxUniqueId commId;
  // for flagcxCommSplit
  struct flagcxHeteroComm* parent;
  int color, key;
};


flagcxResult_t flagcxHeteroGetVersion(int* version) {
 if (version == NULL) return flagcxInvalidArgument;
 *version = FLAGCX_VERSION(1,0,0);
 return flagcxSuccess;
}


static flagcxResult_t flagcxInit() {
  if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE)) return flagcxSuccess;
  pthread_mutex_lock(&initLock);
  if (!initialized) {
    FLAGCXCHECK(loadDeviceSymbol());
    FLAGCXCHECK(bootstrapNetInit());
    __atomic_store_n(&initialized, true, __ATOMIC_RELEASE);
  }
  pthread_mutex_unlock(&initLock);
  return flagcxSuccess;
}


flagcxResult_t flagcxHeteroGetUniqueId(flagcxUniqueId* out) {
  FLAGCXCHECK(flagcxInit());
  flagcxResult_t res = bootstrapGetUniqueId((struct flagcxBootstrapHandle*)out);
  return res;
}

static uint64_t hashUniqueId(flagcxUniqueId const &id) {
  char const *bytes = (char const*)&id;
  uint64_t h = 0xdeadbeef;
  for(int i=0; i < (int)sizeof(flagcxUniqueId); i++) {
    h ^= h >> 32;
    h *= 0x8db3db47fa2994ad;
    h += bytes[i];
  }
  return h;
}

static flagcxResult_t flagcxCommInitRankFunc(struct flagcxAsyncJob* job_) {
  struct flagcxCommInitRankAsyncJob* job = (struct flagcxCommInitRankAsyncJob*)job_;
  flagcxHeteroComm_t comm = job->comm;
  flagcxResult_t res = flagcxSuccess;

  if (!job->parent) {
    // New version of calling bootstrapInit
    struct bootstrapState* state;
    FLAGCXCHECK(flagcxCalloc(&state, 1));
    state->rank = comm->rank;
    state->nranks = comm->nRanks;
    state->abortFlag = comm->abortFlag;
    comm->bootstrap = state;
    state->magic = ((struct flagcxBootstrapHandle*)&job->commId)->magic;
    comm->magic = ((struct flagcxBootstrapHandle*)&job->commId)->magic;
    FLAGCXCHECKGOTO(bootstrapInit((struct flagcxBootstrapHandle*)&job->commId, state), res, fail);
  }

  if (!job->parent) {
    // Setting up proxy network
    int nranks = comm->nRanks;
    for(int i=0;i<MAXCHANNELS;i++){
      FLAGCXCHECK(flagcxCalloc(&comm->channels[i].peers, nranks));    
      for(int r=0;r<nranks;r++)
	    FLAGCXCHECK(flagcxCalloc(&comm->channels[i].peers[r], nranks));
    }
    FLAGCXCHECK(flagcxCalloc(&comm->connectSend, nranks));
    FLAGCXCHECK(flagcxCalloc(&comm->connectRecv, nranks));
    FLAGCXCHECK(flagcxCalloc(&comm->proxyState, 1));
    FLAGCXCHECK(flagcxCalloc(&comm->tasks.peers, nranks));
    FLAGCXCHECK(flagcxCalloc(&comm->tasks.p2pOrder, nranks));
    for(int i=0; i<MAXCHANNELS; i++){
      FLAGCXCHECK(flagcxCalloc(&comm->proxyState->proxyOps[i].consPeers, nranks));
      comm->proxyState->proxyOps[i].consNextChannel = reinterpret_cast<struct flagcxProxyOps*>(0x1);
      comm->proxyState->proxyOps[i].prodNextChannel = reinterpret_cast<struct flagcxProxyOps*>(0x1);
      pthread_mutex_init(&comm->proxyState->proxyOps[i].mutex, 0);
      for(int peer = 0; peer < nranks; peer++){
	      comm->proxyState->proxyOps[i].consPeers[peer].nextPeer = reinterpret_cast<struct flagcxProxyOps::consPeer *>(0x1);
      }
    }
  
    comm->groupNext = reinterpret_cast<struct flagcxHeteroComm*>(0x1);
    comm->preconnectNext = reinterpret_cast<struct flagcxHeteroComm*>(0x1);
    comm->proxyState->nRanks = comm->nRanks;

    FLAGCXCHECK(flagcxProxyInit(comm));
  }
  flagcxNetIb.init(NULL);
  flagcxGetLocalNetFromGpu(comm->cudaDev, &comm->netDev);

exit:
  return res;
fail:
  comm->initState = res;
  goto exit;
}

static flagcxResult_t flagcxCommInitRankDev(flagcxHeteroComm_t* newcomm, int nranks, flagcxUniqueId commId, int myrank, int cudaDev, flagcxConfig_t *config) {
  flagcxResult_t res = flagcxSuccess;
  flagcxHeteroComm_t comm = NULL;
  struct flagcxCommInitRankAsyncJob *job = NULL;
  const char* env = flagcxGetEnv("FLAGCX_COMM_ID");

  if (env && myrank == 0) {
    INFO(FLAGCX_ENV, "FLAGCX_COMM_ID set by environment to %s", env);
    FLAGCXCHECKGOTO(bootstrapCreateRoot((struct flagcxBootstrapHandle*)&commId, true), res, fail);
  }

  if (nranks < 1 || myrank < 0 || myrank >= nranks) {
    WARN("Invalid rank requested : %d/%d", myrank, nranks);
    res = flagcxInvalidArgument;
    goto fail;
  }

  FLAGCXCHECKGOTO(flagcxCalloc(&comm, 1), res, fail);
  comm->startMagic = comm->endMagic = FLAGCX_MAGIC; // Used to detect comm corruption.
  FLAGCXCHECKGOTO(flagcxCalloc((uint32_t**)&comm->abortFlagRefCount, 1), res, fail);
  *comm->abortFlagRefCount = 1;
  /* start with flagcxInternalError and will be changed to flagcxSuccess if init succeeds. */
  comm->initState = flagcxInternalError;
  comm->nRanks = nranks;
  comm->rank = myrank;
  comm->cudaDev = cudaDev;
  *newcomm = comm;

  FLAGCXCHECKGOTO(flagcxCalloc(&job, 1), res, fail);
  job->comm = comm;
  job->nranks = nranks;
  job->commId = commId; // C++ struct assignment
  job->myrank = myrank;
  job->cudaDev = cudaDev;
  flagcxCommInitRankFunc(&job->base);
  free(job);
exit:
  return flagcxGroupErrCheck(res);
fail:
  if (comm) {
    if (comm->abortFlagRefCount) free(comm->abortFlagRefCount);
    free(comm);
  }
  if (newcomm) *newcomm = NULL;
  goto exit;
}

flagcxResult_t flagcxHeteroCommInitRank(flagcxHeteroComm_t* newcomm, int nranks, flagcxUniqueId commId, int myrank) {
  FLAGCXCHECK(flagcxInit());
  int cudaDev = 0;
  flagcxConfig_t config;
  // flagcxGetDevice(&cudaDev);
  deviceAdaptor->getDevice(&cudaDev);
  FLAGCXCHECK(flagcxCommInitRankDev(newcomm, nranks, commId, myrank, cudaDev, &config));
  return flagcxSuccess;
}

flagcxResult_t flagcxHeteroCommCount(const flagcxHeteroComm_t comm, int* count) {
  *count = comm->nRanks;
  return flagcxSuccess;
}

flagcxResult_t flagcxHeteroCommUserRank(const flagcxHeteroComm_t comm, int* rank) {
  *rank = comm->rank;
  return flagcxSuccess;
}

flagcxResult_t flagcxHeteroCommDestroy(flagcxHeteroComm_t comm){
  flagcxProxyDestroy(comm);
  for(int i=0;i<MAXCHANNELS;i++){
    for(int r=0;r<comm->nRanks;r++){
      free(comm->channels[i].peers[r]);
    }
    free(comm->channels[i].peers);
  }
  for(int i=0; i<MAXCHANNELS; i++){
    free(comm->proxyState->proxyOps[i].consPeers);      
  }

  free(comm->connectSend);
  free(comm->connectRecv);
  free(comm->proxyState);
  free(comm->tasks.peers);
  free(comm->tasks.p2pOrder);
  free(comm->abortFlagRefCount);
  free(comm);

  return flagcxSuccess;
}
