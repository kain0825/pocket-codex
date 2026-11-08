#pragma once
#include <string>
#include <lvgl.h>
#include "pocket_calc_model.h"

class PocketCalculator {
public:
    static PocketCalculator& Get(){static PocketCalculator v;return v;}
    void Open(lv_obj_t* parent);
    void Close();
    void Key(const char* key);
    bool IsOpen() const{return root_!=nullptr;}
    lv_obj_t* Button(unsigned i) const{return i<20?buttons_[i]:nullptr;}
    std::string Diagnostic() const;
    static std::string SelfTest();
private:
    void Render();
    lv_obj_t *root_=nullptr,*caption_=nullptr,*result_=nullptr,*buttons_[20]={};
    const lv_font_t* text_font_=nullptr;
    PocketCalcModel model_;
};

class PocketWoodfish {
public:
    static PocketWoodfish& Get(){static PocketWoodfish v;return v;}
    void Open(lv_obj_t* parent);
    void Close();
    void Knock();
    void ToggleSound();
    void Reset();
    bool IsOpen() const{return root_!=nullptr;}
    lv_obj_t* Button(unsigned i) const{return i==0?knock_:i==1?sound_:i==2?reset_:nullptr;}
    std::string Diagnostic() const;
private:
    void Render();
    bool Play();
    lv_obj_t *root_=nullptr,*count_label_=nullptr,*plus_=nullptr,*knock_=nullptr,*sound_=nullptr,*sound_label_=nullptr,*reset_=nullptr;
    unsigned count_=0,saved_=0,sounds_=0,skipped_=0;
    bool sound_enabled_=true,dirty_=false,persist_ok_=true,sound_owned_=false;
};
