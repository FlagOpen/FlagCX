#define CREATE_DEVICE_TOPO_API 
#include "topo.h"
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

    LOADSYMBOL(libHandle, flagcxTopoGetLocalNet);
    return flagcxSuccess;
}

