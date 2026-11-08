LV_FONT_DECLARE(codex_pixel_28);
static lv_font_t CodexTitleFont;
static int CodexStyleMode(){int mode=PocketApps::GetInstance().codex_project_draw_mode_;return mode>=6?0:mode;}
static bool CodexCachedAncestor(lv_obj_t* o){for(auto p=o;p;p=lv_obj_get_parent(p))if(lv_obj_has_flag(p,LV_OBJ_FLAG_USER_2))return true;return false;}
static lv_obj_t* CodexArrow(lv_obj_t* parent,int x,int y,int direction,uint32_t color){
    auto o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,direction==2?20:12,direction==2?12:20);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(o,[](lv_event_t* e){
        if(CodexCachedAncestor(lv_event_get_target_obj(e)))return;
        auto o=lv_event_get_target_obj(e);lv_area_t b;lv_obj_get_coords(o,&b);auto layer=lv_event_get_layer(e);auto key=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));int direction=key&3;uint32_t color=key>>2;
        lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;
        for(int i=0;i<5;i++){int a=abs(i-2)*4,v=i*4;int x=direction==0?a:direction==1?8-a:v,y=direction==2?8-a:v;lv_area_t q={b.x1+x,b.y1+y,b.x1+x+3,b.y1+y+3};lv_draw_rect(layer,&d,&q);}
    },LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>((uintptr_t(color)<<2)|direction));
    return o;
}
std::string PocketApps::CodexRecordDirectory() const{return codex_test_storage_?"/sdcard/PKOS/TEST/DRAFT/":"/sdcard/PKOS/CODEX/"+codex_selected_thread_+"/";}
std::string PocketApps::CodexDraftPath() const{return CodexRecordDirectory()+"DRAFT.JSON";}

