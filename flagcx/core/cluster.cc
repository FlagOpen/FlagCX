#include "cluster.h"
#include <cstring>

flagcxResult_t flagcxCollectClusterInfos(const flagcxVendor *allData,
                                         flagcxCommunicatorType_t &type,
                                         int *homo_rank, int *homo_root_rank, int *homo_ranks,
                                         int *hetero_rank, int *hetero_root_rank, int *hetero_ranks,
                                         int *cluster_id, int *ncluster, int rank, int nranks)
{
    *homo_rank = rank;
    *homo_root_rank = 0;
    *homo_ranks = 1;
    *hetero_rank = -1;
    *hetero_root_rank = -1;
    *hetero_ranks = -1;
    *cluster_id = 0;
    *ncluster = 1;
    type = flagcxCommunicatorHomo;

    if (nranks <= 1)
        return flagcxSuccess;

    std::map<std::string, int> clusterMap;
    clusterMap[allData[0].internal] = 1;
    int numClusters = 1;
    int currCluster = 0;
    int aggRanks = 1;
    int homoRootRank = 0;
    for (int i = 1; i < nranks; ++i)
    {
        std::string cls = allData[i].internal;
        auto it = clusterMap.find(cls);
        if (it != clusterMap.end())
        {
            it->second = it->second + 1;
        }
        else
        {
            clusterMap[cls] = 1;
            numClusters += 1;
            if (*homo_rank >= aggRanks)
            {
                *homo_rank = *homo_rank - aggRanks;
                currCluster += 1;
            }
            aggRanks = 0;
            homoRootRank = i;
        }
        aggRanks += 1;

        if (i == rank)
        {
            *homo_root_rank = homoRootRank;
        }
    }

    *homo_ranks = clusterMap[allData[rank].internal];

    if (clusterMap.size() > 1)
    {
        type = flagcxCommunicatorHybrid;
    }
    else
    {
        type = flagcxCommunicatorHomo;
    }

    if (type == flagcxCommunicatorHybrid)
    {
        const char *useDev = flagcxGetEnv("FLAGCX_USEDEV");
        int useDev_;
        if (useDev == NULL)
        {
            useDev_ = 0;
        }
        else
        {
            useDev_ = std::stoi(useDev);
        }
        int currDev;
        deviceAdaptor->getDevice(&currDev);
        if (currDev == useDev_)
        {
            *hetero_rank = currCluster;
            if (currCluster == 0)
            {
                *hetero_root_rank = rank;
            }
            *hetero_ranks = clusterMap.size();
        }
        *cluster_id = currCluster;
        *ncluster = numClusters;
    }

    return flagcxSuccess;
}