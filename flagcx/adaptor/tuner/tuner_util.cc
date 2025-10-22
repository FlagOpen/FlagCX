#include "tuner/tuner_util.h"

#ifdef USE_NVIDIA_ADAPTOR
static struct flagcxEnvConfig config1 = {
    "defaultConfig1",
    1,
    {FLAGCX_ENV_TYPE_CREATION, "NCCL_P2P_NVL_CHUNKSIZE", "1024", "524288"}};
static struct flagcxEnvConfig config2 = {
    "defaultConfig2",
    1,
    {FLAGCX_ENV_TYPE_CREATION, "NCCL_P2P_NVL_CHUNKSIZE", "524288", "524288"}};

// demo
flagcxResult_t loadConfigList(std::vector<struct flagcxEnvConfig> &cfgList) {
  cfgList.push_back(config1);
  cfgList.push_back(config2);
  return flagcxSuccess;
}
#else
// Default empty implementation
flagcxResult_t loadConfigList(std::vector<struct flagcxEnvConfig> &cfgList) {
  return flagcxSuccess;
}
#endif
