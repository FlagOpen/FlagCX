/*************************************************************************
 * Copyright (c) 2025 BAAI. All rights reserved.
 ************************************************************************/

#include "c2c_algo.h"
#include "runner.h"

#define FLAGCX_CACHE_CAPACITY 16
static flagcxLRUCache<size_t, flagcxC2cPlanner>
    planCache(FLAGCX_CACHE_CAPACITY);

flagcxResult_t hybridRunnerReduce(const void *sendbuff, void *recvbuff,
                                  size_t count, flagcxDataType_t datatype,
                                  flagcxRedOp_t op, int root,
                                  flagcxInnerComm_t comm,
                                  flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  // Construct flagcxC2cPlanner and find corresponding strategy
  flagcxC2cPlanner planner;
  auto hashValue = getC2cCommPatternHash(count, hybridComm->cluster_ids[root],
                                         flagcxCommOpReduce, op, hybridComm);
  if (!planCache.get(hashValue, planner)) {
    INFO(FLAGCX_COLL,
         "No available plan is found, create a new one with "
         "communication pattern "
         "(count, rootClsuterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, hybridComm->cluster_ids[root], flagcxCommOpReduce, op,
         (size_t)((uintptr_t)hybridComm), hashValue);
    planner = flagcxC2cPlanner(count, count, root, hybridComm,
                               flagcxCommOpReduce, op);
    planCache.put(hashValue, planner);
  } else {
    INFO(FLAGCX_COLL,
         "Found available plan with communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, hybridComm->cluster_ids[root], flagcxCommOpReduce, op,
         (size_t)((uintptr_t)hybridComm), hashValue);
  }
  FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, root, stream));
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerGather(const void *sendbuff, void *recvbuff,
                                  size_t count, flagcxDataType_t datatype,
                                  int root, flagcxInnerComm_t comm,
                                  flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  // Construct flagcxC2cPlanner and find corresponding strategy
  flagcxC2cPlanner planner;
  auto hashValue = getC2cCommPatternHash(count, root, flagcxCommOpGather,
                                         flagcxRedNoOp, hybridComm);
  if (!planCache.get(hashValue, planner)) {
    INFO(FLAGCX_COLL,
         "No available plan is found, create a new one with "
         "communication pattern "
         "(count, rootRank, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, root, flagcxCommOpGather, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
    planner = flagcxC2cPlanner(count, count * hybridComm->nranks, root,
                               hybridComm, flagcxCommOpGather, flagcxRedNoOp);
    planCache.put(hashValue, planner);
  } else {
    INFO(FLAGCX_COLL,
         "Found available plan with communication pattern "
         "(count, rootRank, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, root, flagcxCommOpGather, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
  }
  FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, root, stream));
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerScatter(const void *sendbuff, void *recvbuff,
                                   size_t count, flagcxDataType_t datatype,
                                   int root, flagcxInnerComm_t comm,
                                   flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  // Construct flagcxC2cPlanner and find corresponding strategy
  flagcxC2cPlanner planner;
  auto hashValue = getC2cCommPatternHash(count, root, flagcxCommOpScatter,
                                         flagcxRedNoOp, hybridComm);
  if (!planCache.get(hashValue, planner)) {
    INFO(FLAGCX_COLL,
         "No available plan is found, create a new one with "
         "communication pattern "
         "(count, rootRank, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, root, flagcxCommOpScatter, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
    planner = flagcxC2cPlanner(count * hybridComm->nranks, count, root,
                               hybridComm, flagcxCommOpScatter, flagcxRedNoOp);
    planCache.put(hashValue, planner);
  } else {
    INFO(FLAGCX_COLL,
         "Found available plan with communication pattern "
         "(count, rootRank, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, root, flagcxCommOpScatter, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
  }
  FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, root, stream));
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerBroadcast(const void *sendbuff, void *recvbuff,
                                     size_t count, flagcxDataType_t datatype,
                                     int root, flagcxInnerComm_t comm,
                                     flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  // Construct flagcxC2cPlanner and find corresponding strategy
  flagcxC2cPlanner planner;
  auto hashValue =
      getC2cCommPatternHash(count, hybridComm->cluster_ids[root],
                            flagcxCommOpBroadcast, flagcxRedNoOp, hybridComm);
  if (!planCache.get(hashValue, planner)) {
    INFO(FLAGCX_COLL,
         "No available plan is found, create a new one with "
         "communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, hybridComm->cluster_ids[root], flagcxCommOpBroadcast,
         flagcxRedNoOp, (size_t)((uintptr_t)hybridComm), hashValue);
    planner = flagcxC2cPlanner(count, count, root, hybridComm,
                               flagcxCommOpBroadcast, flagcxRedNoOp);
    planCache.put(hashValue, planner);
  } else {
    INFO(FLAGCX_COLL,
         "Found available plan with communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, hybridComm->cluster_ids[root], flagcxCommOpBroadcast,
         flagcxRedNoOp, (size_t)((uintptr_t)hybridComm), hashValue);
  }
  FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, root, stream));
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerAllReduce(const void *sendbuff, void *recvbuff,
                                     size_t count, flagcxDataType_t datatype,
                                     flagcxRedOp_t op, flagcxInnerComm_t comm,
                                     flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  // Construct flagcxC2cPlanner and find corresponding strategy
  flagcxC2cPlanner planner;
  auto hashValue = getC2cCommPatternHash(
      count, hybridComm->nclusters, flagcxCommOpAllReduce, op,
      hybridComm); // use nclusters as rootClusterId for hash
  if (!planCache.get(hashValue, planner)) {
    INFO(FLAGCX_COLL,
         "No available plan is found, create a new one with "
         "communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, hybridComm->nclusters, flagcxCommOpAllReduce, op,
         (size_t)((uintptr_t)hybridComm), hashValue);
    planner = flagcxC2cPlanner(count, count, -1, hybridComm,
                               flagcxCommOpAllReduce, op);
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
         count, hybridComm->nclusters, flagcxCommOpAllReduce, op,
         (size_t)((uintptr_t)hybridComm), hashValue);
  }
  FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream));
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerReduceScatter(const void *sendbuff, void *recvbuff,
                                         size_t recvcount,
                                         flagcxDataType_t datatype,
                                         flagcxRedOp_t op,
                                         flagcxInnerComm_t comm,
                                         flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  // Construct flagcxC2cPlanner and find corresponding strategy
  flagcxC2cPlanner planner;
  auto hashValue = getC2cCommPatternHash(
      recvcount, hybridComm->nclusters, flagcxCommOpReduceScatter, op,
      hybridComm); // use nclusters as rootClusterId for hash
  if (!planCache.get(hashValue, planner)) {
    INFO(FLAGCX_COLL,
         "No available plan is found, create a new one with "
         "communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         recvcount, hybridComm->nclusters, flagcxCommOpReduceScatter, op,
         (size_t)((uintptr_t)hybridComm), hashValue);
    planner = flagcxC2cPlanner(hybridComm->nranks * recvcount, recvcount, -1,
                               hybridComm, flagcxCommOpReduceScatter, op);
    planCache.put(hashValue, planner);
  } else {
    INFO(FLAGCX_COLL,
         "Found available plan with communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         recvcount, hybridComm->nclusters, flagcxCommOpReduceScatter, op,
         (size_t)((uintptr_t)hybridComm), hashValue);
  }
  FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream));
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerAllGather(const void *sendbuff, void *recvbuff,
                                     size_t sendcount,
                                     flagcxDataType_t datatype,
                                     flagcxInnerComm_t comm,
                                     flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  // Construct flagcxC2cPlanner and find corresponding strategy
  flagcxC2cPlanner planner;
  auto hashValue = getC2cCommPatternHash(
      sendcount, hybridComm->nclusters,
      flagcxCommOpAllGather, // use nclusters as rootClusterId for hash
      flagcxRedNoOp, hybridComm);
  if (!planCache.get(hashValue, planner)) {
    INFO(FLAGCX_COLL,
         "No available plan is found, create a new one with "
         "communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         sendcount, hybridComm->nclusters, flagcxCommOpAllGather, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
    planner =
        flagcxC2cPlanner(sendcount, sendcount * hybridComm->nranks, -1,
                         hybridComm, flagcxCommOpAllGather, flagcxRedNoOp);
    planCache.put(hashValue, planner);
  } else {
    INFO(FLAGCX_COLL,
         "Found available plan with communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         sendcount, hybridComm->nclusters, flagcxCommOpAllGather, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
  }
  FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream));
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerAlltoAll(const void *sendbuff, void *recvbuff,
                                    size_t count, flagcxDataType_t datatype,
                                    flagcxInnerComm_t comm,
                                    flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  // Construct flagcxC2cPlanner and find corresponding strategy
  flagcxC2cPlanner planner;
  auto hashValue =
      getC2cCommPatternHash(count, 1, // use 1 as rootClusterId for hash
                            flagcxCommOpAlltoAll, flagcxRedNoOp, hybridComm);
  if (!planCache.get(hashValue, planner)) {
    INFO(FLAGCX_COLL,
         "No available plan is found, create a new one with "
         "communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, 1, flagcxCommOpAlltoAll, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
    planner = flagcxC2cPlanner(count, count, -1, hybridComm,
                               flagcxCommOpAlltoAll, flagcxRedNoOp);
    planCache.put(hashValue, planner);
  } else {
    INFO(FLAGCX_COLL,
         "Found available plan with communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%ld, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         count, 1, flagcxCommOpAlltoAll, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
  }
  FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream));
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerAlltoAllv(const void *sendbuff, size_t *sendcounts,
                                     size_t *sdispls, void *recvbuff,
                                     size_t *recvcounts, size_t *rdispls,
                                     flagcxDataType_t datatype,
                                     flagcxInnerComm_t comm,
                                     flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  flagcxC2cPlanner planner;
  auto hashValue = getC2cCommPatternHash(
      1, 1, // use 1 both as count and rootClusterId for hash
      flagcxCommOpAlltoAllv, flagcxRedNoOp, hybridComm);
  if (!planCache.get(hashValue, planner)) {
    INFO(FLAGCX_COLL,
         "No available plan is found, create a new one with "
         "communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%d, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         1, 1, flagcxCommOpAlltoAllv, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
    planner = flagcxC2cPlanner(1, 1, -1, hybridComm, flagcxCommOpAlltoAllv,
                               flagcxRedNoOp);
    planCache.put(hashValue, planner);
  } else {
    INFO(FLAGCX_COLL,
         "Found available plan with communication pattern "
         "(count, rootClusterId, commOp, redOp, comm) = (%d, %d, %d, %d, "
         "%ld), hashValue = "
         "%ld",
         1, 1, flagcxCommOpAlltoAllv, flagcxRedNoOp,
         (size_t)((uintptr_t)hybridComm), hashValue);
  }
  FLAGCXCHECK(planner.execute(sendbuff, recvbuff, datatype, -1, stream,
                              sendcounts, sdispls, recvcounts, rdispls));
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerSend(const void *sendbuff, size_t count,
                                flagcxDataType_t datatype, int peer,
                                flagcxInnerComm_t comm, flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  if (hybridComm->cluster_ids[hybridComm->rank] ==
      hybridComm->cluster_ids[peer]) {
    FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->send(
        sendbuff, count, datatype, hybridComm->globalrank2homorank[peer],
        hybridComm->homo_comm, stream));
  } else {
    FLAGCXCHECK(flagcxHeteroSend(sendbuff, count, datatype, peer,
                                 hybridComm->hetero_comm, stream));
  }
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerRecv(void *recvbuff, size_t count,
                                flagcxDataType_t datatype, int peer,
                                flagcxInnerComm_t comm, flagcxStream_t stream) {
  // cast flagcxInnerComm_t to flagcxComm_t
  flagcxComm_t hybridComm = reinterpret_cast<flagcxComm_t>(comm);
  if (hybridComm->cluster_ids[hybridComm->rank] ==
      hybridComm->cluster_ids[peer]) {
    FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->recv(
        recvbuff, count, datatype, hybridComm->globalrank2homorank[peer],
        hybridComm->homo_comm, stream));
  } else {
    FLAGCXCHECK(flagcxHeteroRecv(recvbuff, count, datatype, peer,
                                 hybridComm->hetero_comm, stream));
  }
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerGroupStart() {
  FLAGCXCHECK(flagcxHeteroGroupStart());
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->groupStart());
  return flagcxSuccess;
}

flagcxResult_t hybridRunnerGroupEnd() {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->groupEnd());
  FLAGCXCHECK(flagcxHeteroGroupEnd());
  return flagcxSuccess;
}

struct flagcxRunner hybridRunner = {
    // Communication functions
    hybridRunnerReduce, hybridRunnerGather, hybridRunnerScatter,
    hybridRunnerBroadcast, hybridRunnerAllReduce, hybridRunnerReduceScatter,
    hybridRunnerAllGather, hybridRunnerAlltoAll, hybridRunnerAlltoAllv,
    hybridRunnerSend, hybridRunnerRecv,
    // Group semantics
    hybridRunnerGroupStart, hybridRunnerGroupEnd};