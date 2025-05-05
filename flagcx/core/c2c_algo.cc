#include "c2c_algo.h"

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
          return flagcxCommOpAllReduce;
      }
    case flagcxCommOpAllGather:
      return flagcxCommOpAllGather;
    case flagcxCommOpReduceScatter:
      return flagcxCommOpReduceScatter;
    case flagcxCommOpAlltoAll:
      return flagcxCommOpAlltoAll;
    case flagcxCommOpAlltoAllv:
      return flagcxCommOpAlltoAllv;
    default:
      return flagcxCommOpAllReduce;
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
          static_cast<void *>(
              static_cast<char *>(recvbuff) +
              (recvOffset_ + count_ *
                                 (isHomoInterComm_ ? comm->homoInterMyRank
                                                   : comm->homo_rank) *
                                 getFlagcxDataTypeSize(datatype))),
          count_, datatype, redOp,
          isHomoInterComm_ ? comm->homoInterComm : comm->homo_comm, stream);
    default:
      return flagcxSuccess;
  }
  return flagcxSuccess;
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

flagcxC2cPlanner::flagcxC2cPlanner(int totalCount, int recvCount,
                                   flagcxComm_t comm, flagcxCommOp_t commOp,
                                   flagcxRedOp_t redOp)
    : totalCount_(totalCount), recvCount_(recvCount), comm_(comm),
      commOp_(commOp), redOp_(redOp),
      clusterInterRankList_(comm->clusterInterRankList),
      interRankBufferInfoManager_(totalCount),
      clusterId_(comm->cluster_ids[comm->rank]), rank_(comm->rank),
      homoMyRank_(comm->homo_rank), homoRootRank_(comm->homo_root_rank),
      homoRanks_(comm->homo_ranks), homoInterMyRank_(comm->homoInterMyRank),
      homoInterRootRank_(comm->homoInterRootRank),
      homoInterRanks_(comm->homoInterRanks) {
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

  // TODO: init scratch buffer
  scratchBuffer_ = nullptr;
}

flagcxC2cPlanner::~flagcxC2cPlanner() {
  // TODO: destroy scratch buffer
}

flagcxResult_t flagcxC2cPlanner::refresh(int isSendRecv) {
  if (isSendRecv) {
    interRankBufferInfoManager_.resetBufferInfo();
    for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
      size_t nClusterInterRanks =
          multiNic_ ? clusterInterRankList_[i].size() : 1;
      int sendCount = totalCount_ / nClusterInterRanks;
      int sendRes = totalCount_ % nClusterInterRanks;
      for (size_t j = 0; j < nClusterInterRanks; ++j) {
        int finalCount =
            (j == nClusterInterRanks - 1) ? sendCount + sendRes : sendCount;
        for (size_t z = 0; z < clusterInterRankList_.size(); ++z) {
          if (i != z) {
            interRankBufferInfoManager_.pushBackBufferInfo(
                i, clusterInterRankList_[i][j], sendCount * j, finalCount, z, 0,
                0, -1, -1);
          }
        }
      }
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
          if (!it->isScheduled_ && !it->isRecv_) {
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
          if (!it->isScheduled_ && !it->isRecv_) {
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
          preHomoFuncList_.emplace_back(-1, 0, 0, buffer.count_, 0,
                                        preHomoFuncCommOp);
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

    // setup homoInterFuncs
    flagcxCommOp_t homoInterFuncCommOp = getC2cHomoCommOp(commOp_, 1, 1);
    for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
      if (homoInterFuncCommOp == flagcxCommOpAllReduce) {
        homoInterFuncList_.emplace_back(-1, 0, 0, recvCount_, 1,
                                        homoInterFuncCommOp);
      }
    }

    // setup postHomoFuncs
    flagcxCommOp_t postHomoFuncCommOp = getC2cHomoCommOp(commOp_, 2, 1);
    if (eachNicPerRank_) {
      postHomoFuncLoops_ = 0;
    } else {
      postHomoFuncLoops_ = 1;
    }
    for (int i = 0; i < postHomoFuncLoops_; ++i) {
      if (postHomoFuncCommOp == flagcxCommOpAllReduce) {
        postHomoFuncList_.emplace_back(-1, 0, 0, recvCount_, 0,
                                       postHomoFuncCommOp);
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
      }
    }

    // setup heteroFuncs
    heteroAndHomoInterFuncLoops_ = 1;
    for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
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
    }

    // setup homoInterFuncs
    flagcxCommOp_t homoInterFuncCommOp = getC2cHomoCommOp(commOp_, 1, 2);
    for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
      if (homoInterFuncCommOp == flagcxCommOpAllReduce) {
        homoInterFuncList_.emplace_back(-1, 0, 0, recvCount_, 1,
                                        homoInterFuncCommOp);
      }
    }

    // setup postHomoFuncs
    flagcxCommOp_t postHomoFuncCommOp = getC2cHomoCommOp(commOp_, 2, 2);
    postHomoFuncLoops_ = 1;
    for (int i = 0; i < postHomoFuncLoops_; ++i) {
      if (postHomoFuncCommOp == flagcxCommOpAllReduce) {
        postHomoFuncList_.emplace_back(-1, 0, 0, recvCount_, 0,
                                       postHomoFuncCommOp);
      }
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxC2cPlanner::execute(const void *sendbuff, void *recvbuff,
                                         flagcxDataType_t datatype, int root,
                                         flagcxStream_t stream) {
  // execute preHomoFuncs
  for (int i = 0; i < preHomoFuncLoops_; ++i) {
    preHomoFuncList_[i].run(sendbuff, recvbuff, datatype, redOp_, root, comm_,
                            stream);
  }

  for (int i = 0; i < heteroAndHomoInterFuncLoops_; ++i) {
    // execute refreshFunc
    refreshFunc_.run(recvbuff, datatype, stream);

    // TODO: use stream wait rather than stream sync to avoid cpu blocking
    // deviceAdaptor->streamSynchronize(stream);

    // execute heteroFuncs
    heteroFuncList_[i].run(recvbuff, datatype, comm_, stream);

    // TODO: use stream wait rather than stream sync to avoid cpu blocking
    deviceAdaptor->streamSynchronize(stream);

    // execute homoInterFuncs
    homoInterFuncList_[i].run(recvbuff, recvbuff, datatype, redOp_, root, comm_,
                              stream);
  }

  // execute postHomoFuns
  // we assume that there may be multiple post homo-funcs,
  // but now postHomoFuncLoops_ can only be set to 0 and 1
  for (int i = 0; i < postHomoFuncLoops_; ++i) {
    // for single-nic mode, there is not need to call refresh func
    if (multiNic_) {
      refreshFunc_.run(recvbuff, datatype, stream);
    }
    postHomoFuncList_[i].run(recvbuff, recvbuff, datatype, redOp_, root, comm_,
                             stream);
  }

  return flagcxSuccess;
}