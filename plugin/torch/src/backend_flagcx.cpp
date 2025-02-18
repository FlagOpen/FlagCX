#include "backend_flagcx.hpp"
#include "utils_flagcx.hpp"
#include <iostream>

namespace c10d {
namespace {

// FlagCX op mapping
const std::map<ReduceOp::RedOpType, flagcxRedOp_t> flagcxOp = {
    {ReduceOp::MIN, flagcxMin}, {ReduceOp::MAX, flagcxMax},
    {ReduceOp::SUM, flagcxSum}, {ReduceOp::PRODUCT, flagcxProd},
    {ReduceOp::AVG, flagcxAvg},
};

// Helper function that gets the FlagCX reduction operation
flagcxRedOp_t getFlagcxReduceOp(const ReduceOp &reduceOp, at::Tensor &input,
                                const flagcxDataType_t &dataType) {
  try {
    if (input.scalar_type() == at::kBool) {
      if (reduceOp == ReduceOp::SUM) {
        // For bool tensors, map sum to max, which both represent a bitwise or.
        // This is to prevent overflow issues with sum, since we use uint8 to
        // represent a bool (see ncclDataType mapping).
        return flagcxMax;
      }
      if (reduceOp == ReduceOp::AVG) {
        C10_THROW_ERROR(TypeError,
                        "Cannot use ReduceOp.AVG with boolean inputs");
      }
    }
    return flagcxOp.at(reduceOp);
  } catch (const std::out_of_range &) {
    switch (reduceOp) {
      case ReduceOp::AVG:
        C10_THROW_ERROR(ValueError, "Cannot use ReduceOp.AVG with FlagCX");
        break;
      case ReduceOp::BAND:
        C10_THROW_ERROR(ValueError, "Cannot use ReduceOp.BAND with FlagCX");
        break;
      case ReduceOp::BOR:
        C10_THROW_ERROR(ValueError, "Cannot use ReduceOp.BOR with FlagCX");
        break;
      case ReduceOp::BXOR:
        C10_THROW_ERROR(ValueError, "Cannot use ReduceOp.BXOR with FlagCX");
        break;
      default:
        C10_THROW_ERROR(ValueError, "Unhandled ReduceOp");
        break;
    }
  }
}

// FlagCX type typing
std::map<at::ScalarType, flagcxDataType_t> flagcxDataType = {
    {at::kChar, flagcxInt8},
    {at::kByte, flagcxUint8},
    {at::kFloat, flagcxFloat},
    {at::kDouble, flagcxDouble},
    {at::kInt, flagcxInt32},
    {at::kLong, flagcxInt64},
    {at::kHalf, flagcxHalf},
    {at::kBool, flagcxUint8},
    {at::kFloat8_e5m2, flagcxUint8},
    {at::kFloat8_e4m3fn, flagcxUint8},
    /*
    {at::kFloat8_e4m3fnuz, flagcxUint8},
    {at::kFloat8_e5m2fnuz, flagcxUint8},
    */
    {at::kBFloat16, flagcxBfloat16},
};

// Helper function that gets the data type and issues error if not supported
flagcxDataType_t getFlagcxDataType(at::ScalarType type) {
  auto it = flagcxDataType.find(type);
  TORCH_CHECK_WITH(
      TypeError, it != flagcxDataType.end(),
      "Input tensor data type is not supported for FlagCX process group: ",
      type);
  return it->second;
}

bool check_same_size(const std::vector<at::Tensor> &inputTensors) {
  for (const auto &inputTensor : inputTensors) {
    if (!inputTensors[0].is_same_size(inputTensor)) {
      return false;
    }
  }
  return true;
}

void check_device(at::Device dev1, at::Device dev2) {
#ifdef USE_CAMBRICON_ADAPTOR
  if (dev1.is_privateuseone() && dev2.is_privateuseone() && dev1 != dev2) {
    throw std::runtime_error(
        "flagcxBackend does not support multidevice tensors");
  }
#else
  if (dev1.is_cuda() && dev2.is_cuda() && dev1 != dev2) {
    throw std::runtime_error(
        "flagcxBackend does not support multidevice tensors");
  }
#endif
}

int64_t check_gpu_tensors_same_device(const std::vector<at::Tensor> &tensors) {
  if (tensors.empty()) {
    C10_THROW_ERROR(ValueError, "Tensor list must be nonempty");
  }

  const auto &first = tensors.front();

  int64_t totalNumel = 0;
  for (const auto &t : tensors) {
    if (t.is_sparse()) {
      C10_THROW_ERROR(ValueError, "Tensors must be dense");
    }
    if (t.scalar_type() != first.scalar_type()) {
      C10_THROW_ERROR(TypeError, "Tensors must have identical type");
    }
    if (!t.is_non_overlapping_and_dense()) {
      C10_THROW_ERROR(ValueError, "Tensors must be non-overlapping and dense");
    }
    // If we're in this function, the user called a _coalesced collective
    // on a set of tensors with potentially different sizes and strides.
    // Therefore, we don't check for matching sizes and strides,
    // but we do double-check tensors are on the same device.
    TORCH_CHECK_WITH(ValueError, t.get_device() == tensors[0].get_device(),
                     "Expected list of tensors on the same device");
    totalNumel += t.numel();
  }
  return totalNumel;
}

} // namespace

bool flagcxWork::isCompleted() {
  if (!coalesced_) {
    return future_->completed();
  }
  return false;
}

bool flagcxWork::isSuccess() const {
  if (!coalesced_) {
    return future_->hasValue();
  }
  return false;
}

bool flagcxWork::wait(std::chrono::milliseconds /* unused */) {
  if (!coalesced_) {
    event_->block(deviceId_);
  }
  if (isBarrierOp_) {
    C10D_FLAGCX_CHECK(handler_->streamSynchronize(stream_), std::nullopt);
  }
  return true;
}

c10::intrusive_ptr<c10::ivalue::Future> flagcxWork::getFuture() {
  return future_;
}

// If necessary, pass store/rank/size to the ctor and exchange connection
// information here
flagcxBackend::flagcxBackend(const c10::intrusive_ptr<::c10d::Store> &store,
                             int rank, int size)
    : Backend(rank, size), store_(store) {
  deviceId_ = 0;
  status_ = 0;
  activeGroupCounter_ = 0;
  C10D_FLAGCX_CHECK(flagcxHandleInit(&handler_), std::nullopt);
  C10D_FLAGCX_CHECK(handler_->devHandle->getDeviceCount(&nDevs_), std::nullopt);
#ifdef USE_NVIDIA_ADAPTOR
  event_ = std::make_unique<flagcxCudaEvent>();
#elif USE_ILUVATAR_COREX_ADAPTOR
  event_ = std::make_unique<flagcxIxcudaEvent>();
#elif USE_CAMBRICON_ADAPTOR
  event_ = std::make_unique<flagcxMluEvent>();
#endif
}

flagcxBackend::~flagcxBackend() {
  if (status_ == 1) {
    handler_->devHandle->streamDestroy(stream_);
    flagcxCommDestroy(handler_->comm);
    status_ = 0;
  }
  if (status_ == 0) {
    flagcxHandleFree(handler_);
  }
}

void flagcxBackend::initComm(at::Device dev) {
  if (status_ == 0) {
    deviceId_ = dev.index();
    C10D_FLAGCX_CHECK(handler_->devHandle->setDevice(deviceId_), std::nullopt);
    // Get the unique id
    C10D_FLAGCX_CHECK(flagcxGetUniqueId(&handler_->uniqueId), std::nullopt);
    if (rank_ == 0) {
      auto vec =
          std::vector<uint8_t>(reinterpret_cast<uint8_t *>(handler_->uniqueId),
                               reinterpret_cast<uint8_t *>(handler_->uniqueId) +
                                   sizeof(flagcxUniqueId));
      store_->set("flagcx/unique_id", std::string(vec.begin(), vec.end()));
    } else {
      try {
        auto vec = store_->get("flagcx/unique_id");
        TORCH_CHECK_WITH(DistBackendError, vec.size() == sizeof(flagcxUniqueId),
                         "Invalide size for flagcxUniqueId");
        std::memcpy((uint8_t *)handler_->uniqueId, vec.data(),
                    sizeof(flagcxUniqueId));
      } catch (const std::exception &e) {
        throw std::runtime_error(
            "Failed to retrieve the unique id from the store: " +
            std::string(e.what()));
      } catch (...) {
        throw std::runtime_error("Unknown exception during the retrieving of "
                                 "unique id from the store");
      }
    }
    // Initialize the communicator
    C10D_FLAGCX_CHECK(
        flagcxCommInitRank(&handler_->comm, size_, handler_->uniqueId, rank_),
        std::nullopt);
    // Initialize the stream
    C10D_FLAGCX_CHECK(handler_->devHandle->streamCreate(&stream_),
                      std::nullopt);
    status_ = 1;
  } else {
    if (dev.is_cuda() || dev.is_privateuseone()) {
      if (deviceId_ != dev.index()) {
        throw std::runtime_error(
            "flagcx communicator was initialized with different device");
      }
    }
  }
}

void flagcxBackend::initComm() {
#if defined(USE_NVIDIA_ADAPTOR) || defined(USE_ILUVATAR_COREX_ADAPTOR)
  initComm(c10::impl::getDeviceGuardImpl(at::DeviceType::CUDA)->getDevice());
#elif defined(USE_CAMBRICON_ADAPTOR)
  initComm(
      c10::impl::getDeviceGuardImpl(at::DeviceType::PrivateUse1)->getDevice());
#endif
}

void flagcxBackend::syncStream(at::Device device) {
  event_->record(device.index());
  event_->block(stream_, device.index());
}

void flagcxBackend::groupStart() {
  initComm();
  C10D_FLAGCX_CHECK(flagcxGroupStart(handler_->comm), std::nullopt);
  ++activeGroupCounter_;
}

void flagcxBackend::groupEnd() {
  initComm();
  C10D_FLAGCX_CHECK(flagcxGroupEnd(handler_->comm), std::nullopt);
  --activeGroupCounter_;
}

void flagcxBackend::startCoalescing() { groupStart(); }

c10::intrusive_ptr<Work> flagcxBackend::endCoalescing() {
  groupEnd();

  auto work = c10::make_intrusive<flagcxWork>(OpType::COALESCED, stream_,
                                              handler_->devHandle);
  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  work->isBarrierOp_ = true;
  // Create a future to track the coalesced operation
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()));
  work->future_->markCompleted(c10::IValue(0));

  return work;
}

