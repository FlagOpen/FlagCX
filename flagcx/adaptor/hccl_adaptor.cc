#include "ascend_adaptor.h"

#ifdef USE_ASCEND_ADAPTOR

#include <map>

std::map<flagcxDataType_t, HcclDataType> f2c_datatype_map = {
    {flagcxInt8, HCCL_DATA_TYPE_INT8},    {flagcxUint8, HCCL_DATA_TYPE_UINT8},
    {flagcxInt, HCCL_DATA_TYPE_INT32},    {flagcxInt32, HCCL_DATA_TYPE_INT32},
    {flagcxInt64, HCCL_DATA_TYPE_INT64},  {flagcxHalf, HCCL_DATA_TYPE_FP16},
    {flagcxFloat16, HCCL_DATA_TYPE_FP16}, {flagcxBfloat16, HCCL_DATA_TYPE_BFP16},
    {flagcxFloat32, HCCL_DATA_TYPE_FP32}, {flagcxFloat, HCCL_DATA_TYPE_FP32},
    {flagcxDouble, HCCL_DATA_TYPE_FP64},
};

std::map<flagcxRedOp_t, HcclReduceOp> f2c_reduceop_map = {
    {flagcxSum, HCCL_REDUCE_SUM},
    {flagcxProd, HCCL_REDUCE_PROD},
    {flagcxMax, HCCL_REDUCE_MAX},
    {flagcxMin, HCCL_REDUCE_MIN}};

// TODO: unsupported
flagcxResult_t hcclAdaptorGetVersion(int *version) {
  return flagcxUnhandledDeviceError;
}

// TODO: unsupported ???
flagcxResult_t hcclAdaptorGetUniqueId(flagcxUniqueId_t *uniqueId) {
  return flagcxUnhandledDeviceError;
}

const char *hcclAdaptorGetErrorString(flagcxResult_t result) {
    return HcclGetErrorString((HcclResult)result);
}

// TODO: unsupported
const char *hcclAdaptorGetLastError(flagcxInnerComm_t comm) {
    return "Not Implemented";
}

/*
ncclResult_t ncclCommInitRank(
    ncclComm_t* comm,      // 输出参数：指向通信器句柄的指针
    int nranks,            // 输入参数：通信组中的总 GPU 数量
    ncclUniqueId commId,   // 输入参数：由 ncclGetUniqueId 生成的唯一标识符
    int rank               // 输入参数：当前 GPU 在通信组中的唯一标识符（从 0 到 nranks-1）
);

flagcxResult_t cnclAdaptorCommInitRank(xxxx)) {
  if (*comm == NULL) {
    flagcxCalloc(comm, 1);
  }
  int dev_id = 0;
  DEVCHECK(cnrtGetDevice(&dev_id));
  return (flagcxResult_t)c2f_ret_map[cnclInitComms(
      &(*comm)->base, 1 , &dev_id , &rank ,
      nranks, (cnclCliqueId *)commId)];
}
*/
flagcxResult_t hcclAdaptorCommInitRank(flagcxInnerComm_t *comm, int nranks,
                                       flagcxUniqueId_t commId, int rank,
                                       bootstrapState * /*bootstrap*/) {
  if (*comm == NULL) {
    flagcxCalloc(comm, 1);
  }
  HcclRootInfo rootInfo;
  int32_t rootRank = 0;
  if(rank == rootRank) {
      HCCLCHECK(HcclGetRootInfo(&rootInfo));
  }
  return (flagcxResult_t)HcclCommInitRootInfo(nranks, &rootInfo, rank, &(*comm)->base);
}

// TODO: unsupported
flagcxResult_t hcclAdaptorCommFinalize(flagcxInnerComm_t comm) {
  return flagcxUnhandledDeviceError;
}

flagcxResult_t hcclAdaptorCommDestroy(flagcxInnerComm_t comm) {
  return (flagcxResult_t)HcclCommDestroy(comm->base);
}

// TODO: unsupported
flagcxResult_t hcclAdaptorCommAbort(flagcxInnerComm_t comm) {
  return flagcxUnhandledDeviceError;
}

// TODO: unsupported
flagcxResult_t hcclAdaptorCommResume(flagcxInnerComm_t comm) {
  return flagcxUnhandledDeviceError;
}

