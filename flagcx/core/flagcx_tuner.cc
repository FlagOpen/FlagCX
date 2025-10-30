#include "flagcx_tuner.h"
#include "check.h"
#include "param.h"
#include "timer.h"
#include "tuner/tuner_util.h"
#include <cfloat>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <sstream>
// A category of collective operation. the minimal unit for tuning.
struct TunerCollCategory {
  flagcxCommOp_t collType = flagcxNumCommOps;
  size_t nBytes = 0;
};

bool operator<(const struct TunerCollCategory &lhs,
               const struct TunerCollCategory &rhs) {
  if (lhs.collType != rhs.collType) {
    return lhs.collType < rhs.collType;
  }
  return lhs.nBytes < rhs.nBytes;
}

static_assert(FLAGCX_PROFILE_KEY_MAX_LENGTH >= 20,
              "FLAGCX_PROFILE_KEY_MAX_LENGTH < 20, too short");

// Key used for time profiling
struct TunerProfileKey {
  size_t nBytes;
  uint32_t collType; // flagcxCommOp_t
  uint32_t seqId;    // sequence id of collective within this TunerCollCategory
  uint32_t commTagIdx; // index of commTag in configList

  // constructors
  TunerProfileKey() : nBytes(0), collType(0), seqId(0), commTagIdx(0) {}
  TunerProfileKey(size_t n, uint32_t c, uint32_t s, uint32_t i)
      : nBytes(n), collType(c), seqId(s), commTagIdx(i) {}
  TunerProfileKey(const struct flagcxProfileKey &k) {
    const char *ptr = k.key;
    memcpy(&nBytes, ptr, sizeof(nBytes));
    ptr += sizeof(nBytes);
    memcpy(&collType, ptr, sizeof(collType));
    ptr += sizeof(collType);
    memcpy(&seqId, ptr, sizeof(seqId));
    ptr += sizeof(seqId);
    memcpy(&commTagIdx, ptr, sizeof(commTagIdx));
  }

  // conversion function
  operator struct flagcxProfileKey() const {
    struct flagcxProfileKey k;
    memset(k.key, 0, FLAGCX_PROFILE_KEY_MAX_LENGTH);
    char *ptr = k.key;
    memcpy(ptr, &nBytes, sizeof(nBytes));
    ptr += sizeof(nBytes);
    memcpy(ptr, &collType, sizeof(collType));
    ptr += sizeof(collType);
    memcpy(ptr, &seqId, sizeof(seqId));
    ptr += sizeof(seqId);
    memcpy(ptr, &commTagIdx, sizeof(commTagIdx));
    return k;
  }

  bool operator<(const TunerProfileKey &other) const {
    if (nBytes != other.nBytes) {
      return nBytes < other.nBytes;
    } else if (collType != other.collType) {
      return collType < other.collType;
    } else if (seqId != other.seqId) {
      return seqId < other.seqId;
    }
    return commTagIdx < other.commTagIdx;
  }

  bool operator==(const TunerProfileKey &other) const {
    return (nBytes == other.nBytes) && (collType == other.collType) &&
           (seqId == other.seqId) && (commTagIdx == other.commTagIdx);
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "{nBytes=" << nBytes << ",collType=" << collType
        << ",seqId=" << seqId << ",commTagIdx=" << commTagIdx << "}";
    return oss.str();
  }
};

// number loops of collectives call before using profiled data.
// Each loop will go thoroughly through all search space of all candidates.
#define TUNER_SEARCH_NLOOPS 5

// customized context structure for internal use
struct flagcxTunerContext {
  // configure related struct
  std::vector<struct flagcxEnvConfig> configList;
  flagcxDebugLogger_t logger = NULL;
  int envTagIdx = -1; // the index of envTag in configList
  uint32_t searchNLoops = TUNER_SEARCH_NLOOPS;

