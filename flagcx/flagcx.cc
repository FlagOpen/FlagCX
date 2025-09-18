#include "flagcx.h"
#include "adaptor.h"
#include "alloc.h"
#include "bootstrap.h"
#include "c2c_algo.h"
#include "check.h"
#include "cluster.h"
#include "comm.h"
#include "cost_model.h"
#include "flagcx_hetero.h"
#include "flagcx_tuner.h"
#include "param.h"

#include "launch_kernel.h"
#include <cassert>
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <cstring>
#define FLAGCX_CACHE_CAPACITY 16
static flagcxLRUCache<size_t, flagcxC2cPlanner>
    planCache(FLAGCX_CACHE_CAPACITY);

size_t getFlagcxDataTypeSize(flagcxDataType_t dtype) {
  switch (dtype) {
    // case flagcxInt8:
    case flagcxChar:
      return sizeof(char); // 1 byte
    case flagcxUint8:
      return sizeof(unsigned char); // 1 byte
    // case flagcxInt32:
    case flagcxInt:
      return sizeof(int); // 4 bytes
    case flagcxUint32:
      return sizeof(unsigned int); // 4 bytes
    case flagcxInt64:
      return sizeof(long long); // 8 bytes
    case flagcxUint64:
      return sizeof(unsigned long long); // 8 bytes
    // case flagcxFloat16:
    case flagcxHalf:
      return 2; // Half precision float is 2 bytes
    // case flagcxFloat32:
    case flagcxFloat:
      return sizeof(float); // 4 bytes
    // case flagcxFloat64:
    case flagcxDouble:
      return sizeof(double); // 8 bytes
    case flagcxBfloat16:
      return 2; // BFloat16 is typically 2 bytes
    default:
      fprintf(stderr, "Unknown flagcx data type\n");
      return 0;
  }
}

// Wrapper function for deviceMemcpy without the usage of invalid args
flagcxResult_t wrapper_deviceMemcpy(void *dst, void *src, size_t size,
                                    flagcxMemcpyType_t type,
                                    flagcxStream_t stream) {
  return deviceAdaptor->deviceMemcpy(dst, src, size, type, stream, NULL);
}

static struct flagcxDeviceHandle globalDeviceHandle {
  // Basic functions
  deviceAdaptor->deviceSynchronize, wrapper_deviceMemcpy,
      deviceAdaptor->deviceMemset, deviceAdaptor->deviceMalloc,
      deviceAdaptor->deviceFree, deviceAdaptor->setDevice,
      deviceAdaptor->getDevice, deviceAdaptor->getDeviceCount,
      deviceAdaptor->getVendor,
      // Stream functions
      deviceAdaptor->streamCreate, deviceAdaptor->streamDestroy,
      deviceAdaptor->streamCopy, deviceAdaptor->streamFree,
      deviceAdaptor->streamSynchronize, deviceAdaptor->streamQuery,
      deviceAdaptor->streamWaitEvent,
      // Event functions
      deviceAdaptor->eventCreate, deviceAdaptor->eventDestroy,
      deviceAdaptor->eventRecord, deviceAdaptor->eventSynchronize,
      deviceAdaptor->eventQuery,
};

flagcxResult_t flagcxEnsureCommReady(flagcxComm_t comm) {
  if (comm == NULL) {
    return flagcxInternalError;
  }
  if (comm->comm_type != flagcxCommunicatorHybrid &&
      comm->comm_type != flagcxCommunicatorHomo) {
    return flagcxInternalError;
  }
  return flagcxSuccess;
}

bool is_homo_comm(flagcxComm_t comm) {
#if defined(FORCE_HOMO_COMM)
  return true;
#elif defined(FORCE_HYBRID_COMM)
  return false;
#else
  return comm->comm_type == flagcxCommunicatorHomo;
#endif
}

bool use_host_comm() {
  char *useHostComm = getenv("FLAGCX_USE_HOST_COMM");
  if (useHostComm) {
    return std::stoi(useHostComm) == 1;
  }
  return false;
}

flagcxResult_t flagcxHandleInit(flagcxHandlerGroup_t *handler) {
  (*handler) = NULL;
  flagcxCalloc(handler, 1);
  flagcxCalloc(&(*handler)->uniqueId, 1);
  flagcxCalloc(&(*handler)->comm, 1);
  flagcxCalloc(&(*handler)->devHandle, 1);
  *(*handler)->devHandle = globalDeviceHandle;
  return flagcxSuccess;
}