template <typename Fn>
c10::intrusive_ptr<Work>
flagcxBackend::collectiveCoalesced(std::vector<at::Tensor> &inputs,
                                   std::vector<at::Tensor> &outputs, Fn fn,
                                   OpType opType) {
  // Currently, the API permits one scenario where inputs.size() and
  // outputs.size() are > 0.
  // 1. If the call was a _coalesced call, all inputs must be on the same
  // device.
  //    The group of flagcx calls applies the collective separately to each
  //    input, but the group as a whole should be efficient.
  auto device = inputs[0].device();
  initComm(device);

  // TODO: keep track of the coalesced state at backend side.

  // First let flagcx stream wait for input tensor allocation stream
  syncStream(device);
  auto work =
      c10::make_intrusive<flagcxWork>(opType, stream_, handler_->devHandle);

  {
    flagcxGroupGuard guard(handler_->comm);
    for (const auto i : c10::irange(inputs.size())) {
      // TODO: we need to record these input/output to prevent being freed
      // before the collective finished.
      auto inputTensor = inputs[i];
      auto outputTensor = outputs[i];
      // Perform the collective operation
      C10D_FLAGCX_CHECK(fn(inputTensor, outputTensor, handler_->comm, stream_),
                        std::nullopt);
    }
  }

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  work->isBarrierOp_ = false;
  // Create a future to track the coalesced operation
  std::vector<at::Device> devices{inputs[0].device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(outputs[0]));
  return work;
}

