#include "cluster.h"
#include <cstring>

flagcxResult_t parseClusterSplitList(const char *input,
                                     std::vector<int> &output) {
  output.clear();
  std::stringstream ss(input);
  std::string token;

  while (std::getline(ss, token, ',')) {
    try {
      int value = std::stoi(token);
      output.push_back(value);
    } catch (const std::exception &e) {
      WARN("Invalid cluster split info, its format should be like '2,4,8,...'");
      return flagcxSystemError;
    }
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxCollectClusterInfos(const flagcxVendor *allData,
                                         flagcxCommunicatorType_t *type,
                                         int *homo_rank, int *homo_root_rank,
                                         int *homo_ranks, int *cluster_id,
                                         int *cluster_inter_rank, int *ncluster,
                                         int rank, int nranks) {
  *homo_rank = rank;
  *homo_root_rank = 0;
  *homo_ranks = 1;
  *cluster_id = 0;
  *cluster_inter_rank = -1; // deprecated, to be removed
  *ncluster = 1;
  *type = flagcxCommunicatorHomo;

  if (nranks <= 1)
    return flagcxSuccess;

  std::map<std::string, int> clusterMap;
  clusterMap[allData[0].internal] = 1;
  int numClusters = 1;
  int currCluster = 0;
  int aggRanks = 1;
  int homoRootRank = 0;
  std::string myCls = allData[rank].internal;
  for (int i = 1; i < nranks; ++i) {
    std::string cls = allData[i].internal;
    auto it = clusterMap.find(cls);
    if (it != clusterMap.end()) {
      it->second = it->second + 1;
    } else {
      clusterMap[cls] = 1;
      numClusters += 1;
      if (myCls == cls) {
        *homo_rank = *homo_rank - aggRanks;
        currCluster = numClusters - 1;
        homoRootRank = i;
      }
    }
    aggRanks += 1;

    if (i == rank) {
      *homo_root_rank = homoRootRank;
    }
  }

  *homo_ranks = clusterMap[myCls];

  if (clusterMap.size() > 1) {
    *type = flagcxCommunicatorHybrid;
  } else {
    *type = flagcxCommunicatorHomo;
  }

  if (*type == flagcxCommunicatorHybrid) {
    *cluster_id = currCluster;
    *ncluster = numClusters;
  }

  // split and obtain sub-clusters
  const char *clusterSplitInfo = flagcxGetEnv("FLAGCX_CLUSTER_SPLIT_LIST");
  if (clusterSplitInfo != NULL) {
    std::vector<int> clusterSplitList;
    FLAGCXCHECK(parseClusterSplitList(clusterSplitInfo, clusterSplitList));
    if (*ncluster != int(clusterSplitList.size())) {
      WARN("Invalid cluster split info, its length should be equal to the "
           "number of homogeneous cluster");
      return flagcxSystemError;
    }

    int subClusterId = 0;
    for (int i = 0; i < currCluster; ++i) {
      subClusterId += clusterSplitList[i];
    }
    int subHomoRanks = (*homo_ranks) / clusterSplitList[currCluster];
    int hasRes =
        (((*homo_rank) / subHomoRanks) >= clusterSplitList[currCluster]) ? 1
                                                                         : 0;
    subClusterId += (hasRes == 1) ? ((*homo_rank) / subHomoRanks) - 1
                                  : ((*homo_rank) / subHomoRanks);
    int subHomoRank = (hasRes == 1)
                          ? subHomoRanks + ((*homo_rank) % subHomoRanks)
                          : ((*homo_rank) % subHomoRanks);
    int subHomoRootRank = rank - subHomoRank;
    if (hasRes == 1 ||
        ((*homo_rank) / subHomoRanks) == clusterSplitList[currCluster] - 1) {
      subHomoRanks += (*homo_ranks) % clusterSplitList[currCluster];
    }
    int subNClusters = 0;
    for (int i = 0; i < (*ncluster); ++i) {
      subNClusters += clusterSplitList[i];
    }
    *homo_rank = subHomoRank;
    *homo_root_rank = subHomoRootRank;
    *homo_ranks = subHomoRanks;
    *cluster_id = subClusterId;
    *ncluster = subNClusters;
    *type =
        (subNClusters > 1) ? flagcxCommunicatorHybrid : flagcxCommunicatorHomo;
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxFillClusterVendorInfo(const flagcxVendor *allData,
                                           flagcxComm *comm, int *clusterIdData,
                                           int nranks, int ncluster) {
  comm->clusterVendorMap.resize(ncluster);
  for (int i = 0; i < nranks; i++) {
    std::string vendor = allData[i].internal;
    int cluster = clusterIdData[i];
    if (vendor == "NVIDIA") {
      comm->clusterVendorMap[cluster] = FLAGCX_VENDOR_NVIDIA;
    } else if (vendor == "ILUVATAR_COREX") {
      comm->clusterVendorMap[cluster] = FLAGCX_VENDOR_ILUVATAR_COREX;
    } else if (vendor == "MLU") {
      comm->clusterVendorMap[cluster] = FLAGCX_VENDOR_MLU;
    } else if (vendor == "METAX") {
      comm->clusterVendorMap[cluster] = FLAGCX_VENDOR_METAX;
    }
  }
  return flagcxSuccess;
}