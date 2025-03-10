#include "bootstrap_adaptor.h"

#ifdef USE_BOOTSTRAP_ADAPTOR

// TODO: unsupported
flagcxResult_t bootstrapAdaptorGetVersion(int *version) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorGetUniqueId(flagcxUniqueId_t *uniqueId) {
  return flagcxNotSupported;
}

// TODO: unsupported
const char *bootstrapAdaptorGetErrorString(flagcxResult_t result) {
  return "Not Implemented";
}

// TODO: unsupported
const char *bootstrapAdaptorGetLastError(flagcxInnerComm_t comm) {
  return "Not Implemented";
}

flagcxResult_t bootstrapAdaptorCommInitRank(flagcxInnerComm_t *comm, int nranks,
                                            flagcxUniqueId_t /*commId*/,
                                            int rank,
                                            bootstrapState *bootstrap) {
  // We don't need to do anything here
  return flagcxSuccess;
}

flagcxResult_t bootstrapAdaptorCommFinalize(flagcxInnerComm_t comm) {
  // We don't need to do anything here
  return flagcxSuccess;
}

flagcxResult_t bootstrapAdaptorCommDestroy(flagcxInnerComm_t comm) {
  // We don't need to do anything here
  return flagcxSuccess;
}

flagcxResult_t bootstrapAdaptorCommAbort(flagcxInnerComm_t comm) {
  // We don't need to do anything here
  return flagcxSuccess;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorCommResume(flagcxInnerComm_t comm) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorCommSuspend(flagcxInnerComm_t comm) {
  return flagcxNotSupported;
}

flagcxResult_t bootstrapAdaptorCommCount(const flagcxInnerComm_t comm,
                                         int *count) {
  *count = comm->base->nranks;
  return flagcxSuccess;
}

flagcxResult_t bootstrapAdaptorCommCuDevice(const flagcxInnerComm_t comm,
                                            int *device) {
  device = NULL;
  return flagcxSuccess;
}

flagcxResult_t bootstrapAdaptorCommUserRank(const flagcxInnerComm_t comm,
                                            int *rank) {
  *rank = comm->base->rank;
  return flagcxSuccess;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorCommGetAsyncError(flagcxInnerComm_t comm,
                                                 flagcxResult_t asyncError) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorReduce(const void *sendbuff, void *recvbuff,
                                      size_t count, flagcxDataType_t datatype,
                                      flagcxRedOp_t op, int root,
                                      flagcxInnerComm_t comm,
                                      flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorGather(const void *sendbuff, void *recvbuff,
                                      size_t count, flagcxDataType_t datatype,
                                      int root, flagcxInnerComm_t comm,
                                      flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorScatter(const void *sendbuff, void *recvbuff,
                                       size_t count, flagcxDataType_t datatype,
                                       int root, flagcxInnerComm_t comm,
                                       flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorBroadcast(const void *sendbuff, void *recvbuff,
                                         size_t count,
                                         flagcxDataType_t datatype, int root,
                                         flagcxInnerComm_t comm,
                                         flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t
bootstrapAdaptorAllReduce(const void *sendbuff, void *recvbuff, size_t count,
                          flagcxDataType_t datatype, flagcxRedOp_t op,
                          flagcxInnerComm_t comm, flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorReduceScatter(const void *sendbuff,
                                             void *recvbuff, size_t recvcount,
                                             flagcxDataType_t datatype,
                                             flagcxRedOp_t op,
                                             flagcxInnerComm_t comm,
                                             flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorAllGather(const void *sendbuff, void *recvbuff,
                                         size_t sendcount,
                                         flagcxDataType_t datatype,
                                         flagcxInnerComm_t comm,
                                         flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorAlltoAll(const void *sendbuff, void *recvbuff,
                                        size_t count, flagcxDataType_t datatype,
                                        flagcxInnerComm_t comm,
                                        flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t
bootstrapAdaptorAlltoAllv(const void *sendbuff, size_t *sendcounts,
                          size_t *sdispls, void *recvbuff, size_t *recvcounts,
                          size_t *rdispls, flagcxDataType_t datatype,
                          flagcxInnerComm_t comm, flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorSend(const void *sendbuff, size_t count,
                                    flagcxDataType_t datatype, int peer,
                                    flagcxInnerComm_t comm,
                                    flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

// TODO: unsupported
flagcxResult_t bootstrapAdaptorRecv(void *recvbuff, size_t count,
                                    flagcxDataType_t datatype, int peer,
                                    flagcxInnerComm_t comm,
                                    flagcxStream_t /*stream*/) {
  return flagcxNotSupported;
}

flagcxResult_t bootstrapAdaptorGroupStart() {
  // We don't need to do anything here
  return flagcxSuccess;
}

flagcxResult_t bootstrapAdaptorGroupEnd() {
  // We don't need to do anything here
  return flagcxSuccess;
}

struct flagcxCCLAdaptor bootstrapAdaptor = {
    "BOOTSTRAP",
    // Basic functions
    bootstrapAdaptorGetVersion, bootstrapAdaptorGetUniqueId,
    bootstrapAdaptorGetErrorString, bootstrapAdaptorGetLastError,
    // Communicator functions
    bootstrapAdaptorCommInitRank, bootstrapAdaptorCommFinalize,
    bootstrapAdaptorCommDestroy, bootstrapAdaptorCommAbort,
    bootstrapAdaptorCommResume, bootstrapAdaptorCommSuspend,
    bootstrapAdaptorCommCount, bootstrapAdaptorCommCuDevice,
    bootstrapAdaptorCommUserRank, bootstrapAdaptorCommGetAsyncError,
    // Communication functions
    bootstrapAdaptorReduce, bootstrapAdaptorGather, bootstrapAdaptorScatter,
    bootstrapAdaptorBroadcast, bootstrapAdaptorAllReduce,
    bootstrapAdaptorReduceScatter, bootstrapAdaptorAllGather,
    bootstrapAdaptorAlltoAll, bootstrapAdaptorAlltoAllv, bootstrapAdaptorSend,
    bootstrapAdaptorRecv,
    // Group semantics
    bootstrapAdaptorGroupStart, bootstrapAdaptorGroupEnd};

#endif // USE_BOOTSTRAP_ADAPTOR