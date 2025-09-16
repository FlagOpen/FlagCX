#include "flagcx_tuner.h"
#include <map>
#include <vector>

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
};

static struct flagcxEnvConfig config1 = {
  .commTag = "default config 1",
  .envCount = 1,
  .envs = {{.type = 1, .name = "NCCL_BUFFSIZE", .value = "1024", .defaultValue = "4194304"}}
};

static struct flagcxEnvConfig config2 = {
  .commTag = "default config 2",
  .envCount = 1,
  .envs = {{.type = 1, .name = "NCCL_BUFFSIZE", .value = "4194304", .defaultValue = "4194304"}}
};

bool operator<(const struct flagcxCommTag& lhs, const struct flagcxCommTag& rhs) {
  return strcmp(lhs.tag, rhs.tag) < 0;
}

flagcxResult_t TunerInit(size_t nRanks, size_t nNodes,
                         flagcxDebugLogger_t logFunction, void **context) {
  struct TunerContext* ctx = new struct TunerContext;
  //TODO: read config from file.
  ctx->configList.push_back(config1);
  ctx->configList.push_back(config2);
  ctx->logger = logFunction;
  *context = ctx;
  return flagcxSuccess;
}

flagcxResult_t TunerGetCandidateNumber(void* context, int* nCandidates) {
  struct TunerContext* ctx = (struct TunerContext*)context;
  *nCandidates = ctx->configList.size();
  return flagcxSuccess;
}

flagcxResult_t TunerSetCandidate(void* context, int index,
                                const struct flagcxCommTag* commTag) {
  struct TunerContext* ctx = (struct TunerContext*)context;
  if (index >= ctx->configList.size()) {
      WARN("invalid index, index %d must less than config size %ld, reset index to 0", 
            index, ctx->configList.size());
      index = 0;
  }
  // Set env for that communicator index
  bool succ;
  const auto & curCfg = ctx->configList[index];
  for (int i = 0; i < curCfg.envCount; i++) {
    const auto & item = curCfg.envs[i];
    if(setenv(item.name, item.value, 1) != 0) {
      succ = false;
      break;
    }
  }
  // update status for that communicator
  if (!succ) {
    ctx->commsStatusMap[curCfg.commTag] = TunerCommStatusError;
  } else {
    ctx->commsStatusMap[curCfg.commTag] = TunerCommStatusActive;
  }
  commTag = &curCfg.commTag;
  return flagcxSuccess;
}

flagcxResult_t TunerGetCollInfo(void* context, flagcxCommOp_t collType,
                                size_t nBytes, int numPipeOps,
                                float** collCostTable, int regBuff,
                                const struct flagcxCommTag* commTag) {
  struct TunerContext* ctx = (struct TunerContext*)context;
  for (const auto & item : ctx->commsStatusMap) {
    if (item.second == TunerCommStatusActive) {
      commTag = &(item.first);
      break;
    }
  }
  return flagcxSuccess;
}

flagcxResult_t TunerDestroy(void *context) {
  struct TunerContext* ctx = (struct TunerContext*)context;
  free(ctx);
  return flagcxSuccess;
}


flagcxTuner_t internalTuner = {
    "internal tuner",
    TunerInit,
    TunerGetCandidateNumber,
    TunerSetCandidate,
    TunerGetCollInfo,
    TunerDestroy};
