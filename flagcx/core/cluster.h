#ifndef FLAGCX_CLUSTER_H_
#define FLAGCX_CLUSTER_H_

#include "flagcx.h"
#include "adaptor.h"
#include "param.h"
#include <map>
#include <string>

flagcxResult_t flagcxCollectClusterInfos(const flagcxVendor* allData,
                                         flagcxCommunicatorType_t &type,
                                         int *homo_rank, int *homo_root_rank, int *homo_ranks,
                                         int *hetero_rank, int *hetero_root_rank, int *hetero_ranks,
                                         int *cluster_id, int rank, int nranks);

#endif // end include guard