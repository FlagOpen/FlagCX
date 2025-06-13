#include "cost_model.h"
#include "topo.h"

constexpr size_t MB_SIZE = 1024 * 1024;
constexpr size_t GB_SIZE = 1024 * 1024 * 1024;
constexpr size_t CHUNK_SIZE = 4ULL * 1024 * 1024;

std::map<flagcxVendorType,
         std::map<flagcxCommOp_t, std::map<int, std::map<size_t, float>>>>
    flagcxAlgoTimeEstimator::homoTimeMap;
// 0: Nvidia, 1: Iluvatar, 2: MLU, 3: Metax
const float flagcxLatMap[FLAGCX_VENDOR_NUM][2] = {
    {0.0, 0.0},
    {0.0, 0.0},
    {0.0, 0.0},
    {0.0, 0.0}}; // assume that latency have a negligible impact on algo time

flagcxAlgoTimeEstimator::flagcxAlgoTimeEstimator(flagcxDataType_t dtype,
                                                 flagcxC2cPlanner &planner)
    : datatype_(dtype), planner_(planner) {
  comm_ = planner_.comm_;
}

flagcxAlgoTimeEstimator::~flagcxAlgoTimeEstimator() {
  if (plannerInfoData_) {
    free(plannerInfoData_);
    plannerInfoData_ = nullptr;
  }
}

flagcxResult_t flagcxAlgoTimeEstimator::collectPlannerInfo() {
  if (plannerInfoReady_) {
    // already collected planner info from all ranks
    return flagcxSuccess;
  }
  FLAGCXCHECK(flagcxCalloc(&plannerInfoData_, comm_->nranks));
  FLAGCXCHECK(planner_.getPlannerInfo(plannerInfoData_));
  FLAGCXCHECK(bootstrapAllGather(comm_->hetero_comm->bootstrap,
                                 (void *)plannerInfoData_,
                                 sizeof(flagcxC2cPlannerInfo)));
  FLAGCXCHECK(bootstrapBarrier(comm_->hetero_comm->bootstrap, comm_->rank,
                               comm_->nranks, 0));
  plannerInfoReady_ = true;
  return flagcxSuccess;
}

