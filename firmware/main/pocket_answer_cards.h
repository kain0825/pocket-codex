#pragma once
#include <cstdint>
// Original Pocket prompts authored for this implementation, not excerpts
// from Carol Bolt's book. Keep source text in Flash; no full pool RAM copy.
namespace PocketAnswerCards {
inline constexpr const char* Text[]={
    "先给这个念头一点生长的时间。",
    "把它缩小成今天能做的一步。",
    "换一个问题，也许更容易看清。",
    "试着走一条不那么熟悉的路。",
    "有个被略过的细节，值得回头看看。",
    "先把最担心的事说清楚。",
    "这件事可以从一个小实验开始。",
    "今天不必急着给出结论。",
    "你的第一反应，值得再听一遍。",
    "先做一个容易撤回的选择。",
    "答案可能藏在你还没问的人那里。",
    "让事实多走一步，想象先歇一会儿。",
    "找出最重要的那个条件。",
    "先把复杂的部分放到纸上。",
    "换个环境，再想一次。",
    "它可能比想象中更简单。",
    "不要忽略那个让你轻松一点的方向。",
    "给犹豫留个位置，也给行动留一步。",
    "先完成一小块，再决定下一块。",
    "问问自己：我最想保住什么？",
    "这一次，可以从好奇心出发。",
    "把目标讲给一个愿意认真听的人。",
    "给这个想法画一张最简单的草图。",
    "留一点余地，事情还会变化。",
    "答案也许不是二选一。",
    "试着把问题里的一个词换掉。",
    "先确认你是在追求，还是在躲开。",
    "选一个你愿意持续了解的方向。",
    "先听听那个不同意你的声音。",
    "停一下，看看已经拥有的条件。",
    "把期待调小一点，让第一步发生。",
    "这个念头可以先留到明天再看。",
    "给自己一次重新命名它的机会。",
    "找一个能被看见的小结果。",
    "也许需要的是休息，而不是结论。",
    "先问一个更具体的问题。",
    "把事实和猜测分开放一会儿。",
    "向前半步，就可能看到新线索。",
    "先让它有个粗糙的样子。",
    "别急着解释，先认真观察。",
    "看看那个一直被放到最后的选项。",
    "这件事值得再听一个角度。",
    "先处理最容易确认的部分。",
    "想想：怎样才能轻一点地开始？",
    "把问题交给散步的十分钟。",
    "从你最熟悉的地方搭一座小桥。",
    "让一个真实反馈替你回答。",
    "先留下一份记录，再继续探索。",
    "看看能不能减少一个不必要的步骤。",
    "有些答案，要在动手之后才出现。",
    "试着把顺序倒过来。",
    "给那个不起眼的想法一点舞台。",
    "先完成，再回来修饰。",
    "一杯水的时间之后，再读这个问题。",
    "把选择的代价也写在旁边。",
    "找找有没有第三条更温和的路。",
    "别忘了给意外留一扇门。",
    "先让问题安静一会儿。",
    "你可以从一个更小的版本试起。",
    "让下一步比上一遍想得更清楚。",
    "也许已有一个答案，只差说出来。",
    "这次，试着少加一个限制。",
    "把想法说出来，听听它的样子。",
    "这一页，把决定权交还给你。",
};
inline constexpr unsigned Count=sizeof(Text)/sizeof(Text[0]);
static_assert(Count>8);

// Bounded scan avoids retry loops when random numbers repeat. The caller
// supplies its recent unique indices and the current random word.
inline int Pick(uint32_t random,const int* recent,unsigned recent_count){
    if(recent_count>=Count)return -1;
    unsigned rank=random%(Count-recent_count);
    for(unsigned candidate=0;candidate<Count;++candidate){
        bool excluded=false;
        for(unsigned i=0;i<recent_count;++i)if(recent[i]==int(candidate)){excluded=true;break;}
        if(!excluded){if(rank==0)return int(candidate);--rank;}
    }
    return -1;
}
}
