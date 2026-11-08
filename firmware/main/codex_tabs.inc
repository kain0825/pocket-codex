// Bounded metadata only. No second conversation view or framebuffer is retained.
static lv_font_t CodexTabTextFont;
static lv_obj_t* CodexTabNav(lv_obj_t* parent,int x,int direction){
    auto b=lv_obj_create(parent);CodexChatFrame(b,0x217b9d);lv_obj_set_pos(b,x,40);lv_obj_set_size(b,36,56);
    lv_obj_set_style_text_color(b,lv_color_hex(0x82d8d3),0);lv_obj_set_style_text_color(b,lv_color_hex(0x29495b),LV_STATE_DISABLED);
    lv_obj_add_event_cb(b,[](lv_event_t* e){
        auto o=lv_event_get_target_obj(e);lv_area_t a;lv_obj_get_coords(o,&a);int direction=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;d.bg_color=lv_obj_get_style_text_color(o,LV_PART_MAIN);
        int cx=(a.x1+a.x2+1)/2,cy=(a.y1+a.y2+1)/2;
        for(int i=0;i<5;i++){int bend=abs(i-2)*4,x=direction>0?cx+2-bend:cx-6+bend,y=cy-10+i*4;lv_area_t r={x,y,x+3,y+3};lv_draw_rect(lv_event_get_layer(e),&d,&r);}
    },LV_EVENT_DRAW_MAIN_END,reinterpret_cast<void*>(intptr_t(direction)));return b;
}

void PocketApps::TurnCodexTabPage(int delta){
    if(!codex_tabs_panel_||sleeping_||CodexTabBusy()||!codex_close_tab_id_.empty())return;
    int pages=std::max(1,int((codex_tabs_.size()+2)/3)),next=std::clamp(codex_tab_page_+delta,0,pages-1);
    if(next==codex_tab_page_)return;
    codex_tab_page_=next;auto epoch=navigation_epoch_;
    Application::GetInstance().Schedule([this,epoch](){DisplayLockGuard lock(Board::GetInstance().GetDisplay());if(navigation_epoch_==epoch&&codex_tabs_panel_)RefreshCodexTabs();});
}

void PocketApps::CodexTabsNetwork(){
    int64_t now=esp_timer_get_time();if(now-codex_tabs_poll_us_<5000000&&!codex_tabs_json_.empty())return;
    codex_tabs_poll_us_=now;auto raw=CodexHttp("/device/tasks");if(recording_)return;auto doc=cJSON_Parse(raw.c_str());
    auto tabs=cJSON_GetObjectItem(doc,"tasks"),revision=cJSON_GetObjectItem(doc,"revision");
    if(cJSON_IsArray(tabs)&&cJSON_GetArraySize(tabs)<=64&&cJSON_IsString(revision)){
        std::string watch;{std::lock_guard<std::mutex> g(codex_mutex_);watch=codex_tabs_watch_;}
        auto compact=cJSON_CreateObject();cJSON_AddBoolToObject(compact,"connected",cJSON_IsTrue(cJSON_GetObjectItem(doc,"connected")));auto list=cJSON_AddArrayToObject(compact,"tasks");
        for(int i=0;i<cJSON_GetArraySize(tabs);i++){
            auto row=cJSON_GetArrayItem(tabs,i);auto id=cJSON_GetObjectItem(row,"id"),status=cJSON_GetObjectItem(row,"status"),q=cJSON_GetObjectItem(row,"questions");
            if(!cJSON_IsString(id))continue;
            bool needed=watch.find("|"+std::string(id->valuestring)+"|")!=std::string::npos||codex_selected_thread_==id->valuestring||(cJSON_IsString(status)&&strcmp(status->valuestring,"active")==0)||(cJSON_IsNumber(q)&&q->valueint>0)||cJSON_IsTrue(cJSON_GetObjectItem(row,"attention"));
            if(!needed)continue;
            auto item=cJSON_CreateObject();for(auto key:{"id","title","status","label","questions","attention"}){auto v=cJSON_GetObjectItem(row,key);if(v)cJSON_AddItemToObject(item,key,cJSON_Duplicate(v,true));}cJSON_AddItemToArray(list,item);
        }
        char* encoded=cJSON_PrintUnformatted(compact);std::string small=encoded?encoded:"";cJSON_free(encoded);cJSON_Delete(compact);
        if(!small.empty()){auto digest=Hash(small);std::lock_guard<std::mutex> g(codex_mutex_);codex_tabs_json_=std::move(small);codex_tabs_revision_=std::move(digest);}
    }cJSON_Delete(doc);
}

