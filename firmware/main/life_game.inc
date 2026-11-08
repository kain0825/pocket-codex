// Pocket edition: local LVGL interface and crash-safe small-state persistence.
#include "esp_random.h"
#include "life_engine.h"
#include "nvs.h"
static LifeState glLife;
static bool glLoaded=false;
static uint32_t glSeq=0;
static std::string glError;
struct LifeSave {uint32_t magic,seq,checksum;LifeState state;};
static uint32_t LifeChecksum(const LifeState& s){uint32_t h=2166136261u;auto p=(const unsigned char*)&s;for(size_t i=0;i<sizeof(s);i++)h=(h^p[i])*16777619u;return h;}
static bool LifeSaveNow(){
 nvs_handle_t h=0;auto err=nvs_open("pocket_life",NVS_READWRITE,&h);
 if(err!=ESP_OK){glError="存档空间无法打开，请返回后重试。";return false;}
 LifeSave b{};b.magic=0x4c494645;b.seq=glSeq+1;b.state=glLife;b.checksum=LifeChecksum(b.state);
 err=nvs_set_blob(h,b.seq%2?"slot1":"slot0",&b,sizeof(b));
 if(err==ESP_OK)err=nvs_commit(h);
 nvs_close(h);
 if(err!=ESP_OK){glError="存档失败，本次操作未提交，请重试。";return false;}glSeq=b.seq;glError.clear();
 return true;
}
static void LifeLoad(){if(glLoaded)return;glLoaded=true;nvs_handle_t h=0;
 if(nvs_open("pocket_life",NVS_READONLY,&h)!=ESP_OK)return;
 for(const char* key:{"slot0","slot1"}){LifeSave b{};size_t size=sizeof(b);
 if(nvs_get_blob(h,key,&b,&size)==ESP_OK&&size==sizeof(b)&&b.magic==0x4c494645&&b.checksum==LifeChecksum(b.state)&&LifeValid(b.state)&&b.seq>glSeq){glLife=b.state;glSeq=b.seq;}}
 nvs_close(h);glLife.armed=false;
}
static std::string LifeItemText(unsigned bits){std::string s;for(int i=0;i<7;i++)if(bits&(1u<<i)){if(!s.empty())s+=" / ";s+=LIFE_ITEMS[i];}return s.empty()?"暂无物品":s;}
static std::string LifeEffect(const LifeChoice& c){
 std::string s;auto add=[&](const char* key,int n){if(n){if(!s.empty())s+="  ";s+=key;s+=(n>0?"+":"");s+=std::to_string(n);}};
 add("精力",c.energy);add("洞察",c.sense);add("牵挂",c.ties);add("零钱",c.coins);
 if(c.item)s+="\n获得："+LifeItemText(c.item);
 if(c.need)s+="\n需要："+LifeItemText(c.need);
 if(c.minSense)s+="  洞察≥"+std::to_string(c.minSense);
 if(c.minTies)s+="  牵挂≥"+std::to_string(c.minTies);
 return s;
}
void PocketApps::OpenLife(){
 if(reader_active_||recording_){RestoreOpenApp();return;}
 auto& app=Application::GetInstance();app.ReturnToHome();app.Schedule([this](){DisplayLockGuard lock(Board::GetInstance().GetDisplay());
 if(RestoreOpenApp())return;
 LifeLoad();glLife.armed=false;life_active_=true;life_view_=0;RenderLife();
 if(!life_timer_)life_timer_=lv_timer_create([](lv_timer_t*){PocketApps::GetInstance().LifeTick();},1000,nullptr);
 });
}
void PocketApps::LifeTick(){
 if(!life_active_||glLife.phase!=3||!glLife.armed||!glLife.timed||!glError.empty())return;
 if(glLife.remaining>0)--glLife.remaining;
 if(life_clock_)lv_label_set_text(life_clock_,("剩余 "+std::to_string(glLife.remaining)+" 秒 · 回主菜单暂停").c_str());
 if(glLife.remaining==0){LifeAction(13);return;}
 if(glLife.remaining%10==0&&!LifeSaveNow()){glLife.armed=false;RenderLife();}
}
void PocketApps::LifeAction(int action){
 auto before=glLife;bool changed=false;int oldView=life_view_;bool exportJournal=false;
 if(action==100){life_view_=0;glLife.armed=false;}
 else if(action==101){life_view_=1;glLife.armed=false;}
 else if(action==102){life_view_=2;glLife.armed=false;}
 else if(action==103){life_view_=3;glLife.armed=false;}
 else if(action==104){life_view_=4;glLife.armed=false;}
 else if(action==105){life_view_=5;glLife.armed=false;}
 else if(action==110||action==111){LifeNew(glLife,esp_random(),action==111);changed=true;life_view_=1;}
 else if(action>=0&&action<=2)changed=LifeTrait(glLife,action);
 else if(action==3||action==4)changed=LifeRoute(glLife,action-3);
 else if(action==5&&glLife.phase==3){glLife.armed=true;changed=true;}
 else if(action>=10&&action<=13)changed=LifeChoose(glLife,action-10);
 else if(action==20)changed=LifeNext(glLife);
 else if(action>=30&&action<=32)changed=LifeReward(glLife,action-30);
 else if(action==40)changed=LifeRest(glLife);
 else if(action>=50&&action<=53){changed=LifeEnd(glLife,action-50);exportJournal=changed;}
 if(changed&&!LifeSaveNow()){glLife=before;glLife.armed=false;life_view_=oldView;exportJournal=false;}
 if(exportJournal&&mounted_&&!reader_active_){
   std::lock_guard<std::mutex> lock(storage_mutex_);mkdir("/sdcard/PKOS/GAME",0775);
   std::string path="/sdcard/PKOS/GAME/RUN"+std::to_string(glLife.runs)+".MD";FILE* f=fopen(path.c_str(),"w");
   if(f){fprintf(f,"# 空置时刻 · 第 %lu 局\n\n%s\n\n结局：%s\n\n%s\n",(unsigned long)glLife.runs,ClockText().c_str(),LIFE_ENDINGS[glLife.ending],LIFE_END_TEXT[glLife.ending]);
    for(int i=0;i<glLife.step;i++){auto& e=LIFE_EVENTS[glLife.history[i]];int c=glLife.decisions[i];fprintf(f,"\n## %d. %s\n\n%s\n\n%s\n",i+1,e.title,c==3?"做梦 / 未作选择":e.choices[c].label,c==3?e.dream:e.choices[c].result);}int flushed=fflush(f);int closed=fclose(f);
 if(flushed||closed)glError="进度已保存，但存储卡日志写入失败。";
   }else glError="进度已保存，但存储卡日志无法创建。";
 }
 RenderLife();
}
void PocketApps::RenderLife(){
 life_clock_=nullptr;MakeRoot("人生模拟器");
 auto button=[this](const std::string& title,int action,bool enabled=true){auto b=Button(body_,title,[](lv_event_t* e){PocketApps::GetInstance().LifeAction((intptr_t)lv_event_get_user_data(e));},(void*)(intptr_t)action);lv_obj_set_flex_flow(b,LV_FLEX_FLOW_COLUMN);lv_obj_set_height(b,LV_SIZE_CONTENT);lv_obj_set_style_min_height(b,64,0);lv_obj_set_style_pad_ver(b,16,0);
 if(!enabled)lv_obj_add_state(b,LV_STATE_DISABLED);
 return b;};
 if(!glError.empty())Label(body_,glError,0xFFB467);
 Label(body_,"空置时刻 / 夜城漫游",0x32E6CF);
 if(life_view_==0){
  // Original pixel scene built from shapes, no external asset dependency.
  auto scene=lv_obj_create(body_);lv_obj_set_size(scene,LV_PCT(100),120);lv_obj_set_style_pad_all(scene,0,0);PocketPanel(scene,0x607A98);lv_obj_remove_flag(scene,LV_OBJ_FLAG_SCROLLABLE);
  for(int i=0;i<8;i++){int h=25+(i*31)%65;PocketPixel(scene,i*52,116-h,42,h,0x26354D);for(int j=0;j<3;j++)PocketPixel(scene,i*52+6+j*10,120-h,4,6,i%2?0xF878C1:0x32E6CF);}PocketPixel(scene,338,12,20,20,0xFFE4A0);
  Label(body_,"一座藏在日常缝隙里的城市。每局走过八个时刻，带着不同的东西回家。");
  Label(body_,"已完成 "+std::to_string(glLife.runs)+" 局 · 结局 "+std::to_string(LifeCount(glLife.endings))+" / 6 · 事件 "+std::to_string(LifeCount(glLife.catalog))+" / 20",0x9AAFC5);
  if(glLife.phase>0&&glLife.phase<7)button("继续这一局 · "+std::to_string(glLife.step)+" / 8",101);
  else if(glLife.phase==7)button("查看本局结局",101);
  button("新的一局",104);button("行囊与本局记录",102);button("结局图鉴",103);button("玩法说明",105);return;
 }
 button("返回游戏首页",100);
 if(life_view_==4){Label(body_,"新局会替换未完成的进度；已收集的事件、结局和已完成日志保留。",0xFFB467);button("从容漫游 · 不倒计时",110);button("限时漫游 · 每次 45 秒",111);return;}
 if(life_view_==5){
 Label(body_,"每局：选特质 → 二选一路线 → 事件抉择 → 中途补给 → 八站后选择归途。\n\n精力用于行动；洞察解锁线索；牵挂解锁同行；零钱用于购买和休息。数值上限均为 9。\n\n物品不会被使用消耗，同物品重复获得变成一枚零钱。选项会明确显示收益和条件。灰色选项代表条件不足。\n\n限时模式先阅读，再点准备按钮开始倒计时。超时记为做梦，不扣资源；从容模式可主动做梦。离开游戏暂停计时。\n\n存档自动保存在设备内。完成一局后，完整日志另存到存储卡 PKOS/GAME，可用读卡器复制到 Obsidian。\n\n每局路线随机且不重复事件。完成一局解锁拾荒者；两个结局解锁夜行人；三个结局解锁听潮者。开局从已解锁特质中抽三个供选择。\n\n本版为离线规则游戏，不需要 AI 或联网。");return;
 }
 if(life_view_==3){
 for(int i=0;i<6;i++)Label(body_,(glLife.endings&(1u<<i)?std::string("已收集 · ")+LIFE_ENDINGS[i]+"\n"+LIFE_END_TEXT[i]:std::string("未收集 · ")+LIFE_ENDINGS[i]),glLife.endings&(1u<<i)?0x32E6CF:0x9AAFC5);
 Label(body_,"线索：牵挂与温暖的物品能带人回家；洞察配合车票或磁带能记录夜城；钥匙与牵挂能让你守一班夜。其他归途取决于本局洞察与梦境。");return;
 }
 Label(body_,"精力 "+std::to_string(glLife.energy)+"  洞察 "+std::to_string(glLife.sense)+"  牵挂 "+std::to_string(glLife.ties)+"  零钱 "+std::to_string(glLife.coins),0xFFE4A0);
 if(life_view_==2){Label(body_,"特质："+std::string(glLife.trait>=0?LIFE_TRAITS[glLife.trait]:"尚未选择"));Label(body_,"行囊："+LifeItemText(glLife.items));
 for(int i=0;i<glLife.step;i++){auto& e=LIFE_EVENTS[glLife.history[i]];int c=glLife.decisions[i];Label(body_,std::to_string(i+1)+". "+e.title,0x32E6CF);Label(body_,c==3?"做梦 / 未作选择":e.choices[c].label,0xF878C1);Label(body_,c==3?e.dream:e.choices[c].result);}return;}
 Label(body_,"行囊："+LifeItemText(glLife.items),0x9AAFC5);
 switch(glLife.phase){
 case 1:Label(body_,"今晚，你带着哪一种性格出门？");for(int i=0;i<3;i++){int t=glLife.offers[i];button(std::string(LIFE_TRAITS[t])+"\n"+LIFE_TRAIT_DESC[t],i);}break;
 case 2:Label(body_,"第 "+std::to_string(glLife.step+1)+" / 8 个时刻 · 选择路线",0x32E6CF);for(int i=0;i<2;i++){auto& e=LIFE_EVENTS[glLife.offers[i]];button(std::string(LIFE_AREAS[e.area])+"\n"+e.title,3+i);}button("休息一次 · 零钱 -1 / 精力 +3",40,!glLife.rested&&glLife.coins>=1&&glLife.energy<9);button("查看行囊与经历",102);break;
 case 3:{auto& e=LIFE_EVENTS[glLife.current];Label(body_,std::string(LIFE_AREAS[e.area])+" / "+e.title,0x32E6CF);Label(body_,e.description);
 if(!glLife.armed){Label(body_,glLife.timed?"读完再开始；限时期间可滚动。回主菜单暂停。":"从容模式，不设倒计时。");button(glLife.timed?"准备好了，开始抉择":"看看有哪些选择",5);}
 else{life_clock_=Label(body_,glLife.timed?"剩余 "+std::to_string(glLife.remaining)+" 秒 · 回主菜单暂停":"从容模式 · 慢慢决定",0xF878C1);for(int i=0;i<3;i++){auto& c=e.choices[i];button(std::string(c.label)+"\n"+LifeEffect(c),10+i,LifeCan(glLife,i));}button("暂不参与 · 把它当作一个梦",13);}break;}
 case 4:{auto& e=LIFE_EVENTS[glLife.current];Label(body_,glLife.lastChoice==3?"梦中惊醒":"回到日常",0xF878C1);Label(body_,glLife.lastChoice==3?e.dream:e.choices[glLife.lastChoice].result);button(glLife.step==8?"走向归途":"继续这一晚",20);break;}
 case 5:Label(body_,"旅人补给 · 选一件带走\n重复物品变为一枚零钱。");for(int i=0;i<3;i++)button(LIFE_ITEMS[glLife.offers[i]],30+i);break;
 case 6:Label(body_,"夜城的出口 · 你想怎样结束今晚？",0x32E6CF);
 button("回到自己的日常",50);button("送一个人回家\n牵挂≥5；雨伞 / 热饭 / 护符之一",51,LifeEndCan(glLife,1));button("留下夜城地图\n洞察≥6；车票 / 磁带之一",52,LifeEndCan(glLife,2));button("替车站守一班夜\n牵挂≥4；钥匙",53,LifeEndCan(glLife,3));break;
 case 7:Label(body_,std::string("结局 / ")+LIFE_ENDINGS[glLife.ending],0x32E6CF);Label(body_,LIFE_END_TEXT[glLife.ending]);Label(body_,"第 "+std::to_string(glLife.runs)+" 局已完成。\n完整经历可在行囊里重读；存储卡日志在 PKOS/GAME。");button("重读这一局",102);button("结局图鉴",103);button("再次出发",104);break;
 default:life_view_=0;break;
 }
}
