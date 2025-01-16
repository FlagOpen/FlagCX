#include "flagcx.h"
#include "adaptor.h"
#include "bootstrap.h"
#include "flagcx_hetero.h"
#include "comm.h"
#include "alloc.h"
#include "check.h"
#include "param.h"
#include "cluster.h"

#include <cassert>
#include <stdio.h>
#include <string.h>

static flagcxComm_t cur_comm = NULL;

size_t getFlagcxDataTypeSize(flagcxDataType_t dtype)
{
    switch (dtype)
    {
    // case flagcxInt8:
    case flagcxChar:
        return sizeof(char); // 1 byte
    case flagcxUint8:
        return sizeof(unsigned char); // 1 byte
    // case flagcxInt32:
    case flagcxInt:
        return sizeof(int); // 4 bytes
    case flagcxUint32:
        return sizeof(unsigned int); // 4 bytes
    case flagcxInt64:
        return sizeof(long long); // 8 bytes
    case flagcxUint64:
        return sizeof(unsigned long long); // 8 bytes
    // case flagcxFloat16:
    case flagcxHalf:
        return 2; // Half precision float is 2 bytes
    // case flagcxFloat32:
    case flagcxFloat:
        return sizeof(float); // 4 bytes
    // case flagcxFloat64:
    case flagcxDouble:
        return sizeof(double); // 8 bytes
    case flagcxBfloat16:
        return 2; // BFloat16 is typically 2 bytes
    default:
        fprintf(stderr, "Unknown flagcx data type\n");
        return 0;
    }
}

// Wrapper function for deviceMemcpy without the usage of invalid args
flagcxResult_t wrapper_deviceMemcpy(void *dst, void *src, size_t size, flagcxMemcpyType_t type, flagcxStream_t stream)
{
    return deviceAdaptor->deviceMemcpy(dst, src, size, type, stream, NULL);
}

static struct flagcxDeviceHandle globalDeviceHandle
{
    // Basic functions
    deviceAdaptor->deviceSynchronize,
        wrapper_deviceMemcpy,
        deviceAdaptor->deviceMemset,
        deviceAdaptor->deviceMalloc,
        deviceAdaptor->deviceFree,
        deviceAdaptor->setDevice,
        deviceAdaptor->getDevice,
        deviceAdaptor->getDeviceCount,
        deviceAdaptor->getVendor,
        // Stream functions
        deviceAdaptor->streamCreate,
        deviceAdaptor->streamDestroy,
        deviceAdaptor->streamSynchronize,
        deviceAdaptor->streamQuery,
};

flagcxResult_t flagCXEnsureCommReady()
{
    if (cur_comm == NULL)
    {
        return flagcxInternalError;
    }
    if (cur_comm->comm_type != flagcxCommunicatorHybrid && cur_comm->comm_type != flagcxCommunicatorHomo)
    {
        return flagcxInternalError;
    }
    return flagcxSuccess;
}

bool is_homo_comm()
{
    assert(flagCXEnsureCommReady() == flagcxSuccess);
#ifdef FORCE_HOMO_COMM
    return true;
#endif
#ifdef FORCE_HYBRID_COMM
    return false;
#endif
    return cur_comm->comm_type == flagcxCommunicatorHomo;
}

bool has_host_comm()
{
    return (cclAdaptors[flagcxCCLAdaptorHost] != NULL);
}

flagcxResult_t flagcxHandleInit(flagcxHandlerGroup_t *handler)
{
    (*handler) = NULL;
    flagcxCalloc(handler, 1);
    flagcxCalloc(&(*handler)->status, 1);
    flagcxCalloc(&(*handler)->uniqueId, 1);
    flagcxCalloc(&(*handler)->comm, 1);
    flagcxCalloc(&(*handler)->devHandle, 1);
    *(*handler)->status = 0;
    *(*handler)->devHandle = globalDeviceHandle;
    return flagcxSuccess;
}

flagcxResult_t flagcxHandleFree(flagcxHandlerGroup_t handler)
{
    if (handler != NULL)
    {
        free(handler->status);
        free(handler->uniqueId);
        free(handler->comm);
        free(handler->devHandle);
        handler->status = NULL;
        handler->uniqueId = NULL;
        handler->comm = NULL;
        handler->devHandle = NULL;
        free(handler);
        handler = NULL;
    }
    return flagcxSuccess;
}

