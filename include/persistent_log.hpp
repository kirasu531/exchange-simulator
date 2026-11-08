#pragma once

#include "matching_engine.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class PersistentLog {
private:
    std::string path;

    int fd = -1;
    int sync_interval_ms = -1;

    bool enabled = false, dirty = false;
    std::atomic <bool> sync_running;

    std::mutex mutex;
    std::thread sync_thread;

    void SyncLoop();

public:
    PersistentLog(const std::string &path, int sync_interval_ms);

    bool Open();
    void Replay(MatchingEngine &engine);

    void StartSync();
    void StopSync();

    bool Append(const std::string &line);

    void Close();

    ~PersistentLog();
};