flagcxResult_t flagcxHandleFree(flagcxHandlerGroup_t handler) {
  if (handler != NULL) {
    free(handler->uniqueId);
    free(handler->comm);
    free(handler->devHandle);
    handler->uniqueId = NULL;
    handler->comm = NULL;
    handler->devHandle = NULL;
    free(handler);
    handler = NULL;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxIsHomoComm(flagcxComm_t comm, int *isHomo) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    *isHomo = 1;
  } else {
    *isHomo = 0;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxGetVersion(int *version) {
  // TODO: implement a method to retrieve global verison
  return flagcxHeteroGetVersion(version);
}

flagcxResult_t flagcxGetUniqueId(flagcxUniqueId_t *uniqueId) {
  (*uniqueId) = NULL;
  flagcxCalloc(uniqueId, 1);

  // Init bootstrap net
  FLAGCXCHECK(bootstrapNetInit());

  // Init uniqueId using bootstrap
  struct flagcxBootstrapHandle handle;
  FLAGCXCHECK(bootstrapGetUniqueId(&handle));
  // flagcxUniqueId and bootstrapHandle don't have the same size and alignment
  // reset to 0 to avoid undefined data
  memset((void *)*uniqueId, 0, sizeof(**uniqueId));
  // copy to avoid alignment mismatch
  memcpy((void *)*uniqueId, &handle, sizeof(handle));
  return flagcxSuccess;
}

const char *flagcxGetErrorString(flagcxResult_t result) {
  // TODO: implement a method to retrieve error string
  return "Not implemented.";
}

const char *flagcxGetLastError(flagcxComm_t comm) {
  // TODO: implement a method to retrieve last error string
  if (comm == NULL) {
    return "Undefined: flagcxComm is not fully initialized.";
  }
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->getLastError(comm->homo_comm);
  }
  return "Not implemented.";
}

flagcxResult_t flagcxCommInitRank(flagcxComm_t *comm, int nranks,
                                  flagcxUniqueId_t commId, int rank) {
  if (nranks < 1 || rank < 0 || rank >= nranks) {
    WARN("Invalid rank requested : %d/%d", rank, nranks);
    return flagcxInvalidArgument;
  }

  (*comm) = NULL;
  flagcxCalloc(comm, 1);
  (*comm)->rank = rank;
  (*comm)->nranks = nranks;
  (*comm)->nclusters = -1;
  (*comm)->homo_rank = -1;
  (*comm)->homo_root_rank = -1;
  (*comm)->homo_ranks = -1;
  (*comm)->has_single_rank_homo_comm = -1;
  (*comm)->magic = 0;
  (*comm)->abortFlag = 0;
  (*comm)->bootstrap = NULL;
  (*comm)->host_comm = NULL;
  (*comm)->homo_comm = NULL;
  (*comm)->hetero_comm = NULL;
  (*comm)->cluster_ids = NULL;
  (*comm)->cluster_sizes = NULL;
  (*comm)->cluster_inter_ranks = NULL;
  (*comm)->globalrank2homorank = NULL;
  (*comm)->comm_type = flagcxCommunicatorUnknown;
  (*comm)->homoInterRootRank = -1;
  (*comm)->homoInterMyRank = -1;
  (*comm)->homoInterRanks = -1;
  (*comm)->homoInterComm = NULL;

  struct bootstrapState *state = NULL;
  FLAGCXCHECK(flagcxCalloc(&state, 1));
  state->rank = rank;
  state->nranks = nranks;
  state->abortFlag = (*comm)->abortFlag;
  (*comm)->bootstrap = state;
  state->magic = ((struct flagcxBootstrapHandle *)commId)->magic;
  (*comm)->magic = ((struct flagcxBootstrapHandle *)commId)->magic;

  // Init bootstrap net
  FLAGCXCHECK(bootstrapNetInit());

  // Init bootstrap state
  FLAGCXCHECK(bootstrapInit((struct flagcxBootstrapHandle *)commId, state));

  // Ready to detect heterogeneous/homogeneous communicator
  // Use bootstrap allgather to exchange Device info
  flagcxVendor *vendorData =
      NULL; // temp data used for device vendor gather operation.

  // Get current gpu vendor
  flagcxVendor vendor;
  deviceAdaptor->getVendor(vendor.internal);
  FLAGCXCHECK(flagcxCalloc(&vendorData, nranks));
  memcpy(vendorData + rank, &vendor, sizeof(flagcxVendor));
  FLAGCXCHECK(
      bootstrapAllGather(state, (void *)vendorData, sizeof(flagcxVendor)));
  FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));

  // Init cluster info
  int *globalRankToHomoRankData;
  int *clusterIdData;
  int *clusterInterRankData;
  FLAGCXCHECK(flagcxCalloc(&globalRankToHomoRankData, nranks));
  FLAGCXCHECK(flagcxCalloc(&clusterIdData, nranks));
  FLAGCXCHECK(flagcxCalloc(&clusterInterRankData, nranks));
  FLAGCXCHECK(flagcxCollectClusterInfos(
      vendorData, &(*comm)->comm_type, globalRankToHomoRankData + rank,
      &(*comm)->homo_root_rank, &(*comm)->homo_ranks, clusterIdData + rank,
      clusterInterRankData + rank, &(*comm)->nclusters, rank, nranks));
  FLAGCXCHECK(
      bootstrapAllGather(state, (void *)globalRankToHomoRankData, sizeof(int)));
  FLAGCXCHECK(bootstrapAllGather(state, (void *)clusterIdData, sizeof(int)));
  FLAGCXCHECK(
      bootstrapAllGather(state, (void *)clusterInterRankData, sizeof(int)));
  FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));
  (*comm)->homo_rank = globalRankToHomoRankData[rank];
  (*comm)->cluster_ids = clusterIdData;
  (*comm)->globalrank2homorank = globalRankToHomoRankData;

  // fill clusterVendorMap
  FLAGCXCHECK(flagcxFillClusterVendorInfo(vendorData, (*comm), clusterIdData,
                                          nranks, (*comm)->nclusters));

  int *clusterSizes;
  int *clusterInterRanks;
  FLAGCXCHECK(flagcxCalloc(&clusterSizes, (*comm)->nclusters));
  FLAGCXCHECK(flagcxCalloc(&clusterInterRanks, (*comm)->nclusters));
  for (int i = 0; i < (*comm)->nclusters; ++i) {
    clusterInterRanks[i] = -1;
  }

  int cid = 0;
  int sum = 0;
  for (int i = 0; i < nranks; ++i) {
    if (clusterIdData[i] == cid + 1) {
      clusterSizes[cid] = i - sum;
      cid += 1;
      sum = i;
    }
  }
  clusterSizes[cid] = nranks - sum;
  (*comm)->cluster_sizes = clusterSizes;

  for (int i = 0; i < nranks; ++i) {
    if (clusterInterRankData[i] != -1) {
      clusterInterRanks[clusterIdData[i]] = clusterInterRankData[i];
    }
  }
  (*comm)->cluster_inter_ranks = clusterInterRanks;

  int start = 0;
  if (clusterIdData[rank] >= 1) {
    for (int i = 0; i < clusterIdData[rank]; ++i) {
      start += clusterSizes[i];
    }
  }
  (*comm)->homo_inter_rank = clusterInterRanks[clusterIdData[rank]] - start;

  // Update comm has_single_rank_homo_comm
  for (int i = 0; i < (*comm)->nclusters; ++i) {
    if ((*comm)->cluster_sizes[i] == 1) {
      (*comm)->has_single_rank_homo_comm = 1;
    }
  }
  if ((*comm)->has_single_rank_homo_comm == -1) {
    (*comm)->has_single_rank_homo_comm = 0;
  }
  if ((*comm)->has_single_rank_homo_comm == 1 && is_homo_comm(*comm)) {
    // no need to record it for homo comm
    (*comm)->has_single_rank_homo_comm = 0;
  }

  flagcxUniqueId *uniqueIdData;
  FLAGCXCHECK(flagcxCalloc(&uniqueIdData, nranks));
  // Tuner init
  (*comm)->tuner = &internalTuner;
  FLAGCXCHECK((*comm)->tuner->init((*comm)->nranks, 0, flagcxDebugLog, &((*comm)->tunerContext)));
  int nConfigs = 0;
  FLAGCXCHECK((*comm)->tuner->getCandidateNumber((*comm)->tunerContext, &nConfigs));
  assert(nConfigs >= 1);
  (*comm)->homoCommMap.clear();
  for (int i = 0; i < nConfigs; ++i) {
    struct flagcxCommTag tag = {.tag = ""};
    flagcxInnerComm_t innerComm = NULL;
    FLAGCXCHECK((*comm)->tuner->setCandidate((*comm)->tunerContext, i, &tag));
    INFO(FLAGCX_INIT, "start to prepare communicator tag=%s(%d/%d)", tag.tag, i, nConfigs);
    // Note: The tuner only support homo comm optimization for now
    // Reset commId and homo root rank calls underlying GetUniqueId function for
    // initialization of homo communicator
    memset((void *)commId, 0, sizeof(*commId));
    if ((*comm)->homo_rank == 0) {
      cclAdaptors[flagcxCCLAdaptorDevice]->getUniqueId(&commId);
    }

    if ((*comm)->homo_rank == 0) {
      memcpy((void *)&uniqueIdData[rank], (void *)commId, sizeof(flagcxUniqueId));
    }
    FLAGCXCHECK(
        bootstrapAllGather(state, (void *)uniqueIdData, sizeof(flagcxUniqueId)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));

    memcpy((void *)commId, (void *)&uniqueIdData[(*comm)->homo_root_rank],
           sizeof(flagcxUniqueId));
    FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->commInitRank(
        &innerComm, (*comm)->homo_ranks, commId, (*comm)->homo_rank,
        NULL));

    // Insert item into homoCommMap
    (*comm)->homoCommMap[tag] = innerComm;
    // For backward compatible, also asign homo_comm field.
    (*comm)->homo_comm = innerComm;
  }

  if (!is_homo_comm(*comm)) {
    // Reset commId and hetero root rank calls flagcxHeteroGetUniqueId
    memset((void *)commId, 0, sizeof(flagcxUniqueId));
    memset((void *)uniqueIdData, 0, nranks * sizeof(flagcxUniqueId));
    if (rank == 0) {
      flagcxHeteroGetUniqueId(commId);
      memcpy((void *)&uniqueIdData[0], (void *)commId, sizeof(flagcxUniqueId));
    }
    FLAGCXCHECK(bootstrapAllGather(state, (void *)uniqueIdData,
                                   sizeof(flagcxUniqueId)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));

    memcpy((void *)commId, (void *)&uniqueIdData[0], sizeof(flagcxUniqueId));
    // call flagcxHeteroCommInitRank
    FLAGCXCHECK(
        flagcxHeteroCommInitRank(&(*comm)->hetero_comm, nranks, *commId, rank));

    // Init host cclAdaptor
    if (use_host_comm() || (*comm)->has_single_rank_homo_comm) {
      FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorHost]->commInitRank(
          &(*comm)->host_comm, nranks, commId, rank, state));
    }
  }

  if (!is_homo_comm(*comm)) {
    // Experimental for multi-nic support
    // Collect nic distance to ranks
    (*comm)->clusterInterRankList.resize((*comm)->nclusters);
    struct flagcxNicDistance *nicDistanceData;
    FLAGCXCHECK(flagcxCalloc(&nicDistanceData, nranks));
    const char *enableTopoDetect = flagcxGetEnv("FLAGCX_ENABLE_TOPO_DETECT");
    if (enableTopoDetect && strcmp(enableTopoDetect, "TRUE") ==
                                0) { // safety check nic distance is only
                                     // available after topo detection
      FLAGCXCHECK(flagcxGetNicDistance((*comm)->hetero_comm->topoServer, rank,
                                       nicDistanceData + rank));
    } else {
      nicDistanceData[rank].distance = rank % 2 + 1;
      nicDistanceData[rank].netGuid = rank; // give a dummy value
    }
    FLAGCXCHECK(bootstrapAllGather(state, (void *)nicDistanceData,
                                   sizeof(flagcxNicDistance)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));
    for (int i = 0; i < (*comm)->nclusters; ++i) {
      int minDistance = INT_MAX;
      std::unordered_map<int, std::vector<int>> nicDistanceToRanks;
      std::unordered_map<int, std::unordered_set<uint64_t>> nicDistanceToNic;
      for (int j = 0; j < nranks; ++j) {
        if (clusterIdData[j] != i) {
          continue;
        }
        int val = nicDistanceData[j].distance;
        uint64_t netGuid = nicDistanceData[j].netGuid;
        if (nicDistanceToNic[val].find(netGuid) ==
            nicDistanceToNic[val].end()) {
          nicDistanceToRanks[val].push_back(j);
          nicDistanceToNic[val].insert(netGuid);
        }
        minDistance = std::min(minDistance, val);
      }
      (*comm)->clusterInterRankList[i] =
          std::move(nicDistanceToRanks[minDistance]);
    }
    // Set homoInterMyRank, homoInterRootRank and homoInterRanks
    auto &myClusterInterRanks =
        (*comm)->clusterInterRankList[clusterIdData[rank]];
    for (size_t i = 0; i < myClusterInterRanks.size(); ++i) {
      if (rank == myClusterInterRanks[i]) {
        (*comm)->homoInterMyRank = i;
      }
    }
    if ((*comm)->homoInterMyRank != -1) {
      (*comm)->homoInterRootRank = myClusterInterRanks[0];
      (*comm)->homoInterRanks = myClusterInterRanks.size();
    }

    INFO(
        FLAGCX_INIT,
        "rank = %d, nranks = %d, nclusters = %d, cluster_id = %d, cluster_size "
        "= %d, "
        "cluster_inter_rank = %d, homo_rank = %d, homo_root_rank = %d, "
        "homo_inter_rank = %d, homo_ranks = %d, "
        "has_single_rank_homo_comm = %d, "
        "homoInterRootRank = %d, homoInterMyRank = %d, homoInterRanks = %d",
        rank, nranks, (*comm)->nclusters, (*comm)->cluster_ids[rank],
        (*comm)->cluster_sizes[(*comm)->cluster_ids[rank]],
        (*comm)->cluster_inter_ranks[(*comm)->cluster_ids[rank]],
        (*comm)->homo_rank, (*comm)->homo_root_rank, (*comm)->homo_inter_rank,
        (*comm)->homo_ranks, (*comm)->has_single_rank_homo_comm,
        (*comm)->homoInterRootRank, (*comm)->homoInterMyRank,
        (*comm)->homoInterRanks);

    // Experimental for multi-nic support
    // Reset commId and homo inter root rank calls underlying GetUniqueId
    // function for initialization of homo inter communicator
    memset((void *)commId, 0, sizeof(flagcxUniqueId));
    memset((void *)uniqueIdData, 0, nranks * sizeof(flagcxUniqueId));
    // Let homoInterRootRank call underlying GetUniqueId function
    // for initialization of homo inter communicator
    if (rank == (*comm)->homoInterRootRank) {
      cclAdaptors[flagcxCCLAdaptorDevice]->getUniqueId(&commId);
      memcpy((void *)&uniqueIdData[rank], (void *)commId,
             sizeof(flagcxUniqueId));
    }
    // Collect uniqueIdData globally
    FLAGCXCHECK(bootstrapAllGather(state, (void *)uniqueIdData,
                                   sizeof(flagcxUniqueId)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));
    // Call cclAdaptor->commInitRank
    if ((*comm)->homoInterRootRank != -1) {
      memcpy((void *)commId, (void *)&uniqueIdData[(*comm)->homoInterRootRank],
             sizeof(flagcxUniqueId));
      FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->commInitRank(
          &(*comm)->homoInterComm, (*comm)->homoInterRanks, commId,
          (*comm)->homoInterMyRank, NULL));
    }
    free(nicDistanceData);
    const char *deviceFuncPathEnv = flagcxGetEnv("FLAGCX_DEVICE_FUNC_PATH");
    if (deviceFuncPathEnv) {
      FLAGCXCHECK(loadKernelSymbol(deviceFuncPathEnv, "deviceAsyncLoad",
                                   &deviceAsyncLoad));
      FLAGCXCHECK(loadKernelSymbol(deviceFuncPathEnv, "deviceAsyncStore",
                                   &deviceAsyncStore));
      if (deviceAsyncLoad == NULL || deviceAsyncStore == NULL) {
        printf("Failed to load async kernel from %s\n", deviceFuncPathEnv);
        exit(1);
      }
    }
  }

  free(clusterInterRankData);
  free(uniqueIdData);
  free(vendorData);

  return flagcxSuccess;
}

