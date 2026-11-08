#include "pocket_public_config.h"
#include "esp_http_client.h"
#include "codex_chat_skin.inc"
#include "codex_tabs.inc"
#include "codex_draft_storage.inc"
#include "codex_editor_tools.inc"

std::string PocketApps::CodexHttp(const std::string& path,const std::string& body,const std::string& file){
    int status=0;esp_err_t error=ESP_OK;
    auto finish=[&](){std::lock_guard<std::mutex> g(codex_mutex_);codex_http_status_=status;codex_http_error_=error;++codex_http_attempts_;if(error==ESP_OK&&(status==200||status==202))codex_http_success_us_=esp_timer_get_time();};
    std::string url=std::string("https://")+POCKET_PC_HOST+":8791"+path;
    if(path!="/device/time")url+=(path.find('?')==std::string::npos?"?thread=":"&thread=")+codex_selected_thread_;
    esp_http_client_config_t cfg={};cfg.url=url.c_str();cfg.cert_pem=kCodexCert;cfg.common_name="pocket-codex.local";cfg.timeout_ms=12000;cfg.buffer_size=2048;
    auto client=esp_http_client_init(&cfg);if(!client){error=ESP_ERR_NO_MEM;finish();return {};}
    esp_http_client_set_header(client,"Authorization",(std::string("Bearer ")+kCodexToken).c_str());
    esp_http_client_set_header(client,"Connection","close");
    bool post=!body.empty()||!file.empty();FILE* source=nullptr;int length=body.size();
    if(!file.empty()){
        source=fopen(file.c_str(),"rb");if(!source){esp_http_client_cleanup(client);return {};}
        fseek(source,0,SEEK_END);length=ftell(source);rewind(source);
        if(length<44||length>int(kCodexVoiceMaxBytes+44)){fclose(source);esp_http_client_cleanup(client);return {};}
    }
    esp_http_client_set_method(client,post?HTTP_METHOD_POST:HTTP_METHOD_GET);
    if(post)esp_http_client_set_header(client,"Content-Type",source?"audio/wav":"application/json");
    error=esp_http_client_open(client,post?length:0);bool ok=error==ESP_OK;
    char buffer[2048];int offset=0;
    while(ok&&post&&offset<length){
        int count=std::min(int(sizeof(buffer)),length-offset);
        const char* data=source?buffer:body.data()+offset;
        if(source){count=fread(buffer,1,count,source);data=buffer;}
        if(count<=0){ok=false;error=ESP_FAIL;break;}
        int sent=0;while(sent<count){int n=esp_http_client_write(client,data+sent,count-sent);if(n<=0){ok=false;error=ESP_FAIL;break;}sent+=n;}
        offset+=count;
    }
    if(source)fclose(source);
    std::string response;
    if(ok&&esp_http_client_fetch_headers(client)>=0){
        status=esp_http_client_get_status_code(client);
        if(status==200||status==202){int n;size_t cap=(path.rfind("/device/tasks",0)==0||path.rfind("/device/job",0)==0)?65536:12000;while(response.size()<cap&&(n=esp_http_client_read(client,buffer,sizeof(buffer)))>0)response.append(buffer,n);if(response.size()>=cap)response.clear();}
    }
    if(ok&&status==0)error=ESP_FAIL;
    esp_http_client_close(client);esp_http_client_cleanup(client);finish();return response;
}

