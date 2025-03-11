#include "cambricon_adaptor.h"

#ifdef USE_CAMBRICON_ADAPTOR

std::map<flagcxMemcpyType_t, cnrtMemTransDir_t> memcpy_type_map = {
    {flagcxMemcpyHostToDevice, cnrtMemcpyHostToDev},
    {flagcxMemcpyDeviceToHost, cnrtMemcpyDevToHost},
    {flagcxMemcpyDeviceToDevice, cnrtMemcpyDevToDev},
};

flagcxResult_t mluAdaptorDeviceSynchronize() {
  DEVCHECK(cnrtSyncDevice());
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorDeviceMemcpy(void *dst, void *src, size_t size,
                                      flagcxMemcpyType_t type,
                                      flagcxStream_t stream, void *args) {
  if (stream == NULL) {
    DEVCHECK(cnrtMemcpy(dst, src, size, memcpy_type_map[type]));
  } else {
    DEVCHECK(cnrtMemcpyAsync_V2(dst, src, size, stream->base,
                                memcpy_type_map[type]));
  }
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorDeviceMemset(void *ptr, int value, size_t size,
                                      flagcxMemType_t type,
                                      flagcxStream_t stream) {
  if (type == flagcxMemHost) {
    memset(ptr, value, size);
  } else {
    if (stream == NULL) {
      DEVCHECK(cnrtMemset(ptr, value, size));
    } else {
      DEVCHECK(cnrtMemsetAsync(ptr, value, size, stream->base));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorDeviceMalloc(void **ptr, size_t size,
                                      flagcxMemType_t type,
                                      flagcxStream_t stream) {
  if (type == flagcxMemHost) {
    DEVCHECK(cnrtHostMalloc(ptr, size));
  } else {
    DEVCHECK(cnrtMalloc(ptr, size));
    // DEVCHECK(cnrtMallocAsync(ptr, size, stream->base));
  }
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorDeviceFree(void *ptr, flagcxMemType_t type,
                                    flagcxStream_t stream) {
  if (type == flagcxMemHost) {
    DEVCHECK(cnrtFreeHost(ptr));
  } else {
    DEVCHECK(cnrtFree(ptr));
    // DEVCHECK(cnrtFreeAsync(ptr, stream->base));
  }
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorSetDevice(int dev) {
  DEVCHECK(cnrtSetDevice(dev));
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorGetDevice(int *dev) {
  DEVCHECK(cnrtGetDevice(dev));
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorGetDeviceCount(int *count) {
  DEVCHECK(cnrtGetDeviceCount(reinterpret_cast<unsigned int *>(count)));
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorGetVendor(char *vendor) {
  strcpy(vendor, "MLU");
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorGdrMemAlloc(void **ptr, size_t size, void *memHandle) {
  if (ptr == NULL) {
    return flagcxInvalidArgument;
  }
  DEVCHECK(cnrtMalloc(ptr, size));
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorGdrMemFree(void *ptr, void *memHandle) {
  if (ptr == NULL) {
    return flagcxSuccess;
  }
  DEVCHECK(cnrtFree(ptr));
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorStreamCreate(flagcxStream_t *stream) {
  (*stream) = NULL;
  flagcxCalloc(stream, 1);
  DEVCHECK(cnrtQueueCreate((cnrtQueue_t *)(*stream)));
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorStreamDestroy(flagcxStream_t stream) {
  if (stream != NULL) {
    DEVCHECK(cnrtQueueDestroy(stream->base));
    free(stream);
    stream = NULL;
  }
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorStreamCopy(flagcxStream_t *newStream,
                                    void *oldStream) {
  (*newStream) = NULL;
  flagcxCalloc(newStream, 1);
  memcpy((void *)*newStream, oldStream, sizeof(cnrtQueue_t));
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorStreamFree(flagcxStream_t stream) {
  if (stream != NULL) {
    free(stream);
    stream = NULL;
  }
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorStreamSynchronize(flagcxStream_t stream) {
  if (stream != NULL) {
    DEVCHECK(cnrtQueueSync(stream->base));
  }
  return flagcxSuccess;
}

flagcxResult_t mluAdaptorStreamQuery(flagcxStream_t stream) {
  flagcxResult_t res = flagcxSuccess;
  if (stream != NULL) {
    cnrtRet_t error = cnrtQueueQuery(stream->base);
    if (error == cnrtSuccess) {
      res = flagcxSuccess;
    } else if (error == cnrtErrorNotReady) {
      res = flagcxInProgress;
    } else {
      res = flagcxUnhandledDeviceError;
    }
  }
  return res;
}

flagcxResult_t mluAdaptorLaunchHostFunc(flagcxStream_t stream,
                                        void (*fn)(void *), void *args) {
  if (stream != NULL) {
    DEVCHECK(cnrtInvokeHostFunc(stream->base, fn, args));
  }
  return flagcxSuccess;
}

struct flagcxDeviceAdaptor mluAdaptor {
  "MLU",
      // Basic functions
      mluAdaptorDeviceSynchronize, mluAdaptorDeviceMemcpy,
      mluAdaptorDeviceMemset, mluAdaptorDeviceMalloc, mluAdaptorDeviceFree,
      mluAdaptorSetDevice, mluAdaptorGetDevice, mluAdaptorGetDeviceCount,
      mluAdaptorGetVendor,
      // GDR functions
      NULL, // flagcxResult_t (*memHandleInit)(int dev_id, void **memHandle);
      NULL, // flagcxResult_t (*memHandleDestroy)(int dev, void *memHandle);
      mluAdaptorGdrMemAlloc, mluAdaptorGdrMemFree,
      NULL, // flagcxResult_t (*hostShareMemAlloc)(void **ptr, size_t size, void
            // *memHandle);
      NULL, // flagcxResult_t (*hostShareMemFree)(void *ptr, void *memHandle);
      // Stream functions
      mluAdaptorStreamCreate, mluAdaptorStreamDestroy,
      mluAdaptorStreamCopy, mluAdaptorStreamFree, 
      mluAdaptorStreamSynchronize, mluAdaptorStreamQuery,
      // Kernel launch
      NULL, // flagcxResult_t (*launchKernel)(void *func, unsigned int block_x,
            // unsigned int block_y, unsigned int block_z, unsigned int grid_x,
            // unsigned int grid_y, unsigned int grid_z, void **args, size_t
            // share_mem, void *stream, void *memHandle);
      NULL, // flagcxResult_t (*copyArgsInit)(void **args);
      NULL, // flagcxResult_t (*copyArgsFree)(void *args);
      // Others
      NULL, // flagcxResult_t (*getDeviceProperties)(struct flagcxDevProps
            // *props, int dev);
      NULL, // flagcxResult_t (*getDevicePciBusId)(char *pciBusId, int len, int
            // dev);
      mluAdaptorLaunchHostFunc
};

#endif // USE_CAMBRICON_ADAPTOR