void PocketApps::RefreshCodexTabs(){
    if(!codex_tabs_panel_)return;
    std::string raw,revision;bool busy;
    {std::lock_guard<std::mutex> g(codex_mutex_);raw=codex_tabs_json_;revision=codex_tabs_revision_;
        busy=recording_||codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_stop_waiting_||codex_answer_waiting_||codex_continue_waiting_;}
    int pages=std::max(1,int((codex_tabs_.size()+2)/3));codex_tab_page_=std::clamp(codex_tab_page_,0,pages-1);
    auto enable=[](lv_obj_t* o,bool yes){if(yes)lv_obj_remove_state(o,LV_STATE_DISABLED);else lv_obj_add_state(o,LV_STATE_DISABLED);};
    enable(codex_tab_prev_,!busy&&codex_tab_page_>0);enable(codex_tab_next_,!busy&&codex_tab_page_<pages-1);
    for(size_t i=0;i<std::min(codex_tab_widgets_.size(),codex_tabs_.size());i++){
        if(!codex_tab_widgets_[i])continue;
        bool disabled=busy&&codex_tabs_[i].id!=codex_selected_thread_;
        if(disabled)lv_obj_add_state(codex_tab_widgets_[i],LV_STATE_DISABLED);else lv_obj_remove_state(codex_tab_widgets_[i],LV_STATE_DISABLED);
        auto close=lv_obj_get_child(codex_tab_widgets_[i],2);if(busy)lv_obj_add_state(close,LV_STATE_DISABLED);else lv_obj_remove_state(close,LV_STATE_DISABLED);
    }
    if(recording_)return;
    if(revision==codex_tabs_drawn_&&codex_tab_drawn_page_==codex_tab_page_&&!codex_tab_widgets_.empty())return;
    auto input=lv_indev_get_next(nullptr);if(input&&lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED)return;
    auto started=esp_timer_get_time();std::vector<CodexTaskTile> incoming;
    auto doc=cJSON_Parse(raw.c_str());bool connected=cJSON_IsTrue(cJSON_GetObjectItem(doc,"connected"));auto list=cJSON_GetObjectItem(doc,"tasks");
    if(cJSON_IsArray(list))for(int i=0;i<cJSON_GetArraySize(list)&&i<64;i++){
        auto row=cJSON_GetArrayItem(list,i);auto str=[&](const char* k){auto v=cJSON_GetObjectItem(row,k);return cJSON_IsString(v)?std::string(v->valuestring):std::string();};
        auto id=str("id");if(id.size()!=36||id.find_first_not_of("0123456789abcdef-")!=std::string::npos)continue;
        auto q=cJSON_GetObjectItem(row,"questions");incoming.push_back({id,str("title"),str("status"),str("label"),cJSON_IsNumber(q)?q->valueint:-1,bool(cJSON_IsTrue(cJSON_GetObjectItem(row,"attention")))});
    }cJSON_Delete(doc);
    auto ordered=codex_tab_fixture_?codex_tabs_:CodexRetainTabs(codex_tabs_,incoming,codex_closed_tabs_,connected);
    bool save=ordered.size()!=codex_tabs_.size();
    for(size_t i=0;!save&&i<ordered.size();i++)save=ordered[i].title!=codex_tabs_[i].title;
    pages=std::max(1,int((ordered.size()+2)/3));codex_tab_page_=std::clamp(codex_tab_page_,0,pages-1);
    bool same=ordered.size()==codex_tabs_.size()&&codex_tab_widgets_.size()==ordered.size()&&codex_tab_drawn_page_==codex_tab_page_;
    for(size_t i=0;same&&i<ordered.size();i++)same=ordered[i].id==codex_tabs_[i].id;
    codex_tabs_=std::move(ordered);
    if(save&&!codex_tab_fixture_)SaveCodexTabs();
    if(!same){
        lv_obj_clean(codex_tabs_panel_);codex_tab_widgets_.assign(codex_tabs_.size(),nullptr);int slots=std::min(3,int(codex_tabs_.size()));int width=slots?(348-(slots-1)*6)/slots:348;
        bool selected_here=false;for(size_t i=codex_tab_page_*3;i<std::min(codex_tabs_.size(),size_t(codex_tab_page_*3+3));i++)selected_here|=codex_tabs_[i].id==codex_selected_thread_;
        int x=0,base=width;
        for(size_t i=codex_tab_page_*3;i<std::min(codex_tabs_.size(),size_t(codex_tab_page_*3+3));i++){
            bool selected=codex_tabs_[i].id==codex_selected_thread_;int boost=slots==3?8:slots==2?10:0;
            width=base+(selected?boost:selected_here&&slots>1?-boost/(slots-1):0);int height=selected?56:50,state_y=selected?28:26,close_h=selected?24:20;
            auto card=lv_obj_create(codex_tabs_panel_);CodexChatFrame(card,0x217b9d);lv_obj_set_pos(card,x,selected?0:3);lv_obj_set_size(card,width,height);x+=width+6;
            lv_obj_add_flag(card,LV_OBJ_FLAG_PRESS_LOCK);
            auto title=CodexChatText(card,"",8,selected?2:0,width-16,0xe8f9ff);lv_obj_set_height(title,26);lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);lv_obj_set_style_text_font(title,&CodexTabTextFont,0);lv_obj_set_style_text_line_space(title,0,0);
            auto state=CodexChatText(card,"",8,state_y,width-44,0x82d8d3);lv_obj_set_height(state,selected?25:23);lv_label_set_long_mode(state,LV_LABEL_LONG_DOT);lv_obj_set_style_text_font(state,&CodexTabTextFont,0);lv_obj_set_style_text_line_space(state,0,0);
            auto close=lv_obj_create(card);lv_obj_remove_style_all(close);lv_obj_remove_flag(close,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_pos(close,width-32,state_y);lv_obj_set_size(close,28,close_h);
            lv_obj_set_style_bg_color(close,lv_color_hex(0x17445b),LV_STATE_PRESSED);lv_obj_set_style_bg_opa(close,LV_OPA_COVER,LV_STATE_PRESSED);lv_obj_set_style_opa(close,LV_OPA_40,LV_STATE_DISABLED);
            CodexChatIcon(close,8,(close_h-12)/2,9,1,0x72b5c7);
            lv_obj_add_event_cb(card,[](lv_event_t* e){
                auto o=lv_event_get_target_obj(e);if(!lv_obj_has_flag(o,LV_OBJ_FLAG_USER_1))return;
                lv_area_t a;lv_obj_get_coords(o,&a);int x=a.x1+10,y=a.y1+(lv_obj_get_height(o)==56?32:30);
                lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;d.bg_color=lv_color_hex(0x2ce9c7);
                // Pixel hourglass: an explicit running icon without a permanent animation timer.
                for(auto r:{lv_area_t{x,y,x+15,y+2},lv_area_t{x,y+15,x+15,y+17},lv_area_t{x+3,y+3,x+5,y+5},lv_area_t{x+10,y+3,x+12,y+5},lv_area_t{x+6,y+6,x+9,y+11},lv_area_t{x+3,y+12,x+12,y+14}})lv_draw_rect(lv_event_get_layer(e),&d,&r);
            },LV_EVENT_DRAW_MAIN_END,nullptr);
            lv_obj_add_event_cb(close,[](lv_event_t* e){PocketApps::GetInstance().CodexTabTouch(e);},LV_EVENT_ALL,reinterpret_cast<void*>(i|0x10000));
            lv_obj_add_event_cb(card,[](lv_event_t* e){PocketApps::GetInstance().CodexTabTouch(e);},LV_EVENT_ALL,reinterpret_cast<void*>(i));
            codex_tab_widgets_[i]=card;
        }
        ++codex_tab_rebuilds_;
    }else ++codex_tab_updates_;
    for(size_t i=0;i<codex_tabs_.size();i++){
        if(!codex_tab_widgets_[i])continue;
        const auto& t=codex_tabs_[i];auto card=codex_tab_widgets_[i];bool selected=t.id==codex_selected_thread_;
        uint32_t color=selected?0x2ce9c7:(t.questions>0||t.attention)?0xffcd48:0x217b9d;
        lv_obj_set_style_border_color(card,lv_color_hex(color),0);
        auto title=lv_obj_get_child(card,0),label=lv_obj_get_child(card,1);
        if(t.title!=lv_label_get_text(title))lv_label_set_text(title,t.title.c_str());
        bool running=t.status=="active"&&t.questions<=0&&!t.attention;
        if(running!=lv_obj_has_flag(card,LV_OBJ_FLAG_USER_1)){if(running)lv_obj_add_flag(card,LV_OBJ_FLAG_USER_1);else lv_obj_remove_flag(card,LV_OBJ_FLAG_USER_1);lv_obj_invalidate(card);}
        std::string state=t.questions>0?"待答 "+std::to_string(t.questions):t.attention?"待处理":t.status=="active"?"":t.status=="idle"?"空闲":t.status=="notLoaded"?"未运行":"未知";
        if(state!=lv_label_get_text(label))lv_label_set_text(label,state.c_str());
        lv_obj_set_style_text_color(label,lv_color_hex(color==0xffcd48?color:0x82d8d3),0);
        if(busy&&!selected)lv_obj_add_state(card,LV_STATE_DISABLED);else lv_obj_remove_state(card,LV_STATE_DISABLED);
        auto close=lv_obj_get_child(card,2);if(busy)lv_obj_add_state(close,LV_STATE_DISABLED);else lv_obj_remove_state(close,LV_STATE_DISABLED);
    }
    enable(codex_tab_prev_,!busy&&codex_tab_page_>0);enable(codex_tab_next_,!busy&&codex_tab_page_<pages-1);
    std::string label="PAGE "+std::to_string(codex_tab_page_+1)+" / "+std::to_string(pages);if(label!=lv_label_get_text(codex_tab_page_label_))lv_label_set_text(codex_tab_page_label_,label.c_str());
    if(pages<=1)lv_obj_add_flag(codex_tab_page_label_,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(codex_tab_page_label_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(codex_tabs_panel_);codex_tab_drawn_page_=codex_tab_page_;
    codex_tabs_drawn_=revision;codex_tab_build_us_=esp_timer_get_time()-started;
}

void PocketApps::CodexTabTouch(lv_event_t* e){
    auto code=lv_event_get_code(e);auto input=lv_event_get_indev(e);
    if(code==LV_EVENT_PRESSED){codex_tab_touch_=input!=nullptr;codex_tab_moved_=false;if(input)lv_indev_get_point(input,&codex_tab_press_point_);}
    else if(code==LV_EVENT_PRESSING&&input&&codex_tab_touch_){lv_point_t p;lv_indev_get_point(input,&p);if(abs(p.x-codex_tab_press_point_.x)>10||abs(p.y-codex_tab_press_point_.y)>10)codex_tab_moved_=true;}
    else if(code==LV_EVENT_SCROLL_BEGIN||code==LV_EVENT_LONG_PRESSED||code==LV_EVENT_PRESS_LOST)codex_tab_moved_=true;
    else if(code==LV_EVENT_RELEASED&&input&&codex_tab_touch_&&codex_tab_moved_){lv_point_t p;lv_indev_get_point(input,&p);int dx=p.x-codex_tab_press_point_.x,dy=p.y-codex_tab_press_point_.y;if(abs(dx)>=45&&abs(dx)>abs(dy)*2){codex_tab_touch_=false;TurnCodexTabPage(dx<0?1:-1);}}
    else if(code==LV_EVENT_SHORT_CLICKED){bool tap=codex_tab_touch_&&!codex_tab_moved_;codex_tab_touch_=false;size_t key=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)),i=key&0xffff;if(tap&&i<codex_tabs_.size()&&codex_close_tab_id_.empty()){if(key&0x10000)RequestCloseCodexTab(i);else{auto t=codex_tabs_[i];SelectCodexChat(t.id,t.title);}}}
}

void PocketApps::ContinueCodex(){
    if(!codex_active_||codex_dashboard_||sleeping_||monitor_dialog_||codex_editor_||codex_question_overlay_)return;
    if(!codex_close_tab_id_.empty())return;
    std::lock_guard<std::mutex> g(codex_mutex_);
    if(!codex_connected_||recording_||codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_stop_waiting_||codex_answer_waiting_||codex_continue_waiting_)return;
    char id[33];snprintf(id,sizeof(id),"%08x%08x%08x%08x",unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()));
    codex_continue_id_=id;codex_continue_thread_=codex_selected_thread_;codex_continue_pending_=true;codex_continue_waiting_=true;
    codex_control_feedback_="正在发送继续…";
}
