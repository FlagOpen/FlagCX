/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef FLAGCX_TOPO_H_
#define FLAGCX_TOPO_H_

#include "graph.h"
#include "core.h"

#define LOC_BW 5000.0
#define SM60_NVLINK_BW 18.0
#define SM70_NVLINK_BW 20.0
#define SM80_NVLINK_BW 20.0
#define SM90_NVLINK_BW 20.6
#define SM86_NVLINK_BW 12.0
#define PCI_BW 12.0           // PCI Gen3 x16
#define QPI_BW 6.0
#define AMD_BW 16.0
#define SKL_QPI_BW 10.0
#define ZPI_BW 6.0
#define YONGFENG_ZPI_BW 9.0
#define P9_BW 32.0
#define ARM_BW 6.0
#define NET_BW 12.0           // 100Gbit

// Intel CPU convert GPU P2P traffic into 64B PCI TLPs, so GPU
// to GPU traffic consumes more PCI bandwidth.
#define INTEL_P2P_OVERHEAD(bw) (bw*6/5)

#define FLAGCX_TOPO_NODE_TYPES 7
#define GPU 0
#define PCI 1
#define NVS 2
#define CPU 3 // Actually NUMA domains
#define NIC 4
#define NET 5
extern const char* topoNodeTypeStr[];

// We want link types and path types to match as much as possible
#define LINK_LOC 0
#define LINK_NVL 1
// Skipping 2 for PATH_NVB
#define LINK_PCI 3
// Skipping 4 for PATH_PXB
// Skipping 5 for PATH_PXN
// Skipping 6 for PATH_PHB
#define LINK_SYS 7
#define LINK_NET 8
extern const char* topoLinkTypeStr[];

// Local (myself)
#define PATH_LOC 0

// Connection traversing NVLink
#define PATH_NVL 1

// Connection through NVLink using an intermediate GPU
#define PATH_NVB 2

// Connection traversing at most a single PCIe bridge
#define PATH_PIX 3

// Connection traversing multiple PCIe bridges (without traversing the PCIe Host Bridge)
#define PATH_PXB 4

// Connection between a GPU and a NIC using an intermediate GPU. Used to enable rail-local, aggregated network send/recv operations.
#define PATH_PXN 5

// Connection traversing PCIe as well as a PCIe Host Bridge (typically the CPU)
#define PATH_PHB 6

// Connection traversing PCIe as well as the SMP interconnect between NUMA nodes (e.g., QPI/UPI)
#define PATH_SYS 7

// Connection through the network
#define PATH_NET 8

// Disconnected
#define PATH_DIS 9
extern const char* topoPathTypeStr[];

struct flagcxTopoNode;
struct flagcxTopoLink {
  int type;
  float bw;
  struct flagcxTopoNode* remNode;
};
#define FLAGCX_TOPO_MAX_LINKS 128
#define FLAGCX_TOPO_MAX_HOPS (FLAGCX_TOPO_MAX_NODES*FLAGCX_TOPO_NODE_TYPES)

struct flagcxTopoLinkList {
  struct flagcxTopoLink* list[FLAGCX_TOPO_MAX_HOPS];
  int count;
  float bw;
  int type;
};

#define FLAGCX_TOPO_CPU_INTEL_BDW 1
#define FLAGCX_TOPO_CPU_INTEL_SKL 2

#define FLAGCX_TOPO_UNDEF (-1)

#define FLAGCX_TOPO_ID_SYSTEM_ID(id) (id >> 56)
#define FLAGCX_TOPO_ID_LOCAL_ID(id) (id & 0x00ffffffffffffff)
#define FLAGCX_TOPO_ID(systemid, localid) (((int64_t)systemid << 56) + localid)

struct flagcxTopoNode {
  int type;
  int64_t id;
  // Type specific data
  union {
    struct {
      int dev; // NVML dev number
      int rank;
      int cudaCompCap;
      int gdrSupport;
    }gpu;
    struct {
      int dev; // Plugin dev number
      uint64_t asic;
      int port;
      float bw;
      float latency;
      int gdrSupport;
      int collSupport;
      int maxChannels;
    }net;
    struct {
      int arch;
      int vendor;
      int model;
      cpu_set_t affinity;
    }cpu;
    struct {
      uint64_t device;
    }pci;
  };
  int nlinks;
  struct flagcxTopoLink links[FLAGCX_TOPO_MAX_LINKS];
  // Pre-computed paths to GPUs and NICs
  struct flagcxTopoLinkList* paths[FLAGCX_TOPO_NODE_TYPES];
  // Used during search
  uint64_t used;
};

struct flagcxTopoNodeSet {
  int count;
  struct flagcxTopoNode nodes[FLAGCX_TOPO_MAX_NODES];
};

struct flagcxTopoSystem {
  int systemId;
  uint64_t hostHashes[FLAGCX_TOPO_MAX_NODES];
  int nHosts;
  struct flagcxTopoNodeSet nodes[FLAGCX_TOPO_NODE_TYPES];
  float maxBw;
  float totalBw;
};

