#pragma once
#include <cstdint>
#include <string>
#include <lvgl.h>

// UI-owned state. No timer, network, storage writer or audio ownership.
class PocketAnswerBook {
public:
    static PocketAnswerBook& Get(){static PocketAnswerBook value;return value;}
    void Open(lv_obj_t* parent);
    void Close();
    void Flip();
    void Cover();
    bool IsOpen() const{return root_!=nullptr;}
    lv_obj_t* FlipButton() const{return flip_;}
    lv_obj_t* CoverButton() const{return cover_;}
    std::string Diagnostic() const;
private:
    void Render();
    lv_obj_t *root_=nullptr,*card_=nullptr,*answer_=nullptr,*page_=nullptr,*icon_=nullptr;
    lv_obj_t *flip_=nullptr,*flip_label_=nullptr,*cover_=nullptr;
    int current_=-1;
    int recent_[8]={-1,-1,-1,-1,-1,-1,-1,-1};
    unsigned recent_count_=0,recent_next_=0,draws_=0;
};