void PocketApps::OpenCodexChat(bool immediate){
    if(recording_){RestoreOpenApp();return;}
    if(reader_active_){RestoreOpenApp();return;}
    auto render=[this](){
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        if(RestoreOpenApp())return;
        MakeRoot(("CODEX / "+codex_chat_title_).c_str());codex_dashboard_=false;
        LoadCodexTabs();RememberCodexTab(codex_selected_thread_,codex_chat_title_);
        for(size_t i=0;i<codex_tabs_.size();i++)if(codex_tabs_[i].id==codex_selected_thread_)codex_tab_page_=int(i/3);
        lv_obj_add_flag(lv_obj_get_child(root_,0),LV_OBJ_FLAG_HIDDEN);
        auto heading=lv_obj_get_child(root_,1);lv_label_set_text(heading,codex_chat_title_.c_str());
        CodexChatTitleFont=codex_pixel_28;CodexChatTitleFont.fallback=lv_obj_get_style_text_font(root_,LV_PART_MAIN);
        lv_obj_set_pos(heading,80,54);lv_obj_set_size(heading,376,36);lv_label_set_long_mode(heading,LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(heading,&CodexChatTitleFont,0);lv_obj_set_style_text_color(heading,lv_color_hex(0xe8f9ff),0);
        lv_obj_add_flag(body_,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(heading,LV_OBJ_FLAG_HIDDEN);
        auto tab_fallback=lv_obj_get_style_text_font(root_,LV_PART_MAIN);CodexTabTextFont=lv_font_montserrat_14;CodexTabTextFont.fallback=tab_fallback;CodexTabTextFont.line_height=tab_fallback->line_height;CodexTabTextFont.base_line=tab_fallback->base_line;
        codex_tab_prev_=CodexTabNav(root_,24,-1);codex_tab_next_=CodexTabNav(root_,420,1);
        auto turn=[](lv_event_t* e){PocketApps::GetInstance().TurnCodexTabPage(int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));};
        lv_obj_add_event_cb(codex_tab_prev_,turn,LV_EVENT_SHORT_CLICKED,reinterpret_cast<void*>(intptr_t(-1)));
        lv_obj_add_event_cb(codex_tab_next_,turn,LV_EVENT_SHORT_CLICKED,reinterpret_cast<void*>(intptr_t(1)));
        codex_tabs_panel_=lv_obj_create(root_);lv_obj_remove_style_all(codex_tabs_panel_);
        lv_obj_set_pos(codex_tabs_panel_,66,40);lv_obj_set_size(codex_tabs_panel_,348,56);lv_obj_remove_flag(codex_tabs_panel_,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(codex_tabs_panel_,LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_remove_flag(codex_tabs_panel_,LV_OBJ_FLAG_SCROLL_MOMENTUM);lv_obj_remove_flag(codex_tabs_panel_,LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_set_scrollbar_mode(codex_tabs_panel_,LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_event_cb(codex_tabs_panel_,[](lv_event_t* e){PocketApps::GetInstance().CodexTabTouch(e);},LV_EVENT_ALL,reinterpret_cast<void*>(uintptr_t(0xffff)));
        codex_tab_page_label_=CodexChatText(root_,"1 / 1",66,96,348,0x73aabf);lv_obj_set_height(codex_tab_page_label_,14);lv_obj_set_style_text_font(codex_tab_page_label_,&lv_font_montserrat_14,0);lv_obj_set_style_text_align(codex_tab_page_label_,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_line_space(codex_tab_page_label_,0,0);
        auto button_label=[](lv_obj_t* b,int x,int w,uint32_t c){auto l=lv_obj_get_child(b,0);lv_obj_set_align(l,LV_ALIGN_LEFT_MID);lv_obj_set_x(l,x);lv_obj_set_width(l,w);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_LEFT,0);lv_obj_set_style_text_color(l,lv_color_hex(c),0);return l;};

        auto state=lv_obj_create(root_);codex_state_panel_=state;CodexChatFrame(state,0x1c5872);lv_obj_set_size(state,432,50);
        codex_state_dot_=lv_obj_create(state);lv_obj_remove_style_all(codex_state_dot_);lv_obj_set_pos(codex_state_dot_,14,19);lv_obj_set_size(codex_state_dot_,10,10);lv_obj_set_style_bg_opa(codex_state_dot_,LV_OPA_COVER,0);lv_obj_remove_flag(codex_state_dot_,LV_OBJ_FLAG_CLICKABLE);
        codex_state_label_=CodexChatText(state,"连接中",34,10,190,0x72d8f2);lv_obj_set_height(codex_state_label_,30);lv_label_set_long_mode(codex_state_label_,LV_LABEL_LONG_DOT);
        codex_stop_button_=Button(state,"停止",[](lv_event_t*){PocketApps::GetInstance().ConfirmCodexStop();});
        CodexChatFrame(codex_stop_button_,0xed6a8f);lv_obj_set_pos(codex_stop_button_,336,6);lv_obj_set_size(codex_stop_button_,88,38);
        button_label(codex_stop_button_,24,70,0xff8ca9);CodexChatIcon(codex_stop_button_,8,13,3,1,0xff8ca9);
        codex_status_=CodexChatText(root_,"正在连接电脑…",28,110,424,0x73aabf);lv_obj_set_height(codex_status_,29);lv_label_set_long_mode(codex_status_,LV_LABEL_LONG_DOT);

        codex_question_button_=Button(root_,"待回答",[](lv_event_t*){PocketApps::GetInstance().RequestCodexQuestions();});
        CodexChatFrame(codex_question_button_,0xffcd48,0x24271f);lv_obj_set_pos(codex_question_button_,300,102);lv_obj_set_size(codex_question_button_,156,44);
        button_label(codex_question_button_,38,106,0xffda64);CodexChatIcon(codex_question_button_,10,10,5,2,0xffcd48);

        codex_reply_frame_=lv_obj_create(root_);CodexChatFrame(codex_reply_frame_,0x1c5872);
        lv_obj_remove_flag(codex_reply_frame_,LV_OBJ_FLAG_CLICKABLE);
        codex_reply_panel_=lv_obj_create(codex_reply_frame_);lv_obj_remove_style_all(codex_reply_panel_);
        lv_obj_add_flag(codex_reply_panel_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(codex_reply_panel_,LV_DIR_VER);
        lv_obj_remove_flag(codex_reply_panel_,LV_OBJ_FLAG_OVERFLOW_VISIBLE);lv_obj_set_style_pad_row(codex_reply_panel_,12,0);lv_obj_set_flex_flow(codex_reply_panel_,LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_color(codex_reply_panel_,lv_color_hex(0x3bd5cc),LV_PART_SCROLLBAR);lv_obj_set_style_width(codex_reply_panel_,3,LV_PART_SCROLLBAR);
        codex_rendered_messages_="\xff";codex_chat_layout_=-1;codex_actions_layout_=-1;codex_mode_drawn_=-1;

        codex_draft_panel_=lv_obj_create(root_);CodexChatFrame(codex_draft_panel_,0xd99c4a,0x211b16);
        codex_draft_heading_=CodexChatText(codex_draft_panel_,"",14,8,196,0xffc46b);lv_obj_set_height(codex_draft_heading_,32);lv_label_set_long_mode(codex_draft_heading_,LV_LABEL_LONG_DOT);
        codex_continue_button_=Button(codex_draft_panel_,"继续",[](lv_event_t*){PocketApps::GetInstance().ContinueCodex();});
        CodexChatFrame(codex_continue_button_,0x36bda5);lv_obj_set_pos(codex_continue_button_,324,6);lv_obj_set_size(codex_continue_button_,94,36);
        lv_obj_remove_flag(codex_continue_button_,LV_OBJ_FLAG_EVENT_BUBBLE);
        button_label(codex_continue_button_,24,54,0x82e5d3);CodexChatIcon(codex_continue_button_,8,12,4,1,0x82e5d3);
        lv_obj_add_event_cb(codex_draft_panel_,[](lv_event_t* e){PocketApps::GetInstance().CodexDraftTouch(e);},LV_EVENT_ALL,nullptr);
        codex_draft_body_=lv_obj_create(codex_draft_panel_);lv_obj_remove_style_all(codex_draft_body_);lv_obj_set_scroll_dir(codex_draft_body_,LV_DIR_VER);
        lv_obj_remove_flag(codex_draft_body_,LV_OBJ_FLAG_OVERFLOW_VISIBLE);lv_obj_add_flag(codex_draft_body_,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(codex_draft_body_,lv_color_hex(0x3bd5cc),LV_PART_SCROLLBAR);lv_obj_set_style_width(codex_draft_body_,3,LV_PART_SCROLLBAR);
        lv_obj_add_event_cb(codex_draft_body_,[](lv_event_t* e){PocketApps::GetInstance().CodexDraftTouch(e);},LV_EVENT_ALL,nullptr);
        codex_draft_label_=CodexChatText(codex_draft_body_,"",0,0,390);

        const char* names[]={"语音输入","停止并转写","引导"};
        for(int i=0;i<3;i++){
            auto b=Button(i==2?state:root_,names[i],[](lv_event_t* e){PocketApps::GetInstance().CodexAction(int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));},reinterpret_cast<void*>(intptr_t(i)));
            codex_buttons_[i]=b;CodexChatFrame(b,i==2?0x217b9d:0xffd45d,i==2?0x07344b:0xffc332);
            lv_obj_set_pos(b,i==2?240:24,i==2?6:684);lv_obj_set_size(b,i==2?88:236,i==2?38:64);
            button_label(b,i==2?24:53,i==2?100:350,i==2?0x9edff2:0x102131);
            CodexChatIcon(b,i==2?8:20,i==2?13:20,i==2?2:1,i==2?1:2,i==2?0x9edff2:0x102131);
        }
        codex_send_button_=Button(codex_draft_panel_,"发送",[](lv_event_t*){PocketApps::GetInstance().CodexAction(2);});
        CodexChatFrame(codex_send_button_,0x217b9d);lv_obj_set_pos(codex_send_button_,222,6);lv_obj_set_size(codex_send_button_,94,36);lv_obj_remove_flag(codex_send_button_,LV_OBJ_FLAG_EVENT_BUBBLE);
        button_label(codex_send_button_,24,54,0x9edff2);CodexChatIcon(codex_send_button_,8,12,2,1,0x9edff2);
        for(int i=0;i<2;i++){
            auto b=Button(root_,i?"快速":"核对",[](lv_event_t* e){PocketApps::GetInstance().SetCodexMode(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))!=0);},reinterpret_cast<void*>(intptr_t(i)));
            codex_mode_buttons_[i]=b;CodexChatFrame(b,0x29495b);lv_obj_set_pos(b,i?368:270,684);lv_obj_set_size(b,88,64);
            auto label=lv_obj_get_child(b,0);lv_obj_set_align(label,LV_ALIGN_CENTER);lv_obj_set_size(label,72,LV_SIZE_CONTENT);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
        }
        codex_cancel_button_=Button(root_,"取消录音",[](lv_event_t*){PocketApps::GetInstance().CancelCodexCapture();});
        CodexChatFrame(codex_cancel_button_,0xd48ba3);lv_obj_set_pos(codex_cancel_button_,270,684);lv_obj_set_size(codex_cancel_button_,186,64);CodexChatCenterButton(codex_cancel_button_);lv_obj_add_flag(codex_cancel_button_,LV_OBJ_FLAG_HIDDEN);
        codex_mode_hint_=CodexChatText(root_,"核对：先检查草稿，再发送",24,756,432,0x73aabf);lv_obj_set_height(codex_mode_hint_,34);
        lv_obj_update_layout(root_);
        for(auto b:codex_buttons_)CodexChatCenterButton(b);
        CodexChatCenterButton(codex_stop_button_,8);CodexChatCenterButton(codex_continue_button_,6);CodexChatCenterButton(codex_send_button_,6);
        for(auto rail:edge_rails_)lv_obj_move_foreground(rail);
        mkdir("/sdcard/PKOS/CODEX",0775);mkdir(CodexRecordDirectory().c_str(),0775);codex_active_=true;
        {std::lock_guard<std::mutex> g(codex_mutex_);if(codex_state_==1)codex_state_=0;codex_feedback_="正在连接电脑…";codex_connected_=false;}
        if(codex_job_.empty()){
            std::string id,text;if(ReadCodexDraft(id,text)){std::lock_guard<std::mutex> g(codex_mutex_);codex_job_=id;codex_draft_=PocketInput::HasText(text)?text:"";codex_state_=codex_draft_.empty()?0:4;}
        }
        codex_timer_=lv_timer_create([](lv_timer_t*){PocketApps::GetInstance().RefreshCodex();},400,nullptr);
        if(!monitor_task_started_)monitor_task_started_=xTaskCreate([](void* a){static_cast<PocketApps*>(a)->MonitorWorker();},"pc_monitor",12288,this,2,nullptr)==pdPASS;
        RefreshCodex();
    };
    if(immediate)render();else Application::GetInstance().Schedule(std::move(render));
}

// Caller holds the Codex state lock; preserve the previous text until its backup commits.
void PocketApps::AcceptCodexTranscript(const std::string& text){
    if(!PocketInput::HasText(text)){codex_auto_send_=false;codex_job_=codex_voice_original_job_;codex_voice_original_job_.clear();codex_state_=PocketInput::HasText(codex_draft_)?4:0;codex_feedback_="未得到有效转写，原草稿和录音已保留";return;}
    if(!ArchiveCodexDraft()){codex_auto_send_=false;codex_job_=codex_voice_original_job_;codex_voice_original_job_.clear();codex_state_=PocketInput::HasText(codex_draft_)?4:0;codex_feedback_="上一轮备份失败，新转写留在电脑；原草稿保留";return;}
    codex_draft_=text;codex_voice_original_job_.clear();bool saved=StoreCodexDraft(codex_job_,codex_draft_);
    if(codex_auto_send_&&saved){codex_auto_send_=false;codex_state_=5;codex_pending_=2;codex_feedback_="转写完成，正在自动发送…";}
    else{codex_auto_send_=false;codex_state_=4;codex_feedback_=saved?"请核对草稿，再确认发送":"新草稿保存失败，内容暂留当前页面";}
}

void PocketApps::FinishCodexCapture(int action){
    std::lock_guard<std::mutex> g(codex_mutex_);
    codex_voice_limit_reached_=action==3&&RecordingSeconds()>=kCodexVoiceMaxSeconds;
    if(record_error_||record_bytes_<12800||!PocketInput::Audible(record_signal_samples_)){
        codex_state_=PocketInput::HasText(codex_draft_)?4:0;codex_auto_send_=false;codex_pending_=0;
        codex_feedback_=record_error_?"录音异常，原文件和草稿已保留":record_bytes_<12800?"录音过短，未上传；原草稿保留":"未检测到声音，未上传；原草稿保留";return;
    }
    char id[33];snprintf(id,sizeof(id),"%08x%08x%08x%08x",unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()));
    codex_voice_original_job_=codex_job_;codex_auto_send_=action==1&&codex_quick_mode_;codex_job_=id;codex_file_=CodexRecordDirectory()+record_name_;codex_state_=2;codex_feedback_="录音已保存，等待上传";codex_pending_=1;
}

void PocketApps::CodexAction(int action){
    if(!codex_active_||codex_dashboard_||sleeping_)return;
    if(!codex_close_tab_id_.empty()&&action!=3)return;
    if(action==0){
        if(recording_)return;
        record_test_mode_=false;record_test_stall_ms_=0;
        {std::lock_guard<std::mutex> g(codex_mutex_);if(codex_state_==8&&!codex_file_.empty()){codex_state_=2;codex_pending_=1;codex_feedback_="正在重新上传已保存的录音…";return;}}
        {std::lock_guard<std::mutex> g(codex_mutex_);codex_control_feedback_.clear();}
        {std::lock_guard<std::mutex> g(codex_mutex_);if(codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_continue_waiting_)return;}
        {std::lock_guard<std::mutex> g(codex_mutex_);if(PocketInput::HasText(codex_draft_)&&codex_state_!=6&&!StoreCodexDraft(codex_job_,codex_draft_)){codex_feedback_="原草稿保存失败，请先检查存储卡";return;}}
        int before;{std::lock_guard<std::mutex> g(codex_mutex_);before=codex_state_;}
        StartRecording();
        std::lock_guard<std::mutex> g(codex_mutex_);
        if(recording_){codex_before_record_state_=before;codex_state_=1;codex_auto_send_=false;codex_voice_limit_reached_=false;codex_feedback_="正在录音，原草稿已保留…";}
        else codex_feedback_="录音未开始，请检查存储卡";
    }else if(action==1||action==3){
        if(record_test_mode_){StopRecording();RefreshCodex();return;}
        if(!recording_){if(action==1)ConfirmCodexStop();return;}
        StopRecording();
        FinishCodexCapture(action);
    }else if(action==2){
        if(monitor_dialog_)return;
        {std::lock_guard<std::mutex> g(codex_mutex_);
            if(recording_||codex_state_!=4||!PocketInput::HasText(codex_draft_)||!codex_connected_||codex_continue_waiting_||codex_stop_waiting_||codex_answer_waiting_)return;
            // The explicit Send/Guide tap is the user's send action. Claim the
            // draft immediately so a second tap cannot enqueue another send.
            codex_state_=5;codex_pending_=2;codex_auto_send_=false;codex_control_feedback_.clear();codex_feedback_="正在发送…";
        }
        RefreshCodex();
    }
}

void PocketApps::CancelCodexCapture(){
    if(!codex_active_||codex_dashboard_||!recording_)return;
    StopRecording();bool moved=false;
    {
        std::lock_guard<std::mutex> storage(storage_mutex_);
        auto folder=record_path_.substr(0,record_path_.find_last_of('/')+1)+"CANCEL/";mkdir(folder.c_str(),0775);
        for(unsigned i=0;i<32;i++){
            auto name=i?record_name_.substr(0,record_name_.size()-4)+"_"+std::to_string(i)+".WAV":record_name_;auto destination=folder+name;
            if(access(destination.c_str(),F_OK)==0)continue;
            moved=rename(record_path_.c_str(),destination.c_str())==0;break;
        }
    }
    {
        std::lock_guard<std::mutex> g(codex_mutex_);
        codex_pending_=0;codex_auto_send_=false;codex_voice_limit_reached_=false;
        codex_state_=(codex_before_record_state_==0||codex_before_record_state_==4||codex_before_record_state_==6)?codex_before_record_state_:PocketInput::HasText(codex_draft_)?4:0;
        codex_control_feedback_="本次录音已取消";codex_feedback_=moved?"已取消，不转写；原录音留在取消记录":"已取消，不转写；原录音暂留原位置";
        codex_cancelled_bytes_=record_bytes_.load();++codex_cancelled_;
    }
    RefreshCodex();
}

void PocketApps::ConfirmCodexStop(){
    {std::lock_guard<std::mutex> g(codex_mutex_);
        if(!codex_running_||!codex_connected_||codex_turn_.empty()||codex_stop_waiting_||monitor_dialog_)return;
        codex_stop_turn_=codex_turn_;
    }
    auto shade=lv_obj_create(root_);monitor_dialog_=shade;lv_obj_set_size(shade,480,800);lv_obj_set_style_bg_color(shade,lv_color_black(),0);lv_obj_set_style_bg_opa(shade,LV_OPA_80,0);
    auto box=lv_obj_create(shade);lv_obj_set_size(box,420,270);lv_obj_center(box);lv_obj_set_flex_flow(box,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_bg_color(box,lv_color_hex(0x0b1d2c),0);lv_obj_set_style_bg_grad_dir(box,LV_GRAD_DIR_NONE,0);
    auto label=lv_label_create(box);lv_obj_set_width(label,380);lv_obj_set_style_text_color(label,lv_color_hex(0xe8f9ff),0);lv_label_set_text(label,"停止当前 Codex 任务？\n已完成的操作不会撤回。");
    Button(box,"停止当前任务",[](lv_event_t*){auto& s=PocketApps::GetInstance();{
        std::lock_guard<std::mutex> g(s.codex_mutex_);
        if(s.codex_turn_==s.codex_stop_turn_&&s.codex_running_){
            char id[33];snprintf(id,sizeof(id),"%08x%08x%08x%08x",unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()));
            s.codex_stop_id_=id;s.codex_stop_pending_=true;s.codex_stop_waiting_=true;s.codex_control_feedback_="正在停止当前任务…";
        }else s.codex_control_feedback_="任务已变化，请刷新后操作";
    }lv_obj_delete(s.monitor_dialog_);s.monitor_dialog_=nullptr;});
    Button(box,"取消",[](lv_event_t*){auto& s=PocketApps::GetInstance();lv_obj_delete(s.monitor_dialog_);s.monitor_dialog_=nullptr;});
}

void PocketApps::RefreshCodex(){
    if(!codex_active_||codex_dashboard_||!codex_status_||codex_editor_)return;
    if(ClosePendingCodexTab())return;
    if(OpenSelectedCodexChat())return;
    if(codex_return_pending_&&ReturnCodexDashboard())return;
    if(recording_&&(RecordingSeconds()>=kCodexVoiceMaxSeconds||record_error_))CodexAction(3);
    RefreshCodexQuestions();
    RefreshCodexTabs();
    std::lock_guard<std::mutex> g(codex_mutex_);
    auto seconds=RecordingSeconds();char elapsed[24];snprintf(elapsed,sizeof(elapsed),"%02u:%02u",seconds/60,seconds%60);
    auto feedback=recording_?std::string(codex_connected_?"已连接当前聊天":"电脑未连接，录音本地保存"):codex_feedback_;
    if(codex_connected_&&feedback=="正在连接电脑…")feedback=codex_state_==4?"草稿已恢复，核对后可发送":"已连接当前聊天";
    if(!recording_&&codex_voice_limit_reached_)feedback="已达10分钟并保存 · "+feedback;
    if(!recording_&&!codex_control_feedback_.empty())feedback=codex_control_feedback_;
    if(feedback!=lv_label_get_text(codex_status_))lv_label_set_text(codex_status_,feedback.c_str());
    std::string caption=recording_?std::string("录音中 ")+elapsed:codex_state_==2?"上传中…":codex_state_==3?"转写中…":codex_state_==5?"发送中…":codex_state_==7?"保存中…":codex_state_==6?"已发送":!codex_draft_.empty()?"待发送":"";
    if(caption!=lv_label_get_text(codex_draft_heading_))lv_label_set_text(codex_draft_heading_,caption.c_str());
    const char* activity=!codex_connected_?"未连接":codex_stop_waiting_?"停止中":codex_running_?"执行中":"空闲";
    if(strcmp(activity,lv_label_get_text(codex_state_label_)))lv_label_set_text(codex_state_label_,activity);
    uint32_t state_color=!codex_connected_?0x6f8fa1:codex_running_?0x2ce9c7:0x77b5cb;
    if((lv_color_to_u32(lv_obj_get_style_bg_color(codex_state_dot_,LV_PART_MAIN))&0xffffff)!=state_color)lv_obj_set_style_bg_color(codex_state_dot_,lv_color_hex(state_color),0);
    auto set_label=[](lv_obj_t* button,const char* text){auto l=lv_obj_get_child(button,0);if(strcmp(text,lv_label_get_text(l))){lv_label_set_text(l,text);CodexChatCenterButton(button);}};
    if(codex_mode_drawn_!=int(codex_quick_mode_)){codex_mode_drawn_=int(codex_quick_mode_);for(int i=0;i<2;i++){
        auto button=codex_mode_buttons_[i];bool selected=(i==1)==codex_quick_mode_;
        if(selected)lv_obj_add_state(button,LV_STATE_CHECKED);else lv_obj_remove_state(button,LV_STATE_CHECKED);
        uint32_t border=selected?0x35e1c2:0x29495b,bg=selected?0x125a52:0x061b2c,color=selected?0xd5fff2:0x7196a8;
        for(auto state:{uint32_t(0),uint32_t(LV_STATE_DISABLED)}){
            lv_obj_set_style_border_color(button,lv_color_hex(border),state);
            lv_obj_set_style_bg_color(button,lv_color_hex(bg),state);
        }
        auto label=lv_obj_get_child(button,0);if((lv_color_to_u32(lv_obj_get_style_text_color(label,LV_PART_MAIN))&0xffffff)!=color)lv_obj_set_style_text_color(label,lv_color_hex(color),0);
    }}
    const char* mode_hint=codex_quick_mode_?"快速：停止后自动转写并发送":"核对：先检查草稿，再发送";
    if(strcmp(mode_hint,lv_label_get_text(codex_mode_hint_)))lv_label_set_text(codex_mode_hint_,mode_hint);
    set_label(codex_buttons_[0],codex_state_==8?"重试上传":"语音输入");
    set_label(codex_buttons_[1],codex_quick_mode_?"停止并发送":"停止并转写");
    LayoutCodexActions(codex_running_,!codex_draft_.empty()&&codex_state_!=6&&!recording_);
    bool busy=recording_||codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7;
    auto enable=[](lv_obj_t* o,bool yes){if(yes)lv_obj_remove_state(o,LV_STATE_DISABLED);else lv_obj_add_state(o,LV_STATE_DISABLED);};
    for(auto button:codex_mode_buttons_)enable(button,!busy&&!codex_continue_waiting_);
    enable(codex_stop_button_,codex_running_&&codex_connected_&&!codex_turn_.empty()&&!codex_stop_waiting_);
    enable(codex_continue_button_,codex_connected_&&!busy&&!codex_stop_waiting_&&!codex_answer_waiting_&&!codex_continue_waiting_);
    enable(codex_send_button_,codex_state_==4&&PocketInput::HasText(codex_draft_)&&codex_connected_&&!codex_stop_waiting_&&!codex_continue_waiting_);
    enable(codex_question_button_,!recording_);
    std::string notice="待回答 "+std::to_string(codex_question_count_);auto notice_label=lv_obj_get_child(codex_question_button_,0);if(notice!=lv_label_get_text(notice_label)){lv_label_set_text(notice_label,notice.c_str());CodexChatCenterButton(codex_question_button_,6);}
    bool show_draft=codex_state_!=6&&!codex_draft_.empty();
    LayoutCodexChat(codex_question_count_>0,show_draft||busy||codex_state_==8);
    if(!recording_)RenderCodexMessages();
    const char* draft=recording_||codex_state_==6?"":codex_draft_.empty()?(busy?feedback.c_str():codex_state_==8?"原录音已保留，可重试上传":""):codex_draft_.c_str();
    if(strcmp(draft,lv_label_get_text(codex_draft_label_)))lv_label_set_text(codex_draft_label_,draft);
    uint32_t draft_color=codex_draft_.empty()&&!busy&&codex_state_!=8?0x73aabf:0xe8f9ff;
    if((lv_color_to_u32(lv_obj_get_style_text_color(codex_draft_label_,LV_PART_MAIN))&0xffffff)!=draft_color)lv_obj_set_style_text_color(codex_draft_label_,lv_color_hex(draft_color),0);
    if(recording_){lv_obj_add_flag(codex_buttons_[0],LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(codex_buttons_[1],LV_OBJ_FLAG_HIDDEN);}
    else{lv_obj_remove_flag(codex_buttons_[0],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(codex_buttons_[1],LV_OBJ_FLAG_HIDDEN);}
    if(recording_){lv_obj_remove_flag(codex_cancel_button_,LV_OBJ_FLAG_HIDDEN);for(auto b:codex_mode_buttons_)lv_obj_add_flag(b,LV_OBJ_FLAG_HIDDEN);}
    else{lv_obj_add_flag(codex_cancel_button_,LV_OBJ_FLAG_HIDDEN);for(auto b:codex_mode_buttons_)lv_obj_remove_flag(b,LV_OBJ_FLAG_HIDDEN);}
    bool enabled[]={!busy&&!codex_continue_waiting_,recording_.load(),codex_state_==4&&PocketInput::HasText(codex_draft_)&&codex_connected_&&!codex_stop_waiting_&&!codex_continue_waiting_};
    for(int i=0;i<3;i++){
        enable(codex_buttons_[i],enabled[i]);
        uint32_t color=enabled[i]?(i==2?0x9edff2:0x102131):0x63849a;
        auto l=lv_obj_get_child(codex_buttons_[i],0);if((lv_color_to_u32(lv_obj_get_style_text_color(l,LV_PART_MAIN))&0xffffff)!=color)lv_obj_set_style_text_color(l,lv_color_hex(color),0);
    }

}

void PocketApps::SetCodexMode(bool quick){
    {std::lock_guard<std::mutex> g(codex_mutex_);
        if(recording_||codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_continue_waiting_)return;
        codex_quick_mode_=quick;codex_control_feedback_.clear();
        codex_feedback_=quick?"快速模式：停止后自动转写发送":"核对模式：先检查草稿再发送";
    }
    RefreshCodex();
}

void PocketApps::EditCodex(){
    if(!codex_active_||codex_editor_||recording_)return;
    std::string text;{std::lock_guard<std::mutex> g(codex_mutex_);if(codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_continue_waiting_)return;text=codex_draft_;}
    if(text.size()>32000||text.find('\0')!=std::string::npos||PocketInput::Characters(text.c_str())>kCodexDraftMaxChars){std::lock_guard<std::mutex> g(codex_mutex_);codex_control_feedback_.clear();codex_feedback_="草稿超过8000字编辑范围，原文保留且未截断";return;}
    codex_editing_=true;
    auto editor=lv_obj_create(root_);codex_editor_=editor;lv_obj_remove_style_all(editor);lv_obj_set_pos(editor,0,32);lv_obj_set_size(editor,480,768);lv_obj_remove_flag(editor,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(editor,lv_color_hex(0x080d1b),0);lv_obj_set_style_bg_opa(editor,LV_OPA_COVER,0);
    auto title=lv_label_create(editor);codex_editor_notice_=title;lv_label_set_text(title,"编辑草稿 / 点文字定位光标");lv_obj_set_pos(title,24,18);lv_obj_set_style_text_color(title,lv_color_hex(0xe8f9ff),0);
    auto ta=lv_textarea_create(editor);codex_input_=ta;lv_obj_set_pos(ta,24,64);lv_obj_set_size(ta,432,228);if(!CodexEditorLoad(ta,text)){FinishCodexEdit(false);std::lock_guard<std::mutex> g(codex_mutex_);codex_control_feedback_.clear();codex_feedback_="编辑加载未完成，原草稿保持不变";return;}lv_textarea_set_cursor_click_pos(ta,true);lv_obj_set_style_bg_color(ta,lv_color_hex(0x0b1d2c),0);lv_obj_set_style_bg_grad_dir(ta,LV_GRAD_DIR_NONE,0);lv_obj_set_style_text_color(ta,lv_color_hex(0xe8f9ff),0);lv_obj_set_style_border_color(ta,lv_color_hex(0x32e6cf),0);lv_obj_set_style_border_color(ta,lv_color_hex(0xffcf63),LV_PART_CURSOR);lv_obj_set_style_text_color(ta,lv_color_hex(0xffffff),LV_PART_CURSOR);lv_obj_set_style_border_width(ta,2,LV_PART_CURSOR);lv_obj_set_style_border_side(ta,LV_BORDER_SIDE_LEFT,LV_PART_CURSOR);lv_obj_set_style_bg_opa(ta,LV_OPA_TRANSP,LV_PART_CURSOR);lv_obj_set_style_anim_duration(ta,500,LV_PART_CURSOR);lv_obj_add_state(ta,LV_STATE_FOCUSED);lv_textarea_set_cursor_pos(ta,LV_TEXTAREA_CURSOR_LAST);
    const char* labels[]={"退格","清空","取消","保存"};
    for(int i=0;i<4;i++){auto b=Button(editor,labels[i],[](lv_event_t* e){auto& s=PocketApps::GetInstance();int action=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));if(action==0)lv_textarea_delete_char(s.codex_input_);else if(action==1)lv_textarea_set_text(s.codex_input_,"");else s.FinishCodexEdit(action==3);},reinterpret_cast<void*>(intptr_t(i)));lv_obj_set_pos(b,24+i*112,308);lv_obj_set_size(b,100,44);}
    const char* tools[]={"上一轮","复制全文","粘贴"};
    for(int i=0;i<3;i++){auto b=Button(editor,tools[i],[](lv_event_t* e){auto& s=PocketApps::GetInstance();int k=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));if(k==0)s.ShowCodexPrevious();else if(k==1)s.CodexEditorCopy(false);else s.CodexEditorPaste();},reinterpret_cast<void*>(intptr_t(i)));lv_obj_set_pos(b,24+i*148,360);lv_obj_set_size(b,136,44);}
    auto ime=lv_ime_pinyin_create(editor);codex_ime_=ime;lv_obj_set_style_text_color(ime,lv_color_hex(0xe8f9ff),0);
    auto kb=lv_keyboard_create(editor);lv_obj_set_align(kb,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(kb,0,470);codex_keyboard_=kb;lv_obj_set_size(kb,480,286);lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_TEXT_LOWER);lv_ime_pinyin_set_keyboard(ime,kb);lv_keyboard_set_textarea(kb,ta);lv_ime_pinyin_set_mode(ime,LV_IME_PINYIN_MODE_K9);
    lv_obj_set_style_bg_color(kb,lv_color_hex(0x080d1b),0);lv_obj_set_style_bg_color(kb,lv_color_hex(0x193548),LV_PART_ITEMS);lv_obj_set_style_bg_color(kb,lv_color_hex(0x326779),LV_PART_ITEMS|LV_STATE_PRESSED);lv_obj_set_style_text_color(kb,lv_color_hex(0xe8f9ff),LV_PART_ITEMS);
    auto panel=lv_ime_pinyin_get_cand_panel(ime);lv_obj_set_align(panel,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(panel,12,406);lv_obj_set_size(panel,456,52);lv_obj_set_style_bg_color(panel,lv_color_hex(0x193548),0);lv_obj_set_style_text_color(panel,lv_color_hex(0xffe5a0),LV_PART_ITEMS);lv_obj_set_style_bg_color(panel,lv_color_hex(0x193548),LV_PART_ITEMS);
    lv_obj_add_event_cb(kb,[](lv_event_t* e){if(lv_event_get_code(e)==LV_EVENT_READY)PocketApps::GetInstance().FinishCodexEdit(true);},LV_EVENT_ALL,nullptr);
    auto cursor_style=uint32_t(LV_PART_CURSOR)|uint32_t(LV_STATE_FOCUSED);lv_obj_set_style_border_width(ta,2,cursor_style);lv_obj_set_style_border_side(ta,LV_BORDER_SIDE_LEFT,cursor_style);lv_obj_set_style_border_color(ta,lv_color_hex(0xffcf63),cursor_style);lv_obj_set_style_anim_duration(ta,500,cursor_style);lv_obj_set_style_bg_opa(ta,LV_OPA_TRANSP,cursor_style);
    lv_obj_add_state(ta,LV_STATE_FOCUSED);
}

void PocketApps::FinishCodexEdit(bool save){
    if(!codex_editor_)return;
    if(save){
        std::lock_guard<std::mutex> g(codex_mutex_);
        codex_draft_=lv_textarea_get_text(codex_input_);if(!PocketInput::HasText(codex_draft_))codex_draft_.clear();
        if(codex_job_.empty()||codex_state_==6){char id[33];snprintf(id,sizeof(id),"%08x%08x%08x%08x",unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()));codex_job_=id;}
        if(!StoreCodexDraft(codex_job_,codex_draft_)){if(codex_editor_notice_)lv_label_set_text(codex_editor_notice_,"保存失败，编辑内容仍保留在此页");return;}
        codex_state_=7;codex_pending_=3;codex_auto_send_=false;codex_feedback_=codex_draft_.empty()?"草稿已清空，正在同步保存…":"草稿已修改，正在同步保存…";
    }
    auto old=codex_editor_;codex_editing_=false;codex_editor_=nullptr;codex_input_=nullptr;codex_keyboard_=nullptr;codex_ime_=nullptr;codex_editor_notice_=nullptr;codex_history_overlay_=nullptr;lv_obj_delete_async(old);
}

void PocketApps::CodexNetwork(){
    std::lock_guard<std::mutex> session(codex_session_mutex_);
    if(!codex_active_||recording_||codex_editing_||codex_test_network_pause_)return;
    auto yield_to_ui=[this](){std::lock_guard<std::mutex> g(codex_mutex_);return recording_.load()||!codex_select_id_.empty();};
    if(yield_to_ui())return;
    if(!WifiManager::GetInstance().IsConnected()){
        std::lock_guard<std::mutex> g(codex_mutex_);codex_connected_=false;codex_tasks_connected_=false;codex_feedback_="正在连接 Wi-Fi…";return;
    }
    if(codex_dashboard_){
        auto raw=CodexHttp("/device/tasks");auto doc=cJSON_Parse(raw.c_str());auto list=cJSON_GetObjectItem(doc,"tasks"),revision=cJSON_GetObjectItem(doc,"revision");
        UpdateCodexUsage(doc);
        std::lock_guard<std::mutex> g(codex_mutex_);codex_tasks_connected_=cJSON_IsTrue(cJSON_GetObjectItem(doc,"connected"));
        if(cJSON_IsArray(list)&&cJSON_IsString(revision)){char* encoded=cJSON_PrintUnformatted(list);codex_tasks_json_=encoded?encoded:"[]";cJSON_free(encoded);codex_tasks_revision_=revision->valuestring;}
        cJSON_Delete(doc);return;
    }
    // The state endpoint opens the selected scope; question snapshots refresh independently.
    // Do not synchronously refresh the desktop question UI on every tab switch.
    codex_select_pending_=false;
    bool do_continue;std::string continue_id,continue_thread;
    {std::lock_guard<std::mutex> g(codex_mutex_);do_continue=codex_continue_pending_;codex_continue_pending_=false;continue_id=codex_continue_id_;continue_thread=codex_continue_thread_;}
    if(do_continue){
        std::string response;
        if(continue_thread==codex_selected_thread_)response=CodexHttp("/device/continue","{\"confirmed\":true,\"requestId\":\""+continue_id+"\"}");
        auto ack=cJSON_Parse(response.c_str());auto detail=cJSON_GetObjectItem(ack,"detail");bool ok=cJSON_IsTrue(cJSON_GetObjectItem(ack,"ok"));
        {std::lock_guard<std::mutex> g(codex_mutex_);codex_continue_waiting_=false;codex_control_feedback_=ok?"已发送继续":cJSON_IsString(detail)?detail->valuestring:"继续的发送结果未确认，请检查电脑";}
        cJSON_Delete(ack);
    }
    auto raw=CodexHttp("/device/state");if(yield_to_ui())return;auto json=cJSON_Parse(raw.c_str());
    UpdateCodexUsage(json);
    {std::lock_guard<std::mutex> g(codex_mutex_);
        codex_connected_=cJSON_IsTrue(cJSON_GetObjectItem(json,"connected"));
        auto running=cJSON_GetObjectItem(json,"status");codex_running_=cJSON_IsString(running)&&std::string(running->valuestring)=="active";
        auto turn=cJSON_GetObjectItem(json,"activeTurnId");codex_turn_=cJSON_IsString(turn)?turn->valuestring:"";
          auto reply=cJSON_GetObjectItem(json,"reply");if(cJSON_IsString(reply))codex_reply_=reply->valuestring;
        auto pending=cJSON_GetObjectItem(json,"draftId");if(codex_job_.empty()&&codex_state_==0&&cJSON_IsString(pending)){codex_job_=pending->valuestring;codex_state_=3;codex_feedback_="正在恢复未发送草稿…";}
        if(json){codex_updates_++;PocketClock::Get().RequestSync();}
        if(!codex_connected_)codex_feedback_="电脑未连接，等待重连";
        else if(codex_state_==0)codex_feedback_="已连接当前开发板聊天";
    }cJSON_Delete(json);
    if(!codex_active_||yield_to_ui())return;
    CodexQuestionNetwork();
    if(yield_to_ui())return;
    CodexTabsNetwork();
    if(yield_to_ui())return;
    bool stop_pending,stop_waiting;std::string stop_id,stop_turn;
    {std::lock_guard<std::mutex> g(codex_mutex_);stop_pending=codex_stop_pending_;codex_stop_pending_=false;stop_waiting=codex_stop_waiting_;stop_id=codex_stop_id_;stop_turn=codex_stop_turn_;}
    if(stop_waiting){
        std::string response;
        if(stop_pending){
            auto body=cJSON_CreateObject();cJSON_AddStringToObject(body,"requestId",stop_id.c_str());cJSON_AddStringToObject(body,"expectedTurnId",stop_turn.c_str());cJSON_AddBoolToObject(body,"confirmed",true);
            char* encoded=cJSON_PrintUnformatted(body);response=CodexHttp("/device/stop",encoded?encoded:"{}");cJSON_free(encoded);cJSON_Delete(body);
        }else response=CodexHttp("/device/control?id="+stop_id);
        auto ack=cJSON_Parse(response.c_str());auto status=cJSON_GetObjectItem(ack,"state"),detail=cJSON_GetObjectItem(ack,"detail");
        {std::lock_guard<std::mutex> g(codex_mutex_);
            if(cJSON_IsString(status)){
                std::string st=status->valuestring;codex_stop_waiting_=st=="pending";
                codex_control_feedback_=cJSON_IsString(detail)?detail->valuestring:"停止结果未确认，请检查电脑";
                if(st=="interrupted"||st=="finished")codex_running_=false;
            }else codex_control_feedback_="连接中断，正在查询停止结果…";
        }cJSON_Delete(ack);
    }
    int action=codex_pending_.exchange(0);std::string job,file,draft;int state;
    {std::lock_guard<std::mutex> g(codex_mutex_);job=codex_job_;file=codex_file_;draft=codex_draft_;state=codex_state_;}
    if(action==1){
        auto response=CodexHttp("/device/audio?id="+job,"",file);auto ack=cJSON_Parse(response.c_str());
        std::lock_guard<std::mutex> g(codex_mutex_);codex_state_=ack?3:8;codex_feedback_=ack?"电脑正在本地转写…":"上传未完成，录音已保存；点重试上传";cJSON_Delete(ack);
    }else if(action==2||action==3){
        if(action==2&&!PocketInput::HasText(draft)){std::lock_guard<std::mutex> g(codex_mutex_);codex_state_=0;codex_auto_send_=false;codex_feedback_="草稿为空，未发送";return;}
        auto body=cJSON_CreateObject();cJSON_AddStringToObject(body,"id",job.c_str());cJSON_AddStringToObject(body,"text",draft.c_str());cJSON_AddBoolToObject(body,"confirmed",action==2);char* encoded=cJSON_PrintUnformatted(body);std::string payload=encoded?encoded:"";cJSON_free(encoded);cJSON_Delete(body);
        auto response=CodexHttp(action==2?"/device/send":"/device/draft",payload);auto ack=cJSON_Parse(response.c_str());
        bool accepted=cJSON_IsTrue(cJSON_GetObjectItem(ack,"ok"));auto detail=cJSON_GetObjectItem(ack,"detail");std::string delivery_detail=cJSON_IsString(detail)?detail->valuestring:"";cJSON_Delete(ack);
        std::lock_guard<std::mutex> g(codex_mutex_);codex_state_=action==2&&accepted?6:draft.empty()?0:4;
        if(action==3)codex_feedback_=accepted?(draft.empty()?"草稿已清空":"草稿已保存，核对后发送"):"电脑未确认保存，草稿暂留当前页面";
        else{codex_control_feedback_.clear();codex_delivery_pending_=accepted;codex_feedback_=accepted?(delivery_detail.empty()?"已发送，等待当前聊天回复":delivery_detail):"结果未确认，检查电脑后可重试";if(accepted){ArchiveCodexDraft();RemoveCodexDraft();codex_draft_.clear();}}
    }else if(state==6&&!job.empty()&&codex_delivery_pending_){
        auto response=CodexHttp("/device/job?id="+job);auto ack=cJSON_Parse(response.c_str());
        auto delivery=cJSON_GetObjectItem(ack,"delivery"),detail=cJSON_GetObjectItem(ack,"detail");
        if(cJSON_IsString(delivery)){
            std::lock_guard<std::mutex> g(codex_mutex_);std::string status=delivery->valuestring;
            if(cJSON_IsString(detail))codex_feedback_=detail->valuestring;
            if(status=="steered"||status=="started")codex_delivery_pending_=false;
        }cJSON_Delete(ack);
    }else if(state==3&&!job.empty()){
        auto response=CodexHttp("/device/job?id="+job);auto ack=cJSON_Parse(response.c_str());
        auto status=cJSON_GetObjectItem(ack,"state"),text=cJSON_GetObjectItem(ack,"text");
        if(cJSON_IsString(status)){
            std::lock_guard<std::mutex> g(codex_mutex_);std::string st=status->valuestring;
            if(st=="ready"&&cJSON_IsString(text))AcceptCodexTranscript(text->valuestring);
            else if(st=="too_long"){codex_auto_send_=false;codex_job_=codex_voice_original_job_;codex_voice_original_job_.clear();codex_state_=PocketInput::HasText(codex_draft_)?4:0;codex_feedback_="转写超过8000字，完整文本在电脑；原草稿保留";}
            else if(st=="failed"||st=="empty"||st=="missing"||st=="ready"){codex_auto_send_=false;codex_job_=codex_voice_original_job_;codex_voice_original_job_.clear();codex_state_=PocketInput::HasText(codex_draft_)?4:0;codex_feedback_="未得到有效转写，原草稿和录音已保留";}
            else codex_feedback_="电脑正在本地转写…";
        }cJSON_Delete(ack);
    }
}

#include "codex_questions.inc"
#include "codex_dashboard.inc"
