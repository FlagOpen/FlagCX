/*************************************************************************
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
 * Copyright (c) 2023, Meta Platforms, Inc. and affiliates.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef FLAGCX_TUNER_H_
#define FLAGCX_TUNER_H_

#include "core.h"
#include "tuner.h"
#include "flagcx_common.h"

// API of tuner for internal use only
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

  // Gets number of candidate communicator env settings available from this tuner.
  // Inputs:
  //   - context: tuner context object
  // Outputs:
  //   - nCandidates: number of candidate communicator
  flagcxResult_t (*getCandidateNumber)(void* context, int* nCandidates);

  // Set appropriate environment variables according to index, and return the communicator tag.
  // Note that all the env settings are set before returning from this function.
  // Only env of type FLAGCX_ENV_TYPE_CREATION will be set in this function.
  // Inputs:
  //   - context: tuner context object
  //   - index: index of candidate communicator, range [0, nCandidates)
  // Outputs:
  //   - comm_tag: communicator tag for this particular candidate
  flagcxResult_t (*setCandidate)(void* context, int index, struct flagcxCommTag* comm_tag);

  // Select the best communicator candidate for this collective.
  // All the env of type FLAGCX_ENV_TYPE_COLL and FLAGCX_ENV_TYPE_ONETIME if necessary
  // will be set before returning from this function.
  // Inputs:
  //   - context: tuner context object
  //   - collType: collective type , e.g., allreduce, allgather…
  //   - nBytes: collective size in bytes
  //   - numPipeOps: number of operations in the group
  //   - regBuff: can register user buffer
  //
  // Outputs:
  //   - comm_tag: communicator tag, used to select the underlying communicator.
  //
  // InOut:
  //   - collCostTable: collective cost table.
  //
  flagcxResult_t (*getCollInfo)(void* context, flagcxFunc_t collType,
                                size_t nBytes, int numPipeOps,
                                float** collCostTable, int regBuff,
                                struct flagcxCommTag* comm_tag);

  // Terminates the tuner and cleans up any resources that the tuner allocated.
  // context: tuner context object
  flagcxResult_t (*destroy)(void *context);
} flagcxTuner_t;

#endif
