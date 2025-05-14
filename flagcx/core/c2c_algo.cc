#include "c2c_algo.h"
#include <cstdint>

size_t getC2cCommPatternHash(size_t count, flagcxCommOp_t commOp,
                             flagcxRedOp_t redOp, flagcxComm_t comm) {
  std::size_t h1 = std::hash<size_t>()(count);
  std::size_t h2 = std::hash<size_t>()(commOp);
  std::size_t h3 = std::hash<size_t>()(redOp);
  std::size_t h4 = std::hash<size_t>()((size_t)((uintptr_t)comm));
  return static_cast<size_t>(h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3));
}

// homoType: 0, pre; 1, homoInter; 2, post,
// mode: 0, multiNic+eachNicPerRank; 1, normal; 2, single-nic
// For now, we only support AllReduce operator mapping
flagcxCommOp_t getC2cHomoCommOp(flagcxCommOp_t commOp, int homoType, int mode) {
  switch (commOp) {
    case flagcxCommOpSend:
      return flagcxCommOpSend;
    case flagcxCommOpRecv:
      return flagcxCommOpRecv;
    case flagcxCommOpBroadcast:
      switch (homoType) {
        case 0:
          return flagcxCommOpBroadcast;
        case 1:
          return flagcxCommNoOp;
        case 2:
          switch (mode) {
            case 0:
              return flagcxCommOpAllReduce;
            case 1:
              return flagcxCommOpAllReduce;
            case 2:
              return flagcxCommOpBroadcast;
          }
      }
      return flagcxCommOpBroadcast;
    case flagcxCommOpGather:
      return flagcxCommOpGather;
    case flagcxCommOpScatter:
      return flagcxCommOpScatter;
    case flagcxCommOpReduce:
      return flagcxCommOpReduce;
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
      return flagcxCommOpAlltoAll;
    case flagcxCommOpAlltoAllv:
      return flagcxCommOpAlltoAllv;
    default:
      return flagcxCommNoOp;
  }
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
                                     int recvOffset, int count,
                                     int isHomoInterComm, flagcxCommOp_t commOp)
    : rootRank_(rootRank), sendOffset_(sendOffset), recvOffset_(recvOffset),
      count_(count), isHomoInterComm_(isHomoInterComm), commOp_(commOp) {}

flagcxC2cHomoFunc::~flagcxC2cHomoFunc() {}