void PocketApps::SaveCodexPage(){
    if(!codex_reply_panel_)return;
    std::lock_guard<std::mutex> g(codex_mutex_);if(codex_reply_.empty())return;
    if(std::find(codex_closed_tabs_.begin(),codex_closed_tabs_.end(),codex_selected_thread_)!=codex_closed_tabs_.end())return;
    auto data=static_cast<char*>(heap_caps_malloc(codex_reply_.size()+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!data)return;
    memcpy(data,codex_reply_.c_str(),codex_reply_.size()+1);
    auto it=std::find_if(codex_pages_.begin(),codex_pages_.end(),[&](const auto& p){return p.id==codex_selected_thread_;});
    if(it!=codex_pages_.end())codex_pages_.erase(it);
    codex_pages_.push_back({codex_selected_thread_,std::shared_ptr<char>(data,[](char* p){heap_caps_free(p);}),codex_reply_.size(),int(lv_obj_get_scroll_y(codex_reply_panel_))});
    size_t bytes=0;for(auto& p:codex_pages_)bytes+=p.bytes;
    while(codex_pages_.size()>64||bytes>512*1024){bytes-=codex_pages_.front().bytes;codex_pages_.erase(codex_pages_.begin());}
}
void PocketApps::RestoreCodexPage(){
    auto it=std::find_if(codex_pages_.begin(),codex_pages_.end(),[&](const auto& p){return p.id==codex_selected_thread_;});
    codex_cached_page_=it!=codex_pages_.end();codex_cached_scroll_pending_=false;
    if(codex_cached_page_){codex_reply_.assign(it->data.get(),it->bytes);codex_cached_scroll_=it->scroll;codex_cached_scroll_pending_=true;++codex_cache_hits_;}
    else ++codex_cache_misses_;
}

void PocketApps::SaveCodexDraft(){
    std::lock_guard<std::mutex> g(codex_mutex_);
    if(codex_job_.empty()||codex_draft_.empty()||codex_state_==6)return;
    mkdir("/sdcard/PKOS/CODEX",0775);mkdir(CodexRecordDirectory().c_str(),0775);
    StoreCodexDraft(codex_voice_original_job_.empty()?codex_job_:codex_voice_original_job_,codex_draft_);
}

bool PocketApps::ReturnCodexDashboard(){
    if(codex_question_overlay_||codex_editor_||monitor_dialog_){codex_return_pending_=false;return false;}
    if(recording_){CodexAction(3);return false;}
    {std::lock_guard<std::mutex> g(codex_mutex_);if(codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_stop_waiting_||codex_answer_waiting_||codex_continue_waiting_){codex_control_feedback_="请等当前操作完成后再切换";return false;}}
    std::unique_lock<std::mutex> session(codex_session_mutex_,std::try_to_lock);
    if(!session.owns_lock()){codex_return_pending_=true;std::lock_guard<std::mutex> g(codex_mutex_);codex_control_feedback_="正在完成同步，随后返回任务列表";return false;}
    codex_return_pending_=false;Close(false,false);OpenCodex(true);return true;
}

void PocketApps::SelectCodexChat(const std::string& id,const std::string& title){
    if(codex_dashboard_&&codex_dashboard_swipe_)return;
    if(id.size()!=36||id.find_first_not_of("0123456789abcdef-")!=std::string::npos)return;
    LoadCodexTabs();
    if(codex_tabs_.size()>=CodexTabLimit&&std::none_of(codex_tabs_.begin(),codex_tabs_.end(),[&](const auto& t){return t.id==id;})){
        if(codex_task_status_)lv_label_set_text(codex_task_status_,"卡片已满，请先关闭一张");
        return;
    }
    ESP_LOGI(TAG,"Select chat id=%s",id.c_str());
    if(codex_task_list_)codex_dashboard_scroll_=lv_obj_get_scroll_y(codex_task_list_);
    {std::lock_guard<std::mutex> g(codex_mutex_);
        if(!codex_dashboard_&&(id==codex_selected_thread_||recording_||codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_stop_waiting_||codex_answer_waiting_||codex_continue_waiting_||codex_editor_||codex_question_overlay_||monitor_dialog_))return;
        codex_select_id_=id;codex_select_title_=title;
    }
    auto epoch=navigation_epoch_;Application::GetInstance().Schedule([this,epoch](){DisplayLockGuard lock(Board::GetInstance().GetDisplay());if(navigation_epoch_!=epoch||!codex_active_)return;if(codex_dashboard_)RefreshCodexDashboard();else RefreshCodex();});
}

bool PocketApps::OpenSelectedCodexChat(){
    std::string selected,title;
    {std::lock_guard<std::mutex> g(codex_mutex_);selected=codex_select_id_;title=codex_select_title_;}
    if(selected.empty())return false;
    if(!codex_dashboard_){
        std::lock_guard<std::mutex> g(codex_mutex_);
        if(recording_||codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_stop_waiting_||codex_answer_waiting_||codex_continue_waiting_||codex_editor_||codex_question_overlay_||monitor_dialog_){codex_select_id_.clear();codex_control_feedback_="请等当前操作完成后再切换";return false;}
    }
        std::unique_lock<std::mutex> session(codex_session_mutex_,std::try_to_lock);if(!session.owns_lock())return false;
    ActivateCodexChat(selected,title);return true;
}

void PocketApps::ActivateCodexChat(const std::string& selected,const std::string& title){
        Close(false,false);
        {std::lock_guard<std::mutex> g(codex_mutex_);
            codex_selected_thread_=selected;codex_chat_title_=title;codex_select_id_.clear();codex_select_title_.clear();codex_select_pending_=true;
            codex_reply_.clear();codex_draft_.clear();codex_job_.clear();codex_file_.clear();codex_voice_original_job_.clear();codex_feedback_="正在连接所选聊天…";
            RestoreCodexPage();
            codex_state_=0;codex_pending_=0;codex_connected_=false;codex_running_=false;codex_auto_send_=false;codex_delivery_pending_=false;
            codex_continue_pending_=false;codex_continue_waiting_=false;codex_continue_thread_.clear();
            codex_turn_.clear();codex_stop_turn_.clear();codex_stop_id_.clear();codex_control_feedback_.clear();codex_stop_pending_=false;codex_stop_waiting_=false;
            codex_questions_json_="[]";codex_question_id_.clear();codex_answer_.clear();codex_answer_request_.clear();codex_answer_state_.clear();codex_question_feedback_.clear();codex_question_count_=0;codex_questions_available_=false;codex_question_refresh_pending_=false;codex_answer_pending_=false;codex_answer_waiting_=false;codex_question_open_until_=0;codex_question_poll_us_=0;
        }
        OpenCodexChat(true);
}

void PocketApps::OpenCodex(bool immediate){
    if(recording_){RestoreOpenApp();return;}
    if(reader_active_){RestoreOpenApp();return;}
    auto render=[this](){
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());if(RestoreOpenApp())return;
        MakeRoot("Codex");CodexTitleFont=codex_pixel_28;CodexTitleFont.fallback=lv_obj_get_style_text_font(root_,LV_PART_MAIN);lv_obj_add_flag(lv_obj_get_child(root_,0),LV_OBJ_FLAG_HIDDEN);lv_obj_set_pos(lv_obj_get_child(root_,1),88,50);lv_obj_set_style_text_font(lv_obj_get_child(root_,1),&CodexTitleFont,0);PocketIcon(root_,28,48,0,0x91e6f2,5);lv_obj_add_flag(body_,LV_OBJ_FLAG_HIDDEN);
        if(PocketNeon()){lv_obj_add_flag(lv_obj_get_child(root_,2),LV_OBJ_FLAG_HIDDEN);}PocketPixel(root_,24,154,432,2,0x22536c);
        {Settings settings("pocket_ui");codex_project_draw_mode_=settings.GetInt("project_draw",7);}
        codex_dashboard_=true;codex_active_=true;codex_return_pending_=false;codex_tasks_drawn_.clear();codex_dashboard_dirty_=true;codex_restore_scroll_=true;
        static_cast<LcdDisplay*>(Board::GetInstance().GetDisplay())->SetInteractiveRefresh(true);
        auto refresh=Button(root_,"刷新",[](lv_event_t*){auto& s=PocketApps::GetInstance();s.codex_dashboard_dirty_=true;});lv_obj_set_pos(refresh,360,48);lv_obj_set_size(refresh,96,44);PocketPanel(refresh,0x294c65);
        codex_task_status_=lv_label_create(root_);lv_obj_set_pos(codex_task_status_,88,88);lv_obj_set_size(codex_task_status_,260,24);lv_label_set_long_mode(codex_task_status_,LV_LABEL_LONG_DOT);lv_label_set_text(codex_task_status_,"正在读取电脑任务…");lv_obj_set_style_text_color(codex_task_status_,lv_color_hex(0x86dcd7),0);
        for(int mode=0;mode<2;mode++){
            auto b=Button(root_,mode?"最近对话":"按项目",[](lv_event_t* e){auto& s=PocketApps::GetInstance();if(!s.codex_dashboard_swipe_)s.SetCodexDashboardMode(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))!=0);},reinterpret_cast<void*>(intptr_t(mode)));
            codex_dashboard_modes_[mode]=b;lv_obj_set_pos(b,mode?24:252,114);lv_obj_set_size(b,204,34);PocketPanel(b,0x294c65);lv_obj_center(lv_obj_get_child(b,0));lv_obj_add_flag(b,LV_OBJ_FLAG_EVENT_BUBBLE);
        }
        lv_obj_add_event_cb(root_,[](lv_event_t* e){PocketApps::GetInstance().CodexDashboardTouch(e);},LV_EVENT_ALL,nullptr);
        auto list=lv_obj_create(root_);codex_task_list_=list;lv_obj_remove_style_all(list);lv_obj_set_pos(list,24,160);lv_obj_set_size(list,432,600);lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(list,16,0);lv_obj_set_scroll_dir(list,LV_DIR_VER);lv_obj_set_style_bg_color(list,lv_color_hex(0x52758b),LV_PART_SCROLLBAR);lv_obj_add_flag(list,LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_remove_flag(list,LV_OBJ_FLAG_SCROLL_MOMENTUM);lv_obj_remove_flag(list,LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_set_scroll_snap_y(list,LV_SCROLL_SNAP_NONE);lv_obj_set_style_anim_duration(list,0,0);
        auto hint=lv_label_create(root_);lv_label_set_text(hint,"左右切换视图 · 边缘返回");lv_obj_set_style_text_color(hint,lv_color_hex(0x75bcd6),0);lv_obj_set_pos(hint,24,768);lv_obj_set_width(hint,432);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
        for(auto rail:edge_rails_)lv_obj_move_foreground(rail);
        codex_timer_=lv_timer_create([](lv_timer_t*){PocketApps::GetInstance().RefreshCodexDashboard();},400,nullptr);
        if(!monitor_task_started_)monitor_task_started_=xTaskCreate([](void* a){static_cast<PocketApps*>(a)->MonitorWorker();},"pc_monitor",12288,this,2,nullptr)==pdPASS;
        RefreshCodexDashboard();
    };
    if(immediate)render();else Application::GetInstance().Schedule(std::move(render));
}

