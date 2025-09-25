#include <cfloat>
#include <map>
#include <vector>
#include "check.h"
#include "flagcx_tuner.h"
#include "param.h"

// Status of a communicator tracked by a tuner
enum TunerCommStatus {
  TunerCommStatusUnset = 0, // initial state
  TunerCommStatusActive = 1, // active and ready to use
  TunerCommStatusDisabled = 2, // disabled temporarily and can be re-enabled
  TunerCommStatusError = 3, // error happened
  TunerCommStatusDestroyed = 4 // destroyed
};

// A category of collective operation. the minimal unit for tuning.
struct TunerCollCategory {
  flagcxCommOp_t collType = flagcxNumCommOps;
  size_t nBytes = 0;
};

bool operator<(const struct TunerCollCategory& lhs, const struct TunerCollCategory& rhs) {
  if (lhs.collType != rhs.collType) {
    return lhs.collType < rhs.collType;
  }
  return lhs.nBytes < rhs.nBytes;
}

static_assert(FLGACX_PROFILE_KEY_MAX_LENGTH >= 20, "FLGACX_PROFILE_KEY_MAX_LENGTH < 20, too short");
static_assert(alignof(struct flagcxProfileKey) >= 8, "We rely on 8-byte alignment for flagcxProfileKey");

// Key used for time profiling
struct TunerProfileKey {
  size_t nBytes;
  uint32_t collType; // flagcxCommOp_t
  uint32_t seqId; // sequence id of collective within this TunerCollCategory
  uint32_t commTagIdx; // index of commTag in configList

  // constructors
  TunerProfileKey() : nBytes(0), collType(0), seqId(0), commTagIdx(0) {}
  TunerProfileKey(size_t n, uint32_t c, uint32_t s, uint32_t i)
    : nBytes(n), collType(c), seqId(s), commTagIdx(i) {}
  TunerProfileKey(const struct flagcxProfileKey& k) {
    nBytes = *(reinterpret_cast<const size_t*>(k.key));
    collType = *(reinterpret_cast<const uint32_t*>(k.key + 8));
    seqId = *(reinterpret_cast<const uint32_t*>(k.key + 12));
    commTagIdx = *(reinterpret_cast<const uint32_t*>(k.key + 16));
  }

  // conversion function
  operator struct flagcxProfileKey() const {
    struct flagcxProfileKey k;
    memset(k.key, 0, FLGACX_PROFILE_KEY_MAX_LENGTH);
    memcpy(k.key, &nBytes, sizeof(size_t));
    memcpy(k.key + 8, &collType, sizeof(uint32_t));
    memcpy(k.key + 12, &seqId, sizeof(uint32_t));
    memcpy(k.key + 16, &commTagIdx, sizeof(uint32_t));
    return k;
  }

  bool operator<(const TunerProfileKey& other) const {
    if (nBytes != other.nBytes) {
      return nBytes < other.nBytes;
    } else if (collType != other.collType) {
      return collType < other.collType;
    } else if (seqId != other.seqId) {
      return seqId < other.seqId;
    }
    return commTagIdx < other.commTagIdx;
  }

  bool operator==(const TunerProfileKey& other) const {
    return (nBytes == other.nBytes) && (collType == other.collType) 
    && (seqId == other.seqId) && (commTagIdx == other.commTagIdx);
  }
};

// number of warmup collectives before tuning. Default to 100. Can be changed later by environment variable.
#define TUNER_WARMUP_COUNT 100

// customized context structure for internal use
struct TunerContext {
  // configure related struct
  std::vector<struct flagcxEnvConfig> configList;
  flagcxDebugLogger_t logger = NULL;
  int envTagIdx = -1; // the index of envTag in configList
  int warmupCnt = TUNER_WARMUP_COUNT; // number of warmup collectives before tuning

  // runtime related struct
  std::map<struct flagcxCommTag, TunerCommStatus> commsStatusMap;
  std::vector<int> activeCommList; // List of active communicator. Holds indices of configList
  std::map<struct flagcxCommTag, int> commTagIdxMap; // map from commTag to configList index
  std::map<TunerCollCategory, uint32_t> collSeqMap; // record the sequence number of each collective category
  std::map<TunerCollCategory, int> collBestCommMap; // record the best communicator for each collective category. value is comm index in configList.
  // TODO add timer structure here.
};

static struct flagcxEnvConfig config1 = {
  .commTag = "defaultConfig1",
  .envCount = 1,
  .envs = {{.type = FLAGCX_ENV_TYPE_CREATION, .name = "NCCL_P2P_NVL_CHUNKSIZE", .value = "1024", .defaultValue = "524288"}}
};

static struct flagcxEnvConfig config2 = {
  .commTag = "defaultConfig2",
  .envCount = 1,
  .envs = {{.type = FLAGCX_ENV_TYPE_CREATION, .name = "NCCL_P2P_NVL_CHUNKSIZE", .value = "524288", .defaultValue = "524288"}}
};

