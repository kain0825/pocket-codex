// Native desktop questions are opened only after an explicit tap on the question button.
void PocketApps::RequestCodexQuestions(){
    std::lock_guard<std::mutex> g(codex_mutex_);
    if(recording_){codex_control_feedback_="请先结束录音，再查看问题";return;}
    codex_return_pending_=false;++navigation_epoch_;
    codex_question_refresh_pending_=true;codex_question_open_until_=esp_timer_get_time()+8000000;
    codex_control_feedback_="正在同步待回答问题…";
}

void PocketApps::CloseCodexQuestion(){
    auto old=codex_question_overlay_;codex_question_overlay_=nullptr;codex_question_status_=nullptr;codex_question_options_=nullptr;
    {std::lock_guard<std::mutex> g(codex_mutex_);codex_question_open_until_=0;}
    if(old)lv_obj_delete(old);
}

void PocketApps::OpenCodexQuestion(){
    if(!codex_active_||codex_question_overlay_||codex_editor_||monitor_dialog_||recording_)return;
    std::string raw,draft;{std::lock_guard<std::mutex> g(codex_mutex_);if(codex_answer_waiting_)return;raw=codex_questions_json_;draft=codex_draft_;}
    auto json=cJSON_Parse(raw.c_str());auto q=cJSON_GetArrayItem(json,0);
    auto id=cJSON_GetObjectItem(q,"id"),title=cJSON_GetObjectItem(q,"title"),options=cJSON_GetObjectItem(q,"options");
    if(!cJSON_IsString(id)||!cJSON_IsString(title)){cJSON_Delete(json);return;}
    {std::lock_guard<std::mutex> g(codex_mutex_);codex_question_id_=id->valuestring;codex_answer_.clear();codex_answer_state_.clear();codex_question_feedback_="点选一个答案即可提交";codex_question_open_until_=0;}
    auto overlay=lv_obj_create(root_);codex_question_overlay_=overlay;lv_obj_remove_style_all(overlay);lv_obj_set_pos(overlay,0,32);lv_obj_set_size(overlay,480,768);lv_obj_remove_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(overlay,lv_color_hex(0x080d1b),0);lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);
    auto label=[&](lv_obj_t* parent,const char* text,int x,int y,int width){auto l=lv_label_create(parent);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,width);lv_label_set_text(l,text);lv_obj_set_style_text_color(l,lv_color_hex(0xe8f9ff),0);lv_obj_set_style_text_line_space(l,5,0);return l;};
    label(overlay,"待回答 / 当前聊天",24,18,432);
    auto panel=[&](int y,int h){auto p=lv_obj_create(overlay);lv_obj_set_pos(p,24,y);lv_obj_set_size(p,432,h);lv_obj_set_style_bg_color(p,lv_color_hex(0x0b1d2c),0);lv_obj_set_style_bg_grad_dir(p,LV_GRAD_DIR_NONE,0);lv_obj_set_style_border_color(p,lv_color_hex(0x32aeb3),0);lv_obj_set_style_pad_all(p,12,0);lv_obj_set_scroll_dir(p,LV_DIR_VER);return p;};
    auto prompt=panel(64,200);label(prompt,title->valuestring,0,0,398);
    auto list=panel(280,344);codex_question_options_=list;lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(list,10,0);
    auto option=[&](const char* text,int index){auto b=Button(list,text,[](lv_event_t* e){PocketApps::GetInstance().SelectCodexAnswer(int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));},reinterpret_cast<void*>(intptr_t(index)));lv_obj_set_width(b,396);lv_obj_set_height(b,LV_SIZE_CONTENT);lv_obj_set_style_min_height(b,64,0);lv_obj_set_style_pad_all(b,12,0);auto l=lv_obj_get_child(b,0);lv_obj_set_width(l,366);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);return b;};
    for(int i=0;i<cJSON_GetArraySize(options);i++){auto value=cJSON_GetArrayItem(options,i);if(cJSON_IsString(value))option(value->valuestring,i);}
    if(cJSON_IsTrue(cJSON_GetObjectItem(q,"canWrite"))&&!draft.empty()){
        option("使用当前草稿作答",-1);auto preview=label(list,draft.c_str(),0,0,396);lv_obj_set_height(preview,LV_SIZE_CONTENT);
    }
    codex_question_status_=label(overlay,"点选一个答案即可提交",24,638,432);lv_obj_set_height(codex_question_status_,58);lv_label_set_long_mode(codex_question_status_,LV_LABEL_LONG_DOT);
    auto close=Button(overlay,"关闭，稍后回答",[](lv_event_t*){PocketApps::GetInstance().CloseCodexQuestion();});lv_obj_set_pos(close,24,704);lv_obj_set_size(close,432,48);
    cJSON_Delete(json);
}

