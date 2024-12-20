/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "core.h"
#include "graph.h"
#include "topo.h"
#include "comm.h"
#include "net.h"
#include <sys/stat.h>
#include <fcntl.h>
#include "bootstrap.h"

#define BUSID_SIZE (sizeof("0000:00:00.0"))
#define BUSID_REDUCED_SIZE (sizeof("0000:00"))

const char* topoNodeTypeStr[] = { "GPU", "PCI", "NVS", "CPU", "NIC", "NET" };
const char* topoLinkTypeStr[] = { "LOC", "NVL", "",    "PCI",    "",    "",    "", "SYS", "NET" };
const char* topoPathTypeStr[] = { "LOC", "NVL", "NVB", "PIX", "PXB", "PXN", "PHB", "SYS", "NET", "DIS" };

flagcxResult_t flagcxTopoGetLocal(struct flagcxTopoSystem* system, int type, int index, int resultType, int** locals, int* localCount, int* pathType) {
  int minType = PATH_DIS;
  float maxBw = 0;
  int count = 0;
  FLAGCXCHECK(flagcxCalloc(locals, system->nodes[resultType].count));
  struct flagcxTopoLinkList* paths = system->nodes[type].nodes[index].paths[resultType];

  for (int i=0; i<system->nodes[resultType].count; i++) {
    if (paths[i].bw > maxBw || (paths[i].bw == maxBw && paths[i].type < minType)) {
      maxBw = paths[i].bw;
      minType = paths[i].type;
      if (pathType) *pathType = minType;
      count = 0;
    }
    if (paths[i].bw == maxBw && paths[i].type == minType) (*locals)[count++] = i;
  }
  *localCount = count;
  return flagcxSuccess;
}

flagcxResult_t getLocalNetCountByBw(struct flagcxTopoSystem* system, int gpu, int *count) {
  int localNetCount = 0, netCountByBw = 0;
  int* localNets;
  float totalNetBw = 0, gpuBw = 0;

  for (int l=0; l<system->nodes[GPU].nodes[gpu].nlinks; l++) {
    //assuming BW to CPU reflects the GPU bandwidth via P2P or C2C
    //caveat, this could be wrong if there is a PCIe switch,
    //and a narrower link to the CPU
    if (system->nodes[GPU].nodes[gpu].links[l].remNode->type == CPU) {
       gpuBw = system->nodes[GPU].nodes[gpu].links[l].bw;
    }
  }

  FLAGCXCHECK(flagcxTopoGetLocal(system, GPU, gpu, NET, &localNets, &localNetCount, NULL));
  for (int l=0; (l < localNetCount) && (totalNetBw < gpuBw); l++, netCountByBw++) {
     totalNetBw += system->nodes[GPU].nodes[gpu].paths[NET][localNets[l]].bw;
  }
  *count = netCountByBw;

  free(localNets);
  return flagcxSuccess;
}

flagcxResult_t flagcxGetLocalNetFromGpu(int gpu, int *dev) {
  char name[130];
  const char* useNet = flagcxGetEnv("FLAGCX_USENET");
  
  if(useNet == NULL)
    flagcxTopoGetLocalNet(gpu, name);
  else
    strcpy(name, useNet);

  flagcxNetIb.getDevFromName(name, dev);
  return flagcxSuccess;
}

/****************************/
/* External query functions */
/****************************/

flagcxResult_t flagcxTopoCpuType(struct flagcxTopoSystem* system, int* arch, int* vendor, int* model) {
  *arch = system->nodes[CPU].nodes[0].cpu.arch;
  *vendor = system->nodes[CPU].nodes[0].cpu.vendor;
  *model = system->nodes[CPU].nodes[0].cpu.model;
  return flagcxSuccess;
}

FLAGCX_PARAM(IgnoreCpuAffinity, "IGNORE_CPU_AFFINITY", 0);


