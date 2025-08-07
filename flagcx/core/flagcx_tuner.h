/*************************************************************************
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
 * Copyright (c) 2023, Meta Platforms, Inc. and affiliates.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef FLAGCX_TUNER_H_
#define FLAGCX_TUNER_H_

#include "core.h"
#include "flagcx_common.h"

// API to be implemented by external tuner
typedef struct {
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

  // Gets info (algo, protocol, number of ctas and threads) for a given
  // collective. Inputs:
  //   - context: tuner context object
  //   - collType: collective type , e.g., allreduce, allgather…
  //   - nBytes: collective size in bytes
  //   - collNetTypeSupport: whether collnet supports this type
  //   - nvlsTypeSupport: whether nvlink sharp supports this time
  //   - numPipeOps: number of operations in the group
  //
  // Outputs:
  //   - algorithm: selected algorithm to be used for the given collective
  //   - protocol: selected protocol to be used for the given collective
  //   - nChannels: number of channels (hence SMs) to be used.
  //
  // If getCollInfo() does not return flagcxSuccess, FLAGCX will fall back to
  // the default tuning for the given collective. Also, the plugin is allowed to
  // not set any output, or set only the algorithm and protocol, but not only
  // the algorithm or only the protocol. Unset fields will be set automatically
  // by FLAGCX.
  flagcxResult_t (*getCollInfo)(void *context, flagcxFunc_t collType,
                                size_t nBytes, int collNetSupport,
                                int nvlsSupport, int numPipeOps, int *algorithm,
                                int *protocol, int *nChannels);

  // Terminates the plugin and cleans up any resources that the plugin
  // allocated. context: tuner context object
  flagcxResult_t (*destroy)(void *context);
} flagcxTuner_v2_t;

typedef flagcxTuner_v2_t flagcxTuner_t;

#define FLAGCX_TUNER_PLUGIN_SYMBOL "flagcxTunerPlugin_v2"

#endif
