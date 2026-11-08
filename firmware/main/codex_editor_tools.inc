// LVGL inserts text one character at a time when max_length is nonzero.
// Validate first, replace in one operation, then restore the interactive limit.
static size_t CodexEditorByteOffset(const char* text,uint32_t characters){
    size_t byte=0;
    while(text[byte]&&characters){++byte;while((static_cast<unsigned char>(text[byte])&0xc0)==0x80)++byte;--characters;}
    return byte;
}
static bool CodexEditorLoad(lv_obj_t* input,const std::string& text){
    if(text.size()>32000||text.find('\0')!=std::string::npos||PocketInput::Characters(text.c_str())>8000)return false;
    lv_textarea_set_max_length(input,0);lv_textarea_set_text(input,text.c_str());lv_textarea_set_max_length(input,8000);
    return text==lv_textarea_get_text(input);
}

void PocketApps::ShowCodexPrevious(){
    if(!codex_editor_||codex_history_overlay_)return;
    std::string job,text;if(!ReadCodexDraft(job,text,true)||!PocketInput::HasText(text)){lv_label_set_text(codex_editor_notice_,"还没有上一轮草稿");return;}
    auto overlay=lv_obj_create(codex_editor_);codex_history_overlay_=overlay;lv_obj_set_pos(overlay,0,0);lv_obj_set_size(overlay,480,768);lv_obj_remove_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(overlay,lv_color_hex(0x080d1b),0);lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);lv_obj_set_style_pad_all(overlay,0,0);lv_obj_set_style_border_width(overlay,0,0);
    CodexChatText(overlay,"上一轮草稿 / 本聊天",24,18,432,0xffcf63);
    auto scroll=lv_obj_create(overlay);lv_obj_set_pos(scroll,24,62);lv_obj_set_size(scroll,432,580);lv_obj_set_style_bg_color(scroll,lv_color_hex(0x0b1d2c),0);lv_obj_set_scroll_dir(scroll,LV_DIR_VER);
    auto body=CodexChatText(scroll,text.c_str(),0,0,396,0xe8f9ff);lv_obj_set_height(body,LV_SIZE_CONTENT);
    auto copy=Button(overlay,"复制上一轮",[](lv_event_t*){PocketApps::GetInstance().CodexEditorCopy(true);});lv_obj_set_pos(copy,24,668);lv_obj_set_size(copy,208,56);
    auto back=Button(overlay,"返回当前草稿",[](lv_event_t*){auto& s=PocketApps::GetInstance();auto old=s.codex_history_overlay_;s.codex_history_overlay_=nullptr;lv_obj_add_flag(old,LV_OBJ_FLAG_HIDDEN);lv_obj_delete_async(old);lv_obj_add_state(s.codex_input_,LV_STATE_FOCUSED);});lv_obj_set_pos(back,248,668);lv_obj_set_size(back,208,56);
}
void PocketApps::CodexEditorCopy(bool previous){
    if(!codex_editor_||!codex_input_)return;
    std::string job,history;const char* text=lv_textarea_get_text(codex_input_);
    if(previous){if(!ReadCodexDraft(job,history,true))return;text=history.c_str();}
    size_t size=strlen(text);auto buffer=static_cast<char*>(heap_caps_malloc(size+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!buffer){lv_label_set_text(codex_editor_notice_,"剪贴板空间不足");return;}
    memcpy(buffer,text,size+1);codex_clipboard_=std::shared_ptr<char>(buffer,[](char* p){heap_caps_free(p);});
    if(codex_history_overlay_){auto old=codex_history_overlay_;codex_history_overlay_=nullptr;lv_obj_add_flag(old,LV_OBJ_FLAG_HIDDEN);lv_obj_delete_async(old);}
    lv_label_set_text(codex_editor_notice_,previous?"上一轮已复制 · 点文字定位后粘贴":"当前全文已复制 · 可在光标处粘贴");lv_obj_add_state(codex_input_,LV_STATE_FOCUSED);
}
void PocketApps::CodexEditorPaste(){
    if(!codex_input_||!codex_clipboard_||codex_history_overlay_)return;
    if(PocketInput::Characters(lv_textarea_get_text(codex_input_))+PocketInput::Characters(codex_clipboard_.get())>kCodexDraftMaxChars){lv_label_set_text(codex_editor_notice_,"超过8000字，未粘贴");return;}
    std::string text=lv_textarea_get_text(codex_input_);auto cursor=lv_textarea_get_cursor_pos(codex_input_);
    size_t offset=CodexEditorByteOffset(text.c_str(),cursor);text.insert(offset,codex_clipboard_.get());
    auto next=cursor+PocketInput::Characters(codex_clipboard_.get());
    if(!CodexEditorLoad(codex_input_,text)){lv_label_set_text(codex_editor_notice_,"粘贴未完整加载，原草稿仍保留；请取消后重开");return;}
    lv_obj_add_state(codex_input_,LV_STATE_FOCUSED);lv_textarea_set_cursor_pos(codex_input_,next);lv_label_set_text(codex_editor_notice_,"已在光标位置粘贴");
}