flagcxResult_t flagcxAlgoTimeEstimator::getAlgoTime(float *time) {
  const char *enableTopoDetect = flagcxGetEnv("FLAGCX_ENABLE_TOPO_DETECT");
  const char *interServerTopoFile =
      flagcxGetEnv("FLAGCX_INTERSERVER_ROUTE_FILE");
  if (enableTopoDetect && interServerTopoFile &&
      strcmp(enableTopoDetect, "TRUE") == 0) {
    // algo time estimator depends on cluster level topology detection
    FLAGCXCHECK(collectPlannerInfo());
    float preHomoTime, heteroTime, postHomoTime;
    INFO(FLAGCX_GRAPH, "COST_MODEL: getting time for prehomo funcs");
    FLAGCXCHECK(getPreHomoAlgoTime(&preHomoTime));
    INFO(FLAGCX_GRAPH, "COST_MODEL: getting time for hetero funcs");
    FLAGCXCHECK(getHeteroAlgoTime(&heteroTime));
    INFO(FLAGCX_GRAPH, "COST_MODEL: getting time for posthomo funcs");
    FLAGCXCHECK(getPostHomoAlgoTime(&postHomoTime));
    INFO(FLAGCX_GRAPH,
         "COST_MODEL: preHomoTime = %f, heteroTime = %f, postHomoTime = %f",
         preHomoTime, heteroTime, postHomoTime);
    *time = preHomoTime + heteroTime + postHomoTime;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxAlgoTimeEstimator::getPreHomoAlgoTime(float *time) {
  float totalPreHomoTime = 0.0;
  for (int r = 0; r < comm_->nranks; r++) {
    int clusterId = comm_->cluster_ids[r];
    int clusterRankSize = comm_->cluster_sizes[clusterId];
    flagcxVendorType vendor = comm_->clusterVendorMap[clusterId];
    auto &preHomoFuncs = plannerInfoData_[r].preHomoFuncList;
    float preHomoTimeForRank = 0.0;
    for (int i = 0; i < plannerInfoData_[r].preHomoFuncLoops; i++) {
      auto &func = preHomoFuncs[i];
      float algoTime = 0.0;
      FLAGCXCHECK(getHomoAlgoTime(func, r, clusterRankSize, vendor, &algoTime));
      preHomoTimeForRank += algoTime;
    }
    totalPreHomoTime = std::max(totalPreHomoTime, preHomoTimeForRank);
  }
  *time = totalPreHomoTime;
  return flagcxSuccess;
}

flagcxResult_t flagcxAlgoTimeEstimator::getPostHomoAlgoTime(float *time) {
  float totalPostHomoTime = 0.0;
  for (int r = 0; r < comm_->nranks; r++) {
    int clusterId = comm_->cluster_ids[r];
    int clusterRankSize = comm_->cluster_sizes[clusterId];
    flagcxVendorType vendor = comm_->clusterVendorMap[clusterId];
    auto &postHomoFuncs = plannerInfoData_[r].postHomoFuncList;
    float postHomoTimeForRank = 0.0;
    for (int i = 0; i < plannerInfoData_[r].postHomoFuncLoops; i++) {
      auto &func = postHomoFuncs[i];
      float algoTime = 0.0;
      FLAGCXCHECK(getHomoAlgoTime(func, r, clusterRankSize, vendor, &algoTime));
      postHomoTimeForRank += algoTime;
    }
    totalPostHomoTime = std::max(totalPostHomoTime, postHomoTimeForRank);
  }
  *time = totalPostHomoTime;
  return flagcxSuccess;
}

flagcxResult_t
flagcxAlgoTimeEstimator::getHomoAlgoTime(flagcxC2cHomoFuncInfo &homoFunc,
                                         int rank, int rankSize,
                                         flagcxVendorType vendor, float *time) {
  size_t totalSize = homoFunc.count * getFlagcxDataTypeSize(datatype_);
  if (!homoFunc.isHomoInterComm) {
    rankSize = comm_->cluster_sizes[comm_->cluster_ids[rank]];
  }
  if (homoFunc.commOp == flagcxCommOpReduceScatter) {
    totalSize = homoFunc.count * rankSize * getFlagcxDataTypeSize(datatype_);
  }
  INFO(FLAGCX_GRAPH,
       "COST_MODEL: getHomoAlgoTime: vendor = %d, commOp = %d, rankSize = %d, "
       "totalSize = %ld",
       vendor, homoFunc.commOp, rankSize, totalSize);
  float defaultTime = homoTimeMap[vendor][homoFunc.commOp][rankSize][totalSize];
  *time = defaultTime;
  return flagcxSuccess;
}

flagcxResult_t flagcxAlgoTimeEstimator::getHomoInterAlgoTime(int rank, int loop,
                                                             float *time) {

  float totalHomoInterTime = 0.0;

  int clusterId = comm_->cluster_ids[rank];
  int clusterRankSize = plannerInfoData_[rank].homoInterRanks;
  flagcxVendorType vendor = comm_->clusterVendorMap[clusterId];
  float homoInterTimeForRank = 0.0;
  auto &func = plannerInfoData_[rank].homoInterFuncList[loop];
  FLAGCXCHECK(getHomoAlgoTime(func, rank, clusterRankSize, vendor,
                              &homoInterTimeForRank));
  totalHomoInterTime = std::max(totalHomoInterTime, homoInterTimeForRank);

  *time = totalHomoInterTime;
  return flagcxSuccess;
}

float flagcxAlgoTimeEstimator::getRefreshTime() {
  return 0.0; // return fixed time for now
}

flagcxResult_t flagcxAlgoTimeEstimator::getHeteroAlgoTime(float *time) {
  float totalTime = 0.0;
  for (int r = 0; r < comm_->nranks; r++) {
    float heteroTimePerRank = 0.0;
    auto &heteroFuncList = plannerInfoData_[r].heteroFuncList;
    int heteroAndHomoFuncLoops =
        plannerInfoData_[r].heteroAndHomoInterFuncLoops;
    for (int i = 0; i < heteroAndHomoFuncLoops; i++) {
      float p2pTime = 0.0;
      FLAGCXCHECK(getP2pTime(r, heteroFuncList[i], &p2pTime));
      INFO(FLAGCX_COLL, "COST_MODEL: p2pTime for rank %d is %f", r, p2pTime);
      float homoInterTime = 0.0;
      FLAGCXCHECK(getHomoInterAlgoTime(r, i, &homoInterTime));
      INFO(FLAGCX_COLL, "COST_MODEL: homoInterAlgoTime for rank %d is %f", r,
           homoInterTime);
      heteroTimePerRank += p2pTime + homoInterTime;
    }
    totalTime = std::max(totalTime, heteroTimePerRank);
  }
  *time = totalTime;

  return flagcxSuccess;
}

flagcxResult_t flagcxAlgoTimeEstimator::getP2pTime(
    int rank, flagcxC2cHeteroFuncInfo &heteroFunc, float *time) {
  flagcxHeteroComm_t heteroComm = comm_->hetero_comm;
  struct flagcxTopoServer *localServer;
  struct flagcxTopoNode *localNet;
  FLAGCXCHECK(flagcxTopoGetServerFromRank(
      rank, heteroComm->interServerTopo, heteroComm->topoServer, &localServer));
  FLAGCXCHECK(flagcxTopoGetLocalNetNode(localServer, rank, &localNet));
  int clusterId = comm_->cluster_ids[rank];        // {rank: clusterId}
  int vendor = comm_->clusterVendorMap[clusterId]; // {clusterId: vendor}
  float curClusterLat = flagcxLatMap[vendor][FLAGCX_INTER_LAT_IDX];
  float sendTime = 0.0;
  float recvTime = 0.0;
  for (int i = 0; i < heteroFunc.numOps; i++) {
    flagcxC2cP2POpInfo &p2pOp = heteroFunc.p2pOps[i];
    int remoteRank = p2pOp.peerRank;
    int remoteClusterId = comm_->cluster_ids[remoteRank];
    int remoteVendor = comm_->clusterVendorMap[remoteClusterId];
    float remoteClusterLat = flagcxLatMap[remoteVendor][FLAGCX_INTER_LAT_IDX];
    // get nic of remote rank
    struct flagcxTopoServer *remoteServer;
    struct flagcxTopoNode *remoteNet;
    // get server of current rank
    FLAGCXCHECK(
        flagcxTopoGetServerFromRank(remoteRank, heteroComm->interServerTopo,
                                    heteroComm->topoServer, &remoteServer));
    // get local nic used by current rank
    FLAGCXCHECK(
        flagcxTopoGetLocalNetNode(remoteServer, remoteRank, &remoteNet));
    float routeBw = heteroComm->interServerTopo
                        ->routeMap[localNet->net.guid][remoteNet->net.guid]
                        ->interBw;
    if (p2pOp.isRecv) {
      recvTime += getSendRecvTime(curClusterLat, remoteClusterLat, routeBw,
                                  p2pOp.count, CHUNK_SIZE);
    } else {
      sendTime += getSendRecvTime(curClusterLat, remoteClusterLat, routeBw,
                                  p2pOp.count, CHUNK_SIZE);
    }
  }
  *time = std::max(sendTime, recvTime);
  return flagcxSuccess;
}

float flagcxAlgoTimeEstimator::getSendRecvTime(float curClusterLat,
                                               float remoteClusterLat, float bw,
                                               int totalCount,
                                               size_t chunkSize) {
  // in the current implementation, chunks are sent in serial order
  float lat =
      std::max(curClusterLat,
               remoteClusterLat); // use the higher latency between two clusters
  size_t bytes = totalCount * getFlagcxDataTypeSize(datatype_);
  int steps = (bytes + chunkSize - 1) / chunkSize;
  float time = 0.0;
  int sizeSent = 0;
  for (int s = 0; s < steps; s++) {
    size_t sendSize = std::min(chunkSize, bytes - sizeSent);
    time += lat + sendSize / (1000 * bw); // convert to us (bw in GB/s)
    sizeSent += sendSize;
  }
  return time;
}
