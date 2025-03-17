/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "topo.h"
#include "bootstrap.h"
#include "comm.h"
#include "core.h"
#include "graph.h"
#include "net.h"
#include "transport.h"
#include "xml.h"
#include <fcntl.h>

#define BUSID_SIZE (sizeof("0000:00:00.0"))
#define BUSID_REDUCED_SIZE (sizeof("0000:00"))

const char *topoNodeTypeStr[] = {"APU", "PCI", "CCI", "CPU",
                                 "NIC", "NET", "HBD"};
const char *topoLinkTypeStr[] = {"LOC", "CCI", "",    "PCI", "",
                                 "",    "",    "SYS", "NET"};
const char *topoPathTypeStr[] = {"LOC", "CCI", "CCB", "PIX", "PXB",
                                 "PXN", "PHB", "SYS", "NET", "DIS"};

flagcxResult_t flagcxTopoGetLocal(struct flagcxTopoServer *system, int type,
                                  int index, int resultType, int **locals,
                                  int *localCount, int *pathType) {
  int minType = PATH_DIS;
  float maxBw = 0;
  int count = 0;
  FLAGCXCHECK(flagcxCalloc(locals, system->nodes[resultType].count));
  struct flagcxTopoPath *paths =
      system->nodes[type].nodes[index].paths[resultType];

  for (int i = 0; i < system->nodes[resultType].count; i++) {
    if (paths[i].bw > maxBw ||
        (paths[i].bw == maxBw && paths[i].type < minType)) {
      maxBw = paths[i].bw;
      minType = paths[i].type;
      if (pathType)
        *pathType = minType;
      count = 0;
    }
    if (paths[i].bw == maxBw && paths[i].type == minType)
      (*locals)[count++] = i;
  }
  *localCount = count;
  return flagcxSuccess;
}

// flagcxResult_t getLocalNetCountByBw(struct flagcxTopoServer* system, int gpu,
// int *count) {
//   int localNetCount = 0, netCountByBw = 0;
//   int* localNets;
//   float totalNetBw = 0, gpuBw = 0;

//   for (int l=0; l<system->nodes[GPU].nodes[gpu].nlinks; l++) {
//     //assuming BW to CPU reflects the GPU bandwidth via P2P or C2C
//     //caveat, this could be wrong if there is a PCIe switch,
//     //and a narrower link to the CPU
//     if (system->nodes[GPU].nodes[gpu].links[l].remNode->type == CPU) {
//        gpuBw = system->nodes[GPU].nodes[gpu].links[l].bw;
//     }
//   }

//   FLAGCXCHECK(flagcxTopoGetLocal(system, GPU, gpu, NET, &localNets,
//   &localNetCount, NULL)); for (int l=0; (l < localNetCount) && (totalNetBw <
//   gpuBw); l++, netCountByBw++) {
//      totalNetBw += system->nodes[GPU].nodes[gpu].paths[NET][localNets[l]].bw;
//   }
//   *count = netCountByBw;

//   free(localNets);
//   return flagcxSuccess;
// }

// a temprarory function to get the local net from topo xml file.
// devId: the device id of the GPU
// netName: the name of the net
// strlen: the length of the netName
static flagcxResult_t flagcxGetLocalNetFromXmlFile(int devId, char *netName,
                                                   int strlen) {
  flagcxResult_t ret = flagcxSuccess;
  flagcxXmlNode *node = NULL;
  int dev = -1;
  // step 1: parse the xml file and load it into flagcxXml struct
  struct flagcxXml *xml;
  const char *xmlTopoFile = flagcxGetEnv("FLAGCX_TOPO_FILE");
  if (!xmlTopoFile) {
    INFO(FLAGCX_ENV, "FLAGCX_TOPO_FILE environment variable not set");
    return ret;
  }
  FLAGCXCHECK(xmlAlloc(&xml, FLAGCX_TOPO_XML_MAX_NODES));
  INFO(FLAGCX_ENV, "FLAGCX_TOPO_FILE set by environment to %s", xmlTopoFile);
  FLAGCXCHECKGOTO(flagcxTopoGetXmlFromFile(xmlTopoFile, xml, 1), ret, fail);

  // step 2: scan flagcxXml struct to find the netName for the given devId
  FLAGCXCHECKGOTO(xmlFindTag(xml, "gpu", &node), ret, fail);
  while (node != NULL) {
    // find the gpu node with the right dev
    FLAGCXCHECKGOTO(xmlGetAttrInt(node, "dev", &dev), ret, fail);
    if (dev == devId) {
      const char *str;
      FLAGCXCHECKGOTO(xmlGetAttr(node, "net", &str), ret, fail);
      if (str != NULL) {
        INFO(FLAGCX_GRAPH, "GPU %d use net %s specified in topo file %s", dev,
             str, xmlTopoFile);
        strncpy(netName, str, strlen - 1);
        netName[strlen - 1] = '\0';
        break;
      } else {
        WARN("GPU %d net attribute is not specified in topo file %s", dev,
             xmlTopoFile);
        ret = flagcxInternalError;
        goto fail;
      }
    }
    flagcxXmlNode *next = NULL;
    FLAGCXCHECKGOTO(xmlFindNextTag(xml, "gpu", node, &next), ret, fail);
    node = next;
  }
  if (dev != devId) {
    // device not found
    WARN("GPU %d not found in topo file %s", devId, xmlTopoFile);
    ret = flagcxInternalError;
    goto fail;
  }
exit:
  free(xml);
  return ret;
fail:
  goto exit;
}

