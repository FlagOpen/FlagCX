#ifndef FLAGCX_COST_MODEL_H
#define FLAGCX_COST_MODEL_H

#include "c2c_algo.h"
#include "flagcx.h"
#include <vector>

// typedef enum {
//     FullyConnected,
//     Ring,
// } flagcxHomoSimTopo;

// struct flagcxHomoSimInfo {
//     flagcxHomoSimTopo topo[2];
//     int npusCount[2];
//     float bandwidth[2];
//     float latency[2];
// };

constexpr int FLAGCX_INTRA_LAT_IDX = 0;
constexpr int FLAGCX_INTER_LAT_IDX = 1;

#define FLAGCX_VENDOR_NUM 4

class flagcxAlgoTimeEstimator {
public:
  flagcxAlgoTimeEstimator(flagcxDataType_t dtype, flagcxC2cPlanner &planner);
  ~flagcxAlgoTimeEstimator();

  flagcxResult_t getAlgoTime(float *time);

private:
  flagcxResult_t collectPlannerInfo();
  flagcxResult_t getPreHomoAlgoTime(float *time);
  flagcxResult_t getP2pTime(int rank, flagcxC2cHeteroFuncInfo &heteroFunc,
                            float *time);

  flagcxResult_t getPostHomoAlgoTime(float *time);

  flagcxResult_t getHomoAlgoTime(flagcxC2cHomoFuncInfo &homoFunc, int rank,
                                 int rankSize, flagcxVendorType vendor,
                                 float *time);

  flagcxResult_t getHeteroAlgoTime(float *time);

  flagcxResult_t getHomoInterAlgoTime(int rank, int loop, float *time);

  float getRefreshTime();

  float getSendRecvTime(float curClusterLat, float remoteClusterLat, float bw,
                        int totalCount, size_t chunkSize);

  // flagcxC2cPlanner &planner_;
  flagcxDataType_t datatype_;
  flagcxC2cPlanner &planner_;
  flagcxComm_t comm_;
  bool plannerInfoReady_{false};
  flagcxC2cPlannerInfo *plannerInfoData_{nullptr};
  static std::map<
      flagcxVendorType,
      std::map<flagcxCommOp_t, std::map<int, std::map<size_t, float>>>>
      homoTimeMap;
};

#endif