struct topoArgs{
  int rank; 
  int nranks; 
  flagcxUniqueId uniqueId; 
  void *bootstrap;
};

flagcxResult_t flagcxTopoGetNode(struct flagcxTopoSystem* system, struct flagcxTopoNode** node, int type, uint64_t id);
flagcxResult_t flagcxTopoCreateNode(struct flagcxTopoSystem* system, struct flagcxTopoNode** node, int type, uint64_t id);
flagcxResult_t flagcxTopoRemoveNode(struct flagcxTopoSystem* system, int type, int id);
flagcxResult_t flagcxTopoConnectNodes(struct flagcxTopoNode* node, struct flagcxTopoNode* remNode, int type, float bw);
flagcxResult_t flagcxTopoPrintPaths(struct flagcxTopoSystem* system);
flagcxResult_t flagcxTopoLoadSystem(const char* xmlTopoFile, struct flagcxTopoSystem* system);
flagcxResult_t flagcxTopoGetIntermediateRank(struct flagcxTopoSystem* system, int rank, int64_t netId, int* intermediateRank);

#define FLAGCX_TOPO_XML_MAX_NODES 256
#define FLAGCX_GRAPH_XML_MAX_NODES 4096
flagcxResult_t flagcxTopoGetSystemFromXml(struct flagcxXml* xml, struct flagcxTopoSystem** topoSystem, uint64_t localHostHash);
flagcxResult_t flagcxTopoGetGraphFromXml(struct flagcxXmlNode *xmlGraphs, struct flagcxTopoSystem* system, struct flagcxTopoGraph* graph, int* nChannels);
flagcxResult_t flagcxTopoGetXmlFromGraphs(int ngraphs, struct flagcxTopoGraph** graphs, struct flagcxTopoSystem* system, struct flagcxXml *xml);

flagcxResult_t flagcxTopoGetCompCap(struct flagcxTopoSystem* system, int* ccMin, int* ccMax);

static flagcxResult_t flagcxTopoIdToIndex(struct flagcxTopoSystem* system, int type, int64_t id, int* index) {
  *index = -1;
  for (int i=0; i<system->nodes[type].count; i++) {
    if (system->nodes[type].nodes[i].id == id) {
      *index = i;
      return flagcxSuccess;
    }
  }
  return flagcxInternalError;
}

static flagcxResult_t flagcxTopoRankToIndex(struct flagcxTopoSystem* system, int rank, int* index) {
  *index = -1;
  for (int i=0; i<system->nodes[GPU].count; i++) {
    if (system->nodes[GPU].nodes[i].gpu.rank == rank) {
      *index = i;
      return flagcxSuccess;
    }
  }
  return flagcxInternalError;
}

static flagcxResult_t flagcxTopoDevToRank(struct flagcxTopoSystem* system, int dev, int* rank) {
  *rank = -1;
  for (int i=0; i<system->nodes[GPU].count; i++) {
    if (FLAGCX_TOPO_ID_SYSTEM_ID(system->nodes[GPU].nodes[i].id) != system->systemId) continue; // Only consider GPUs on our node
    if (system->nodes[GPU].nodes[i].gpu.dev == dev) {
      *rank = system->nodes[GPU].nodes[i].gpu.rank;
      return flagcxSuccess;
    }
  }
  return flagcxInternalError;
}

static flagcxResult_t flagcxTopoIdToNetDev(struct flagcxTopoSystem* system, int64_t id, int* netDev) {
  *netDev = -1;
  for (int i=0; i<system->nodes[NET].count; i++) {
    if (system->nodes[NET].nodes[i].id == id) {
      *netDev = system->nodes[NET].nodes[i].net.dev;
      return flagcxSuccess;
    }
  }
  WARN("Could not find NET with id %lx\n", id);
  return flagcxInternalError;
}

// Returns NVLink bw in GB/s
static float flagcxTopoNVLinkBw(int cudaCompCap) {
  return
    cudaCompCap >= 90 ? SM90_NVLINK_BW :
    cudaCompCap == 86 ? SM86_NVLINK_BW :
    cudaCompCap >= 80 ? SM80_NVLINK_BW :
    cudaCompCap >= 70 ? SM70_NVLINK_BW :
    cudaCompCap >= 60 ? SM60_NVLINK_BW :
    SM80_NVLINK_BW;
}

// Mirror bits
static bool isPow2(int val) {
  return (val & (val-1)) == 0;
}
static int mirrorBits(int val, int pow2) {
  int mirror = 0;
  for (int b=1, mb=(pow2>>1); b<pow2; b<<=1, mb>>=1) if (val & b) mirror |= mb;
  return mirror;
}


#ifdef CREATE_DEVICE_TOPO_API
#define DEVICE_TOPO_API_EXTERN
#else
#define DEVICE_TOPO_API_EXTERN extern
#endif

DEVICE_TOPO_API_EXTERN flagcxResult_t (*flagcxTopoGetLocalNet)(int gpu, char *name);

#endif
