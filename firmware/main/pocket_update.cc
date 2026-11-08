#include "pocket_update.h"
#include "application.h"
#include "board.h"
#include "display.h"
#include "lcd_display.h"
#include "pocket_clock.h"
#include "pocket_public_config.h"
#include "wifi_manager.h"
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace {
std::string Hex(const unsigned char* value){char text[65];for(int i=0;i<32;i++)snprintf(text+i*2,3,"%02x",value[i]);return text;}
bool Digest(const std::string& value){return value.size()==64&&std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
bool Version(const std::string& value){return !value.empty()&&value.size()<32&&std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isalnum(c)||c=='.'||c=='-'||c=='_';});}
bool Equal(const std::string& a,const std::string& b){if(a.size()!=b.size())return false;unsigned difference=0;for(size_t i=0;i<a.size();i++)difference|=a[i]^b[i];return difference==0;}
}

void PocketUpdate::Initialize(std::function<bool()> ready,std::function<bool()> health){
    ready_=std::move(ready);health_=std::move(health);boot_at_=esp_timer_get_time();
    Application::GetInstance().Schedule([this](){DisplayLockGuard lock(Board::GetInstance().GetDisplay());lv_timer_create([](lv_timer_t* timer){++static_cast<PocketUpdate*>(lv_timer_get_user_data(timer))->heartbeat_;},1000,this);});
}
void PocketUpdate::Abort(const char* reason){
    if(handle_)esp_ota_abort(handle_);
    handle_=0;if(buffer_)heap_caps_free(buffer_);buffer_=nullptr;
    mbedtls_sha256_free(&hash_);active_=false;stage_=4;error_=reason;reboot_at_=0;
}
bool PocketUpdate::Command(const std::string& command,const std::function<void(const std::string&)>& reply){
    std::istringstream args(command);std::string verb;args>>verb;
    if(!verb.starts_with("OTA_"))return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto fail=[&](const char* reason){reply(std::string("@PK ERR OTA_")+reason+"\n");};
    if(verb=="OTA_STATUS"){
        auto running=esp_ota_get_running_partition();esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
        esp_ota_get_state_partition(running,&state);
        char line[280];snprintf(line,sizeof(line),"@PK OTA_STATUS version=%s running=%s stage=%d active=%d received=%u size=%u healthy=%d pending_verify=%d battery_mv=%d error=%s\n",esp_app_get_description()->version,running->label,stage_,int(Active()),unsigned(received_),unsigned(size_),int(healthy_),int(state==ESP_OTA_IMG_PENDING_VERIFY),PocketBatteryMillivolts(),error_.empty()?"none":error_.c_str());reply(line);return true;
    }
    if(verb=="OTA_ABORT"){
        if(stage_==3){fail("COMMITTED");return true;}
        if(Active())Abort("cancelled");
        reply("@PK OK OTA_ABORT\n");return true;
    }
    if(verb=="OTA_BEGIN"){
        size_t size=0;std::string sha,version,mac,extra;args>>size>>sha>>version>>mac;
        if(!args||args>>extra||!Digest(sha)||!Digest(mac)||!Version(version)||size<512){fail("ARGS");return true;}
        auto payload="POCKET-OTA/1\n"+std::to_string(size)+"\n"+sha+"\n"+version+"\n";
        unsigned char digest[32];auto algorithm=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if(mbedtls_md_hmac(algorithm,reinterpret_cast<const unsigned char*>(kCodexToken),strlen(kCodexToken),reinterpret_cast<const unsigned char*>(payload.data()),payload.size(),digest)!=0||!Equal(Hex(digest),mac)){fail("AUTH");return true;}
        if(Active()){fail("BUSY");return true;}
        if(!healthy_||!ready_||!ready_()){fail("RETURN_HOME_OR_BUSY");return true;}
        if(version==esp_app_get_description()->version){fail("SAME_VERSION");return true;}
        target_=esp_ota_get_next_update_partition(nullptr);
        if(!target_||target_==esp_ota_get_running_partition()||size>target_->size){fail("SIZE");return true;}
        int level=0;bool charging=false,discharging=false;
        if(Board::GetInstance().GetBatteryLevel(level,charging,discharging)&&!charging&&level<20){fail("LOW_BATTERY");return true;}
        int millivolts=PocketBatteryMillivolts();
        if(millivolts<3550){fail(millivolts?"LOW_BATTERY":"POWER_UNKNOWN");return true;}
        // Flash operations may disable the external-memory cache. Keep the
        // small write buffer in internal RAM rather than forcing the driver
        // to copy PSRAM data through repeated 32-byte temporary writes.
        buffer_=static_cast<unsigned char*>(heap_caps_malloc(3072,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        if(!buffer_){fail("MEMORY");return true;}
        size_=size;digest_=sha;version_=version;received_=0;error_.clear();active_=true;stage_=1;touched_=esp_timer_get_time();
        auto err=esp_ota_begin(target_,OTA_WITH_SEQUENTIAL_WRITES,&handle_);
        if(err!=ESP_OK){Abort("begin");fail("BEGIN");return true;}
        mbedtls_sha256_init(&hash_);mbedtls_sha256_starts(&hash_,0);
        reply(std::string("@PK OK OTA_BEGIN ")+target_->label+"\n");return true;
    }
    if(!Active()||!handle_){fail("NOT_STARTED");return true;}
    if(verb=="OTA_DATA"){
        size_t offset=0,used=0;std::string encoded,extra;args>>offset>>encoded;
        if(!args||args>>extra||encoded.size()>4096||offset!=received_){fail("OFFSET");return true;}
        if(mbedtls_base64_decode(buffer_,3072,&used,reinterpret_cast<const unsigned char*>(encoded.data()),encoded.size())!=0||!used||received_+used>size_){Abort("data");fail("DATA");return true;}
        if(received_==0){
            constexpr size_t at=sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t);
            if(used<at+sizeof(esp_app_desc_t)){Abort("header");fail("HEADER");return true;}
            esp_app_desc_t app;memcpy(&app,buffer_+at,sizeof(app));
            if(buffer_[0]!=ESP_IMAGE_HEADER_MAGIC||app.magic_word!=ESP_APP_DESC_MAGIC_WORD||strncmp(app.project_name,esp_app_get_description()->project_name,sizeof(app.project_name))!=0||strncmp(app.version,version_.c_str(),sizeof(app.version))!=0){Abort("image");fail("IMAGE");return true;}
        }
        if(esp_ota_write(handle_,buffer_,used)!=ESP_OK){Abort("write");fail("WRITE");return true;}
        mbedtls_sha256_update(&hash_,buffer_,used);received_+=used;touched_=esp_timer_get_time();
        reply("@PK OK OTA_DATA "+std::to_string(received_)+"\n");return true;
    }
    if(verb=="OTA_COMMIT"){
        if(received_!=size_){fail("INCOMPLETE");return true;}
        unsigned char digest[32];mbedtls_sha256_finish(&hash_,digest);
        if(!Equal(Hex(digest),digest_)){Abort("hash");fail("HASH");return true;}
        auto handle=handle_;handle_=0;
        if(esp_ota_end(handle)!=ESP_OK){Abort("validate");fail("VALIDATE");return true;}
        if(esp_ota_set_boot_partition(target_)!=ESP_OK){Abort("boot");fail("BOOT");return true;}
        heap_caps_free(buffer_);buffer_=nullptr;mbedtls_sha256_free(&hash_);
        PocketClock::Get().Checkpoint(true);stage_=3;reboot_at_=esp_timer_get_time()+3000000;
        reply("@PK OK OTA_COMMIT "+version_+"\n");return true;
    }
    fail("COMMAND");return true;
}

void PocketUpdate::Tick(){
    bool active=false,reboot=false,failed=false;size_t received=0,total=0;std::string version;
    {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return;
        auto now=esp_timer_get_time();
        auto pulse=heartbeat_.load();if(pulse!=last_heartbeat_){last_heartbeat_=pulse;heartbeat_seen_=now;}
        if(!healthy_&&pulse>=5&&now-heartbeat_seen_<3000000&&now-boot_at_>15000000&&health_&&health_()&&WifiManager::GetInstance().IsConnected()){
            auto running=esp_ota_get_running_partition();esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;esp_ota_get_state_partition(running,&state);
            if(state!=ESP_OTA_IMG_PENDING_VERIFY||esp_ota_mark_app_valid_cancel_rollback()==ESP_OK)healthy_=true;
        }
        if(!healthy_&&now-boot_at_>90000000){
            esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;esp_ota_get_state_partition(esp_ota_get_running_partition(),&state);
            if(state==ESP_OTA_IMG_PENDING_VERIFY)esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        if(stage_==1&&now-touched_>45000000)Abort("timeout");
        active=Active();received=received_;total=size_;version=version_;
        reboot=stage_==3&&now>=reboot_at_;failed=stage_==4;
    }
    auto display=Board::GetInstance().GetDisplay();
    {DisplayLockGuard lock(display);
        if(active){
            if(!overlay_){
                overlay_=lv_obj_create(lv_display_get_layer_sys(static_cast<LcdDisplay*>(display)->GetLvDisplay()));
                lv_obj_remove_style_all(overlay_);lv_obj_set_size(overlay_,480,800);lv_obj_remove_flag(overlay_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(overlay_,LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_bg_color(overlay_,lv_color_hex(0x041521),0);lv_obj_set_style_bg_opa(overlay_,LV_OPA_COVER,0);
                label_=lv_label_create(overlay_);lv_obj_set_width(label_,424);lv_label_set_long_mode(label_,LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_font(label_,lv_obj_get_style_text_font(lv_screen_active(),LV_PART_MAIN),0);lv_obj_set_style_text_color(label_,lv_color_hex(0xffcf63),0);lv_obj_set_style_text_align(label_,LV_TEXT_ALIGN_CENTER,0);
            }
            char message[240];snprintf(message,sizeof(message),"无线更新\n%s\n\n%u%%\n\n正在安装，请保持电源\n录音和文件均保留",version.c_str(),total?unsigned(received*100/total):0);
            lv_label_set_text(label_,message);lv_obj_center(label_);lv_obj_move_foreground(overlay_);
        }else if(overlay_){lv_obj_delete(overlay_);overlay_=label_=nullptr;if(failed)display->ShowNotification("更新已取消，继续使用当前版本",5000);}
    }
    if(reboot)esp_restart();
}
