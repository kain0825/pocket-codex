#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>

// Wall-clock ownership is separate from USB, Codex pages and recorder UI.
class PocketClock {
public:
    static PocketClock& Get(){static PocketClock value;return value;}
    static bool Valid(int64_t epoch){return epoch>=1767225600LL&&epoch<4102444800LL;}
    void Initialize();
    bool Set(int64_t epoch,bool from_usb=false);
    bool SyncDue();
    void RetryLater();
    void RequestSync();
    void Checkpoint(bool force=false);
    bool Approximate() const{return approximate_.load();}
    bool BootRestored() const{return boot_restored_.load();}
    const char* Source() const;
private:
    std::mutex mutex_;
    std::atomic<bool> approximate_{true};
    std::atomic<bool> boot_restored_{false};
    std::atomic<int> source_{0};
    int64_t next_sync_us_=0,last_checkpoint_us_=0;
    uint32_t nvs_=0;
    void SaveLocked(int64_t epoch);
};