flagcxResult_t flagcxGetVersion(int *version)
{
    // TODO: check how to return flagcx version including flagcx core and flagcx adaptor
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->getVersion(version);
    }
    return flagcxHeteroGetVersion(version);
}

flagcxResult_t flagcxGetUniqueId(flagcxUniqueId_t *uniqueId)
{
    (*uniqueId) = NULL;
    flagcxCalloc(uniqueId, 1);

    // Init bootstrap net
    FLAGCXCHECK(bootstrapNetInit());

    // Init uniqueId using bootstrap
    struct flagcxBootstrapHandle handle;
    FLAGCXCHECK(bootstrapGetUniqueId(&handle));
    // flagcxUniqueId and bootstrapHandle don't have the same size and alignment
    // reset to 0 to avoid undefined data
    memset((void *)*uniqueId, 0, sizeof(**uniqueId));
    // copy to avoid alignment mismatch
    memcpy((void *)*uniqueId, &handle, sizeof(handle));
    return flagcxSuccess;
}

const char *flagcxGetErrorString(flagcxResult_t result)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->getErrorString(result);
    }
    return "Not implemented.";
}

const char *flagcxGetLastError(flagcxComm_t comm)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->getLastError(comm->homo_comm);
    }
    return "Not implemented.";
}

flagcxResult_t flagcxCommInitRank(flagcxComm_t *comm, int nranks, flagcxUniqueId_t commId, int rank)
{
    if (nranks < 1 || rank < 0 || rank >= nranks)
    {
        WARN("Invalid rank requested : %d/%d", rank, nranks);
        return flagcxInvalidArgument;
    }

    (*comm) = NULL;
    flagcxCalloc(comm, 1);
    (*comm)->rank = rank;
    (*comm)->nranks = nranks;
    (*comm)->nclusters = -1;
    (*comm)->homo_rank = -1;
    (*comm)->homo_root_rank = -1;
    (*comm)->homo_ranks = -1;
    (*comm)->magic = 0;
    (*comm)->abortFlag = 0;
    (*comm)->bootstrap = NULL;
    (*comm)->host_comm = NULL;
    (*comm)->homo_comm = NULL;
    (*comm)->hetero_comm = NULL;
    (*comm)->cluster_ids = NULL;
    (*comm)->cluster_sizes = NULL;
    (*comm)->cluster_inter_ranks = NULL;
    (*comm)->globalrank2homorank = NULL;
    (*comm)->comm_type = flagcxCommunicatorUnknown;
    cur_comm = *comm;

    struct bootstrapState *state = NULL;
    FLAGCXCHECK(flagcxCalloc(&state, 1));
    state->rank = rank;
    state->nranks = nranks;
    state->abortFlag = (*comm)->abortFlag;
    (*comm)->bootstrap = state;
    state->magic = ((struct flagcxBootstrapHandle *)commId)->magic;
    (*comm)->magic = ((struct flagcxBootstrapHandle *)commId)->magic;

    // Init bootstrap net
    FLAGCXCHECK(bootstrapNetInit());

    // Init bootstrap state
    FLAGCXCHECK(bootstrapInit((struct flagcxBootstrapHandle *)commId, state));

    // Ready to detect heterogeneous/homogeneous communicator
    // Use bootstrap allgather to exchange Device info
    flagcxVendor *vendorData = NULL; // temp data used for device vendor gather operation.

    // Get current gpu vendor
    flagcxVendor vendor;
    deviceAdaptor->getVendor(vendor.internal);
    FLAGCXCHECK(flagcxCalloc(&vendorData, nranks));
    memcpy(vendorData + rank, &vendor, sizeof(flagcxVendor));
    FLAGCXCHECK(bootstrapAllGather(state, (void *)vendorData, sizeof(flagcxVendor)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));

    // Init cluster info
    int *globalRankToHomoRankData;
    int *clusterIdData;
    int *clusterInterRankData;
    FLAGCXCHECK(flagcxCalloc(&globalRankToHomoRankData, nranks));
    FLAGCXCHECK(flagcxCalloc(&clusterIdData, nranks));
    FLAGCXCHECK(flagcxCalloc(&clusterInterRankData, nranks));
    FLAGCXCHECK(flagcxCollectClusterInfos(vendorData,
                                          (*comm)->comm_type,
                                          globalRankToHomoRankData + rank, &(*comm)->homo_root_rank, &(*comm)->homo_ranks,
                                          clusterIdData + rank, clusterInterRankData + rank, &(*comm)->nclusters, rank, nranks));
    FLAGCXCHECK(bootstrapAllGather(state, (void *)globalRankToHomoRankData, sizeof(int)));
    FLAGCXCHECK(bootstrapAllGather(state, (void *)clusterIdData, sizeof(int)));
    FLAGCXCHECK(bootstrapAllGather(state, (void *)clusterInterRankData, sizeof(int)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));
    (*comm)->homo_rank = globalRankToHomoRankData[rank];
    (*comm)->cluster_ids = clusterIdData;
    (*comm)->globalrank2homorank = globalRankToHomoRankData;

    int *clusterSizes;
    int *clusterInterRanks;
    FLAGCXCHECK(flagcxCalloc(&clusterSizes, (*comm)->nclusters));
    FLAGCXCHECK(flagcxCalloc(&clusterInterRanks, (*comm)->nclusters));
    int cid = 0;
    int sum = 0;
    for (int i = 0; i < nranks; ++i)
    {
        if (clusterIdData[i] == cid + 1)
        {
            clusterSizes[cid] = i - sum;
            cid += 1;
            sum = i;
        }
    }
    clusterSizes[cid] = nranks - sum;
    (*comm)->cluster_sizes = clusterSizes;

    for (int i = 0; i < nranks; ++i)
    {
        if (clusterInterRankData[i] != -1)
        {
            clusterInterRanks[clusterIdData[i]] = clusterInterRankData[i];
        }
    }
    (*comm)->cluster_inter_ranks = clusterInterRanks;

    int start = 0;
    if (clusterIdData[rank] >= 1)
    {
        for (int i = 0; i < clusterIdData[rank]; ++i)
        {
            start += clusterSizes[i];
        }
    }
    (*comm)->homo_inter_rank = clusterInterRanks[clusterIdData[rank]] - start;

    // Reset commId and homo root rank calls underlying GetUniqueId function for initialization of homo communicator
    memset((void *)commId, 0, sizeof(*commId));
    if ((*comm)->homo_rank == 0)
    {
        cclAdaptors[flagcxCCLAdaptorDevice]->getUniqueId(&commId);
    }
    flagcxUniqueId *uniqueIdData;
    FLAGCXCHECK(flagcxCalloc(&uniqueIdData, nranks));
    if ((*comm)->homo_rank == 0)
    {
        memcpy((void *)&uniqueIdData[rank], (void *)commId, sizeof(flagcxUniqueId));
    }
    FLAGCXCHECK(bootstrapAllGather(state, (void *)uniqueIdData, sizeof(flagcxUniqueId)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));

    memcpy((void *)commId, (void *)&uniqueIdData[(*comm)->homo_root_rank], sizeof(flagcxUniqueId));
    FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->commInitRank(&(*comm)->homo_comm, (*comm)->homo_ranks, commId, (*comm)->homo_rank, NULL));

    if (!is_homo_comm())
    {
        // Reset commId and hetero root rank calls flagcxHeteroGetUniqueId
        memset((void *)commId, 0, sizeof(flagcxUniqueId));
        memset((void *)uniqueIdData, 0, nranks * sizeof(flagcxUniqueId));
        if (rank == 0)
        {
            flagcxHeteroGetUniqueId(commId);
            memcpy((void *)&uniqueIdData[0], (void *)commId, sizeof(flagcxUniqueId));
        }
        FLAGCXCHECK(bootstrapAllGather(state, (void *)uniqueIdData, sizeof(flagcxUniqueId)));
        FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));

        memcpy((void *)commId, (void *)&uniqueIdData[0], sizeof(flagcxUniqueId));
        // call flagcxHeteroCommInitRank
        FLAGCXCHECK(flagcxHeteroCommInitRank(&(*comm)->hetero_comm, nranks, *commId, rank));

        // Init host cclAdaptor
        if (has_host_comm())
        {
            cclAdaptors[flagcxCCLAdaptorHost]->commInitRank(&(*comm)->host_comm, nranks, commId, rank, state);
        }
    }

    free(clusterInterRankData);
    free(uniqueIdData);
    free(vendorData);
    INFO(FLAGCX_INIT, "rank = %d, nranks = %d, nclusters = %d, cluster_id = %d, cluster_size = %d, cluster_inter_rank = %d, homo_rank = %d, homo_root_rank = %d, homo_inter_rank = %d, homo_ranks = %d",
         rank,
         nranks,
         (*comm)->nclusters,
         (*comm)->cluster_ids[rank],
         (*comm)->cluster_sizes[(*comm)->cluster_ids[rank]],
         (*comm)->cluster_inter_ranks[(*comm)->cluster_ids[rank]],
         (*comm)->homo_rank,
         (*comm)->homo_root_rank,
         (*comm)->homo_inter_rank,
         (*comm)->homo_ranks);

    return flagcxSuccess;
}