c10::intrusive_ptr<Work>
flagcxBackend::allgather(std::vector<std::vector<at::Tensor>> &outputTensors,
                         std::vector<at::Tensor> &inputTensors,
                         const AllgatherOptions & /* unused */) {
  auto inputTensor = inputTensors.back();
  auto outputTensorsTmp = outputTensors.back();
  auto flagcxDataType = getFlagcxDataType(inputTensor.scalar_type());
  auto work = c10::make_intrusive<flagcxWork>(OpType::ALLGATHER, stream_,
                                              handler_->devHandle);
  check_device(inputTensor.device(), outputTensorsTmp[0].device());
  initComm(inputTensor.device());
  syncStream(inputTensor.device());

  if (!check_same_size(outputTensorsTmp)) {
    throw std::runtime_error(
        "flagcx only support same size allgather operation");
  } else {
    // Flatten a vector of tensors into a single, stacked tensor.
    at::Tensor outputFlattened = newLikeFlat(outputTensorsTmp);

    // Perform the allgather operation
    C10D_FLAGCX_CHECK(flagcxAllGather(inputTensor.data_ptr(),
                                      outputFlattened.data_ptr(),
                                      inputTensor.numel(), flagcxDataType,
                                      handler_->comm, stream_),
                      std::nullopt);

    // Copy the flattened tensor back into a vector of tensors.
    for (const auto j : c10::irange(outputTensorsTmp.size())) {
      outputTensorsTmp[j].copy_(outputFlattened[j], true);
    }
  }

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the allgather operation
  std::vector<at::Device> devices{inputTensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(outputTensorsTmp));
  return work;
}