#define FLAGCX_MAX_NET_NAME 128

flagcxResult_t flagcxGetLocalNetFromXml(struct flagcxXml *xml, int apu,
                                        char *name, int strlen) {
  struct flagcxXmlNode *apuNode = NULL;
  FLAGCXCHECK(xmlGetApuByIndex(xml, apu, &apuNode));
  if (apuNode == NULL) {
    WARN("invalid apu index %d", apu);
    return flagcxInternalError;
  }
  struct flagcxXmlNode *netNode = NULL;
  // first try to find the closest net under one CPU node
  FLAGCXCHECK(xmlFindClosestNetUnderCpu(xml, apuNode, &netNode));
  if (netNode == NULL) {
    // if there is no net node that share the same CPU ancestor node with the
    // APU try to find a net node from the server scope
    FLAGCXCHECK(xmlFindClosestNetUnderServer(xml, apuNode, &netNode));
  }
  if (netNode != NULL) {
    // found a net node
    const char *str;
    FLAGCXCHECK(xmlGetAttrStr(netNode, "name", &str)); // get net name
    strncpy(name, str, strlen);
    INFO(FLAGCX_INIT, "local net for apu %d is %s", apu, name);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxGetLocalNetFromGpu(int apu, int *dev,
                                        struct flagcxHeteroComm *comm) {
  char name[FLAGCX_MAX_NET_NAME + 1] = {0};
  // first try getting local net from existing xml file
  FLAGCXCHECK(flagcxGetLocalNetFromXmlFile(apu, name, FLAGCX_MAX_NET_NAME + 1));
  if (strlen(name) == 0) {
    // try building xml by topo detect and find closest nic based on xml topo
    struct flagcxXml *xml;
    FLAGCXCHECK(xmlAlloc(&xml, FLAGCX_TOPO_XML_MAX_NODES));
    FLAGCXCHECK(flagcxTopoGetXmlTopo(comm, xml));
    FLAGCXCHECK(
        flagcxGetLocalNetFromXml(xml, apu, name, FLAGCX_MAX_NET_NAME + 1));
    free(xml);
  }
  if (strlen(name) == 0) {
    INFO(FLAGCX_GRAPH, "did not find local net for apu %d in xml topo", apu);
    const char *useNet = flagcxGetEnv("FLAGCX_USENET");
    if (useNet != NULL) {
      INFO(FLAGCX_GRAPH,
           "APU %d use net %s specified in FLAGCX_USENET environment variable.",
           apu, useNet);
      strncpy(name, useNet, FLAGCX_MAX_NET_NAME);
    }
  }
  flagcxNetIb.getDevFromName(name, dev);

  return flagcxSuccess;
}

/****************************/
/* External query functions */
/****************************/

// flagcxResult_t flagcxTopoCpuType(struct flagcxTopoServer* system, int* arch,
// int* vendor, int* model) {
//   *arch = system->nodes[CPU].nodes[0].cpu.arch;
//   *vendor = system->nodes[CPU].nodes[0].cpu.vendor;
//   *model = system->nodes[CPU].nodes[0].cpu.model;
//   return flagcxSuccess;
// }

// FLAGCX_PARAM(IgnoreCpuAffinity, "IGNORE_CPU_AFFINITY", 0);

// flagcxResult_t flagcxTopoGetGpuCount(struct flagcxTopoServer* system, int*
// count) {
//   *count = system->nodes[GPU].count;
//   return flagcxSuccess;
// }

// flagcxResult_t flagcxTopoGetNetCount(struct flagcxTopoServer* system, int*
// count) {
//   *count = system->nodes[NET].count;
//   return flagcxSuccess;
// }

// flagcxResult_t flagcxTopoGetNvsCount(struct flagcxTopoServer* system, int*
// count) {
//   *count = system->nodes[NVS].count;
//   return flagcxSuccess;
// }

// flagcxResult_t flagcxTopoGetCompCap(struct flagcxTopoServer* system, int*
// ccMin, int* ccMax) {
//   if (system->nodes[GPU].count == 0) return flagcxInternalError;
//   int min, max;
//   min = max = system->nodes[GPU].nodes[0].apu.cudaCompCap;
//   for (int g=1; g<system->nodes[GPU].count; g++) {
//     min = std::min(min, system->nodes[GPU].nodes[g].gpu.cudaCompCap);
//     max = std::max(max, system->nodes[GPU].nodes[g].gpu.cudaCompCap);
//   }
//   if (ccMin) *ccMin = min;
//   if (ccMax) *ccMax = max;
//   return flagcxSuccess;
// }

// static flagcxResult_t flagcxTopoPrintRec(struct flagcxTopoNode* node, struct
// flagcxTopoNode* prevNode, char* line, int offset) {
//   if (node->type == GPU) {
//     sprintf(line+offset, "%s/%lx-%lx (%d)", topoNodeTypeStr[node->type],
//     FLAGCX_TOPO_ID_SERVER_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id),
//     node->apu.rank);
//   } else if (node->type == CPU) {
//     sprintf(line+offset, "%s/%lx-%lx (%d/%d/%d)",
//     topoNodeTypeStr[node->type], FLAGCX_TOPO_ID_SERVER_ID(node->id),
//     FLAGCX_TOPO_ID_LOCAL_ID(node->id), node->cpu.arch, node->cpu.vendor,
//     node->cpu.model);
//   } else if (node->type == PCI) {
//     sprintf(line+offset, "%s/%lx-%lx (%lx)", topoNodeTypeStr[node->type],
//     FLAGCX_TOPO_ID_SERVER_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id),
//     node->pci.device);
//   } else {
//     sprintf(line+offset, "%s/%lx-%lx", topoNodeTypeStr[node->type],
//     FLAGCX_TOPO_ID_SERVER_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id));
//   }
//   INFO(FLAGCX_GRAPH, "%s", line);
//   for (int i=0; i<offset; i++) line[i] = ' ';

//   for (int l=0; l<node->nlinks; l++) {
//     struct flagcxTopoLink* link = node->links+l;
//     if (link->type == LINK_LOC) continue;
//     if (link->type != LINK_PCI || link->remNode != prevNode) {
//       sprintf(line+offset, "+ %s[%2.1f] - ", topoLinkTypeStr[link->type],
//       link->bw); int nextOffset = strlen(line); if (link->type == LINK_PCI) {
//         FLAGCXCHECK(flagcxTopoPrintRec(link->remNode, node, line,
//         nextOffset));
//       } else {
//         if (link->remNode->type == NET) {
//           sprintf(line+nextOffset, "%s/%lX (%lx/%d/%f)",
//           topoNodeTypeStr[link->remNode->type], link->remNode->id,
//           link->remNode->net.asic, link->remNode->net.port,
//           link->remNode->net.bw);
//         } else {
//           sprintf(line+nextOffset, "%s/%lX",
//           topoNodeTypeStr[link->remNode->type], link->remNode->id);
//         }
//         INFO(FLAGCX_GRAPH, "%s", line);
//       }
//     }
//   }
//   return flagcxSuccess;
// }

// flagcxResult_t flagcxTopoPrint(struct flagcxTopoServer* s) {
//   INFO(FLAGCX_GRAPH, "=== System : maxBw %2.1f totalBw %2.1f ===", s->maxBw,
//   s->totalBw); char line[1024]; for (int n=0; n<s->nodes[CPU].count; n++)
//   FLAGCXCHECK(flagcxTopoPrintRec(s->nodes[CPU].nodes+n, NULL, line, 0));
//   INFO(FLAGCX_GRAPH, "==========================================");
//   FLAGCXCHECK(flagcxTopoPrintPaths(s));
//   return flagcxSuccess;
// }

// will remove this function when we finish the function that builds server topo
flagcxResult_t flagcxTopoGetXmlTopo(struct flagcxHeteroComm *comm,
                                    struct flagcxXml *xml) {
  // create root node if we didn't get topo from xml file
  if (xml->maxIndex == 0) {
    INFO(FLAGCX_INIT, "creating root XML node");
    // Create top tag
    struct flagcxXmlNode *top;
    // TODO: change root node name from "system" to "root"
    FLAGCXCHECK(xmlAddNode(xml, NULL, "system", &top));
    FLAGCXCHECK(xmlSetAttrInt(top, "version", FLAGCX_TOPO_XML_VERSION));
  }

  INFO(FLAGCX_INIT, "start detecting APUs");
  for (int r = 0; r < comm->nRanks; r++) {
    if (comm->peerInfo[r].hostHash == comm->peerInfo[comm->rank].hostHash) {
      INFO(FLAGCX_INIT, "preparing to detect APU for rank %d", r);
      char busId[FLAGCX_DEVICE_PCI_BUSID_BUFFER_SIZE];
      INFO(FLAGCX_INIT, "converting busId to string");
      FLAGCXCHECK(int64ToBusId(comm->peerInfo[r].busId, busId));
      struct flagcxXmlNode *node;
      FLAGCXCHECK(flagcxTopoFillApu(xml, busId, &node));
      if (node == NULL) {
        continue;
      }
      int devLogicalIdx = 0;
      deviceAdaptor->getDeviceByPciBusId(&devLogicalIdx, busId);
      FLAGCXCHECK(xmlSetAttrInt(node, "dev", devLogicalIdx));
    }
  }

  int netDevCount = 0;
  FLAGCXCHECK(flagcxNetIb.devices(&netDevCount));
  for (int n = 0; n < netDevCount; n++) {
    flagcxNetProperties_t props;
    FLAGCXCHECK(flagcxNetIb.getProperties(n, &props));
    struct flagcxXmlNode *netNode;
    FLAGCXCHECK(flagcxTopoFillNet(xml, props.pciPath, props.name, &netNode));
  }

  if (comm->rank == 0) {
    const char *xmlTopoFile = flagcxGetEnv("FLAGCX_TOPO_DUMP_FILE");
    INFO(FLAGCX_ENV, "FLAGCX_TOPO_DUMP_FILE is %s", xmlTopoFile);
    if (xmlTopoFile && comm->rank == 0) {
      INFO(FLAGCX_INIT, "start dumping topo to xml file");
      FLAGCXCHECK(flagcxTopoDumpXmlToFile(xmlTopoFile, xml));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoGetServerTopo(struct flagcxHeteroComm *comm,
                                       struct flagcxTopoServer **serverTopo) {
  // TODO: first try to acquire topo from xml file
  struct flagcxXml *xml;
  INFO(FLAGCX_INIT, "allocing flagcxXml");
  FLAGCXCHECK(xmlAlloc(&xml, FLAGCX_TOPO_XML_MAX_NODES));

  // create root node if we didn't get topo from xml file
  if (xml->maxIndex == 0) {
    INFO(FLAGCX_INIT, "creating root XML node");
    // Create top tag
    struct flagcxXmlNode *top;
    // TODO: change root node name from "system" to "root"
    FLAGCXCHECK(xmlAddNode(xml, NULL, "system", &top));
    FLAGCXCHECK(xmlSetAttrInt(top, "version", FLAGCX_TOPO_XML_VERSION));
  }

  INFO(FLAGCX_INIT, "start detecting APUs");
  for (int r = 0; r < comm->nRanks; r++) {
    if (comm->peerInfo[r].hostHash == comm->peerInfo[comm->rank].hostHash) {
      INFO(FLAGCX_INIT, "preparing to detect APU for rank %d", r);
      char busId[FLAGCX_DEVICE_PCI_BUSID_BUFFER_SIZE];
      INFO(FLAGCX_INIT, "converting busId to string");
      FLAGCXCHECK(int64ToBusId(comm->peerInfo[r].busId, busId));
      struct flagcxXmlNode *node;
      FLAGCXCHECK(flagcxTopoFillApu(xml, busId, &node));
      if (node == NULL) {
        continue;
      }
      int devLogicalIdx = 0;
      deviceAdaptor->getDeviceByPciBusId(&devLogicalIdx, busId);
      FLAGCXCHECK(xmlSetAttrInt(node, "dev", devLogicalIdx));
    }
  }

  int netDevCount = 0;
  INFO(FLAGCX_INIT, "getting the number of net devices");
  FLAGCXCHECK(flagcxNetIb.devices(&netDevCount));
  for (int n = 0; n < netDevCount; n++) {
    flagcxNetProperties_t props;
    INFO(FLAGCX_INIT, "getting properties of net device %d", n);
    FLAGCXCHECK(flagcxNetIb.getProperties(n, &props));
    struct flagcxXmlNode *netNode;
    FLAGCXCHECK(flagcxTopoFillNet(xml, props.pciPath, props.name, &netNode));
  }

  if (comm->rank == 0) {
    const char *xmlTopoFile = flagcxGetEnv("FLAGCX_TOPO_DUMP_FILE");
    INFO(FLAGCX_INIT, "FLAGCX_TOPO_DUMP_FILE is %s", xmlTopoFile);
    if (xmlTopoFile && comm->rank == 0) {
      INFO(FLAGCX_ENV, "start dumping topo to xml file");
      FLAGCXCHECK(flagcxTopoDumpXmlToFile(xmlTopoFile, xml));
    }
  }
  // FLAGCXCHECK(flagcxTopoGetServerTopoFromXml(
  //     xml, serverTopo, comm->peerInfo[comm->rank].hostHash));

  free(xml);
  return flagcxSuccess;
}
