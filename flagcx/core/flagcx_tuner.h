/*************************************************************************
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
 * Copyright (c) 2023, Meta Platforms, Inc. and affiliates.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef FLAGCX_TUNER_H_
#define FLAGCX_TUNER_H_

#include <string>
#include <map>
#include <utility>
#include <vector>
#include "core.h"
#include "flagcx_common.h"

// Environment list type for tuner. Each element is a pair of <env_name, env_value>
using TunerEnvList = std::vector<std::pair<std::string, std::string>>;

// API of tuner to be implemented by internal only
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

  // Gets all candidate communicator settings from this tuner.
  // Note that these env settings take effect only for creating the underlying 
  // communicator. Each communicator has its own unique communicator tag.
  // Inputs:
  //   - context: tuner context object
  // Outputs:
  //   - comm_env_map: a map of communicator tag and corresponding env settings(a list of env)
  //                   for creating the underlying communicator.
  //                   key: communicator tag, value: env setting list.
  flagcxResult_t (*getCandidates)(void* context,
                                 std::map<std::string, TunerEnvList>& comm_env_map);

  // Gets communicator tag and corresponding collective env setting(a list of env) for a given
  // collective op. Note these env settings take effect only for this specific collective.
  // Inputs:
  //   - context: tuner context object
  //   - collType: collective type , e.g., allreduce, allgather…
  //   - nBytes: collective size in bytes
  //   - numPipeOps: number of operations in the group
  //   - regBuff: can register user buffer
  //
  // Outputs:
  //   - comm_tag: communicator tag, used to select the underlying communicator.
  //   - env_list: a list of env settings for this specific collective op.
  //
  // InOut:
  //   - collCostTable: collective cost table.
  //
  flagcxResult_t (*getCollInfo)(void* context, flagcxFunc_t collType,
                                size_t nBytes, int numPipeOps,
                                float** collCostTable, int regBuff,
                                std::string& comm_tag,
                                TunerEnvList& env_list);

  // Terminates the tuner and cleans up any resources that the tuner allocated.
  // context: tuner context object
  flagcxResult_t (*destroy)(void *context);
} flagcxTuner_t;

#define FLAGCX_TUNER_PLUGIN_SYMBOL "flagcxTunerPlugin_v1"

#endif
