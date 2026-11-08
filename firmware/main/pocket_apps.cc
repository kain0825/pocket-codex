#include "pocket_public_config.h"
#include "esp_random.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include "wifi_manager.h"
#include <cmath>
#include "pocket_apps.h"
#include "pocket_answer_book.h"
#include "pocket_tools.h"
#include "codex_input_policy.h"
#include "pocket_theme.h"
#include "application.h"
#include "board.h"
#include "display.h"
#include "lvgl_theme.h"
#include "lcd_display.h"
#include "esp_lvgl_port.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <sys/time.h>
#include <cstdlib>
#include <esp_heap_caps.h>

static constexpr const char* TAG = "PocketApps";
// Bounded text and key helpers used by Codex's own tabs and drafts.
static std::string ReadText(const std::string& path){
    FILE* file=fopen(path.c_str(),"rb");if(!file)return {};
    fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    if(size<0||size>128*1024){fclose(file);return {};}
    std::string text(size_t(size),'\0');text.resize(fread(text.data(),1,text.size(),file));fclose(file);return text;
}
static std::string Hash(const std::string& value){
    unsigned char digest[32];mbedtls_sha256(reinterpret_cast<const unsigned char*>(value.data()),value.size(),digest,0);
    char text[65];for(int i=0;i<32;i++)snprintf(text+i*2,3,"%02x",digest[i]);return text;
}

#include "pocket_clock.inc"
static bool ClockReady() { return PocketClock::Valid(time(nullptr)); }
static std::string ClockText() {
    if(!ClockReady())return "可直接录音，联网后自动校时";
    time_t now=time(nullptr);struct tm local={};localtime_r(&now,&local);
    char text[40];strftime(text,sizeof(text),"%Y-%m-%d %H:%M:%S",&local);
    return std::string(PocketClock::Get().Approximate()?"北京时间 ~":"北京时间 ")+text;
}
static lv_obj_t* Label(lv_obj_t* parent,const std::string& text,uint32_t color=0xE8EDF2) {
    auto label=lv_label_create(parent);lv_label_set_text(label,text.c_str());
    lv_obj_set_width(label,LV_PCT(100));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label,lv_color_hex(color),0);return label;
}
static lv_obj_t* Button(lv_obj_t* parent,const std::string& text,lv_event_cb_t callback,void* data=nullptr) {
    auto button=lv_button_create(parent);lv_obj_set_width(button,LV_PCT(100));
    lv_obj_set_height(button,64);lv_obj_set_style_radius(button,12,0);
    lv_obj_set_style_bg_color(button,lv_color_hex(0x263E50),0);
    if(PocketNeon())PocketPanel(button);
    auto label=lv_label_create(button);lv_label_set_text(label,text.c_str());
    lv_obj_set_width(label,LV_PCT(100));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label);
    if(callback)lv_obj_add_event_cb(button,callback,LV_EVENT_CLICKED,data);
    return button;
}

// Life game is excluded from the recorder build.
void PocketApps::OpenLife() {}
#include "codex_tab_registry.inc"
static size_t CodexEditorByteOffset(const char* text,uint32_t characters);
#include "pocket_diagnostics.inc"


void PocketApps::Initialize() {
    setenv("TZ","CST-8",1);tzset();
    PocketClock::Get().Initialize();
    PocketUpdate::Get().Initialize([this](){return mounted_&&!recording_&&!reader_active_&&!sleeping_&&!preparing_sleep_&&!open_&&!Application::GetInstance().IsChatAppOpen();},[this](){return mount_done_&&mounted_;});
    xTaskCreate([](void* arg){auto self=(PocketApps*)arg;self->MountStorage();self->UsbTask();vTaskDelete(nullptr);},
        "pocket_storage",8192,this,3,nullptr);
    if(!monitor_task_started_)monitor_task_started_=xTaskCreate([](void* a){static_cast<PocketApps*>(a)->MonitorWorker();},"pc_monitor",12288,this,2,nullptr)==pdPASS;
}

void PocketApps::MountStorage() {
    tinyusb_msc_driver_config_t msc={};
    msc.callback=[](tinyusb_msc_storage_handle_t, tinyusb_msc_event_t* event, void* arg){
        auto self=static_cast<PocketApps*>(arg);
        if(event->id==TINYUSB_MSC_EVENT_MOUNT_COMPLETE){
            self->msc_app_owned_=event->mount_point==TINYUSB_MSC_STORAGE_MOUNT_APP;
            self->msc_mount_error_=false;
            ESP_LOGI(TAG,"Card ownership: %s",self->msc_app_owned_?"APP":"USB");
        } else if(event->id==TINYUSB_MSC_EVENT_MOUNT_FAILED || event->id==TINYUSB_MSC_EVENT_FORMAT_REQUIRED || event->id==TINYUSB_MSC_EVENT_FORMAT_FAILED){
            self->msc_mount_error_=true;self->msc_app_owned_=false;
            ESP_LOGE(TAG,"Card mount failed; formatting disabled");
        }
    };
    msc.callback_arg=this;
    esp_err_t err=tinyusb_msc_install_driver(&msc);
    if(err!=ESP_OK){mount_done_=true;return;}
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.slot=SDMMC_HOST_SLOT_0;host.max_freq_khz=SDMMC_FREQ_DEFAULT;
    sd_pwr_ctrl_ldo_config_t power={.ldo_chan_id=4};sd_pwr_ctrl_handle_t handle=nullptr;
    err=sd_pwr_ctrl_new_on_chip_ldo(&power,&handle);
    if(err==ESP_OK){
        host.pwr_ctrl_handle=handle;
        sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=4;slot.flags=0;
        auto card=static_cast<sdmmc_card_t*>(calloc(1,sizeof(sdmmc_card_t)));
        err=card?host.init():ESP_ERR_NO_MEM;
        if(err==ESP_OK)err=sdmmc_host_init_slot(host.slot,&slot);
        if(err==ESP_OK)err=sdmmc_card_init(&host,card);
        if(err==ESP_OK){
            tinyusb_msc_storage_config_t config={};
            config.medium.card=card;config.mount_point=TINYUSB_MSC_STORAGE_MOUNT_APP;
            config.fat_fs.base_path=const_cast<char*>("/sdcard");config.fat_fs.config.max_files=8;
            config.fat_fs.config.format_if_mount_failed=false;config.fat_fs.do_not_format=true;
            err=tinyusb_msc_new_storage_sdmmc(&config,&msc_storage_);
            if(msc_mount_error_)err=ESP_FAIL;
        }
        if(err==ESP_OK){
            unsigned char sector[512];
            if(sdmmc_read_sectors(card,sector,0,1)==ESP_OK){
                uint32_t lba=uint32_t(sector[454])|(uint32_t(sector[455])<<8)|(uint32_t(sector[456])<<16)|(uint32_t(sector[457])<<24);
                if(sector[0]!=0xEB&&sector[0]!=0xE9&&lba>0)sdmmc_read_sectors(card,sector,lba,1);
                ESP_LOGI(TAG,"Card boot format: %.8s",sector+3);
            }
            mkdir("/sdcard/PKOS",0775);mkdir("/sdcard/PKOS/CODEX",0775);
            struct stat st={};bool usable=stat("/sdcard/PKOS/CODEX",&st)==0&&S_ISDIR(st.st_mode);
            mounted_=usable;
            ESP_LOGI(TAG,"SD mounted; directories ready=%d; capacity=%lu MB",int(usable),
                (unsigned long)(uint64_t(card->csd.capacity)*card->csd.sector_size/(1024*1024)));
        }
    }
    if(err!=ESP_OK)ESP_LOGE(TAG,"SD mount failed: %s (formatting is disabled)",esp_err_to_name(err));
    mount_done_=true;
}

