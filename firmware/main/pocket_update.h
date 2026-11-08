#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <lvgl.h>

// Bounded, paired-workstation application OTA. No bootloader/assets/SD writes.
class PocketUpdate {
public:
    static PocketUpdate& Get(){static PocketUpdate instance;return instance;}
    bool Active()const{return active_.load();}
    void Initialize(std::function<bool()> ready,std::function<bool()> health);
    bool Command(const std::string& command,const std::function<void(const std::string&)>& reply);
    void Tick();
private:
    void Abort(const char* reason);
    std::mutex mutex_;
    std::function<bool()> ready_;
    std::function<bool()> health_;
    std::atomic<bool> active_{false};
    esp_ota_handle_t handle_=0;
    const esp_partition_t* target_=nullptr;
    size_t size_=0,received_=0;
    int stage_=0;
    int64_t touched_=0,reboot_at_=0,boot_at_=0;
    bool healthy_=false;
    std::atomic<unsigned> heartbeat_{0};
    unsigned last_heartbeat_=0;
    int64_t heartbeat_seen_=0;
    std::string digest_,version_,error_;
    unsigned char* buffer_=nullptr;
    mbedtls_sha256_context hash_={};
    lv_obj_t *overlay_=nullptr,*label_=nullptr;
};
