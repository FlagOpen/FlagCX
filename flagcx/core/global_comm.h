#ifndef FLAGCX_GLOBAL_COMM_H_
#define FLAGCX_GLOBAL_COMM_H_

#include "flagcx.h"
#include "bootstrap.h"

/* Opaque handle to flagcxHomoComm */
typedef struct flagcxHomoComm* flagcxHomoComm_t;

/* Forward declaration of flagcxHeteroComm*/
struct flagcxHeteroComm;
typedef struct flagcxHeteroComm* flagcxHeteroComm_t;

typedef enum {
flagcxCommunicatorUnknown = 0,
flagcxCommunicatorHomo = 1, // Homogeneous Communicator
flagcxCommunicatorHybrid = 2 // Hybrid Communicator
} flagcxCommunicatorType_t;

struct flagcxComm {
    int rank;
    int nranks;
    uint64_t magic;
    flagcxCommunicatorType_t comm_type;
    volatile uint32_t* abortFlag;
    bootstrapState* bootstrap;
    flagcxHomoComm_t homo_comm;
    flagcxHeteroComm_t hetero_comm;
};

#endif // end include guard
