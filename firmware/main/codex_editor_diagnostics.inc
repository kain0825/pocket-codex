        if(action=="CX_COMPOSE_INFO"&&codex_active_&&!codex_dashboard_){
            lv_obj_update_layout(root_);lv_area_t box,caption,notice,status;lv_obj_get_coords(codex_draft_panel_,&box);lv_obj_get_coords(codex_draft_heading_,&caption);lv_obj_get_coords(codex_question_button_,&notice);lv_obj_get_coords(codex_status_,&status);
            auto text=lv_label_get_text(codex_draft_heading_);bool inside=caption.x1>box.x1&&caption.x2<box.x2&&caption.y1>box.y1&&caption.y2<box.y2;
            printf("@PK CX_COMPOSE_INFO recording=%d seconds=%u caption_inside=%d recording_caption=%d top_recording=%d pending_visible=%d status_gap=%ld header=%s\n",int(recording_.load()),RecordingSeconds(),int(inside),int(strncmp(text,"录音中",9)==0),int(strncmp(lv_label_get_text(codex_status_),"录音中",9)==0),int(!lv_obj_has_flag(codex_question_button_,LV_OBJ_FLAG_HIDDEN)),long(notice.x1-status.x2-1),text);fflush(stdout);return;
        }
        if(action=="CX_NOTICE_TEST"&&codex_active_&&!codex_dashboard_&&!recording_&&!codex_editor_&&!codex_question_overlay_&&!monitor_dialog_){
            int previous=codex_chat_layout_;int scroll=lv_obj_get_scroll_y(codex_reply_panel_);bool stable=true,gap=true,centered=true;int hits=0;
            auto same=[](const lv_area_t& a,const lv_area_t& b){return a.x1==b.x1&&a.x2==b.x2&&a.y1==b.y1&&a.y2==b.y2;};
            for(bool draft:{false,true}){
                LayoutCodexChat(false,draft);lv_obj_update_layout(root_);lv_area_t reply,state,box;lv_obj_get_coords(codex_reply_frame_,&reply);lv_obj_get_coords(codex_state_panel_,&state);lv_obj_get_coords(codex_draft_panel_,&box);
                for(int i=0;i<6;i++){
                    LayoutCodexChat(true,draft);lv_obj_update_layout(root_);lv_area_t a,b,c,n,s,l,g;lv_obj_get_coords(codex_reply_frame_,&a);lv_obj_get_coords(codex_state_panel_,&b);lv_obj_get_coords(codex_draft_panel_,&c);lv_obj_get_coords(codex_question_button_,&n);lv_obj_get_coords(codex_status_,&s);lv_obj_get_coords(lv_obj_get_child(codex_question_button_,0),&l);lv_obj_get_coords(lv_obj_get_child(codex_question_button_,1),&g);
                    stable&=same(reply,a)&&same(state,b)&&same(box,c);gap&=s.x2<n.x1&&n.y2<reply.y1&&n.x2<480&&n.y1>=96;
                    centered&=abs(std::min(l.x1,g.x1)+std::max(l.x2,g.x2)-n.x1-n.x2)<=1&&abs(l.y1+l.y2-n.y1-n.y2)<=1&&abs(g.y1+g.y2-n.y1-n.y2)<=1;
                    if(i==0)for(int k=0;k<5;k++){lv_point_t p={k==4?(n.x1+n.x2)/2:(k&1?n.x2-12:n.x1+12),k==4?(n.y1+n.y2)/2:(k&2?n.y2-12:n.y1+12)};hits+=lv_indev_search_obj(root_,&p)==codex_question_button_;}
                    LayoutCodexChat(false,draft);lv_obj_update_layout(root_);lv_obj_get_coords(codex_reply_frame_,&a);stable&=same(reply,a);
                }
            }
            LayoutCodexChat(previous&1,previous&2);lv_obj_scroll_to_y(codex_reply_panel_,scroll,LV_ANIM_OFF);
            bool warm=(lv_color_to_u32(lv_obj_get_style_bg_color(codex_draft_panel_,LV_PART_MAIN))&0xffffff)==0x211b16;
            printf("@PK CX_NOTICE_TEST ready=1 body_stable=%d status_gap=%d centered=%d hits=%d warm_compose=%d restored=%d\n",int(stable),int(gap),int(centered),hits,int(warm),int(codex_chat_layout_==previous));fflush(stdout);return;
        }
        if(action=="CX_EDITOR_BENCH"&&codex_active_&&!codex_dashboard_&&!recording_&&!codex_editor_&&!monitor_dialog_){
            bool paused=codex_test_network_pause_;codex_test_network_pause_=true;
            std::unique_lock<std::mutex> session(codex_session_mutex_,std::try_to_lock);
            if(!session.owns_lock()){codex_test_network_pause_=paused;printf("@PK CX_EDITOR_BENCH ready=0\n");fflush(stdout);return;}
            auto draft=codex_draft_,feedback=codex_feedback_;int state=codex_state_;bool waiting=codex_continue_waiting_;
            unsigned chars=std::clamp(unsigned(index?index:1903),1u,8000u);std::string unit="长文编辑测试，保留全文。\n",fixture;auto count=PocketInput::Characters(unit.c_str());
            for(unsigned i=0;i<chars/count;i++)fixture+=unit;
            fixture.append(unit,0,CodexEditorByteOffset(unit.c_str(),chars%count));
            auto disp=display->GetLvDisplay();lv_refr_now(disp);
            struct Paint {int64_t began=0,first=0,last=0,wait_start=0,wait=0;unsigned flushes=0;} paint;
            auto event=[](lv_event_t* e){auto& p=*static_cast<Paint*>(lv_event_get_user_data(e));auto now=esp_timer_get_time();
                if(lv_event_get_code(e)==LV_EVENT_FLUSH_START){if(!p.first)p.first=now;p.last=now;++p.flushes;}
                if(lv_event_get_code(e)==LV_EVENT_FLUSH_WAIT_START)p.wait_start=now;
                if(lv_event_get_code(e)==LV_EVENT_FLUSH_WAIT_FINISH&&p.wait_start){p.wait+=now-p.wait_start;p.wait_start=0;}
            };
            codex_draft_=fixture;codex_state_=4;codex_continue_waiting_=false;
            auto began=esp_timer_get_time();EditCodex();long build_us=long(esp_timer_get_time()-began);
            bool loaded=codex_input_&&fixture==lv_textarea_get_text(codex_input_);
            lv_display_add_event_cb(disp,event,LV_EVENT_ALL,&paint);paint.began=esp_timer_get_time();lv_refr_now(disp);long render_us=long(esp_timer_get_time()-paint.began);
            lv_display_remove_event_cb_with_user_data(disp,event,&paint);
            if(codex_editor_)FinishCodexEdit(false);
            codex_draft_=draft;codex_state_=state;codex_continue_waiting_=waiting;codex_feedback_=feedback;codex_test_network_pause_=paused;RefreshCodex();
            printf("@PK CX_EDITOR_BENCH ready=1 chars=%u loaded=%d preserved=%d build_us=%ld render_us=%ld total_us=%ld flushes=%u first_flush_us=%ld last_flush_us=%ld wait_us=%ld\n",chars,int(loaded),int(codex_draft_==draft&&!codex_editing_),build_us,render_us,build_us+render_us,paint.flushes,long(paint.first-paint.began),long(paint.last-paint.began),long(paint.wait));fflush(stdout);return;
        }
        if(action=="CX_MODE_TEST"&&codex_active_&&!codex_dashboard_&&!recording_&&!codex_editor_&&!monitor_dialog_){
            bool paused=codex_test_network_pause_;codex_test_network_pause_=true;
            std::unique_lock<std::mutex> session(codex_session_mutex_,std::try_to_lock);
            if(!session.owns_lock()){codex_test_network_pause_=paused;printf("@PK CX_MODE_TEST ready=0\n");fflush(stdout);return;}
            auto quick=codex_quick_mode_;auto state=codex_state_;auto feedback=codex_feedback_,control=codex_control_feedback_;
            bool waiting=codex_continue_waiting_;codex_state_=0;codex_continue_waiting_=false;
            auto selected=[&](bool quick){return lv_obj_has_state(codex_mode_buttons_[0],LV_STATE_CHECKED)==!quick&&lv_obj_has_state(codex_mode_buttons_[1],LV_STATE_CHECKED)==quick;};
            PocketNativeTabTap(codex_mode_buttons_[0]);bool review=!codex_quick_mode_&&selected(false);
            PocketNativeTabTap(codex_mode_buttons_[1]);bool fast=codex_quick_mode_&&selected(true);
            PocketNativeTabTap(codex_mode_buttons_[1]);bool stable=codex_quick_mode_&&selected(true);
            codex_state_=3;SetCodexMode(false);RefreshCodex();bool busy=codex_quick_mode_&&selected(true)&&lv_obj_has_state(codex_mode_buttons_[0],LV_STATE_DISABLED);
            codex_state_=0;RefreshCodex();PocketNativeTabTap(codex_mode_buttons_[0]);bool back=!codex_quick_mode_&&selected(false);
            lv_obj_update_layout(root_);lv_area_t voice,a,b,header,cont;lv_obj_get_coords(codex_buttons_[0],&voice);lv_obj_get_coords(codex_mode_buttons_[0],&a);lv_obj_get_coords(codex_mode_buttons_[1],&b);lv_obj_get_coords(codex_draft_panel_,&header);lv_obj_get_coords(codex_continue_button_,&cont);
            auto centered=[](lv_obj_t* o){lv_area_t a,b;lv_obj_get_coords(o,&a);lv_obj_get_coords(lv_obj_get_child(o,0),&b);return abs(a.x1+a.x2-b.x1-b.x2)<=1&&abs(a.y1+a.y2-b.y1-b.y2)<=1;};
            bool geometry=voice.x2<a.x1&&a.x2<b.x1&&voice.y1==a.y1&&a.y1==b.y1&&voice.y2==b.y2&&centered(codex_mode_buttons_[0])&&centered(codex_mode_buttons_[1]);
            bool parent=lv_obj_get_parent(codex_continue_button_)==codex_draft_panel_&&cont.x1>header.x1&&cont.x2<header.x2&&cont.y1>header.y1&&cont.y2<header.y1+48;
            // Exercise the actual capture transition with networking paused, not a duplicate formula.
            auto saved_job=codex_job_,saved_file=codex_file_,saved_voice=codex_voice_original_job_;int pending=codex_pending_;bool automatic=codex_auto_send_,limit=codex_voice_limit_reached_;
            auto bytes=record_bytes_.load(),signal=record_signal_samples_.load();bool error=record_error_.load();
            record_bytes_=32000;record_signal_samples_=1600;record_error_=false;
            codex_quick_mode_=false;FinishCodexCapture(1);bool review_capture=codex_state_==2&&codex_pending_==1&&!codex_auto_send_;
            codex_quick_mode_=true;FinishCodexCapture(1);bool quick_capture=codex_state_==2&&codex_pending_==1&&codex_auto_send_;
            FinishCodexCapture(3);bool limit_capture=codex_state_==2&&codex_pending_==1&&!codex_auto_send_;
            record_bytes_=bytes;record_signal_samples_=signal;record_error_=error;codex_job_=saved_job;codex_file_=saved_file;codex_voice_original_job_=saved_voice;codex_pending_=pending;codex_auto_send_=automatic;codex_voice_limit_reached_=limit;
            codex_quick_mode_=quick;codex_state_=state;codex_continue_waiting_=waiting;codex_feedback_=feedback;codex_control_feedback_=control;codex_test_network_pause_=paused;RefreshCodex();
            printf("@PK CX_MODE_TEST ready=1 review_selected=%d quick_selected=%d same_stays_selected=%d busy_locked=%d return_review=%d same_row_centered=%d continue_in_draft=%d review_capture=%d quick_capture=%d limit_keeps_review=%d\n",int(review),int(fast),int(stable),int(busy),int(back),int(geometry),int(parent),int(review_capture),int(quick_capture),int(limit_capture));fflush(stdout);return;
        }
        if(action=="CX_LONG_EDIT_TEST"&&codex_active_&&!codex_dashboard_&&!recording_&&!codex_editor_&&!monitor_dialog_){
            bool paused=codex_test_network_pause_;codex_test_network_pause_=true;
            std::unique_lock<std::mutex> session(codex_session_mutex_,std::try_to_lock);
            if(!session.owns_lock()){codex_test_network_pause_=paused;printf("@PK CX_LONG_EDIT_TEST ready=0\n");fflush(stdout);return;}
            auto draft=codex_draft_,job=codex_job_,feedback=codex_feedback_;auto state=codex_state_;auto clip=codex_clipboard_;bool waiting=codex_continue_waiting_;
            unsigned chars=std::clamp(unsigned(index?index:1903),1u,8000u);std::string unit="长文编辑测试，保留全文。\n",fixture;auto count=PocketInput::Characters(unit.c_str());
            for(unsigned i=0;i<chars/count;i++){fixture+=unit;}
            fixture.append(unit,0,CodexEditorByteOffset(unit.c_str(),chars%count));
            codex_draft_=fixture;codex_state_=4;codex_continue_waiting_=false;auto start=esp_timer_get_time();EditCodex();long build_us=long(esp_timer_get_time()-start);
            bool loaded=codex_input_&&fixture==lv_textarea_get_text(codex_input_)&&lv_textarea_get_max_length(codex_input_)==kCodexDraftMaxChars;
            bool editing_gate=codex_editing_.load();auto replies=codex_rendered_messages_;codex_rendered_messages_="editor-diagnostic-sentinel";RefreshCodex();bool frozen=codex_rendered_messages_=="editor-diagnostic-sentinel"&&codex_editor_;codex_rendered_messages_=replies;
            bool paste=false,capacity=false;long paste_us=0;
            if(loaded){
                std::string addition=chars<=6000?std::string("粘贴内容。\n"):std::string("多");
                if(chars<=6000){std::string repeat;for(int i=0;i<150;i++)repeat+=addition;addition=std::move(repeat);}
                auto p=static_cast<char*>(heap_caps_malloc(addition.size()+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
                if(p){memcpy(p,addition.c_str(),addition.size()+1);codex_clipboard_=std::shared_ptr<char>(p,[](char* p){heap_caps_free(p);});lv_textarea_set_cursor_pos(codex_input_,chars/2);size_t offset=CodexEditorByteOffset(fixture.c_str(),chars/2);auto expected=fixture;expected.insert(offset,addition);
                    auto time=esp_timer_get_time();CodexEditorPaste();paste_us=long(esp_timer_get_time()-time);
                    if(PocketInput::Characters(expected.c_str())<=kCodexDraftMaxChars)paste=expected==lv_textarea_get_text(codex_input_)&&lv_textarea_get_cursor_pos(codex_input_)==chars/2+PocketInput::Characters(addition.c_str());
                    else capacity=fixture==lv_textarea_get_text(codex_input_);
                }
            }
            if(codex_editor_){FinishCodexEdit(false);}
            codex_draft_=draft;codex_job_=job;codex_state_=state;codex_clipboard_=clip;codex_continue_waiting_=waiting;codex_feedback_=feedback;codex_test_network_pause_=paused;RefreshCodex();
            printf("@PK CX_LONG_EDIT_TEST ready=1 chars=%u bytes=%u loaded=%d editing_gate=%d background_frozen=%d paste=%d capacity_guard=%d cancelled_preserved=%d build_us=%ld paste_us=%ld\n",chars,unsigned(fixture.size()),int(loaded),int(editing_gate),int(frozen),int(paste),int(capacity),int(codex_draft_==draft&&codex_job_==job&&!codex_editing_),build_us,paste_us);fflush(stdout);return;
        }