// TODO: unsupported
flagcxResult_t hcclAdaptorCommSuspend(flagcxInnerComm_t comm) {
  return flagcxUnhandledDeviceError;
}

flagcxResult_t hcclAdaptorCommCount(const flagcxInnerComm_t comm, int *count) {
  return (flagcxResult_t)HcclGetRankSize(comm->base, (uint32_t*)count);
}

// TODO: unsupported
flagcxResult_t hcclAdaptorCommCuDevice(const flagcxInnerComm_t comm,
                                       int *device) {
  return flagcxUnhandledDeviceError;
}

flagcxResult_t hcclAdaptorCommUserRank(const flagcxInnerComm_t comm,
                                       int *rank) {
  return (flagcxResult_t)HcclGetRankId(comm->base, (uint32_t*)rank);
}

// TODO: unsupported
flagcxResult_t hcclAdaptorCommGetAsyncError(flagcxInnerComm_t comm,
                                            flagcxResult_t asyncError) {
  return flagcxUnhandledDeviceError;
}

flagcxResult_t hcclAdaptorReduce(const void *sendbuff, void *recvbuff,
                                 size_t count, flagcxDataType_t datatype,
                                 flagcxRedOp_t op, int root,
                                 flagcxInnerComm_t comm,
                                 flagcxStream_t stream) {
  void *sendbuffptr = (void *)sendbuff;
  return (flagcxResult_t)HcclReduce(sendbuffptr, recvbuff, count,
                                       (HcclDataType)f2c_datatype_map[datatype],
                                       (HcclReduceOp)f2c_reduceop_map[op],
                                       root, comm->base, stream->base);
}

// TODO: unsupported
flagcxResult_t hcclAdaptorGather(const void *sendbuff, void *recvbuff,
                                 size_t count, flagcxDataType_t datatype,
                                 int root, flagcxInnerComm_t comm,
                                 flagcxStream_t stream) {
  return flagcxUnhandledDeviceError;
}

flagcxResult_t hcclAdaptorScatter(const void *sendbuff, void *recvbuff,
                                  size_t count, flagcxDataType_t datatype,
                                  int root, flagcxInnerComm_t comm,
                                  flagcxStream_t stream) {
  void *sendbuffptr = (void *)sendbuff;
  return (flagcxResult_t)HcclScatter(sendbuffptr, recvbuff, count,
                                    (HcclDataType)f2c_datatype_map[datatype],
                                    root, comm->base, stream->base);
}

flagcxResult_t hcclAdaptorBroadcast(const void *sendbuff, void *recvbuff,
                                    size_t count, flagcxDataType_t datatype,
                                    int root, flagcxInnerComm_t comm,
                                    flagcxStream_t stream) {
  if (root == 0) {
    void *sendbuffptr = (void *)sendbuff;
    return (flagcxResult_t)HcclBroadcast(sendbuffptr, count,
                                         (HcclDataType)f2c_datatype_map[datatype],
                                          root, comm->base, stream->base);
  }
  else {
    return (flagcxResult_t)HcclBroadcast(recvbuff, count,
                                         (HcclDataType)f2c_datatype_map[datatype],
                                          root, comm->base, stream->base);
  }
  return flagcxUnhandledDeviceError;
}

flagcxResult_t hcclAdaptorAllReduce(const void *sendbuff, void *recvbuff,
                                    size_t count, flagcxDataType_t datatype,
                                    flagcxRedOp_t op, flagcxInnerComm_t comm,
                                    flagcxStream_t stream) {
  void *sendbuffptr = (void *)sendbuff;
  return (flagcxResult_t)HcclAllReduce(sendbuffptr, recvbuff, count,
                                       (HcclDataType)f2c_datatype_map[datatype],
                                       (HcclReduceOp)f2c_reduceop_map[op],
                                       comm->base, stream->base);
}

flagcxResult_t 
hcclAdaptorReduceScatter(const void *sendbuff, void *recvbuff, size_t recvcount,
                         flagcxDataType_t datatype, flagcxRedOp_t op,
                         flagcxInnerComm_t comm, flagcxStream_t stream) {
  void *sendbuffptr = (void *)sendbuff;
  return (flagcxResult_t)HcclReduceScatter(
      sendbuffptr, recvbuff, recvcount, (HcclDataType)f2c_datatype_map[datatype], 
      (HcclReduceOp)f2c_reduceop_map[op], comm->base, stream->base);
}

