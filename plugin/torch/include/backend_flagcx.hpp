#pragma once

#include <torch/python.h>

#include <torch/csrc/distributed/c10d/Backend.hpp>
#include <torch/csrc/distributed/c10d/Work.hpp>
#include <torch/csrc/distributed/c10d/Store.hpp>
#include <torch/csrc/distributed/c10d/Types.hpp>
#include <torch/csrc/distributed/c10d/Utils.hpp>

#include <pybind11/chrono.h>

#include <unordered_map>
#include <memory>

#include "event_flagcx.hpp"

namespace c10d
{
    class BackendFlagcx : public Backend
    {
    public:
        explicit BackendFlagcx(
            const c10::intrusive_ptr<::c10d::Store>& store,
            int rank = -1,
            int size = -1);

        ~BackendFlagcx() override;

        c10::intrusive_ptr<Work> broadcast(
            std::vector<at::Tensor>& data,
            const BroadcastOptions& opts = BroadcastOptions()) override;

        c10::intrusive_ptr<Work> allreduce(
            std::vector<at::Tensor>& tensors,
            const AllreduceOptions& opts = AllreduceOptions()) override;

        c10::intrusive_ptr<Work> allreduce_coalesced(
            std::vector<at::Tensor>& tensors,
            const AllreduceCoalescedOptions& opts =
                AllreduceCoalescedOptions()) override;

        c10::intrusive_ptr<Work> reduce(
            std::vector<at::Tensor>& tensors,
            const ReduceOptions& opts = ReduceOptions()) override;

        c10::intrusive_ptr<Work> allgather(
            std::vector<std::vector<at::Tensor>>& outputTensors,
            std::vector<at::Tensor>& inputTensors,
            const AllgatherOptions& opts = AllgatherOptions()) override;

        c10::intrusive_ptr<Work> _allgather_base(
            at::Tensor& outputBuffer,
            at::Tensor& inputBuffer,
            const AllgatherOptions& opts = AllgatherOptions()) override;

        c10::intrusive_ptr<Work> barrier(
            const BarrierOptions& opts = BarrierOptions()) override;

        c10::intrusive_ptr<Work> gather(
            std::vector<std::vector<at::Tensor>>& outputTensors,
            std::vector<at::Tensor>& inputTensors,
            const GatherOptions& opts = GatherOptions()) override;

        c10::intrusive_ptr<Work> scatter(
            std::vector<at::Tensor>& outputTensors,
            std::vector<std::vector<at::Tensor>>& inputTensors,
            const ScatterOptions& opts = ScatterOptions()) override;

        c10::intrusive_ptr<Work> reduce_scatter(
            std::vector<at::Tensor>& outputTensors,
            std::vector<std::vector<at::Tensor>>& inputTensors,
            const ReduceScatterOptions& opts = ReduceScatterOptions()) override;

        c10::intrusive_ptr<Work> alltoall_base(
            at::Tensor& outputTensor,
            at::Tensor& inputTensor,
            std::vector<int64_t>& outputSplitSizes,
            std::vector<int64_t>& inputSplitSizes,
            const AllToAllOptions& opts = AllToAllOptions()) override;

        c10::intrusive_ptr<Work> alltoall(
            std::vector<at::Tensor>& outputTensors,
            std::vector<at::Tensor>& inputTensors,
            const AllToAllOptions& opts = AllToAllOptions()) override;

        c10::intrusive_ptr<Work> send(
            std::vector<at::Tensor>& tensors,
            int dstRank,
            int tag) override;

        c10::intrusive_ptr<Work> recv(
            std::vector<at::Tensor>& tensors,
            int srcRank,
            int tag) override;

        c10::intrusive_ptr<Work> recvAnysource(
            std::vector<at::Tensor>& tensors,
            int tag) override;

        static c10::intrusive_ptr<Backend> createBackendFlagcx(
            const c10::intrusive_ptr<::c10d::Store>& store,
            int rank,
            int size,
            const std::chrono::duration<float>& timeout);

        static void BackendFlagcxConstructor() __attribute__((constructor))
        {
        std::string dev_name="cuda";
#ifdef USE_NVIDIA_ADAPTOR
        dev_name = "cuda";
#elif USE_ILUVATAR_COREX_ADAPTOR
        dev_name = "cuda";
#elif USE_CAMBRICON_ADAPTOR
        dev_name = "mlu";
#endif
            py::object module = py::module::import("torch.distributed");
            py::object register_backend =
                module.attr("Backend").attr("register_backend");
            register_backend("flagcx", py::cpp_function(createBackendFlagcx), py::arg("devices") = py::make_tuple(dev_name));
        }

    protected:
        void initComm(at::Device dev);
        void syncStream(at::Device device);


    protected:
        c10::intrusive_ptr<::c10d::Store> store;
        int nDevs;
        flagcxStream_t stream;
        flagcxHandlerGroup_t handler;
        std::unique_ptr<EventFlagcx> event;
    };

    class WorkFlagcx : public Work
    {
        friend class BackendFlagcx;

    public:
        WorkFlagcx(
            OpType opType,
            c10::intrusive_ptr<c10::ivalue::Future> future) // future of the output
            : Work(
                  -1, // rank, only used by recvAnySource, irrelevant in this implementation
                  opType),
              future_(std::move(future))
        {
        }
        bool isCompleted() override;
        bool isSuccess() const override;
        bool wait(std::chrono::milliseconds timeout = kUnsetTimeout) override;
        virtual c10::intrusive_ptr<c10::ivalue::Future> getFuture() override;

    protected:
        bool isBarrierOp_{false};

    private:
        c10::intrusive_ptr<c10::ivalue::Future> future_;
    };
} // namespace c10d