c10::intrusive_ptr<Work>
flagcxBackend::_allgather_base(at::Tensor &outputTensor,
                               at::Tensor &inputTensor,
                               const AllgatherOptions & /* unused */) {
  auto flagcxDataType = getFlagcxDataType(inputTensor.scalar_type());
  auto work = c10::make_intrusive<flagcxWork>(OpType::_ALLGATHER_BASE, stream_,
                                              handler_->devHandle);
  check_device(inputTensor.device(), outputTensor.device());
  initComm(inputTensor.device());
  syncStream(inputTensor.device());

  // Perform the allgather operation
  C10D_FLAGCX_CHECK(flagcxAllGather(inputTensor.data_ptr(),
                                    outputTensor.data_ptr(),
                                    inputTensor.numel(), flagcxDataType,
                                    handler_->comm, stream_),
                    std::nullopt);

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the allgather operation
  std::vector<at::Device> devices{inputTensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(outputTensor));
  return work;
}

c10::intrusive_ptr<Work>
flagcxBackend::allgather_into_tensor_coalesced(std::vector<at::Tensor> &outputs,
                                               std::vector<at::Tensor> &inputs,
                                               const AllgatherOptions &opts) {
  // parameter validation
  check_gpu_tensors_same_device(inputs);

  return collectiveCoalesced(
      inputs, outputs,
      [&](at::Tensor &input, at::Tensor &output, flagcxComm_t comm,
          flagcxStream_t stream) {
        auto flagcxDataType = getFlagcxDataType(input.scalar_type());
        return flagcxAllGather(input.data_ptr(), output.data_ptr(),
                               input.numel(), flagcxDataType, comm, stream);
      },
      OpType::COALESCED);
}

c10::intrusive_ptr<Work>
flagcxBackend::allreduce(std::vector<at::Tensor> &tensors,
                         const AllreduceOptions &opts) {
  auto &tensor = tensors.back();
  auto flagcxDataType = getFlagcxDataType(tensor.scalar_type());
  auto flagcxReduceOp =
      getFlagcxReduceOp(opts.reduceOp, tensor, flagcxDataType);
  auto work = c10::make_intrusive<flagcxWork>(OpType::ALLREDUCE, stream_,
                                              handler_->devHandle);
  initComm(tensor.device());
  syncStream(tensor.device());

  // Perform the allreduce operation
  C10D_FLAGCX_CHECK(flagcxAllReduce(tensor.data_ptr(), tensor.data_ptr(),
                                    tensor.numel(), flagcxDataType,
                                    flagcxReduceOp, handler_->comm, stream_),
                    std::nullopt);

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the allreduce operation
  std::vector<at::Device> devices{tensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(tensors));
  return work;
}