flagcxResult_t flagcxCommFinalize(flagcxComm_t comm)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->commFinalize(comm->homo_comm);
    }
    // TODO: to be implemented
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommDestroy(flagcxComm_t comm)
{
    // Destroy cluster info
    free(comm->cluster_ids);
    free(comm->cluster_sizes);
    free(comm->globalrank2homorank);

    // Destroy bootstrap state and net
    bootstrapClose(comm->bootstrap);

    // Destroy hetero comm
    if (!is_homo_comm())
    {
        FLAGCXCHECK(flagcxHeteroCommDestroy(comm->hetero_comm));
        // Destroy homo comm
        if (has_host_comm()) {
            FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorHost]->commDestroy(comm->host_comm));
        }
    }

    return flagcxSuccess;
}

flagcxResult_t flagcxCommAbort(flagcxComm_t comm)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->commAbort(comm->homo_comm);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommResume(flagcxComm_t comm)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->commResume(comm->homo_comm);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommSuspend(flagcxComm_t comm)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->commSuspend(comm->homo_comm);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommCount(const flagcxComm_t comm, int *count)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->commCount(comm->homo_comm, count);
    }
    return flagcxHeteroCommCount(comm->hetero_comm, count);
}

flagcxResult_t flagcxCommGetDeviceNumber(const flagcxComm_t comm, int *device)
{
    return cclAdaptors[flagcxCCLAdaptorDevice]->commGetDeviceNumber(comm->homo_comm, device);
}

