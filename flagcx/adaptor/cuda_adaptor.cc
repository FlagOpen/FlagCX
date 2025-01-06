#include "nvidia_adaptor.h"

#ifdef USE_NVIDIA_ADAPTOR

std::map<flagcxMemcpyType_t, cudaMemcpyKind> memcpy_type_map = {
    {flagcxMemcpyHostToDevice, cudaMemcpyHostToDevice},
    {flagcxMemcpyDeviceToHost, cudaMemcpyDeviceToHost},
    {flagcxMemcpyDeviceToDevice, cudaMemcpyDeviceToDevice},
};

flagcxResult_t cudaAdaptorDeviceSynchronize() {
    DEVCHECK(cudaDeviceSynchronize());
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorDeviceMemcpy(void *dst, void *src, size_t size, flagcxMemcpyType_t type, flagcxStream_t stream, void *args) {
    if (stream == NULL) {
        DEVCHECK(cudaMemcpy(dst, src, size, memcpy_type_map[type]));
    } else {
        DEVCHECK(cudaMemcpyAsync(dst, src, size, memcpy_type_map[type], stream->base));
    }
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorDeviceMemset(void *ptr, int value, size_t size, flagcxMemType_t type, flagcxStream_t stream) {
    if (type == flagcxMemHost) {
        memset(ptr, value, size);
    } else {
        if (stream == NULL) {
            DEVCHECK(cudaMemset(ptr, value, size));
        } else {
            DEVCHECK(cudaMemsetAsync(ptr, value, size, stream->base));
        }
    }
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorDeviceMalloc(void **ptr, size_t size, flagcxMemType_t type) {
    if (type == flagcxMemHost) {
        DEVCHECK(cudaMallocHost(ptr, size));
    } else if (type == flagcxMemDevice) {
        DEVCHECK(cudaMalloc(ptr, size));
    } else if (type == flagcxMemManaged) {
        DEVCHECK(cudaMallocManaged(ptr, size, cudaMemAttachGlobal));
    }
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorDeviceFree(void *ptr, flagcxMemType_t type) {
    if (type == flagcxMemHost) {
        DEVCHECK(cudaFreeHost(ptr));
    } else {
        DEVCHECK(cudaFree(ptr));
    }
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorSetDevice(int dev) {
    DEVCHECK(cudaSetDevice(dev));
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorGetDevice(int *dev) {
    DEVCHECK(cudaGetDevice(dev));
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorGetDeviceCount(int *count) {
    DEVCHECK(cudaGetDeviceCount(count));
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorGetVendor(char *vendor) {
    strcpy(vendor, "NVIDIA");
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorGdrMemAlloc(void **ptr, size_t size, void *memHandle) {
    if (ptr == NULL) {
        return flagcxInvalidArgument;
    }
    DEVCHECK(cudaMalloc(ptr, size));
    cudaPointerAttributes attrs;
    DEVCHECK(cudaPointerGetAttributes(&attrs, *ptr));
    unsigned flags = 1;
    DEVCHECK(cuPointerSetAttribute(&flags, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, (CUdeviceptr) attrs.devicePointer));
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorGdrMemFree(void *ptr, void *memHandle) {
    if (ptr == NULL) {
        return flagcxSuccess;
    }
    DEVCHECK(cudaFree(ptr));
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorStreamCreate(flagcxStream_t *stream) {
    (*stream) = NULL;
    flagcxCalloc(stream, 1);
    DEVCHECK(cudaStreamCreateWithFlags((cudaStream_t *)(*stream), cudaStreamNonBlocking));
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorStreamDestroy(flagcxStream_t stream) {
    if (stream != NULL) {
        DEVCHECK(cudaStreamDestroy(stream->base));
        free(stream);
        stream = NULL;
    }
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorStreamSynchronize(flagcxStream_t stream) {
    if (stream != NULL) {
        DEVCHECK(cudaStreamSynchronize(stream->base));
    }
    return flagcxSuccess;
}

flagcxResult_t cudaAdaptorStreamQuery(flagcxStream_t stream) {
    flagcxResult_t res = flagcxSuccess;
    if (stream != NULL) {
        cudaError error = cudaStreamQuery(stream->base);
        if(error == cudaSuccess) {
            res = flagcxSuccess;
        } else if (error == cudaErrorNotReady) {
            res = flagcxInProgress;
        } else {
            res = flagcxUnhandledDeviceError;   
        }
    }
    return res;
}

flagcxResult_t cudaAdaptorLaunchHostFunc(flagcxStream_t stream, void (*fn)(void *),  void *args) {
    if (stream != NULL) {
        DEVCHECK(cudaLaunchHostFunc(stream->base, fn, args));
    }
    return flagcxSuccess;
}

struct flagcxDeviceAdaptor cudaAdaptor {
   "CUDA",
   // Basic functions
   cudaAdaptorDeviceSynchronize,
   cudaAdaptorDeviceMemcpy,
   cudaAdaptorDeviceMemset,
   cudaAdaptorDeviceMalloc,
   cudaAdaptorDeviceFree,
   cudaAdaptorSetDevice,
   cudaAdaptorGetDevice,
   cudaAdaptorGetDeviceCount,
   cudaAdaptorGetVendor,
   // GDR functions
   NULL, // flagcxResult_t (*memHandleInit)(int dev_id, void **memHandle);
   NULL, // flagcxResult_t (*memHandleDestroy)(int dev, void *memHandle);
   cudaAdaptorGdrMemAlloc,
   cudaAdaptorGdrMemFree,
   NULL, // flagcxResult_t (*hostShareMemAlloc)(void **ptr, size_t size, void *memHandle);
   NULL, // flagcxResult_t (*hostShareMemFree)(void *ptr, void *memHandle);
   // Stream functions
   cudaAdaptorStreamCreate,
   cudaAdaptorStreamDestroy,
   cudaAdaptorStreamSynchronize,
   cudaAdaptorStreamQuery,
   // Kernel launch
   NULL, // flagcxResult_t (*launchKernel)(void *func, unsigned int block_x, unsigned int block_y, unsigned int block_z, unsigned int grid_x, unsigned int grid_y, unsigned int grid_z, void **args, size_t share_mem, void *stream, void *memHandle);
   NULL, // flagcxResult_t (*copyArgsInit)(void **args);
   NULL, // flagcxResult_t (*copyArgsFree)(void *args);
   // Others
   NULL, // flagcxResult_t (*topoGetSystem)(void *topoArgs, void **system);
   cudaAdaptorLaunchHostFunc
};

#endif // USE_NVIDIA_ADAPTOR