bool operator<(const struct flagcxCommTag& lhs, const struct flagcxCommTag& rhs) {
  return strcmp(lhs.tag, rhs.tag) < 0;
}

bool operator==(const struct flagcxCommTag& lhs, const struct flagcxCommTag& rhs) {
    return strcmp(lhs.tag, rhs.tag) == 0;
}

// A helper function set envs filtered by envType mask
static flagcxResult_t setEnvConfig(const struct flagcxEnvConfig& cfg, uint32_t mask) {
  for (int i = 0; i < cfg.envCount; i++) {
    const auto & item = cfg.envs[i];
    if (item.type & mask) {
      if(setenv(item.name, item.value, 1) != 0) {
        return flagcxInternalError;
      }
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerInit(size_t nRanks, size_t nNodes,
                              flagcxDebugLogger_t logFunction, void **context) {
  struct TunerContext* ctx = new struct TunerContext;
  ctx->configList.push_back(config1);
  ctx->configList.push_back(config2);
  ctx->logger = logFunction;
  *context = ctx;

  // Initialize commsStatusMap and commTagIdxMap
  for (size_t i = 0; i < ctx->configList.size(); ++i) {
    const auto & cfg = ctx->configList[i];
    ctx->commsStatusMap[cfg.commTag] = TunerCommStatusUnset;
    ctx->commTagIdxMap[cfg.commTag] = i;
  }

  // Whether comm tag specified by environment variable
  const char *tagEnv = flagcxGetEnv("FLAGCX_USE_COMM_TAG");
  if (tagEnv != nullptr) {
    struct flagcxCommTag envTag;
    snprintf(envTag.tag, FLAGCX_COMM_TAG_MAX_LENGTH, "%s", tagEnv);
    auto it = ctx->commTagIdxMap.find(envTag);
    if (it == ctx->commTagIdxMap.end()) {
      WARN("Communicator tag %s set by environment not found in config list.", envTag.tag);
      return flagcxInvalidArgument;
    }
    ctx->envTagIdx = it->second;
    INFO(FLAGCX_ENV|FLAGCX_TUNING, "Communicator tag set by environment to %s.", envTag.tag);
  }

  // Whether to change warmup count by environment variable
  const char *warmupEnv = flagcxGetEnv("FLAGCX_TUNER_WARMUP_COUNT");
  if (warmupEnv != nullptr) {
    int val = std::stoi(warmupEnv);
    if (val >= 0) {
      ctx->warmupCnt = val;
      INFO(FLAGCX_ENV|FLAGCX_TUNING, "Tuner warmup count set by environment to %d.", ctx->warmupCnt);
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerGetCandidateNumber(void* context, uint32_t* nCandidates) {
  struct TunerContext* ctx = static_cast<struct TunerContext*>(context);
  *nCandidates = ctx->configList.size();
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerSetCandidate(void* context, uint32_t index,
                                      struct flagcxCommTag* commTag) {
  struct TunerContext* ctx = static_cast<struct TunerContext*>(context);
  if (index >= ctx->configList.size()) {
      WARN("invalid index, index %u must less than config size %zu.",
            index, ctx->configList.size());
      return flagcxInvalidArgument;
  }
  // Set env for that communicator index
  const auto & curCfg = ctx->configList[index];
  FLAGCXCHECK(setEnvConfig(curCfg, FLAGCX_ENV_TYPE_CREATION));
  ctx->commsStatusMap[curCfg.commTag] = TunerCommStatusActive;
  *commTag = curCfg.commTag;
  ctx->activeCommList.push_back(index);
  return flagcxSuccess;
}

// Helper function to find the best communicator for a collective category based on profiling data
// Strategy:
// For each active communicator, check if we have profiling data for that collective category.
// If yes, use that data to calculate the time for that collective category.
// If no, skip that communicator.
// Finally, select the communicator with the minimum time as the best communicator.
static flagcxResult_t findBestComm(struct TunerContext* ctx, const struct TunerCollCategory& cat) {
  int bestCommIdx = -1; // index of best communicator in configList
  float minTime = FLT_MAX;
  // calculate the best communicator based on profiling data
  for (const auto & idx : ctx->activeCommList) {
    // For now, use seqId = 2 metric as the time of that collective category.
    uint32_t seqId = 2;
    TunerProfileKey profileKey(cat.nBytes, static_cast<uint32_t>(cat.collType), seqId, idx);
    // TODO get time from timer
    float time = 0;
    if (time < minTime) {
      minTime = time;
      bestCommIdx = idx;
    }
  }
  if (bestCommIdx == -1) {
    WARN("No best communicator found for collective type %d with size %zu.", cat.collType, cat.nBytes);
    return flagcxInternalError;
  }
  ctx->collBestCommMap[cat] = bestCommIdx;
  return flagcxSuccess;
}

// Communicator selection logic:
// Always favor the communicator specified by environment variable if possible.
// Otherwise,
// for the first WARMUP_COUNT collectives {collType, nBytes}
// we will cycle through all the communicators use round-robin policy.
// after that, we will select the best communicator based on profiling data
// if no profiling data available, we will return flagcxInternalError for now.
flagcxResult_t flagcxTunerGetCollInfo(void* context, flagcxCommOp_t collType,
                                      size_t nBytes, int numPipeOps,
                                      float** collCostTable, int regBuff,
                                      struct flagcxCommTag* commTag) {
  struct TunerContext* ctx = static_cast<struct TunerContext*>(context);
  // Use env comm tag when possible.
  if (ctx->envTagIdx != -1) {
    FLAGCXCHECK(setEnvConfig(ctx->configList[ctx->envTagIdx], FLAGCX_ENV_TYPE_COLL));
    *commTag = ctx->configList[ctx->envTagIdx].commTag;
    INFO(FLAGCX_TUNING, "Use Communicator tag %s set by environment.", commTag->tag);
    return flagcxSuccess;
  }

  // for the first WARMUP_COUNT collectives, use round-robin policy
  struct TunerCollCategory collCat = {collType, nBytes};
  auto it = ctx->collSeqMap.find(collCat);
  uint32_t seqId = 0;
  if (it == ctx->collSeqMap.end()) {
    ctx->collSeqMap[collCat] = 0;
  } else {
    it->second++;
    seqId = it->second;
  }
  if (seqId < ctx->warmupCnt) {
    int idx = seqId % ctx->activeCommList.size();
    const auto & cfg = ctx->configList[ctx->activeCommList[idx]];
    FLAGCXCHECK(setEnvConfig(cfg, FLAGCX_ENV_TYPE_COLL));
    *commTag = cfg.commTag;
    INFO(FLAGCX_TUNING, "Use Communicator tag %s in warmup phase seqId=%u.", commTag->tag, seqId);
    return flagcxSuccess;
  }

  // Select a communicator from active communicators based on profiling data after WARMUP_COUNT collectives.
  // If we do not have a best communicator recorded for this collective category, find it.
  if (ctx->collBestCommMap.find(collCat) == ctx->collBestCommMap.end()) {
    FLAGCXCHECK(findBestComm(ctx, collCat));
  }

  // Use the best communicator calculated earlier.
  auto it2 = ctx->collBestCommMap.find(collCat);
  if (it2 == ctx->collBestCommMap.end()) {
    WARN("No best communicator found for collective type %d with size %zu.", collType, nBytes);
    return flagcxInternalError;
  }
  auto & cfg = ctx->configList[it2->second];
  FLAGCXCHECK(setEnvConfig(cfg, FLAGCX_ENV_TYPE_COLL));
  *commTag = cfg.commTag;
  INFO(FLAGCX_TUNING, "Use Communicator tag %s.", commTag->tag);
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerStartProfiling(void* context, flagcxCommOp_t collType,
                                        size_t nBytes, flagcxStream_t stream,
                                        struct flagcxCommTag commTag,
                                        struct flagcxProfileKey *key) {
  struct TunerContext* ctx = static_cast<struct TunerContext*>(context);
  // Always generate the key, even if we do not do profiling for this collective.
  struct TunerCollCategory collCat = {collType, nBytes};
  auto it = ctx->collSeqMap.find(collCat);
  if (it == ctx->collSeqMap.end()) {
    return flagcxInvalidArgument;
  }
  uint32_t seqId = it->second;

  auto it2 = ctx->commTagIdxMap.find(commTag);
  if (it2 == ctx->commTagIdxMap.end()) {
      WARN("Communicator tag %s not found in config list.", commTag.tag);
      return flagcxInvalidArgument;
  }
  uint32_t commTagIdx = it2->second;
  TunerProfileKey profileKey(nBytes, static_cast<uint32_t>(collType), seqId, commTagIdx);
  *key = profileKey;

  // do profile only for warmup collectives
  if (seqId < ctx->warmupCnt) {
    // TODO: time call start
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerStopProfiling(void* context, struct flagcxProfileKey key){
  struct TunerContext* ctx = static_cast<struct TunerContext*>(context);
  TunerProfileKey profileKey(key);
  // do profile only for warmup collectives
  if (profileKey.seqId < ctx->warmupCnt) {
    // TODO: time call stop and record the time
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerDestroy(void *context) {
  struct TunerContext* ctx = static_cast<struct TunerContext*>(context);
  delete ctx;
  return flagcxSuccess;
}


flagcxTuner_t internalTuner = {
  "internal tuner",
  flagcxTunerInit,
  flagcxTunerGetCandidateNumber,
  flagcxTunerSetCandidate,
  flagcxTunerGetCollInfo,
  flagcxTunerStartProfiling,
  flagcxTunerStopProfiling,
  flagcxTunerDestroy};