c10::intrusive_ptr<Work>
flagcxBackend::allreduce_coalesced(std::vector<at::Tensor> &tensors,
                                   const AllreduceCoalescedOptions &opts) {
  // parameter validation
  check_gpu_tensors_same_device(tensors);
  TORCH_CHECK(
      !isFloat8Type(tensors.back().scalar_type()),
      "Float8 dtypes are not currenlty supported for FlagCX reductions");

  return collectiveCoalesced(
      tensors, tensors,
      [&](at::Tensor &input, at::Tensor &output, flagcxComm_t comm,
          flagcxStream_t stream) {
        auto flagcxDataType = getFlagcxDataType(input.scalar_type());
        auto flagcxReduceOp =
            getFlagcxReduceOp(opts.reduceOp, input, flagcxDataType);
        return flagcxAllReduce(input.data_ptr(), output.data_ptr(),
                               input.numel(), flagcxDataType, flagcxReduceOp,
                               comm, stream);
      },
      OpType::COALESCED);
}

c10::intrusive_ptr<Work>
flagcxBackend::alltoall(std::vector<at::Tensor> &outputTensors,
                        std::vector<at::Tensor> &inputTensors,
                        const AllToAllOptions & /* unused */) {
  if (inputTensors.size() != outputTensors.size())
    throw std::runtime_error("Input and output tensors size must be equal");
  if (inputTensors[0].numel() != outputTensors[0].numel())
    throw std::runtime_error(
        "Input and output tensors must have the same number of elements");
  if (!check_same_size(inputTensors) || !check_same_size(outputTensors))
    throw std::runtime_error(
        "flagcx only support same size alltoall operation");
  if (getFlagcxDataType(inputTensors[0].scalar_type()) !=
      getFlagcxDataType(outputTensors[0].scalar_type()))
    throw std::runtime_error(
        "Input and output tensors must have the same data type");
  auto count = inputTensors[0].numel();
  auto flagcxDataType = getFlagcxDataType(inputTensors[0].scalar_type());
  auto device = outputTensors[0].device();
  auto work = c10::make_intrusive<flagcxWork>(OpType::ALLTOALL, stream_,
                                              handler_->devHandle);
  initComm(device);
  syncStream(device);

  // Flatten a vector of tensors into a single, stacked tensor.
  at::Tensor inputFlattened = newLikeFlat(inputTensors);
  at::Tensor outputFlattened = newLikeFlat(outputTensors);

  // Copy the input tensors to the flattened tensor.
  for (const auto j : c10::irange(inputTensors.size())) {
    inputFlattened[j].copy_(inputTensors[j], true);
  }

  C10D_FLAGCX_CHECK(flagcxAlltoAll(inputFlattened.data_ptr(),
                                   outputFlattened.data_ptr(), count,
                                   flagcxDataType, handler_->comm, stream_),
                    std::nullopt);

  // Copy the flattened tensor back into a vector of tensors.
  for (const auto j : c10::irange(outputTensors.size())) {
    outputTensors[j].copy_(outputFlattened[j], true);
  }

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the alltoall operation
  std::vector<at::Device> devices{device};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(outputTensors));
  return work;
}

c10::intrusive_ptr<Work>
flagcxBackend::alltoall_base(at::Tensor &outputTensor, at::Tensor &inputTensor,
                             std::vector<int64_t> &outputSplitSizes,
                             std::vector<int64_t> &inputSplitSizes,
                             const AllToAllOptions & /* unused */) {
  throw std::runtime_error("flagcxBackend does not support alltoall_base");
}

c10::intrusive_ptr<Work> flagcxBackend::barrier(const BarrierOptions &opts) {
  initComm();
  auto work = c10::make_intrusive<flagcxWork>(OpType::BARRIER, stream_,
                                              handler_->devHandle);
  C10D_FLAGCX_CHECK(flagcxBarrier(handler_->comm, stream_), std::nullopt);

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  work->isBarrierOp_ = true;
  // Create a future to track the barrier operation
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()));
  work->future_->markCompleted(c10::IValue(0));
  return work;
}

