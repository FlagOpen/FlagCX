#ifndef FLAGCX_ADAPTOR_TUNER_H_
#define FLAGCX_ADAPTOR_TUNER_H_

#include "debug.h"
#include "flagcx.h"

#ifdef __cplusplus
extern "C" {
#endif

// Flagcx environment variable types
enum flagcxEnvType {
  FLAGCX_ENV_TYPE_CREATION = 0x01, // envs used when creating the communicator
  FLAGCX_ENV_TYPE_COLL =
      0x02, // envs should be set for every specific collective at runtime
  FLAGCX_ENV_TYPE_ONETIME =
      0x04 // envs take effect only the first time they are used at runtime
};

#define FLAGCX_ENV_ENTITY_MAX_LENGTH                                           \
  128 // max length of a single env name or value
struct flagcxEnvEntity {
  uint32_t type; // env type bitmap, OR of FLAGCX_ENV_TYPE_*
  char name[FLAGCX_ENV_ENTITY_MAX_LENGTH];
  char value[FLAGCX_ENV_ENTITY_MAX_LENGTH];
  char defaultValue[FLAGCX_ENV_ENTITY_MAX_LENGTH]; // default value used to
                                                   // unset this env
};

#define FLAGCX_COMM_TAG_MAX_LENGTH 64 // max length of communicator tag
// A tag to identify a specific communicator configuration
struct flagcxCommTag {
  char tag[FLAGCX_COMM_TAG_MAX_LENGTH]; // tag string
};

// Structure of environment list for a specific communicator candidate
// configuration
#define FLAGCX_ENV_LIST_MAX_LENGTH 32 // max length of env list per communicator
struct flagcxEnvConfig {
  flagcxCommTag commTag; // communicator tag
  int envCount;          // number of env vars
  struct flagcxEnvEntity envs[FLAGCX_ENV_LIST_MAX_LENGTH];
};

#define FLAGCX_ENV_CONFIG_MAX_COUNT 4 // max number of communicator configs
// A list of flagcxEnvConfig
struct flagcxEnvConfigList {
  int nConfigs; // number of communicator configs
  struct flagcxEnvConfig configList[FLAGCX_ENV_CONFIG_MAX_COUNT];
};

// Used to pair ProfilingStart()/ProfilingStop() calls
#define FLAGCX_PROFILE_KEY_MAX_LENGTH 64 // max length of profiling key string
struct flagcxProfileKey {
  char key[FLAGCX_PROFILE_KEY_MAX_LENGTH]; // profiling key string
};

#ifdef __cplusplus
} // end extern "C"
#endif

#endif // FLAGCX_ADAPTOR_TUNER_H_
