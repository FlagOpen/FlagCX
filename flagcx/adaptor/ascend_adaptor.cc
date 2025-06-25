#include "ascend_adaptor.h"

#ifdef USE_ASCEND_ADAPTOR

std::map<flagcxMemcpyType_t, ascendMemcpyKind> memcpy_type_map = {
    {flagcxMemcpyHostToDevice, ascendMemcpyHostToDevice},
    {flagcxMemcpyDeviceToHost, ascendMemcpyDeviceToHost},
    {flagcxMemcpyDeviceToDevice, ascendMemcpyDeviceToDevice},
};

flagcxResult_t ascendAdaptorDeviceSynchronize() {
  DEVCHECK(ascendDeviceSynchronize());
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorDeviceMemcpy(void *dst, void *src, size_t size,
                                       flagcxMemcpyType_t type,
                                       flagcxStream_t stream, void *args) {
  if (stream == NULL) {
    DEVCHECK(aclrtMemcpy(dst, size, src, size, memcpy_type_map[type]));
  } else {
    DEVCHECK(
        aclrtMemcpyAsync(dst, size, src, size, memcpy_type_map[type], stream->base));
  }
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorDeviceMemset(void *ptr, int value, size_t size,
                                       flagcxMemType_t type,
                                       flagcxStream_t stream) {
  if (type == flagcxMemHost) {
    memset(ptr, value, size);
  } else {
    if (stream == NULL) {
      DEVCHECK(aclrtMemset(ptr, size, value, size));
    } else {
      DEVCHECK(aclrtMemsetAsync(ptr, size, value, size, stream->base));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorSetDevice(int dev) {
  DEVCHECK(aclrtSetDevice(dev));
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorGetDevice(int *dev) {
  DEVCHECK(aclrtGetDevice(dev));
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorGetDeviceCount(int *count) {
  DEVCHECK(aclrtGetDeviceCount(count));
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorGdrMemFree(void *ptr, void *memHandle) {
  if (ptr == NULL) {
    return flagcxSuccess;
  }
  DEVCHECK(aclrtFree(ptr));
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorStreamDestroy(flagcxStream_t stream) {
  if (stream != NULL) {
    DEVCHECK(aclrtDestroyStream(stream->base));
    free(stream);
    stream = NULL;
  }
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorStreamSynchronize(flagcxStream_t stream) {
  if (stream != NULL) {
    DEVCHECK(aclrtSynchronizeStream(stream->base));
  }
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorEventDestroy(flagcxEvent_t event) {
  if (event != NULL) {
    DEVCHECK(aclrtDestroyEvent(event->base));
    free(event);
    event = NULL;
  }
  return flagcxSuccess;
}

flagcxResult_t ascendAdaptorEventSynchronize(flagcxEvent_t event) {
  if (event != NULL) {
    DEVCHECK(aclrtSynchronizeEvent(event->base));
  }
  return flagcxSuccess;
}

struct flagcxDeviceAdaptor ascendAdaptor {
  "ASCEND",
      // Basic functions
      ascendAdaptorDeviceSynchronize, ascendAdaptorDeviceMemcpy,
      ascendAdaptorDeviceMemset,
      ascendAdaptorSetDevice, ascendAdaptorGetDevice, ascendAdaptorGetDeviceCount,
      // GDR functions
      NULL, // flagcxResult_t (*memHandleInit)(int dev_id, void **memHandle);
      NULL, // flagcxResult_t (*memHandleDestroy)(int dev, void *memHandle);
      ascendAdaptorGdrMemFree,
      NULL, // flagcxResult_t (*hostShareMemAlloc)(void **ptr, size_t size, void
            // *memHandle);
      NULL, // flagcxResult_t (*hostShareMemFree)(void *ptr, void *memHandle);
      // Stream functions
      ascendAdaptorStreamDestroy, ascendAdaptorStreamSynchronize,
      ascendAdaptorStreamQuery, ascendAdaptorStreamWaitEvent,
      // Event functions
      ascendAdaptorEventDestroy, ascendAdaptorEventSynchronize,
      // Kernel launch
      NULL, // flagcxResult_t (*launchKernel)(void *func, unsigned int block_x,
            // unsigned int block_y, unsigned int block_z, unsigned int grid_x,
            // unsigned int grid_y, unsigned int grid_z, void **args, size_t
            // share_mem, void *stream, void *memHandle);
      NULL, // flagcxResult_t (*copyArgsInit)(void **args);
      NULL, // flagcxResult_t (*copyArgsFree)(void *args);
      ascendAdaptorLaunchHostFunc
};

#endif // USE_ASCEND_ADAPTOR
