#include "ascend_adaptor.h"

#ifdef USE_ASCEND_ADAPTOR

const char *hcclAdaptorGetErrorString(flagcxResult_t result) {
  return hcclGetErrorString((hcclResult_t)result);
}

flagcxResult_t hcclAdaptorCommDestroy(flagcxInnerComm_t comm) {
  return (flagcxResult_t)HcclCommDestroy(comm->base);
}

flagcxResult_t hcclAdaptorCommGetAsyncError(flagcxInnerComm_t comm,
                                              flagcxResult_t asyncError) {
  return (flagcxResult_t)HcclGetCommAsyncError(comm->base,
                                               (hcclResult_t *)&asyncError);
}

flagcxResult_t hcclAdaptorReduce(const void *sendbuff, void *recvbuff,
                                   size_t count, flagcxDataType_t datatype,
                                   flagcxRedOp_t op, int root,
                                   flagcxInnerComm_t comm,
                                   flagcxStream_t stream) {
  return (flagcxResult_t)HcclReduce(sendbuff, recvbuff, count,
                                    (hcclDataType_t)datatype, (hcclRedOp_t)op,
                                    root, comm->base, stream->base);
}

flagcxResult_t hcclAdaptorScatter(const void *sendbuff, void *recvbuff,
                                    size_t count, flagcxDataType_t datatype,
                                    int root, flagcxInnerComm_t comm,
                                    flagcxStream_t stream) {
  return (flagcxResult_t)HcclScatter(sendbuff, recvbuff, count,
		                     (hcclDataType_t)datatype,
				     root, comm->base, stream->base);
}

flagcxResult_t hcclAdaptorBroadcast(const void *sendbuff, void *recvbuff,
                                      size_t count, flagcxDataType_t datatype,
                                      int root, flagcxInnerComm_t comm,
                                      flagcxStream_t stream) {
  return (flagcxResult_t)HcclBroadcast(sendbuff, count,
                                       (hcclDataType_t)datatype, root,
                                       comm->base, stream->base);
}

flagcxResult_t hcclAdaptorAllReduce(const void *sendbuff, void *recvbuff,
                                      size_t count, flagcxDataType_t datatype,
                                      flagcxRedOp_t op, flagcxInnerComm_t comm,
                                      flagcxStream_t stream) {
  return (flagcxResult_t)HcclAllReduce(
      sendbuff, recvbuff, count, (hcclDataType_t)datatype, (hcclRedOp_t)op,
      comm->base, stream->base);
}

flagcxResult_t hcclAdaptorReduceScatter(const void *sendbuff, void *recvbuff,
                                          size_t recvcount,
                                          flagcxDataType_t datatype,
                                          flagcxRedOp_t op,
                                          flagcxInnerComm_t comm,
                                          flagcxStream_t stream) {
  return (flagcxResult_t)HcclReduceScatter(
      sendbuff, recvbuff, recvcount, (hcclDataType_t)datatype, (mcclRedOp_t)op,
      comm->base, stream->base);
}

flagcxResult_t hcclAdaptorAllGather(const void *sendbuff, void *recvbuff,
                                      size_t sendcount,
                                      flagcxDataType_t datatype,
                                      flagcxInnerComm_t comm,
                                      flagcxStream_t stream) {
  return (flagcxResult_t)HcclAllGather(sendbuff, recvbuff, sendcount,
                                       (hcclDataType_t)datatype, comm->base,
                                       stream->base);
}

flagcxResult_t hcclAdaptorAlltoAll(const void *sendbuff, void *recvbuff,
                                     size_t count, flagcxDataType_t datatype,
                                     flagcxInnerComm_t comm,
                                     flagcxStream_t stream) {
  return (flagcxResult_t)HcclAlltoAll(sendbuff, count, (hcclDataType_t)datatype,
		                      recvbuff, count, (hcclDataType_t)datatype,
				      comm->base, stream->base);
}

flagcxResult_t hcclAdaptorAlltoAllv(const void *sendbuff, size_t *sendcounts,
                                      size_t *sdispls, void *recvbuff,
                                      size_t *recvcounts, size_t *rdispls,
                                      flagcxDataType_t datatype,
                                      flagcxInnerComm_t comm,
                                      flagcxStream_t stream) {
  return (flagcxResult_t)HcclAlltoAllv(sendbuff, sendcounts, sdispls,
		                       (hcclDataType_t)datatype, recvbuff,
				       recvcounts, rdispls, (hcclDataType_t)datatype,
				       comm->base, stream->base);
}

flagcxResult_t hcclAdaptorSend(const void *sendbuff, size_t count,
                                 flagcxDataType_t datatype, int peer,
                                 flagcxInnerComm_t comm,
                                 flagcxStream_t stream) {
  return (flagcxResult_t)HcclSend(sendbuff, count, (mcclDataType_t)datatype,
                                  peer, comm->base, stream->base);
}

flagcxResult_t hcclAdaptorRecv(void *recvbuff, size_t count,
                                 flagcxDataType_t datatype, int peer,
                                 flagcxInnerComm_t comm,
                                 flagcxStream_t stream) {
  return (flagcxResult_t)HcclRecv(recvbuff, count, (mcclDataType_t)datatype,
                                  peer, comm->base, stream->base);
}

struct flagcxCCLAdaptor hcclAdaptor = {
    "HCCL",
    // Basic functions
    hcclAdaptorGetErrorString,
    // Communicator functions
    hcclAdaptorCommDestroy, hcclAdaptorCommGetAsyncError,
    // Communication functions
    hcclAdaptorReduce, hcclAdaptorScatter,
    hcclAdaptorBroadcast, hcclAdaptorAllReduce, hcclAdaptorReduceScatter,
    hcclAdaptorAllGather, hcclAdaptorAlltoAll, hcclAdaptorAlltoAllv,
    hcclAdaptorSend, hcclAdaptorRecv};

#endif // USE_ASCEND_ADAPTOR