flagcxResult_t flagcxTopoGetGpuCount(struct flagcxTopoSystem* system, int* count) {
  *count = system->nodes[GPU].count;
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoGetNetCount(struct flagcxTopoSystem* system, int* count) {
  *count = system->nodes[NET].count;
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoGetNvsCount(struct flagcxTopoSystem* system, int* count) {
  *count = system->nodes[NVS].count;
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoGetCompCap(struct flagcxTopoSystem* system, int* ccMin, int* ccMax) {
  if (system->nodes[GPU].count == 0) return flagcxInternalError;
  int min, max;
  min = max = system->nodes[GPU].nodes[0].gpu.cudaCompCap;
  for (int g=1; g<system->nodes[GPU].count; g++) {
    min = std::min(min, system->nodes[GPU].nodes[g].gpu.cudaCompCap);
    max = std::max(max, system->nodes[GPU].nodes[g].gpu.cudaCompCap);
  }
  if (ccMin) *ccMin = min;
  if (ccMax) *ccMax = max;
  return flagcxSuccess;
}

static flagcxResult_t flagcxTopoPrintRec(struct flagcxTopoNode* node, struct flagcxTopoNode* prevNode, char* line, int offset) {
  if (node->type == GPU) {
    sprintf(line+offset, "%s/%lx-%lx (%d)", topoNodeTypeStr[node->type], FLAGCX_TOPO_ID_SYSTEM_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id), node->gpu.rank);
  } else if (node->type == CPU) {
    sprintf(line+offset, "%s/%lx-%lx (%d/%d/%d)", topoNodeTypeStr[node->type], FLAGCX_TOPO_ID_SYSTEM_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id), node->cpu.arch, node->cpu.vendor, node->cpu.model);
  } else if (node->type == PCI) {
    sprintf(line+offset, "%s/%lx-%lx (%lx)", topoNodeTypeStr[node->type], FLAGCX_TOPO_ID_SYSTEM_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id), node->pci.device);
  } else {
    sprintf(line+offset, "%s/%lx-%lx", topoNodeTypeStr[node->type], FLAGCX_TOPO_ID_SYSTEM_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id));
  }
  INFO(FLAGCX_GRAPH, "%s", line);
  for (int i=0; i<offset; i++) line[i] = ' ';

  for (int l=0; l<node->nlinks; l++) {
    struct flagcxTopoLink* link = node->links+l;
    if (link->type == LINK_LOC) continue;
    if (link->type != LINK_PCI || link->remNode != prevNode) {
      sprintf(line+offset, "+ %s[%2.1f] - ", topoLinkTypeStr[link->type], link->bw);
      int nextOffset = strlen(line);
      if (link->type == LINK_PCI) {
        FLAGCXCHECK(flagcxTopoPrintRec(link->remNode, node, line, nextOffset));
      } else {
        if (link->remNode->type == NET) {
          sprintf(line+nextOffset, "%s/%lX (%lx/%d/%f)", topoNodeTypeStr[link->remNode->type], link->remNode->id, link->remNode->net.asic, link->remNode->net.port, link->remNode->net.bw);
        } else {
          sprintf(line+nextOffset, "%s/%lX", topoNodeTypeStr[link->remNode->type], link->remNode->id);
        }
        INFO(FLAGCX_GRAPH, "%s", line);
      }
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoPrint(struct flagcxTopoSystem* s) {
  INFO(FLAGCX_GRAPH, "=== System : maxBw %2.1f totalBw %2.1f ===", s->maxBw, s->totalBw);
  char line[1024];
  for (int n=0; n<s->nodes[CPU].count; n++) FLAGCXCHECK(flagcxTopoPrintRec(s->nodes[CPU].nodes+n, NULL, line, 0));
  INFO(FLAGCX_GRAPH, "==========================================");
  FLAGCXCHECK(flagcxTopoPrintPaths(s));
  return flagcxSuccess;
}
