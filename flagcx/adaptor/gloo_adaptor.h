#ifdef USE_GLOO_ADAPTOR

#include "comm.h"
#include "utils.h"
#include "alloc.h"
#include "check.h"
#include "flagcx.h"
#include "adaptor.h"

#include <gloo/context.h>
#include <gloo/algorithm.h>
#include <gloo/rendezvous/store.h>
#include <gloo/rendezvous/context.h>
#include <gloo/rendezvous/prefix_store.h>
#include <gloo/transport/device.h>
#include <gloo/transport/context.h>
#include <gloo/transport/tcp/device.h>
#include <gloo/allgather.h>
#include <gloo/allgatherv.h>
#include <gloo/allreduce.h>
#include <gloo/alltoall.h>
#include <gloo/alltoallv.h>
#include <gloo/barrier.h>
#include <gloo/broadcast.h>
#include <gloo/gather.h>
#include <gloo/reduce.h>
#include <gloo/scatter.h>

#include <map>
#include <chrono>
#include <vector>
#include <queue>
#include <memory>
#include <string>
#include <cstring>

using buffer_ptr = std::unique_ptr<::gloo::transport::UnboundBuffer>;
static std::queue<buffer_ptr> inputBuffers;
static constexpr std::chrono::milliseconds flagcxGlooDefaultTimeout = std::chrono::seconds(10000);
static bool groupStarted = false;

#define GENERATE_ALL_TYPES(type, func, args...)  \
  switch (type) {                                \
    case flagcxChar:                             \
      func<char>(args);                          \
      break;                                     \
    case flagcxUint8:                            \
      func<uint8_t>(args);                       \
      break;                                     \
    case flagcxInt:                              \
      func<int>(args);                           \
      break;                                     \
    case flagcxUint32:                           \
      func<uint32_t>(args);                      \
      break;                                     \
    case flagcxInt64:                            \
      func<int64_t>(args);                       \
      break;                                     \
    case flagcxUint64:                           \
      func<uint64_t>(args);                      \
      break;                                     \
    case flagcxHalf:                             \
      func<::gloo::float16>(args);               \
      break;                                     \
    case flagcxFloat:                            \
      func<float>(args);                         \
      break;                                     \
    case flagcxDouble:                           \
      func<double>(args);                        \
      break;                                     \
    case flagcxBfloat16:                         \
      printf("Invalid data type");               \
      break;                                     \
    default:                                     \
      printf("Invalid data type");               \
      break;                                     \
  }

typedef void (*flagcxGlooReduceFunc)(void*, const void*, const void*, size_t);
template <typename T>
flagcxGlooReduceFunc getGlooReduceFunc(flagcxRedOp_t op)
{
    switch (op)
    {
    case flagcxSum:
        return flagcxGlooReduceFunc(&::gloo::sum<T>);
    case flagcxProd:
        return flagcxGlooReduceFunc(&::gloo::product<T>);
    case flagcxMax:
        return flagcxGlooReduceFunc(&::gloo::max<T>);
    case flagcxMin:
        return flagcxGlooReduceFunc(&::gloo::min<T>);
    case flagcxAvg:
        printf("Gloo backend does not support flagcxAvg Redop\n");
        return nullptr;
    default:
        return nullptr;
    }
}

template <typename T, typename F>
void getFunction(F& fn, flagcxRedOp_t op) {
    fn = getGlooReduceFunc<T>(op);
}

template <typename F>
F getFunction(flagcxDataType_t dtype, flagcxRedOp_t op) 
{
  F fn;
  GENERATE_ALL_TYPES(dtype, getFunction, fn, op);
  return fn;
}

template <typename T, typename O>
void setInput(O& opts, void *ptr, size_t count) {
  opts.setInput(static_cast<T*>(ptr), count);
}

template <typename T, typename O>
void setOutput(O& opts,  void *ptr, size_t count) {
  opts.setOutput(static_cast<T*>(ptr), count);
}

struct MaxLengthData
{
    unsigned long maxLength;
};

class flagcxGlooContext : public ::gloo::Context
{
public:
    flagcxGlooContext(int rank, int nranks, bootstrapState *bootstrap)
        : ::gloo::Context(rank, nranks)
    {
        bootstrap_ = bootstrap;
    }

    ~flagcxGlooContext() {}

    void connectFullMesh(std::shared_ptr<::gloo::transport::Device> &dev)
    {
        unsigned long maxLength = 0;
        std::vector<std::vector<char>> addresses(size);
        auto transportContext = dev->createContext(rank, size);
        // transportContext->setTimeout(getTimeout());

        for (int i = 0; i < size; i++)
        {
            if (i == rank)
                continue;

            auto &pair = transportContext->createPair(i);

            // Store address for pair for this rank
            auto address = pair->address().bytes();
            maxLength = std::max(maxLength, address.size());
            addresses[i] = std::move(address);
        }

        // bootstrap allgather to get max length
        MaxLengthData *maxLengthData;
        flagcxCalloc(&maxLengthData, size);
        maxLengthData[rank].maxLength = maxLength;
        bootstrapAllGather(bootstrap_, (void *)maxLengthData, sizeof(MaxLengthData));
        bootstrapBarrier(bootstrap_, rank, size, 0);
        for (int i = 0; i < size; ++i)
        {
            maxLength = std::max(maxLength, maxLengthData[i].maxLength);
        }

        // Prepare input and output
        std::vector<char> addressData(size * size * maxLength);
        for (int i = 0; i < size; ++i)
        {
            if (i == rank)
            {
                continue;
            }

            auto offset = (rank * size + i) * maxLength;
            auto &address = addresses[i];
            memcpy(addressData.data() + offset, address.data(), address.size());
        }

        // bootstrap allgather to get all addresses
        bootstrapAllGather(bootstrap_, (void *)addressData.data(), size * maxLength);
        bootstrapBarrier(bootstrap_, rank, size, 0);

        // Connect every pair
        for (int i = 0; i < size; ++i)
        {
            if (i == rank)
            {
                continue;
            }

            auto offset = (rank + i * size) * maxLength;
            std::vector<char> address(maxLength);
            memcpy(address.data(), addressData.data() + offset, maxLength);
            transportContext->getPair(i)->connect(address);
        }

        // printf("rank = %d, wait before bootstrap allgather\n", rank_);
        device_ = dev;
        transportContext_ = std::move(transportContext);
    }
    
public:
    bootstrapState *bootstrap_;
    const uint64_t tag = 0x1337;
};

struct flagcxHomoComm
{
    std::shared_ptr<flagcxGlooContext> base;
    // std::shared_ptr<::gloo::rendezvous::Context> base;
    // std::shared_ptr<::gloo::rendezvous::Store> store;
    // std::shared_ptr<::gloo::transport::Device> device;
};

#endif // USE_GLOO_ADAPTOR