flagcxResult_t flagcxCommFinalize(flagcxComm_t comm) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  FLAGCXCHECK(
      cclAdaptors[flagcxCCLAdaptorDevice]->commFinalize(comm->homo_comm));
  if (!is_homo_comm(comm)) {
    // TODO: to be implemented
    return flagcxNotSupported;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxCommDestroy(flagcxComm_t comm) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));

  // Destroy cluster info
  free(comm->cluster_ids);
  free(comm->cluster_sizes);
  free(comm->globalrank2homorank);

  // Destroy bootstrap state and net
  bootstrapClose(comm->bootstrap);

  if (!is_homo_comm(comm)) {
    // Destroy hetero comm
    FLAGCXCHECK(flagcxHeteroCommDestroy(comm->hetero_comm));
    // Destroy host comm
    if (use_host_comm()) {
      FLAGCXCHECK(
          cclAdaptors[flagcxCCLAdaptorHost]->commDestroy(comm->host_comm));
    }
  }

  // Destroy homo comms
  for (const auto & item : comm->homoCommMap) {
    FLAGCXCHECK(
      cclAdaptors[flagcxCCLAdaptorDevice]->commDestroy(item.second));
  }

  // Destroy tuner
  if (comm->tuner) {
    comm->tuner->destroy(comm->tunerContext);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxCommAbort(flagcxComm_t comm) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->commAbort(comm->homo_comm));
  if (!is_homo_comm(comm)) {
    // TODO: to be implemented.
    return flagcxNotSupported;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxCommResume(flagcxComm_t comm) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->commResume(comm->homo_comm));
  if (!is_homo_comm(comm)) {
    // TODO: to be implemented.
    return flagcxNotSupported;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxCommSuspend(flagcxComm_t comm) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  FLAGCXCHECK(
      cclAdaptors[flagcxCCLAdaptorDevice]->commSuspend(comm->homo_comm));
  if (!is_homo_comm(comm)) {
    // TODO: to be implemented.
    return flagcxNotSupported;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxCommCount(const flagcxComm_t comm, int *count) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->commCount(comm->homo_comm,
                                                          count);
  }
  return flagcxHeteroCommCount(comm->hetero_comm, count);
}