flagcxResult_t flagcxCommUserRank(const flagcxComm_t comm, int *rank)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->commUserRank(comm->homo_comm, rank);
    }
    return flagcxHeteroCommUserRank(comm->hetero_comm, rank);
}

flagcxResult_t flagcxCommGetAsyncError(flagcxComm_t comm, flagcxResult_t asyncError)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->commGetAsyncError(comm->homo_comm, asyncError);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxReduce(const void *sendbuff, void *recvbuff, size_t count,
                            flagcxDataType_t datatype, flagcxRedOp_t op, int root,
                            flagcxComm_t comm, flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->reduce(sendbuff, recvbuff, count, datatype, op, root, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxGather(const void *sendbuff, void *recvbuff, size_t count,
                            flagcxDataType_t datatype, int root, flagcxComm_t comm,
                            flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->gather(sendbuff, recvbuff, count, datatype, root, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxScatter(const void *sendbuff, void *recvbuff, size_t count,
                             flagcxDataType_t datatype, int root, flagcxComm_t comm,
                             flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->scatter(sendbuff, recvbuff, count, datatype, root, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxBroadcast(const void *sendbuff, void *recvbuff, size_t count,
                               flagcxDataType_t datatype, int root, flagcxComm_t comm,
                               flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->broadcast(sendbuff, recvbuff, count, datatype, root, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

// A wrapper over BootstrapAllReduce.
// TODO: consider move to another place.
flagcxResult_t wrapperAllReduceBootstrap(const void* sendbuff, void* recvbuff, size_t count,
                                        flagcxDataType_t datatype, flagcxRedOp_t op, flagcxComm_t comm,
                                        flagcxStream_t stream) {
  uint64_t timers[TIMERS_COLL_COUNT] = {0};
  timers[TIMER_COLL_TOTAL] = clockNano();

  // step 0: parameter validation
  if (datatype == flagcxHalf || datatype == flagcxBfloat16) {
    WARN("Unsupported datatype %d", datatype);
    return flagcxInvalidArgument;
  }
  if (op != flagcxSum && op != flagcxMax && op != flagcxMin) {
    WARN("Unsupported reduction operation %d", op);
    return flagcxInvalidArgument;
  }
  // step 1: copy data from GPU memory to Host memory
  timers[TIMER_COLL_MEM_D2H] = clockNano();
  size_t databytes = count * getFlagcxDataTypeSize(datatype);
  char *sbuff = nullptr;
  FLAGCXCHECK(flagcxCalloc(&sbuff, databytes));
  wrapper_deviceMemcpy(sbuff, const_cast<void *>(sendbuff), databytes, flagcxMemcpyDeviceToHost, stream);
  // step 2: wait until memcpy done.
  deviceAdaptor->streamSynchronize(stream);
  timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

  // step 3: start the allreduce primitive via bootstrap
  // use in-place version
  timers[TIMER_COLL_COMM] = clockNano();
  flagcxResult_t res = AllReduceBootstrap(comm->bootstrap, (void *)sbuff, (void *)sbuff, count, datatype, op);
  timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

  // step 4: copy data back to GPU memory
  timers[TIMER_COLL_MEM_H2D] = clockNano();
  wrapper_deviceMemcpy(recvbuff, (void *)sbuff, databytes, flagcxMemcpyHostToDevice, stream);

  // For now, use synchronized way to free the buffer
  deviceAdaptor->streamSynchronize(stream);
  free(sbuff);
  timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];
  timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
  INFO(FLAGCX_COLL,
       "Flagcx timings - %s: rank %d nranks %d total %.2fms (memory d2h %.2fms, memory h2d %.2fms, comm %.2fms)",
       "wrapperAllReduceBootstrap", comm->rank, comm->nranks,
       timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6, timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
  return res;
}

flagcxResult_t flagcxAllReduce(const void* sendbuff, void* recvbuff, size_t count,
                               flagcxDataType_t datatype, flagcxRedOp_t op, flagcxComm_t comm,
                               flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->allReduce(sendbuff, recvbuff, count, datatype, op, comm->homo_comm, stream);
    }
    else
    {
        char *useBootstrap = getenv("USE_BOOTSTRAP_CCL");
        if (useBootstrap)
        {
            INFO(FLAGCX_COLL | FLAGCX_ENV, "USE_BOOTSTRAP_CCL is set.");
            return wrapperAllReduceBootstrap(sendbuff, recvbuff, count, datatype, op, comm, stream);
        }
        if (has_host_comm())
        {
            void *buff_in;
            void *buff_out;
            size_t size = count * getFlagcxDataTypeSize(datatype);
            deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost);
            deviceAdaptor->deviceMalloc(&buff_out, size, flagcxMemHost);
            deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size, flagcxMemcpyDeviceToHost, NULL, NULL);
            cclAdaptors[flagcxCCLAdaptorHost]->allReduce(buff_in, buff_out, count, datatype, op, comm->host_comm, NULL);
            deviceAdaptor->deviceMemcpy(recvbuff, buff_out, size, flagcxMemcpyHostToDevice, NULL, NULL);
            deviceAdaptor->deviceFree(buff_in, flagcxMemHost);
            deviceAdaptor->deviceFree(buff_out, flagcxMemHost);
        }
        else
        {
            // intra-cluster reduce
            FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->reduce(sendbuff, recvbuff, count, datatype, op, comm->homo_inter_rank, comm->homo_comm, stream));

            // inter-cluster sendrecv
            deviceAdaptor->streamSynchronize(stream);
            if (comm->homo_inter_rank != comm->homo_rank)
            {
                deviceAdaptor->deviceMemset(recvbuff, 0, count * getFlagcxDataTypeSize(datatype), flagcxMemDevice, stream);
            }
            int cid = 0;
            flagcxGroupStart();
            for (int i = 0; i < comm->nclusters; ++i)
            {
                if (comm->cluster_ids[comm->rank] == i)
                    continue;
                // TODO: better to add an assertation ensuring that comm->ncluster <= comm->homo_ranks
                int homo_rank_to_recv_from_cluster = (comm->homo_inter_rank - cid - 1 + comm->homo_ranks) % comm->homo_ranks;
                if (comm->homo_rank == homo_rank_to_recv_from_cluster)
                {
                    FLAGCXCHECK(flagcxHeteroRecv(recvbuff, count, datatype, comm->cluster_inter_ranks[i], comm->hetero_comm, stream));
                }
                int homo_rank_to_send_to_cluster = (comm->globalrank2homorank[comm->cluster_inter_ranks[i]] - cid - 1 + comm->cluster_sizes[i]) % comm->cluster_sizes[i];
                int global_rank_to_send_to_cluster = homo_rank_to_send_to_cluster - comm->globalrank2homorank[comm->cluster_inter_ranks[i]] + comm->cluster_inter_ranks[i];
                if (comm->homo_inter_rank == comm->homo_rank)
                {
                    FLAGCXCHECK(flagcxHeteroSend(recvbuff, count, datatype, global_rank_to_send_to_cluster, comm->hetero_comm, stream));
                }
                cid += 1;
            }
            flagcxGroupEnd();

            // intra-cluster allreduce
            FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->allReduce(recvbuff, recvbuff, count, datatype, op, comm->homo_comm, stream));
        }
    }
    return flagcxSuccess;
}

flagcxResult_t flagcxReduceScatter(const void *sendbuff, void *recvbuff, size_t recvcount,
                                   flagcxDataType_t datatype, flagcxRedOp_t op, flagcxComm_t comm,
                                   flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->reduceScatter(sendbuff, recvbuff, recvcount, datatype, op, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

// A wrapper over AllGatherBootstrap.
// TODO: consider move to another place.
flagcxResult_t wrapperAllGatherBootstrap(const void* sendbuff, void* recvbuff, size_t sendcount,
                                         flagcxDataType_t datatype, flagcxComm_t comm, flagcxStream_t stream) {
  uint64_t timers[TIMERS_COLL_COUNT] = {0};
  timers[TIMER_COLL_TOTAL] = clockNano();

  // step 1: alloc recv buffer and copy sendbuff from GPU memory to Host memory
  timers[TIMER_COLL_MEM_D2H] = clockNano();
  size_t recvbytes = sendcount * getFlagcxDataTypeSize(datatype) * comm->nranks;
  char *rbuff = nullptr;
  FLAGCXCHECK(flagcxCalloc(&rbuff, recvbytes));
  // use in-place buffer
  char *sbuff = rbuff + comm->rank * getFlagcxDataTypeSize(datatype) * sendcount;
  wrapper_deviceMemcpy(sbuff, const_cast<void *>(sendbuff), getFlagcxDataTypeSize(datatype) * sendcount, flagcxMemcpyDeviceToHost, stream);

  // step 2: wait until memcpy done.
  deviceAdaptor->streamSynchronize(stream);
  timers[TIMER_COLL_MEM_D2H] = clockNano() - timers[TIMER_COLL_MEM_D2H];

  // step 3: start the allreduce primitive via bootstrap
  // use in-place version
  timers[TIMER_COLL_COMM] = clockNano();
  flagcxResult_t res = AllGatherBootstrap(comm->bootstrap, (void *)(sbuff), (void *)rbuff, sendcount, datatype);
  timers[TIMER_COLL_COMM] = clockNano() - timers[TIMER_COLL_COMM];

  // step 4: copy data back to GPU memory
  timers[TIMER_COLL_MEM_H2D] = clockNano();
  wrapper_deviceMemcpy(recvbuff, (void *)rbuff, recvbytes, flagcxMemcpyHostToDevice, stream);

  // For now, use synchronized way to free the buffer
  deviceAdaptor->streamSynchronize(stream);
  free(rbuff);
  timers[TIMER_COLL_MEM_H2D] = clockNano() - timers[TIMER_COLL_MEM_H2D];
  timers[TIMER_COLL_TOTAL] = clockNano() - timers[TIMER_COLL_TOTAL];
  INFO(FLAGCX_COLL,
       "Flagcx timings - %s: rank %d nranks %d total %.2fms (memory d2h %.2fms, memory h2d %.2fms, comm %.2fms)",
       "wrapperAllGatherBootstrap", comm->rank, comm->nranks,
       timers[TIMER_COLL_TOTAL] / 1e6, timers[TIMER_COLL_MEM_D2H] / 1e6, timers[TIMER_COLL_MEM_H2D] / 1e6, timers[TIMER_COLL_COMM] / 1e6);
  return res;
}

flagcxResult_t flagcxAllGather(const void *sendbuff, void *recvbuff, size_t sendcount,
                               flagcxDataType_t datatype, flagcxComm_t comm, flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->allGather(sendbuff, recvbuff, sendcount, datatype, comm->homo_comm, stream);
    }
    else
    {
        char* useBootstrap = getenv("USE_BOOTSTRAP_CCL");
        if (useBootstrap) {
            return wrapperAllGatherBootstrap(sendbuff, recvbuff, sendcount, datatype, comm, stream);
        }
        if (has_host_comm())
        {
            void *buff_in;
            void *buff_out;
            size_t size = sendcount * getFlagcxDataTypeSize(datatype);
            size_t totalSize = comm->nranks * size;
            deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost);
            deviceAdaptor->deviceMalloc(&buff_out, totalSize, flagcxMemHost);
            deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size, flagcxMemcpyDeviceToHost, NULL, NULL);
            cclAdaptors[flagcxCCLAdaptorHost]->allGather(buff_in, buff_out, sendcount, datatype, comm->host_comm, NULL);
            deviceAdaptor->deviceMemcpy(recvbuff, buff_out, totalSize, flagcxMemcpyHostToDevice, NULL, NULL);
            deviceAdaptor->deviceFree(buff_in, flagcxMemHost);
            deviceAdaptor->deviceFree(buff_out, flagcxMemHost);
        }
        else
        {
            // intra-cluster gather
            int offset = 0;
            for (int i = 0; i < comm->cluster_ids[comm->rank]; ++i)
            {
                offset += comm->cluster_sizes[i];
            }
            FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->gather(sendbuff, (void *)((char *)recvbuff + getFlagcxDataTypeSize(datatype) * offset * sendcount), sendcount, datatype, comm->homo_inter_rank, comm->homo_comm, stream));

            // inter-cluster sendrecv
            if (comm->homo_inter_rank == comm->homo_rank)
            {
                int offset_recv = 0;
                flagcxGroupStart();
                for (int i = 0; i < comm->nclusters; ++i)
                {
                    if (comm->cluster_ids[comm->rank] == i)
                    {
                        offset_recv += comm->cluster_sizes[i];
                        continue;
                    }
                    FLAGCXCHECK(flagcxHeteroSend((void *)((char *)recvbuff + getFlagcxDataTypeSize(datatype) * offset * sendcount), sendcount * comm->cluster_sizes[comm->cluster_ids[comm->rank]], datatype, comm->cluster_inter_ranks[i], comm->hetero_comm, stream));
                    FLAGCXCHECK(flagcxHeteroRecv((void *)((char *)recvbuff + getFlagcxDataTypeSize(datatype) * offset_recv * sendcount), sendcount * comm->cluster_sizes[i], datatype, comm->cluster_inter_ranks[i], comm->hetero_comm, stream));
                    offset_recv += comm->cluster_sizes[i];
                }
                flagcxGroupEnd();
            }

            // intra-cluster broadcast
            FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->broadcast(recvbuff, recvbuff, sendcount * comm->nranks, datatype, comm->homo_inter_rank, comm->homo_comm, stream));
        }
    }
    return flagcxSuccess;
}