c10::intrusive_ptr<Work>
flagcxBackend::broadcast(std::vector<at::Tensor> &tensors,
                         const BroadcastOptions &opts) {
  auto &tensor = tensors.back();
  auto flagcxDataType = getFlagcxDataType(tensor.scalar_type());
  auto work = c10::make_intrusive<flagcxWork>(OpType::BROADCAST, stream_,
                                              handler_->devHandle);
  initComm(tensor.device());
  syncStream(tensor.device());

  const auto root = opts.rootRank + opts.rootTensor;

  C10D_FLAGCX_CHECK(flagcxBroadcast(tensor.data_ptr(), tensor.data_ptr(),
                                    tensor.numel(), flagcxDataType, root,
                                    handler_->comm, stream_),
                    std::nullopt);

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the broadcast operation
  std::vector<at::Device> devices{tensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(tensors));
  return work;
}

c10::intrusive_ptr<Work>
flagcxBackend::gather(std::vector<std::vector<at::Tensor>> &outputTensors,
                      std::vector<at::Tensor> &inputTensors,
                      const GatherOptions &opts) {
  auto &inputTensor = inputTensors.back();
  auto flagcxDataType = getFlagcxDataType(inputTensor.scalar_type());
  auto work = c10::make_intrusive<flagcxWork>(OpType::GATHER, stream_,
                                              handler_->devHandle);
  initComm(inputTensor.device());
  syncStream(inputTensor.device());

  auto root = opts.rootRank;
  std::vector<at::Tensor> outputTensorsTmp;
  if (rank_ == root) {
    outputTensorsTmp = outputTensors.back();
  } else {
    outputTensorsTmp = {};
    outputTensorsTmp.emplace_back(
        at::ones({1}, at::TensorOptions().device(inputTensor.device())));
  }

  // Flatten a vector of tensors into a single, stacked tensor.
  at::Tensor outputFlattened = newLikeFlat(outputTensorsTmp);

  // Perform the gather operation
  C10D_FLAGCX_CHECK(flagcxGather(inputTensor.data_ptr(),
                                 outputFlattened.data_ptr(),
                                 inputTensor.numel(), flagcxDataType, root,
                                 handler_->comm, stream_),
                    std::nullopt);

  // Unflatten the flattened tensor back into a vector of tensors.
  if (rank_ == root) {
    for (const auto j : c10::irange(outputTensorsTmp.size())) {
      outputTensorsTmp[j].copy_(outputFlattened[j], true);
    }
  }

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the gather operation
  std::vector<at::Device> devices{inputTensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(outputTensorsTmp));
  return work;
}

c10::intrusive_ptr<Work> flagcxBackend::reduce(std::vector<at::Tensor> &tensors,
                                               const ReduceOptions &opts) {
  auto &tensor = tensors.back();
  auto flagcxDataType = getFlagcxDataType(tensor.scalar_type());
  auto flagcxReduceOp =
      getFlagcxReduceOp(opts.reduceOp, tensor, flagcxDataType);
  auto work = c10::make_intrusive<flagcxWork>(OpType::REDUCE, stream_,
                                              handler_->devHandle);
  initComm(tensor.device());
  syncStream(tensor.device());

  const auto root = opts.rootRank + opts.rootTensor;

  C10D_FLAGCX_CHECK(flagcxReduce(tensor.data_ptr(), tensor.data_ptr(),
                                 tensor.numel(), flagcxDataType, flagcxReduceOp,
                                 root, handler_->comm, stream_),
                    std::nullopt);

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the reduce operation
  std::vector<at::Device> devices{tensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(tensors));
  return work;
}

