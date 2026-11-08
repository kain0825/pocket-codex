#include "pocket_clock.h"
#include "nvs.h"

void PocketClock::Initialize(){
    std::lock_guard<std::mutex> lock(mutex_);
    nvs_handle_t handle=0;int64_t saved=0;
    if(nvs_open("pocket_clock",NVS_READWRITE,&handle)==ESP_OK){nvs_=handle;nvs_get_i64(handle,"anchor",&saved);}
    if(!Valid(time(nullptr))&&Valid(saved)){
        timeval tv={.tv_sec=(time_t)saved,.tv_usec=0};
        if(settimeofday(&tv,nullptr)==0){source_=1;boot_restored_=true;}
    }else if(Valid(time(nullptr)))source_=4;
    approximate_=true;next_sync_us_=0;
    ESP_LOGI("PocketClock","Boot clock source=%s approximate=1",Source());
}
const char* PocketClock::Source() const{
    switch(source_.load()){case 1:return "saved";case 2:return "lan";case 3:return "usb";case 4:return "rtc";default:return "unknown";}
}
void PocketClock::SaveLocked(int64_t epoch){
    if(!nvs_||!Valid(epoch))return;
    if(nvs_set_i64(nvs_,"anchor",epoch)==ESP_OK&&nvs_commit(nvs_)==ESP_OK)last_checkpoint_us_=esp_timer_get_time();
    else ESP_LOGW("PocketClock","Time checkpoint failed; live clock continues");
}
bool PocketClock::Set(int64_t epoch,bool from_usb){
    if(!Valid(epoch))return false;
    std::lock_guard<std::mutex> lock(mutex_);
    timeval tv={.tv_sec=(time_t)epoch,.tv_usec=0};
    if(settimeofday(&tv,nullptr)!=0)return false;
    approximate_=false;source_=from_usb?3:2;next_sync_us_=esp_timer_get_time()+6LL*3600*1000000;
    SaveLocked(epoch);ESP_LOGI("PocketClock","Clock synchronized source=%s",Source());return true;
}
bool PocketClock::SyncDue(){std::lock_guard<std::mutex> lock(mutex_);return esp_timer_get_time()>=next_sync_us_;}
void PocketClock::RetryLater(){std::lock_guard<std::mutex> lock(mutex_);next_sync_us_=std::max(next_sync_us_,esp_timer_get_time()+300LL*1000000);}
void PocketClock::RequestSync(){if(!approximate_)return;std::lock_guard<std::mutex> lock(mutex_);next_sync_us_=0;}
void PocketClock::Checkpoint(bool force){
    std::lock_guard<std::mutex> lock(mutex_);
    if(force||esp_timer_get_time()-last_checkpoint_us_>=300LL*1000000)SaveLocked(time(nullptr));
}