flagcxResult_t flagcxC2cHomoFunc::run(const void *sendbuff, void *recvbuff,
                                      flagcxDataType_t datatype,
                                      flagcxRedOp_t redOp, int root,
                                      flagcxComm_t comm,
                                      flagcxStream_t stream) {
  if (isHomoInterComm_ && comm->homoInterMyRank == -1) {
    return flagcxSuccess;
  }
  TRACE_CALL(
      "flagcxC2cHomoFunc run: rank = %d, rootRank = %d, sendOffset = %d, "
      "recvOffset = %d, count = %d, "
      "isHomoInterComm = %d, commOp = %d, datatype = %d, redOp = %d, root = %d",
      comm->rank, rootRank_, sendOffset_, recvOffset_, count_, isHomoInterComm_,
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
          isHomoInterComm_ ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpAllReduce:
      return cclAdaptors[flagcxCCLAdaptorDevice]->allReduce(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, redOp,
          isHomoInterComm_ ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpReduceScatter:
      return cclAdaptors[flagcxCCLAdaptorDevice]->reduceScatter(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, redOp,
          isHomoInterComm_ ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpAllGather:
      return cclAdaptors[flagcxCCLAdaptorDevice]->allGather(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype,
          isHomoInterComm_ ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpGather:
      return cclAdaptors[flagcxCCLAdaptorDevice]->gather(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, (rootRank_ == -1) ? root : rootRank_,
          isHomoInterComm_ ? comm->homoInterComm : comm->homo_comm, stream);
    case flagcxCommOpBroadcast:
      return cclAdaptors[flagcxCCLAdaptorDevice]->broadcast(
          const_cast<const void *>(static_cast<void *>(
              static_cast<char *>(const_cast<void *>(sendbuff)) +
              sendOffset_ * getFlagcxDataTypeSize(datatype))),
          static_cast<void *>(static_cast<char *>(recvbuff) +
                              recvOffset_ * getFlagcxDataTypeSize(datatype)),
          count_, datatype, (rootRank_ == -1) ? root : rootRank_,
          isHomoInterComm_ ? comm->homoInterComm : comm->homo_comm, stream);
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

flagcxResult_t flagcxC2cHeteroFunc::run(void *buff, flagcxDataType_t datatype,
                                        flagcxComm_t comm,
                                        flagcxStream_t stream) {
  flagcxHeteroGroupStart();
  for (auto op : p2pOps_) {
    FLAGCXCHECK(op.run(buff, datatype, comm, stream));
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

flagcxC2cPlanner::flagcxC2cPlanner(int sendCount, int recvCount,
                                   flagcxComm_t comm, flagcxCommOp_t commOp,
                                   flagcxRedOp_t redOp)
    : sendCount_(sendCount), recvCount_(recvCount), comm_(comm),
      commOp_(commOp), redOp_(redOp),
      clusterInterRankList_(comm->clusterInterRankList),
      clusterId_(comm->cluster_ids[comm->rank]), rank_(comm->rank),
      homoMyRank_(comm->homo_rank), homoRootRank_(comm->homo_root_rank),
      homoRanks_(comm->homo_ranks), homoInterMyRank_(comm->homoInterMyRank),
      homoInterRootRank_(comm->homoInterRootRank),
      homoInterRanks_(comm->homoInterRanks) {
  // set totalCount_
  totalCount_ = (sendCount_ >= recvCount_) ? sendCount_ : recvCount_;

  // calculate clusterOffset_ and clusterCount_
  clusterOffset_ = 0;
  for (int i = 0; i < clusterId_; ++i) {
    clusterOffset_ += comm_->cluster_sizes[i];
  }
  clusterCount_ = comm_->cluster_sizes[clusterId_];

  // if inter ranks in all clusters equal to 1 （single-nic）
  multiNic_ = 0;
  for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
    if (clusterInterRankList_[i].size() != 1) {
      multiNic_ = 1;
      break;
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

  // init inter-rank buffer info manager
  interRankBufferInfoManager_ = flagcxInterRankBufferInfoManager(totalCount_);
}

flagcxC2cPlanner::~flagcxC2cPlanner() {}

flagcxResult_t flagcxC2cPlanner::refresh(int isSendRecv) {
  if (isSendRecv) {
    int clusterOffset = 0;
    interRankBufferInfoManager_.resetBufferInfo();
    for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
      size_t nClusterInterRanks =
          multiNic_ ? clusterInterRankList_[i].size() : 1;
      int myCount = (sendCount_ > recvCount_)
                        ? totalCount_ / nClusterInterRanks
                        : (sendCount_ * homoRanks_) / nClusterInterRanks;
      int myRes = (sendCount_ > recvCount_)
                      ? totalCount_ % nClusterInterRanks
                      : (sendCount_ * homoRanks_) % nClusterInterRanks;
      for (size_t j = 0; j < nClusterInterRanks; ++j) {
        int finalCount =
            (j == nClusterInterRanks - 1) ? myCount + myRes : myCount;
        for (size_t z = 0; z < clusterInterRankList_.size(); ++z) {
          if (i != z) {
            interRankBufferInfoManager_.pushBackBufferInfo(
                i, clusterInterRankList_[i][j],
                (sendCount_ > recvCount_)
                    ? myCount * j
                    : clusterOffset * sendCount_ + myCount * j,
                finalCount, z, 0, 0, -1, -1);
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

  if (multiNic_) {
    // multi-nic
    // setup preHomoFuncs
    if (eachNicPerRank_) {
      // inter ranks equaling to homo ranks
      // setup preHomoFuncs
      flagcxCommOp_t preHomoFuncCommOp = getC2cHomoCommOp(commOp_, 0, 0);
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
        }
      }
    } else {
      // otherwise
      flagcxCommOp_t preHomoFuncCommOp = getC2cHomoCommOp(commOp_, 0, 1);
      preHomoFuncLoops_ = clusterInterRankList_[clusterId_].size();
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
        } else if (preHomoFuncCommOp == flagcxCommOpAllGather) {
          preHomoFuncList_.emplace_back(-1, 0, clusterOffset_ * sendCount_,
                                        sendCount_, 0, preHomoFuncCommOp);
        }
      }
    }

    // determine hetero send/recv strategies
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

      if (!scheduleCompleted) {
        refresh(0);
        heteroAndHomoInterFuncLoops_ += 1;
      }
    }
    interRankBufferInfoManager_.printBufferInfo(2);

    // setup homoInterFuncs
    flagcxCommOp_t homoInterFuncCommOp = eachNicPerRank_
                                             ? getC2cHomoCommOp(commOp_, 1, 0)
                                             : getC2cHomoCommOp(commOp_, 1, 1);
    for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
      if (homoInterFuncCommOp == flagcxCommOpAllReduce) {
        homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1,
                                        homoInterFuncCommOp);
      } else if (homoInterFuncCommOp == flagcxCommNoOp) {
        homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1,
                                        homoInterFuncCommOp);
      }
    }

    // setup postHomoFuncs
    flagcxCommOp_t postHomoFuncCommOp = eachNicPerRank_
                                            ? getC2cHomoCommOp(commOp_, 2, 0)
                                            : getC2cHomoCommOp(commOp_, 2, 1);
    if (postHomoFuncCommOp == flagcxCommOpAllReduce) {
      postHomoFuncLoops_ = 1;
      postHomoFuncList_.emplace_back(-1, 0, 0, recvCount_, 0,
                                     postHomoFuncCommOp);
    } else if (postHomoFuncCommOp == flagcxCommOpReduceScatter) {
      postHomoFuncLoops_ = 1;
      postHomoFuncList_.emplace_back(-1, clusterOffset_ * recvCount_, 0,
                                     recvCount_, 0, postHomoFuncCommOp);
    } else if (postHomoFuncCommOp == flagcxCommNoOp) {
      postHomoFuncLoops_ = 0;
    } else if (postHomoFuncCommOp == flagcxCommOpBroadcast) {
      postHomoFuncLoops_ = 0;
      for (size_t i = 0; i < clusterInterRankList_[clusterId_].size(); ++i) {
        auto &buffList = interRankBufferInfoManager_.getBufferInfoList(
            clusterId_, clusterInterRankList_[clusterId_][i]);
        for (auto it = buffList.begin(); it != buffList.end(); it++) {
          if (it->isRecv_) {
            postHomoFuncList_.emplace_back(
                clusterInterRankList_[clusterId_][i] - (rank_ - homoMyRank_),
                it->offset_, it->offset_, it->count_, 0, postHomoFuncCommOp);
            postHomoFuncLoops_ += 1;
          }
        }
      }
    }
  } else {
    // single-nic
    // setup preHomoFuncs
    flagcxCommOp_t preHomoFuncCommOp = getC2cHomoCommOp(commOp_, 0, 2);
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
      }
    }

    // setup heteroFuncs
    if (preHomoFuncCommOp == flagcxCommOpReduce) {
      heteroAndHomoInterFuncLoops_ = 1;
      flagcxC2cHeteroFunc heteroFunc = flagcxC2cHeteroFunc();
      int cid = 0;
      for (size_t j = 0; j < clusterInterRankList_.size(); ++j) {
        if (clusterId_ == j) {
          continue;
        }
        int homoRankToRecvFromCluster =
            (comm_->globalrank2homorank[clusterInterRankList_[clusterId_][0]] -
             cid - 1 + homoRanks_) %
            homoRanks_;
        if (homoMyRank_ == homoRankToRecvFromCluster) {
          heteroFunc.addP2pOp(rank_, clusterInterRankList_[j][0], 0,
                              totalCount_, 1);
        }
        int homoRankToSendToCluster =
            (comm_->globalrank2homorank[clusterInterRankList_[j][0]] - cid - 1 +
             comm_->cluster_sizes[j]) %
            comm_->cluster_sizes[j];
        int globalRankToSendToCluster =
            homoRankToSendToCluster -
            comm_->globalrank2homorank[clusterInterRankList_[j][0]] +
            clusterInterRankList_[j][0];
        if (homoMyRank_ ==
            comm_->globalrank2homorank[clusterInterRankList_[clusterId_][0]]) {
          heteroFunc.addP2pOp(rank_, globalRankToSendToCluster, 0, totalCount_,
                              0);
        }
        cid += 1;
      }
      heteroFuncList_.push_back(std::move(heteroFunc));
    } else if (preHomoFuncCommOp == flagcxCommOpGather) {
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
    flagcxCommOp_t homoInterFuncCommOp = getC2cHomoCommOp(commOp_, 1, 2);
    for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
      if (homoInterFuncCommOp == flagcxCommOpAllReduce) {
        homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_,
                                        0, // use homo comm instead
                                        homoInterFuncCommOp);
      } else if (homoInterFuncCommOp == flagcxCommNoOp) {
        homoInterFuncList_.emplace_back(-1, 0, 0, totalCount_, 1,
                                        homoInterFuncCommOp);
      }
    }

    // setup postHomoFuncs
    flagcxCommOp_t postHomoFuncCommOp = getC2cHomoCommOp(commOp_, 2, 2);
    if (postHomoFuncCommOp == flagcxCommOpReduceScatter) {
      postHomoFuncLoops_ = 1;
      postHomoFuncList_.emplace_back(-1, clusterOffset_ * recvCount_, 0,
                                     recvCount_, 0, postHomoFuncCommOp);
    } else if (postHomoFuncCommOp == flagcxCommNoOp) {
      postHomoFuncLoops_ = 0;
    } else if (postHomoFuncCommOp == flagcxCommOpBroadcast) {
      postHomoFuncLoops_ = 0;
      int clusterOffset = 0;
      for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
        postHomoFuncList_.emplace_back(
            clusterInterRankList_[clusterId_][0] - (rank_ - homoMyRank_),
            clusterOffset * sendCount_, clusterOffset * sendCount_,
            comm_->cluster_sizes[i] * sendCount_, 0, postHomoFuncCommOp);
        postHomoFuncLoops_ += 1;
        clusterOffset += comm_->cluster_sizes[i];
      }
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxC2cPlanner::execute(const void *sendbuff, void *recvbuff,
                                         flagcxDataType_t datatype, int root,
                                         flagcxStream_t stream) {
  // redOp validation
  if (redOp_ != flagcxRedNoOp) {
    if (redOp_ != flagcxSum && redOp_ != flagcxMax && redOp_ != flagcxMin) {
      WARN("Unsupported reduction operation %d", redOp_);
      return flagcxInvalidArgument;
    }
  }

  // init scratch buffer if needed
  if (commOp_ == flagcxCommOpReduceScatter ||
      commOp_ == flagcxCommOpBroadcast) {
    deviceAdaptor->deviceMalloc(&scratchBuffer_,
                                totalCount_ * getFlagcxDataTypeSize(datatype),
                                flagcxMemDevice, stream);
  } else {
    scratchBuffer_ = nullptr;
  }

  void *recvTmpBuff = (scratchBuffer_ == nullptr) ? recvbuff : scratchBuffer_;

  // execute preHomoFuncs
  for (int i = 0; i < preHomoFuncLoops_; ++i) {
    preHomoFuncList_[i].run(sendbuff, recvTmpBuff, datatype, redOp_, root,
                            comm_, stream);
  }

  for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
    // execute refreshFunc
    refreshFunc_.run(recvTmpBuff, datatype, stream);

    // TODO: use stream wait rather than stream sync to avoid cpu blocking
    // deviceAdaptor->streamSynchronize(stream);

    // execute heteroFuncs
    heteroFuncList_[i].run(recvTmpBuff, datatype, comm_, stream);

    // TODO: use stream wait rather than stream sync to avoid cpu blocking
    deviceAdaptor->streamSynchronize(stream);
    if (commOp_ != flagcxCommOpBroadcast) {
      // execute homoInterFuncs
      homoInterFuncList_[i].run(recvTmpBuff, recvTmpBuff, datatype, redOp_,
                                root, comm_, stream);
    }
  }

  // execute postHomoFuns
  // we assume that there may be multiple post homo-funcs,
  // but now postHomoFuncLoops_ can only be set to 0 and 1
  for (int i = 0; i < postHomoFuncLoops_; ++i) {
    // execute refresh func
    if (commOp_ != flagcxCommOpBroadcast) {
      refreshFunc_.run(recvTmpBuff, datatype, stream);
    }

    // execute postHomoFunc
    postHomoFuncList_[i].run(recvTmpBuff, recvbuff, datatype, redOp_, root,
                             comm_, stream);
  }

  // free scratch buffer if needed
  if (scratchBuffer_ != nullptr) {
    deviceAdaptor->deviceFree(scratchBuffer_, flagcxMemDevice, stream);
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxC2cPlanner::findStrategyBroadcast(int root) {
  refresh(1);
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
  if (multiNic_) {
    // multi-nic
    preHomoFuncLoops_ = 0;
    flagcxCommOp_t preHomoFuncCommOp = getC2cHomoCommOp(commOp_, 0, 0);
    if (clusterId_ == comm_->cluster_ids[root]) {
      preHomoFuncLoops_ = 1;
      for (int i = 0; i < preHomoFuncLoops_; ++i) {
        if (preHomoFuncCommOp == flagcxCommOpBroadcast) {
          preHomoFuncList_.emplace_back(comm_->globalrank2homorank[root], 0, 0,
                                        recvCount_, 1, preHomoFuncCommOp);
        }
      }
    }
    // otherwise
    // determine hetero send/recv strategies
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

      if (!scheduleCompleted) {
        refresh(0);
        heteroAndHomoInterFuncLoops_ += 1;
      }
    }
    interRankBufferInfoManager_.printBufferInfo(2);

    // setup heteroFuncs
    for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
      flagcxC2cHeteroFunc heteroFunc = flagcxC2cHeteroFunc();
      for (size_t j = 0; j < clusterInterRankList_.size(); ++j) {
        for (size_t z = 0; z < clusterInterRankList_[j].size(); ++z) {
          if (rank_ == clusterInterRankList_[j][z]) {
            auto &rankList =
                interRankBufferInfoManager_.getBufferInfoList(j, rank_);
            for (auto it = rankList.begin(); it != rankList.end(); ++it) {
              if (it->loopId_ == i) {
                heteroFunc.addP2pOp(rank_, it->peerRank_, it->offset_,
                                    it->count_, it->isRecv_);
              }
            }
          }
        }
      }
      heteroFuncList_.push_back(std::move(heteroFunc));
    }

    postHomoFuncLoops_ = 1;
    flagcxCommOp_t postHomoFuncCommOp = getC2cHomoCommOp(commOp_, 2, 0);
    for (int i = 0; i < postHomoFuncLoops_; ++i) {
      postHomoFuncList_.emplace_back(-1, 0, 0, totalCount_, 0,
                                     postHomoFuncCommOp);
    }
  } else {
    // single-nic
    // setup preHomoFuncs
    flagcxCommOp_t preHomoFuncCommOp = getC2cHomoCommOp(commOp_, 0, 2);
    preHomoFuncLoops_ = 0;
    if (clusterId_ == comm_->cluster_ids[root]) {
      preHomoFuncLoops_ = 1;
      for (int i = 0; i < preHomoFuncLoops_; ++i) {
        auto &buffer = interRankBufferInfoManager_
                           .getBufferInfoList(
                               clusterId_, clusterInterRankList_[clusterId_][i])
                           .front();
        if (preHomoFuncCommOp == flagcxCommOpBroadcast) {
          preHomoFuncList_.emplace_back(comm_->globalrank2homorank[root],
                                        buffer.offset_, buffer.offset_,
                                        buffer.count_, 0, preHomoFuncCommOp);
        }
      }
    }

    // setup heteroFuncs
    heteroAndHomoInterFuncLoops_ = 1;
    for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
      WARN("comm_->homo_inter_rank = %d comm_->homo_rank= %d ",
           clusterInterRankList_[clusterId_][0], comm_->homo_rank);

      flagcxC2cHeteroFunc heteroFunc = flagcxC2cHeteroFunc();
      if (clusterInterRankList_[clusterId_][0] == rank_) {
        if (comm_->cluster_ids[rank_] == comm_->cluster_ids[root]) {
          for (int j = 0; j < comm_->nclusters; ++j) {
            if (j == comm_->cluster_ids[root]) {
              continue;
            }
            heteroFunc.addP2pOp(rank_, clusterInterRankList_[j][0], 0,
                                totalCount_, 0);
          }
        } else {
          heteroFunc.addP2pOp(
              rank_, clusterInterRankList_[comm_->cluster_ids[root]][0], 0,
              totalCount_, 1);
        }
      }
      heteroFuncList_.push_back(std::move(heteroFunc));
    }
    flagcxCommOp_t postHomoFuncCommOp = getC2cHomoCommOp(commOp_, 2, 2);
    postHomoFuncLoops_ = 1;
    for (int i = 0; i < postHomoFuncLoops_; ++i) {
      auto &buffer = interRankBufferInfoManager_
                         .getBufferInfoList(
                             clusterId_, clusterInterRankList_[clusterId_][i])
                         .front();
      if (postHomoFuncCommOp == flagcxCommOpBroadcast) {
        postHomoFuncList_.emplace_back(clusterInterRankList_[clusterId_][i] -
                                           (rank_ - homoMyRank_),
                                       buffer.offset_, buffer.offset_,
                                       buffer.count_, 0, postHomoFuncCommOp);
      }
    }
  }
  return flagcxSuccess;
}
