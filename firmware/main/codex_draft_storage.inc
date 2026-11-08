// Call while the Codex state lock is held. Two complete files permit recovery
// from interruption between FAT renames; an intentionally empty draft is valid.
bool PocketApps::ReadCodexDraft(std::string& job,std::string& text,bool history){
    auto path=history?CodexRecordDirectory()+"PREVIOUS.JSON":CodexDraftPath();
    for(auto candidate:{path,path+".BAK"}){
        auto raw=ReadText(candidate);auto doc=cJSON_Parse(raw.c_str());auto id=cJSON_GetObjectItem(doc,"id"),t=cJSON_GetObjectItem(doc,"text");
        bool ok=cJSON_IsString(id)&&cJSON_IsString(t)&&strlen(t->valuestring)<=32000;
        if(ok){job=id->valuestring;text=t->valuestring;}cJSON_Delete(doc);if(ok)return true;
    }return false;
}
bool PocketApps::StoreCodexDraft(const std::string& job,const std::string& text,bool history){
    if(!mounted_||recording_)return false;
    mkdir("/sdcard/PKOS/CODEX",0775);if(codex_test_storage_)mkdir("/sdcard/PKOS/TEST",0775);mkdir(CodexRecordDirectory().c_str(),0775);
    auto path=history?CodexRecordDirectory()+"PREVIOUS.JSON":CodexDraftPath();auto temp=path+".NEW",backup=path+".BAK";
    auto doc=cJSON_CreateObject();cJSON_AddStringToObject(doc,"id",job.c_str());cJSON_AddStringToObject(doc,"text",text.c_str());cJSON_AddStringToObject(doc,"threadId",codex_selected_thread_.c_str());
    char* encoded=cJSON_PrintUnformatted(doc);std::string raw=encoded?encoded:"";cJSON_free(encoded);cJSON_Delete(doc);if(raw.empty())return false;
    auto file=fopen(temp.c_str(),"wb");if(!file)return false;
    bool ok=fwrite(raw.data(),1,raw.size(),file)==raw.size();ok=fflush(file)==0&&ok;ok=fsync(fileno(file))==0&&ok;ok=fclose(file)==0&&ok;
    if(!ok||ReadText(temp)!=raw){unlink(temp.c_str());return false;}
    bool moved=false;
    if(access(path.c_str(),F_OK)==0){unlink(backup.c_str());if(rename(path.c_str(),backup.c_str())!=0){unlink(temp.c_str());return false;}moved=true;}
    if(rename(temp.c_str(),path.c_str())!=0){if(moved)rename(backup.c_str(),path.c_str());return false;}
    return ReadText(path)==raw;
}
bool PocketApps::ArchiveCodexDraft(){
    if(!PocketInput::HasText(codex_draft_))return true;
    return StoreCodexDraft(codex_voice_original_job_.empty()?codex_job_:codex_voice_original_job_,codex_draft_,true);
}
void PocketApps::RemoveCodexDraft(){
    // Commit an empty marker before cleanup, so an older backup cannot resurrect.
    StoreCodexDraft(codex_job_,"");
}
