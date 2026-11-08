#pragma once
#include <cstdint>
#include <algorithm>
#include "life_content.h"
// Shared deterministic engine. UI, persistence and device I/O are kept outside.
struct LifeState {
 uint32_t version=1,seed=1,seen=0,catalog=0,endings=0,runs=0;
 int phase=0,step=0,energy=6,sense=1,ties=1,coins=3,trait=-1,items=0,dreams=0;
 int offers[3]={0,1,2},current=0,lastChoice=0,ending=-1,remaining=45;
 int history[8]={-1,-1,-1,-1,-1,-1,-1,-1};
 int decisions[8]={-1,-1,-1,-1,-1,-1,-1,-1};
 bool timed=false,armed=false,rewardDone=false,rested=false;
};
static const char* LIFE_AREAS[]={"地铁","站台","街区","屋顶"};
static const char* LIFE_TRAITS[]={"慢半拍","观察者","热心肠","拾荒者","夜行人","听潮者"};
static const char* LIFE_TRAIT_DESC[]={"限时模式每次多 20 秒。","开局洞察 +2。","开局牵挂 +2。","开局零钱 +2；通关后解锁。","开局精力 +2；收集两个结局解锁。","开局带一盘磁带；收集三个结局解锁。"};
static const char* LIFE_ITEMS[]={"异常车票","潮声磁带","旧雨伞","无名钥匙","热饭","小灯","旅人护符"};
static const char* LIFE_ENDINGS[]={"平凡的灯火","把一个人送回家","夜城的记录者","留下守一班夜","梦醒仍有归处","明天再出发"};
static const char* LIFE_END_TEXT[]={
 "你回到熟悉的街口。世界没有解释今晚的一切，但窗户里有灯，口袋里还有属于你的钥匙。普通的一天，也值得认真结束。",
 "有人在末班车门前犹豫。你叫住他，一起穿过雨和隧道。回家的路没有变短，却终于不需要一个人走。",
 "你把沿途线索连成一张夜城地图，留下给下一位迷路的人。异常没有消失，但它们从此有了可以被讲述的名字。",
 "你打开站务室，替疲惫的值班人守到天亮。每一个走出车站的人，都被你轻轻说了一声：路上小心。",
 "你在座位上醒来，发现自己把很多地方梦了一遍。手机里有一条普通的问候。你回复了它，梦也有了落脚的地方。",
 "你没有带走完整答案，而是把今天记在一张纸上。明天还可以选择另一条路；没有看完的风景，不是失败。"};
inline int LifeCount(uint32_t x){int n=0;for(;x;x&=x-1)++n;return n;}
inline uint32_t LifeRand(LifeState& s){s.seed^=s.seed<<13;s.seed^=s.seed>>17;s.seed^=s.seed<<5;return s.seed;}
inline void LifeGive(LifeState& s,int item){if(s.items&item)s.coins=std::min(9,s.coins+1);else s.items|=item;}
inline void LifeRoutes(LifeState& s){
 int pool[LIFE_EVENT_COUNT],n=0;for(int i=0;i<LIFE_EVENT_COUNT;i++)if(!(s.seen&(1u<<i)))pool[n++]=i;
 s.offers[0]=pool[LifeRand(s)%n];int alt[LIFE_EVENT_COUNT],m=0;
 for(int i=0;i<n;i++)if(LIFE_EVENTS[pool[i]].area!=LIFE_EVENTS[s.offers[0]].area)alt[m++]=pool[i];
 if(!m)for(int i=0;i<n;i++)if(pool[i]!=s.offers[0])alt[m++]=pool[i];
 s.offers[1]=alt[LifeRand(s)%m];s.phase=2;s.armed=false;s.rested=false;
}
inline void LifeNew(LifeState& s,uint32_t seed,bool timed){
 auto old=s;s=LifeState{};s.seed=seed?seed:1;s.catalog=old.catalog;s.endings=old.endings;s.runs=old.runs;s.timed=timed;s.phase=1;
 int pool[6]={0,1,2,3,4,5},n=3+(s.runs>0)+(LifeCount(s.endings)>=2)+(LifeCount(s.endings)>=3);
 for(int i=0;i<3;i++){int k=i+LifeRand(s)%(n-i);std::swap(pool[k],pool[i]);s.offers[i]=pool[i];}
}
inline bool LifeTrait(LifeState& s,int pick){if(s.phase!=1||pick<0||pick>2)return false;s.trait=s.offers[pick];
 if(s.trait==1)s.sense+=2;
 if(s.trait==2)s.ties+=2;
 if(s.trait==3)s.coins+=2;
 if(s.trait==4)s.energy+=2;
 if(s.trait==5)s.items|=2;
 LifeRoutes(s);
 return true;}
inline bool LifeRoute(LifeState& s,int pick){if(s.phase!=2||pick<0||pick>1)return false;s.current=s.offers[pick];s.phase=3;s.remaining=s.trait==0?65:45;s.armed=false;
 return true;}
inline bool LifeCan(const LifeState& s,int c){if(s.phase!=3||c<0||c>2)return false;auto& a=LIFE_EVENTS[s.current].choices[c];
 return (s.items&a.need)==a.need&&s.sense>=a.minSense&&s.ties>=a.minTies&&s.energy+a.energy>=0&&s.coins+a.coins>=0;}
