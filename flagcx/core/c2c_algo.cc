#include "c2c_algo.h"

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
    int isRecv, int isScheduled, int peerRank) {
  bufferInfos_[clusterId][rank].emplace_back(offset, count, clusterIdToSend,
                                             isRecv, isScheduled, peerRank);
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
          INFO(FLAGCX_COLL,
               "Initial InterRankBufferInfo: cluster_id = %d, rank = %d, "
               "offset = %d, count = %d, clusterIdToSend = %d, "
               "isRecv = %d, isScheduled = %d, peerRank = %d",
               clusterIt->first, rankIt->first, bufferIt->offset_,
               bufferIt->count_, bufferIt->clusterIdToSend_, bufferIt->isRecv_,
               bufferIt->isScheduled_, bufferIt->peerRank_);
        } else if (step == 1) {
          INFO(FLAGCX_COLL,
               "Internal InterRankBufferInfo: cluster_id = %d, rank = %d, "
               "offset = %d, count = %d, clusterIdToSend = %d, "
               "isRecv = %d, isScheduled = %d, peerRank = %d",
               clusterIt->first, rankIt->first, bufferIt->offset_,
               bufferIt->count_, bufferIt->clusterIdToSend_, bufferIt->isRecv_,
               bufferIt->isScheduled_, bufferIt->peerRank_);
        } else if (step == 2) {
          INFO(FLAGCX_COLL,
               "Final InterRankBufferInfo: cluster_id = %d, rank = %d, "
               "offset = %d, count = %d, clusterIdToSend = %d, "
               "isRecv = %d, isScheduled = %d, peerRank = %d",
               clusterIt->first, rankIt->first, bufferIt->offset_,
               bufferIt->count_, bufferIt->clusterIdToSend_, bufferIt->isRecv_,
               bufferIt->isScheduled_, bufferIt->peerRank_);
        }
      }
    }
  }
}

flagcxC2cP2pOp::flagcxC2cP2pOp(int rank, int offset, int peerRank,
                               int peerOffset, int count, int isRecv)
    : rank_(rank), offset_(offset), peerRank_(peerRank),
      peerOffset_(peerOffset), count_(count), isRecv_(isRecv) {}
flagcxC2cP2pOp::~flagcxC2cP2pOp() {}

flagcxResult_t flagcxC2cP2pOp::run(const void *sendbuff, void *recvbuff,
                                   flagcxDataType_t datatype, flagcxComm_t comm,
                                   flagcxStream_t stream) {
  if (isRecv_) {
    FLAGCXCHECK(flagcxHeteroRecv(
        static_cast<void *>(static_cast<char *>(recvbuff) +
                            peerOffset_ * getFlagcxDataTypeSize(datatype)),
        count_, datatype, peerRank_, comm->hetero_comm, stream));
  } else {
    FLAGCXCHECK(
        flagcxHeteroSend(const_cast<const void *>(static_cast<void *>(
                             static_cast<char *>(const_cast<void *>(sendbuff)) +
                             offset_ * getFlagcxDataTypeSize(datatype))),
                         count_, datatype, rank_, comm->hetero_comm, stream));
  }
  return flagcxSuccess;
}

flagcxC2cHomoFunc::flagcxC2cHomoFunc(int rank, int rootRank, int offset,
                                     int count, flagcxCommOp_t commOp)
    : rank_(rank), rootRank_(rootRank), offset_(offset), count_(count),
      commOp_(commOp) {}

flagcxC2cHomoFunc::~flagcxC2cHomoFunc() {}

flagcxResult_t flagcxC2cHomoFunc::run(const void *sendbuff, void *recvbuff,
                                      flagcxDataType_t datatype, int root,
                                      flagcxComm_t comm,
                                      flagcxStream_t stream) {
  // case commOp_
  // cclAdaptor[commOp_](rank, rootRank_, ...);
  return flagcxSuccess;
}

flagcxC2cHeteroFunc::flagcxC2cHeteroFunc() {}
flagcxC2cHeteroFunc::~flagcxC2cHeteroFunc() {}

void flagcxC2cHeteroFunc::addP2pOp(int rank, int offset, int peerRank,
                                   int peerOffset, int count, int isRecv) {
  p2pOps_.emplace_back(rank, offset, peerRank, peerOffset, count, isRecv);
}