flagcxResult_t flagcxCommGetDeviceNumber(const flagcxComm_t comm, int *device) {
  return cclAdaptors[flagcxCCLAdaptorDevice]->commGetDeviceNumber(
      comm->homo_comm, device);
}

flagcxResult_t flagcxCommUserRank(const flagcxComm_t comm, int *rank) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->commUserRank(comm->homo_comm,
                                                             rank);
  }
  return flagcxHeteroCommUserRank(comm->hetero_comm, rank);
}

flagcxResult_t flagcxCommGetAsyncError(flagcxComm_t comm,
                                       flagcxResult_t asyncError) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->commGetAsyncError(
        comm->homo_comm, asyncError);
  }
  // TODO: to be implemented.
  return flagcxNotSupported;
}

flagcxResult_t flagcxBarrier(flagcxComm_t comm, flagcxStream_t stream) {
  void *barrierBuff;
  deviceAdaptor->deviceMalloc(&barrierBuff, comm->nranks, flagcxMemDevice,
                              stream);
  deviceAdaptor->deviceMemset(barrierBuff, 0, comm->nranks, flagcxMemDevice,
                              stream);
  flagcxAllReduce(barrierBuff, barrierBuff, comm->nranks, flagcxChar, flagcxMax,
                  comm, stream);
  deviceAdaptor->deviceFree(barrierBuff, flagcxMemDevice, stream);
  deviceAdaptor->streamSynchronize(stream);
  return flagcxSuccess;
}

