#include "flagcx_hetero.h"
#include "adaptor.h"
#include "utils.h"

struct flagcxCCLAdaptor flagcxHeteroAdaptor{
    "flagcx_Hetero",
    LOADAPI(flagcxCCLAdaptor,getVersion,    flagcxHeteroGetVersion),
    LOADAPI(flagcxCCLAdaptor,getUniqueId,   flagcxHeteroGetUniqueId),
    LOADAPI(flagcxCCLAdaptor,commInitRank,  flagcxHeteroCommInitRank),
    LOADAPI(flagcxCCLAdaptor,commDestroy,   flagcxHeteroCommDestroy),
    LOADAPI(flagcxCCLAdaptor,commCount,     flagcxHeteroCommCount),
    LOADAPI(flagcxCCLAdaptor,commUserRank,  flagcxHeteroCommUserRank),
    LOADAPI(flagcxCCLAdaptor,send,          flagcxHeteroSend),
    LOADAPI(flagcxCCLAdaptor,recv,          flagcxHeteroRecv),
    LOADAPI(flagcxCCLAdaptor,groupStart,    flagcxHeteroGroupStart),
    LOADAPI(flagcxCCLAdaptor,groupEnd,      flagcxHeteroGroupEnd)
};