void PocketApps::SelectCodexAnswer(int index){
    std::lock_guard<std::mutex> g(codex_mutex_);if(codex_answer_waiting_)return;
    codex_answer_.clear();
    auto json=cJSON_Parse(codex_questions_json_.c_str());
    for(int i=0;i<cJSON_GetArraySize(json);i++){
        auto q=cJSON_GetArrayItem(json,i);auto id=cJSON_GetObjectItem(q,"id");
        if(!cJSON_IsString(id)||codex_question_id_!=id->valuestring)continue;
        auto value=cJSON_GetArrayItem(cJSON_GetObjectItem(q,"options"),index);
        if(index==-1&&cJSON_IsTrue(cJSON_GetObjectItem(q,"canWrite")))codex_answer_=codex_draft_;
        else if(index>=0&&cJSON_IsString(value))codex_answer_=value->valuestring;
        break;
    }cJSON_Delete(json);
    if(codex_answer_waiting_||codex_answer_.empty()||codex_question_id_.empty())return;
    char id[33];snprintf(id,sizeof(id),"%08x%08x%08x%08x",unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()));
    codex_answer_request_=id;codex_answer_pending_=true;codex_answer_waiting_=true;codex_answer_state_="pending";codex_question_feedback_="正在提交回答…";
}

void PocketApps::RefreshCodexQuestions(){
    bool open=false,close=false;std::string text;bool waiting=false;
    {std::lock_guard<std::mutex> g(codex_mutex_);
        if(codex_question_open_until_){
            open=codex_question_count_>0;
            if(!open&&esp_timer_get_time()>codex_question_open_until_){codex_question_open_until_=0;codex_control_feedback_=codex_questions_available_?"当前没有待回答问题":"问题同步暂不可用，请检查电脑";}
        }
        if(codex_question_overlay_){
            if(codex_answer_state_=="answered"){close=true;codex_control_feedback_="回答已送达当前聊天";codex_answer_state_.clear();}
            else if(!codex_answer_waiting_&&codex_questions_available_&&codex_questions_json_.find(codex_question_id_)==std::string::npos){close=true;codex_control_feedback_="该问题已处理或不再有效";}
            text=codex_question_feedback_;waiting=codex_answer_waiting_;
        }
    }
    if(close){CloseCodexQuestion();return;}
    if(open)OpenCodexQuestion();
    if(codex_question_status_)lv_label_set_text(codex_question_status_,text.empty()?"点选一个答案即可提交":text.c_str());
    if(codex_question_options_)for(uint32_t i=0;i<lv_obj_get_child_count(codex_question_options_);i++){
        auto option=lv_obj_get_child(codex_question_options_,i);
        if(waiting)lv_obj_add_state(option,LV_STATE_DISABLED);else lv_obj_remove_state(option,LV_STATE_DISABLED);
    }
}

void PocketApps::CodexQuestionNetwork(){
    bool refresh,pending,waiting;std::string qid,answer,request;
    {std::lock_guard<std::mutex> g(codex_mutex_);refresh=codex_question_refresh_pending_;codex_question_refresh_pending_=false;pending=codex_answer_pending_;codex_answer_pending_=false;waiting=codex_answer_waiting_;qid=codex_question_id_;answer=codex_answer_;request=codex_answer_request_;}
    if(refresh)CodexHttp("/device/questions/refresh","{}");
    if(waiting){
        std::string response;
        if(pending){auto body=cJSON_CreateObject();cJSON_AddStringToObject(body,"requestId",request.c_str());cJSON_AddStringToObject(body,"questionId",qid.c_str());cJSON_AddStringToObject(body,"answer",answer.c_str());cJSON_AddBoolToObject(body,"confirmed",true);char* raw=cJSON_PrintUnformatted(body);response=CodexHttp("/device/answer",raw?raw:"{}");cJSON_free(raw);cJSON_Delete(body);}
        else response=CodexHttp("/device/answer?id="+request);
        auto ack=cJSON_Parse(response.c_str());auto state=cJSON_GetObjectItem(ack,"state"),detail=cJSON_GetObjectItem(ack,"detail"),rid=cJSON_GetObjectItem(ack,"requestId");
        {std::lock_guard<std::mutex> g(codex_mutex_);
            if(cJSON_IsString(state)){codex_answer_state_=state->valuestring;codex_answer_waiting_=codex_answer_state_=="pending"||codex_answer_state_=="submitted";codex_question_feedback_=cJSON_IsString(detail)?detail->valuestring:"回答结果未确认，请检查电脑";if(cJSON_IsString(rid))codex_answer_request_=rid->valuestring;}
            else codex_question_feedback_="连接中断，正在查询回答结果…";
        }cJSON_Delete(ack);
    }
    if(refresh||esp_timer_get_time()-codex_question_poll_us_>3000000){
        codex_question_poll_us_=esp_timer_get_time();auto response=CodexHttp("/device/questions");if(recording_)return;auto ack=cJSON_Parse(response.c_str());auto questions=cJSON_GetObjectItem(ack,"questions");
        if(cJSON_IsArray(questions)){char* encoded=cJSON_PrintUnformatted(questions);std::lock_guard<std::mutex> g(codex_mutex_);codex_questions_available_=cJSON_IsTrue(cJSON_GetObjectItem(ack,"available"));codex_questions_json_=encoded?encoded:"[]";codex_question_count_=cJSON_GetArraySize(questions);cJSON_free(encoded);}cJSON_Delete(ack);
    }
}
