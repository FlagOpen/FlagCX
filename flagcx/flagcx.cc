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

size_t getFlagcxDataTypeSize(flagcxDataType_t dtype) {
    switch (dtype) {
        // case flagcxInt8:
        case flagcxChar:
            return sizeof(char);           // 1 byte
        case flagcxUint8:
            return sizeof(unsigned char);   // 1 byte
        // case flagcxInt32:
        case flagcxInt:
            return sizeof(int);             // 4 bytes
        case flagcxUint32:
            return sizeof(unsigned int);    // 4 bytes
        case flagcxInt64:
            return sizeof(long long);       // 8 bytes
        case flagcxUint64:
            return sizeof(unsigned long long); // 8 bytes
        // case flagcxFloat16:
        case flagcxHalf:
            return 2;                       // Half precision float is 2 bytes
        // case flagcxFloat32:
        case flagcxFloat:
            return sizeof(float);           // 4 bytes
        // case flagcxFloat64:
        case flagcxDouble:
            return sizeof(double);          // 8 bytes
        case flagcxBfloat16:
            return 2;                       // BFloat16 is typically 2 bytes
        default:
            fprintf(stderr, "Unknown flagcx data type\n");
            return 0;
    }
}

// Wrapper function for deviceMemcpy that provides a default value
flagcxResult_t wrapper_deviceMemcpy(void *dst, void *src, size_t size, flagcxMemcpyType_t type, flagcxStream_t stream) {
    return deviceAdaptor->deviceMemcpy(dst, src, size, type, stream, NULL);
}

