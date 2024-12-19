#include "flagcx.h"
#include "adaptor.h"
#include "bootstrap.h"
#include "flagcx_hetero.h"
#include "comm.h"
#include "alloc.h"
#include "check.h"
#include "param.h"

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
    //printf("comm_type:%d\n", cur_comm->comm_type);
#ifdef FORCE_HOMO_COMM
    return true;
#endif
#ifdef FORCE_HYBRID_COMM
    return false;
#endif
    return cur_comm->comm_type == flagcxCommunicatorHomo;
}

static flagcxResult_t flagcxDetectCommType(const flagcxVendor* allData, int nranks, flagcxCommunicatorType_t &type) {
    if (nranks <= 1) {
        type = flagcxCommunicatorHomo;
        return flagcxSuccess;
    }
    for (int i = 1; i < nranks; ++i) {
        if (strncmp(allData[i].internal, allData[0].internal, MAX_VENDOR_LEN)) {
        type = flagcxCommunicatorHybrid;
        return flagcxSuccess;
        }
    }
    type = flagcxCommunicatorHomo;
    return flagcxSuccess;
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
    (*comm)->magic = 0;
    (*comm)->abortFlag = 0;
    (*comm)->bootstrap = NULL;
    (*comm)->homo_comm = NULL;
    (*comm)->hetero_comm = NULL;
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
    bootstrapBarrier((*comm)->bootstrap, rank, nranks, 0);
    FLAGCXCHECK(flagcxDetectCommType(vendorData, nranks, (*comm)->comm_type));

    // Reset commId and root rank calls underlying GetUniqueId function
    memset((void *)commId, 0, sizeof(*commId));
    if (rank == 0) {
        if (is_homo_comm()) {
            cclAdaptor->getUniqueId(&commId);
        } else {
            flagcxHeteroGetUniqueId(commId);
        }
    }

    // Use bootstrap to broadcast the new generated CommId across all ranks.
    bootstrapBroadcast((*comm)->bootstrap, rank, nranks, 0 /*root*/, (void*) commId, sizeof(flagcxUniqueId));
    bootstrapBarrier((*comm)->bootstrap, rank, nranks, 0);
    
    // Destroy bootstrap state and net if we use adaptor under homo circumstance
    bootstrapClose(state);

    // call underlying commInitRank
    if (is_homo_comm()) {
        // call flagcxHomoComm commInitRank
        return cclAdaptor->commInitRank(&(*comm)->homo_comm, nranks, commId, rank);
    }

    return flagcxHeteroCommInitRank(&(*comm)->hetero_comm, nranks, *commId, rank);
}

flagcxResult_t flagcxCommFinalize(flagcxComm_t comm) {
    if (is_homo_comm()) {
        return cclAdaptor->commFinalize(comm->homo_comm);
    }
    // TODO: to be implemented
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommDestroy(flagcxComm_t comm) {
    if (is_homo_comm()) {
        return cclAdaptor->commDestroy(comm->homo_comm);
    }
    return flagcxHeteroCommDestroy(comm->hetero_comm);
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
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
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
    if (is_homo_comm()) {
        return cclAdaptor->send(sendbuff, count, datatype, peer, comm->homo_comm, stream);
    }
    return flagcxHeteroSend(sendbuff, count, datatype, peer, comm->hetero_comm, stream);
}

flagcxResult_t flagcxRecv(void* recvbuff, size_t count, flagcxDataType_t datatype, int peer,
                          flagcxComm_t comm, flagcxStream_t stream) {
    if (is_homo_comm()) {
        return cclAdaptor->recv(recvbuff, count, datatype, peer, comm->homo_comm, stream);
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

flagcxResult_t flagcxStreamCreate(flagcxStream_t *stream) {
    return deviceAdaptor->streamCreate(stream);
}

flagcxResult_t flagcxStreamSynchronize(flagcxStream_t stream) {
    return deviceAdaptor->streamSynchronize(stream);
}

flagcxResult_t flagcxStreamDestroy(flagcxStream_t stream) {
    return deviceAdaptor->streamDestroy(stream);
}

flagcxResult_t flagcxMemAlloc(void** ptr, size_t size) {
    if (is_homo_comm()) {
        return cclAdaptor->memAlloc(ptr, size);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxMemFree(void *ptr) {
    if (is_homo_comm()) {
        return cclAdaptor->memFree(ptr);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommRegister(const flagcxComm_t comm, void* buff, size_t size, void** handle) {
    if (is_homo_comm()) {
        return cclAdaptor->commRegister(comm->homo_comm, buff, size, handle);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}

flagcxResult_t flagcxCommDeregister(const flagcxComm_t comm, void* handle) {
    if (is_homo_comm()) {
        return cclAdaptor->commDeregister(comm->homo_comm, handle);
    }
    // TODO: to be implemented.
    return flagcxNotSupported;
}
