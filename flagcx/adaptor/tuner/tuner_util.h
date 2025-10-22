#ifndef FLAGCX_TUNER_UTIL_H_
#define FLAGCX_TUNER_UTIL_H_

#include "tuner.h" // struct flagcxEnvConfig
#include <vector>
#include <string>

// This is a demonstration function that provide a way to load all config list for a specific GPU.

struct EnvVar {
    std::string name;
    std::vector<std::string> choices;
    std::string defaultValue;
    EnvVar(std::string n="") : name(std::move(n)) {}
    EnvVar(std::string n, std::vector<std::string> c, std::string d = "")
      : name(std::move(n)), choices(std::move(c)), defaultValue(std::move(d)) {}
};

flagcxResult_t generateCandidate(std::vector<struct flagcxEnvConfig> &cfgList);
static void safeStrCopy(char *dst, size_t dstSize, const std::string &src);

#ifdef USE_NVIDIA_ADAPTOR

static EnvVar algo(
    "NCCL_ALGO",
    {"ring", "tree"},
    "ring"
);

static EnvVar proto(
    "NCCL_PROTO",
    {"LL", "LL128", "Simple"},
    "Simple"
);

static EnvVar thread(
    "NCCL_NTHREADS",
    {"128", "256"},
    "256"
);

static EnvVar minChannel(
    "NCCL_MIN_NCHANNELS",
    {"16", "32"},
    "16"
);

static EnvVar chunkSize(
    "NCCL_P2P_NVL_CHUNKSIZE",
    {"1024", "2048"},
    "1024"
);
static std::vector<EnvVar> vars = {algo, proto, thread, minChannel, chunkSize};

#else
static std::vector<EnvVar> vars = {};

#endif

#endif // end include guard