c10::intrusive_ptr<Work> flagcxBackend::reduce_scatter(
    std::vector<at::Tensor> &outputTensors,
    std::vector<std::vector<at::Tensor>> &inputTensors,
    const ReduceScatterOptions &opts) {
  auto outputTensor = outputTensors.back();
  auto inputTensorsTmp = inputTensors.back();
  auto flagcxDataType = getFlagcxDataType(outputTensor.scalar_type());
  auto flagcxReduceOp =
      getFlagcxReduceOp(opts.reduceOp, outputTensor, flagcxDataType);
  auto work = c10::make_intrusive<flagcxWork>(OpType::REDUCE_SCATTER, stream_,
                                              handler_->devHandle);
  check_device(outputTensor.device(), inputTensorsTmp[0].device());
  initComm(outputTensor.device());
  syncStream(outputTensor.device());

  if (!check_same_size(inputTensorsTmp)) {
    throw std::runtime_error(
        "flagcx only support same size reducescatter operation");
  } else {
    // Flatten a vector of tensors into a single, stacked tensor.
    at::Tensor inputFlattened = newLikeFlat(inputTensorsTmp);

    // Copy the input tensors to the flattened tensor.
    for (const auto j : c10::irange(inputTensorsTmp.size())) {
      inputFlattened[j].copy_(inputTensorsTmp[j], true);
    }

    // Perform the reducescatter operation
    C10D_FLAGCX_CHECK(
        flagcxReduceScatter(inputFlattened.data_ptr(), outputTensor.data_ptr(),
                            outputTensor.numel(), flagcxDataType,
                            flagcxReduceOp, handler_->comm, stream_),
        std::nullopt);
  }

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the reducescatter operation
  std::vector<at::Device> devices{outputTensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(outputTensor));
  return work;
}

c10::intrusive_ptr<Work>
flagcxBackend::_reduce_scatter_base(at::Tensor &outputTensor,
                                    at::Tensor &inputTensor,
                                    const ReduceScatterOptions &opts) {
  auto flagcxDataType = getFlagcxDataType(outputTensor.scalar_type());
  auto flagcxReduceOp =
      getFlagcxReduceOp(opts.reduceOp, outputTensor, flagcxDataType);
  auto work = c10::make_intrusive<flagcxWork>(OpType::_REDUCE_SCATTER_BASE,
                                              stream_, handler_->devHandle);
  check_device(outputTensor.device(), inputTensor.device());
  initComm(outputTensor.device());
  syncStream(outputTensor.device());

  if (inputTensor.numel() != outputTensor.numel() * size_) {
    throw std::runtime_error(
        "Input tensor must be the same szie as output size times world size");
  } else {
    // Perform the reducescatter operation
    C10D_FLAGCX_CHECK(
        flagcxReduceScatter(inputTensor.data_ptr(), outputTensor.data_ptr(),
                            outputTensor.numel(), flagcxDataType,
                            flagcxReduceOp, handler_->comm, stream_),
        std::nullopt);
  }

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the reducescatter operation
  std::vector<at::Device> devices{outputTensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(outputTensor));
  return work;
}

c10::intrusive_ptr<Work> flagcxBackend::reduce_scatter_tensor_coalesced(
    std::vector<at::Tensor> &outputs, std::vector<at::Tensor> &inputs,
    const ReduceScatterOptions &opts) {
  // parameter validation
  check_gpu_tensors_same_device(inputs);
  TORCH_CHECK(
      !isFloat8Type(inputs.back().scalar_type()),
      "Float8 dtypes are not currenlty supported for FlagCX reductions");

  return collectiveCoalesced(
      inputs, outputs,
      [&](at::Tensor &input, at::Tensor &output, flagcxComm_t comm,
          flagcxStream_t stream) {
        auto flagcxDataType = getFlagcxDataType(input.scalar_type());
        auto flagcxReduceOp =
            getFlagcxReduceOp(opts.reduceOp, input, flagcxDataType);
        return flagcxReduceScatter(input.data_ptr(), output.data_ptr(),
                                   output.numel(), flagcxDataType,
                                   flagcxReduceOp, comm, stream);
      },
      OpType::COALESCED);
}

