#include "launch_kernel.h"

void cpuAsyncLaunch(void *_args){
    struct hostLaunchArgs *args = (struct hostLaunchArgs *) _args;
    while(!args->stopLaunch);
    __atomic_store_n(&args->retLaunch, 1, __ATOMIC_RELAXED);
}