flagcxResult_t flagcxC2cHeteroFunc::run(const void *sendbuff, void *recvbuff,
                                        flagcxDataType_t datatype,
                                        flagcxComm_t comm,
                                        flagcxStream_t stream) {
  flagcxHeteroGroupStart();
  for (auto op : p2pOps_) {
    op.run(sendbuff, recvbuff, datatype, comm, stream);
  }
  flagcxHeteroGroupEnd();
  return flagcxSuccess;
}

flagcxC2cPlanner::flagcxC2cPlanner(
    int clusterId, int rank, int homoMyRank, int homoRootRank, int homoRanks,
    int homoInterMyRank, int homoInterRootRank, int homoInterRanks,
    int totalCount, flagcxCommOp_t commOp, flagcxRedOp_t redOp,
    std::vector<std::vector<int>> &clusterInterRankList)
    : clusterId_(clusterId), rank_(rank), homoMyRank_(homoMyRank),
      homoRootRank_(homoRootRank), homoRanks_(homoRanks),
      homoInterMyRank_(homoInterMyRank), homoInterRootRank_(homoInterRootRank),
      homoInterRanks_(homoInterRanks), totalCount_(totalCount), commOp_(commOp),
      redOp_(redOp), clusterInterRankList_(clusterInterRankList),
      interRankBufferInfoManager_(totalCount) {
  // if inter ranks in all clusters equal to 1 （单网卡）
  multiNic_ = 0;
  for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
    if (clusterInterRankList_[i].size() != 1) {
      multiNic_ = 1;
      break;
    }
  }

  // if inter ranks in current cluster equal to homo ranks
  eachNicPerRank_ = 1;
  for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
    if (clusterInterRankList_[i].size() != homoRanks_) {
      eachNicPerRank_ = 0;
      break;
    }
  }
}

flagcxC2cPlanner::~flagcxC2cPlanner() {}

