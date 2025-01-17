#pragma once

#include "flagcx.h"

#ifdef USE_NVIDIA_ADAPTOR
#include <cuda_runtime.h>
#include <ATen/cuda/CUDAEvent.h>
#elif USE_ILUVATAR_COREX_ADAPTOR
#include <cuda_runtime.h>
#include <ATen/cuda/CUDAEvent.h>
#elif USE_CAMBRICON_ADAPTOR
#include "framework/core/MLUEvent.h"
#include "framework/core/MLUStream.h"
#endif

namespace c10d
{
    class EventFlagcx
    {
    public:
        virtual ~EventFlagcx() = default;

        virtual void record(const int device_id) = 0;
        virtual void record(const flagcxStream_t &stream, const int device_id) = 0;

        virtual void block(const int device_id) = 0;
        virtual void block(const flagcxStream_t &stream, const int device_id) = 0;
    };

#ifdef USE_NVIDIA_ADAPTOR
    class CUDAEventFlagcx : public EventFlagcx
    {
    public:
        CUDAEventFlagcx() {
            cuda_event = at::cuda::CUDAEvent(cudaEventDisableTiming);
        }

        void record(const int device_id) override
        {
            cuda_event.record(at::cuda::getCurrentCUDAStream(device_id));
        }

        void record(const flagcxStream_t &stream, const int device_id) override
        {
            cuda_event.record(at::cuda::getStreamFromExternal(*(cudaStream_t *)stream, device_id));
        }

        void block(const int device_id) override
        {
            cuda_event.block(at::cuda::getCurrentCUDAStream(device_id));
        }

        void block(const flagcxStream_t &stream, const int device_id) override
        {
            cuda_event.block(at::cuda::getStreamFromExternal(*(cudaStream_t *)stream, device_id));
        }
    private:
        at::cuda::CUDAEvent cuda_event;
    };
#elif USE_ILUVATAR_COREX_ADAPTOR
    class IXCUDAEventFlagcx : public EventFlagcx
    {
    public:
        IXCUDAEventFlagcx() {
            ixcuda_event = at::cuda::CUDAEvent(cudaEventDisableTiming);
        }

        void record(const int device_id) override
        {
            ixcuda_event.record(at::cuda::getCurrentCUDAStream(device_id));
        }

        void record(const flagcxStream_t &stream, const int device_id) override
        {
            ixcuda_event.record(at::cuda::getStreamFromExternal(*(cudaStream_t *)stream, device_id));
        }

        void block(const int device_id) override
        {
            ixcuda_event.block(at::cuda::getCurrentCUDAStream(device_id));
        }

        void block(const flagcxStream_t &stream, const int device_id) override
        {
            ixcuda_event.block(at::cuda::getStreamFromExternal(*(cudaStream_t *)stream, device_id));
        }
    private:
        at::cuda::CUDAEvent ixcuda_event;
    };
#elif USE_CAMBRICON_ADAPTOR
    class MLUEventFlagcx : public EventFlagcx
    {
    public:
        MLUEventFlagcx() {
            mlu_event = torch_mlu::MLUEvent();
        }

        void record(const int device_id) override
        {
            mlu_event.place(torch_mlu::getCurrentMLUStream(device_id));
        }

        void record(const flagcxStream_t &stream, const int device_id) override
        {
            mlu_event.place(torch_mlu::getStreamFromExternal(*(cnrtQueue_t *)stream, device_id));
        }

        void block(const int device_id) override
        {
            mlu_event.wait(torch_mlu::getCurrentMLUStream(device_id));
        }

        void block(const flagcxStream_t &stream, const int device_id) override
        {
            mlu_event.wait(torch_mlu::getStreamFromExternal(*(cnrtQueue_t *)stream, device_id));
        }
    private:
        torch_mlu::MLUEvent mlu_event;
    };
#endif

} // namespace c10d