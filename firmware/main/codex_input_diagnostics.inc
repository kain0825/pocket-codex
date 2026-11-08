        if(action=="CX_INPUT_TEST"&&codex_active_&&!codex_dashboard_&&!CodexTabBusy()){
            std::unique_lock<std::mutex> session(codex_session_mutex_,std::try_to_lock);
            if(!session.owns_lock()){printf("@PK CX_INPUT_TEST ready=0\n");fflush(stdout);return;}
            auto real_path=CodexDraftPath(),real_current=ReadText(real_path),real_backup=ReadText(real_path+".BAK");
            auto draft=codex_draft_,job=codex_job_,voice=codex_voice_original_job_,feedback=codex_feedback_,control=codex_control_feedback_;auto clip=codex_clipboard_;
            int state=codex_state_,pending=codex_pending_;bool connected=codex_connected_,automatic=codex_auto_send_,paused=codex_test_network_pause_;
            codex_test_network_pause_=true;codex_test_storage_=true;codex_pending_=0;codex_auto_send_=false;codex_voice_original_job_.clear();codex_job_="input-fixture";
            bool policy=!PocketInput::HasText("")&&!PocketInput::HasText(" \r\n\t")&&!PocketInput::HasText("　 ​")&&PocketInput::HasText("你好")&&PocketInput::HasText(" A ");
            bool energy=!PocketInput::Audible(0)&&!PocketInput::Audible(1599)&&PocketInput::Audible(1600);
            auto bytes=record_bytes_.load(),signal=record_signal_samples_.load();bool error=record_error_.load(),limit=codex_voice_limit_reached_;
            codex_draft_="保留原草稿";codex_state_=1;record_bytes_=32000;record_signal_samples_=0;record_error_=false;FinishCodexCapture(1);
            bool silence=codex_pending_==0&&codex_state_==4&&codex_draft_=="保留原草稿"&&codex_job_=="input-fixture"&&!codex_auto_send_;
            record_bytes_=bytes;record_signal_samples_=signal;record_error_=error;codex_voice_limit_reached_=limit;
            bool blocked=true;
            for(auto blank:{""," \n\t","　 "}){codex_draft_=blank;codex_state_=4;codex_connected_=true;CodexAction(2);blocked&=monitor_dialog_==nullptr&&codex_pending_==0;}
            codex_draft_="guard fixture";codex_state_=4;codex_connected_=false;CodexAction(2);
            bool offline=codex_state_==4&&codex_pending_==0&&monitor_dialog_==nullptr;
            codex_connected_=true;CodexAction(2);bool direct=codex_state_==5&&codex_pending_==2&&monitor_dialog_==nullptr&&codex_draft_=="guard fixture";
            CodexAction(2);bool duplicate=codex_state_==5&&codex_pending_==2&&monitor_dialog_==nullptr&&codex_draft_=="guard fixture";
            // Network is paused and session-locked; discard the isolated send
            // request before restoring production state. Nothing is dispatched.
            codex_pending_=0;codex_state_=4;
            const std::string previous="上一轮完整草稿\n第二行",current="AB";
            codex_draft_=previous;codex_job_="current-fixture";codex_voice_original_job_="previous-fixture";AcceptCodexTranscript(current);
            std::string stored_id,stored_text,previous_id,previous_text;
            bool storage=ReadCodexDraft(stored_id,stored_text)&&stored_id=="current-fixture"&&stored_text==current&&ReadCodexDraft(previous_id,previous_text,true)&&previous_id=="previous-fixture"&&previous_text==previous;
            codex_voice_original_job_=codex_job_;codex_job_="blank-transcript-fixture";AcceptCodexTranscript(" \n　");bool empty_transcript=codex_draft_==current&&codex_job_=="current-fixture"&&codex_state_==4;
            codex_draft_=current;codex_job_="current-fixture";codex_state_=4;EditCodex();bool cursor=false,paste=false,history=false,empty_saved=false,cleared_blocked=false;
            if(codex_input_){
                cursor=lv_obj_has_state(codex_input_,LV_STATE_FOCUSED)&&lv_obj_get_style_border_width(codex_input_,LV_PART_CURSOR)==2&&lv_obj_get_style_border_side(codex_input_,LV_PART_CURSOR)==LV_BORDER_SIDE_LEFT&&lv_obj_get_style_anim_duration(codex_input_,LV_PART_CURSOR)==500;
                printf("@PK CX_CURSOR focused=%d width=%d side=%d blink=%lu\n",int(lv_obj_has_state(codex_input_,LV_STATE_FOCUSED)),int(lv_obj_get_style_border_width(codex_input_,LV_PART_CURSOR)),int(lv_obj_get_style_border_side(codex_input_,LV_PART_CURSOR)),(unsigned long)lv_obj_get_style_anim_duration(codex_input_,LV_PART_CURSOR));
                PocketNativeTabTap(lv_obj_get_child(codex_editor_,6));history=codex_history_overlay_!=nullptr;
                if(codex_history_overlay_){auto scroll=lv_obj_get_child(codex_history_overlay_,1);history&=previous==lv_label_get_text(lv_obj_get_child(scroll,0));PocketNativeTabTap(lv_obj_get_child(codex_history_overlay_,2));history&=codex_history_overlay_==nullptr&&codex_clipboard_&&previous==codex_clipboard_.get();}
                lv_textarea_set_cursor_pos(codex_input_,1);PocketNativeTabTap(lv_obj_get_child(codex_editor_,8));paste=std::string(lv_textarea_get_text(codex_input_))=="A"+previous+"B"&&lv_textarea_get_cursor_pos(codex_input_)==1+PocketInput::Characters(previous.c_str());
                PocketNativeTabTap(lv_obj_get_child(codex_editor_,7));paste&=codex_clipboard_&&std::string(codex_clipboard_.get())=="A"+previous+"B";
                lv_textarea_set_text(codex_input_," \n　");FinishCodexEdit(true);
                std::string saved_id,saved_text,history_id,history_text;empty_saved=ReadCodexDraft(saved_id,saved_text)&&saved_text.empty()&&ReadCodexDraft(history_id,history_text,true)&&history_text==previous;
                cleared_blocked=codex_draft_.empty()&&codex_pending_==3;codex_pending_=0;codex_state_=4;CodexAction(2);cleared_blocked&=monitor_dialog_==nullptr&&codex_pending_==0;
            }
            if(codex_editor_)FinishCodexEdit(false);
            for(auto name:{"DRAFT.JSON","DRAFT.JSON.BAK","DRAFT.JSON.NEW","PREVIOUS.JSON","PREVIOUS.JSON.BAK","PREVIOUS.JSON.NEW"})unlink((CodexRecordDirectory()+name).c_str());
            codex_test_storage_=false;codex_draft_=draft;codex_job_=job;codex_voice_original_job_=voice;codex_feedback_=feedback;codex_control_feedback_=control;codex_clipboard_=clip;codex_state_=state;codex_pending_=pending;codex_connected_=connected;codex_auto_send_=automatic;codex_test_network_pause_=paused;
            bool preserved=ReadText(real_path)==real_current&&ReadText(real_path+".BAK")==real_backup;RefreshCodex();
            printf("@PK CX_INPUT_TEST ready=1 blank_policy=%d energy_gate=%d silence_keeps_draft=%d empty_blocked=%d direct_send=%d offline_blocked=%d duplicate_blocked=%d transcript_history=%d empty_transcript_keeps_draft=%d cursor=%d previous_full=%d paste_at_cursor=%d empty_saved=%d cleared_blocked=%d original_preserved=%d\n",int(policy),int(energy),int(silence),int(blocked),int(direct),int(offline),int(duplicate),int(storage),int(empty_transcript),int(cursor),int(history),int(paste),int(empty_saved),int(cleared_blocked),int(preserved));fflush(stdout);return;
        }
