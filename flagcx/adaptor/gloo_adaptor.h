#ifdef USE_GLOO_ADAPTOR

#define MAX_GLOO_STORE_SIZE 16

#include "comm.h"
#include "utils.h"
#include "alloc.h"
#include "check.h"
#include "flagcx.h"
#include "adaptor.h"

#include <gloo/context.h>
#include <gloo/algorithm.h>
// #include <gloo/common/error.h>
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
#include <memory>
#include <string>
#include <cstring>

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
};

struct flagcxHomoComm
{
    std::shared_ptr<flagcxGlooContext> base;
    // std::shared_ptr<::gloo::rendezvous::Context> base;
    // std::shared_ptr<::gloo::rendezvous::Store> store;
    // std::shared_ptr<::gloo::transport::Device> device;
};

// struct KVData
// {
//     char key[MAX_GLOO_STORE_SIZE];
//     char value[MAX_GLOO_STORE_SIZE];
// };

// class flagcxGlooStore : public ::gloo::rendezvous::Store
// {
// public:
//     flagcxGlooStore(int rank, int nranks, bootstrapState *bootstrap)
//     {
//         rank_ = rank;
//         nranks_ = nranks;
//         bootstrap_ = bootstrap;
//     }

//     void set(const std::string &key, const std::vector<char> &value) override
//     {
//         std::string tmp(value.begin(), value.end());
//         kv_map_[key] = tmp;
//         key_ = key;
//         value_ = tmp;
//         printf("rank = %d, set [%s, %s]\n", rank_, key.c_str(), tmp.c_str());
//     }

//     std::vector<char> get(const std::string &key) override
//     {
//         auto value = kv_map_[key];
//         printf("rank = %d, get [%s, %s]\n", rank_, key.c_str(), value.c_str());
//         return std::vector<char>(value.begin(), value.end());
//     }

//     void wait(const std::vector<std::string> &keys) override
//     {
//         bootstrapBarrier(bootstrap_, rank_, nranks_, 0);
//         printf("rank = %d, wait after bootstrap barrier\n", rank_);
//         for (auto key : keys)
//         {
//             if (kv_map_.find(key) == kv_map_.end())
//             {
//                 KVData *kvData;
//                 KVData myData;
//                 flagcxCalloc(&kvData, nranks_);
//                 std::strcpy(myData.key, key_.c_str());
//                 std::strcpy(myData.value, value_.c_str());
//                 memcpy(kvData + rank_, &myData, sizeof(KVData));
//                 printf("rank = %d, wait before bootstrap allgather\n", rank_);
//                 bootstrapAllGather(bootstrap_, (void *)kvData, sizeof(KVData));
//                 printf("rank = %d, wait after bootstrap allgather\n", rank_);
//                 bootstrapBarrier(bootstrap_, rank_, nranks_, 0);
//                 for (int i = 0; i < nranks_; i++)
//                 {
//                     KVData *data = kvData + i;
//                     printf("rank = %d, wait [%s, %s]\n", rank_, (data->key), (data->value));
//                     if (kv_map_.find(data->key) == kv_map_.end())
//                     {
//                         kv_map_[data->key] = data->value;
//                     }
//                 }
//                 free(kvData);
//                 break;
//             }
//         }
//         printf("rank = %d, wait terminated\n", rank_);
//         bootstrapBarrier(bootstrap_, rank_, nranks_, 0);
//     }

//     void wait(
//         const std::vector<std::string> &keys,
//         const std::chrono::milliseconds &timeout) override
//     {
//         // TODO: timeout is not supported
//         wait(keys);
//     }

// public:
//     int rank_;
//     int nranks_;
//     std::string key_;
//     std::string value_;
//     bootstrapState *bootstrap_;
//     std::map<std::string, std::string> kv_map_;
// };

// std::map<flagcxDataType_t, cnclDataType_t> f2c_datatype_map = {
//     {flagcxInt8, cnclInt8},
//     {flagcxHalf, cnclHalf},
//     {flagcxFloat16, cnclFloat16},
//     {flagcxBfloat16, cnclBfloat16},
//     {flagcxFloat32, cnclFloat32},
//     {flagcxFloat, cnclFloat},
// };

// std::map<flagcxRedOp_t, cnclReduceOp_t> f2c_reduceop_map = {
//     {flagcxSum, cnclSum},
//     {flagcxProd, cnclProd},
//     {flagcxMax, cnclMax},
//     {flagcxMin, cnclMin}
// };

// //TODO: not match fully
// std::map<cnclResult_t, flagcxResult_t> c2f_ret_map = {
//     {CNCL_RET_SUCCESS, flagcxSuccess},
//     {CNCL_RET_ERR_UNSUPPORTED, flagcxUnhandledDeviceError},
//     {CNCL_RET_ASYNC_ERROR , flagcxRemoteError}
// };

// std::map<flagcxResult_t, cnclResult_t> f2c_ret_map = {
//     {flagcxSuccess, CNCL_RET_SUCCESS},
//     {flagcxUnhandledDeviceError, CNCL_RET_ERR_UNSUPPORTED}
// };

#endif // USE_GLOO_ADAPTOR