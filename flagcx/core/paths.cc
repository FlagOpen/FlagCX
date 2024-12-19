/*************************************************************************
 * Copyright (c) 2018-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "core.h"
#include "graph.h"
#include "topo.h"
#include "comm.h"
#include "net.h"

// Pre-compute GPU->NIC, GPU->GPU and NIC->GPU paths

struct flagcxTopoNodeList {
  struct flagcxTopoNode* list[FLAGCX_TOPO_MAX_NODES];
  int count;
};

static flagcxResult_t getPath(struct flagcxTopoSystem* system, struct flagcxTopoNode* node, int t, int64_t id, struct flagcxTopoLinkList** path) {
  for (int i=0; i<system->nodes[t].count; i++) {
    if (system->nodes[t].nodes[i].id == id) {
      *path = node->paths[t]+i;
      return flagcxSuccess;
    }
  }
  WARN("Could not find node of type %d id %lx", t, id);
  return flagcxInternalError;
}

FLAGCX_PARAM(NvbDisable, "NVB_DISABLE", 0);

static flagcxResult_t flagcxTopoSetPaths(struct flagcxTopoNode* baseNode, struct flagcxTopoSystem* system) {
  if (baseNode->paths[baseNode->type] == NULL) {
    FLAGCXCHECK(flagcxCalloc(baseNode->paths+baseNode->type, system->nodes[baseNode->type].count));
  }

  // breadth-first search to set all paths to that node in the system
  struct flagcxTopoNodeList nodeList;
  struct flagcxTopoNodeList nextNodeList;
  nodeList.count = 1; nodeList.list[0] = baseNode;
  nextNodeList.count = 0;
  struct flagcxTopoLinkList* basePath;
  FLAGCXCHECK(getPath(system, baseNode, baseNode->type, baseNode->id, &basePath));
  basePath->count = 0;
  basePath->bw = LOC_BW;
  basePath->type = PATH_LOC;

  while (nodeList.count) {
    nextNodeList.count = 0;
    for (int n=0; n<nodeList.count; n++) {
      struct flagcxTopoNode* node = nodeList.list[n];
      struct flagcxTopoLinkList* path;
      FLAGCXCHECK(getPath(system, node, baseNode->type, baseNode->id, &path));
      for (int l=0; l<node->nlinks; l++) {
        struct flagcxTopoLink* link = node->links+l;
        struct flagcxTopoNode* remNode = link->remNode;
        if (remNode->paths[baseNode->type] == NULL) {
          FLAGCXCHECK(flagcxCalloc(remNode->paths+baseNode->type, system->nodes[baseNode->type].count));
          for (int i=0; i<system->nodes[baseNode->type].count; i++) remNode->paths[baseNode->type][i].type = PATH_DIS;
        }
        struct flagcxTopoLinkList* remPath;
        FLAGCXCHECK(getPath(system, remNode, baseNode->type, baseNode->id, &remPath));
        float bw = std::min(path->bw, link->bw);

        // allow routing through a GPU only as 1 hop
        if (node != baseNode && node->type == GPU &&
            (flagcxParamNvbDisable() || link->type != LINK_NVL || remNode->type != GPU || path->count > 1)) continue;

        if ((remPath->bw == 0 || remPath->count > path->count) && remPath->bw < bw) {
          // Find reverse link
          for (int l=0; l<remNode->nlinks; l++) {
            if (remNode->links[l].remNode == node && remNode->links[l].type == link->type) {
              remPath->list[0] = remNode->links+l;
              break;
            }
          }
          if (remPath->list[0] == NULL) {
            WARN("Failed to find reverse path from remNode %d/%lx nlinks %d to node %d/%lx",
                 remNode->type, remNode->id, remNode->nlinks, node->type, node->id);
            return flagcxInternalError;
          }
          // Copy the rest of the path
          for (int i=0; i<path->count; i++) remPath->list[i+1] = path->list[i];
          remPath->count = path->count + 1;
          remPath->bw = bw;

          // Start with path type = link type. PATH and LINK types are supposed to match.
          // Don't consider LINK_NET as we only care about the NIC->GPU path.
          int type = link->type == LINK_NET ? LINK_LOC : link->type;
          // Differentiate between one and multiple PCI switches
          if (node->type == PCI && remNode->type == PCI) type = PATH_PXB;
          // Consider a path going through the CPU as PATH_PHB
          if (link->type == LINK_PCI && (node->type == CPU || link->remNode->type == CPU)) type = PATH_PHB;
          // Set 1 hop NVLink as NVB
          if (node->type == GPU && path->type == PATH_NVL && type == PATH_NVL && remPath->count > 1) type = PATH_NVB;

          remPath->type = std::max(path->type, type);

          // Add to the list for the next iteration if not already in the list
          int i;
          for (i=0; i<nextNodeList.count; i++) if (nextNodeList.list[i] == remNode) break;
          if (i == nextNodeList.count) nextNodeList.list[nextNodeList.count++] = remNode;
        }
      }
    }
    memcpy(&nodeList, &nextNodeList, sizeof(nodeList));
  }
  return flagcxSuccess;
}

static void printNodePaths(struct flagcxTopoSystem* system, struct flagcxTopoNode* node) {
  const int linesize = 1024;
  char line[linesize];
#ifdef ENABLE_TRACE
  INFO(FLAGCX_GRAPH, "Paths from %s/%lX :", topoNodeTypeStr[node->type], node->id);
#else
  snprintf(line, linesize, "%s/%lX :", topoNodeTypeStr[node->type], node->id);
  int offset = strlen(line);
#endif
  for (int t=0; t<FLAGCX_TOPO_NODE_TYPES; t++) {
    if (node->paths[t] == NULL) continue;
    for (int n = 0; n<system->nodes[t].count; n++) {
#ifdef ENABLE_TRACE
      line[0] = 0;
      int offset = 0;
      for (int i=0; i<node->paths[t][n].count; i++) {
        struct flagcxTopoLink* link = node->paths[t][n].list[i];
        struct flagcxTopoNode* remNode = link->remNode;
        snprintf(line+offset, linesize-offset, "--%s(%g)->%s/%lx-%lx", topoLinkTypeStr[link->type], link->bw, topoNodeTypeStr[remNode->type], FLAGCX_TOPO_ID_SYSTEM_ID(remNode->id), FLAGCX_TOPO_ID_LOCAL_ID(remNode->id));
        offset = strlen(line);
      }
      INFO(FLAGCX_GRAPH, "%s (%f)", line, node->paths[t][n].bw);
#else
      snprintf(line+offset, linesize-offset, "%s/%lx-%lx (%d/%.1f/%s) ", topoNodeTypeStr[t], FLAGCX_TOPO_ID_SYSTEM_ID(system->nodes[t].nodes[n].id), FLAGCX_TOPO_ID_LOCAL_ID(system->nodes[t].nodes[n].id), node->paths[t][n].count, node->paths[t][n].bw, topoPathTypeStr[node->paths[t][n].type]);
      offset = strlen(line);
#endif
    }
  }
#ifndef ENABLE_TRACE
  INFO(FLAGCX_GRAPH, "%s", line);
#endif
}

flagcxResult_t flagcxTopoPrintPaths(struct flagcxTopoSystem* system) {
  for (int i=0; i<system->nodes[GPU].count; i++) {
    printNodePaths(system, system->nodes[GPU].nodes+i);
  }
  for (int i=0; i<system->nodes[NET].count; i++) {
    printNodePaths(system, system->nodes[NET].nodes+i);
  }
  return flagcxSuccess;
}

// Remove/free paths for a given type
static void flagcxTopoRemovePathType(struct flagcxTopoSystem* system, int nodeType) {
  for (int t=0; t<FLAGCX_TOPO_NODE_TYPES; t++) {
    // Remove links _to_ the given type
    for (int n=0; n<system->nodes[t].count; n++) {
      struct flagcxTopoNode* node = system->nodes[t].nodes+n;
      free(node->paths[nodeType]);
      node->paths[nodeType] = NULL;
    }
    // Remove links _from_ the given type
    for (int n=0; n<system->nodes[nodeType].count; n++) {
      struct flagcxTopoNode* node = system->nodes[nodeType].nodes+n;
      free(node->paths[t]);
      node->paths[t] = NULL;
    }
  }
}

// This is a tailored version of the original one.
flagcxResult_t flagcxTopoComputePaths(struct flagcxTopoSystem* system, struct flagcxHeteroComm* comm) {
  // Precompute paths between GPUs/NICs.

  // Remove everything in case we're re-computing
  for (int t=0; t<FLAGCX_TOPO_NODE_TYPES; t++) flagcxTopoRemovePathType(system, t);

  // Set direct paths to CPUs. We need them in many cases.
  for (int c=0; c<system->nodes[CPU].count; c++) {
    FLAGCXCHECK(flagcxTopoSetPaths(system->nodes[CPU].nodes+c, system));
  }

  // Set direct paths to GPUs.
  for (int g=0; g<system->nodes[GPU].count; g++) {
    FLAGCXCHECK(flagcxTopoSetPaths(system->nodes[GPU].nodes+g, system));
  }

  // Set direct paths to NICs.
  for (int n=0; n<system->nodes[NET].count; n++) {
    FLAGCXCHECK(flagcxTopoSetPaths(system->nodes[NET].nodes+n, system));
  }

  // Set direct paths to NVSwitches.
  for (int n=0; n<system->nodes[NVS].count; n++) {
    FLAGCXCHECK(flagcxTopoSetPaths(system->nodes[NVS].nodes+n, system));
  }

  // TODO: Update path for GPUs when we don't want to / can't use GPU Direct P2P

  // TODO: Update paths for NICs (no GPU Direct, PXN, ...)
  return flagcxSuccess;
}
