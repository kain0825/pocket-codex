#include "pocket_answer_book.h"
#include "pocket_answer_cards.h"
#include "pocket_theme.h"
#include <esp_random.h>
#include <cstdio>

namespace {
lv_obj_t* Text(lv_obj_t* parent,const char* value,int width,uint32_t color){
    auto label=lv_label_create(parent);lv_obj_set_width(label,width);
    lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label,lv_color_hex(color),0);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_text(label,value);lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);return label;
}
}

void PocketAnswerBook::Open(lv_obj_t* parent){
    Close();
    current_=-1;
    root_=lv_obj_create(parent);lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_,LV_PCT(100),LV_PCT(100));
    lv_obj_remove_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(parent);const int width=lv_obj_get_width(root_);
    auto hint=Text(root_,"在心里想一个问题\n准备好了，就翻一页",width,0x8faebc);lv_obj_set_pos(hint,0,0);
    card_=lv_obj_create(root_);PocketPanel(card_,0x36cdb9);
    lv_obj_set_pos(card_,0,72);lv_obj_set_size(card_,width,390);
    lv_obj_set_style_pad_all(card_,0,0);lv_obj_remove_flag(card_,LV_OBJ_FLAG_SCROLLABLE);
    auto caption=Text(card_,"POCKET / 随机灵感",width-32,0x7fa5b5);lv_obj_set_pos(caption,16,24);
    PocketPixel(card_,24,64,width-48,2,0x22536c);
    PocketIcon(card_,(width-64)/2,108,2,0xffcf63,8);icon_=lv_obj_get_child(card_,-1);
    answer_=Text(card_,"",width-64,0xf0f8fc);
    lv_obj_set_style_text_line_space(answer_,12,0);
    page_=Text(card_,"",width-32,0x8faebc);lv_obj_set_pos(page_,16,344);
    flip_=lv_button_create(root_);PocketPanel(flip_,0xffcf63);
    lv_obj_set_pos(flip_,0,486);lv_obj_set_size(flip_,width,64);
    lv_obj_set_style_pad_all(flip_,0,0);
    lv_obj_set_style_bg_color(flip_,lv_color_hex(0xffcf63),0);
    lv_obj_set_style_bg_color(flip_,lv_color_hex(0xd5a548),LV_STATE_PRESSED);
    flip_label_=Text(flip_,"翻一页",width-32,0x17202b);lv_obj_center(flip_label_);
    lv_obj_add_event_cb(flip_,[](lv_event_t*){PocketAnswerBook::Get().Flip();},LV_EVENT_CLICKED,nullptr);
    cover_=lv_button_create(root_);PocketPanel(cover_,0x31576c);
    lv_obj_set_pos(cover_,(width-192)/2,574);lv_obj_set_size(cover_,192,48);
    lv_obj_set_style_pad_all(cover_,0,0);
    auto label=Text(cover_,"合上书",160,0xc0dae5);lv_obj_center(label);
    lv_obj_add_event_cb(cover_,[](lv_event_t*){PocketAnswerBook::Get().Cover();},LV_EVENT_CLICKED,nullptr);
    auto footer=Text(root_,"边缘侧滑返回设备",width,0x7fa5b5);lv_obj_set_pos(footer,0,630);
    Render();
}

void PocketAnswerBook::Close(){
    if(root_)lv_obj_delete(root_);
    root_=card_=answer_=page_=icon_=flip_=flip_label_=cover_=nullptr;
}

void PocketAnswerBook::Flip(){
    if(!root_)return;
    int next=PocketAnswerCards::Pick(esp_random(),recent_,recent_count_);
    if(next<0)return;
    current_=next;recent_[recent_next_]=next;recent_next_=(recent_next_+1)%8;
    if(recent_count_<8){++recent_count_;}
    ++draws_;
    Render();
}

void PocketAnswerBook::Cover(){if(root_){current_=-1;Render();}}

void PocketAnswerBook::Render(){
    if(!root_)return;
    const bool opened=current_>=0;
    lv_obj_set_style_text_color(answer_,lv_color_hex(opened?0xf0f8fc:0xc3dce6),0);
    lv_label_set_text(answer_,opened?PocketAnswerCards::Text[current_]:"答案留在下一页。\n等你准备好，再把它打开。");
    lv_obj_align(answer_,LV_ALIGN_CENTER,0,opened?-4:60);
    if(opened)lv_obj_add_flag(icon_,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(icon_,LV_OBJ_FLAG_HIDDEN);
    if(opened)lv_obj_remove_flag(cover_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(cover_,LV_OBJ_FLAG_HIDDEN);
    char page[48];if(opened)snprintf(page,sizeof(page),"第 %02d 页 / %u",current_+1,PocketAnswerCards::Count);
    else snprintf(page,sizeof(page),"%u 页小小的可能",PocketAnswerCards::Count);
    lv_label_set_text(page_,page);lv_label_set_text(flip_label_,opened?"再翻一页":"翻一页");
    lv_obj_center(flip_label_);
}

std::string PocketAnswerBook::Diagnostic() const{
    char result[192];snprintf(result,sizeof(result),"active=%d index=%d draws=%u recent=%u cards=%u timers=0 network=0 storage=0",
        int(IsOpen()),current_,draws_,recent_count_,PocketAnswerCards::Count);return result;
}
