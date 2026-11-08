#include "pocket_theme.h"
#include "wifi_manager.h"
LV_FONT_DECLARE(launcher_chinese_24);

namespace {
struct PocketHomeState {
    LcdDisplay* display=nullptr;
    lv_obj_t *root=nullptr,*title=nullptr,*status=nullptr,*questions=nullptr,*usage=nullptr;
    lv_obj_t *record_button=nullptr,*record_label=nullptr,*record_info=nullptr,*wave=nullptr,*wifi=nullptr;
    lv_timer_t* timer=nullptr;
    uint32_t period=1000,last_metadata=0,last_stop=0;
    bool metadata_seen=false,recording=false;
    unsigned levels[24]={};
};

bool PocketHomeVisible(PocketHomeState* state){
    auto& app=PocketApps::GetInstance();
    return state&&state->root&&!lv_obj_has_flag(state->root,LV_OBJ_FLAG_HIDDEN)
        &&!state->display->IsStandby()&&!app.IsSleeping()&&!app.IsOpen()
        &&!Application::GetInstance().IsChatAppOpen();
}

void PocketHomeSetText(lv_obj_t* label,const std::string& text){
    if(text!=lv_label_get_text(label))lv_label_set_text(label,text.c_str());
}

void PocketHomeFrame(lv_obj_t* object,uint32_t accent,uint32_t background=0x0c2232){
    lv_obj_remove_style_all(object);lv_obj_remove_flag(object,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(object,0,0);lv_obj_set_style_radius(object,0,0);
    lv_obj_set_style_bg_color(object,lv_color_hex(background),0);lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(object,lv_color_hex(0x17445b),LV_STATE_PRESSED);
    lv_obj_set_style_border_width(object,0,0);lv_obj_set_style_border_color(object,lv_color_hex(accent),0);
    // Drawing the outline does not move the content origin or affect alignment.
    lv_obj_add_event_cb(object,[](lv_event_t* e){
        auto object=lv_event_get_target_obj(e);lv_area_t a;lv_obj_get_coords(object,&a);
        lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;
        d.bg_color=lv_obj_get_style_border_color(object,LV_PART_MAIN);
        for(auto line:{lv_area_t{a.x1,a.y1,a.x2,a.y1+1},lv_area_t{a.x1,a.y2-1,a.x2,a.y2},
                       lv_area_t{a.x1,a.y1+2,a.x1+1,a.y2-2},lv_area_t{a.x2-1,a.y1+2,a.x2,a.y2-2}})
            lv_draw_rect(lv_event_get_layer(e),&d,&line);
    },LV_EVENT_DRAW_MAIN_END,nullptr);
}

lv_obj_t* PocketHomeLabel(lv_obj_t* parent,const char* text,int x,int y,int width,
                          uint32_t color,const lv_font_t* font){
    auto label=lv_label_create(parent);lv_obj_set_pos(label,x,y);lv_obj_set_size(label,width,font->line_height);
    lv_obj_set_style_text_font(label,font,0);lv_obj_set_style_text_color(label,lv_color_hex(color),0);
    lv_obj_set_style_text_line_space(label,0,0);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
    lv_label_set_text(label,text);lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);return label;
}

void PocketHomeUpdate(PocketHomeState* state,bool force=false){
    if(!PocketHomeVisible(state)){if(state&&state->timer)lv_timer_pause(state->timer);return;}
    auto& app=PocketApps::GetInstance();
    const uint32_t now=lv_tick_get();
    if(force||!state->metadata_seen||now-state->last_metadata>=1000){
        state->metadata_seen=true;state->last_metadata=now;
        const auto info=app.GetHomeCodexInfo();
        PocketHomeSetText(state->title,info.title.empty()?"最近对话":info.title);
        PocketHomeSetText(state->status,info.status.empty()?"进入后同步状态":info.status);
        PocketHomeSetText(state->usage,info.usage);
        const auto pending=info.questions<0?std::string("待回答：待同步"):
            info.questions?"待回答 "+std::to_string(info.questions)+" · 上次":"暂无待回答 · 上次";
        PocketHomeSetText(state->questions,pending);
        const uint32_t color=info.questions>0?0xffcf63:0x7fa5b5;
        if((lv_color_to_u32(lv_obj_get_style_text_color(state->questions,LV_PART_MAIN))&0xffffff)!=color)
            lv_obj_set_style_text_color(state->questions,lv_color_hex(color),0);
        PocketHomeSetText(state->wifi,WifiManager::GetInstance().IsConnected()?"Wi-Fi 已连接":"Wi-Fi 未连接");
    }
}
}

void LcdDisplay::SetLauncherActive(bool active){
    if(!launcher_)return;
    auto state=static_cast<PocketHomeState*>(lv_obj_get_user_data(launcher_));
    if(!state||!state->timer)return;
    if(!active||!PocketHomeVisible(state)){lv_timer_pause(state->timer);return;}
    PocketHomeUpdate(state,true);lv_timer_reset(state->timer);lv_timer_resume(state->timer);
}

