#define CREATE_GPU_MEMALLOC_API
#include "hostGpuMemAlloc.h"
#include "debug.h"
#include <stdlib.h>
#include <getopt.h>
#include <memory.h>
#include <stdio.h>
#include <math.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <dlfcn.h>
#include "adaptor.h"
#include "utils.h"
#include "param.h"

using namespace std;

struct flagcxDeviceAdaptor devRunTimeApi;

void *dlsymCheck(void *handle, const char *funcName){
    void *funcPtr = dlsym(handle, funcName);
    if(funcPtr == NULL) INFO(FLAGCX_INIT, "fail to load symbol %s", funcName);
    return funcPtr;
}

#define LOADSYMBOL(handle,api) do{ \
    api = (typeof(api)) dlsymCheck(handle, #api); \
}while(0);

flagcxResult_t loadDeviceSymbol(){
    void *libHandle = dlopen("./libmylib.so", RTLD_LAZY);
    if(libHandle == nullptr){
        const char* useNet = flagcxGetEnv("FLAGCX_USENET");
        if(useNet == NULL){
            INFO(FLAGCX_INIT, "fail to open libmylib.so");
            return flagcxRemoteError;
        }
        return flagcxSuccess;
    }

    LOADSYMBOL(libHandle, flagcxDevKernelFunc);
    LOADSYMBOL(libHandle, flagcxCuInit);
    LOADSYMBOL(libHandle, flagcxCuGdrMemAlloc);
    LOADSYMBOL(libHandle, flagcxDeviceCreateStream);
    LOADSYMBOL(libHandle, _flagcxLaunchKernel);
    LOADSYMBOL(libHandle, flagcxDeviceStreamSynchronize);
    LOADSYMBOL(libHandle, flagcxDeviceDestroyStream);
    LOADSYMBOL(libHandle, flagcxCuGdrMemFree);
    LOADSYMBOL(libHandle, flagcxCuDestroy);
    LOADSYMBOL(libHandle, flagcxHostShareMemAlloc);
    LOADSYMBOL(libHandle, flagcxHostShareMemFree);
    LOADSYMBOL(libHandle, flagcxDeviceSynchronize);
    LOADSYMBOL(libHandle, flagcxDeviceMemcpy);
    LOADSYMBOL(libHandle, flagcxDeviceMemset);
    LOADSYMBOL(libHandle, flagcxDeviceMalloc);
    LOADSYMBOL(libHandle, flagcxDeviceFree);
    LOADSYMBOL(libHandle, flagcxSetDevice);
    LOADSYMBOL(libHandle, flagcxGetDevice);
    LOADSYMBOL(libHandle, flagcxGetVendor);
    LOADSYMBOL(libHandle, flagcxDeviceStreamQuery);
    LOADSYMBOL(libHandle, flagcxCopyArgsInit);
    LOADSYMBOL(libHandle, flagcxCopyArgsFree);
    LOADSYMBOL(libHandle, flagcxDeviceCreateEvent);
    LOADSYMBOL(libHandle, flagcxDeviceEventQuery);
    LOADSYMBOL(libHandle, flagcxDeviceEventBlock);
    LOADSYMBOL(libHandle, flagcxDeviceDestroyEvent);
    LOADSYMBOL(libHandle, flagcxDeviceEventRecord);
    LOADSYMBOL(libHandle, flagcxDeviceLaunchHostFunc);
    LOADSYMBOL(libHandle, flagcxTopoGetLocalNet);

    

    struct flagcxDeviceAdaptor loadApi{
        "runTimeApi",
        LOADAPI(flagcxDeviceAdaptor,deviceSynchronize,  flagcxDeviceSynchronize),
        LOADAPI(flagcxDeviceAdaptor,deviceMemcpy,       flagcxDeviceMemcpy),
        LOADAPI(flagcxDeviceAdaptor,deviceMemset,       flagcxDeviceMemset),
        LOADAPI(flagcxDeviceAdaptor,deviceMalloc,       flagcxDeviceMalloc),
        LOADAPI(flagcxDeviceAdaptor,deviceFree,         flagcxDeviceFree),
        LOADAPI(flagcxDeviceAdaptor,setDevice,          flagcxSetDevice),
        LOADAPI(flagcxDeviceAdaptor,getDevice,          flagcxGetDevice),
        LOADAPI(flagcxDeviceAdaptor,getVendor,          flagcxGetVendor),
        LOADAPI(flagcxDeviceAdaptor,memHandleInit,      flagcxCuInit),
        LOADAPI(flagcxDeviceAdaptor,memHandleDestroy,   flagcxCuDestroy),
        LOADAPI(flagcxDeviceAdaptor,gdrMemAlloc,        flagcxCuGdrMemAlloc),
        LOADAPI(flagcxDeviceAdaptor,gdrMemFree,         flagcxCuGdrMemFree),
        LOADAPI(flagcxDeviceAdaptor,hostShareMemAlloc,  flagcxHostShareMemAlloc),
        LOADAPI(flagcxDeviceAdaptor,hostShareMemFree,   flagcxHostShareMemFree),
        LOADAPI(flagcxDeviceAdaptor,streamCreate,       flagcxDeviceCreateStream),
        LOADAPI(flagcxDeviceAdaptor,streamDestroy,      flagcxDeviceDestroyStream),
        LOADAPI(flagcxDeviceAdaptor,streamSynchronize,  flagcxDeviceStreamSynchronize),
        LOADAPI(flagcxDeviceAdaptor,streamQuery,        flagcxDeviceStreamQuery),
        LOADAPI(flagcxDeviceAdaptor,launchKernel,       _flagcxLaunchKernel),
        LOADAPI(flagcxDeviceAdaptor,copyArgsInit,       flagcxCopyArgsInit),
        LOADAPI(flagcxDeviceAdaptor,copyArgsFree,       flagcxCopyArgsFree),
        LOADAPI(flagcxDeviceAdaptor,topoGetSystem,      flagcxTopoGetSystem),
        LOADAPI(flagcxDeviceAdaptor,launchHostFunc,     flagcxDeviceLaunchHostFunc),
    };
    devRunTimeApi = loadApi;
    return flagcxSuccess;
}


flagcxResult_t flagcxLaunchKernel(void *func, DIM3 grid, DIM3 block, void **args, size_t share_mem, void *stream, void *memHandle){
    return _flagcxLaunchKernel(func, block.x, block.y, block.z, grid.x, grid.y, grid.z, args, share_mem, stream, memHandle);
}

void cpuAsyncLaunch(void *_args){
    struct hostLaunchArgs *args = (struct hostLaunchArgs *) _args;
    while(!args->stopLaunch);
    __atomic_store_n(&args->retLaunch, 1, __ATOMIC_RELAXED);
}