c10::intrusive_ptr<Work>
flagcxBackend::scatter(std::vector<at::Tensor> &outputTensors,
                       std::vector<std::vector<at::Tensor>> &inputTensors,
                       const ScatterOptions &opts) {
  auto &outputTensor = outputTensors.back();
  auto flagcxDataType = getFlagcxDataType(outputTensor.scalar_type());
  auto work = c10::make_intrusive<flagcxWork>(OpType::SCATTER, stream_,
                                              handler_->devHandle);
  initComm(outputTensor.device());
  syncStream(outputTensor.device());

  auto root = opts.rootRank;
  std::vector<at::Tensor> inputTensorsTmp;
  if (rank_ == root) {
    inputTensorsTmp = inputTensors.back();
  } else {
    inputTensorsTmp = {};
    inputTensorsTmp.emplace_back(
        at::ones({1}, at::TensorOptions().device(outputTensor.device())));
  }

  // Flatten a vector of tensors into a single, stacked tensor.
  at::Tensor inputFlattened = newLikeFlat(inputTensorsTmp);

  // Copy the input tensors to the flattened tensor.
  if (rank_ == root) {
    for (const auto j : c10::irange(inputTensorsTmp.size())) {
      inputFlattened[j].copy_(inputTensorsTmp[j], true);
    }
  }

  // Perform the scatter operation
  C10D_FLAGCX_CHECK(flagcxScatter(inputFlattened.data_ptr(),
                                  outputTensor.data_ptr(), outputTensor.numel(),
                                  flagcxDataType, root, handler_->comm,
                                  stream_),
                    std::nullopt);

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = false;
  // Create a future to track the scatter operation
  std::vector<at::Device> devices{outputTensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(outputTensor));
  return work;
}

c10::intrusive_ptr<Work> flagcxBackend::send(std::vector<at::Tensor> &tensors,
                                             int dstRank, int tag) {
  auto &tensor = tensors.back();
  auto flagcxDataType = getFlagcxDataType(tensor.scalar_type());
  auto work = c10::make_intrusive<flagcxWork>(OpType::SEND, stream_,
                                              handler_->devHandle);
  initComm(tensor.device());
  syncStream(tensor.device());

  // Perform the send operation
  C10D_FLAGCX_CHECK(flagcxSend(tensor.data_ptr(), tensor.numel(),
                               flagcxDataType, dstRank, handler_->comm,
                               stream_),
                    std::nullopt);

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = (activeGroupCounter_ > 0);
  // Create a future to track the send operation
  std::vector<at::Device> devices{tensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(tensors));
  return work;
}

c10::intrusive_ptr<Work> flagcxBackend::recv(std::vector<at::Tensor> &tensors,
                                             int srcRank, int tag) {
  auto &tensor = tensors.back();
  auto flagcxDataType = getFlagcxDataType(tensor.scalar_type());
  auto work = c10::make_intrusive<flagcxWork>(OpType::RECV, stream_,
                                              handler_->devHandle);
  initComm(tensor.device());
  syncStream(tensor.device());

  // Perform the recv operation
  C10D_FLAGCX_CHECK(flagcxRecv(tensor.data_ptr(), tensor.numel(),
                               flagcxDataType, srcRank, handler_->comm,
                               stream_),
                    std::nullopt);

  work->event_->record(stream_, deviceId_);
  work->deviceId_ = deviceId_;
  work->coalesced_ = (activeGroupCounter_ > 0);
  // Create a future to track the recv operation
  std::vector<at::Device> devices{tensor.device()};
  work->future_ = c10::make_intrusive<c10::ivalue::Future>(
      c10::ListType::create(c10::TensorType::get()), devices);
  work->future_->markCompleted(c10::IValue(tensors));
  return work;
}

c10::intrusive_ptr<Work>
flagcxBackend::recvAnysource(std::vector<at::Tensor> &tensors, int tag) {
  throw std::runtime_error("flagcxBackend does not support recvAnysource");
}

c10::intrusive_ptr<Backend> flagcxBackend::createFlagcxBackend(
    const c10::intrusive_ptr<::c10d::Store> &store, int rank, int size,
    const std::chrono::duration<float> & /* unused */) {
  return c10::make_intrusive<flagcxBackend>(store, rank, size);
}

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
  m.def("createFlagcxBackend", &flagcxBackend::createFlagcxBackend);
}

} // namespace c10d