void PocketApps::SetCodexDashboardMode(bool recent){
    if(!codex_active_||!codex_dashboard_||!codex_task_list_||recent==codex_dashboard_recent_)return;
    codex_dashboard_scrolls_[codex_dashboard_recent_]=lv_obj_get_scroll_y(codex_task_list_);
    if(!codex_dashboard_recent_){codex_saved_project_pages_.clear();for(const auto& p:codex_projects_)codex_saved_project_pages_.push_back({p.id,p.page});}
    codex_dashboard_recent_=recent;codex_dashboard_scroll_=codex_dashboard_scrolls_[recent];codex_restore_scroll_=true;
    codex_tasks_drawn_.clear();codex_dashboard_dirty_=true;++navigation_epoch_;
}
void PocketApps::CodexDashboardTouch(lv_event_t* e){
    if(!codex_dashboard_)return;
    auto code=lv_event_get_code(e);
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESS_LOST&&code!=LV_EVENT_RELEASED)return;
    auto input=lv_event_get_indev(e);if(!input)return;
    if(code==LV_EVENT_PRESSED){lv_indev_get_point(input,&codex_dashboard_origin_);codex_dashboard_touch_=true;codex_dashboard_swipe_=false;codex_swipe_consumed_=false;}
    if(code==LV_EVENT_PRESS_LOST)codex_dashboard_touch_=false;
    if(code==LV_EVENT_RELEASED&&codex_dashboard_touch_){
        codex_dashboard_touch_=false;lv_point_t p;lv_indev_get_point(input,&p);int dx=p.x-codex_dashboard_origin_.x,dy=p.y-codex_dashboard_origin_.y;
        if(!codex_swipe_consumed_&&codex_dashboard_origin_.x>20&&codex_dashboard_origin_.x<460&&abs(dx)>=70&&abs(dx)>2*abs(dy)){
            codex_dashboard_swipe_=true;SetCodexDashboardMode(!codex_dashboard_recent_);
        }
    }
}

