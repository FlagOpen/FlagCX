#include "timer.h"


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
float flagcxTimer<T>::getRecord(const RecordKey<T> &recordKey, bool blocking) {
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
flagcxResult_t flagcxTimer<T>::begin(const RecordKey<T> &recordKey, flagcxStream_t stream_, bool blocking) {
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
        record->stream = (flagcxStream_t)stream_;
        FLAGCXCHECK(deviceAdaptor->eventRecord(record->begin_event, record->stream));
        using_records.push(record);
    }
}

template <typename T>
flagcxResult_t flagcxTimer<T>::end(const RecordKey<T> &recordKey, bool blocking) {
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
}

