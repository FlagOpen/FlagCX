#ifndef FLAGCX_KERNEL_H_
#define FLAGCX_KERNEL_H_

#include "adaptor.h"
#include "flagcx.h"

#define FLAGCX_KERNEL_FIFO_CAPACITY 16

typedef enum {
  flagcxDevicePrimSend = 0,
  flagcxDevicePrimRecv = 1,
  flagcxDevicePrimTerm = 2,
  flagcxDevicePrimWait = 3
} flagcxDevicePrim;

constexpr unsigned int flagcxDeviceTriggerBitsAddr = 64;
// constexpr unsigned int flagcxDeviceTriggerBitsOffset = 32;
constexpr unsigned int flagcxDeviceTriggerBitsCount = 32;
constexpr unsigned int flagcxDeviceTriggerBitsPeerRank = 20;
constexpr unsigned int flagcxDeviceTriggerBitsDatatype = 4;
constexpr unsigned int flagcxDeviceTriggerBitsPrim = 4;
constexpr unsigned int flagcxDeviceTriggerBitsFifoReserved = 1;

constexpr unsigned int flagcxReduceTriggerBitsAddr = 64;
constexpr unsigned int flagcxReduceTriggerBitsCount = 32;
constexpr unsigned int flagcxReduceTriggerBitsNThreads = 16;
constexpr unsigned int flagcxReduceTriggerBitsDatatype = 4;
constexpr unsigned int flagcxReduceTriggerBitsRedop = 4;
constexpr unsigned int flagcxReduceTriggerBitsFifoReserved = 1;

// typedef union alignas(16) {
//   struct {
//     uint64_t fst;
//     uint64_t snd;
//   } value;
//   // The summation of number of bits must be 128 or less.
//   struct {
//     // First 64 bits: value[0]
//     uint64_t addr : flagcxDeviceTriggerBitsAddr;
//     // uint64_t offset : flagcxDeviceTriggerBitsOffset;
//     // Second 64 bits: value[1]
//     uint64_t count : flagcxDeviceTriggerBitsCount;
//     uint64_t peerRank : flagcxDeviceTriggerBitsPeerRank;
//     uint64_t datatype : flagcxDeviceTriggerBitsDatatype;
//     uint64_t type : flagcxDeviceTriggerBitsPrim;
//     uint64_t
//         : (64 - flagcxDeviceTriggerBitsCount -
//         flagcxDeviceTriggerBitsPeerRank -
//            flagcxDeviceTriggerBitsDatatype - flagcxDeviceTriggerBitsPrim -
//            flagcxDeviceTriggerBitsFifoReserved); // ensure 64-bit alignment
//     uint64_t reserved : flagcxDeviceTriggerBitsFifoReserved;
//   } fields;
// } flagcxDeviceTrigger;
typedef struct {
  uint64_t addr;
  uint64_t count;
  uint64_t peerRank;
  uint64_t datatype;
  uint64_t type;
} flagcxDeviceTrigger;
typedef flagcxDeviceTrigger *flagcxDeviceTrigger_t;

typedef union alignas(16) {
  uint64_t value[4];
  struct {
    // First 64 bits: value[0]
    uint64_t addr1 : flagcxReduceTriggerBitsAddr;
    // Second 64 bits: value[1]
    uint64_t addr2 : flagcxReduceTriggerBitsAddr;
    // Third 64 bits: value[2]
    uint64_t addr3 : flagcxReduceTriggerBitsAddr;
    // Last 64 bits: value[3]
    uint64_t count : flagcxReduceTriggerBitsCount;
    uint64_t nthreads : flagcxReduceTriggerBitsNThreads;
    uint64_t datatype : flagcxReduceTriggerBitsDatatype;
    uint64_t redop : flagcxReduceTriggerBitsRedop;
    uint64_t
        : (64 - flagcxReduceTriggerBitsCount - flagcxReduceTriggerBitsNThreads -
           flagcxReduceTriggerBitsDatatype - flagcxReduceTriggerBitsRedop -
           flagcxReduceTriggerBitsFifoReserved);
    uint64_t reserved : flagcxReduceTriggerBitsFifoReserved;
  } fields;
} flagcxReduceTrigger;
typedef flagcxReduceTrigger *flagcxReduceTrigger_t;

struct flagcxFifo {
  // [capacity, consumed, produced, trigger buffer]
  uint64_t *buffer;

public:
  flagcxFifo() {
    // TODO: use a better way to initialize FIFO
    deviceAdaptor->deviceMalloc((void **)&buffer,
                                3 * sizeof(uint64_t) +
                                    FLAGCX_KERNEL_FIFO_CAPACITY *
                                        sizeof(flagcxDeviceTrigger),
                                flagcxMemHost, NULL);
    buffer[0] = FLAGCX_KERNEL_FIFO_CAPACITY;
    buffer[1] = 0;
    buffer[2] = 0;
    memset((void *)(buffer + 3), 0,
           FLAGCX_KERNEL_FIFO_CAPACITY * sizeof(flagcxDeviceTrigger));
  }
  ~flagcxFifo() {
    deviceAdaptor->deviceFree((void *)buffer, flagcxMemHost, NULL);
  }
};
typedef struct flagcxFifo *flagcxFifo_t;

FLAGCX_HOST_DECORATOR flagcxResult_t dequeue(void *fifoBuffer,
                                             flagcxDeviceTrigger_t trigger);
#ifdef COMPILE_KERNEL
// device-producer + host-consumer APIs
FLAGCX_DEVICE_DECORATOR flagcxResult_t enqueue(void *fifoBuffer, uint64_t addr,
                                               uint64_t count,
                                               uint64_t peerRank,
                                               uint64_t datatype,
                                               uint64_t type);
// host-producer + device-consumer APIs
// FLAGCX_HOST_DECORATOR flagcxResult_t enqueue(flagcxReduceTrigger trigger);
// FLAGCX_HOST_DECORATOR  flagcxResult_t dequeue(flagcxReduceTrigger_t trigger);

FLAGCX_DEVICE_DECORATOR size_t
getFlagcxDataTypeSizeDevice(flagcxDataType_t dtype);

FLAGCX_DEVICE_DECORATOR flagcxResult_t
flagcxDeviceSend(const void *sendbuff, size_t count, flagcxDataType_t datatype,
                 int peer, void *fifoBuffer);
FLAGCX_DEVICE_DECORATOR flagcxResult_t
flagcxDeviceRecv(void *sendbuff, size_t count, flagcxDataType_t datatype,
                 int peer, void *fifoBuffer);
FLAGCX_DEVICE_DECORATOR flagcxResult_t flagcxDeviceTerm(void *fifoBuffer);
FLAGCX_DEVICE_DECORATOR flagcxResult_t flagcxDeviceWait(void *fifoBuffer);
// FLAGCX_GLOBAL_DECORATOR void flagcxCollectiveKernel(flagcxFifo_t q); // TBD
void flagcxP2pDemo(const void *sendbuff, void *recvbuff, size_t count,
                   flagcxDataType_t datatype, int sendPeer, int recvPeer,
                   flagcxComm_t comm, flagcxStream_t stream);
#endif // COMPILE_KERNEL
#endif
