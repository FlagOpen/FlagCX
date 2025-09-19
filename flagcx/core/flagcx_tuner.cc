#include <map>
#include <vector>
#include "flagcx_tuner.h"
#include "param.h"
#include "check.h"

// Status of a communicator tracked by a tuner
enum TunerCommStatus {
  TunerCommStatusUnset = 0, // initial state
  TunerCommStatusActive = 1, // active and ready to use
  TunerCommStatusDisabled = 2, // disabled temporarily and can be re-enabled
  TunerCommStatusError = 3, // error happened
  TunerCommStatusDestroyed = 4 // destroyed
};

// customized context structure for internal use
struct TunerContext {
  std::map<struct flagcxCommTag, TunerCommStatus> commsStatusMap;
  std::vector<struct flagcxEnvConfig> configList;
  flagcxDebugLogger_t logger = NULL;
  struct flagcxCommTag envTag = {.tag = ""}; // envTag specified by FLAGCX_USE_COMM_TAG environment
  int envTagIdx = -1; // the index of envTag in configList
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
  //TODO: read config from file.
  //ctx->configList.push_back(config1);
  (void) config1;
  ctx->configList.push_back(config2);
  ctx->logger = logFunction;
  *context = ctx;

  // Whether comm tag specified by environment variable
  const char *tagEnv = flagcxGetEnv("FLAGCX_USE_COMM_TAG");
  if (tagEnv != nullptr) {
    snprintf(ctx->envTag.tag, FLAGCX_COMM_TAG_MAX_LENGTH, "%s", tagEnv);
    for (size_t i = 0; i < ctx->configList.size(); ++i) {
      if (ctx->envTag == ctx->configList[i].commTag) {
        ctx->envTagIdx = i;
        INFO(FLAGCX_INIT, "Communicator tag set by environment to %s.", ctx->envTag.tag);
        break;
      }
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
  return flagcxSuccess;
}

flagcxResult_t flagcxTunerGetCollInfo(void* context, flagcxCommOp_t collType,
                                      size_t nBytes, int numPipeOps,
                                      float** collCostTable, int regBuff,
                                      struct flagcxCommTag* commTag) {
  struct TunerContext* ctx = static_cast<struct TunerContext*>(context);
  // Use env comm tag when possible.
  if (ctx->envTagIdx != -1) {
    FLAGCXCHECK(setEnvConfig(ctx->configList[ctx->envTagIdx], FLAGCX_ENV_TYPE_COLL));
    *commTag = ctx->envTag;
    INFO(FLAGCX_COLL, "Use Communicator tag %s set by environment.", commTag->tag);
    return flagcxSuccess;
  }
  // TODO: Implement logic to select the best communicator based on performance metrics.
  // Currently, selects the first active communicator found.
  for (size_t i = 0; i < ctx->configList.size(); ++i) {
    const auto & cfg = ctx->configList[i];
    const auto it = ctx->commsStatusMap.find(cfg.commTag);
    if (it != ctx->commsStatusMap.end() &&
        it->second == TunerCommStatusActive) {
      FLAGCXCHECK(setEnvConfig(cfg, FLAGCX_ENV_TYPE_COLL));
      *commTag = cfg.commTag;
      INFO(FLAGCX_COLL, "Use Communicator tag %s.", commTag->tag);
      return flagcxSuccess;
    }
  }
  return flagcxInternalError;
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
    flagcxTunerDestroy};