void PocketApps::MakeRoot(const char* title) {
    PocketCalculator::Get().Close();
    PocketWoodfish::Get().Close();
    PocketAnswerBook::Get().Close();
    ++navigation_epoch_;navigation_changed_us_=esp_timer_get_time();
    app_title_=title;
    recorder_page_=false;
    Board::GetInstance().GetDisplay()->ShowLocalApp();
    if(root_)lv_obj_delete(root_);
    ReleaseCodexProjectCaches();
    root_=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_,480,800);lv_obj_set_style_bg_opa(root_,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(root_,lv_color_hex(PocketNeon()?0x080D1B:0x101820),0);
    auto theme=static_cast<LvglTheme*>(Board::GetInstance().GetDisplay()->GetTheme());
    lv_obj_set_style_text_font(root_,theme->text_font()->font(),0);
    lv_obj_set_style_text_color(root_,lv_color_hex(0xE8EDF2),0);
    lv_obj_remove_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
    auto back=Button(root_,"返回",[](lv_event_t*){PocketApps::GetInstance().Back();});
    lv_obj_set_pos(back,24,40);lv_obj_set_size(back,116,48);
    auto heading=lv_label_create(root_);lv_label_set_text(heading,title);lv_obj_set_pos(heading,156,50);
    if(PocketNeon())PocketPixel(root_,24,96,432,2,0x32E6CF);
    body_=lv_obj_create(root_);lv_obj_set_pos(body_,24,104);lv_obj_set_size(body_,432,684);
    lv_obj_set_style_bg_opa(body_,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(body_,0,0);
    lv_obj_set_style_pad_all(body_,12,0);lv_obj_set_style_pad_row(body_,16,0);
    lv_obj_set_flex_flow(body_,LV_FLEX_FLOW_COLUMN);lv_obj_set_scroll_dir(body_,LV_DIR_VER);
    for(int side=0;side<2;side++){
        auto rail=lv_obj_create(root_);edge_rails_[side]=rail;lv_obj_remove_style_all(rail);
        lv_obj_set_pos(rail,side?460:0,32);lv_obj_set_size(rail,20,768);
        lv_obj_add_flag(rail,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(rail,LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_remove_flag(rail,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(rail,[](lv_event_t* e){
            static lv_point_t origin={};static bool armed=false;static uint32_t epoch=0;
            auto& self=PocketApps::GetInstance();auto indev=lv_indev_active();if(!indev)return;
            lv_point_t point;lv_indev_get_point(indev,&point);
            auto code=lv_event_get_code(e);
            if(code==LV_EVENT_PRESSED){origin=point;epoch=self.navigation_epoch_;armed=true;}
            if(code==LV_EVENT_PRESS_LOST){armed=false;}
            if(code==LV_EVENT_RELEASED){
                if(!armed){return;}armed=false;
                int dx=point.x-origin.x,dy=point.y-origin.y;
                bool inward=(origin.x<=20&&dx>=60)||(origin.x>=460&&dx<=-60);
                if(inward&&abs(dx)>abs(dy)*2){
                    ESP_LOGI(TAG,"Edge back epoch=%lu dx=%d dy=%d",(unsigned long)epoch,dx,dy);
                    auto expected=epoch;Application::GetInstance().Schedule([expected](){auto& s=PocketApps::GetInstance();if(s.navigation_epoch_==expected)s.Back();});
                }
            }
        },LV_EVENT_ALL,nullptr);
    }
    open_=true;
}

bool PocketApps::RestoreOpenApp() {
    if(IsUpdating())return true;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if(!open_||!root_)return false;
    Board::GetInstance().GetDisplay()->ShowLocalApp();
    lv_obj_remove_flag(root_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    return true;
}

void PocketApps::Close(bool stop,bool show_home) {
    ++navigation_epoch_;
    if(show_home){std::lock_guard<std::mutex> g(codex_mutex_);codex_select_id_.clear();codex_select_title_.clear();}
    if(codex_active_&&!codex_dashboard_){SaveCodexPage();SaveCodexDraft();}
    CloseCodexQuestion();
    if(codex_editor_)FinishCodexEdit(true);
    if(codex_editor_)return; // A failed save must leave the editor and its text alive.
    if(codex_active_){StopRecording();codex_active_=false;int pending=codex_pending_.exchange(0);std::lock_guard<std::mutex> g(codex_mutex_);codex_auto_send_=false;if(codex_continue_pending_){codex_continue_pending_=false;codex_continue_waiting_=false;codex_control_feedback_="已取消尚未发送的继续";}if(pending)codex_state_=pending>=2&&!codex_draft_.empty()?4:0;}
    if(codex_timer_){lv_timer_delete(codex_timer_);codex_timer_=nullptr;}
    codex_status_=nullptr;codex_reply_label_=nullptr;codex_draft_label_=nullptr;
    codex_question_button_=nullptr;codex_stop_button_=nullptr;codex_reply_frame_=nullptr;codex_reply_panel_=nullptr;codex_draft_panel_=nullptr;codex_draft_body_=nullptr;codex_draft_heading_=nullptr;
    codex_send_button_=nullptr;codex_continue_button_=nullptr;codex_cancel_button_=nullptr;codex_tabs_panel_=nullptr;codex_tab_widgets_.clear();codex_tabs_drawn_.clear();
    codex_tab_prev_=nullptr;codex_tab_next_=nullptr;codex_tab_page_label_=nullptr;codex_tab_drawn_page_=-1;
    codex_close_tab_id_.clear();
    codex_tab_touch_=false;codex_tab_moved_=false;
    codex_draft_touch_=false;codex_draft_moved_=false;
    codex_state_panel_=nullptr;codex_state_label_=nullptr;codex_state_dot_=nullptr;codex_mode_buttons_[0]=nullptr;codex_mode_buttons_[1]=nullptr;codex_mode_hint_=nullptr;codex_editing_=false;codex_chat_layout_=-1;codex_actions_layout_=-1;codex_rendered_messages_.clear();
    codex_task_list_=nullptr;codex_task_status_=nullptr;codex_return_pending_=false;codex_dashboard_modes_[0]=codex_dashboard_modes_[1]=nullptr;codex_dashboard_touch_=false;codex_dashboard_swipe_=false;

    monitor_active_=false;monitor_pending_=0;monitor_busy_=false;monitor_dialog_=nullptr;
    if(monitor_timer_){lv_timer_delete(monitor_timer_);monitor_timer_=nullptr;}
    monitor_label_=nullptr;
    if(reader_active_&&!ExitCardReader())return;
    if(life_timer_){lv_timer_delete(life_timer_);life_timer_=nullptr;}
    if(stop)StopRecording();
    EndComputerVoice();
    if(pc_timer_){lv_timer_delete(pc_timer_);pc_timer_=nullptr;}
    pc_label_=nullptr;pc_focus_=nullptr;pc_dictate_=nullptr;pc_send_=nullptr;
    if(record_timer_){lv_timer_delete(record_timer_);record_timer_=nullptr;}
    PocketAnswerBook::Get().Close();
    PocketCalculator::Get().Close();
    PocketWoodfish::Get().Close();
    if(root_){lv_obj_delete(root_);root_=nullptr;body_=nullptr;}
    ReleaseCodexProjectCaches();
    record_label_=nullptr;record_button_label_=nullptr;record_stop_button_=nullptr;clock_label_=nullptr;clock_input_=nullptr;open_=false;recorder_page_=false;app_title_.clear();
    static_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())->SetInteractiveRefresh(false);
    if(show_home)Board::GetInstance().GetDisplay()->ShowLauncher(true);
    ESP_LOGI(TAG,"Local app closed; home=%d",int(show_home));
}

void PocketApps::OpenComputerControl() {}
void PocketApps::SendComputerAction(int) {}
void PocketApps::EndComputerVoice() {}
void PocketApps::RefreshComputerControl() {}

void PocketApps::OpenCardReader() {
    if(reader_active_||recording_){RestoreOpenApp();return;}
    auto& app=Application::GetInstance();app.ReturnToHome();
    app.Schedule([this](){
        DisplayLockGuard display_lock(Board::GetInstance().GetDisplay());
        if(RestoreOpenApp())return;
        std::lock_guard<std::mutex> storage_lock(storage_mutex_);
        MakeRoot("读卡器模式");
        if(!mounted_||!msc_storage_){Label(body_,"存储卡暂时无法读取。");return;}
        Label(body_,"把存储卡交给电脑使用",0x80D8CA);
        Label(body_,"请将数据线接到板子的另一个 Type-C 口。电脑识别后，会出现存储卡盘符。");
        Label(body_,"此模式暂停本机存储卡使用，请勿在电脑传输时退出。");
        reader_label_=Label(body_,"正在连接电脑……",0xD8BC80);
        Label(body_,"完成复制后，请先在电脑安全弹出磁盘，再点退出。不能直接在传输中退出。",0xD8BC80);
        Button(body_,"退出读卡器",[](lv_event_t*){PocketApps::GetInstance().Close();});
        reader_confirm_button_=Button(body_,"电脑已安全弹出，确认退出",[](lv_event_t*){
            auto& self=PocketApps::GetInstance();
            if(self.reader_active_ && self.ExitCardReader(true))self.Close();
        });
        lv_obj_set_height(reader_confirm_button_,88);
        lv_obj_add_flag(reader_confirm_button_,LV_OBJ_FLAG_HIDDEN);
        reader_exit_requested_=false;
        reader_active_=true;mounted_=false;upload_name_.clear();upload_buffer_.clear();
        tinyusb_msc_set_storage_mount_point(msc_storage_,TINYUSB_MSC_STORAGE_MOUNT_USB);
        if(msc_mount_error_||msc_app_owned_){reader_active_=false;lv_label_set_text(reader_label_,"存储卡切换失败，请重启后再试。");return;}
        tinyusb_config_t config=TINYUSB_DEFAULT_CONFIG();
        esp_err_t err=tinyusb_driver_install(&config);
        if(err!=ESP_OK){
            tinyusb_msc_set_storage_mount_point(msc_storage_,TINYUSB_MSC_STORAGE_MOUNT_APP);
            mounted_=msc_app_owned_&&!msc_mount_error_;reader_active_=false;
            lv_label_set_text(reader_label_,"连接启动失败，请重启后再试。");return;
        }
        usb_installed_=true;
        reader_timer_=lv_timer_create([](lv_timer_t*){PocketApps::GetInstance().RefreshCardReader();},500,nullptr);
        RefreshCardReader();ESP_LOGI(TAG,"READER active; high-speed USB; local access blocked");
    });
}

void PocketApps::RefreshCardReader() {
    if(!reader_label_||reader_exit_requested_)return;
    const char* text=msc_mount_error_?"存储卡读取异常，请先在电脑安全弹出。":
        msc_app_owned_?"电脑已释放存储卡，可以退出。":
        tud_mounted()?"电脑正在使用存储卡，请在资源管理器中操作。":"等待读卡器连接，请检查 Type-C 接口。";
    lv_label_set_text(reader_label_,text);
}

bool PocketApps::ExitCardReader(bool confirmed) {
    if(!confirmed&&usb_installed_&&tud_connected()&&!msc_app_owned_){
        reader_exit_requested_=true;
        if(reader_label_)lv_label_set_text(reader_label_,"请先在电脑安全弹出。如果电脑已完成弹出，请点下方确认退出。");
        if(reader_confirm_button_){lv_obj_remove_flag(reader_confirm_button_,LV_OBJ_FLAG_HIDDEN);lv_obj_scroll_to_view(reader_confirm_button_,LV_ANIM_OFF);}
        return false;
    }
    std::lock_guard<std::mutex> storage_lock(storage_mutex_);
    if(usb_installed_){
        // The user has ejected in Windows, or the host has released/disconnected.
        // Disconnect first while the TinyUSB task is alive to drain queued writes.
        tud_disconnect();vTaskDelay(pdMS_TO_TICKS(250));
        esp_err_t err=tinyusb_driver_uninstall();
        if(err!=ESP_OK){lv_label_set_text(reader_label_,"连接尚未结束，请稍后再试。");return false;}
        usb_installed_=false;
    }
    tinyusb_msc_set_storage_mount_point(msc_storage_,TINYUSB_MSC_STORAGE_MOUNT_APP);
    struct stat st={};mounted_=msc_app_owned_&&!msc_mount_error_&&stat("/sdcard",&st)==0;
    if(reader_timer_){lv_timer_delete(reader_timer_);reader_timer_=nullptr;}
    reader_label_=nullptr;reader_confirm_button_=nullptr;reader_exit_requested_=false;reader_active_=false;
    ESP_LOGI(TAG,"READER exited; local storage ready=%d",int(mounted_.load()));
    return true;
}



void PocketApps::WriteWavHeader() {
    const uint32_t bytes=record_bytes_.load();unsigned char header[44]={};
    auto u16=[&](int p,uint16_t n){header[p]=n&255;header[p+1]=n>>8;};
    auto u32=[&](int p,uint32_t n){for(int k=0;k<4;++k)header[p+k]=(n>>(8*k))&255;};
    memcpy(header,"RIFF",4);u32(4,36+bytes);memcpy(header+8,"WAVEfmt ",8);u32(16,16);u16(20,1);u16(22,1);
    u32(24,16000);u32(28,32000);u16(32,2);u16(34,16);memcpy(header+36,"data",4);u32(40,bytes);
    fseek(record_file_,0,SEEK_SET);
    if(fwrite(header,1,sizeof(header),record_file_)!=sizeof(header))record_error_=true;
    fseek(record_file_,0,SEEK_END);
}

void PocketApps::StartRecording() {
    if(!codex_active_||recording_||sleeping_||preparing_sleep_||reader_active_)return;
    if(!mounted_)return;
    std::unique_lock<std::mutex> lock(storage_mutex_);
    if(IsUpdating()||sleeping_||preparing_sleep_||reader_active_)return;
    if(!record_ring_)record_ring_=static_cast<uint8_t*>(heap_caps_malloc(kRecordRingBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!record_ring_){record_message_="录音缓冲空间不足";return;}
    if(!record_writer_&&xTaskCreatePinnedToCore([](void* a){static_cast<PocketApps*>(a)->RecordWriter();},"record_writer",6144,this,4,&record_writer_,1)!=pdPASS){record_message_="录音写入任务未启动";return;}
    if(record_test_mode_)mkdir("/sdcard/PKOS/TEST",0775);
    time_t started=time(nullptr);struct tm local={};localtime_r(&started,&local);
    char stamp[24]={};if(ClockReady())strftime(stamp,sizeof(stamp),"%Y%m%d_%H%M%S",&local);
    for(unsigned i=0;i<9999999;++i){
        char name[64];
        if(stamp[0]) {
            if(i==0)snprintf(name,sizeof(name),"%s.WAV",stamp);
            else snprintf(name,sizeof(name),"%s_%u.WAV",stamp,i);
        } else snprintf(name,sizeof(name),"UNSET_%07u.WAV",i+1);
        auto path=(record_test_mode_?std::string("/sdcard/PKOS/TEST/"):CodexRecordDirectory())+name;
        int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0664);
        if(fd<0){if(errno==EEXIST)continue;record_message_="无法创建录音文件，请检查卡片剩余空间。";RefreshRecorder();return;}
        record_file_=fdopen(fd,"wb");if(!record_file_)::close(fd);record_name_=name;record_path_=path;break;
    }
    if(!record_file_){record_message_="无法开始录音。";RefreshRecorder();return;}
    audio_level_=0;audio_rms_=0;record_signal_samples_=0;
    setvbuf(record_file_,nullptr,_IOFBF,8192);
    record_ring_read_=0;record_ring_write_=0;record_high_water_=0;record_dropped_=0;
    record_write_max_us_=0;record_sync_max_us_=0;record_callback_max_us_=0;
    record_first_sample_us_=0;record_started_us_=0;record_ended_us_=0;
    auto codec=Board::GetInstance().GetAudioCodec();record_input_overruns_begin_=codec->input_overruns();record_input_errors_begin_=codec->input_errors();
    record_limit_bytes_=codex_active_?kCodexVoiceMaxBytes:32000U*3600;
    record_bytes_=0;flush_bytes_=0;record_error_=false;WriteWavHeader();
    if(record_error_){fclose(record_file_);record_file_=nullptr;record_message_="存储卡写入失败。";RefreshRecorder();return;}
    record_writer_stop_=false;record_writer_done_=false;record_started_us_=esp_timer_get_time();recording_=true;
    lock.unlock();
    Application::GetInstance().GetAudioService().StartLocalRecording([this](const std::vector<int16_t>& pcm){WritePcm(pcm);});
    xTaskNotifyGive(record_writer_);
    ESP_LOGI(TAG,"Recording started: %s",record_name_.c_str());RefreshRecorder();
}

void PocketApps::WritePcm(const std::vector<int16_t>& pcm) {
    if(!recording_||record_error_||!record_file_)return;
    auto began=esp_timer_get_time();
    if(!record_first_sample_us_.load()){
        auto codec=Board::GetInstance().GetAudioCodec();record_input_overruns_begin_=codec->input_overruns();record_input_errors_begin_=codec->input_errors();record_first_sample_us_=began;
    }
    // Read-only metering from the same PCM that is written; no extra audio capture or UI work here.
    if(!pcm.empty()){
        uint64_t energy=0;for(int16_t sample:pcm){int32_t v=sample;energy+=int64_t(v)*v;}
        unsigned rms=unsigned(std::sqrt(double(energy)/pcm.size()));audio_rms_=rms;
        if(rms>PocketInput::MeterFloor)record_signal_samples_+=pcm.size();
        double normalized=rms>PocketInput::MeterFloor?std::sqrt(double(rms-PocketInput::MeterFloor)/6000.0)*100.0:0;
        audio_level_=unsigned(std::min(100.0,normalized));
    }
    uint32_t write=record_ring_write_.load(std::memory_order_relaxed),read=record_ring_read_.load(std::memory_order_acquire);
    if(write>=record_limit_bytes_)return;
    size_t wanted=std::min(pcm.size()*sizeof(int16_t),size_t(record_limit_bytes_-write));
    if(wanted>kRecordRingBytes-(write-read)){record_dropped_+=wanted;record_error_=true;return;}
    size_t offset=write&(kRecordRingBytes-1),first=std::min(wanted,size_t(kRecordRingBytes-offset));
    memcpy(record_ring_+offset,pcm.data(),first);if(first<wanted)memcpy(record_ring_,reinterpret_cast<const uint8_t*>(pcm.data())+first,wanted-first);
    record_ring_write_.store(write+wanted,std::memory_order_release);
    record_high_water_=std::max(record_high_water_.load(),uint32_t(write+wanted-read));
    record_callback_max_us_=std::max(record_callback_max_us_.load(),uint32_t(esp_timer_get_time()-began));
    xTaskNotifyGive(record_writer_);
}

void PocketApps::RecordWriter(){
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        for(;;){
            uint32_t read=record_ring_read_.load(std::memory_order_relaxed),write=record_ring_write_.load(std::memory_order_acquire),available=write-read;
            if(!available){if(record_writer_stop_)record_writer_done_=true;break;}
            if(available<4096&&!record_writer_stop_){ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(80));continue;}
            size_t offset=read&(kRecordRingBytes-1),wanted=std::min(size_t(available),std::min(size_t(8192),size_t(kRecordRingBytes-offset)));
            auto started=esp_timer_get_time();size_t written=fwrite(record_ring_+offset,1,wanted,record_file_);
            record_write_max_us_=std::max(record_write_max_us_.load(),uint32_t(esp_timer_get_time()-started));
            record_bytes_+=written;if(written!=wanted)record_error_=true;
            // A failed disk write must terminate cleanly without blocking the capture task forever.
            record_ring_read_.store(read+wanted,std::memory_order_release);
            if(record_bytes_.load()-flush_bytes_>=160000){
                started=esp_timer_get_time();WriteWavHeader();if(fflush(record_file_)!=0||fsync(fileno(record_file_))!=0)record_error_=true;
                if(record_test_mode_&&record_test_stall_ms_)vTaskDelay(pdMS_TO_TICKS(record_test_stall_ms_));
                record_sync_max_us_=std::max(record_sync_max_us_.load(),uint32_t(esp_timer_get_time()-started));flush_bytes_=record_bytes_.load();
            }
        }
    }
}

void PocketApps::StopRecording() {
    // STATUS/FILES/GET must not observe idle until the WAV header is committed.
    std::lock_guard<std::mutex> lock(storage_mutex_);
    if(!recording_)return;
    record_ended_us_=esp_timer_get_time();
    Application::GetInstance().GetAudioService().StopLocalRecording();
    auto codec=Board::GetInstance().GetAudioCodec();record_input_overruns_end_=codec->input_overruns();record_input_errors_end_=codec->input_errors();
    record_writer_stop_=true;xTaskNotifyGive(record_writer_);
    while(!record_writer_done_)vTaskDelay(pdMS_TO_TICKS(1));
    if(record_first_sample_us_.load()&&(record_input_overruns_end_.load()>record_input_overruns_begin_.load()||record_input_errors_end_.load()>record_input_errors_begin_.load()))record_error_=true;
    if(record_file_){
        WriteWavHeader();if(fflush(record_file_)!=0||fsync(fileno(record_file_))!=0)record_error_=true;
        if(fclose(record_file_)!=0)record_error_=true;
        record_file_=nullptr;
    }
    recording_=false;record_test_mode_=false;record_test_stall_ms_=0;
    audio_level_=0;audio_rms_=0;
    record_message_=std::string(record_error_?"录音已停止，卡片写入出现错误，请检查文件：":"已保存：")+record_name_+
        "\n"+std::to_string(record_bytes_.load()/32000)+" 秒";
    ESP_LOGI(TAG,"Recording saved: %s; bytes=%lu; error=%d",record_name_.c_str(),(unsigned long)record_bytes_.load(),int(record_error_.load()));
}



void PocketApps::UsbTask() {
    if(!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config={.tx_buffer_size=2048,.rx_buffer_size=8192};
        if(usb_serial_jtag_driver_install(&config)!=ESP_OK){ESP_LOGE(TAG,"USB driver initialization failed");return;}
    }
    usb_serial_jtag_vfs_use_driver();
    int fd=open("/dev/usbserjtag",O_RDWR|O_NONBLOCK);
    if(fd<0){usb_serial_jtag_vfs_register();fd=open("/dev/usbserjtag",O_RDWR|O_NONBLOCK);}
    if(fd<0){ESP_LOGE(TAG,"USB transfer endpoint unavailable");return;}
    // A 4096-byte package block expands to 5464 base64 characters. Keep
    // framing bounded, while allowing one complete block and its metadata.
    std::string line;line.reserve(6144);bool overflow=false;
    ESP_LOGI(TAG,"USB local transfer ready");
    while(true){
        char buf[512];int length=read(fd,buf,sizeof(buf));
        for(int i=0;i<length;++i){
            char c=buf[i];if(c=='\r')continue;
            if(c=='\n'){
                if(!overflow&&line.starts_with("@PK "))UsbCommand(line.substr(4));
                else if(overflow){printf("@PK ERR FRAME_TOO_LONG\n");fflush(stdout);}
                line.clear();overflow=false;
            }
            else if(line.size()<6144)line+=c;else overflow=true;
        }
        vTaskDelay(length>0 ? 1 : pdMS_TO_TICKS(3));
    }
}

void PocketApps::UsbCommand(const std::string& command) {
    if(Diagnostics(command))return;
    TransferCommand(command);
}



bool PocketApps::CanAutoStandby(){
    auto& app=Application::GetInstance();
    if(sleeping_||preparing_sleep_||IsUpdating()||reader_active_||recording_||monitor_busy_||monitor_pending_)return false;
    if(app.IsChatAppOpen()&&app.GetDeviceState()!=kDeviceStateIdle)return false;
    std::unique_lock<std::mutex> transfer(transfer_mutex_,std::try_to_lock);if(!transfer.owns_lock())return false;
    std::unique_lock<std::mutex> storage(storage_mutex_,std::try_to_lock);if(!storage.owns_lock()||!upload_name_.empty())return false;
    std::unique_lock<std::mutex> state(codex_mutex_,std::try_to_lock);if(!state.owns_lock())return false;
    return !codex_editor_&&!monitor_dialog_&&!codex_editing_&&!codex_pending_&&codex_state_!=2&&codex_state_!=3&&codex_state_!=5&&codex_state_!=7&&!codex_stop_waiting_&&!codex_answer_waiting_&&!codex_continue_waiting_;
}
uint32_t PocketApps::StandbyTimeoutSeconds(){
    return 180;
}
bool PocketApps::PrepareStandby(){
    auto display=Board::GetInstance().GetDisplay();
    if(preparing_sleep_.exchange(true))return false;
    struct Transition {std::atomic<bool>& flag;bool committed=false;~Transition(){if(!committed)flag=false;}} transition{preparing_sleep_};
    if(IsUpdating()||reader_active_){display->ShowNotification("文件同步或更新中，请稍后待机",4000);return false;}
    {
        std::unique_lock<std::mutex> transfer(transfer_mutex_,std::try_to_lock);
        std::unique_lock<std::mutex> storage(storage_mutex_,std::try_to_lock);
        if(!transfer.owns_lock()||!storage.owns_lock()||!upload_name_.empty()){display->ShowNotification("文件正在保存，请稍后待机",4000);return false;}
    }
    {
        std::unique_lock<std::mutex> state(codex_mutex_,std::try_to_lock);
        if(!state.owns_lock()||codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_pending_||codex_stop_waiting_||codex_answer_waiting_||codex_continue_waiting_){display->ShowNotification("请等当前操作完成后再待机",4000);return false;}
    }
    bool was_recording=recording_;if(was_recording)StopRecording();
    if(recording_||(was_recording&&record_error_)){display->ShowNotification("录音保存异常，请先检查记录",5000);return false;}
    Application::GetInstance().ReturnToHome();Close();if(open_)return false;
    PocketClock::Get().Checkpoint(true);transition.committed=true;return true;
}

PocketApps::HomeCodexInfo PocketApps::GetHomeCodexInfo(){
    if(!recording_&&!open_&&!sleeping_&&esp_timer_get_time()>=usage_next_us_.load())usage_pending_=true;
    std::unique_lock<std::mutex> state(codex_mutex_,std::try_to_lock);
    if(!state.owns_lock())return home_codex_info_;
    home_codex_info_.title=codex_chat_title_.empty()?"打开最近对话":codex_chat_title_;
    home_codex_info_.cached=codex_http_success_us_>0;
    home_codex_info_.questions=codex_questions_available_?codex_question_count_:-1;
    home_codex_info_.status=home_codex_info_.cached?(codex_running_?"上次状态：执行中":"上次状态：空闲"):"进入后同步状态";
    home_codex_info_.usage=usage_text_;
    if(usage_observed_us_&&esp_timer_get_time()-usage_observed_us_>180LL*1000000&&home_codex_info_.usage.rfind("上次",0)!=0)home_codex_info_.usage="上次"+home_codex_info_.usage;
    return home_codex_info_;
}
void PocketApps::UpdateCodexUsage(const cJSON* doc){
    auto usage=cJSON_GetObjectItem(doc,"usage");if(!cJSON_IsObject(usage))return;
    auto text=cJSON_GetObjectItem(usage,"text"),details=cJSON_GetObjectItem(usage,"details");
    if(!cJSON_IsString(text)||strlen(text->valuestring)>512)return;
    std::lock_guard<std::mutex> lock(codex_mutex_);usage_text_=text->valuestring;
    usage_details_=cJSON_IsString(details)?std::string(details->valuestring).substr(0,2048):"额度信息暂不可用。";
    usage_observed_us_=esp_timer_get_time();usage_next_us_=usage_observed_us_+60LL*1000000;usage_pending_=false;
}
void PocketApps::OpenCodexUsage(){
    if(recording_){RestoreOpenApp();return;}if(reader_active_||sleeping_||IsUpdating())return;
    Application::GetInstance().ReturnToHome();
    Application::GetInstance().Schedule([this](){
        if(recording_||reader_active_||sleeping_||IsUpdating())return;
        auto display=Board::GetInstance().GetDisplay();DisplayLockGuard lock(display);MakeRoot("Codex 额度");
        lv_obj_add_flag(lv_obj_get_child(root_,0),LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(lv_obj_get_child(root_,1),24,50);lv_obj_set_width(lv_obj_get_child(root_,1),432);
        std::string details;{std::lock_guard<std::mutex> state(codex_mutex_);details=usage_details_;}
        Label(body_,details.c_str());
        Label(body_,"以上为电脑最近返回的账号额度。\n不是当前对话的字数或上下文。",0x7fa5b5);
        Button(body_,"刷新并返回首页",[](lv_event_t*){auto& app=PocketApps::GetInstance();app.usage_next_us_=0;app.usage_pending_=true;Application::GetInstance().ReturnToHome();});
    });
}
void PocketApps::ResumeCodex(){
    if(recording_){RestoreOpenApp();return;}
    if(reader_active_||sleeping_||IsUpdating())return;
    if(codex_selected_thread_.empty())OpenCodex();else OpenCodexChat();
}
void PocketApps::OpenDeviceTools(bool immediate){
    if(recording_){RestoreOpenApp();return;}
    if(reader_active_||sleeping_||IsUpdating())return;
    auto build=[this](){
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        if(recording_||reader_active_||sleeping_||IsUpdating()||RestoreOpenApp())return;
        MakeRoot("设备");lv_obj_add_flag(lv_obj_get_child(root_,0),LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(lv_obj_get_child(root_,1),24,50);
        Button(body_,"答案之书",[](lv_event_t*){auto& s=PocketApps::GetInstance();s.Close(false,false);s.OpenAnswerBook(true);});
        Button(body_,"电子木鱼",[](lv_event_t*){auto& s=PocketApps::GetInstance();s.Close(false,false);s.OpenWoodfish(true);});
        Button(body_,"计算器",[](lv_event_t*){auto& s=PocketApps::GetInstance();s.Close(false,false);s.OpenCalculator(true);});
        Button(body_,"文件传输",[](lv_event_t*){auto& s=PocketApps::GetInstance();s.Close(false,false);s.OpenCardReader();});
        Button(body_,"小智语音",[](lv_event_t*){auto& s=PocketApps::GetInstance();s.Close(false,false);Application::GetInstance().OpenChatApp();});
        Button(body_,"待机",[](lv_event_t*){static_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())->SetStandby(true);});
        Label(body_,"BOOT 按键待机 / 唤醒\n边缘侧滑返回");
        for(auto rail:edge_rails_)lv_obj_move_foreground(rail);
    };
    if(immediate){build();return;}
    Application::GetInstance().ReturnToHome();
    Application::GetInstance().Schedule(build);
}

void PocketApps::OpenAnswerBook(bool immediate){
    if(recording_){RestoreOpenApp();return;}
    if(reader_active_||sleeping_||IsUpdating())return;
    auto build=[this](){
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        if(recording_||reader_active_||sleeping_||IsUpdating()||RestoreOpenApp())return;
        MakeRoot("答案之书");lv_obj_add_flag(lv_obj_get_child(root_,0),LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(lv_obj_get_child(root_,1),24,50);
        PocketAnswerBook::Get().Open(body_);
        for(auto rail:edge_rails_)lv_obj_move_foreground(rail);
    };
    if(immediate){build();return;}
    Application::GetInstance().ReturnToHome();
    Application::GetInstance().Schedule(build);
}

void PocketApps::OpenCalculator(bool immediate){
    if(recording_){RestoreOpenApp();return;}
    if(reader_active_||sleeping_||IsUpdating())return;
    auto build=[this](){
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        if(recording_||reader_active_||sleeping_||IsUpdating()||RestoreOpenApp())return;
        MakeRoot("计算器");lv_obj_add_flag(lv_obj_get_child(root_,0),LV_OBJ_FLAG_HIDDEN);lv_obj_set_pos(lv_obj_get_child(root_,1),24,50);
        PocketCalculator::Get().Open(body_);for(auto rail:edge_rails_)lv_obj_move_foreground(rail);
    };
    if(immediate){build();return;}
    Application::GetInstance().ReturnToHome();Application::GetInstance().Schedule(build);
}
void PocketApps::OpenWoodfish(bool immediate){
    if(recording_){RestoreOpenApp();return;}
    if(reader_active_||sleeping_||IsUpdating())return;
    auto build=[this](){
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        if(recording_||reader_active_||sleeping_||IsUpdating()||RestoreOpenApp())return;
        MakeRoot("电子木鱼");lv_obj_add_flag(lv_obj_get_child(root_,0),LV_OBJ_FLAG_HIDDEN);lv_obj_set_pos(lv_obj_get_child(root_,1),24,50);
        PocketWoodfish::Get().Open(body_);for(auto rail:edge_rails_)lv_obj_move_foreground(rail);
    };
    if(immediate){build();return;}
    Application::GetInstance().ReturnToHome();Application::GetInstance().Schedule(build);
}

void PocketApps::Back(){
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if(sleeping_||!open_)return;
    if(PocketAnswerBook::Get().IsOpen()||PocketCalculator::Get().IsOpen()||PocketWoodfish::Get().IsOpen()){Close(false,false);if(!open_)OpenDeviceTools(true);return;}
    ESP_LOGI(TAG,"Back dashboard=%d question=%d pending=%d epoch=%lu",int(codex_dashboard_.load()),int(codex_question_overlay_!=nullptr),int(codex_return_pending_), (unsigned long)navigation_epoch_);
    if(codex_history_overlay_){auto old=codex_history_overlay_;codex_history_overlay_=nullptr;lv_obj_add_flag(old,LV_OBJ_FLAG_HIDDEN);lv_obj_delete_async(old);if(codex_input_)lv_obj_add_state(codex_input_,LV_STATE_FOCUSED);return;}
    if(codex_editor_){FinishCodexEdit(false);return;}
    if(codex_question_overlay_){CloseCodexQuestion();return;}
    if(monitor_dialog_){lv_obj_delete(monitor_dialog_);monitor_dialog_=nullptr;return;}
    if(codex_active_&&!codex_dashboard_){ReturnCodexDashboard();return;}
    Close(false);
}



void PocketApps::OpenMonitor(){
    if(recording_){RestoreOpenApp();return;}
    if(reader_active_){RestoreOpenApp();return;}
    Application::GetInstance().Schedule([this](){
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        if(RestoreOpenApp())return;
        MakeRoot("电脑监控");
        {std::lock_guard<std::mutex> guard(monitor_mutex_);monitor_text_="连接中";monitor_raw_.clear();monitor_action_text_.clear();}
        monitor_active_=true;RenderMonitor();
        monitor_timer_=lv_timer_create([](lv_timer_t*){PocketApps::GetInstance().RefreshMonitor();},500,nullptr);
        if(!monitor_task_started_){
            monitor_task_started_=xTaskCreate([](void* a){static_cast<PocketApps*>(a)->MonitorWorker();},"pc_monitor",12288,this,2,nullptr)==pdPASS;
            if(!monitor_task_started_){monitor_active_=false;lv_label_set_text(monitor_label_,"内存不足，请返回后重试。");}
        }
    });
}
static std::string MonitorFetch(const char*,const std::string& path="/api/metrics",const std::string& body=""){
    if(path!="/api/metrics"&&path!="/api/control")return {};
    return PocketApps::GetInstance().CodexHttp("/device/monitor"+path.substr(4),body);
}

void PocketApps::MonitorWorker(){
    bool initialized=false,station=false;int64_t clock_window=0,usage_window=0;std::string control_id;unsigned control_started=0;
    for(;;){
        if(!recording_&&!sleeping_)PocketClock::Get().Checkpoint();
        bool clock_due=PocketClock::Get().SyncDue()&&!recording_&&!reader_active_;
        bool app_active=monitor_active_||codex_active_;
        bool diary_sync=false;
        bool usage_due=usage_pending_&&!open_&&!recording_&&!reader_active_&&!IsUpdating()&&!Application::GetInstance().IsChatAppOpen();
        if((!app_active&&!clock_due&&!diary_sync&&!usage_due)||sleeping_||(recording_&&!codex_active_)){
            if(station){WifiManager::GetInstance().StopStation();station=false;}
            clock_window=0;
            usage_window=0;
            vTaskDelay(pdMS_TO_TICKS(sleeping_?1000:200));continue;
        }
        if(!station){
            if(clock_due)clock_window=esp_timer_get_time()+20000000;
            if(!initialized){Board::GetInstance().StartNetwork();initialized=true;}
            else WifiManager::GetInstance().StartStation();
            station=true;
        }
        if(clock_due){
            if(!clock_window)clock_window=esp_timer_get_time()+20000000;
            if(WifiManager::GetInstance().IsConnected()){
                auto raw=CodexHttp("/device/time");auto doc=cJSON_Parse(raw.c_str());auto epoch=cJSON_GetObjectItem(doc,"epoch");
                bool ok=cJSON_IsNumber(epoch)&&PocketClock::Get().Set(int64_t(epoch->valuedouble));UpdateCodexUsage(doc);
                cJSON_Delete(doc);if(!ok)PocketClock::Get().RetryLater();clock_window=0;
            }else if(esp_timer_get_time()>=clock_window){PocketClock::Get().RetryLater();clock_window=0;}
        }
        if(usage_due&&usage_pending_){
            if(!usage_window)usage_window=esp_timer_get_time()+20000000;
            if(WifiManager::GetInstance().IsConnected()){
                usage_pending_=false;usage_next_us_=esp_timer_get_time()+60LL*1000000;
                auto raw=CodexHttp("/device/usage");auto doc=cJSON_Parse(raw.c_str());UpdateCodexUsage(doc);cJSON_Delete(doc);usage_window=0;
            }else if(esp_timer_get_time()>=usage_window){usage_pending_=false;usage_next_us_=esp_timer_get_time()+300LL*1000000;usage_window=0;}
        }
        if(!app_active){vTaskDelay(pdMS_TO_TICKS(200));continue;}
        if(codex_active_){
            if(!recording_)CodexNetwork();
            vTaskDelay(pdMS_TO_TICKS(1000));continue;
        }
        int action=monitor_pending_.exchange(0);
        if(action){
            std::string message="未连接，操作未发送";
            if(WifiManager::GetInstance().IsConnected()){
                const char* names[]={"","single","extend","sleep","shutdown"};
                char id[32];snprintf(id,sizeof(id),"p4-%08x-%08x",unsigned(esp_random()),unsigned(esp_random()));
                std::string body=std::string("{\"action\":\"")+names[action]+"\",\"requestId\":\""+id+"\"}";
                auto response=MonitorFetch(POCKET_PC_HOST,"/api/control",body);auto ack=cJSON_Parse(response.c_str());
                auto ok=cJSON_GetObjectItem(ack,"ok");
                if(cJSON_IsTrue(ok)){control_id=id;control_started=unsigned(esp_timer_get_time()/1000);message="已提交，等待电脑执行";}
                else{message="未获确认，结果未知，请检查电脑";monitor_busy_=false;}
                cJSON_Delete(ack);
            }else monitor_busy_=false;
            std::lock_guard<std::mutex> guard(monitor_mutex_);monitor_action_text_=message;
        }
        std::string snapshot;
        std::string text="未连接电脑 / 正在重连\n旧数据已隐藏";
        if(!WifiManager::GetInstance().IsConnected())text="正在连接 Wi-Fi…\n需要与电脑处于同一局域网";
        else{
            const char* host=POCKET_PC_HOST;
            auto raw=MonitorFetch(host);
            if(!raw.empty()){
                auto json=cJSON_Parse(raw.c_str());
                if(json){
                    PocketClock::Get().RequestSync();
                    snapshot=raw;
                    auto control=cJSON_GetObjectItem(json,"control");auto id=cJSON_GetObjectItem(control,"requestId");auto state=cJSON_GetObjectItem(control,"state");
                    if(cJSON_IsString(id)&&control_id==id->valuestring&&cJSON_IsString(state)){
                        std::string st=state->valuestring;
                        if(st=="completed"||st=="failed"){
                            monitor_busy_=false;control_id.clear();
                            std::lock_guard<std::mutex> guard(monitor_mutex_);monitor_action_text_=st=="completed"?"电脑已执行请求":"执行失败，请检查电脑";
                        }
                    }
                    auto field=[&](const char* group,const char* key,const char* suffix){
                        auto obj=cJSON_GetObjectItem(json,group);auto value=cJSON_GetObjectItem(obj,key);
                        if(!cJSON_IsNumber(value))return std::string("--");
                        char buf[48];snprintf(buf,sizeof(buf),"%.0f%s",value->valuedouble,suffix);return std::string(buf);
                    };
                    auto mb=[&](const char* group,const char* key){auto v=cJSON_GetObjectItem(cJSON_GetObjectItem(json,group),key);if(!cJSON_IsNumber(v))return std::string("--");char b[24];snprintf(b,sizeof(b),"%.1f",v->valuedouble/1073741824.0);return std::string(b);};
                    text="LIVE / 实时数据\n\nCPU  "+field("cpu","usagePercent","%")+"\n内存  "+field("memory","usagePercent","%")+"  "+mb("memory","usedBytes")+" / "+mb("memory","totalBytes")+" GB\n\nGPU  "+field("gpu","usagePercent","%")+"\n显存  "+mb("gpu","usedVramBytes")+" / "+mb("gpu","totalVramBytes")+" GB\n温度  "+field("gpu","temperatureC"," C")+"\n功耗  "+field("gpu","powerWatts"," W");
                    auto diag=cJSON_GetObjectItem(json,"diagnostics");auto age=cJSON_GetObjectItem(diag,"gpuAgeMs");auto err=cJSON_GetObjectItem(diag,"gpuError");
                    if(!cJSON_IsNumber(age)||age->valuedouble>5000||cJSON_IsString(err))text.replace(0,std::string("LIVE / 实时数据").size(),"STALE / 数据过期");
                    ++monitor_updates_;cJSON_Delete(json);
                }
            }

        }
        if(!control_id.empty()&&unsigned(esp_timer_get_time()/1000)-control_started>15000){monitor_busy_=false;control_id.clear();std::lock_guard<std::mutex> guard(monitor_mutex_);monitor_action_text_="已提交，电脑离线或结果待确认";}
        {std::lock_guard<std::mutex> guard(monitor_mutex_);monitor_text_=text;monitor_raw_=snapshot;}
        for(int i=0;i<10&&monitor_active_&&!sleeping_&&!recording_;i++)vTaskDelay(pdMS_TO_TICKS(100));
    }
}

#include "monitor_ui.inc"

#include "codex_ui.inc"

// Public build: voice capture is entered only from a selected Codex chat.
void PocketApps::RefreshRecorder(){
    if(recording_&&(record_error_||record_bytes_>=kCodexVoiceMaxBytes))StopRecording();
}
void PocketApps::TransferCommand(const std::string& command,const std::function<void(const std::string&)>& output){
    auto reply=[&](const std::string& value){if(output)output(value);else{printf("%s",value.c_str());fflush(stdout);}};
    if(Diagnostics(command))return;
    std::istringstream input(command);std::string verb;input>>verb;
    if(verb=="STATUS"){
        char value[180];snprintf(value,sizeof(value),"@PK STATUS mounted=%d done=%d recording=%d open=%d reader=%d\n",int(mounted_.load()),int(mount_done_.load()),int(recording_.load()),int(open_.load()),int(reader_active_.load()));reply(value);return;
    }
    if(verb=="CLOCK"){reply(std::string("@PK CLOCK text=")+ClockText()+"\n");return;}
    if(verb=="TIME"){
        long long value=0;input>>value;
        if(recording_||value<1767225600LL||value>=4102444800LL||!PocketClock::Get().Set(value,true)){reply("@PK ERR TIME_ARGS\n");return;}
        reply("@PK OK TIME\n");return;
    }
    reply("@PK ERR UNSUPPORTED_COMMAND\n");
}
