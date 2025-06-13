#include "c2c_algo.h"
#include <cstdint>
#include <cstdlib>

// GCD using Euclidean algorithm
inline int gcd(int a, int b) {
  while (b != 0) {
    int tmp = b;
    b = a % b;
    a = tmp;
  }
  return a;
}

// LCM using GCD
inline int lcm(int a, int b) { return std::abs(a * b) / gcd(a, b); }

// LCM of clusterInterRankList
inline int getLcmOfInterRankList(
    const std::vector<std::vector<int>> &clusterInterRankList) {
  int result = 1;
  for (size_t i = 0; i < clusterInterRankList.size(); ++i) {
    result = lcm(result, clusterInterRankList[i].size());
  }
  return result;
}

size_t getC2cCommPatternHash(size_t count, size_t rootClusterId,
                             flagcxCommOp_t commOp, flagcxRedOp_t redOp,
                             flagcxComm_t comm) {
  std::size_t h1 = std::hash<size_t>()(count);
  std::size_t h2 = std::hash<size_t>()(rootClusterId);
  std::size_t h3 = std::hash<size_t>()(commOp);
  std::size_t h4 = std::hash<size_t>()(redOp);
  std::size_t h5 = std::hash<size_t>()((size_t)((uintptr_t)comm));
  return static_cast<size_t>(h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^
                             (h5 << 4));
}

flagcxInterRankBufferInfoManager::flagcxInterRankBufferInfoManager(
    int totalCount)
    : totalCount_(totalCount) {}

flagcxInterRankBufferInfoManager::~flagcxInterRankBufferInfoManager() {}

