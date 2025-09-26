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
#define FLGACX_PROFILE_KEY_MAX_LENGTH 64 // max length of profiling key string
struct alignas(16) flagcxProfileKey {
  char key[FLGACX_PROFILE_KEY_MAX_LENGTH]; // profiling key string
};

struct flagcxTuner {
  // Name of the tuner
  const char *name;

  // Initializes tuner states.
  // Inputs:
  //   - nRanks: number of ranks in current communicator. Each communicator
  //   initialize its own tuner.
  //   - nNodes: number of nodes in current communicator.
  //   - logFunction: a logFunction can be useful to integrate logging together
  //   with FLAGCX core.
  // Outputs:
  //   - context: tuner context object
  flagcxResult_t (*init)(size_t nRanks, size_t nNodes,
                         flagcxDebugLogger_t logFunction, void **context);

  // Gets number of candidate communicator env settings available from this
  // tuner. Inputs:
  //   - context: tuner context object
  // Outputs:
  //   - nCandidates: number of candidate communicator
  flagcxResult_t (*getCandidateNumber)(void *context, uint32_t *nCandidates);

  // Set appropriate environment variables according to index, and return the
  // communicator tag. Note that all the env settings are set before returning
  // from this function. Only env of type FLAGCX_ENV_TYPE_CREATION will be set
  // in this function. Inputs:
  //   - context: tuner context object
  //   - index: index of candidate communicator, range [0, nCandidates)
  // Outputs:
  //   - commTag: communicator tag for this particular candidate
  flagcxResult_t (*setCandidate)(void *context, uint32_t index,
                                 struct flagcxCommTag *commTag);

  // Select the best communicator candidate for this collective.
  // All the env of type FLAGCX_ENV_TYPE_COLL and FLAGCX_ENV_TYPE_ONETIME if
  // necessary will be set before returning from this function. Inputs:
  //   - context: tuner context object
  //   - collType: collective type , e.g., allreduce, allgather…
  //   - nBytes: collective size in bytes
  //   - numPipeOps: number of operations in the group
  //   - regBuff: If non-zero, register user buffer is used.
  // Outputs:
  //   - commTag: communicator tag, used to select the underlying communicator.
  //
  // InOut:
  //   - collCostTable: collective cost table.  the caller is responsible for
  //   allocating and
  //                    deallocating the memory
  //
  flagcxResult_t (*getCollInfo)(void *context, flagcxCommOp_t collType,
                                size_t nBytes, int numPipeOps,
                                float** collCostTable, int regBuff,
                                struct flagcxCommTag* commTag);
  
  // Start profiling for a specific collective with given parameters.
  // Inputs:
  //   - context: tuner context object
  //   - collType: collective type , e.g., allreduce, allgather…
  //   - nBytes: collective size in bytes
  //   - stream: the stream that the collective will be launched on
  //   - commTag: communicator tag
  // Outputs:
  //   - key: profiling key to pair with stopProfiling
  //
  flagcxResult_t (*startProfiling)(void* context, flagcxCommOp_t collType,
                                  size_t nBytes, flagcxStream_t stream,
                                  struct flagcxCommTag commTag,
                                  struct flagcxProfileKey *key);

  // Stop profiling for a specific collective with given key.
  // Inputs:
  //   - context: tuner context object
  //   - key: profiling key returned by startProfiling
  // Outputs:
  //   - None
  //
  flagcxResult_t (*stopProfiling)(void* context, struct flagcxProfileKey key);

  // Terminates the tuner and cleans up any resources that the tuner allocated.
  flagcxResult_t (*destroy)(void *context);
};

typedef struct flagcxTuner flagcxTuner_t;
#ifdef __cplusplus
} // end extern "C"
#endif

#endif // FLAGCX_ADAPTOR_TUNER_H_