flagcxResult_t hcclAdaptorAllGather(const void *sendbuff, void *recvbuff,
                                    size_t sendcount, flagcxDataType_t datatype,
                                    flagcxInnerComm_t comm,
                                    flagcxStream_t stream) {
  void *sendbuffptr = (void *)sendbuff;
  return (flagcxResult_t)HcclAllGather(sendbuffptr, recvbuff, sendcount,
                                       (HcclDataType)f2c_datatype_map[datatype], 
                                       comm->base, stream->base);
}

flagcxResult_t hcclAdaptorAlltoAll(const void *sendbuff, void *recvbuff,
                                   size_t count, flagcxDataType_t datatype,
                                   flagcxInnerComm_t comm,
                                   flagcxStream_t stream) {
  // TODO: 这么写好像太简单了
  void *sendbuffptr = (void *)sendbuff;
  return (flagcxResult_t)HcclAlltoAll(sendbuffptr, count, (HcclDataType)f2c_datatype_map[datatype],
                                      recvbuff, count, (HcclDataType)f2c_datatype_map[datatype],
                                      comm->base, stream->base);
}

flagcxResult_t hcclAdaptorAlltoAllv(const void *sendbuff, size_t *sendcounts,
                                    size_t *sdispls, void *recvbuff,
                                    size_t *recvcounts, size_t *rdispls,
                                    flagcxDataType_t datatype,
                                    flagcxInnerComm_t comm,
                                    flagcxStream_t stream) {
  // TODO: 这么写好像太简单了
  void *sendbuffptr = (void *)sendbuff;
  return (flagcxResult_t)HcclAlltoAllV(sendbuffptr, sendcounts, sdispls,
                                      (HcclDataType)f2c_datatype_map[datatype],
                                      recvbuff, recvcounts, rdispls,
                                      (HcclDataType)f2c_datatype_map[datatype],
                                      comm->base, stream->base);
}

flagcxResult_t hcclAdaptorSend(const void *sendbuff, size_t count,
                               flagcxDataType_t datatype, int peer,
                               flagcxInnerComm_t comm, flagcxStream_t stream) {
  void *sendbuffptr = (void *)sendbuff;
  return (flagcxResult_t)HcclSend(sendbuffptr, count, (HcclDataType)f2c_datatype_map[datatype],
                                  (uint32_t)peer, comm->base, stream->base);
}

flagcxResult_t hcclAdaptorRecv(void *recvbuff, size_t count,
                               flagcxDataType_t datatype, int peer,
                               flagcxInnerComm_t comm, flagcxStream_t stream) {
  return (flagcxResult_t)HcclRecv(recvbuff, count, (HcclDataType)f2c_datatype_map[datatype],
                                  peer, comm->base, stream->base);
}

// TODO: unsupported
flagcxResult_t hcclAdaptorGroupStart() {
  return flagcxUnhandledDeviceError;
}

// TODO: unsupported
flagcxResult_t hcclAdaptorGroupEnd() { 
    return flagcxUnhandledDeviceError;
}

struct flagcxCCLAdaptor hcclAdaptor = {
    "HCCL",
    // Basic functions
    hcclAdaptorGetVersion, hcclAdaptorGetUniqueId, hcclAdaptorGetErrorString,
    hcclAdaptorGetLastError,
    // Communicator functions
    hcclAdaptorCommInitRank, hcclAdaptorCommFinalize, hcclAdaptorCommDestroy,
    hcclAdaptorCommAbort, hcclAdaptorCommResume, hcclAdaptorCommSuspend,
    hcclAdaptorCommCount, hcclAdaptorCommCuDevice, hcclAdaptorCommUserRank,
    hcclAdaptorCommGetAsyncError,
    // Communication functions
    hcclAdaptorReduce, hcclAdaptorGather, hcclAdaptorScatter,
    hcclAdaptorBroadcast, hcclAdaptorAllReduce, hcclAdaptorReduceScatter,
    hcclAdaptorAllGather, hcclAdaptorAlltoAll, hcclAdaptorAlltoAllv,
    hcclAdaptorSend, hcclAdaptorRecv,
    // Group semantics
    hcclAdaptorGroupStart, hcclAdaptorGroupEnd};

#endif // USE_ASCEND_ADAPTOR