  // runtime related struct
  std::vector<int> activeCommList; // List of active communicator. Holds indices
                                   // of configList
  std::map<struct flagcxCommTag, int>
      commTagIdxMap; // map from commTag to configList index
  std::map<TunerCollCategory, uint32_t>
      collSeqMap; // record the sequence number of each collective category
  std::map<TunerCollCategory, int>
      collBestCommMap; // record the best communicator for each collective
                       // category. value is comm index in configList.

  // timer
  flagcxTimer<TunerProfileKey> timer;
};

bool operator<(const struct flagcxCommTag &lhs,
               const struct flagcxCommTag &rhs) {
  return strcmp(lhs.tag, rhs.tag) < 0;
}

bool operator==(const struct flagcxCommTag &lhs,
                const struct flagcxCommTag &rhs) {
  return strcmp(lhs.tag, rhs.tag) == 0;
}

// A helper function set envs filtered by envType mask
static flagcxResult_t setEnvConfig(const struct flagcxEnvConfig &cfg,
                                   uint32_t mask) {
  for (int i = 0; i < cfg.envCount; i++) {
    const auto &item = cfg.envs[i];
    if (item.type & mask) {
      if (setenv(item.name, item.value, 1) != 0) {
        return flagcxInternalError;
      }
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerInit(size_t nRanks, size_t nNodes,
                               flagcxDebugLogger_t logFunction,
                               void **context) {
  struct flagcxTunerContext *ctx = new struct flagcxTunerContext;
  FLAGCXCHECK(generateCandidate(ctx->configList));
  INFO(FLAGCX_TUNING, "Candidate number: %ld.", ctx->configList.size());
  ctx->logger = logFunction;
  *context = ctx;

  // Initialize commTagIdxMap
  for (size_t i = 0; i < ctx->configList.size(); ++i) {
    const auto &cfg = ctx->configList[i];
    ctx->commTagIdxMap[cfg.commTag] = i;
  }

  // Whether comm tag specified by environment variable
  const char *tagEnv = flagcxGetEnv("FLAGCX_USE_COMM_TAG");
  if (tagEnv != nullptr) {
    struct flagcxCommTag envTag;
    snprintf(envTag.tag, FLAGCX_COMM_TAG_MAX_LENGTH, "%s", tagEnv);
    auto it = ctx->commTagIdxMap.find(envTag);
    if (it == ctx->commTagIdxMap.end()) {
      WARN("Communicator tag %s set by environment not found in config list.",
           envTag.tag);
      return flagcxInvalidArgument;
    }
    ctx->envTagIdx = it->second;
    INFO(FLAGCX_ENV | FLAGCX_TUNING,
         "Communicator tag set by environment to %s.", envTag.tag);
  }

  // Whether to change search nloops by environment variable
  const char *nLoopsEnv = flagcxGetEnv("FLAGCX_TUNER_SEARCH_NLOOPS");
  if (nLoopsEnv != nullptr) {
    try {
      int val = std::stoi(nLoopsEnv);
      if (val >= 1) {
        ctx->searchNLoops = val;
        INFO(FLAGCX_ENV | FLAGCX_TUNING,
             "Tuner search nloops set by environment to %d.",
             ctx->searchNLoops);
      }
    } catch (const std::exception &e) {
      WARN("Invalid value for FLAGCX_TUNER_SEARCH_NLOOPS: %s. Using default.",
           nLoopsEnv);
    }
  }

  // initialize profilingResults pointer
  FLAGCXCHECK(
      flagcxCalloc(&internalTuner.profilingResults, internalTuner.nranks));
  // start timer
  ctx->timer.start();
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerGetCandidateNumber(void *context,
                                             uint32_t *nCandidates) {
  struct flagcxTunerContext *ctx =
      static_cast<struct flagcxTunerContext *>(context);
  *nCandidates = ctx->configList.size();
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerSetCandidate(void *context, uint32_t index,
                                       struct flagcxCommTag *commTag) {
  struct flagcxTunerContext *ctx =
      static_cast<struct flagcxTunerContext *>(context);
  if (index >= ctx->configList.size()) {
    WARN("invalid index, index %u must less than config size %zu.", index,
         ctx->configList.size());
    return flagcxInvalidArgument;
  }
  // Set env for that communicator index
  const auto &curCfg = ctx->configList[index];
  FLAGCXCHECK(setEnvConfig(curCfg, FLAGCX_ENV_TYPE_CREATION));
  *commTag = curCfg.commTag;
  ctx->activeCommList.push_back(index);
  return flagcxSuccess;
}

// Given a startup phase seqId, get the corresponding communicator index in
// configList. Logic must be consistent with getSeqIdForCommIdx.
static int getCommIdxFromSeqId(const struct flagcxTunerContext *ctx,
                               uint32_t seqId) {
  if (ctx->activeCommList.size() == 0) {
    return -1;
  }
  return ctx->activeCommList[seqId % ctx->activeCommList.size()];
}

// Given a communicator index in configList, get the corresponding startup phase
// seqId for specific round. Logic must be consistent with getCommIdxFromSeqId.
static int getSeqIdForCommIdx(const struct flagcxTunerContext *ctx, int commIdx,
                              uint32_t round) {
  int seqId = round * ctx->activeCommList.size();
  bool found = false;
  for (const auto &idx : ctx->activeCommList) {
    if (idx != commIdx) {
      seqId++;
    } else {
      found = true;
      break;
    }
  }
  return (found ? seqId : -1);
}

// add a small factor to avoid switching between two close communicators caused
// by measurement noise
const float tunerProfileFactor = 0.95f;

// Helper function to find the best communicator for a collective category based
// on profiling data Strategy: For each active communicator, check if we have
// profiling data for that collective category. If yes, use that data to
// calculate the time for that collective category. If no, skip that
// communicator. Finally, select the communicator with the minimum time as the
// best communicator.
static flagcxResult_t findBestComm(struct flagcxTunerContext *ctx,
                                   const struct TunerCollCategory &cat) {
  int bestCommIdx = -1; // index of best communicator in configList
  float minTime = FLT_MAX;
  // calculate the best communicator based on profiling data
  for (const auto &idx : ctx->activeCommList) {
    // For now, use round = 2 as the time of that collective category.
    const uint32_t kProfileDataRound =
        2; // Use data from the 3rd round, as it's likely more stable.
    int seqId = getSeqIdForCommIdx(
        ctx, idx,
        std::min(kProfileDataRound,
                 static_cast<uint32_t>(ctx->searchNLoops - 1)));
    TunerProfileKey profileKey(cat.nBytes, static_cast<uint32_t>(cat.collType),
                               static_cast<uint32_t>(seqId), idx);
    struct flagcxRecordKey<TunerProfileKey> rkey(profileKey);
    float duration = ctx->timer.getRecord(rkey, true);

    if (duration <= 0) {
      // no profiling data for this communicator and collective category
      WARN("No profiling data for (commId=%d,coll=%d,size=%zu,seq=%u).", idx,
           cat.collType, cat.nBytes, seqId);
      continue;
    }

    memcpy(internalTuner.profilingResults + internalTuner.rank, &duration,
           sizeof(float));
    // get average duration across all ranks
    FLAGCXCHECK(bootstrapAllGather(internalTuner.bootstrap,
                                   (void *)internalTuner.profilingResults,
                                   sizeof(float)));
    FLAGCXCHECK(bootstrapBarrier(internalTuner.bootstrap, internalTuner.rank,
                                 internalTuner.nranks, 0));
    duration = 0.0f;
    for (int i = 0; i < internalTuner.nranks; ++i) {
      duration += internalTuner.profilingResults[i];
    }
    duration /= internalTuner.nranks;

    INFO(FLAGCX_TUNING,
         "Profiling data for (commId=%d,coll=%d,size=%zu,seq=%u) is %.3fms.",
         idx, cat.collType, cat.nBytes, seqId, duration);

    if (duration < minTime * tunerProfileFactor) {
      minTime = duration;
      bestCommIdx = idx;
    }
  }
  if (bestCommIdx == -1) {
    WARN("No best communicator found for (coll=%d, size=%zu).", cat.collType,
         cat.nBytes);
    return flagcxInternalError;
  }

  const flagcxEnvConfig& bestConfig = ctx->configList[bestCommIdx];
  std::stringstream msg;
  msg << "Best Envs: ";
  for (int i = 0; i < bestConfig.envCount; i++) {
      msg << bestConfig.envs[i].name << "=" << bestConfig.envs[i].value
          << "(default=" << bestConfig.envs[i].defaultValue << ")";
      if (i < bestConfig.envCount - 1) msg << "  ";
  }
  // Output the best config
  INFO(FLAGCX_TUNING, "Find (coll=%d,size=%zu) best CommId=%d. %s", cat.collType,
       cat.nBytes, bestCommIdx, msg.str().c_str());
  
  ctx->collBestCommMap[cat] = bestCommIdx;
  return flagcxSuccess;
}

// Communicator selection logic:
// Always favor the communicator specified by environment variable if possible.
// Otherwise,
// for the first searchNLoops * activeCommCount collectives {collType, nBytes}
// we will cycle through all the communicators use round-robin policy.
// after that, we will select the best communicator based on profiling data
// if no profiling data available, we will return flagcxInternalError for now.
flagcxResult_t flagcxTunerGetCollInfo(void *context, flagcxCommOp_t collType,
                                      size_t nBytes, int numPipeOps,
                                      float **collCostTable, int regBuff,
                                      struct flagcxCommTag *commTag) {
  struct flagcxTunerContext *ctx =
      static_cast<struct flagcxTunerContext *>(context);
  // Use env comm tag when possible.
  if (ctx->envTagIdx != -1) {
    FLAGCXCHECK(
        setEnvConfig(ctx->configList[ctx->envTagIdx], FLAGCX_ENV_TYPE_COLL));
    *commTag = ctx->configList[ctx->envTagIdx].commTag;
    INFO(FLAGCX_TUNING, "Use Communicator tag %s set by environment.",
         commTag->tag);
    return flagcxSuccess;
  }

  // for the first searchNLoops * activeCommCount collectives, use round-robin
  // policy
  struct TunerCollCategory collCat = {collType, nBytes};
  auto it = ctx->collSeqMap.find(collCat);
  uint32_t seqId = 0;
  if (it == ctx->collSeqMap.end()) {
    ctx->collSeqMap[collCat] = 0;
  } else {
    it->second++;
    seqId = it->second;
  }

  if (seqId < ctx->searchNLoops * ctx->activeCommList.size()) {
    int idx = getCommIdxFromSeqId(ctx, seqId);
    if (idx == -1) {
      WARN("No active communicator found for startup phase seqId=%u.", seqId);
      return flagcxInternalError;
    }
    const auto &cfg = ctx->configList[idx];
    FLAGCXCHECK(setEnvConfig(cfg, FLAGCX_ENV_TYPE_COLL));
    *commTag = cfg.commTag;
    INFO(FLAGCX_TUNING, "Use Communicator tag %s in startup phase seqId=%u.",
         commTag->tag, seqId);
    return flagcxSuccess;
  }

  // Select a communicator from active communicators based on profiling data
  // after searchNLoops * activeCommCount collectives. If we do not have a best
  // communicator recorded for this collective category, find it.
  if (ctx->collBestCommMap.find(collCat) == ctx->collBestCommMap.end()) {
    FLAGCXCHECK(findBestComm(ctx, collCat));
  }

  // Use the best communicator calculated earlier.
  auto it2 = ctx->collBestCommMap.find(collCat);
  if (it2 == ctx->collBestCommMap.end()) {
    WARN("No best communicator found for collective type %d with size %zu.",
         collType, nBytes);
    return flagcxInternalError;
  }
  auto &cfg = ctx->configList[it2->second];
  FLAGCXCHECK(setEnvConfig(cfg, FLAGCX_ENV_TYPE_COLL));
  *commTag = cfg.commTag;
  INFO(FLAGCX_TUNING, "Use Communicator tag %s based on profile data.",
       commTag->tag);
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerStartProfiling(void *context, flagcxCommOp_t collType,
                                         size_t nBytes, flagcxStream_t stream,
                                         const struct flagcxCommTag *commTag,
                                         struct flagcxProfileKey *key) {
  struct flagcxTunerContext *ctx =
      static_cast<struct flagcxTunerContext *>(context);
  struct TunerCollCategory collCat = {collType, nBytes};

  auto it = ctx->collSeqMap.find(collCat);
  uint32_t seqId = 0;
  if (it != ctx->collSeqMap.end()) {
    seqId = it->second;
  } else {
    WARN("Collective category (coll=%d,size=%zu) not found in collSeqMap.",
         collType, nBytes);
    return flagcxInvalidArgument;
  }

  auto it2 = ctx->commTagIdxMap.find(*commTag);
  if (it2 == ctx->commTagIdxMap.end()) {
    WARN("Communicator tag %s not found in config list.", commTag->tag);
    return flagcxInvalidArgument;
  }
  uint32_t commTagIdx = it2->second;

  // Always generate the key, even if we do not do profiling for this
  // collective.
  TunerProfileKey profileKey(nBytes, static_cast<uint32_t>(collType), seqId,
                             commTagIdx);
  /*
  INFO(FLAGCX_TUNING, "Enter StartProfiling for
  (commId=%d,coll=%d,size=%zu,seq=%u).", profileKey.commTagIdx,
  profileKey.collType, profileKey.nBytes, profileKey.seqId);
  */
  *key = profileKey;

  // do profile only for startup collectives
  if (seqId < ctx->searchNLoops * ctx->activeCommList.size()) {
    struct flagcxRecordKey<TunerProfileKey> rkey(profileKey);
    FLAGCXCHECK(ctx->timer.begin(rkey, stream));
  }
  /*
  INFO(FLAGCX_TUNING, "Leave StartProfiling for
  (commId=%d,coll=%d,size=%zu,seq=%u).", profileKey.commTagIdx,
  profileKey.collType, profileKey.nBytes, profileKey.seqId);
  */
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerStopProfiling(void *context,
                                        const struct flagcxProfileKey *key) {
  struct flagcxTunerContext *ctx =
      static_cast<struct flagcxTunerContext *>(context);
  TunerProfileKey profileKey(*key);
  /*
  INFO(FLAGCX_TUNING, "Enter StopProfiling for
  (commId=%d,coll=%d,size=%zu,seq=%u).", profileKey.commTagIdx,
  profileKey.collType, profileKey.nBytes, profileKey.seqId);
  */
  // do profile only for startup collectives
  if (profileKey.seqId < ctx->searchNLoops * ctx->activeCommList.size()) {
    struct flagcxRecordKey<TunerProfileKey> rkey(profileKey);
    FLAGCXCHECK(ctx->timer.end(rkey));
  }
  /*
  INFO(FLAGCX_TUNING, "Leave StopProfiling for
  (commId=%d,coll=%d,size=%zu,seq=%u).", profileKey.commTagIdx,
  profileKey.collType, profileKey.nBytes, profileKey.seqId);
  */
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerDestroy(void *context) {
  struct flagcxTunerContext *ctx =
      static_cast<struct flagcxTunerContext *>(context);
  // INFO(FLAGCX_TUNING, "Enter flagcxTunerDestroy.");

  // stop timer
  ctx->timer.stop();
  free(internalTuner.profilingResults);
  delete ctx;
  return flagcxSuccess;
}

flagcxTuner_t internalTuner = {"internal tuner",
                               NULL, // assigned during flagcxCommInit
                               0,    // assigned during flagcxCommInit
                               0,    // assigned during flagcxCommInit
                               NULL, // initialized during tunerInit
                               flagcxTunerInit,
                               flagcxTunerGetCandidateNumber,
                               flagcxTunerSetCandidate,
                               flagcxTunerGetCollInfo,
                               flagcxTunerStartProfiling,
                               flagcxTunerStopProfiling,
                               flagcxTunerDestroy};