flagcxResult_t flagcxReduce(const void *sendbuff, void *recvbuff, size_t count,
                            flagcxDataType_t datatype, flagcxRedOp_t op,
                            int root, flagcxComm_t comm,
                            flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->reduce(
        sendbuff, recvbuff, count, datatype, op, root, comm->homo_comm, stream);
  } else {
    char *useBootstrap = getenv("USE_BOOTSTRAP_CCL");
    if (useBootstrap) {
      // TODO: to be implemented.
      return flagcxNotSupported;
    }
    if (use_host_comm() || comm->has_single_rank_homo_comm) {
      // c2c validation
      if (comm->has_single_rank_homo_comm) {
        WARN("Host comm is required to perform C2C reduce op when "
             "comm->has_single_rank_homo_comm is True");
      }
      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      void *buff_out;
      size_t size = count * getFlagcxDataTypeSize(datatype);

      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost, NULL);
      deviceAdaptor->deviceMalloc(&buff_out, size, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: memcpy d2h
      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size,
                                  flagcxMemcpyDeviceToHost, NULL, NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      // step 3: reduce
      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->reduce(
          buff_in, buff_out, count, datatype, op, root, comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // step 4: memcpy h2d
      timers[TIMER_COLL_MEM_H2D] = clockNano();
      if (comm->rank == root) {
        deviceAdaptor->deviceMemcpy(recvbuff, buff_out, size,
                                    flagcxMemcpyHostToDevice, NULL, NULL);
      }
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      // step 5: free host buffer
      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s Reduce: rank %d nranks %d total %.2fms "
           "(memory alloc "
           "%.2fms, memory free %.2fms, memory d2h %.2fms, memory h2d %.2fms, "
           "comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6,
           timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);

      return flagcxSuccess;
    } else {
      // Experimental for multi-nic support
      // Construct flagcxC2cPlanner and find corresponding strategy
      flagcxC2cPlanner planner;
      auto hashValue = getC2cCommPatternHash(count, comm->cluster_ids[root],
                                             flagcxCommOpReduce, op, comm);
      if (!planCache.get(hashValue, planner)) {
        INFO(FLAGCX_COLL,
             "No available plan is found, create a new one with "
             "communication pattern "
             "(count, rootClsuterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, comm->cluster_ids[root], flagcxCommOpReduce, op,
             (size_t)((uintptr_t)comm), hashValue);
        planner =
            flagcxC2cPlanner(count, count, root, comm, flagcxCommOpReduce, op);
        planCache.put(hashValue, planner);
      } else {
        INFO(FLAGCX_COLL,
             "Found available plan with communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, comm->cluster_ids[root], flagcxCommOpReduce, op,
             (size_t)((uintptr_t)comm), hashValue);
      }
      FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, root, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxGather(const void *sendbuff, void *recvbuff, size_t count,
                            flagcxDataType_t datatype, int root,
                            flagcxComm_t comm, flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->gather(
        sendbuff, recvbuff, count, datatype, root, comm->homo_comm, stream);
  } else {
    if (use_host_comm() || comm->has_single_rank_homo_comm) {
      // c2c validation
      if (comm->has_single_rank_homo_comm) {
        WARN("Host comm is required to perform C2C gather op when "
             "comm->has_single_rank_homo_comm is True");
      }

      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      void *buff_out;
      size_t size = count * getFlagcxDataTypeSize(datatype);
      size_t totalSize = comm->nranks * size;
      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost, NULL);
      deviceAdaptor->deviceMalloc(&buff_out, totalSize, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: memcpy d2h
      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size,
                                  flagcxMemcpyDeviceToHost, NULL, NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      // step 3: gather
      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->gather(
          buff_in, buff_out, count, datatype, root, comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // step 4: memcpy h2d
      timers[TIMER_COLL_MEM_H2D] = clockNano();
      deviceAdaptor->deviceMemcpy(recvbuff, buff_out, totalSize,
                                  flagcxMemcpyHostToDevice, NULL, NULL);
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      // step 5: free host buffer
      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s gather: rank %d nranks %d total %.2fms "
           "(memory alloc "
           "%.2fms, memory free %.2fms, memory d2h %.2fms, memory h2d %.2fms, "
           "comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6,
           timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
    } else {
      // Experimental for multi-nic support
      // Construct flagcxC2cPlanner and find corresponding strategy
      flagcxC2cPlanner planner;
      auto hashValue = getC2cCommPatternHash(count, root, flagcxCommOpGather,
                                             flagcxRedNoOp, comm);
      if (!planCache.get(hashValue, planner)) {
        INFO(FLAGCX_COLL,
             "No available plan is found, create a new one with "
             "communication pattern "
             "(count, rootRank, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, root, flagcxCommOpGather, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
        planner = flagcxC2cPlanner(count, count * comm->nranks, root, comm,
                                   flagcxCommOpGather, flagcxRedNoOp);
        planCache.put(hashValue, planner);
      } else {
        INFO(FLAGCX_COLL,
             "Found available plan with communication pattern "
             "(count, rootRank, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, root, flagcxCommOpGather, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
      }
      FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, root, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxScatter(const void *sendbuff, void *recvbuff, size_t count,
                             flagcxDataType_t datatype, int root,
                             flagcxComm_t comm, flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->scatter(
        sendbuff, recvbuff, count, datatype, root, comm->homo_comm, stream);
  } else {
    if (use_host_comm() || comm->has_single_rank_homo_comm) {
      // c2c validation
      if (comm->has_single_rank_homo_comm) {
        WARN("Host comm is required to perform C2C scatter op when "
             "comm->has_single_rank_homo_comm is True");
      }

      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      void *buff_out;
      size_t size = count * getFlagcxDataTypeSize(datatype);
      size_t totalSize = comm->nranks * size;
      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, totalSize, flagcxMemHost, NULL);
      deviceAdaptor->deviceMalloc(&buff_out, size, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: memcpy d2h
      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff),
                                  totalSize, flagcxMemcpyDeviceToHost, NULL,
                                  NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      // step 3: scatter
      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->scatter(
          buff_in, buff_out, count, datatype, root, comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // step 4: memcpy h2d
      timers[TIMER_COLL_MEM_H2D] = clockNano();
      deviceAdaptor->deviceMemcpy(recvbuff, buff_out, size,
                                  flagcxMemcpyHostToDevice, NULL, NULL);
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      // step 5: free host buffer
      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s Scatter: rank %d nranks %d total %.2fms "
           "(memory alloc "
           "%.2fms, memory free %.2fms, memory d2h %.2fms, memory h2d %.2fms, "
           "comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6,
           timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
    } else {
      // Experimental for multi-nic support
      // Construct flagcxC2cPlanner and find corresponding strategy
      flagcxC2cPlanner planner;
      auto hashValue = getC2cCommPatternHash(count, root, flagcxCommOpScatter,
                                             flagcxRedNoOp, comm);
      if (!planCache.get(hashValue, planner)) {
        INFO(FLAGCX_COLL,
             "No available plan is found, create a new one with "
             "communication pattern "
             "(count, rootRank, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, root, flagcxCommOpScatter, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
        planner = flagcxC2cPlanner(count * comm->nranks, count, root, comm,
                                   flagcxCommOpScatter, flagcxRedNoOp);
        planCache.put(hashValue, planner);
      } else {
        INFO(FLAGCX_COLL,
             "Found available plan with communication pattern "
             "(count, rootRank, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, root, flagcxCommOpScatter, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
      }
      FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, root, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxBroadcast(const void *sendbuff, void *recvbuff,
                               size_t count, flagcxDataType_t datatype,
                               int root, flagcxComm_t comm,
                               flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->broadcast(
        sendbuff, recvbuff, count, datatype, root, comm->homo_comm, stream);
  } else {
    if (use_host_comm() || comm->has_single_rank_homo_comm) {
      // c2c validation
      if (comm->has_single_rank_homo_comm) {
        WARN("Host comm is required to perform C2C broadcast op when "
             "comm->has_single_rank_homo_comm is True");
      }

      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      void *buff_out;
      size_t size = count * getFlagcxDataTypeSize(datatype);

      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost, NULL);
      deviceAdaptor->deviceMalloc(&buff_out, size, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: memcpy d2h
      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size,
                                  flagcxMemcpyDeviceToHost, NULL, NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      // step 3: broadcast
      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->broadcast(
          buff_in, buff_out, count, datatype, root, comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // step 4: memcpy h2d
      timers[TIMER_COLL_MEM_H2D] = clockNano();
      deviceAdaptor->deviceMemcpy(recvbuff, buff_out, size,
                                  flagcxMemcpyHostToDevice, NULL, NULL);
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      // step 5: free host buffer
      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s Broadcast: rank %d nranks %d total %.2fms "
           "(memory alloc "
           "%.2fms, memory free %.2fms, memory d2h %.2fms, memory h2d %.2fms, "
           "comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6,
           timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
    } else {
      // Experimental for multi-nic support
      // Construct flagcxC2cPlanner and find corresponding strategy
      flagcxC2cPlanner planner;
      auto hashValue =
          getC2cCommPatternHash(count, comm->cluster_ids[root],
                                flagcxCommOpBroadcast, flagcxRedNoOp, comm);
      if (!planCache.get(hashValue, planner)) {
        INFO(FLAGCX_COLL,
             "No available plan is found, create a new one with "
             "communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, comm->cluster_ids[root], flagcxCommOpBroadcast,
             flagcxRedNoOp, (size_t)((uintptr_t)comm), hashValue);
        planner = flagcxC2cPlanner(count, count, root, comm,
                                   flagcxCommOpBroadcast, flagcxRedNoOp);
        planCache.put(hashValue, planner);
      } else {
        INFO(FLAGCX_COLL,
             "Found available plan with communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, comm->cluster_ids[root], flagcxCommOpBroadcast,
             flagcxRedNoOp, (size_t)((uintptr_t)comm), hashValue);
      }
      FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, root, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxAllReduce(const void *sendbuff, void *recvbuff,
                               size_t count, flagcxDataType_t datatype,
                               flagcxRedOp_t op, flagcxComm_t comm,
                               flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    struct flagcxCommTag tag = {.tag = ""};
    FLAGCXCHECK(comm->tuner->getCollInfo(comm->tunerContext, flagcxCommOpAllReduce, count * getFlagcxDataTypeSize(datatype), 0, NULL, 0, &tag));
    const auto it = comm->homoCommMap.find(tag);
    assert(it != comm->homoCommMap.end());
    flagcxInnerComm_t innerComm = it->second;
    assert(innerComm != NULL);
    return cclAdaptors[flagcxCCLAdaptorDevice]->allReduce(
        sendbuff, recvbuff, count, datatype, op, innerComm, stream);
  } else {
    if (use_host_comm() || comm->has_single_rank_homo_comm) {
      // c2c validation
      if (comm->has_single_rank_homo_comm) {
        WARN("Host comm is required to perform C2C allreduce op when "
             "comm->has_single_rank_homo_comm is True");
      }

      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      void *buff_out;
      size_t size = count * getFlagcxDataTypeSize(datatype);

      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost, NULL);
      deviceAdaptor->deviceMalloc(&buff_out, size, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: memcpy d2h
      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size,
                                  flagcxMemcpyDeviceToHost, NULL, NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      // step 3: allreduce
      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->allReduce(
          buff_in, buff_out, count, datatype, op, comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // step 4: memcpy h2d
      timers[TIMER_COLL_MEM_H2D] = clockNano();
      deviceAdaptor->deviceMemcpy(recvbuff, buff_out, size,
                                  flagcxMemcpyHostToDevice, NULL, NULL);
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      // step 5: free host buffer
      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s AllReduce: rank %d nranks %d total %.2fms "
           "(memory alloc "
           "%.2fms, memory free %.2fms, memory d2h %.2fms, memory h2d %.2fms, "
           "comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6,
           timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
    } else {
      // Experimental for multi-nic support
      // Construct flagcxC2cPlanner and find corresponding strategy
      flagcxC2cPlanner planner;
      auto hashValue = getC2cCommPatternHash(
          count, comm->nclusters, flagcxCommOpAllReduce, op,
          comm); // use nclusters as rootClusterId for hash
      if (!planCache.get(hashValue, planner)) {
        INFO(FLAGCX_COLL,
             "No available plan is found, create a new one with "
             "communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, comm->nclusters, flagcxCommOpAllReduce, op,
             (size_t)((uintptr_t)comm), hashValue);
        planner =
            flagcxC2cPlanner(count, count, -1, comm, flagcxCommOpAllReduce, op);
        planCache.put(hashValue, planner);
        // TODO: add estimator part
        // flagcxAlgoTimeEstimator estimator(planner, datatype);
        // float time = 0.0;
        // FLAGCXCHECK(estimator.getAlgoTime(&time));
      } else {
        INFO(FLAGCX_COLL,
             "Found available plan with communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, comm->nclusters, flagcxCommOpAllReduce, op,
             (size_t)((uintptr_t)comm), hashValue);
      }
      FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxReduceScatter(const void *sendbuff, void *recvbuff,
                                   size_t recvcount, flagcxDataType_t datatype,
                                   flagcxRedOp_t op, flagcxComm_t comm,
                                   flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->reduceScatter(
        sendbuff, recvbuff, recvcount, datatype, op, comm->homo_comm, stream);
  } else {
    if (use_host_comm() || comm->has_single_rank_homo_comm) {
      // c2c validation
      if (comm->has_single_rank_homo_comm) {
        WARN("Host comm is required to perform C2C reducescatter op when "
             "comm->has_single_rank_homo_comm is True");
      }

      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      void *buff_out;
      size_t recv_size = recvcount * getFlagcxDataTypeSize(datatype);
      size_t send_size = comm->nranks * recv_size;

      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, send_size, flagcxMemHost, NULL);
      deviceAdaptor->deviceMalloc(&buff_out, recv_size, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: memcpy d2h
      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff),
                                  send_size, flagcxMemcpyDeviceToHost, NULL,
                                  NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      // step 3: reducescatter
      timers[TIMER_COLL_COMM] = clockNano();
      FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorHost]->reduceScatter(
          buff_in, buff_out, recvcount, datatype, op, comm->host_comm, NULL));
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // step 4: memcpy h2d
      timers[TIMER_COLL_MEM_H2D] = clockNano();
      deviceAdaptor->deviceMemcpy(recvbuff, buff_out, recv_size,
                                  flagcxMemcpyHostToDevice, NULL, NULL);
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      // step 5: free host buffer
      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s ReduceScatter: rank %d nranks %d total %.2fms "
           "(memory alloc "
           "%.2fms, memory free %.2fms, memory d2h %.2fms, memory h2d %.2fms, "
           "comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6,
           timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
    } else {
      // Experimental for multi-nic support
      // Construct flagcxC2cPlanner and find corresponding strategy
      flagcxC2cPlanner planner;
      auto hashValue = getC2cCommPatternHash(
          recvcount, comm->nclusters, flagcxCommOpReduceScatter, op,
          comm); // use nclusters as rootClusterId for hash
      if (!planCache.get(hashValue, planner)) {
        INFO(FLAGCX_COLL,
             "No available plan is found, create a new one with "
             "communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             recvcount, comm->nclusters, flagcxCommOpReduceScatter, op,
             (size_t)((uintptr_t)comm), hashValue);
        planner = flagcxC2cPlanner(comm->nranks * recvcount, recvcount, -1,
                                   comm, flagcxCommOpReduceScatter, op);
        planCache.put(hashValue, planner);
      } else {
        INFO(FLAGCX_COLL,
             "Found available plan with communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             recvcount, comm->nclusters, flagcxCommOpReduceScatter, op,
             (size_t)((uintptr_t)comm), hashValue);
      }
      FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxAllGather(const void *sendbuff, void *recvbuff,
                               size_t sendcount, flagcxDataType_t datatype,
                               flagcxComm_t comm, flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->allGather(
        sendbuff, recvbuff, sendcount, datatype, comm->homo_comm, stream);
  } else {
    if (use_host_comm()) {
      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      void *buff_out;
      size_t size = sendcount * getFlagcxDataTypeSize(datatype);
      size_t totalSize = comm->nranks * size;

      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost, NULL);
      deviceAdaptor->deviceMalloc(&buff_out, totalSize, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: memcpy d2h
      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size,
                                  flagcxMemcpyDeviceToHost, NULL, NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      // step 3: allgather
      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->allGather(
          buff_in, buff_out, sendcount, datatype, comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // step 4: memcpy h2d
      timers[TIMER_COLL_MEM_H2D] = clockNano();
      deviceAdaptor->deviceMemcpy(recvbuff, buff_out, totalSize,
                                  flagcxMemcpyHostToDevice, NULL, NULL);
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      // step 5: free host buffer
      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s AllGather: rank %d nranks %d total %.2fms "
           "(memory alloc "
           "%.2fms, memory free %.2fms, memory d2h %.2fms, memory h2d %.2fms, "
           "comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6,
           timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
    } else {
      // Experimental for multi-nic support
      // Construct flagcxC2cPlanner and find corresponding strategy
      flagcxC2cPlanner planner;
      auto hashValue = getC2cCommPatternHash(
          sendcount, comm->nclusters,
          flagcxCommOpAllGather, // use nclusters as rootClusterId for hash
          flagcxRedNoOp, comm);
      if (!planCache.get(hashValue, planner)) {
        INFO(FLAGCX_COLL,
             "No available plan is found, create a new one with "
             "communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             sendcount, comm->nclusters, flagcxCommOpAllGather, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
        planner = flagcxC2cPlanner(sendcount, sendcount * comm->nranks, -1,
                                   comm, flagcxCommOpAllGather, flagcxRedNoOp);
        planCache.put(hashValue, planner);
      } else {
        INFO(FLAGCX_COLL,
             "Found available plan with communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             sendcount, comm->nclusters, flagcxCommOpAllGather, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
      }
      FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxAlltoAll(const void *sendbuff, void *recvbuff,
                              size_t count, flagcxDataType_t datatype,
                              flagcxComm_t comm, flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->alltoAll(
        sendbuff, recvbuff, count, datatype, comm->homo_comm, stream);
  } else {
    if (use_host_comm()) {
      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      void *buff_out;
      size_t size = comm->nranks * count * getFlagcxDataTypeSize(datatype);

      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost, NULL);
      deviceAdaptor->deviceMalloc(&buff_out, size, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: memcpy d2h
      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size,
                                  flagcxMemcpyDeviceToHost, NULL, NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      // step 3: alltoall
      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->alltoAll(
          buff_in, buff_out, count, datatype, comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // step 4: memcpy h2d
      timers[TIMER_COLL_MEM_H2D] = clockNano();
      deviceAdaptor->deviceMemcpy(recvbuff, buff_out, size,
                                  flagcxMemcpyHostToDevice, NULL, NULL);
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      // step 5: free host buffer
      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s AlltoAll: rank %d nranks %d total %.2fms "
           "(memory alloc "
           "%.2fms, memory free %.2fms, memory d2h %.2fms, memory h2d %.2fms, "
           "comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6,
           timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
    } else {
      // Move it into flagcxC2cPlanner workflow
      flagcxC2cPlanner planner;
      auto hashValue =
          getC2cCommPatternHash(count, 1, // use 1 as rootClusterId for hash
                                flagcxCommOpAlltoAll, flagcxRedNoOp, comm);
      if (!planCache.get(hashValue, planner)) {
        INFO(FLAGCX_COLL,
             "No available plan is found, create a new one with "
             "communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, 1, flagcxCommOpAlltoAll, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
        planner = flagcxC2cPlanner(count, count, -1, comm, flagcxCommOpAlltoAll,
                                   flagcxRedNoOp);
        planCache.put(hashValue, planner);
      } else {
        INFO(FLAGCX_COLL,
             "Found available plan with communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             count, 1, flagcxCommOpAlltoAll, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
      }
      FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxAlltoAllv(const void *sendbuff, size_t *sendcounts,
                               size_t *sdispls, void *recvbuff,
                               size_t *recvcounts, size_t *rdispls,
                               flagcxDataType_t datatype, flagcxComm_t comm,
                               flagcxStream_t stream) {

  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->alltoAllv(
        sendbuff, sendcounts, sdispls, recvbuff, recvcounts, rdispls, datatype,
        comm->homo_comm, stream);
  } else {
    if (use_host_comm()) {
      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      void *buff_out;

      // Calculate max possible size needed for send and receive buffers
      size_t max_send_size = 0, max_recv_size = 0, send_size = 0, recv_size = 0;
      for (int i = 0; i < comm->nranks; i++) {
        send_size =
            (sendcounts[i] + sdispls[i]) * getFlagcxDataTypeSize(datatype);
        recv_size =
            (recvcounts[i] + rdispls[i]) * getFlagcxDataTypeSize(datatype);
        if (send_size > max_send_size)
          max_send_size = send_size;
        if (recv_size > max_recv_size)
          max_recv_size = recv_size;
      }
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, max_send_size, flagcxMemHost, NULL);
      deviceAdaptor->deviceMalloc(&buff_out, max_recv_size, flagcxMemHost,
                                  NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff),
                                  max_send_size, flagcxMemcpyDeviceToHost, NULL,
                                  NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->alltoAllv(
          buff_in, sendcounts, sdispls, buff_out, recvcounts, rdispls, datatype,
          comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      timers[TIMER_COLL_MEM_H2D] = clockNano();
      deviceAdaptor->deviceMemcpy(recvbuff, buff_out, max_recv_size,
                                  flagcxMemcpyHostToDevice, NULL, NULL);
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s AlltoAllv: rank %d nranks %d total %.2fms "
           "(memory alloc %.2fms, memory free %.2fms, memory d2h %.2fms, "
           "memory h2d %.2fms, comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6,
           timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
    } else {
      // Move it into flagcxC2cPlanner workflow
      flagcxC2cPlanner planner;
      auto hashValue = getC2cCommPatternHash(
          1, 1, // use 1 both as count and rootClusterId for hash
          flagcxCommOpAlltoAllv, flagcxRedNoOp, comm);
      if (!planCache.get(hashValue, planner)) {
        INFO(FLAGCX_COLL,
             "No available plan is found, create a new one with "
             "communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%d, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             1, 1, flagcxCommOpAlltoAllv, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
        planner = flagcxC2cPlanner(1, 1, -1, comm, flagcxCommOpAlltoAllv,
                                   flagcxRedNoOp);
        planCache.put(hashValue, planner);
      } else {
        INFO(FLAGCX_COLL,
             "Found available plan with communication pattern "
             "(count, rootClusterId, commOp, redOp, comm) = (%d, %d, %d, %d, "
             "%ld), hashValue = "
             "%ld",
             1, 1, flagcxCommOpAlltoAllv, flagcxRedNoOp,
             (size_t)((uintptr_t)comm), hashValue);
      }
      FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream,
                                  sendcounts, sdispls, recvcounts, rdispls));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxSend(const void *sendbuff, size_t count,
                          flagcxDataType_t datatype, int peer,
                          flagcxComm_t comm, flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->send(
        sendbuff, count, datatype, peer, comm->homo_comm, stream);
  } else {
    if (use_host_comm()) {
      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_in;
      size_t size = count * getFlagcxDataTypeSize(datatype);

      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: memcpy d2h
      timers[TIMER_COLL_MEM_D2H] = clockNano();
      deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size,
                                  flagcxMemcpyDeviceToHost, NULL, NULL);
      timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

      // step 3: send
      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->send(buff_in, count, datatype, peer,
                                              comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // buff_in will be freed in gloo adaptor send function?
      // TODO: check if buff_in should be freed here
      // deviceAdaptor->deviceFree(buff_in, flagcxMemHost, NULL);

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s Send: rank %d nranks %d total %.2fms (memory "
           "alloc "
           "%.2fms, memory d2h %.2fms, comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_MEM_D2H] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
    } else {
      FLAGCXCHECK(flagcxHeteroSend(sendbuff, count, datatype, peer,
                                   comm->hetero_comm, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxRecv(void *recvbuff, size_t count,
                          flagcxDataType_t datatype, int peer,
                          flagcxComm_t comm, flagcxStream_t stream) {
  FLAGCXCHECK(flagcxEnsureCommReady(comm));
  if (is_homo_comm(comm)) {
    return cclAdaptors[flagcxCCLAdaptorDevice]->recv(
        recvbuff, count, datatype, peer, comm->homo_comm, stream);
  } else {
    if (use_host_comm()) {
      uint64_t timers[TIMERS_COLL_COUNT] = {0};
      timers[TIMER_COLL_TOTAL] = clockNano();
      void *buff_out;
      size_t size = count * getFlagcxDataTypeSize(datatype);

      // step 1: malloc host buffer
      timers[TIMER_COLL_ALLOC] = clockNano();
      deviceAdaptor->deviceMalloc(&buff_out, size, flagcxMemHost, NULL);
      timers[TIMER_COLL_ALLOC] = clockNano() - timers[TIMER_COLL_ALLOC];

      // step 2: recv
      timers[TIMER_COLL_COMM] = clockNano();
      cclAdaptors[flagcxCCLAdaptorHost]->recv(buff_out, count, datatype, peer,
                                              comm->host_comm, NULL);
      timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

      // step 3: memcpy h2d
      timers[TIMER_COLL_MEM_H2D] = clockNano();
      deviceAdaptor->deviceMemcpy(recvbuff, buff_out, size,
                                  flagcxMemcpyHostToDevice, NULL, NULL);
      timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];

      // step 4: free host buffer
      timers[TIMER_COLL_FREE] = clockNano();
      deviceAdaptor->deviceFree(buff_out, flagcxMemHost, NULL);
      timers[TIMER_COLL_FREE] = clockNano() - timers[TIMER_COLL_FREE];

      timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
      INFO(FLAGCX_COLL,
           "Flagcx timings - %s Recv: rank %d nranks %d total %.2fms (memory "
           "alloc "
           "%.2fms, memory free %.2fms, memory h2d %.2fms, comm %.2fms)",
           cclAdaptors[flagcxCCLAdaptorHost]->name, comm->rank, comm->nranks,
           timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_ALLOC] / 1e6,
           timers[TIMER_COLL_FREE] / 1e6, timers[TIMER_COLL_MEM_H2D] / 1e6,
           timers[TIMER_COLL_COMM] / 1e6);
    } else {
      FLAGCXCHECK(flagcxHeteroRecv(recvbuff, count, datatype, peer,
                                   comm->hetero_comm, stream));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxGroupStart(flagcxComm_t comm) {
  if (!is_homo_comm(comm)) {
    FLAGCXCHECK(flagcxHeteroGroupStart());
  } else {
    if (use_host_comm()) {
      FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorHost]->groupStart());
    } else {
      FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->groupStart());
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxGroupEnd(flagcxComm_t comm) {
  if (!is_homo_comm(comm)) {
    FLAGCXCHECK(flagcxHeteroGroupEnd());
  } else {
    if (use_host_comm()) {
      FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorHost]->groupEnd());
    } else {
      FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->groupEnd());
    }
  }
  return flagcxSuccess;
}