std::string LcdDisplay::LauncherDiagnostic(){
    DisplayLockGuard lock(this);
    auto state=launcher_?static_cast<PocketHomeState*>(lv_obj_get_user_data(launcher_)):nullptr;
    auto count=[](auto&& self,lv_obj_t* object)->unsigned{
        if(!object)return 0;
        unsigned result=1;
        for(uint32_t i=0;i<lv_obj_get_child_count(object);i++)result+=self(self,lv_obj_get_child(object,i));
        return result;
    };
    char report[256];snprintf(report,sizeof(report),
        "native=1 visible=%d hidden=%d timer_paused=%d timer_period=%u fixed_font_px=24 fixed_font_h=%u objects=%u recording=%d title_bytes=%u",
        int(PocketHomeVisible(state)),int(!launcher_||lv_obj_has_flag(launcher_,LV_OBJ_FLAG_HIDDEN)),
        state&&state->timer?int(lv_timer_get_paused(state->timer)):-1,state?unsigned(state->period):0,
        unsigned(launcher_chinese_24.line_height),count(count,launcher_),int(PocketApps::GetInstance().IsRecording()),
        state&&state->title?unsigned(strlen(lv_label_get_text(state->title))):0);
    return report;
}

void LcdDisplay::ShowLauncher(bool home){
    DisplayLockGuard lock(this);
    if(PocketApps::GetInstance().RestoreOpenApp()){SetLauncherActive(false);return;}
    if(!home){
        SetLauncherActive(false);SetChatBackgroundVisible(true);
        if(launcher_)lv_obj_add_flag(launcher_,LV_OBJ_FLAG_HIDDEN);
        if(gif_controller_)gif_controller_->Resume();
        auto controls=lv_display_get_layer_top(display_);
        if(!conversation_button_){
            auto make=[&](int y,uint32_t color,const char* text,lv_event_cb_t cb){
                auto b=lv_button_create(controls);lv_obj_set_pos(b,24,y);lv_obj_set_size(b,432,64);PocketPanel(b,color);
                lv_obj_set_style_text_font(b,&launcher_chinese_24,0);lv_obj_set_style_text_color(b,lv_color_hex(0xe8f9ff),0);
                auto l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);return b;
            };
            conversation_button_=make(640,0x32e6cf,"等待连接",[](lv_event_t*){Application::GetInstance().ToggleConversation();});
            conversation_button_label_=lv_obj_get_child(conversation_button_,-1);lv_obj_add_state(conversation_button_,LV_STATE_DISABLED);
            return_home_button_=make(720,0x217b9d,"返回首页",[](lv_event_t*){Application::GetInstance().ReturnToHome();});
            if(bottom_bar_)lv_obj_align(bottom_bar_,LV_ALIGN_BOTTOM_MID,0,-176);
        }
        lv_obj_remove_flag(conversation_button_,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(return_home_button_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(conversation_button_);lv_obj_move_foreground(return_home_button_);return;
    }
    for(auto control:{return_home_button_,conversation_button_})if(control)lv_obj_add_flag(control,LV_OBJ_FLAG_HIDDEN);
    SetChatBackgroundVisible(false);if(gif_controller_)gif_controller_->Pause();
    if(!launcher_){
        const auto dynamic_font=static_cast<LvglTheme*>(GetTheme())->text_font()->font();
        launcher_=lv_obj_create(lv_display_get_layer_top(display_));lv_obj_remove_style_all(launcher_);
        lv_obj_set_size(launcher_,480,800);lv_obj_remove_flag(launcher_,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(launcher_,lv_color_hex(0x071522),0);lv_obj_set_style_bg_opa(launcher_,LV_OPA_COVER,0);
        lv_obj_set_style_text_font(launcher_,&launcher_chinese_24,0);
        auto state=new PocketHomeState{};state->root=launcher_;state->display=this;lv_obj_set_user_data(launcher_,state);
        lv_obj_add_event_cb(launcher_,[](lv_event_t* e){
            auto state=static_cast<PocketHomeState*>(lv_event_get_user_data(e));
            if(lv_event_get_target_obj(e)!=state->root)return;
            if(state->timer)lv_timer_delete(state->timer);
            if(state->display->launcher_==lv_event_get_target_obj(e))state->display->launcher_=nullptr;
            delete state;
        },LV_EVENT_DELETE,state);
        auto text=[&](lv_obj_t* parent,const char* value,int x,int y,int width,uint32_t color,bool dynamic=false){
            return PocketHomeLabel(parent,value,x,y,width,color,dynamic?dynamic_font:&launcher_chinese_24);
        };
        auto panel=[&](lv_obj_t* parent,int x,int y,int w,int h,uint32_t color){
            auto p=lv_obj_create(parent);PocketHomeFrame(p,color);lv_obj_set_pos(p,x,y);lv_obj_set_size(p,w,h);
            lv_obj_remove_flag(p,LV_OBJ_FLAG_CLICKABLE);return p;
        };
        auto button=[&](lv_obj_t* parent,const char* title,int x,int y,int w,int h,uint32_t color,lv_event_cb_t cb,void* data=nullptr){
            auto b=lv_button_create(parent);PocketHomeFrame(b,color);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
            auto label=text(b,title,0,0,w-24,0xe8f9ff);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label);
            if(cb)lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data);
            return b;
        };
        text(launcher_,"Pocket Codex",24,48,330,0xe8f9ff);PocketPixel(launcher_,24,96,432,2,0x22536c);
        auto codex=panel(launcher_,24,112,432,244,0x32cdb6);PocketIcon(codex,16,18,3,0x32e6cf,4);
        text(codex,"Codex",64,21,148,0xe8f9ff);
        state->status=text(codex,"进入后同步状态",204,22,212,0x7fa5b5,true);
        state->title=text(codex,"最近对话",16,68,400,0xc6e2eb,true);
        state->usage=text(codex,"额度：待同步",16,94,400,0x7fa5b5,true);
        lv_obj_add_flag(state->usage,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(state->usage,3);
        lv_obj_add_event_cb(state->usage,[](lv_event_t*){PocketApps::GetInstance().OpenCodexUsage();},LV_EVENT_CLICKED,nullptr);
        button(codex,"继续当前对话",16,124,400,48,0x32cdb6,[](lv_event_t*){PocketApps::GetInstance().ResumeCodex();});
        button(codex,"最近对话",16,188,180,40,0x22536c,[](lv_event_t*){
            auto& app=PocketApps::GetInstance();app.codex_dashboard_recent_=true;app.OpenCodex();
        });
        state->questions=text(codex,"待回答：待同步",212,195,204,0x7fa5b5,true);
        auto monitor=button(launcher_,"电脑监控",24,380,432,120,0x22536c,[](lv_event_t*){PocketApps::GetInstance().OpenMonitor();});
        auto monitor_label=lv_obj_get_child(monitor,0);lv_obj_set_width(monitor_label,136);lv_obj_set_pos(monitor_label,64,46);lv_obj_set_align(monitor_label,LV_ALIGN_TOP_LEFT);lv_obj_set_style_text_align(monitor_label,LV_TEXT_ALIGN_LEFT,0);PocketIcon(monitor,16,42,3,0x32e6cf,4);
        auto device=button(launcher_,"设备",24,520,432,120,0x22536c,[](lv_event_t*){PocketApps::GetInstance().OpenDeviceTools();});
        auto device_label=lv_obj_get_child(device,0);lv_obj_set_width(device_label,136);lv_obj_set_pos(device_label,64,46);lv_obj_set_align(device_label,LV_ALIGN_TOP_LEFT);lv_obj_set_style_text_align(device_label,LV_TEXT_ALIGN_LEFT,0);PocketIcon(device,16,42,5,0x7fa5b5,4);
        state->wifi=text(launcher_,"Wi-Fi 未连接",24,718,296,0x7fa5b5,true);
        button(launcher_,"待机",344,704,112,52,0x22536c,[](lv_event_t* e){static_cast<LcdDisplay*>(lv_event_get_user_data(e))->SetStandby(true);},this);
        auto hint=text(launcher_,"BOOT 待机 / 唤醒",24,768,432,0x567b8d);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
        state->timer=lv_timer_create([](lv_timer_t* timer){PocketHomeUpdate(static_cast<PocketHomeState*>(lv_timer_get_user_data(timer)));},1000,state);
        lv_timer_pause(state->timer);
    }
    lv_obj_remove_flag(launcher_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(launcher_);SetLauncherActive(true);
}

void LcdDisplay::ShowLocalApp(){
    DisplayLockGuard lock(this);SetLauncherActive(false);SetChatBackgroundVisible(false);
    for(auto object:{launcher_,return_home_button_,conversation_button_})if(object)lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
    if(gif_controller_)gif_controller_->Pause();
}

void LcdDisplay::BenchmarkBackground(bool legacy){
    SetLauncherActive(false);SetChatBackgroundVisible(legacy);
    if(launcher_){if(legacy)lv_obj_remove_flag(launcher_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(launcher_,LV_OBJ_FLAG_HIDDEN);}
}

void LcdDisplay::SetChatBackgroundVisible(bool visible){
    if(visible){
        if(!chat_ui_hidden_)return;
        for(auto object:chat_visible_objects_)lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN);
        chat_visible_objects_.clear();chat_ui_hidden_=false;return;
    }
    auto screen=lv_display_get_screen_active(display_);
    for(uint32_t i=0;i<lv_obj_get_child_count(screen);i++){
        auto object=lv_obj_get_child(screen,i);
        if(!chat_ui_hidden_&&!lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN))chat_visible_objects_.push_back(object);
        lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
    }
    chat_ui_hidden_=true;
}

void LcdDisplay::SetChatControl(bool ready,bool active){
    DisplayLockGuard lock(this);if(!conversation_button_)return;
    lv_label_set_text(conversation_button_label_,ready?(active?"结束对话":"开始对话"):"等待连接");lv_obj_center(conversation_button_label_);
    if(ready)lv_obj_remove_state(conversation_button_,LV_STATE_DISABLED);else lv_obj_add_state(conversation_button_,LV_STATE_DISABLED);
}
