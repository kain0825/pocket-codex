// Tab membership is local UI state; activity only updates metadata.
static constexpr size_t CodexTabLimit=64;
static constexpr const char* CodexTabsFile="/sdcard/PKOS/CODEX/TABS.JSON";
static std::string CodexTabsPath(int64_t generation){return generation&1?"/sdcard/PKOS/CODEX/TABS.BAK":CodexTabsFile;}
static bool CodexTabId(const std::string& id){return id.size()==36&&id.find_first_not_of("0123456789abcdef-")==std::string::npos;}
static std::vector<PocketApps::CodexTaskTile> CodexRetainTabs(const std::vector<PocketApps::CodexTaskTile>& open,const std::vector<PocketApps::CodexTaskTile>& incoming,const std::vector<std::string>& closed,bool connected){
    auto result=open;
    for(auto& old:result){
        auto row=std::find_if(incoming.begin(),incoming.end(),[&](const auto& t){return t.id==old.id;});
        if(row!=incoming.end())old=*row;
        else{old.status="unknown";old.questions=-1;old.attention=false;}
        if(!connected){old.status="unknown";old.questions=-1;old.attention=false;}
    }
    if(connected)for(const auto& t:incoming){
        bool eligible=t.status=="active"||t.questions>0||t.attention;
        if(eligible&&result.size()<CodexTabLimit&&std::find(closed.begin(),closed.end(),t.id)==closed.end()&&std::none_of(result.begin(),result.end(),[&](const auto& x){return x.id==t.id;}))result.push_back(t);
    }
    return result;
}
static std::string CodexTabsEncode(const std::vector<PocketApps::CodexTaskTile>& open,const std::vector<std::string>& closed,int64_t generation=0){
    auto doc=cJSON_CreateObject();cJSON_AddNumberToObject(doc,"version",1);cJSON_AddNumberToObject(doc,"generation",generation);auto list=cJSON_AddArrayToObject(doc,"open");
    for(auto& t:open){auto row=cJSON_CreateObject();cJSON_AddStringToObject(row,"id",t.id.c_str());cJSON_AddStringToObject(row,"title",t.title.c_str());cJSON_AddItemToArray(list,row);}
    auto hidden=cJSON_AddArrayToObject(doc,"closed");for(auto& id:closed)cJSON_AddItemToArray(hidden,cJSON_CreateString(id.c_str()));
    char* data=cJSON_PrintUnformatted(doc);std::string text=data?data:"";cJSON_free(data);cJSON_Delete(doc);return text;
}
static bool CodexTabsDecode(const std::string& raw,std::vector<PocketApps::CodexTaskTile>& open,std::vector<std::string>& closed,int64_t* generation=nullptr){
    auto doc=cJSON_Parse(raw.c_str());auto list=cJSON_GetObjectItem(doc,"open"),hidden=cJSON_GetObjectItem(doc,"closed");
    if(!cJSON_IsArray(list)||!cJSON_IsArray(hidden)||cJSON_GetArraySize(list)>int(CodexTabLimit)||cJSON_GetArraySize(hidden)>256){cJSON_Delete(doc);return false;}
    std::vector<PocketApps::CodexTaskTile> restored;std::vector<std::string> dismissed;
    for(int i=0;i<cJSON_GetArraySize(list);i++){
        auto row=cJSON_GetArrayItem(list,i);auto id=cJSON_GetObjectItem(row,"id"),title=cJSON_GetObjectItem(row,"title");
        if(cJSON_IsString(id)&&CodexTabId(id->valuestring)&&cJSON_IsString(title)&&strlen(title->valuestring)<=1024&&std::none_of(restored.begin(),restored.end(),[&](const auto& t){return t.id==id->valuestring;}))restored.push_back({id->valuestring,title->valuestring,"unknown","",-1,false});
    }
    for(int i=0;i<cJSON_GetArraySize(hidden);i++){auto id=cJSON_GetArrayItem(hidden,i);if(cJSON_IsString(id)&&CodexTabId(id->valuestring)&&std::find(dismissed.begin(),dismissed.end(),id->valuestring)==dismissed.end())dismissed.push_back(id->valuestring);}
    auto epoch=cJSON_GetObjectItem(doc,"generation");if(generation)*generation=cJSON_IsNumber(epoch)&&epoch->valuedouble>=0&&epoch->valuedouble<1e12?int64_t(epoch->valuedouble):0;
    cJSON_Delete(doc);open=std::move(restored);closed=std::move(dismissed);return true;
}