static struct flagcxDeviceHandle globalDeviceHandle {
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

flagcxResult_t flagCXEnsureCommReady() {
    if (cur_comm == NULL) {
        return flagcxInternalError;
    }
    if (cur_comm->comm_type != flagcxCommunicatorHybrid && cur_comm->comm_type != flagcxCommunicatorHomo) {
        return flagcxInternalError;
    }
    return flagcxSuccess;
}

bool is_homo_comm() {
    assert(flagCXEnsureCommReady() == flagcxSuccess);
#ifdef FORCE_HOMO_COMM
    return true;
#endif
#ifdef FORCE_HYBRID_COMM
    return false;
#endif
    return cur_comm->comm_type == flagcxCommunicatorHomo;
}

flagcxResult_t flagcxHandleInit(flagcxHandlerGroup_t *handler) {
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

flagcxResult_t flagcxHandleFree(flagcxHandlerGroup_t handler) {
    if (handler != NULL) {
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

flagcxResult_t flagcxGetVersion(int *version) {
    // TODO: check how to return flagcx version including flagcx core and flagcx adaptor
    if (is_homo_comm()) {
        return cclAdaptor->getVersion(version);
    }
    return flagcxHeteroGetVersion(version);
}

flagcxResult_t flagcxGetUniqueId(flagcxUniqueId_t *uniqueId) {
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

const char* flagcxGetErrorString(flagcxResult_t result) {
    if (is_homo_comm()) {
        return cclAdaptor->getErrorString(result);
    }
    return "Not implemented.";
}

const char* flagcxGetLastError(flagcxComm_t comm) {
    if (is_homo_comm()) {
        return cclAdaptor->getLastError(comm->homo_comm);
    }
    return "Not implemented.";
}

flagcxResult_t flagcxCommInitRank(flagcxComm_t *comm, int nranks, flagcxUniqueId_t commId, int rank) {
    if (nranks < 1 || rank < 0 || rank >= nranks) {
      WARN("Invalid rank requested : %d/%d", rank, nranks);
      return flagcxInvalidArgument;
    }

    // TODO: maybe move bootstrap out of flagcxComm and use a struct to store all handles
    // including uniqueId, bootstrap, communicator, stream, etc.
    (*comm) = NULL;
    flagcxCalloc(comm, 1);
    (*comm)->rank = rank;
    (*comm)->nranks = nranks;
    (*comm)->nclusters = -1;
    (*comm)->homo_root_rank = -1;
    (*comm)->homo_inter_rank = -1;
    (*comm)->homo_ranks = -1;
    (*comm)->hetero_rank = -1;
    (*comm)->hetero_root_rank = -1;
    (*comm)->hetero_ranks = -1;
    (*comm)->magic = 0;
    (*comm)->abortFlag = 0;
    (*comm)->bootstrap = NULL;
    (*comm)->homo_comm = NULL;
    (*comm)->hetero_comm = NULL;
    (*comm)->rank_to_homorank = NULL;
    (*comm)->cluster_ids = NULL;
    (*comm)->cluster_sizes = NULL;
    (*comm)->comm_type = flagcxCommunicatorUnknown;
    cur_comm = *comm;

    struct bootstrapState* state = NULL;
    FLAGCXCHECK(flagcxCalloc(&state, 1));
    state->rank = rank;
    state->nranks = nranks;
    state->abortFlag = (*comm)->abortFlag;
    (*comm)->bootstrap = state;
    state->magic = ((struct flagcxBootstrapHandle*)commId)->magic;
    (*comm)->magic = ((struct flagcxBootstrapHandle*)commId)->magic;

    // Init bootstrap net
    FLAGCXCHECK(bootstrapNetInit());

    // Init bootstrap state
    FLAGCXCHECK(bootstrapInit((struct flagcxBootstrapHandle*)commId, state));

    // Ready to detect heterogeneous/homogeneous communicator
    // Use bootstrap allgather to exchange Device info
    flagcxVendor *vendorData = NULL;  // temp data used for device vendor gather operation.
    
    // Get current gpu vendor
    flagcxVendor vendor;
    deviceAdaptor->getVendor(vendor.internal);
    FLAGCXCHECK(flagcxCalloc(&vendorData, nranks));
    memcpy(vendorData + rank, &vendor, sizeof(flagcxVendor));
    FLAGCXCHECK(bootstrapAllGather(state, (void *)vendorData, sizeof(flagcxVendor)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));

    // Init cluster info
    int *rankToHomoRankData;
    int *clusterIdData;
    int *heteroRankData;
    FLAGCXCHECK(flagcxCalloc(&rankToHomoRankData, nranks));
    FLAGCXCHECK(flagcxCalloc(&clusterIdData, nranks));
    FLAGCXCHECK(flagcxCalloc(&heteroRankData, nranks));
    FLAGCXCHECK(flagcxCollectClusterInfos(vendorData,
                                         (*comm)->comm_type,
                                         rankToHomoRankData + rank, &(*comm)->homo_root_rank, &(*comm)->homo_ranks,
                                         heteroRankData + rank, &(*comm)->hetero_root_rank, &(*comm)->hetero_ranks,
                                         clusterIdData + rank, &(*comm)->nclusters, rank, nranks));
    FLAGCXCHECK(bootstrapAllGather(state, (void *)rankToHomoRankData, sizeof(int)));
    FLAGCXCHECK(bootstrapAllGather(state, (void *)clusterIdData, sizeof(int)));
    FLAGCXCHECK(bootstrapAllGather(state, (void *)heteroRankData, sizeof(int)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));
    (*comm)->rank_to_homorank = rankToHomoRankData;
    (*comm)->cluster_ids = clusterIdData;
    (*comm)->hetero_rank = heteroRankData[rank];

    int *clusterSizes;
    FLAGCXCHECK(flagcxCalloc(&clusterSizes, (*comm)->nclusters));
    int cid = 0;
    int sum = 0;
    for (int i = 0; i < nranks; ++i) {
        if (clusterIdData[i] == cid + 1) {
            clusterSizes[cid] = i - sum;
            cid += 1;
            sum = i;
        }
    }
    clusterSizes[cid] = nranks - sum;
    (*comm)->cluster_sizes = clusterSizes;

    int start = 0;
    if (clusterIdData[rank] >= 1) {
        for (int i = 0; i < clusterIdData[rank]; ++i) {
            start += clusterSizes[i];
        }
    }
    for (int i = start; i < start + clusterSizes[clusterIdData[rank]]; ++i) {
        if (heteroRankData[i] != -1) {
            (*comm)->homo_inter_rank = i - start;
            break;
        }
    }

    // Reset commId and homo root rank calls underlying GetUniqueId function for initialization of homo communicator
    memset((void *)commId, 0, sizeof(*commId));
    if ((*comm)->rank_to_homorank[rank] == 0) {
        cclAdaptor->getUniqueId(&commId);
    }
    flagcxUniqueId *uniqueIdData;
    FLAGCXCHECK(flagcxCalloc(&uniqueIdData, nranks));
    if ((*comm)->rank_to_homorank[rank] == 0) {
        memcpy((void *)&uniqueIdData[rank], (void *)commId, sizeof(flagcxUniqueId));
    }
    FLAGCXCHECK(bootstrapAllGather(state, (void *)uniqueIdData, sizeof(flagcxUniqueId)));
    FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));

    memcpy((void *)commId, (void *)&uniqueIdData[(*comm)->homo_root_rank], sizeof(flagcxUniqueId));
    // call flagcxHomoComm commInitRank
    FLAGCXCHECK(cclAdaptor->commInitRank(&(*comm)->homo_comm, (*comm)->homo_ranks, commId, (*comm)->rank_to_homorank[rank]));

    // Reset commId and hetero root rank calls flagcxHeteroGetUniqueId
    if (!is_homo_comm()) {
        int *heteroRootRanks;
         FLAGCXCHECK(flagcxCalloc(&heteroRootRanks, nranks));
         heteroRootRanks[rank] = (*comm)->hetero_root_rank;
         FLAGCXCHECK(bootstrapAllGather(state, (void *)heteroRootRanks, sizeof(int)));
         if ((*comm)->hetero_ranks != -1) {
             for (int i = 0; i < nranks; ++i) {
                if (heteroRootRanks[i] != -1) {
                     (*comm)->hetero_root_rank = heteroRootRanks[i];
                     break;
                }
            }
         }
        memset((void *)commId, 0, sizeof(flagcxUniqueId));
        memset((void *)uniqueIdData, 0, nranks * sizeof(flagcxUniqueId));
        if ((*comm)->hetero_rank == 0) {
            flagcxHeteroGetUniqueId(commId);
            memcpy((void *)&uniqueIdData[(*comm)->rank], (void *)commId, sizeof(flagcxUniqueId));
        }
        FLAGCXCHECK(bootstrapAllGather(state, (void *)uniqueIdData, sizeof(flagcxUniqueId)));
        FLAGCXCHECK(bootstrapBarrier(state, rank, nranks, 0));

        if ((*comm)->hetero_rank != -1) {
            memcpy((void *)commId, (void *)&uniqueIdData[(*comm)->hetero_root_rank], sizeof(flagcxUniqueId));
            // call flagcxHeteroCommInitRank
            FLAGCXCHECK(flagcxHeteroCommInitRank(&(*comm)->hetero_comm, (*comm)->hetero_ranks, *commId, (*comm)->hetero_rank));
        }

        free(heteroRootRanks);
    }

    free(heteroRankData);
    free(uniqueIdData);
    free(vendorData);
    INFO(FLAGCX_INIT, "rank = %d, nranks = %d, nclusters = %d, cluster_id = %d, cluster_size = %d, homo_rank = %d, homo_root_rank = %d, homo_inter_rank = %d, homo_ranks = %d, hetero_rank = %d, hetero_root_rank = %d, hetero_ranks = %d",
         rank,
         nranks,
         (*comm)->nclusters,
         (*comm)->cluster_ids[rank],
         (*comm)->cluster_sizes[(*comm)->cluster_ids[rank]],
         (*comm)->rank_to_homorank[rank],
         (*comm)->homo_root_rank,
         (*comm)->homo_inter_rank,
         (*comm)->homo_ranks,
         (*comm)->hetero_rank,
         (*comm)->hetero_root_rank,
         (*comm)->hetero_ranks);

    return flagcxSuccess;
}

flagcxResult_t flagcxCommFinalize(flagcxComm_t comm) {
    if (is_homo_comm()) {
        return cclAdaptor->commFinalize(comm->homo_comm);
    }
    // TODO: to be implemented
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommDestroy(flagcxComm_t comm) {
    // Destroy cluster info
    free(comm->rank_to_homorank);
    free(comm->cluster_ids);
    free(comm->cluster_sizes);

    // Destroy bootstrap state and net
    bootstrapClose(comm->bootstrap);

    // Destroy hetero comm
    if (comm->hetero_rank != -1) {
        FLAGCXCHECK(flagcxHeteroCommDestroy(comm->hetero_comm));
    }

    // Destroy homo comm
    FLAGCXCHECK(cclAdaptor->commDestroy(comm->homo_comm));

    return flagcxSuccess;
}

flagcxResult_t flagcxCommAbort(flagcxComm_t comm) {
    if (is_homo_comm()) {
        return cclAdaptor->commAbort(comm->homo_comm);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommResume(flagcxComm_t comm) {
    if (is_homo_comm()) {
        return cclAdaptor->commResume(comm->homo_comm);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommSuspend(flagcxComm_t comm) {
    if (is_homo_comm()) {
        return cclAdaptor->commSuspend(comm->homo_comm);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommCount(const flagcxComm_t comm, int* count) {
    if (is_homo_comm()) {
        return cclAdaptor->commCount(comm->homo_comm, count);
    }
    return flagcxHeteroCommCount(comm->hetero_comm, count);
}

flagcxResult_t flagcxCommGetDeviceNumber(const flagcxComm_t comm, int* device) {
    if (is_homo_comm()) {
        return cclAdaptor->commGetDeviceNumber(comm->homo_comm, device);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommUserRank(const flagcxComm_t comm, int* rank) {
    if (is_homo_comm()) {
        return cclAdaptor->commUserRank(comm->homo_comm, rank);
    }
    return flagcxHeteroCommUserRank(comm->hetero_comm, rank);
}

flagcxResult_t flagcxCommGetAsyncError(flagcxComm_t comm, flagcxResult_t asyncError) {
    if (is_homo_comm()) {
        return cclAdaptor->commGetAsyncError(comm->homo_comm, asyncError);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxReduce(const void* sendbuff, void* recvbuff, size_t count,
                            flagcxDataType_t datatype, flagcxRedOp_t op, int root,
                            flagcxComm_t comm, flagcxStream_t stream) {
    if (is_homo_comm()) {
        return cclAdaptor->reduce(sendbuff, recvbuff, count, datatype, op, root, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxGather(const void* sendbuff, void* recvbuff, size_t count,
                            flagcxDataType_t datatype, int root, flagcxComm_t comm,
                            flagcxStream_t stream) {
    if (is_homo_comm()) {
        return cclAdaptor->gather(sendbuff, recvbuff, count, datatype, root, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxScatter(const void* sendbuff, void* recvbuff, size_t count,
                             flagcxDataType_t datatype, int root, flagcxComm_t comm,
                             flagcxStream_t stream) {
    if (is_homo_comm()) {
        return cclAdaptor->scatter(sendbuff, recvbuff, count, datatype, root, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxBroadcast(const void* sendbuff, void* recvbuff, size_t count,
                               flagcxDataType_t datatype, int root, flagcxComm_t comm,
                               flagcxStream_t stream) {
    if (is_homo_comm()) {
        return cclAdaptor->broadcast(sendbuff, recvbuff, count, datatype, root, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxAllReduce(const void* sendbuff, void* recvbuff, size_t count,
                               flagcxDataType_t datatype, flagcxRedOp_t op, flagcxComm_t comm,
                               flagcxStream_t stream) {
    if (is_homo_comm()) {
        return cclAdaptor->allReduce(sendbuff, recvbuff, count, datatype, op, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxReduceScatter(const void* sendbuff, void* recvbuff, size_t recvcount,
                                   flagcxDataType_t datatype, flagcxRedOp_t op, flagcxComm_t comm,
                                   flagcxStream_t stream) {
    if (is_homo_comm()) {
        return cclAdaptor->reduceScatter(sendbuff, recvbuff, recvcount, datatype, op, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxAllGather(const void* sendbuff, void* recvbuff, size_t sendcount,
                               flagcxDataType_t datatype, flagcxComm_t comm, flagcxStream_t stream) {
    if (is_homo_comm()) {
        return cclAdaptor->allGather(sendbuff, recvbuff, sendcount, datatype, comm->homo_comm, stream);
    } else {
        // intra-cluster gather
        int offset = 0;
        for (int i = 0; i < comm->cluster_ids[comm->rank]; ++i) {
            offset += comm->cluster_sizes[i];
        }
        FLAGCXCHECK(cclAdaptor->gather(sendbuff, (void *)((char *)recvbuff + getFlagcxDataTypeSize(datatype) * offset * sendcount), sendcount, datatype, comm->homo_inter_rank, comm->homo_comm, stream));


        // inter-cluster sendrecv
        if (comm->homo_inter_rank == comm->rank_to_homorank[comm->rank]) {
            int offset_recv = 0;
            flagcxGroupStart();
            for (int i = 0; i < comm->hetero_ranks; ++i) {
                if (comm->hetero_rank == i) {
                    offset_recv += comm->cluster_sizes[i];
                    continue;
                }
                FLAGCXCHECK(flagcxHeteroSend((void *)((char *)recvbuff + getFlagcxDataTypeSize(datatype) * offset * sendcount), sendcount * comm->cluster_sizes[comm->hetero_rank], datatype, i, comm->hetero_comm, stream));
                FLAGCXCHECK(flagcxHeteroRecv((void *)((char *)recvbuff + getFlagcxDataTypeSize(datatype) * offset_recv * sendcount), sendcount * comm->cluster_sizes[i], datatype, i, comm->hetero_comm, stream));
                offset_recv += comm->cluster_sizes[i];
            }
            flagcxGroupEnd();
        }

        // intra-cluster broadcast
        FLAGCXCHECK(cclAdaptor->broadcast(recvbuff, recvbuff, sendcount * comm->nranks, datatype, comm->homo_inter_rank, comm->homo_comm, stream));
    }
    return flagcxSuccess;
}

flagcxResult_t flagcxAlltoAll(const void* sendbuff, void* recvbuff, size_t count,
                              flagcxDataType_t datatype, flagcxComm_t comm, flagcxStream_t stream) {
    if (is_homo_comm()) {
        return cclAdaptor->alltoAll(sendbuff, recvbuff, count, datatype, comm->homo_comm, stream);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxSend(const void* sendbuff, size_t count, flagcxDataType_t datatype, int peer,
                          flagcxComm_t comm, flagcxStream_t stream) {
    if (is_homo_comm() || comm->cluster_ids[comm->rank] == comm->cluster_ids[peer]) {
        return cclAdaptor->send(sendbuff, count, datatype, comm->rank_to_homorank[peer], comm->homo_comm, stream);
    }
    return flagcxHeteroSend(sendbuff, count, datatype, peer, comm->hetero_comm, stream);
}

flagcxResult_t flagcxRecv(void* recvbuff, size_t count, flagcxDataType_t datatype, int peer,
                          flagcxComm_t comm, flagcxStream_t stream) {
    if (is_homo_comm() || comm->cluster_ids[comm->rank] == comm->cluster_ids[peer]) {
        return cclAdaptor->recv(recvbuff, count, datatype, comm->rank_to_homorank[peer], comm->homo_comm, stream);
    }
    return flagcxHeteroRecv(recvbuff, count, datatype, peer, comm->hetero_comm, stream);
}

flagcxResult_t flagcxGroupStart() {
    if (is_homo_comm()) {
        return cclAdaptor->groupStart();
    }
    return flagcxHeteroGroupStart();
}

flagcxResult_t flagcxGroupEnd() {
    if (is_homo_comm()) {
        return cclAdaptor->groupEnd();
    }
    return flagcxHeteroGroupEnd();
}