void PocketApps::RefreshCodexDashboard(){
    if(!codex_active_||!codex_dashboard_||!codex_task_list_)return;
    std::string selected,title,raw,revision;bool connected;
    {std::lock_guard<std::mutex> g(codex_mutex_);selected=codex_select_id_;title=codex_select_title_;raw=codex_tasks_json_;revision=codex_tasks_revision_;connected=codex_tasks_connected_;}
    if(!selected.empty()){lv_label_set_text(codex_task_status_,"正在打开所选聊天…");OpenSelectedCodexChat();return;}
    auto indev=lv_indev_get_next(nullptr);
    bool touching=indev&&lv_indev_get_state(indev)==LV_INDEV_STATE_PRESSED;
    if(!codex_restore_scroll_)codex_dashboard_scroll_=lv_obj_get_scroll_y(codex_task_list_);
    if(!touching&&revision!=codex_tasks_drawn_){
        auto doc=cJSON_Parse(raw.c_str());
        if(cJSON_IsArray(doc)){
            std::vector<CodexProjectTile> groups;
            std::vector<CodexTaskTile> recent;
            for(int i=0;i<cJSON_GetArraySize(doc);i++){
                auto row=cJSON_GetArrayItem(doc,i);auto str=[&](const char* key){auto v=cJSON_GetObjectItem(row,key);return cJSON_IsString(v)?std::string(v->valuestring):std::string();};
                auto id=str("id");if(id.empty())continue;auto pid=str("projectId");if(pid.empty())pid="unassigned";
                auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& p){return p.id==pid;});
                if(group==groups.end()){CodexProjectTile p;p.id=pid;p.name=str("project");if(p.name.empty())p.name="未分组聊天";auto old=std::find_if(codex_projects_.begin(),codex_projects_.end(),[&](const auto& x){return x.id==pid;});if(old!=codex_projects_.end())p.page=old->page;else for(const auto& saved:codex_saved_project_pages_)if(saved.first==pid)p.page=saved.second;groups.push_back(p);group=groups.end()-1;}
                auto q=cJSON_GetObjectItem(row,"questions");CodexTaskTile task{id,str("title"),str("status"),str("label"),cJSON_IsNumber(q)?q->valueint:-1};task.project=str("project");auto updated=cJSON_GetObjectItem(row,"updatedAt");task.updated=cJSON_IsNumber(updated)?updated->valuedouble:0;group->tasks.push_back(task);recent.push_back(std::move(task));
            }
            if(codex_dashboard_recent_){
                std::stable_sort(recent.begin(),recent.end(),[](const auto& a,const auto& b){return a.updated>b.updated;});if(recent.size()>24)recent.resize(24);groups.clear();
                for(size_t i=0;i<recent.size();i++){if(i%2==0){CodexProjectTile p;p.id="recent-"+std::to_string(i/2);groups.push_back(std::move(p));}groups.back().tasks.push_back(std::move(recent[i]));}
            }
            for(auto& p:groups)p.page=std::max(0,std::min(p.page,int((p.tasks.size()-1)/2)));
            bool same_shape=groups.size()==codex_projects_.size();
            for(size_t i=0;same_shape&&i<groups.size();i++){
                const auto& a=groups[i];const auto& b=codex_projects_[i];
                same_shape=a.id==b.id&&a.name==b.name&&a.tasks.size()==b.tasks.size();
                for(size_t j=0;same_shape&&j<a.tasks.size();j++)same_shape=a.tasks[j].id==b.tasks[j].id&&a.tasks[j].title==b.tasks[j].title;
            }
            codex_projects_=std::move(groups);
            if(!codex_projects_initialized_&&!codex_projects_.empty()){
                codex_expanded_project_=codex_projects_[0].id;
                for(auto& p:codex_projects_)for(auto& t:p.tasks)if(t.id==codex_selected_thread_)codex_expanded_project_=p.id;
                codex_projects_initialized_=true;
            }
            codex_tasks_drawn_=revision;
            if(!same_shape)codex_dashboard_dirty_=true;
            else if(!codex_dashboard_dirty_)UpdateCodexProjectStates();
        }
        cJSON_Delete(doc);
    }
    std::string status=connected?(codex_dashboard_recent_?"最近更新的24条对话":"按项目查看"):"电脑连接异常 · 正在重连";
    for(int i=0;i<2;i++){auto b=codex_dashboard_modes_[i];if(b){bool selected=(i==int(codex_dashboard_recent_.load()));lv_obj_set_style_border_color(b,lv_color_hex(selected?0x53d9c1:0x294c65),0);lv_obj_set_style_bg_color(b,lv_color_hex(selected?0x125a52:0x061b2c),0);}}
    if(status!=lv_label_get_text(codex_task_status_))lv_label_set_text(codex_task_status_,status.c_str());
    if(!touching&&codex_dashboard_dirty_)RenderCodexProjects();
}

