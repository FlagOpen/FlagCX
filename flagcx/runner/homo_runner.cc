/*************************************************************************
 * Copyright (c) 2025 BAAI. All rights reserved.
 ************************************************************************/

#include "runner.h"

flagcxResult_t homoRunnerReduce(const void *sendbuff, void *recvbuff,
                                size_t count, flagcxDataType_t datatype,
                                flagcxRedOp_t op, int root,
                                flagcxInnerComm_t comm, flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->reduce(
      sendbuff, recvbuff, count, datatype, op, root, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerGather(const void *sendbuff, void *recvbuff,
                                size_t count, flagcxDataType_t datatype,
                                int root, flagcxInnerComm_t comm,
                                flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->gather(
      sendbuff, recvbuff, count, datatype, root, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerScatter(const void *sendbuff, void *recvbuff,
                                 size_t count, flagcxDataType_t datatype,
                                 int root, flagcxInnerComm_t comm,
                                 flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->scatter(
      sendbuff, recvbuff, count, datatype, root, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerBroadcast(const void *sendbuff, void *recvbuff,
                                   size_t count, flagcxDataType_t datatype,
                                   int root, flagcxInnerComm_t comm,
                                   flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->broadcast(
      sendbuff, recvbuff, count, datatype, root, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerAllReduce(const void *sendbuff, void *recvbuff,
                                   size_t count, flagcxDataType_t datatype,
                                   flagcxRedOp_t op, flagcxInnerComm_t comm,
                                   flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->allReduce(
      sendbuff, recvbuff, count, datatype, op, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerReduceScatter(const void *sendbuff, void *recvbuff,
                                       size_t recvcount,
                                       flagcxDataType_t datatype,
                                       flagcxRedOp_t op, flagcxInnerComm_t comm,
                                       flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->reduceScatter(
      sendbuff, recvbuff, recvcount, datatype, op, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerAllGather(const void *sendbuff, void *recvbuff,
                                   size_t sendcount, flagcxDataType_t datatype,
                                   flagcxInnerComm_t comm,
                                   flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->allGather(
      sendbuff, recvbuff, sendcount, datatype, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerAlltoAll(const void *sendbuff, void *recvbuff,
                                  size_t count, flagcxDataType_t datatype,
                                  flagcxInnerComm_t comm,
                                  flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->alltoAll(
      sendbuff, recvbuff, count, datatype, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerAlltoAllv(const void *sendbuff, size_t *sendcounts,
                                   size_t *sdispls, void *recvbuff,
                                   size_t *recvcounts, size_t *rdispls,
                                   flagcxDataType_t datatype,
                                   flagcxInnerComm_t comm,
                                   flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->alltoAllv(
      sendbuff, sendcounts, sdispls, recvbuff, recvcounts, rdispls, datatype,
      comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerSend(const void *sendbuff, size_t count,
                              flagcxDataType_t datatype, int peer,
                              flagcxInnerComm_t comm, flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->send(
      sendbuff, count, datatype, peer, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerRecv(void *recvbuff, size_t count,
                              flagcxDataType_t datatype, int peer,
                              flagcxInnerComm_t comm, flagcxStream_t stream) {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->recv(
      recvbuff, count, datatype, peer, comm, stream));
  return flagcxSuccess;
}

flagcxResult_t homoRunnerGroupStart() {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->groupStart());
  return flagcxSuccess;
}

flagcxResult_t homoRunnerGroupEnd() {
  FLAGCXCHECK(cclAdaptors[flagcxCCLAdaptorDevice]->groupEnd());
  return flagcxSuccess;
}

struct flagcxRunner homoRunner = {
    // Communication functions
    homoRunnerReduce, homoRunnerGather, homoRunnerScatter, homoRunnerBroadcast,
    homoRunnerAllReduce, homoRunnerReduceScatter, homoRunnerAllGather,
    homoRunnerAlltoAll, homoRunnerAlltoAllv, homoRunnerSend, homoRunnerRecv,
    // Group semantics
    homoRunnerGroupStart, homoRunnerGroupEnd};