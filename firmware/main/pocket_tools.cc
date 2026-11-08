#include <freertos/FreeRTOS.h>
#include "pocket_tools.h"
#include "pocket_theme.h"
#include "pocket_wood_sound.h"
#include "pocket_apps.h"
#include "application.h"
#include <nvs.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <algorithm>
#include <cstring>
LV_FONT_DECLARE(codex_pixel_28);

namespace {
lv_obj_t* Text(lv_obj_t* parent,const char* text,int width,uint32_t color=0xe8edf2){
    auto o=lv_label_create(parent);lv_obj_set_width(o,width);lv_label_set_text(o,text);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
lv_obj_t* Root(lv_obj_t* parent){auto o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_size(o,LV_PCT(100),LV_PCT(100));lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_update_layout(parent);return o;}
lv_obj_t* Button(lv_obj_t* parent,const char* text,int x,int y,int w,int h,uint32_t accent,lv_event_cb_t callback,void* data=nullptr){
    auto o=lv_button_create(parent);PocketPanel(o,accent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_pad_all(o,0,0);
    auto l=Text(o,text,w-8);
    if(text[0]>='0'&&text[0]<='9'&&!text[1]){lv_obj_set_style_text_font(l,&codex_pixel_28,0);}
    lv_obj_center(l);
    lv_obj_add_event_cb(o,callback,LV_EVENT_CLICKED,data);return o;
}
void Fade(void* object,int32_t value){lv_obj_set_style_opa(static_cast<lv_obj_t*>(object),lv_opa_t(value),0);}
}

void PocketCalculator::Open(lv_obj_t* parent){
    Close();root_=Root(parent);int width=lv_obj_get_width(root_);text_font_=lv_obj_get_style_text_font(root_,LV_PART_MAIN);
    auto panel=lv_obj_create(root_);PocketPanel(panel,0x36cdb9);lv_obj_set_pos(panel,0,0);lv_obj_set_size(panel,width,142);lv_obj_set_style_pad_all(panel,0,0);lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    caption_=Text(panel,"",width-32,0x8faebc);lv_obj_set_pos(caption_,16,12);lv_obj_set_height(caption_,52);lv_label_set_long_mode(caption_,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(caption_,LV_TEXT_ALIGN_RIGHT,0);
    result_=Text(panel,"0",width-32);lv_obj_set_pos(result_,16,86);lv_obj_set_height(result_,34);lv_label_set_long_mode(result_,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(result_,LV_TEXT_ALIGN_RIGHT,0);
    static const char* keys[]={"AC","BS","%","/","7","8","9","*","4","5","6","-","1","2","3","+","SIGN","0",".","="};
    static const char* labels[]={"AC","退格","%","÷","7","8","9","×","4","5","6","−","1","2","3","+","±","0",".","="};
    int cell=(width-24)/4;
    for(unsigned i=0;i<20;++i){uint32_t color=i==19?0xffcf63:i%4==3?0x36cdb9:0x31576c;
        buttons_[i]=::Button(root_,labels[i],int(i%4)*(cell+8),160+int(i/4)*84,cell,74,color,
            [](lv_event_t* e){PocketCalculator::Get().Key(static_cast<const char*>(lv_event_get_user_data(e)));},const_cast<char*>(keys[i]));
    }
    auto note=Text(root_,"百分比支持加减基数\n边缘侧滑返回设备",width,0x7fa5b5);lv_obj_set_pos(note,0,594);Render();
}
void PocketCalculator::Close(){if(root_)lv_obj_delete(root_);root_=caption_=result_=nullptr;for(auto& b:buttons_)b=nullptr;}
void PocketCalculator::Key(const char* key){if(root_){model_.Key(key);Render();}}
void PocketCalculator::Render(){
    if(!root_)return;
    lv_label_set_text(caption_,model_.Caption().c_str());
    lv_obj_set_style_text_font(result_,model_.Error()?text_font_:&codex_pixel_28,0);
    lv_obj_set_style_text_color(result_,lv_color_hex(model_.Error()?0xffa6bc:0xe8edf2),0);lv_label_set_text(result_,model_.Value().c_str());
}
std::string PocketCalculator::Diagnostic() const{char s[192];snprintf(s,sizeof(s),"active=%d error=%d value=%s bytes=%u",int(IsOpen()),int(model_.Error()),model_.Value().c_str(),unsigned(model_.Expression().size()));return s;}
std::string PocketCalculator::SelfTest(){
    struct Case {const char* keys;const char* result;};
    const Case cases[]={{"2+3*4=","14"},{"9/3+2=","5"},{"1.5+2.25=","3.75"},{"0.1+0.2=","0.3"},{"5*0=","0"},{"8/4/2=","1"},{"3-8=","-5"},{"5*-2=","-10"},{"2+*3=","6"},{"50%=","0.5"},{"200+10%=","220"},{"200-10%=","180"},{"200*10%=","20"},{"200/10%=","2000"},{".5+.25=","0.75"},{"00.50+0=","0.5"},{"1..2+1=","2.2"},{"2+3==","5"}};
    unsigned pass=0,total=0,first_bad=0;auto verify=[&](bool ok){++total;if(ok)++pass;else if(!first_bad)first_bad=total;};
    for(auto test:cases){PocketCalcModel m;for(const char* p=test.keys;*p;++p)m.Key(std::string(1,*p));verify(!m.Error()&&m.Value()==test.result);}
    PocketCalcModel m;m.Key("9");m.Key("SIGN");m.Key("*");m.Key("2");m.Key("=");verify(m.Value()=="-18");m.Key("AC");verify(m.Value()=="0"&&!m.Error());
    m.Key("8");m.Key("/");m.Key("0");m.Key("=");verify(m.Error());m.Key("3");verify(!m.Error()&&m.Value()=="3");
    m.Key("2");m.Key("BS");verify(m.Value()=="3");m.Key("BS");verify(m.Value()=="0");
    m.Key("2");m.Key("+");m.Key("=");verify(m.Error());m.Key("AC");for(int i=0;i<110;++i)m.Key("9");verify(m.Expression().size()<=96);
    double out=0;verify(PocketCalcModel::Evaluate("1e308*1e308",out)!=0);verify(PocketCalcModel::Evaluate("1/0",out)==1);
    char result[96];snprintf(result,sizeof(result),"passed=%d cases=%u good=%u first_bad=%u",int(pass==total),total,pass,first_bad);return result;
}

void PocketWoodfish::Open(lv_obj_t* parent){
    Close();nvs_handle_t handle=0;uint32_t saved=0;uint8_t enabled=1;
    if(nvs_open("pocket_muyu",NVS_READONLY,&handle)==ESP_OK){nvs_get_u32(handle,"count",&saved);nvs_get_u8(handle,"sound",&enabled);nvs_close(handle);}
    count_=saved_=std::min<uint32_t>(saved,99999999U);sound_enabled_=enabled!=0;dirty_=false;
    root_=Root(parent);int width=lv_obj_get_width(root_);
    auto hint=Text(root_,"敲一下，慢下来",width,0x8faebc);lv_obj_set_pos(hint,0,0);
    auto heading=Text(root_,"敲击次数",width,0x7fa5b5);lv_obj_set_pos(heading,0,48);
    count_label_=Text(root_,"0",width);lv_obj_set_style_text_font(count_label_,&codex_pixel_28,0);lv_obj_set_pos(count_label_,0,88);
    knock_=::Button(root_,"",0,164,width,274,0xd49a52,[](lv_event_t*){});
    lv_obj_add_event_cb(knock_,[](lv_event_t*){PocketWoodfish::Get().Knock();},LV_EVENT_PRESSED,nullptr);
    auto picture=lv_obj_create(knock_);lv_obj_remove_style_all(picture);lv_obj_set_size(picture,144,128);lv_obj_set_pos(picture,(width-144)/2,48);lv_obj_remove_flag(picture,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(picture,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(picture,[](lv_event_t* e){
        static const uint16_t rows[]={0x03e0,0x0ff8,0x1ffc,0x3ffe,0x7fff,0xffff,0xfff7,0xffe7,0xffcf,0xff9f,0xff3f,0x7e7f,0x3ffe,0x1ffc,0x0ff8,0x03e0};
        auto object=lv_event_get_target_obj(e);lv_area_t a;lv_obj_get_coords(object,&a);lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;
        for(int y=0;y<16;++y)for(int x=0;x<16;++x)if(rows[y]&(1U<<(15-x))){d.bg_color=lv_color_hex(y<5?0xe2b273:0xba7c42);lv_area_t p={a.x1+x*9,a.y1+y*8,a.x1+x*9+8,a.y1+y*8+7};lv_draw_rect(lv_event_get_layer(e),&d,&p);}
    },LV_EVENT_DRAW_MAIN,nullptr);
    auto label=Text(knock_,"敲一下",width-24);lv_obj_set_pos(label,12,218);
    plus_=Text(knock_,"+1",width-24,0xffcf63);lv_obj_set_style_text_font(plus_,&codex_pixel_28,0);lv_obj_set_pos(plus_,12,8);lv_obj_set_style_opa(plus_,LV_OPA_TRANSP,0);
    sound_=::Button(root_,"",0,466,(width-16)/2,56,0x36cdb9,[](lv_event_t*){PocketWoodfish::Get().ToggleSound();});sound_label_=lv_obj_get_child(sound_,0);
    reset_=::Button(root_,"长按清零",(width+16)/2,466,(width-16)/2,56,0x31576c,[](lv_event_t*){});
    lv_obj_add_event_cb(reset_,[](lv_event_t*){PocketWoodfish::Get().Reset();},LV_EVENT_LONG_PRESSED,nullptr);
    auto note=Text(root_,"退出时保存计数\n边缘侧滑返回设备",width,0x7fa5b5);lv_obj_set_pos(note,0,594);Render();
}
bool PocketWoodfish::Play(){
    auto& app=Application::GetInstance();
    if(PocketApps::GetInstance().IsRecording()||app.IsChatAppOpen())return false;
    auto& audio=app.GetAudioService();
    if(!sound_owned_&&!audio.IsIdle())return false;
    bool queued=audio.PlayShortEffect(PocketWoodSound::Pcm,PocketWoodSound::Samples,16000);
    if(queued){sound_owned_=true;++sounds_;}return queued;
}
void PocketWoodfish::Knock(){
    if(!root_||PocketApps::GetInstance().IsSleeping()||PocketApps::GetInstance().IsRecording())return;
    if(count_<99999999U){++count_;}
    dirty_=true;Render();
    if(sound_enabled_&&!Play())++skipped_;
    lv_anim_delete(plus_,nullptr);lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,plus_);lv_anim_set_values(&a,255,0);lv_anim_set_duration(&a,140);lv_anim_set_exec_cb(&a,Fade);lv_anim_start(&a);
}
void PocketWoodfish::ToggleSound(){if(root_){sound_enabled_=!sound_enabled_;dirty_=true;Render();}}
void PocketWoodfish::Reset(){if(root_){count_=0;dirty_=true;Render();}}
void PocketWoodfish::Render(){if(!root_)return;char text[24];snprintf(text,sizeof(text),"%u",count_);lv_label_set_text(count_label_,text);lv_label_set_text(sound_label_,sound_enabled_?"音效：开":"音效：关");lv_obj_center(sound_label_);}
void PocketWoodfish::Close(){
    if(!root_)return;
    if(sound_owned_&&!PocketApps::GetInstance().IsRecording()&&!Application::GetInstance().IsChatAppOpen()){
        Application::GetInstance().GetAudioService().CancelShortEffects();
    }
    sound_owned_=false;
    if(dirty_){nvs_handle_t h=0;esp_err_t error=nvs_open("pocket_muyu",NVS_READWRITE,&h);if(error==ESP_OK){error=nvs_set_u32(h,"count",count_);if(error==ESP_OK)error=nvs_set_u8(h,"sound",sound_enabled_?1:0);if(error==ESP_OK)error=nvs_commit(h);nvs_close(h);}persist_ok_=error==ESP_OK;if(persist_ok_){saved_=count_;dirty_=false;}else ESP_LOGW("PocketWoodfish","count save failed: %s",esp_err_to_name(error));}
    lv_anim_delete(plus_,nullptr);lv_obj_delete(root_);root_=count_label_=plus_=knock_=sound_=sound_label_=reset_=nullptr;
}
std::string PocketWoodfish::Diagnostic() const{char s[192];snprintf(s,sizeof(s),"active=%d count=%u sound=%d sounds=%u skipped=%u dirty=%d saved=%u persist_ok=%d",int(IsOpen()),count_,int(sound_enabled_),sounds_,skipped_,int(dirty_),saved_,int(persist_ok_));return std::string(s)+" "+Application::GetInstance().GetAudioService().ShortEffectDiagnostic();}