void PocketApps::ToggleCodexProject(size_t index){
    if(codex_dashboard_recent_||codex_dashboard_swipe_)return;
    if(index>=codex_projects_.size())return;
    auto id=codex_projects_[index].id;codex_expanded_project_=codex_expanded_project_==id?"":id;
    codex_dashboard_dirty_=true;
}

void PocketApps::TurnCodexProjectPage(int delta){
    if(codex_dashboard_recent_||codex_dashboard_swipe_)return;
    for(auto& p:codex_projects_)if(p.id==codex_expanded_project_){
        int next=std::max(0,std::min(p.page+delta,int((p.tasks.size()-1)/2)));
        if(next!=p.page){p.page=next;codex_dashboard_dirty_=true;}return;
    }
}

static void CodexPixelFrame(lv_obj_t* o,uint32_t accent){
    lv_obj_remove_style_all(o);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,lv_color_hex(0x061b2c),0);
    lv_obj_set_style_bg_grad_color(o,lv_color_hex(0x09253a),0);lv_obj_set_style_bg_grad_dir(o,CodexStyleMode()?LV_GRAD_DIR_NONE:LV_GRAD_DIR_VER,0);
    if(CodexStyleMode())lv_obj_set_style_bg_color(o,lv_color_hex(0x082135),0);
    lv_obj_set_style_border_width(o,3,0);lv_obj_set_style_border_color(o,lv_color_hex(accent),0);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x22516a),LV_STATE_PRESSED);
    lv_obj_set_style_opa(o,LV_OPA_80,LV_STATE_DISABLED);
    lv_obj_add_event_cb(o,[](lv_event_t* e){
        if(CodexStyleMode()>=3||CodexCachedAncestor(lv_event_get_target_obj(e)))return;
        auto o=lv_event_get_target_obj(e);lv_area_t b;lv_obj_get_coords(o,&b);auto layer=lv_event_get_layer(e);
        lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(0x080d1b);d.bg_opa=LV_OPA_COVER;
        auto rect=[&](int x,int y,int w,int h,uint32_t c){d.bg_color=lv_color_hex(c);lv_area_t q={x,y,x+w-1,y+h-1};lv_draw_rect(layer,&d,&q);};
        uint32_t accent=lv_color_to_u32(lv_obj_get_style_border_color(o,LV_PART_MAIN))&0xffffff;
        for(int i=0;i<4;i++){
            bool right=i&1,bottom=i&2;int x=right?b.x2-7:b.x1,y=bottom?b.y2-7:b.y1;
            rect(x,y,8,8,0x080d1b);
            rect(right?b.x2-11:b.x1+8,bottom?b.y2-7:b.y1+5,4,3,accent);
            rect(right?b.x2-7:b.x1+5,bottom?b.y2-11:b.y1+8,3,4,accent);
        }
        rect(b.x1+12,b.y1+5,b.x2-b.x1-23,2,0x27647a);
        rect(b.x1+8,b.y2-5,b.x2-b.x1-15,2,0x031321);
        if(CodexStyleMode()<2&&b.y2-b.y1>150)for(int y=b.y1+50;y<b.y2-12;y+=8)for(int x=b.x2-24;x<b.x2-8;x+=8)rect(x,y,2,2,0x123046);
    },LV_EVENT_DRAW_MAIN_END,nullptr);
}

