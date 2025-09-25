/*************************************************************************
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef FLAGCX_TIMER_H_
#define FLAGCX_TIMER_H_
#if ENABLE_TIMER
#include <sys/time.h>
#include <unistd.h>
#include <x86intrin.h>
static double freq = -1;
static void calibrate() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint64_t timeCycles = __rdtsc();
  double time = -tv.tv_sec * 1E6 - tv.tv_usec;
  uint64_t total = 0ULL;
  for (int i = 0; i < 10000; i++)
    total += __rdtsc();
  gettimeofday(&tv, NULL);
  timeCycles = __rdtsc() - timeCycles;
  time += tv.tv_sec * 1E6 + tv.tv_usec;
  freq = timeCycles / time;
}
static inline double gettime() {
  if (freq == -1)
    calibrate();
  return __rdtsc() / freq;
}
static uint64_t counts[8];
static double times[8];
static double startTimes[8];
#define TIME_START(index)                                                      \
  do {                                                                         \
    counts[index]++;                                                           \
    startTimes[index] = gettime();                                             \
  } while (0);

#define TIME_STOP(index)                                                       \
  do {                                                                         \
    times[index] += gettime() - startTimes[index];                             \
  } while (0);

#define TIME_CANCEL(index)                                                     \
  do {                                                                         \
    counts[index]--;                                                           \
  } while (0);

#define TIME_PRINT(name)                                                       \
  do {                                                                         \
    printf("%s stats", name);                                                  \
    for (int i = 0; i < 8; i++) {                                              \
      if (counts[i])                                                           \
        printf(" [%d] %g/%ld = %g", i, times[i], counts[i],                    \
               times[i] / counts[i]);                                          \
      counts[i] = 0;                                                           \
    }                                                                          \
    printf("\n");                                                              \
  } while (0);
#else
#define TIME_START(index)                                                      \
  while (0)                                                                    \
    ;
#define TIME_STOP(index)                                                       \
  while (0)                                                                    \
    ;
#define TIME_CANCEL(index)                                                     \
  while (0)                                                                    \
    ;
#define TIME_PRINT(name)
#endif


#include <pthread.h>
#include <queue>
#include <tuple>
#include <vector>
#include <sstream>
#include <cassert>

#include "flagcx.h"
#include "debug.h"
#include "adaptor.h"

constexpr int RECORD_NUM = 2048;

template <typename T>
struct RecordKey {
    T value;

    RecordKey() = default;
    RecordKey(const T &v) : value(v) {}

    std::string debugStr() const {
        std::stringstream ss;
        ss << value;
        return ss.str();
    }

    bool operator<(const RecordKey<T> &other) const {
        return value < other.value;
    }
};



template <typename T>
struct flagcxRecord {
    flagcxEvent_t begin_event;
    flagcxEvent_t end_event;
    RecordKey<T> recordKey; // commHash = Hash(groupID, datasize, comm_type)
    float duration;      // ms
    flagcxStream_t stream;

    flagcxRecord();
    flagcxRecord(const flagcxRecord &) = delete;
    flagcxRecord &operator=(const flagcxRecord &) = delete;
    flagcxRecord(flagcxRecord &&) = delete;
    flagcxRecord &operator=(flagcxRecord &&) = delete;

    ~flagcxRecord();
};

template <typename T>
flagcxRecord<T>::flagcxRecord() : duration(0.0f), stream(NULL) {
    deviceAdaptor->eventCreate(&begin_event);
    deviceAdaptor->eventCreate(&end_event);
}

template <typename T>
flagcxRecord<T>::~flagcxRecord<T>() {
    deviceAdaptor->eventDestroy(begin_event);
    deviceAdaptor->eventDestroy(end_event);
}

template <typename T>
class flagcxTimer {
  
  private:
    flagcxRecord<T> flagcxRecords[RECORD_NUM];
    pthread_t query_thread;
    bool stop_query = false;
    std::queue<flagcxRecord<T> *> available_records;  // NOLINT
    std::queue<flagcxRecord<T> *> using_records;      // NOLINT
    std::queue<flagcxRecord<T> *> profiling_records;  // NOLINT
    std::queue<flagcxRecord<T> *> profiled_records;   // NOLINT
    pthread_mutex_t mutex_available{};
    pthread_cond_t  cond_available{};
    pthread_mutex_t mutex_profiling{};
    pthread_cond_t  cond_profiling{};
    pthread_mutex_t mutex_profiled{};
    flagcxStream_t stream = NULL;

    void initSyncPrimitives();
    void destroySyncPrimitives();

  public:
    flagcxTimer();
    ~flagcxTimer();

    void start();
    void stop();

    flagcxResult_t begin(const RecordKey<T> &recordKey, flagcxStream_t stream, bool blocking=false);
    flagcxResult_t end(const RecordKey<T> &recordKey, bool blocking=false);

    float getRecord(const RecordKey<T> &recordKey, bool blocking=false);
    
};

template <typename T>
void flagcxTimer<T>::initSyncPrimitives() {
    pthread_mutex_init(&mutex_available,  nullptr);
    pthread_mutex_init(&mutex_profiling,  nullptr);
    pthread_mutex_init(&mutex_profiled,   nullptr);

    pthread_cond_init(&cond_available, nullptr);
    pthread_cond_init(&cond_profiling, nullptr);

    stream = nullptr;
}

template <typename T>
void flagcxTimer<T>::destroySyncPrimitives() {
    pthread_cond_destroy(&cond_available);
    pthread_cond_destroy(&cond_profiling);

    pthread_mutex_destroy(&mutex_available);
    pthread_mutex_destroy(&mutex_profiling);
    pthread_mutex_destroy(&mutex_profiled);
}

template <typename T>
void *flagcxQuery(void *flagcxTimer_) {
    auto *timer = reinterpret_cast<flagcxTimer<T> *>(flagcxTimer_);
    flagcxRecord<T> *curr_record = nullptr;

    while (true) {
        pthread_mutex_lock(&timer->mutex_profiling);
        while (!timer->stop_query && timer->profiling_records.empty()) {
            pthread_cond_wait(&timer->cond_profiling, &timer->mutex_profiling);
        }
        if (timer->stop_query && timer->profiling_records.empty() && !curr_record) {
            pthread_mutex_unlock(&timer->mutex_profiling);
            break;
        }
        if (!timer->profiling_records.empty()) {
            curr_record = timer->profiling_records.front();
        }
        pthread_mutex_unlock(&timer->mutex_profiling);

        while (curr_record) {
            flagcxResult_t res = flagcxSuccess;

            res = deviceAdaptor->eventQuery(curr_record->end_event);
            if(res != flagcxSuccess){
                WARN("Cannot query end event, will try again later");
                break;
            }

            res = deviceAdaptor->eventElapsedTime(&curr_record->duration,
                                                        curr_record->begin_event,
                                                        curr_record->end_event);  // ms

            if(res != flagcxSuccess){
                WARN("Cannot get elapsed time, will try again later");
                break;
            }

            pthread_mutex_lock(&timer->mutex_profiled);
            timer->profiled_records.push(curr_record);
            pthread_mutex_unlock(&timer->mutex_profiled);

            curr_record = nullptr;

            pthread_mutex_lock(&timer->mutex_profiling);
            timer->profiling_records.pop();
            pthread_mutex_unlock(&timer->mutex_profiling);
        }
    }
    return nullptr;
}

#endif