flagcxResult_t flagcxAlltoAll(const void *sendbuff, void *recvbuff, size_t count,
                              flagcxDataType_t datatype, flagcxComm_t comm, flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->alltoAll(sendbuff, recvbuff, count, datatype, comm->homo_comm, stream);
    }
    else
    {
        if (has_host_comm())
        {
            void *buff_in;
            void *buff_out;
            size_t size = comm->nranks * count * getFlagcxDataTypeSize(datatype);
            deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost);
            deviceAdaptor->deviceMalloc(&buff_out, size, flagcxMemHost);
            deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size, flagcxMemcpyDeviceToHost, NULL, NULL);
            cclAdaptors[flagcxCCLAdaptorHost]->allGather(buff_in, buff_out, count, datatype, comm->host_comm, NULL);
            deviceAdaptor->deviceMemcpy(recvbuff, buff_out, size, flagcxMemcpyHostToDevice, NULL, NULL);
            deviceAdaptor->deviceFree(buff_in, flagcxMemHost);
            deviceAdaptor->deviceFree(buff_out, flagcxMemHost);
        }
        else
        {
            int size = count * getFlagcxDataTypeSize(datatype);
            const char *buffer_in = static_cast<const char *>(sendbuff);
            char *buffer_out = static_cast<char *>(recvbuff);

            // intra-cluster alltoall
            int offset = 0;
            for (int i = 0; i < comm->cluster_ids[comm->rank]; ++i)
            {
                offset += comm->cluster_sizes[i];
            }
            FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->alltoAll(static_cast<const void *>(buffer_in + offset * size), static_cast<void *>(buffer_out + offset * size), count, datatype, comm->homo_comm, stream))

            // inter-cluster sendrecv
            // TODO: use cluster_inter_rank to perform hetero sendrecv operation
            flagcxGroupStart();
            for (int r = 0; r < comm->nranks; ++r)
            {
                if (comm->cluster_ids[comm->rank] != comm->cluster_ids[r])
                {
                    FLAGCXCHECK(flagcxHeteroSend(static_cast<const void *>(buffer_in + r * size), count, datatype, r, comm->hetero_comm, stream));
                    FLAGCXCHECK(flagcxHeteroRecv(static_cast<void *>(buffer_out + r * size), count, datatype, r, comm->hetero_comm, stream));
                }
            }
            flagcxGroupEnd();
        }
    }
    return flagcxSuccess;
}