bool flagcxInterRankBufferInfoManager::checkIfPossibleToPush(int clusterId,
                                                             int rank,
                                                             int offset,
                                                             int count) {
  if (auto clusterSearch = bufferInfos_.find(clusterId);
      clusterSearch != bufferInfos_.end()) {
    if (auto rankSearch = clusterSearch->second.find(rank);
        rankSearch != clusterSearch->second.end()) {
      auto infoList = rankSearch->second;
      for (auto info : infoList) {
        if ((offset < info.offset_ && offset + count > info.offset_) ||
            offset == info.offset_ ||
            (offset > info.offset_ && offset < info.offset_ + info.count_)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool flagcxInterRankBufferInfoManager::checkIfPossibleToSplitAndPush(
    int clusterId, int rank, int offset, int count, int *splitCount,
    int *pushMode) {
  int maxSplitCount = 0;
  int finalPushMode = 0; // 0: prePush, 1: postPush
  if (auto clusterSearch = bufferInfos_.find(clusterId);
      clusterSearch != bufferInfos_.end()) {
    if (auto rankSearch = clusterSearch->second.find(rank);
        rankSearch != clusterSearch->second.end()) {
      auto infoList = rankSearch->second;
      for (auto info : infoList) {
        if (offset < info.offset_ && offset + count > info.offset_) {
          if (checkIfPossibleToPush(clusterId, rank, offset,
                                    info.offset_ - offset)) {
            maxSplitCount = std::max(info.offset_ - offset, maxSplitCount);
            finalPushMode = 0;
          }
        }
        if (offset >= info.offset_ && offset < info.offset_ + info.count_ &&
            offset + count > info.offset_ + info.count_) {
          if (checkIfPossibleToPush(clusterId, rank, info.offset_ + info.count_,
                                    offset + count - info.offset_ -
                                        info.count_)) {
            maxSplitCount = std::max(
                offset + count - info.offset_ - info.count_, maxSplitCount);
            finalPushMode = 1;
          }
        }
      }
      if (maxSplitCount > 0) {
        *splitCount = maxSplitCount;
        *pushMode = finalPushMode;
        return true;
      }
    }
  }
  return false;
}

bool flagcxInterRankBufferInfoManager::checkIsFull(int clusterId, int rank) {
  int rankCount = 0;
  if (auto clusterSearch = bufferInfos_.find(clusterId);
      clusterSearch != bufferInfos_.end()) {
    if (auto rankSearch = clusterSearch->second.find(rank);
        rankSearch != clusterSearch->second.end()) {
      auto infoList = rankSearch->second;
      for (auto info : infoList) {
        rankCount += info.count_;
      }
    }
  }
  if (rankCount == totalCount_) {
    return true;
  }
  return false;
}

bool flagcxInterRankBufferInfoManager::checkIsScheduled(int clusterId,
                                                        int rank) {
  if (auto clusterSearch = bufferInfos_.find(clusterId);
      clusterSearch != bufferInfos_.end()) {
    if (auto rankSearch = clusterSearch->second.find(rank);
        rankSearch != clusterSearch->second.end()) {
      auto infoList = rankSearch->second;
      for (auto info : infoList) {
        if (!info.isScheduled_) {
          return false;
        }
      }
    }
  }
  return true;
}

std::list<flagcxBufferInfo> &
flagcxInterRankBufferInfoManager::getBufferInfoList(int clusterId, int rank) {
  if (auto clusterSearch = bufferInfos_.find(clusterId);
      clusterSearch != bufferInfos_.end()) {
    if (auto rankSearch = clusterSearch->second.find(rank);
        rankSearch != clusterSearch->second.end()) {
      return rankSearch->second;
    } else {
      clusterSearch->second[rank] = {};
      return clusterSearch->second[rank];
    }
  } else {
    bufferInfos_[clusterId][rank] = {};
    return bufferInfos_[clusterId][rank];
  }
}

void flagcxInterRankBufferInfoManager::pushBackBufferInfo(
    int clusterId, int rank, int offset, int count, int clusterIdToSend,
    int isRecv, int isScheduled, int peerRank, int loopId) {
  bufferInfos_[clusterId][rank].emplace_back(
      offset, count, clusterIdToSend, isRecv, isScheduled, peerRank, loopId);
}

void flagcxInterRankBufferInfoManager::popFrontBufferInfo(int clusterId,
                                                          int rank) {
  bufferInfos_[clusterId][rank].pop_front();
}

void flagcxInterRankBufferInfoManager::resetBufferInfo() {
  for (auto clusterIt = bufferInfos_.begin(); clusterIt != bufferInfos_.end();
       ++clusterIt) {
    for (auto rankIt = clusterIt->second.begin();
         rankIt != clusterIt->second.end(); ++rankIt) {
      rankIt->second.clear();
    }
  }
}

void flagcxInterRankBufferInfoManager::printBufferInfo(int step) {
  for (auto clusterIt = bufferInfos_.begin(); clusterIt != bufferInfos_.end();
       ++clusterIt) {
    for (auto rankIt = clusterIt->second.begin();
         rankIt != clusterIt->second.end(); ++rankIt) {
      for (auto bufferIt = rankIt->second.begin();
           bufferIt != rankIt->second.end(); ++bufferIt) {
        if (step == 0) {
          TRACE_CALL(
              "Initial InterRankBufferInfo: cluster_id = %d, rank = %d, "
              "offset = %d, count = %d, clusterIdToSend = %d, "
              "isRecv = %d, isScheduled = %d, peerRank = %d, loopId = %d",
              clusterIt->first, rankIt->first, bufferIt->offset_,
              bufferIt->count_, bufferIt->clusterIdToSend_, bufferIt->isRecv_,
              bufferIt->isScheduled_, bufferIt->peerRank_, bufferIt->loopId_);
        } else if (step == 1) {
          TRACE_CALL(
              "Internal InterRankBufferInfo: cluster_id = %d, rank = %d, "
              "offset = %d, count = %d, clusterIdToSend = %d, "
              "isRecv = %d, isScheduled = %d, peerRank = %d, loopId = %d",
              clusterIt->first, rankIt->first, bufferIt->offset_,
              bufferIt->count_, bufferIt->clusterIdToSend_, bufferIt->isRecv_,
              bufferIt->isScheduled_, bufferIt->peerRank_, bufferIt->loopId_);
        } else if (step == 2) {
          TRACE_CALL(
              "Final InterRankBufferInfo: cluster_id = %d, rank = %d, "
              "offset = %d, count = %d, clusterIdToSend = %d, "
              "isRecv = %d, isScheduled = %d, peerRank = %d, loopId = %d",
              clusterIt->first, rankIt->first, bufferIt->offset_,
              bufferIt->count_, bufferIt->clusterIdToSend_, bufferIt->isRecv_,
              bufferIt->isScheduled_, bufferIt->peerRank_, bufferIt->loopId_);
        }
      }
    }
  }
}

flagcxC2cP2pOp::flagcxC2cP2pOp(int rank, int peerRank, int offset, int count,
                               int isRecv)
    : rank_(rank), peerRank_(peerRank), offset_(offset), count_(count),
      isRecv_(isRecv) {}
flagcxC2cP2pOp::~flagcxC2cP2pOp() {}

flagcxResult_t flagcxC2cP2pOp::run(void *buff, flagcxDataType_t datatype,
                                   flagcxComm_t comm, flagcxStream_t stream) {
  TRACE_CALL(
      "flagcxC2cP2pOp run: rank = %d, peerRank = %d, offset = %d, count = %d, "
      "isRecv = %d, datatype = %d",
      comm->rank, peerRank_, offset_, count_, isRecv_, datatype);
  void *ptr =
      static_cast<char *>(buff) + offset_ * getFlagcxDataTypeSize(datatype);
  if (isRecv_) {
    return flagcxHeteroRecv(static_cast<void *>(ptr), count_, datatype,
                            peerRank_, comm->hetero_comm, stream);
  } else {
    return flagcxHeteroSend(static_cast<void *>(ptr), count_, datatype,
                            peerRank_, comm->hetero_comm, stream);
  }
}

flagcxC2cHomoFunc::flagcxC2cHomoFunc(int rootRank, int sendOffset,
                                     int recvOffset, int count, int homoType,
                                     flagcxCommOp_t commOp)
    : rootRank_(rootRank), sendOffset_(sendOffset), recvOffset_(recvOffset),
      count_(count), homoType_(homoType), commOp_(commOp),
      interRankBufferInfoManager_(0) {}

flagcxC2cHomoFunc::flagcxC2cHomoFunc(
    int rootRank, int sendOffset, int recvOffset, int count, int homoType,
    flagcxCommOp_t commOp,
    flagcxInterRankBufferInfoManager interRankBufferInfoManager)
    : rootRank_(rootRank), sendOffset_(sendOffset), recvOffset_(recvOffset),
      count_(count), homoType_(homoType), commOp_(commOp),
      interRankBufferInfoManager_(interRankBufferInfoManager) {}

flagcxC2cHomoFunc::~flagcxC2cHomoFunc() {}

flagcxResult_t flagcxC2cHomoFunc::run(const void *sendbuff, void *recvbuff,
                                      flagcxDataType_t datatype,
                                      flagcxRedOp_t redOp, int root,
                                      flagcxComm_t comm, flagcxStream_t stream,
                                      size_t *sendCounts, size_t *sDispls,
                                      size_t *recvCounts, size_t *rDispls) {
  if (homoType_ == 1 && comm->homoInterMyRank == -1) {
    return flagcxSuccess;
  }

  TRACE_CALL(
      "flagcxC2cHomoFunc run: rank = %d, rootRank = %d, sendOffset = %d, "
      "recvOffset = %d, count = %d, "
      "homoType = %d, commOp = %d, datatype = %d, redOp = %d, root = %d",
      comm->rank, rootRank_, sendOffset_, recvOffset_, count_, homoType_,
      commOp_, datatype, redOp, root);

  switch (commOp_) {
    case flagcxCommOpReduce:
      return cclAdaptors[flagcxCCLAdaptorDevice]->reduce(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, redOp, (rootRank_ == -1) ? root : rootRank_,
          homoType_ == 1 ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpAllReduce:
      return cclAdaptors[flagcxCCLAdaptorDevice]->allReduce(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, redOp,
          homoType_ == 1 ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpReduceScatter:
      return cclAdaptors[flagcxCCLAdaptorDevice]->reduceScatter(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, redOp,
          homoType_ == 1 ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpAllGather:
      return cclAdaptors[flagcxCCLAdaptorDevice]->allGather(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype,
          homoType_ == 1 ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpGather:
      return cclAdaptors[flagcxCCLAdaptorDevice]->gather(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, (rootRank_ == -1) ? root : rootRank_,
          homoType_ == 1 ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpScatter:
      return cclAdaptors[flagcxCCLAdaptorDevice]->scatter(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, (rootRank_ == -1) ? root : rootRank_,
          homoType_ == 1 ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpSend:
      cclAdaptors[flagcxCCLAdaptorDevice]->groupStart();
      if (homoType_ == 0) {
        // send from root to inter-ranks
        if (comm->homo_rank == ((rootRank_ == -1) ? root : rootRank_)) {
          int clusterId = comm->cluster_ids[comm->rank];
          for (size_t i = 0; i < comm->clusterInterRankList[clusterId].size();
               ++i) {
            if (comm->homoInterMyRank != int(i)) {
              cclAdaptors[flagcxCCLAdaptorDevice]->send(
                  const_cast<const void *>(static_cast<void *>(
                      static_cast<char *>(const_cast<void *>(sendbuff)) +
                      sendOffset_ * getFlagcxDataTypeSize(datatype))),
                  count_, datatype,
                  comm->globalrank2homorank
                      [comm->clusterInterRankList[clusterId][i]],
                  comm->homo_comm, stream);
            }
          }
        }
      } else if (homoType_ == 1) {
        // send from inter-rank 1,2,...,n to inter-rank 0
        if (comm->homoInterMyRank > 0) {
          int clusterId = comm->cluster_ids[comm->rank];
          auto &buffList = interRankBufferInfoManager_.getBufferInfoList(
              clusterId, comm->rank);
          for (auto it = buffList.begin(); it != buffList.end(); it++) {
            if (it->isRecv_) {
              cclAdaptors[flagcxCCLAdaptorDevice]->send(
                  const_cast<const void *>(static_cast<void *>(
                      static_cast<char *>(const_cast<void *>(sendbuff)) +
                      it->offset_ * getFlagcxDataTypeSize(datatype))),
                  it->count_, datatype, 0, comm->homoInterComm, stream);
            }
          }
        }
      } else if (homoType_ == 2) {
        // send from inter-rank 0 to root
        if (comm->homoInterMyRank == 0 &&
            (comm->homo_rank != ((rootRank_ == -1) ? root : rootRank_))) {
          cclAdaptors[flagcxCCLAdaptorDevice]->send(
              const_cast<const void *>(static_cast<void *>(
                  static_cast<char *>(const_cast<void *>(sendbuff)) +
                  sendOffset_ * getFlagcxDataTypeSize(datatype))),
              count_, datatype, (rootRank_ == -1) ? root : rootRank_,
              comm->homo_comm, stream);
        }
      }
      cclAdaptors[flagcxCCLAdaptorDevice]->groupEnd();
      return flagcxSuccess;
    case flagcxCommOpRecv:
      cclAdaptors[flagcxCCLAdaptorDevice]->groupStart();
      if (homoType_ == 0) {
        // recv at inter-rank from root
        if (comm->homoInterMyRank != -1 &&
            comm->homo_rank != ((rootRank_ == -1) ? root : rootRank_)) {
          cclAdaptors[flagcxCCLAdaptorDevice]->recv(
              static_cast<void *>(
                  static_cast<char *>(const_cast<void *>(recvbuff)) +
                  recvOffset_ * getFlagcxDataTypeSize(datatype)),
              count_, datatype, (rootRank_ == -1) ? root : rootRank_,
              comm->homo_comm, stream);
        }
      } else if (homoType_ == 1) {
        // recv at inter-rank 0 from inter-rank 1,2,...,n
        if (comm->homoInterMyRank == 0) {
          int clusterId = comm->cluster_ids[comm->rank];
          for (size_t i = 1; i < comm->clusterInterRankList[clusterId].size();
               ++i) {
            auto &buffList = interRankBufferInfoManager_.getBufferInfoList(
                clusterId, comm->clusterInterRankList[clusterId][i]);
            for (auto it = buffList.begin(); it != buffList.end(); it++) {
              if (it->isRecv_) {
                cclAdaptors[flagcxCCLAdaptorDevice]->recv(
                    static_cast<void *>(
                        static_cast<char *>(const_cast<void *>(recvbuff)) +
                        it->offset_ * getFlagcxDataTypeSize(datatype)),
                    it->count_, datatype, i, comm->homoInterComm, stream);
              }
            }
          }
        }
      } else if (homoType_ == 2) {
        // recv at root from inter-rank 0
        if (comm->homoInterMyRank != 0 &&
            comm->homo_rank == ((rootRank_ == -1) ? root : rootRank_)) {
          int clusterId = comm->cluster_ids[comm->rank];
          cclAdaptors[flagcxCCLAdaptorDevice]->recv(
              static_cast<void *>(
                  static_cast<char *>(const_cast<void *>(recvbuff)) +
                  recvOffset_ * getFlagcxDataTypeSize(datatype)),
              count_, datatype,
              comm->globalrank2homorank[comm->clusterInterRankList[clusterId]
                                                                  [0]],
              comm->homo_comm, stream);
        }
      }
      cclAdaptors[flagcxCCLAdaptorDevice]->groupEnd();
      return flagcxSuccess;
    case flagcxCommOpBroadcast:
      return cclAdaptors[flagcxCCLAdaptorDevice]->broadcast(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, (rootRank_ == -1) ? root : rootRank_,
          homoType_ == 1 ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpAlltoAll:
      return cclAdaptors[flagcxCCLAdaptorDevice]->alltoAll(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype,
          homoType_ == 1 ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpAlltoAllv:
      cclAdaptors[flagcxCCLAdaptorDevice]->groupStart();
      for (size_t i = 0; i < comm->nranks; ++i) {
        if (flagcxCCLAdaptorNeedSendrecv(sendCounts[i])) {
          if (comm->cluster_ids[comm->rank] == comm->cluster_ids[i]) {
            FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->send(
                const_cast<const void *>(static_cast<void *>(
                    static_cast<char *>(const_cast<void *>(sendbuff)) +
                    sDispls[i] * getFlagcxDataTypeSize(datatype))),
                sendCounts[i], datatype, comm->globalrank2homorank[i],
                comm->homo_comm, stream));
          }
        }
        if (flagcxCCLAdaptorNeedSendrecv(recvCounts[i])) {
          if (comm->cluster_ids[comm->rank] == comm->cluster_ids[i]) {
            FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->recv(
                static_cast<void *>(static_cast<char *>(recvbuff) +
                                    rDispls[i] *
                                        getFlagcxDataTypeSize(datatype)),
                recvCounts[i], datatype, comm->globalrank2homorank[i],
                comm->homo_comm, stream));
          }
        }
      }
      cclAdaptors[flagcxCCLAdaptorDevice]->groupEnd();
      return flagcxSuccess;
    default:
      return flagcxSuccess;
  }
}

flagcxC2cHeteroFunc::flagcxC2cHeteroFunc() {}
flagcxC2cHeteroFunc::~flagcxC2cHeteroFunc() {}

void flagcxC2cHeteroFunc::addP2pOp(int rank, int peerRank, int offset,
                                   int count, int isRecv) {
  p2pOps_.emplace_back(rank, peerRank, offset, count, isRecv);
}

flagcxResult_t flagcxC2cHeteroFunc::run(void *sendbuff, void *recvbuff,
                                        flagcxDataType_t datatype,
                                        flagcxComm_t comm,
                                        flagcxStream_t stream) {
  flagcxHeteroGroupStart();
  for (auto op : p2pOps_) {
    if (op.isRecv_) {
      FLAGCXCHECK(op.run(recvbuff, datatype, comm, stream));
    } else {
      FLAGCXCHECK(op.run(sendbuff, datatype, comm, stream));
    }
  }
  flagcxHeteroGroupEnd();
  return flagcxSuccess;
}

flagcxC2cRefreshFunc::flagcxC2cRefreshFunc()
    : offset_(0), count_(0), totalCount_(0), redOp_(flagcxSum) {}
flagcxC2cRefreshFunc::flagcxC2cRefreshFunc(int offset, int count,
                                           int totalCount, flagcxRedOp_t redOp)
    : offset_(offset), count_(count), totalCount_(totalCount), redOp_(redOp) {}
flagcxC2cRefreshFunc::~flagcxC2cRefreshFunc() {}

flagcxResult_t flagcxC2cRefreshFunc::run(void *buff, flagcxDataType_t datatype,
                                         flagcxStream_t stream) {
  TRACE_CALL("flagcxC2cRefreshFunc run: offset = %d, count = %d, "
             "datatype = %d, redOp = %d",
             offset_, count_, datatype, redOp_);
  if (redOp_ == flagcxSum) {
    deviceAdaptor->deviceMemset(buff, 0,
                                offset_ * getFlagcxDataTypeSize(datatype),
                                flagcxMemDevice, stream);
    deviceAdaptor->deviceMemset(
        static_cast<void *>(static_cast<char *>(buff) +
                            (offset_ + count_) *
                                getFlagcxDataTypeSize(datatype)),
        0, (totalCount_ - offset_ - count_) * getFlagcxDataTypeSize(datatype),
        flagcxMemDevice, stream);
  }
  return flagcxSuccess;
}

flagcxC2cPlanner::flagcxC2cPlanner(int sendCount, int recvCount, int rootRank,
                                   flagcxComm_t comm, flagcxCommOp_t commOp,
                                   flagcxRedOp_t redOp)
    : sendCount_(sendCount), recvCount_(recvCount), rootRank_(rootRank),
      comm_(comm), commOp_(commOp), redOp_(redOp), sendCounts_(nullptr),
      sDispls_(nullptr), recvCounts_(nullptr), rDispls_(nullptr),
      clusterInterRankList_(comm->clusterInterRankList),
      clusterId_(comm->cluster_ids[comm->rank]), rank_(comm->rank),
      homoMyRank_(comm->homo_rank), homoRootRank_(comm->homo_root_rank),
      homoRanks_(comm->homo_ranks), homoInterMyRank_(comm->homoInterMyRank),
      homoInterRootRank_(comm->homoInterRootRank),
      homoInterRanks_(comm->homoInterRanks) {
  // set totalCount_
  totalCount_ = (sendCount_ >= recvCount_) ? sendCount_ : recvCount_;

  // set rootClusterId_ and isRootCluster_
  rootClusterId_ = comm_->cluster_ids[rootRank_];
  isRootCluster_ = (rootClusterId_ == clusterId_) ? 1 : 0;

  // calculate clusterOffset_ and clusterCount_
  clusterOffset_ = 0;
  for (int i = 0; i < clusterId_; ++i) {
    clusterOffset_ += comm_->cluster_sizes[i];
  }
  clusterCount_ = comm_->cluster_sizes[clusterId_];

  // if inter ranks in all clusters equal to 1 （single-nic）
  if (commOp_ == flagcxCommOpAllReduce ||
      commOp_ == flagcxCommOpReduceScatter) {
    multiNic_ = 1;
    for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
      if (clusterInterRankList_[i].size() == 1) {
        multiNic_ = 0;
        break;
      }
    }
  } else {
    multiNic_ = 0;
    for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
      if (clusterInterRankList_[i].size() != 1) {
        multiNic_ = 1;
        break;
      }
    }
  }

  // if inter ranks in a cluster is superier to totalCount_ (single-nic)
  for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
    if (clusterInterRankList_[i].size() > totalCount_) {
      multiNic_ = 0;
      break;
    }
  }

  // if inter ranks in current cluster equal to homo ranks
  eachNicPerRank_ = 1;
  for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
    if (clusterInterRankList_[i].size() != comm->cluster_sizes[i]) {
      eachNicPerRank_ = 0;
      break;
    }
  }

  // set strategyFound_ to 0
  strategyFound_ = 0;

  // init inter-rank buffer info manager
  interRankBufferInfoManager_ = flagcxInterRankBufferInfoManager(totalCount_);
}

flagcxC2cPlanner::~flagcxC2cPlanner() {}

// homoType: 0, pre; 1, homoInter; 2, post,
// mode: 0, multiNic+eachNicPerRank; 1, normal; 2, single-nic
// For now, we support AllReduce, AllGather, ReduceScatter, Reduce, Broadcast,
// AlltoAll/v operator mapping
flagcxCommOp_t flagcxC2cPlanner::getC2cHomoCommOp(int homoType, int mode) {
  switch (commOp_) {
    case flagcxCommOpSend:
      return flagcxCommOpSend;
    case flagcxCommOpRecv:
      return flagcxCommOpRecv;
    case flagcxCommOpBroadcast:
      switch (homoType) {
        case 0:
          switch (isRootCluster_) {
            case 0:
              return flagcxCommNoOp;
            case 1:
              return flagcxCommOpBroadcast;
          }
        case 1:
          return flagcxCommNoOp;
        case 2:
          switch (isRootCluster_) {
            case 0:
              return flagcxCommOpBroadcast;
            case 1:
              return flagcxCommNoOp;
          }
      }
    case flagcxCommOpGather:
      switch (homoType) {
        case 0:
          switch (mode) {
            case 0:
              return flagcxCommOpAllGather;
            case 1:
              return flagcxCommOpAllGather;
            case 2:
              return flagcxCommOpGather;
          }
        case 1:
          switch (isRootCluster_) {
            case 0:
              return flagcxCommNoOp;
            case 1:
              switch (homoInterMyRank_ == 0) {
                case 0:
                  return flagcxCommOpSend;
                case 1:
                  return flagcxCommOpRecv;
              }
          }
        case 2:
          switch (isRootCluster_) {
            case 0:
              return flagcxCommNoOp;
            case 1:
              switch (rank_ == rootRank_) {
                case 0:
                  return flagcxCommOpSend;
                case 1:
                  return flagcxCommOpRecv;
              }
          }
      }
    case flagcxCommOpScatter:
      switch (homoType) {
        case 0:
          switch (isRootCluster_) {
            case 0:
              return flagcxCommNoOp;
            case 1:
              switch (rank_ == rootRank_) {
                case 0:
                  return flagcxCommOpRecv;
                case 1:
                  return flagcxCommOpSend;
              }
          }
        case 1:
          switch (isRootCluster_) {
            case 0:
              switch (homoInterMyRank_ == 0) {
                case 0:
                  return flagcxCommOpSend;
                case 1:
                  return flagcxCommOpRecv;
              }
            case 1:
              return flagcxCommNoOp;
          }
        case 2:
          return flagcxCommOpScatter;
      }
    case flagcxCommOpReduce:
      switch (homoType) {
        case 0:
          switch (mode) {
            case 0:
              return flagcxCommOpReduceScatter;
            case 1:
              return flagcxCommOpReduce;
            case 2:
              return flagcxCommOpReduce;
          }
        case 1:
          switch (isRootCluster_) {
            case 0:
              return flagcxCommNoOp;
            case 1:
              return flagcxCommOpReduce;
          }
        case 2:
          return flagcxCommNoOp;
      }
    case flagcxCommOpAllReduce:
      switch (homoType) {
        case 0:
          switch (mode) {
            case 0:
              return flagcxCommOpReduceScatter;
            case 1:
              return flagcxCommOpReduce;
            case 2:
              return flagcxCommOpReduce;
          }
        case 1:
          return flagcxCommOpAllReduce;
        case 2:
          switch (mode) {
            case 0:
              return flagcxCommNoOp;
            case 1:
              return flagcxCommOpAllReduce;
            case 2:
              return flagcxCommNoOp;
          }
      }
    case flagcxCommOpAllGather:
      switch (homoType) {
        case 0:
          switch (mode) {
            case 0:
              return flagcxCommOpAllGather;
            case 1:
              return flagcxCommOpAllGather;
            case 2:
              return flagcxCommOpGather;
          }
        case 1:
          return flagcxCommNoOp;
        case 2:
          return flagcxCommOpBroadcast;
      }
    case flagcxCommOpReduceScatter:
      switch (homoType) {
        case 0:
          switch (mode) {
            case 0:
              return flagcxCommOpReduceScatter;
            case 1:
              return flagcxCommOpReduce;
            case 2:
              return flagcxCommOpReduce;
          }
        case 1:
          return flagcxCommOpAllReduce;
        case 2:
          return flagcxCommOpReduceScatter;
      }
    case flagcxCommOpAlltoAll:
      switch (homoType) {
        case 0:
          return flagcxCommOpAlltoAll;
        case 1:
          return flagcxCommNoOp;
        case 2:
          return flagcxCommNoOp;
      }
    case flagcxCommOpAlltoAllv:
      switch (homoType) {
        case 0:
          return flagcxCommOpAlltoAllv;
        case 1:
          return flagcxCommNoOp;
        case 2:
          return flagcxCommNoOp;
      }
    default:
      return flagcxCommNoOp;
  }
}

flagcxResult_t flagcxC2cPlanner::refresh(int isSendRecv) {
  if (isSendRecv) {
    int clusterOffset = 0;
    int lcm = getLcmOfInterRankList(clusterInterRankList_);
    // we use fine-grained granularity to search for balanced hetero-send/recv
    // workloads
    std::string searchGranularity;
    const char *searchGranularityPtr =
        flagcxGetEnv("FLAGCX_C2C_SEARCH_GRANULARITY");
    if (searchGranularityPtr == NULL) {
      searchGranularity = std::string("COARSE");
    } else {
      searchGranularity = std::string(searchGranularityPtr);
      if (searchGranularity != "COARSE" && searchGranularity != "FINE") {
        searchGranularity = std::string("COARSE");
      }
    }
    interRankBufferInfoManager_.resetBufferInfo();
    for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
      size_t nClusterInterRanks =
          multiNic_ ? clusterInterRankList_[i].size() : 1;
      int myCount =
          (sendCount_ >= recvCount_)
              ? totalCount_ / nClusterInterRanks
              : (sendCount_ * comm_->cluster_sizes[i]) / nClusterInterRanks;
      int myRes =
          (sendCount_ >= recvCount_)
              ? totalCount_ % nClusterInterRanks
              : (sendCount_ * comm_->cluster_sizes[i]) % nClusterInterRanks;
      int minCount = (sendCount_ >= recvCount_)
                         ? totalCount_ / lcm
                         : (sendCount_ * comm_->cluster_sizes[i]) / lcm;
      if (searchGranularity == "COARSE") {
        minCount = myCount;
      }
      // for root-required ops, root cluster send or recv buffers based on
      // comm op type we use two flags to avoid redundant sendrecv ops
      int isScheduled = 0;
      int isUseless = 0;
      if (rootClusterId_ >= 0) {
        if ((commOp_ == flagcxCommOpReduce || commOp_ == flagcxCommOpGather) &&
            i == rootClusterId_) {
          isScheduled = 1;
        }
        if ((commOp_ == flagcxCommOpScatter ||
             commOp_ == flagcxCommOpBroadcast) &&
            i != rootClusterId_) {
          isUseless = 1;
        }
      }
      for (size_t j = 0; j < nClusterInterRanks; ++j) {
        for (size_t z = 0; z < clusterInterRankList_.size(); ++z) {
          if (i != z) {
            if (isUseless == 0) {
              for (int k = 0; k < myCount / minCount; ++k) {
                interRankBufferInfoManager_.pushBackBufferInfo(
                    i, clusterInterRankList_[i][j],
                    (sendCount_ >= recvCount_) ? myCount * j + minCount * k
                                               : clusterOffset * sendCount_ +
                                                     myCount * j + minCount * k,
                    minCount, z, 0, isScheduled, -1, -1);
              }
              if (j == nClusterInterRanks - 1 && myRes > 0) {
                interRankBufferInfoManager_.pushBackBufferInfo(
                    i, clusterInterRankList_[i][j],
                    (sendCount_ >= recvCount_)
                        ? myCount * j
                        : clusterOffset * sendCount_ + myCount * j,
                    myRes, z, 0, isScheduled, -1, -1);
              }
            }
          }
        }
      }
      clusterOffset += comm_->cluster_sizes[i];
    }
    interRankBufferInfoManager_.printBufferInfo(0);
  } else {
    for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
      for (size_t j = 0; j < clusterInterRankList_[i].size(); ++j) {
        auto &rankList = interRankBufferInfoManager_.getBufferInfoList(
            i, clusterInterRankList_[i][j]);
        for (auto it = rankList.begin(); it != rankList.end();) {
          int erased = 0;
          if (it->isRecv_) {
            it = rankList.erase(it);
            erased = 1;
          }
          if (!erased) {
            it++;
          }
        }
      }
    }
    interRankBufferInfoManager_.printBufferInfo(1);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxC2cPlanner::searchHeteroSendRecvOps(int searchMethod,
                                                         int loopId) {
  // cluster j send to cluster z, cluster z recv from cluster j
  for (size_t j = 0; j < clusterInterRankList_.size(); ++j) {
    for (size_t z = j + 1; z < clusterInterRankList_.size(); ++z) {
      for (size_t r1 = 0; r1 < clusterInterRankList_[j].size(); ++r1) {
        auto &jList = interRankBufferInfoManager_.getBufferInfoList(
            j, clusterInterRankList_[j][r1]);
        for (auto it = jList.begin(); it != jList.end();) {
          int erased = 0;
          if (!it->isScheduled_ && !it->isRecv_ && it->clusterIdToSend_ == z) {
            for (size_t r2 = 0; r2 < clusterInterRankList_[z].size(); ++r2) {
              size_t newR2 = (searchMethod == 1)
                                 ? (r2 + r1) % clusterInterRankList_[z].size()
                                 : r2;
              if (interRankBufferInfoManager_.checkIfPossibleToPush(
                      z, clusterInterRankList_[z][newR2], it->offset_,
                      it->count_)) {
                interRankBufferInfoManager_.pushBackBufferInfo(
                    z, clusterInterRankList_[z][newR2], it->offset_, it->count_,
                    0, 1, 1, clusterInterRankList_[j][r1], loopId);
                it->isScheduled_ = 1;
                it->peerRank_ = clusterInterRankList_[z][newR2];
                it->loopId_ = loopId;
                break;
              }
            }
            if (!it->isScheduled_) {
              int splitCount = 0;
              int maxSplitCount = 0;
              int pushMode = 0;
              int finalPushMode = 0;
              int splitRank = clusterInterRankList_[z][0];
              for (size_t r2 = 0; r2 < clusterInterRankList_[z].size(); ++r2) {
                size_t newR2 = (r2 + r1) % clusterInterRankList_[z].size();
                if (interRankBufferInfoManager_.checkIfPossibleToSplitAndPush(
                        z, clusterInterRankList_[z][newR2], it->offset_,
                        it->count_, &splitCount, &pushMode)) {
                  if (maxSplitCount < splitCount) {
                    maxSplitCount = splitCount;
                    finalPushMode = pushMode;
                    splitRank = clusterInterRankList_[z][newR2];
                  }
                }
              }
              if (maxSplitCount > 0) {
                if (finalPushMode == 0) {
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      z, splitRank, it->offset_, maxSplitCount, 0, 1, 1,
                      clusterInterRankList_[j][r1], loopId);
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      j, clusterInterRankList_[j][r1], it->offset_,
                      maxSplitCount, it->clusterIdToSend_, 0, 1, splitRank,
                      loopId);
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      j, clusterInterRankList_[j][r1],
                      it->offset_ + maxSplitCount, it->count_ - maxSplitCount,
                      it->clusterIdToSend_, 0, 0, -1, -1);
                } else if (finalPushMode == 1) {
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      z, splitRank, it->offset_ + it->count_ - maxSplitCount,
                      maxSplitCount, 0, 1, 1, clusterInterRankList_[j][r1],
                      loopId);
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      j, clusterInterRankList_[j][r1],
                      it->offset_ + it->count_ - maxSplitCount, maxSplitCount,
                      it->clusterIdToSend_, 0, 1, splitRank, loopId);
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      j, clusterInterRankList_[j][r1], it->offset_,
                      it->count_ - maxSplitCount, it->clusterIdToSend_, 0, 0,
                      -1, -1);
                }
                it = jList.erase(it);
                erased = 1;
              }
            }
          }
          if (!erased) {
            it++;
          }
        }
      }
    }
  }
  // cluster z send to cluster j, cluster j recv from cluster z
  for (size_t j = 0; j < clusterInterRankList_.size(); ++j) {
    for (size_t z = j + 1; z < clusterInterRankList_.size(); ++z) {
      for (size_t r1 = 0; r1 < clusterInterRankList_[z].size(); ++r1) {
        auto &zList = interRankBufferInfoManager_.getBufferInfoList(
            z, clusterInterRankList_[z][r1]);
        for (auto it = zList.begin(); it != zList.end();) {
          int erased = 0;
          if (!it->isScheduled_ && !it->isRecv_ && it->clusterIdToSend_ == j) {
            for (size_t r2 = 0; r2 < clusterInterRankList_[j].size(); ++r2) {
              size_t newR2 = (searchMethod == 1)
                                 ? (r2 + r1) % clusterInterRankList_[j].size()
                                 : r2;
              if (interRankBufferInfoManager_.checkIfPossibleToPush(
                      j, clusterInterRankList_[j][newR2], it->offset_,
                      it->count_)) {
                interRankBufferInfoManager_.pushBackBufferInfo(
                    j, clusterInterRankList_[j][newR2], it->offset_, it->count_,
                    0, 1, 1, clusterInterRankList_[z][r1], loopId);
                it->isScheduled_ = 1;
                it->peerRank_ = clusterInterRankList_[j][newR2];
                it->loopId_ = loopId;
                break;
              }
            }
            if (!it->isScheduled_) {
              int splitCount = 0;
              int maxSplitCount = 0;
              int pushMode = 0;
              int finalPushMode = 0;
              int splitRank = clusterInterRankList_[j][0];
              for (size_t r2 = 0; r2 < clusterInterRankList_[j].size(); ++r2) {
                size_t newR2 = (r2 + r1) % clusterInterRankList_[j].size();
                if (interRankBufferInfoManager_.checkIfPossibleToSplitAndPush(
                        j, clusterInterRankList_[j][newR2], it->offset_,
                        it->count_, &splitCount, &pushMode)) {
                  if (maxSplitCount < splitCount) {
                    maxSplitCount = splitCount;
                    finalPushMode = pushMode;
                    splitRank = clusterInterRankList_[j][newR2];
                  }
                }
              }
              if (maxSplitCount > 0) {
                if (finalPushMode == 0) {
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      j, splitRank, it->offset_, maxSplitCount, 0, 1, 1,
                      clusterInterRankList_[z][r1], loopId);
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      z, clusterInterRankList_[z][r1], it->offset_,
                      maxSplitCount, it->clusterIdToSend_, 0, 1, splitRank,
                      loopId);
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      z, clusterInterRankList_[z][r1],
                      it->offset_ + maxSplitCount, it->count_ - maxSplitCount,
                      it->clusterIdToSend_, 0, 0, -1, -1);
                } else if (finalPushMode == 1) {
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      j, splitRank, it->offset_ + it->count_ - maxSplitCount,
                      maxSplitCount, 0, 1, 1, clusterInterRankList_[z][r1],
                      loopId);
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      z, clusterInterRankList_[z][r1],
                      it->offset_ + it->count_ - maxSplitCount, maxSplitCount,
                      it->clusterIdToSend_, 0, 1, splitRank, loopId);
                  interRankBufferInfoManager_.pushBackBufferInfo(
                      z, clusterInterRankList_[z][r1], it->offset_,
                      it->count_ - maxSplitCount, it->clusterIdToSend_, 0, 0,
                      -1, -1);
                }
                it = zList.erase(it);
                erased = 1;
              }
            }
          }
          if (!erased) {
            it++;
          }
        }
      }
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxC2cPlanner::findStrategy() {
  refresh(1);
  // setup refreshFunc
  if (!interRankBufferInfoManager_.getBufferInfoList(clusterId_, rank_)
           .empty()) {
    auto &buffer =
        interRankBufferInfoManager_.getBufferInfoList(clusterId_, rank_)
            .front();
    refreshFunc_ = flagcxC2cRefreshFunc(buffer.offset_, buffer.count_,
                                        totalCount_, redOp_);
  } else {
    refreshFunc_ = flagcxC2cRefreshFunc(0, 0, totalCount_, redOp_);
  }

  // reset multiNic_ based on comm op type
  // since broadcast, alltoall, alltoallv, scatter, gather ops behave
  // identically in both single-nic and multi-nic modes, no special handling is
  // required
  if (commOp_ == flagcxCommOpBroadcast || commOp_ == flagcxCommOpAlltoAll ||
      commOp_ == flagcxCommOpAlltoAllv || commOp_ == flagcxCommOpScatter ||
      commOp_ == flagcxCommOpGather) {
    multiNic_ = 1;
  }

  // reset eachNicPerRank_ based on comm op type
  // since alltoall, alltoallv ops behave identically in both
  // normal and rank-local multi-nic modes
  if (commOp_ == flagcxCommOpAlltoAll || commOp_ == flagcxCommOpAlltoAllv) {
    eachNicPerRank_ = 1;
  }

  if (multiNic_) {
    // multi-nic
    // setup preHomoFuncs
    if (eachNicPerRank_) {
      // inter ranks equaling to homo ranks
      // setup preHomoFuncs
      flagcxCommOp_t preHomoFuncCommOp = getC2cHomoCommOp(0, 0);
      preHomoFuncLoops_ = 1;
      for (int i = 0; i < preHomoFuncLoops_; ++i) {
        auto &buffer =
            interRankBufferInfoManager_.getBufferInfoList(clusterId_, rank_)
                .front();
        if (preHomoFuncCommOp == flagcxCommOpReduceScatter) {
          preHomoFuncList_.emplace_back(-1, 0, homoMyRank_ * buffer.count_,
                                        buffer.count_, 0, preHomoFuncCommOp);
        } else if (preHomoFuncCommOp == flagcxCommOpAllGather) {
          preHomoFuncList_.emplace_back(-1, 0, clusterOffset_ * sendCount_,
                                        sendCount_, 0, preHomoFuncCommOp);
        } else if (preHomoFuncCommOp == flagcxCommOpBroadcast) {
          preHomoFuncList_.emplace_back(-1, 0, 0, totalCount_, 0,
                                        preHomoFuncCommOp);
        } else if (preHomoFuncCommOp == flagcxCommOpSend) {
          preHomoFuncList_.emplace_back(comm_->globalrank2homorank[rootRank_],
                                        0, 0, totalCount_, 0,
                                        preHomoFuncCommOp);
        } else if (preHomoFuncCommOp == flagcxCommOpRecv) {
          preHomoFuncList_.emplace_back(comm_->globalrank2homorank[rootRank_],
                                        0, 0, totalCount_, 0,
                                        preHomoFuncCommOp);
        } else if (preHomoFuncCommOp == flagcxCommOpAlltoAll) {
          preHomoFuncList_.emplace_back(
              -1, clusterOffset_ * sendCount_, clusterOffset_ * recvCount_,
              totalCount_, 0, // sendCount_ = recvCount_ = totalCount_
              preHomoFuncCommOp);
        } else if (preHomoFuncCommOp == flagcxCommOpAlltoAllv) {
          preHomoFuncList_.emplace_back(-1, 0, 0, 0, 0, preHomoFuncCommOp);
        } else if (preHomoFuncCommOp == flagcxCommNoOp) {
          preHomoFuncList_.emplace_back(-1, 0, 0, totalCount_, 0,
                                        preHomoFuncCommOp);
        }
      }
    } else {
      // otherwise
      flagcxCommOp_t preHomoFuncCommOp = getC2cHomoCommOp(0, 1);
      if (preHomoFuncCommOp == flagcxCommOpReduce) {
        preHomoFuncLoops_ = clusterInterRankList_[clusterId_].size();
        for (int i = 0; i < preHomoFuncLoops_; ++i) {
          auto &buffer =
              interRankBufferInfoManager_
                  .getBufferInfoList(clusterId_,
                                     clusterInterRankList_[clusterId_][i])
                  .front();
          preHomoFuncList_.emplace_back(clusterInterRankList_[clusterId_][i] -
                                            (rank_ - homoMyRank_),
                                        buffer.offset_, buffer.offset_,
                                        buffer.count_, 0, preHomoFuncCommOp);
        }
      } else if (preHomoFuncCommOp == flagcxCommOpAllGather) {
        preHomoFuncLoops_ = 1;
        preHomoFuncList_.emplace_back(-1, 0, clusterOffset_ * sendCount_,
                                      sendCount_, 0, preHomoFuncCommOp);
      } else if (preHomoFuncCommOp == flagcxCommOpBroadcast) {
        preHomoFuncLoops_ = 1;
        preHomoFuncList_.emplace_back(-1, 0, 0, totalCount_, 0,
                                      preHomoFuncCommOp);
      } else if (preHomoFuncCommOp == flagcxCommOpSend) {
        preHomoFuncLoops_ = 1;
        preHomoFuncList_.emplace_back(comm_->globalrank2homorank[rootRank_], 0,
                                      0, totalCount_, 0, preHomoFuncCommOp);
      } else if (preHomoFuncCommOp == flagcxCommOpRecv) {
        preHomoFuncLoops_ = 1;
        preHomoFuncList_.emplace_back(comm_->globalrank2homorank[rootRank_], 0,
                                      0, totalCount_, 0, preHomoFuncCommOp);
      } else if (preHomoFuncCommOp == flagcxCommNoOp) {
        preHomoFuncLoops_ = 1;
        preHomoFuncList_.emplace_back(-1, 0, 0, totalCount_, 0,
                                      preHomoFuncCommOp);
      }
    }

    // determine hetero send/recv strategies
    // and setup homoInterFuncs
    if (commOp_ == flagcxCommOpAlltoAll) {
      heteroAndHomoInterFuncLoops_ = 1;
      flagcxC2cHeteroFunc heteroFunc = flagcxC2cHeteroFunc();
      for (size_t i = 0; i < comm_->nranks; ++i) {
        if (clusterId_ != comm_->cluster_ids[i]) {
          heteroFunc.addP2pOp(rank_, i, i * sendCount_, sendCount_, 0);
          heteroFunc.addP2pOp(rank_, i, i * recvCount_, recvCount_, 1);
        }
      }
      heteroFuncList_.push_back(std::move(heteroFunc));
      homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1, flagcxCommNoOp);
    } else if (commOp_ == flagcxCommOpAlltoAllv) {
      heteroAndHomoInterFuncLoops_ = 1;
      flagcxC2cHeteroFunc heteroFunc = flagcxC2cHeteroFunc();
      for (size_t i = 0; i < comm_->nranks; ++i) {
        if (flagcxCCLAdaptorNeedSendrecv(sendCounts_[i]) &&
            clusterId_ != comm_->cluster_ids[i]) {
          heteroFunc.addP2pOp(rank_, i, sDispls_[i], sendCounts_[i], 0);
        }
        if (flagcxCCLAdaptorNeedSendrecv(recvCounts_[i]) &&
            clusterId_ != comm_->cluster_ids[i]) {
          heteroFunc.addP2pOp(rank_, i, rDispls_[i], recvCounts_[i], 1);
        }
      }
      heteroFuncList_.push_back(std::move(heteroFunc));
      homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1, flagcxCommNoOp);
    } else {
      heteroAndHomoInterFuncLoops_ = 1;
      for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
        // search by BFS or DFS
        searchHeteroSendRecvOps(1, i);

        int scheduleCompleted = 1;
        for (size_t j = 0; j < clusterInterRankList_.size(); ++j) {
          for (size_t z = 0; z < clusterInterRankList_[j].size(); ++z) {
            if (!interRankBufferInfoManager_.checkIsScheduled(
                    j, clusterInterRankList_[j][z])) {
              scheduleCompleted = 0;
              break;
            }
          }
          if (!scheduleCompleted) {
            break;
          }
        }

        // setup heteroFuncs
        flagcxC2cHeteroFunc heteroFunc = flagcxC2cHeteroFunc();
        for (size_t j = 0; j < clusterInterRankList_.size(); ++j) {
          for (size_t z = 0; z < clusterInterRankList_[j].size(); ++z) {
            if (rank_ == clusterInterRankList_[j][z]) {
              auto &rankList =
                  interRankBufferInfoManager_.getBufferInfoList(j, rank_);
              for (auto it = rankList.begin(); it != rankList.end(); ++it) {
                if (it->isScheduled_ && it->loopId_ == i) {
                  heteroFunc.addP2pOp(rank_, it->peerRank_, it->offset_,
                                      it->count_, it->isRecv_);
                }
              }
            }
          }
        }
        heteroFuncList_.push_back(std::move(heteroFunc));

        // setup homoInterFuncs
        flagcxCommOp_t homoInterFuncCommOp =
            eachNicPerRank_ ? getC2cHomoCommOp(1, 0) : getC2cHomoCommOp(1, 1);
        if (homoInterFuncCommOp == flagcxCommOpAllReduce) {
          homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1,
                                          homoInterFuncCommOp);
        } else if (homoInterFuncCommOp == flagcxCommOpReduce) {
          homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_,
                                          0, // use homo_comm
                                          homoInterFuncCommOp);
        } else if (homoInterFuncCommOp == flagcxCommOpSend) {
          homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1,
                                          homoInterFuncCommOp,
                                          interRankBufferInfoManager_);
        } else if (homoInterFuncCommOp == flagcxCommOpRecv) {
          homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1,
                                          homoInterFuncCommOp,
                                          interRankBufferInfoManager_);
        } else if (homoInterFuncCommOp == flagcxCommNoOp) {
          homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1,
                                          homoInterFuncCommOp);
        }

        if (!scheduleCompleted) {
          refresh(0);
          heteroAndHomoInterFuncLoops_ += 1;
        }
      }
    }
    interRankBufferInfoManager_.printBufferInfo(2);

    // setup postHomoFuncs
    flagcxCommOp_t postHomoFuncCommOp =
        eachNicPerRank_ ? getC2cHomoCommOp(2, 0) : getC2cHomoCommOp(2, 1);
    if (postHomoFuncCommOp == flagcxCommOpAllReduce) {
      postHomoFuncLoops_ = 1;
      postHomoFuncList_.emplace_back(-1, 0, 0, recvCount_, 2,
                                     postHomoFuncCommOp);
    } else if (postHomoFuncCommOp == flagcxCommOpReduceScatter) {
      postHomoFuncLoops_ = 1;
      postHomoFuncList_.emplace_back(-1, clusterOffset_ * recvCount_, 0,
                                     recvCount_, 2, postHomoFuncCommOp);
    } else if (postHomoFuncCommOp == flagcxCommOpBroadcast) {
      postHomoFuncLoops_ = 0;
      for (size_t i = 0; i < clusterInterRankList_[clusterId_].size(); ++i) {
        auto &buffList = interRankBufferInfoManager_.getBufferInfoList(
            clusterId_, clusterInterRankList_[clusterId_][i]);
        for (auto it = buffList.begin(); it != buffList.end(); it++) {
          if (it->isRecv_) {
            postHomoFuncList_.emplace_back(
                clusterInterRankList_[clusterId_][i] - (rank_ - homoMyRank_),
                it->offset_, it->offset_, it->count_, 2, postHomoFuncCommOp);
            postHomoFuncLoops_ += 1;
          }
        }
      }
    } else if (postHomoFuncCommOp == flagcxCommOpScatter) {
      postHomoFuncLoops_ = 1;
      postHomoFuncList_.emplace_back(
          clusterInterRankList_[clusterId_][0] - (rank_ - homoMyRank_),
          clusterOffset_ * recvCount_, 0, recvCount_, 2, postHomoFuncCommOp);
    } else if (postHomoFuncCommOp == flagcxCommOpSend) {
      postHomoFuncLoops_ = 1;
      postHomoFuncList_.emplace_back(comm_->globalrank2homorank[rootRank_], 0,
                                     0, totalCount_, 2, postHomoFuncCommOp);
    } else if (postHomoFuncCommOp == flagcxCommOpRecv) {
      postHomoFuncLoops_ = 1;
      postHomoFuncList_.emplace_back(comm_->globalrank2homorank[rootRank_], 0,
                                     0, totalCount_, 2, postHomoFuncCommOp);
    } else if (postHomoFuncCommOp == flagcxCommNoOp) {
      postHomoFuncLoops_ = 0;
    }
  } else {
    // single-nic
    // setup preHomoFuncs
    flagcxCommOp_t preHomoFuncCommOp = getC2cHomoCommOp(0, 2);
    preHomoFuncLoops_ = 1;
    for (int i = 0; i < preHomoFuncLoops_; ++i) {
      auto &buffer = interRankBufferInfoManager_
                         .getBufferInfoList(
                             clusterId_, clusterInterRankList_[clusterId_][i])
                         .front();
      if (preHomoFuncCommOp == flagcxCommOpReduce) {
        preHomoFuncList_.emplace_back(clusterInterRankList_[clusterId_][i] -
                                          (rank_ - homoMyRank_),
                                      buffer.offset_, buffer.offset_,
                                      buffer.count_, 0, preHomoFuncCommOp);
      } else if (preHomoFuncCommOp == flagcxCommOpGather) {
        preHomoFuncList_.emplace_back(
            clusterInterRankList_[clusterId_][i] - (rank_ - homoMyRank_), 0,
            clusterOffset_ * sendCount_, sendCount_, 0, preHomoFuncCommOp);
      } else if (preHomoFuncCommOp == flagcxCommNoOp) {
        preHomoFuncList_.emplace_back(-1, 0, 0, totalCount_, 0,
                                      preHomoFuncCommOp);
      }
    }

    // setup heteroFuncs
    if (commOp_ == flagcxCommOpAllReduce ||
        commOp_ == flagcxCommOpReduceScatter || commOp_ == flagcxCommOpReduce) {
      heteroAndHomoInterFuncLoops_ = 1;
      flagcxC2cHeteroFunc heteroFunc = flagcxC2cHeteroFunc();
      for (size_t j = 0; j < clusterInterRankList_.size(); ++j) {
        if (clusterId_ == j) {
          continue;
        }
        if (isRootCluster_ || commOp_ != flagcxCommOpReduce) {
          int homoRankToRecvFromCluster =
              (comm_
                   ->globalrank2homorank[clusterInterRankList_[clusterId_][0]] -
               j - 1 + homoRanks_) %
              homoRanks_;
          if (homoMyRank_ == homoRankToRecvFromCluster) {
            heteroFunc.addP2pOp(rank_, clusterInterRankList_[j][0], 0,
                                totalCount_, 1);
          }
        }
        if (!isRootCluster_ || commOp_ != flagcxCommOpReduce) {
          int homoRankToSendToCluster =
              (comm_->globalrank2homorank[clusterInterRankList_[j][0]] -
               clusterId_ - 1 + comm_->cluster_sizes[j]) %
              comm_->cluster_sizes[j];
          int globalRankToSendToCluster =
              homoRankToSendToCluster -
              comm_->globalrank2homorank[clusterInterRankList_[j][0]] +
              clusterInterRankList_[j][0];
          if (homoMyRank_ ==
              comm_
                  ->globalrank2homorank[clusterInterRankList_[clusterId_][0]]) {
            heteroFunc.addP2pOp(rank_, globalRankToSendToCluster, 0,
                                totalCount_, 0);
          }
        }
      }
      heteroFuncList_.push_back(std::move(heteroFunc));
    } else if (commOp_ == flagcxCommOpAllGather) {
      heteroAndHomoInterFuncLoops_ = 1;
      flagcxC2cHeteroFunc heteroFunc = flagcxC2cHeteroFunc();
      int recvOffset = 0;
      for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
        if (clusterId_ == i) {
          recvOffset += comm_->cluster_sizes[i];
          continue;
        }
        if (homoInterMyRank_ != -1) {
          heteroFunc.addP2pOp(rank_, clusterInterRankList_[i][0],
                              clusterOffset_ * sendCount_,
                              clusterCount_ * sendCount_, 0);
          heteroFunc.addP2pOp(rank_, clusterInterRankList_[i][0],
                              recvOffset * sendCount_,
                              comm_->cluster_sizes[i] * sendCount_, 1);
        }
        recvOffset += comm_->cluster_sizes[i];
      }
      heteroFuncList_.push_back(std::move(heteroFunc));
    }

    // setup homoInterFuncs
    flagcxCommOp_t homoInterFuncCommOp = getC2cHomoCommOp(1, 2);
    for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
      if (homoInterFuncCommOp == flagcxCommOpAllReduce) {
        homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_,
                                        0, // use homo_comm
                                        homoInterFuncCommOp);
      } else if (homoInterFuncCommOp == flagcxCommOpReduce) {
        homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_,
                                        0, // use homo_comm
                                        homoInterFuncCommOp);
      } else if (homoInterFuncCommOp == flagcxCommNoOp) {
        homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1,
                                        homoInterFuncCommOp);
      }
    }

    // setup postHomoFuncs
    flagcxCommOp_t postHomoFuncCommOp = getC2cHomoCommOp(2, 2);
    if (postHomoFuncCommOp == flagcxCommOpReduceScatter) {
      postHomoFuncLoops_ = 1;
      postHomoFuncList_.emplace_back(-1, clusterOffset_ * recvCount_, 0,
                                     recvCount_, 2, postHomoFuncCommOp);
    } else if (postHomoFuncCommOp == flagcxCommOpBroadcast) {
      postHomoFuncLoops_ = 0;
      int clusterOffset = 0;
      for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
        postHomoFuncList_.emplace_back(
            clusterInterRankList_[clusterId_][0] - (rank_ - homoMyRank_),
            clusterOffset * sendCount_, clusterOffset * sendCount_,
            comm_->cluster_sizes[i] * sendCount_, 2, postHomoFuncCommOp);
        postHomoFuncLoops_ += 1;
        clusterOffset += comm_->cluster_sizes[i];
      }
    } else if (postHomoFuncCommOp == flagcxCommNoOp) {
      postHomoFuncLoops_ = 0;
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxC2cPlanner::execute(const void *sendbuff, void *recvbuff,
                                         flagcxDataType_t datatype, int root,
                                         flagcxStream_t stream,
                                         size_t *sendCounts, size_t *sDispls,
                                         size_t *recvCounts, size_t *rDispls) {
  // redOp validation
  if (redOp_ != flagcxRedNoOp) {
    if (redOp_ != flagcxSum && redOp_ != flagcxMax && redOp_ != flagcxMin) {
      WARN("Unsupported reduction operation %d", redOp_);
      return flagcxNotSupported;
    }
  }

  // root validation
  if (root != -1 && comm_->cluster_ids[root] != rootClusterId_) {
    WARN("Sorry, the input root cluster id is not valid %d[%d]",
         comm_->cluster_ids[root], rootClusterId_);
    return flagcxInvalidArgument;
  }

  // commOp validation
  // abort if nclusters > n-inter-ranks
  if (commOp_ == flagcxCommOpAllReduce ||
      commOp_ == flagcxCommOpReduceScatter) {
    int clusterCountValid_ = 1;
    for (int i = 0; i < comm_->nclusters; ++i) {
      if (comm_->nclusters > int(clusterInterRankList_[i].size())) {
        clusterCountValid_ = 0;
        break;
      }
    }
    if (!clusterCountValid_) {
      WARN("Unsupported communication operation %d since cluster count is "
           "larger than inter-rank count",
           commOp_);
      return flagcxNotSupported;
    }
  }
  // sendrecv counts and displs validation and initialization
  if (commOp_ == flagcxCommOpAlltoAllv) {
    if (sendCounts == nullptr || sDispls == nullptr || recvCounts == nullptr ||
        rDispls == nullptr) {
      WARN("Sorry, sendrecv counts and displacements need to be set for "
           "AlltoAllv operation");
      return flagcxInvalidArgument;
    }
    sendCounts_ = sendCounts;
    sDispls_ = sDispls;
    recvCounts_ = recvCounts;
    rDispls_ = rDispls;
  }

  if (!strategyFound_) {
    FLAGCXCHECK(findStrategy());
    strategyFound_ = 1;
  }

  // init scratch buffer if need
  if (commOp_ == flagcxCommOpReduceScatter || commOp_ == flagcxCommOpScatter ||
      (commOp_ == flagcxCommOpGather && rank_ != rootRank_)) {
    deviceAdaptor->deviceMalloc(&scratchBuffer_,
                                totalCount_ * getFlagcxDataTypeSize(datatype),
                                flagcxMemDevice, stream);
  } else {
    scratchBuffer_ = nullptr;
  }

  void *recvTmpBuff = (scratchBuffer_ == nullptr) ? recvbuff : scratchBuffer_;
  void *sendTmpBuff =
      (commOp_ == flagcxCommOpAlltoAll || commOp_ == flagcxCommOpAlltoAllv ||
       (commOp_ == flagcxCommOpScatter && rank_ == rootRank_))
          ? const_cast<void *>(sendbuff)
          : recvTmpBuff;

  // execute preHomoFuncs
  cclAdaptors[flagcxCCLAdaptorDevice]->groupStart();
  for (int i = 0; i < preHomoFuncLoops_; ++i) {
    preHomoFuncList_[i].run(sendbuff, recvTmpBuff, datatype, redOp_,
                            comm_->globalrank2homorank[root], comm_, stream,
                            sendCounts_, sDispls_, recvCounts_, rDispls_);
  }
  cclAdaptors[flagcxCCLAdaptorDevice]->groupEnd();

  for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
    // execute refreshFunc
    refreshFunc_.run(sendTmpBuff, datatype, stream);

    // TODO: use stream wait rather than stream sync to avoid cpu blocking
    // deviceAdaptor->streamSynchronize(stream);

    // execute heteroFuncs
    heteroFuncList_[i].run(sendTmpBuff, recvTmpBuff, datatype, comm_, stream);

    // TODO: use stream wait rather than stream sync to avoid cpu blocking
    deviceAdaptor->streamSynchronize(stream);

    // execute homoInterFuncs
    homoInterFuncList_[i].run(sendTmpBuff, recvTmpBuff, datatype, redOp_,
                              comm_->globalrank2homorank[root], comm_, stream);
  }

  // execute postHomoFuns
  cclAdaptors[flagcxCCLAdaptorDevice]->groupStart();
  for (int i = 0; i < postHomoFuncLoops_; ++i) {
    // execute refresh func
    refreshFunc_.run(sendTmpBuff, datatype, stream);

    // execute postHomoFunc
    postHomoFuncList_[i].run(sendTmpBuff, recvbuff, datatype, redOp_,
                             comm_->globalrank2homorank[root], comm_, stream);
  }
  cclAdaptors[flagcxCCLAdaptorDevice]->groupEnd();

  // free scratch buffer if needed
  if (scratchBuffer_ != nullptr) {
    deviceAdaptor->deviceFree(scratchBuffer_, flagcxMemDevice, stream);
  }

  return flagcxSuccess;
}