static void CodexFolderSprite(lv_obj_t* parent){
    auto icon=lv_obj_create(parent);lv_obj_remove_style_all(icon);lv_obj_set_pos(icon,14,18);lv_obj_set_size(icon,58,46);lv_obj_remove_flag(icon,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(icon,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(icon,[](lv_event_t* e){
        if(CodexStyleMode()>=4||CodexCachedAncestor(lv_event_get_target_obj(e)))return;
        lv_area_t b;lv_obj_get_coords(lv_event_get_target_obj(e),&b);auto layer=lv_event_get_layer(e);
        auto rect=[&](int x,int y,int w,int h,uint32_t c){lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(c);d.bg_opa=LV_OPA_COVER;lv_area_t r={b.x1+x,b.y1+y,b.x1+x+w-1,b.y1+y+h-1};lv_draw_rect(layer,&d,&r);};
        rect(6,4,22,8,0x031524);rect(2,10,52,34,0x031524);rect(8,6,18,8,0xb1e8f4);rect(6,12,44,28,0x227ca7);rect(8,12,40,4,0x94def0);rect(4,18,50,22,0x1c618d);rect(8,16,44,22,0x368eb4);rect(10,18,40,4,0x5fbad1);rect(8,38,42,4,0x11405e);rect(4,22,4,16,0x70d2e4);rect(50,20,4,18,0x103a59);
    },LV_EVENT_DRAW_MAIN,nullptr);
}

static std::string CodexProjectSummary(const PocketApps::CodexProjectTile& project){
    int running=0,questions=0;for(const auto& t:project.tasks){if(t.status=="active")running++;if(t.questions>0)questions+=t.questions;}
    auto text=std::to_string(project.tasks.size())+" 对话";if(running)text+=" · "+std::to_string(running)+" 执行";if(questions)text+=" · "+std::to_string(questions)+" 待答";return text;
}
#include "codex_project_cache.inc"
void PocketApps::SetCodexProjectDrawMode(int mode,bool persist){
    if(mode<0||mode>7||!codex_dashboard_||!codex_task_list_)return;
    codex_dashboard_scroll_=lv_obj_get_scroll_y(codex_task_list_);codex_project_draw_mode_=mode;
    RenderCodexProjects();
    if(mode==5){
        auto hide=[](auto&& self,lv_obj_t* node)->void{if(lv_obj_check_type(node,&lv_label_class))lv_obj_add_flag(node,LV_OBJ_FLAG_HIDDEN);for(uint32_t i=0;i<lv_obj_get_child_count(node);i++)self(self,lv_obj_get_child(node,i));};hide(hide,codex_task_list_);
    }
    if(persist&&(mode<=2||mode>=6)){Settings settings("pocket_ui",true);settings.SetInt("project_draw",mode);}
}
void PocketApps::UpdateCodexProjectStates(){
    if(codex_project_summaries_.size()!=codex_projects_.size()){codex_dashboard_dirty_=true;return;}
    auto set=[](lv_obj_t* label,const std::string& text){if(!label||text==lv_label_get_text(label))return false;lv_label_set_text(label,text.c_str());return true;};
    std::vector<bool> changed(codex_projects_.size());
    for(size_t i=0;i<codex_projects_.size();i++)changed[i]=set(codex_project_summaries_[i],CodexProjectSummary(codex_projects_[i]));
    for(const auto& w:codex_card_widgets_){
        const auto& t=codex_projects_[w.project].tasks[w.task];auto color=lv_color_hex(t.status=="active"?0x53d9c1:0x90b4c6);
        changed[w.project]=set(w.status,t.label)||changed[w.project];lv_obj_set_style_text_color(w.status,color,0);lv_obj_set_style_bg_color(w.dot,color,0);
        changed[w.project]=changed[w.project]||(lv_obj_has_flag(w.badge,LV_OBJ_FLAG_HIDDEN)!=(t.questions<=0))||(lv_obj_has_flag(w.unknown,LV_OBJ_FLAG_HIDDEN)!=(t.questions>=0));
        if(t.questions>0){changed[w.project]=set(w.badge_label,"待回答 "+std::to_string(t.questions))||changed[w.project];lv_obj_remove_flag(w.badge,LV_OBJ_FLAG_HIDDEN);}else lv_obj_add_flag(w.badge,LV_OBJ_FLAG_HIDDEN);
        if(t.questions<0)lv_obj_remove_flag(w.unknown,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(w.unknown,LV_OBJ_FLAG_HIDDEN);
    }
    if(codex_project_draw_mode_>=6)for(size_t i=0;i<changed.size();i++)if(changed[i])RefreshCodexProjectCache(i);
    ++codex_project_state_updates_;
}
void PocketApps::RenderCodexProjects(){
    auto started=esp_timer_get_time();codex_project_summaries_.clear();codex_card_widgets_.clear();
    lv_obj_clean(codex_task_list_);
    ReleaseCodexProjectCaches();
    auto text=[](lv_obj_t* parent,const std::string& value,int x,int y,int w,int h,uint32_t color){auto l=lv_label_create(parent);lv_obj_set_pos(l,x,y);lv_obj_set_size(l,w,h);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_label_set_text(l,value.c_str());lv_obj_set_style_text_color(l,lv_color_hex(color),0);lv_obj_remove_flag(l,LV_OBJ_FLAG_CLICKABLE);return l;};
    for(size_t pi=0;pi<codex_projects_.size();pi++){
        auto& p=codex_projects_[pi];bool recent=codex_dashboard_recent_,expanded=recent||p.id==codex_expanded_project_;
        auto group=lv_obj_create(codex_task_list_);lv_obj_remove_style_all(group);lv_obj_set_size(group,432,recent?208:expanded?364:84);lv_obj_remove_flag(group,LV_OBJ_FLAG_SCROLLABLE);
        if(!recent){
        auto head=lv_button_create(group);CodexPixelFrame(head,expanded?0x53d9c1:0x285875);lv_obj_set_size(head,432,84);CodexFolderSprite(head);
        auto name=text(head,p.name,80,7,310,38,0xe8f9ff);lv_obj_set_style_text_font(name,&CodexTitleFont,0);
        codex_project_summaries_.push_back(text(head,CodexProjectSummary(p),80,46,310,28,0x8dcad9));CodexArrow(head,396,29,expanded?2:1,0x8dcad9);
        lv_obj_add_event_cb(head,[](lv_event_t* e){PocketApps::GetInstance().ToggleCodexProject(size_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,reinterpret_cast<void*>(uintptr_t(pi)));
        }else codex_project_summaries_.push_back(nullptr);
        if(!expanded)continue;
        auto deck=lv_obj_create(group);lv_obj_remove_style_all(deck);lv_obj_set_pos(deck,0,recent?0:96);lv_obj_set_size(deck,432,208);lv_obj_remove_flag(deck,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(deck,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(deck,[](lv_event_t* e){
            auto& s=PocketApps::GetInstance();if(s.codex_dashboard_recent_)return;auto indev=lv_indev_active();if(!indev)return;lv_point_t point;lv_indev_get_point(indev,&point);
            if(lv_event_get_code(e)==LV_EVENT_PRESSED){s.codex_swipe_origin_=point;s.codex_swipe_consumed_=false;}
            if(lv_event_get_code(e)==LV_EVENT_RELEASED){int dx=point.x-s.codex_swipe_origin_.x,dy=point.y-s.codex_swipe_origin_.y;if(s.codex_swipe_origin_.x>20&&s.codex_swipe_origin_.x<460&&abs(dx)>=50&&abs(dx)>abs(dy)*2){s.codex_swipe_consumed_=true;s.TurnCodexProjectPage(dx<0?1:-1);}}
        },LV_EVENT_ALL,nullptr);
        for(int column=0;column<2;column++){
            size_t ti=p.page*2+column;if(ti>=p.tasks.size())break;auto& t=p.tasks[ti];
            auto card=lv_button_create(deck);CodexPixelFrame(card,t.id==codex_selected_thread_?0x53d9c1:0x285875);lv_obj_set_pos(card,column*224,0);lv_obj_set_size(card,208,208);lv_obj_add_flag(card,LV_OBJ_FLAG_EVENT_BUBBLE);
            auto name=text(card,t.title,18,18,172,recent?64:98,0xe8f9ff);lv_obj_set_style_text_font(name,&CodexTitleFont,0);
            if(recent)text(card,t.project,14,88,180,28,0x728ea3);
            uint32_t color=t.status=="active"?0x53d9c1:0x90b4c6;auto dot_index=lv_obj_get_child_count(card);PocketPixel(card,14,recent?132:124,8,8,color);auto dot=lv_obj_get_child(card,dot_index);auto status=text(card,t.label,30,recent?120:112,164,32,color);
            auto badge=lv_obj_create(card);lv_obj_remove_style_all(badge);lv_obj_set_pos(badge,12,160);lv_obj_set_size(badge,180,34);lv_obj_set_style_bg_color(badge,lv_color_hex(0xffc760),0);lv_obj_set_style_bg_opa(badge,LV_OPA_COVER,0);lv_obj_remove_flag(badge,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(badge,LV_OBJ_FLAG_SCROLLABLE);
            auto badge_label=text(badge,"待回答 "+std::to_string(std::max(0,t.questions)),8,0,166,32,0x14202a);if(t.questions<=0)lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);
            auto unknown=text(card,"待答未同步",12,160,184,32,0x728ea3);if(t.questions>=0)lv_obj_add_flag(unknown,LV_OBJ_FLAG_HIDDEN);
            codex_card_widgets_.push_back({pi,ti,status,dot,badge,badge_label,unknown});
            auto key=(pi<<16)|ti;
            lv_obj_add_event_cb(card,[](lv_event_t* e){auto& s=PocketApps::GetInstance();if(s.codex_swipe_consumed_)return;size_t key=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)),pi=key>>16,ti=key&65535;if(pi>=s.codex_projects_.size()||ti>=s.codex_projects_[pi].tasks.size())return;auto t=s.codex_projects_[pi].tasks[ti];s.SelectCodexChat(t.id,t.title);},LV_EVENT_CLICKED,reinterpret_cast<void*>(key));
        }
        if(recent)continue;
        int pages=(p.tasks.size()+1)/2;
        auto prev=Button(group,"",[](lv_event_t*){PocketApps::GetInstance().TurnCodexProjectPage(-1);});CodexPixelFrame(prev,0x285875);lv_obj_set_pos(prev,24,316);lv_obj_set_size(prev,64,44);lv_obj_center(CodexArrow(prev,0,0,0,p.page==0?0x7a9eaf:0xd9faff));if(p.page==0)lv_obj_add_state(prev,LV_STATE_DISABLED);
        auto label=text(group,std::to_string(p.page+1)+" / "+std::to_string(pages),106,320,220,34,0xc5e7f1);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
        auto next=Button(group,"",[](lv_event_t*){PocketApps::GetInstance().TurnCodexProjectPage(1);});CodexPixelFrame(next,0x285875);lv_obj_set_pos(next,344,316);lv_obj_set_size(next,64,44);lv_obj_center(CodexArrow(next,0,0,1,p.page+1>=pages?0x7a9eaf:0xd9faff));if(p.page+1>=pages)lv_obj_add_state(next,LV_STATE_DISABLED);
    }
    auto bubble=[](auto&& self,lv_obj_t* o)->void{lv_obj_add_flag(o,LV_OBJ_FLAG_EVENT_BUBBLE);if(lv_obj_check_type(o,&lv_button_class))lv_obj_add_flag(o,LV_OBJ_FLAG_PRESS_LOCK);for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)self(self,lv_obj_get_child(o,i));};bubble(bubble,codex_task_list_);
    lv_obj_update_layout(codex_task_list_);CacheCodexProjectGroups();lv_obj_scroll_to_y(codex_task_list_,codex_dashboard_scroll_,LV_ANIM_OFF);codex_restore_scroll_=false;codex_dashboard_dirty_=false;
    ++codex_project_rebuilds_;codex_project_build_us_=uint32_t(esp_timer_get_time()-started);
}