flagcxResult_t flagcxSend(const void *sendbuff, size_t count, flagcxDataType_t datatype, int peer,
                          flagcxComm_t comm, flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->send(sendbuff, count, datatype, peer, comm->homo_comm, stream);
    }
    else
    {
        if (has_host_comm())
        {
            void *buff_in;
            size_t size = count * getFlagcxDataTypeSize(datatype);
            deviceAdaptor->deviceMalloc(&buff_in, size, flagcxMemHost);
            deviceAdaptor->deviceMemcpy(buff_in, const_cast<void *>(sendbuff), size, flagcxMemcpyDeviceToHost, NULL, NULL);
            cclAdaptors[flagcxCCLAdaptorHost]->send(buff_in, count, datatype, peer, comm->host_comm, NULL);
            // buff_in will be freed in gloo adaptor send function
            // deviceAdaptor->deviceFree(buff_in, flagcxMemHost);
        }
        else
        {
            FLAGCXCHECK(flagcxHeteroSend(sendbuff, count, datatype, peer, comm->hetero_comm, stream));
        }
    }
    return flagcxSuccess;
}

flagcxResult_t flagcxRecv(void *recvbuff, size_t count, flagcxDataType_t datatype, int peer,
                          flagcxComm_t comm, flagcxStream_t stream)
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->recv(recvbuff, count, datatype, peer, comm->homo_comm, stream);
    }
    else
    {
        if (has_host_comm())
        {
            void *buff_out;
            size_t size = count * getFlagcxDataTypeSize(datatype);
            deviceAdaptor->deviceMalloc(&buff_out, size, flagcxMemHost);
            cclAdaptors[flagcxCCLAdaptorHost]->recv(buff_out, count, datatype, peer, comm->host_comm, NULL);
            deviceAdaptor->deviceMemcpy(recvbuff, buff_out, size, flagcxMemcpyHostToDevice, NULL, NULL);
            deviceAdaptor->deviceFree(buff_out, flagcxMemHost);
        }
        else
        {
            FLAGCXCHECK(flagcxHeteroRecv(recvbuff, count, datatype, peer, comm->hetero_comm, stream));
        }
    }
    return flagcxSuccess;
}

flagcxResult_t flagcxGroupStart()
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->groupStart();
    }
    else
    {
        if (has_host_comm())
        {
            cclAdaptors[flagcxCCLAdaptorHost]->groupStart();
        }
        else
        {
            FLAGCXCHECK(flagcxHeteroGroupStart());
        }
    }
    return flagcxSuccess;
}

flagcxResult_t flagcxGroupEnd()
{
    if (is_homo_comm())
    {
        return cclAdaptors[flagcxCCLAdaptorDevice]->groupEnd();
    }
    else
    {
        if (has_host_comm())
        {
            cclAdaptors[flagcxCCLAdaptorHost]->groupEnd();
        }
        else
        {
            FLAGCXCHECK(flagcxHeteroGroupEnd());
        }
    }
    return flagcxSuccess;
}
