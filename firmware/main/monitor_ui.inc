#include "display/scifi_monitor_asset.h"
LV_FONT_DECLARE(launcher_chinese_24);
void PocketApps::RenderMonitor(){
    std::vector<lv_obj_t*> rails;
    for(uint32_t i=0;i<lv_obj_get_child_count(root_);i++){
        auto child=lv_obj_get_child(root_,i);
        if(child==edge_rails_[0]||child==edge_rails_[1])rails.push_back(child);
        else lv_obj_add_flag(child,LV_OBJ_FLAG_HIDDEN);
    }
    auto image=lv_image_create(root_);lv_image_set_src(image,Load_scifi_monitor());lv_obj_set_pos(image,0,32);
    auto label=[&](int x,int y,int w,int h,const lv_font_t* font,uint32_t color){
        auto box=lv_obj_create(root_);lv_obj_remove_style_all(box);lv_obj_set_pos(box,x,y);lv_obj_set_size(box,w,h);lv_obj_remove_flag(box,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(box,lv_color_hex(0x00101b),0);lv_obj_set_style_bg_opa(box,LV_OPA_COVER,0);
        auto obj=lv_label_create(box);lv_label_set_text(obj,"--");lv_obj_set_style_text_font(obj,font,0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);lv_obj_center(obj);return obj;
    };
    monitor_connection_=label(390,87,80,43,&launcher_chinese_24,0x32e6cf);
    monitor_values_[0]=label(36,226,180,72,&lv_font_montserrat_48,0x32e6cf);
    monitor_values_[1]=label(273,219,170,58,&lv_font_montserrat_48,0xf45895);
    monitor_values_[2]=label(386,311,65,24,&lv_font_montserrat_20,0xf45895);
    monitor_values_[3]=label(375,339,76,24,&lv_font_montserrat_20,0xf45895);
    monitor_values_[4]=label(250,402,192,30,&lv_font_montserrat_20,0x8de9f5);
    monitor_values_[5]=label(207,480,230,20,&lv_font_montserrat_20,0x8de9f5);
    const int rects[4][4]={{32,307,184,23},{264,279,186,24},{41,439,398,22},{172,499,265,14}};
    for(int i=0;i<4;i++){
        auto bar=lv_bar_create(root_);monitor_bars_[i]=bar;lv_obj_set_pos(bar,rects[i][0],rects[i][1]);lv_obj_set_size(bar,rects[i][2],rects[i][3]);lv_bar_set_range(bar,0,100);lv_bar_set_value(bar,0,LV_ANIM_OFF);
        lv_obj_set_style_radius(bar,0,0);lv_obj_set_style_bg_color(bar,lv_color_hex(0x123443),0);lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,0);lv_obj_set_style_border_width(bar,1,0);lv_obj_set_style_border_color(bar,lv_color_hex(0x286276),0);
        lv_obj_set_style_radius(bar,0,LV_PART_INDICATOR);lv_obj_set_style_bg_color(bar,lv_color_hex(i==1?0xf45895:0x32e6cf),LV_PART_INDICATOR);lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,LV_PART_INDICATOR);
    }
    const int hits[3][4]={{20,537,440,87},{20,632,214,68},{246,632,214,68}};
    const int actions[3]={0,3,4};
    for(int i=0;i<3;i++){
        auto b=lv_button_create(root_);monitor_controls_[i]=b;lv_obj_remove_style_all(b);lv_obj_set_pos(b,hits[i][0],hits[i][1]);lv_obj_set_size(b,hits[i][2],hits[i][3]);
        lv_obj_set_style_bg_color(b,lv_color_hex(0x263849),LV_STATE_PRESSED);lv_obj_set_style_bg_opa(b,LV_OPA_50,LV_STATE_PRESSED);
        lv_obj_add_event_cb(b,[](lv_event_t* e){PocketApps::GetInstance().MonitorDialog(int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,reinterpret_cast<void*>(intptr_t(actions[i])));
    }
    monitor_label_=label(12,743,456,57,&launcher_chinese_24,0x8095aa);lv_label_set_text(monitor_label_,"连接中");
    lv_obj_set_width(monitor_label_,432);lv_obj_set_style_text_align(monitor_label_,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(monitor_label_);
    for(auto rail:rails)lv_obj_move_foreground(rail);
    RefreshMonitor();
}
void PocketApps::RefreshMonitor(){
    if(!monitor_active_||!monitor_label_)return;
    std::string raw,message;{std::lock_guard<std::mutex> guard(monitor_mutex_);raw=monitor_raw_;message=monitor_action_text_;}
    auto json=cJSON_Parse(raw.c_str());bool live=json!=nullptr;
    auto num=[&](const char* g,const char* k)->cJSON*{return cJSON_GetObjectItem(cJSON_GetObjectItem(json,g),k);};
    auto display=[&](int slot,cJSON* v,const char* fmt){char buf[64];if(cJSON_IsNumber(v))snprintf(buf,sizeof(buf),fmt,v->valuedouble);else snprintf(buf,sizeof(buf),"--");lv_label_set_text(monitor_values_[slot],buf);lv_obj_center(monitor_values_[slot]);};
    auto percent=[&](int index,cJSON* v){lv_bar_set_value(monitor_bars_[index],cJSON_IsNumber(v)?std::max(0,std::min(100,int(v->valuedouble))):0,LV_ANIM_OFF);};
    display(0,num("cpu","usagePercent"),"%.0f%%");display(1,num("gpu","usagePercent"),"%.0f%%");display(2,num("gpu","temperatureC"),"%.0f C");display(3,num("gpu","powerWatts"),"%.0f W");
    auto memory=[&](int slot,const char* group,const char* used,const char* total){char buf[64];auto a=num(group,used),b=num(group,total);if(cJSON_IsNumber(a)&&cJSON_IsNumber(b))snprintf(buf,sizeof(buf),"%.1f / %.1f GB",a->valuedouble/1073741824.,b->valuedouble/1073741824.);else snprintf(buf,sizeof(buf),"-- / -- GB");lv_label_set_text(monitor_values_[slot],buf);lv_obj_center(monitor_values_[slot]);};
    memory(4,"memory","usedBytes","totalBytes");memory(5,"gpu","usedVramBytes","totalVramBytes");
    percent(0,num("cpu","usagePercent"));percent(1,num("gpu","usagePercent"));percent(2,num("memory","usagePercent"));
    auto used=num("gpu","usedVramBytes"),total=num("gpu","totalVramBytes");lv_bar_set_value(monitor_bars_[3],cJSON_IsNumber(used)&&cJSON_IsNumber(total)&&total->valuedouble>0?std::min(100,int(used->valuedouble*100/total->valuedouble)):0,LV_ANIM_OFF);
    auto age=num("diagnostics","gpuAgeMs");bool stale=live&&(!cJSON_IsNumber(age)||age->valuedouble>5000||cJSON_IsString(num("diagnostics","gpuError")));
    lv_label_set_text(monitor_connection_,live?(stale?"过期":"已连接"):"未连接");lv_obj_center(monitor_connection_);
    for(auto b:monitor_controls_){if(!live||stale||monitor_busy_)lv_obj_add_state(b,LV_STATE_DISABLED);else lv_obj_remove_state(b,LV_STATE_DISABLED);}
    if(message.empty())message=live?(stale?"数据已过期":"LIVE / 1s"):"未连接电脑，正在重连";
    lv_label_set_text(monitor_label_,message.c_str());lv_obj_center(monitor_label_);cJSON_Delete(json);
}
void PocketApps::MonitorDialog(int action){
    if(!monitor_active_||monitor_busy_)return;
    if(monitor_dialog_){lv_obj_delete(monitor_dialog_);monitor_dialog_=nullptr;}
    auto shade=lv_obj_create(root_);monitor_dialog_=shade;lv_obj_remove_style_all(shade);lv_obj_set_size(shade,480,800);lv_obj_set_style_bg_color(shade,lv_color_black(),0);lv_obj_set_style_bg_opa(shade,LV_OPA_80,0);lv_obj_add_flag(shade,LV_OBJ_FLAG_CLICKABLE);
    auto box=lv_obj_create(shade);PocketPanel(box);lv_obj_set_size(box,424,410);lv_obj_center(box);lv_obj_set_style_text_font(box,Board::GetInstance().GetDisplay()->GetTheme()?static_cast<LvglTheme*>(Board::GetInstance().GetDisplay()->GetTheme())->text_font()->font():&launcher_chinese_24,0);
    auto title=lv_label_create(box);const char* titles[]={"显示器切换","确认切到单屏？","确认切到扩展屏？","确认让电脑休眠？","确认关闭电脑？"};lv_label_set_text(title,titles[action]);lv_obj_set_pos(title,12,14);lv_obj_set_width(title,370);lv_obj_set_style_text_color(title,lv_color_hex(action==4?0xf45870:0x32e6cf),0);
    auto make=[&](const char* text,int y,lv_event_cb_t cb,void* data){auto b=Button(box,text,cb,data);lv_obj_set_pos(b,12,y);lv_obj_set_size(b,364,64);return b;};
    if(action==0){
        make("单屏（只保留主屏）",84,[](lv_event_t*){PocketApps::GetInstance().MonitorDialog(1);},nullptr);
        make("扩展显示器",168,[](lv_event_t*){PocketApps::GetInstance().MonitorDialog(2);},nullptr);
    }else{
        auto info=lv_label_create(box);lv_label_set_text(info,action>=3?"请先保存电脑中的工作。\n此操作针对电脑，不是终端待机。":"将切换电脑显示模式。");lv_obj_set_width(info,364);lv_obj_set_pos(info,12,78);
        make("确认执行",194,[](lv_event_t* e){auto& self=PocketApps::GetInstance();if(self.monitor_busy_.exchange(true))return;int action=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));self.monitor_pending_=action;{std::lock_guard<std::mutex> guard(self.monitor_mutex_);self.monitor_action_text_="请求发送中…";}lv_obj_delete(self.monitor_dialog_);self.monitor_dialog_=nullptr;self.RefreshMonitor();},reinterpret_cast<void*>(intptr_t(action)));
    }
    make("取消",292,[](lv_event_t*){auto& self=PocketApps::GetInstance();lv_obj_delete(self.monitor_dialog_);self.monitor_dialog_=nullptr;},nullptr);
}