flagcxResult_t flagcxC2cPlanner::refresh(int isSendRecv) {
  if (isSendRecv) {
    interRankBufferInfoManager_.resetBufferInfo();
    for (size_t i = 0; i < clusterInterRankList_.size(); ++i) {
      int sendCount = totalCount_ / clusterInterRankList_[i].size();
      int sendRes = totalCount_ % clusterInterRankList_[i].size();
      for (size_t j = 0; j < clusterInterRankList_[i].size(); ++j) {
        int finalCount = (j == clusterInterRankList_[i].size() - 1)
                             ? sendCount + sendRes
                             : sendCount;
        for (size_t z = 0; z < clusterInterRankList_.size(); ++z) {
          if (i != z) {
            interRankBufferInfoManager_.pushBackBufferInfo(
                i, clusterInterRankList_[i][j], sendCount * j, finalCount, z, 0,
                0, -1);
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

flagcxResult_t flagcxC2cPlanner::findStrategy() {
  refresh(1);
  if (multiNic_) {
    // multi-nic
    if (eachNicPerRank_) {
      // inter ranks equaling to homo ranks
      preHomoFuncLoops_ = 1;

      // setup preHomoFuncs
      for (int i = 0; i < preHomoFuncLoops_; ++i) {
        if (commOp_ == flagcxCommOpAllReduce) {
          // preHomoFuncList_.emplace_back(...);
        }
      }

      // setup heteroFuncs
      for (int i = 0; i < heteroAndPostHomoFuncLoops_; ++i) {
        // heteroFuncList_.emplace_back();
      }

      // setup postHomoFuncs
      for (int i = 0; i < heteroAndPostHomoFuncLoops_; ++i) {
        if (commOp_ == flagcxCommOpAllReduce) {
          // postHomoFuncList_.emplace_back(...);
        }
      }
    } else {
      // otherwise
      // setup preHomoFuncs
      preHomoFuncLoops_ = clusterInterRankList_[clusterId_].size();
      for (int i = 0; i < preHomoFuncLoops_; ++i) {
        if (commOp_ == flagcxCommOpAllReduce) {
          // preHomoFuncList_.emplace_back(...);
        }
      }

      // determine hetero send/recv strategies
      heteroAndPostHomoFuncLoops_ = 1;
      for (int i = 0; i < heteroAndPostHomoFuncLoops_; ++i) {
        for (size_t j = 0; j < clusterInterRankList_.size(); ++j) {
          for (size_t z = j + 1; z < clusterInterRankList_.size(); ++z) {
            // cluster j send to cluster z, cluster z recv from cluster j
            for (size_t r1 = 0; r1 < clusterInterRankList_[j].size(); ++r1) {
              auto &jList = interRankBufferInfoManager_.getBufferInfoList(
                  j, clusterInterRankList_[j][r1]);
              for (auto it = jList.begin(); it != jList.end();) {
                int erased = 0;
                if (!it->isScheduled_ && !it->isRecv_) {
                  for (size_t r2 = 0; r2 < clusterInterRankList_[z].size();
                       ++r2) {
                    if (interRankBufferInfoManager_.checkIfPossibleToPush(
                            z, clusterInterRankList_[z][r2], it->offset_,
                            it->count_)) {
                      interRankBufferInfoManager_.pushBackBufferInfo(
                          z, clusterInterRankList_[z][r2], it->offset_,
                          it->count_, 0, 1, 1, clusterInterRankList_[j][r1]);
                      it->isScheduled_ = 1;
                      it->peerRank_ = clusterInterRankList_[z][r2];
                      break;
                    }
                  }
                  if (!it->isScheduled_) {
                    int splitCount = 0;
                    int maxSplitCount = 0;
                    int pushMode = 0;
                    int finalPushMode = 0;
                    int splitRank = clusterInterRankList_[z][0];
                    for (size_t r2 = 0; r2 < clusterInterRankList_[z].size();
                         ++r2) {
                      if (interRankBufferInfoManager_
                              .checkIfPossibleToSplitAndPush(
                                  z, clusterInterRankList_[z][r2], it->offset_,
                                  it->count_, &splitCount, &pushMode)) {
                        if (maxSplitCount < splitCount) {
                          maxSplitCount = splitCount;
                          finalPushMode = pushMode;
                          splitRank = clusterInterRankList_[z][r2];
                        }
                      }
                    }
                    if (maxSplitCount > 0) {
                      if (finalPushMode == 0) {
                        interRankBufferInfoManager_.pushBackBufferInfo(
                            z, splitRank, it->offset_, maxSplitCount, 0, 1, 1,
                            clusterInterRankList_[j][r1]);
                        interRankBufferInfoManager_.pushBackBufferInfo(
                            j, clusterInterRankList_[j][r1], it->offset_,
                            maxSplitCount, it->clusterIdToSend_, 0, 1,
                            splitRank);
                        interRankBufferInfoManager_.pushBackBufferInfo(
                            j, clusterInterRankList_[j][r1],
                            it->offset_ + maxSplitCount,
                            it->count_ - maxSplitCount, it->clusterIdToSend_, 0,
                            0, -1);
                      } else if (finalPushMode == 1) {
                        interRankBufferInfoManager_.pushBackBufferInfo(
                            z, splitRank,
                            it->offset_ + it->count_ - maxSplitCount,
                            maxSplitCount, 0, 1, 1,
                            clusterInterRankList_[j][r1]);
                        interRankBufferInfoManager_.pushBackBufferInfo(
                            j, clusterInterRankList_[j][r1],
                            it->offset_ + it->count_ - maxSplitCount,
                            maxSplitCount, it->clusterIdToSend_, 0, 1,
                            splitRank);
                        interRankBufferInfoManager_.pushBackBufferInfo(
                            j, clusterInterRankList_[j][r1], it->offset_,
                            it->count_ - maxSplitCount, it->clusterIdToSend_, 0,
                            0, -1);
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
              // cluster z send to cluster j, cluster j recv from cluster z
              for (size_t r1 = 0; r1 < clusterInterRankList_[z].size(); ++r1) {
                auto &zList = interRankBufferInfoManager_.getBufferInfoList(
                    z, clusterInterRankList_[z][r1]);
                for (auto it = zList.begin(); it != zList.end();) {
                  int erased = 0;
                  if (!it->isScheduled_ && !it->isRecv_) {
                    for (size_t r2 = 0; r2 < clusterInterRankList_[j].size();
                         ++r2) {
                      if (interRankBufferInfoManager_.checkIfPossibleToPush(
                              j, clusterInterRankList_[j][r2], it->offset_,
                              it->count_)) {
                        interRankBufferInfoManager_.pushBackBufferInfo(
                            j, clusterInterRankList_[j][r2], it->offset_,
                            it->count_, 0, 1, 1, clusterInterRankList_[z][r1]);
                        it->isScheduled_ = 1;
                        it->peerRank_ = clusterInterRankList_[j][r2];
                        break;
                      }
                    }
                    if (!it->isScheduled_) {
                      int splitCount = 0;
                      int maxSplitCount = 0;
                      int pushMode = 0;
                      int finalPushMode = 0;
                      int splitRank = clusterInterRankList_[j][0];
                      for (size_t r2 = 0; r2 < clusterInterRankList_[j].size();
                           ++r2) {
                        if (interRankBufferInfoManager_
                                .checkIfPossibleToSplitAndPush(
                                    j, clusterInterRankList_[j][r2],
                                    it->offset_, it->count_, &splitCount,
                                    &pushMode)) {
                          if (maxSplitCount < splitCount) {
                            maxSplitCount = splitCount;
                            finalPushMode = pushMode;
                            splitRank = clusterInterRankList_[j][r2];
                          }
                        }
                      }
                      if (maxSplitCount > 0) {
                        if (finalPushMode == 0) {
                          interRankBufferInfoManager_.pushBackBufferInfo(
                              j, splitRank, it->offset_, maxSplitCount, 0, 1, 1,
                              clusterInterRankList_[z][r1]);
                          interRankBufferInfoManager_.pushBackBufferInfo(
                              z, clusterInterRankList_[z][r1], it->offset_,
                              maxSplitCount, it->clusterIdToSend_, 0, 1,
                              splitRank);
                          interRankBufferInfoManager_.pushBackBufferInfo(
                              z, clusterInterRankList_[z][r1],
                              it->offset_ + maxSplitCount,
                              it->count_ - maxSplitCount, it->clusterIdToSend_,
                              0, 0, -1);
                        } else if (finalPushMode == 1) {
                          interRankBufferInfoManager_.pushBackBufferInfo(
                              j, splitRank,
                              it->offset_ + it->count_ - maxSplitCount,
                              maxSplitCount, 0, 1, 1,
                              clusterInterRankList_[z][r1]);
                          interRankBufferInfoManager_.pushBackBufferInfo(
                              z, clusterInterRankList_[z][r1],
                              it->offset_ + it->count_ - maxSplitCount,
                              maxSplitCount, it->clusterIdToSend_, 0, 1,
                              splitRank);
                          interRankBufferInfoManager_.pushBackBufferInfo(
                              z, clusterInterRankList_[z][r1], it->offset_,
                              it->count_ - maxSplitCount, it->clusterIdToSend_,
                              0, 0, -1);
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
        }

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
          heteroAndPostHomoFuncLoops_ += 1;
        }
      }
      interRankBufferInfoManager_.printBufferInfo(2);

      // setup heteroFuncs
      for (int i = 0; i < heteroAndPostHomoFuncLoops_; ++i) {
        // heteroFuncList_.emplace_back();
      }

      // setup postHomoFuncs
      for (int i = 0; i < heteroAndPostHomoFuncLoops_; ++i) {
        if (commOp_ == flagcxCommOpAllReduce) {
          // postHomoFuncList_.emplace_back(...);
        }
      }
    }
  } else {
    // single-nic
    preHomoFuncLoops_ = 1;

    // setup preHomoFuncs
    for (int i = 0; i < preHomoFuncLoops_; ++i) {
      if (commOp_ == flagcxCommOpAllReduce) {
        // preHomoFuncList_.emplace_back(...);
      }
    }

    // setup heteroFuncs
    for (int i = 0; i < heteroAndPostHomoFuncLoops_; ++i) {
      // heteroFuncList_.emplace_back();
    }

    // setup postHomoFuncs
    for (int i = 0; i < heteroAndPostHomoFuncLoops_; ++i) {
      if (commOp_ == flagcxCommOpAllReduce) {
        // postHomoFuncList_.emplace_back(...);
      }
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxC2cPlanner::execute(const void *sendbuff, void *recvbuff,
                                         flagcxDataType_t datatype, int root,
                                         flagcxComm_t comm,
                                         flagcxStream_t stream) {
  return flagcxSuccess;
}