#include "launch_kernel.h"
#include "group.h"
#include <dlfcn.h>
#include <stdio.h>

flagcxLaunchFunc_t deviceKernel = NULL;

flagcxResult_t loadAsyncKernelSymbol(const char *path, flagcxLaunchFunc_t *fn) {
    void* handle = openLib(path, RTLD_LAZY, [](const char* p, int err, const char* msg) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
    });
    
    if (!handle) return flagcxSystemError;
    
    void* sym = dlsym(handle, "launchAsyncKernel");
    if (!sym) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        return flagcxSystemError;
    }
    
    *fn = (flagcxLaunchFunc_t)sym;
    return flagcxSuccess;
}

void cpuStreamWait(void *_args) {
  bool *volatile args = (bool *)_args;
  __atomic_store_n(args, 1, __ATOMIC_RELAXED);
}

void cpuAsyncLaunch(void *_args) {
  FuncArgs *args = (FuncArgs *)_args;
  bool *volatile event = (bool *)args->hEvent;

  __atomic_store_n(event, 1, __ATOMIC_RELAXED);
  bool *volatile hargs = (bool *)args->hargs;
  while (!__atomic_load_n(hargs, __ATOMIC_RELAXED))
    ;

  free(hargs);
  free(event);
  free(args);
}
