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
struct flagcxRecordKey {
    T value;

    flagcxRecordKey() = default;
    flagcxRecordKey(const T &v) : value(v) {}

    std::string debugStr() const {
        std::stringstream ss;
        ss << value;
        return ss.str();
    }

    bool operator<(const flagcxRecordKey<T> &other) const {
        return value < other.value;
    }
    bool operator==(const flagcxRecordKey<T> &other) const {
        return value == other.value;
    }
};



template <typename T>
struct flagcxRecord {
    flagcxEvent_t begin_event;
    flagcxEvent_t end_event;
    flagcxRecordKey<T> recordKey;
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
flagcxRecord<T>::flagcxRecord() : duration(0.0f){
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
  
  public:
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

    void initSyncPrimitives();
    void destroySyncPrimitives();

  public:
    flagcxTimer();
    ~flagcxTimer();

    void start();
    void stop();

    flagcxResult_t begin(const flagcxRecordKey<T> &recordKey, flagcxStream_t stream, bool blocking=false);
    flagcxResult_t end(const flagcxRecordKey<T> &recordKey, bool blocking=false);

    float getRecord(const flagcxRecordKey<T> &recordKey, bool blocking=false);
    
};

template <typename T>
void flagcxTimer<T>::initSyncPrimitives() {
    pthread_mutex_init(&mutex_available,  nullptr);
    pthread_mutex_init(&mutex_profiling,  nullptr);
    pthread_mutex_init(&mutex_profiled,   nullptr);

    pthread_cond_init(&cond_available, nullptr);
    pthread_cond_init(&cond_profiling, nullptr);
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
        curr_record = nullptr;
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
              if(res != flagcxInProgress){
                curr_record = nullptr;
                pthread_mutex_lock(&timer->mutex_profiling);
                timer->profiling_records.pop();
                pthread_mutex_unlock(&timer->mutex_profiling);
                WARN("Cannot query event, drop this record");
              }
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

template <typename T>
flagcxTimer<T>::flagcxTimer() {
    initSyncPrimitives();
    for (auto &rec : flagcxRecords) {
        this->available_records.push(&rec);
    }
}
template <typename T>
flagcxTimer<T>::~flagcxTimer() {
  if (!stop_query) {
    stop();
  }
  destroySyncPrimitives();
}

template <typename T>
void flagcxTimer<T>::start() {
    pthread_create(&query_thread, NULL, &flagcxQuery<T>, this);
    INFO(FLAGCX_TUNING, "flagcx timer start profiling thread");
}

template <typename T>
void flagcxTimer<T>::stop() {
    INFO(FLAGCX_TUNING, "stopping timer");
    pthread_mutex_lock(&this->mutex_profiling);
    stop_query = true;
    pthread_cond_signal(&this->cond_profiling);
    pthread_mutex_unlock(&this->mutex_profiling);
    pthread_join(query_thread, NULL);

}

template <typename T>
float flagcxTimer<T>::getRecord(const flagcxRecordKey<T> &recordKey, bool blocking) {
    std::vector<flagcxRecord<T> *> temp_available_records;

    while (blocking) {
        pthread_mutex_lock(&this->mutex_profiling);
        if (this->profiling_records.empty()) {
            pthread_mutex_unlock(&this->mutex_profiling);
            break;
        }
        pthread_mutex_unlock(&this->mutex_profiling);
    }

    float duration = -1.0f;
    flagcxRecord<T> *found_record = nullptr;
    std::queue<flagcxRecord<T> *> remaining_records;

    pthread_mutex_lock(&this->mutex_profiled);
    while (!this->profiled_records.empty()) {
        flagcxRecord<T> *record = this->profiled_records.front();
        this->profiled_records.pop();
        if (found_record == nullptr && record->recordKey == recordKey) {
            found_record = record;
        } else {
            remaining_records.push(record);
        }
    }
    this->profiled_records.swap(remaining_records);
    pthread_mutex_unlock(&this->mutex_profiled);

    if (found_record) {
        duration = found_record->duration;
        pthread_mutex_lock(&this->mutex_available);
        this->available_records.push(found_record);
        pthread_cond_signal(&this->cond_available);
        pthread_mutex_unlock(&this->mutex_available);
        return duration;
    }

    return -1.0f; // Indicate that no matching record was found
}

template <typename T>
flagcxResult_t flagcxTimer<T>::begin(const flagcxRecordKey<T> &recordKey, flagcxStream_t stream_, bool blocking) {
    flagcxRecord<T> *record = nullptr;

    pthread_mutex_lock(&this->mutex_available);
    while (available_records.empty() && blocking) {
        WARN("Logger::LogSubSys::TIMER: flagcx event is empty!");
        pthread_cond_wait(&this->cond_available, &this->mutex_available);
    }
    if (!available_records.empty()) {
        record = available_records.front();
        available_records.pop();
    }
    pthread_mutex_unlock(&this->mutex_available);

    if (record) {
        record->recordKey = recordKey;
        FLAGCXCHECK(deviceAdaptor->eventRecord(record->begin_event, record->stream));
        using_records.push(record);
    }
    
    return flagcxSuccess;
}

template <typename T>
flagcxResult_t flagcxTimer<T>::end(const flagcxRecordKey<T> &recordKey, bool blocking) {
    if (using_records.empty()) {
        return flagcxInvalidUsage;
    }

    // Find the record with recordKey
    flagcxRecord<T> *record = nullptr;
    std::queue<flagcxRecord<T> *> using_records_copy;

    while (!using_records.empty()) {
        record = using_records.front();
        using_records.pop();
        if (record->recordKey == recordKey) {
            // Record found, update the end_event and add it back to using_records
            FLAGCXCHECK(deviceAdaptor->eventRecord(record->end_event, record->stream));
            break;
        } else {
            WARN("Logger::LogSubSys::TIMER: begin-end is not a pair");
            record = nullptr;
            // Record not found, keep it in using_records
            using_records_copy.push(record);
        }
    }

    // Add the records from using_records_copy to using_records
    while (!using_records.empty()) {
        using_records_copy.push(using_records.front());
        using_records.pop();
    }
    using_records = using_records_copy;

    if (record == nullptr) {
        WARN("Logger::LogSubSys::TIMER: no matching begin for end call");
        return flagcxInvalidUsage;
    }

    if (blocking) {
        FLAGCXCHECK(deviceAdaptor->streamSynchronize(record->stream));
    }

    pthread_mutex_lock(&this->mutex_profiling);
    this->profiling_records.push(record);
    pthread_cond_signal(&this->cond_profiling);
    pthread_mutex_unlock(&this->mutex_profiling);

    return flagcxSuccess;
}

#endif
