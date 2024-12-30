#ifndef FLAGCX_GLOBAL_COMM_H_
#define FLAGCX_GLOBAL_COMM_H_

#include "flagcx.h"
#include "bootstrap.h"

/* Opaque handle to flagcxHomoComm */
typedef struct flagcxHomoComm* flagcxHomoComm_t;

/* Opaque handle to flagcxHeteroComm */
typedef struct flagcxHeteroComm* flagcxHeteroComm_t;

typedef enum {
    flagcxCommunicatorUnknown = 0,
    flagcxCommunicatorHomo = 1, // Homogeneous Communicator
    flagcxCommunicatorHybrid = 2 // Hybrid Communicator
} flagcxCommunicatorType_t;

struct flagcxComm {
    int rank;
    int nranks;
    int homo_root_rank;
    int homo_ranks;
    // we do not support for intermediate transfer (src rank -> intra-cluster proxy rank -> net) for now
    int hetero_rank;
    int hetero_root_rank;
    int hetero_ranks;
    flagcxCommunicatorType_t comm_type;
    uint64_t magic;
    volatile uint32_t* abortFlag;
    int *rank_to_homorank;
    int *cluster_ids;
    // int *rank_to_heterorank;
    bootstrapState* bootstrap;
    flagcxHomoComm_t homo_comm;
    flagcxHeteroComm_t hetero_comm;
};

#endif // end include guard