void PocketApps::LoadCodexTabs(){
    if(codex_tabs_loaded_)return;
    codex_tabs_loaded_=true;codex_tabs_generation_=0;int64_t best=-1;
    for(auto path:{CodexTabsFile,"/sdcard/PKOS/CODEX/TABS.BAK"}){
        std::vector<CodexTaskTile> open;std::vector<std::string> closed;int64_t generation=0;
        if(CodexTabsDecode(ReadText(path),open,closed,&generation)&&generation>best){best=generation;codex_tabs_generation_=generation;codex_tabs_=std::move(open);codex_closed_tabs_=std::move(closed);}
    }
    PublishCodexTabs();
}
void PocketApps::PublishCodexTabs(){
    std::string watch="|";for(auto& t:codex_tabs_)watch+=t.id+"|";
    std::lock_guard<std::mutex> g(codex_mutex_);codex_tabs_watch_=std::move(watch);
}
bool PocketApps::SaveCodexTabs(){
    PublishCodexTabs();
    if(!mounted_||recording_)return false;
    // FAT rename does not replace existing files. Alternate complete snapshots so a failed write leaves the previous valid generation.
    mkdir("/sdcard/PKOS/CODEX",0775);auto next=codex_tabs_generation_+1;auto raw=CodexTabsEncode(codex_tabs_,codex_closed_tabs_,next);if(raw.empty())return false;
    auto target=CodexTabsPath(next);FILE* f=fopen(target.c_str(),"wb");bool ok=false;
    if(f){ok=fwrite(raw.data(),1,raw.size(),f)==raw.size();ok=fflush(f)==0&&ok;ok=fsync(fileno(f))==0&&ok;ok=fclose(f)==0&&ok;}
    if(ok)ok=ReadText(target)==raw;
    if(ok)codex_tabs_generation_=next;
    if(!ok){std::lock_guard<std::mutex> g(codex_mutex_);codex_control_feedback_="卡片已更新，存储卡保存失败";ESP_LOGW(TAG,"Tab registry save failed errno=%d",errno);}
    return ok;
}
bool PocketApps::RememberCodexTab(const std::string& id,const std::string& title){
    LoadCodexTabs();if(!CodexTabId(id))return false;
    auto old=std::find_if(codex_tabs_.begin(),codex_tabs_.end(),[&](const auto& t){return t.id==id;});
    if(old==codex_tabs_.end()&&codex_tabs_.size()>=CodexTabLimit)return false;
    bool changed=old==codex_tabs_.end()||old->title!=title||std::find(codex_closed_tabs_.begin(),codex_closed_tabs_.end(),id)!=codex_closed_tabs_.end();
    codex_closed_tabs_.erase(std::remove(codex_closed_tabs_.begin(),codex_closed_tabs_.end(),id),codex_closed_tabs_.end());
    if(old==codex_tabs_.end())codex_tabs_.push_back({id,title,"unknown","",-1,false});else old->title=title;
    if(changed){SaveCodexTabs();codex_tabs_drawn_.clear();}return true;
}
bool PocketApps::CodexTabBusy(){
    std::lock_guard<std::mutex> g(codex_mutex_);
    return recording_||codex_state_==2||codex_state_==3||codex_state_==5||codex_state_==7||codex_stop_waiting_||codex_answer_waiting_||codex_continue_waiting_||codex_editor_||codex_question_overlay_||monitor_dialog_||!codex_select_id_.empty();
}
void PocketApps::RequestCloseCodexTab(size_t index){
    if(index>=codex_tabs_.size()||!codex_close_tab_id_.empty()||CodexTabBusy())return;
    codex_close_tab_id_=codex_tabs_[index].id;
}
bool PocketApps::ClosePendingCodexTab(){
    if(codex_close_tab_id_.empty())return false;
    if(CodexTabBusy()){codex_close_tab_id_.clear();return false;}
    auto row=std::find_if(codex_tabs_.begin(),codex_tabs_.end(),[&](const auto& t){return t.id==codex_close_tab_id_;});
    if(row==codex_tabs_.end()){codex_close_tab_id_.clear();return false;}
    bool current=row->id==codex_selected_thread_;size_t index=row-codex_tabs_.begin();
    std::unique_lock<std::mutex> session(codex_session_mutex_,std::defer_lock);
    if(current&&!session.try_lock()){
        std::lock_guard<std::mutex> g(codex_mutex_);codex_control_feedback_="正在完成同步，随后关闭卡片";
        lv_label_set_text(codex_status_,codex_control_feedback_.c_str());return true;
    }
    codex_closed_tabs_.erase(std::remove(codex_closed_tabs_.begin(),codex_closed_tabs_.end(),row->id),codex_closed_tabs_.end());
    codex_closed_tabs_.push_back(row->id);if(codex_closed_tabs_.size()>256)codex_closed_tabs_.erase(codex_closed_tabs_.begin());
    codex_pages_.erase(std::remove_if(codex_pages_.begin(),codex_pages_.end(),[&](const auto& p){return p.id==row->id;}),codex_pages_.end());
    codex_tabs_.erase(row);codex_close_tab_id_.clear();SaveCodexTabs();codex_tabs_drawn_.clear();
    if(!current)return false;
    if(codex_tabs_.empty()){Close(false,false);OpenCodex(true);}
    else{auto next=codex_tabs_[std::min(index,codex_tabs_.size()-1)];ActivateCodexChat(next.id,next.title);}
    return true;
}