inline bool LifeChoose(LifeState& s,int c){if(s.phase!=3||c<0||c>3||s.step>=8||(c<3&&!LifeCan(s,c)))return false;
 if(c==3)++s.dreams;else{auto& a=LIFE_EVENTS[s.current].choices[c];
 s.energy=std::clamp(s.energy+a.energy,0,9);s.sense=std::clamp(s.sense+a.sense,0,9);s.ties=std::clamp(s.ties+a.ties,0,9);s.coins=std::clamp(s.coins+a.coins,0,9);
 if(a.item)LifeGive(s,a.item);}
 s.history[s.step]=s.current;s.decisions[s.step]=c;++s.step;s.lastChoice=c;s.seen|=1u<<s.current;s.catalog|=1u<<s.current;s.phase=4;s.armed=false;
 return true;}
inline bool LifeNext(LifeState& s){if(s.phase!=4)return false;
 if(s.step==8){s.phase=6;
 return true;}
 if(s.step==4&&!s.rewardDone){int pool[7]={0,1,2,3,4,5,6};for(int i=0;i<3;i++){int k=i+LifeRand(s)%(7-i);std::swap(pool[k],pool[i]);s.offers[i]=pool[i];}s.phase=5;
 return true;}LifeRoutes(s);
 return true;}
inline bool LifeReward(LifeState& s,int pick){if(s.phase!=5||pick<0||pick>2)return false;LifeGive(s,1<<s.offers[pick]);s.rewardDone=true;LifeRoutes(s);
 return true;}
inline bool LifeRest(LifeState& s){if(s.phase!=2||s.rested||s.coins<1||s.energy>=9)return false;--s.coins;s.energy=std::min(9,s.energy+3);s.rested=true;
 return true;}
inline bool LifeEndCan(const LifeState& s,int pick){if(s.phase!=6||pick<0||pick>3)return false;
 if(pick==1)return s.ties>=5&&(s.items&(4|16|64));
 if(pick==2)return s.sense>=6&&(s.items&(1|2));
 if(pick==3)return s.ties>=4&&(s.items&8);
 return true;}
inline bool LifeEnd(LifeState& s,int pick){if(!LifeEndCan(s,pick))return false;s.ending=pick==1?1:pick==2?2:pick==3?3:s.dreams>=3?4:s.sense>=4?5:0;
 s.endings|=1u<<s.ending;++s.runs;s.phase=7;s.armed=false;
 return true;}
inline bool LifeValid(const LifeState& s){
 if(s.version!=1||!s.seed||s.phase<0||s.phase>7||s.step<0||s.step>8||s.current<0||s.current>=LIFE_EVENT_COUNT||s.trait< -1||s.trait>5||s.ending< -1||s.ending>5||s.items<0||s.items>127||s.remaining<0||s.remaining>65||s.endings>63)return false;
 for(int v:{s.energy,s.sense,s.ties,s.coins})if(v<0||v>9)return false;
 if(s.dreams<0||s.dreams>s.step||LifeCount(s.seen)!=s.step||s.catalog>>LIFE_EVENT_COUNT||s.seen>>LIFE_EVENT_COUNT)return false;
 for(int i=0;i<3;i++){int max=s.phase==1?5:s.phase==5?6:LIFE_EVENT_COUNT-1;
 if(s.offers[i]<0||s.offers[i]>max)return false;}
 if(s.phase>=2&&s.trait<0)return false;
 if(s.phase==7&&s.ending<0)return false;
 for(int i=0;i<s.step;i++)if(s.history[i]<0||s.history[i]>=LIFE_EVENT_COUNT||s.decisions[i]<0||s.decisions[i]>3)return false;
 return true;
}
inline bool LifeTests(){
 for(unsigned seed=1;seed<=1500;seed++){
 LifeState s;LifeNew(s,seed,seed%2);
 if(!LifeTrait(s,seed%3)||!LifeValid(s))return false;
 for(int step=0;step<8;step++){
 if(s.offers[0]==s.offers[1]||LIFE_EVENTS[s.offers[0]].area==LIFE_EVENTS[s.offers[1]].area)return false;
 if(!LifeRoute(s,LifeRand(s)%2)||(s.seen&(1u<<s.current)))return false;
 int choice=LifeRand(s)%4;
 if(choice<3&&!LifeCan(s,choice))choice=2;
 if(!LifeChoose(s,choice)||LifeChoose(s,choice)||!LifeValid(s))return false;
 if(!LifeNext(s))return false;
 if(s.phase==5&&!LifeReward(s,LifeRand(s)%3))return false;
 }
 int pick=LifeRand(s)%4;
 if(!LifeEndCan(s,pick))pick=0;
 if(!LifeEnd(s,pick)||s.phase!=7||s.step!=8||LifeEnd(s,pick)||!LifeValid(s))return false;
 }
 for(int ending=0;ending<6;ending++){
 LifeState s;s.phase=6;s.ties=9;s.sense=9;s.items=127;
 if(ending==0)s.sense=0;
 if(ending==4)s.dreams=3;
 if(!LifeEnd(s,ending>=1&&ending<=3?ending:0)||s.ending!=ending)return false;
 }
 LifeState s;LifeNew(s,1,false);LifeTrait(s,0);LifeRest(s);
 if(LifeRest(s))return false;
 return true;
}
