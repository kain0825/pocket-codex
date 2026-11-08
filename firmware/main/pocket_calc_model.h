#pragma once
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

class PocketCalcModel {
public:
    const std::string& Expression() const{return expression_;}
    const std::string& Value() const{return value_;}
    bool Error() const{return error_;}
    void Key(const std::string& key){
        if(key=="AC"){Clear();return;}
        if(key=="BS"){
            if(error_||evaluated_){Clear();return;}
            if(!expression_.empty())expression_.pop_back();
            if(expression_.empty()||expression_=="-")expression_="0";
            FindEntry();value_=expression_.substr(entry_);return;
        }
        if(key=="="){
            if(error_||evaluated_)return;
            double result=0;int error=Evaluate(expression_,result);
            if(error){Fail(error==1?"不能除以零":"表达式无效或超出范围");return;}
            value_=Format(result);history_=expression_;expression_=value_;entry_=0;evaluated_=true;return;
        }
        if(error_)Clear();
        if(key=="SIGN"){
            if(entry_==expression_.size())expression_+="0";
            if(expression_[entry_]=='-')expression_.erase(entry_,1);else expression_.insert(entry_,"-");
            value_=expression_.substr(entry_);return;
        }
        if(key=="%"){
            const std::string current=expression_.substr(entry_);double value=0;
            if(Evaluate(current,value)){Fail("请先输入数字");return;}
            value/=100;
            if(entry_>0&&(expression_[entry_-1]=='+'||expression_[entry_-1]=='-')){
                double base=0;if(Evaluate(expression_.substr(0,entry_-1),base)){Fail("表达式无效");return;}value*=base;
            }
            if(!std::isfinite(value)){Fail("计算超出范围");return;}
            value_=Format(value);expression_.replace(entry_,std::string::npos,value_);return;
        }
        if(key.size()==1&&Operator(key[0])){
            if(entry_==expression_.size()){
                if(key=="-"&&(expression_.back()=='*'||expression_.back()=='/'))expression_+='-';
                else expression_.back()=key[0];
                return;
            }
            if(expression_.substr(entry_)=="-")return;
            if(!Room(1))return;
            expression_+=key;entry_=expression_.size();evaluated_=false;return;
        }
        if(key.size()!=1||!((key[0]>='0'&&key[0]<='9')||key=="."))return;
        if(evaluated_)Clear();
        if(!Room(1))return;
        auto current=expression_.substr(entry_);
        if(key!="."){
            unsigned digits=0;for(char c:current)if(c>='0'&&c<='9')++digits;
            if(digits>=15){Fail("数字过长");return;}
        }
        if(key=="."){
            if(current.find('.')!=std::string::npos)return;
            if(current.empty()||current=="-")expression_+="0";
            expression_+='.';
        }else if(current=="0"||current=="-0")expression_.back()=key[0];
        else expression_+=key;
        value_=expression_.substr(entry_);
    }
    std::string Caption() const{return evaluated_?history_+" =":expression_;}
    static int Evaluate(const std::string& text,double& value){
        Parser parser{text.c_str()};value=parser.Sum();
        return parser.error?parser.error:(*parser.p||!std::isfinite(value)?2:0);
    }
private:
    struct Parser {
        const char* p;int error=0;
        double Number(){
            errno=0;char* end=nullptr;double value=std::strtod(p,&end);
            if(end==p||errno==ERANGE||!std::isfinite(value)){error=2;return 0;}p=end;return value;
        }
        double Product(){
            double result=Number();
            while(!error&&(*p=='*'||*p=='/')){char op=*p++;double rhs=Number();if(error)break;
                if(op=='/'&&rhs==0){error=1;return 0;}
                result=op=='*'?result*rhs:result/rhs;if(!std::isfinite(result))error=2;
            }return result;
        }
        double Sum(){double result=Product();while(!error&&(*p=='+'||*p=='-')){char op=*p++;double rhs=Product();result=op=='+'?result+rhs:result-rhs;if(!std::isfinite(result))error=2;}return result;}
    };
    static bool Operator(char c){return c=='+'||c=='-'||c=='*'||c=='/';}
    static std::string Format(double value){char s[40];snprintf(s,sizeof(s),"%.12g",value==0?0:value);return s;}
    void Clear(){expression_="0";value_="0";history_.clear();entry_=0;error_=evaluated_=false;}
    void Fail(const char* message){error_=true;evaluated_=false;value_=message;}
    bool Room(size_t add){if(expression_.size()+add>96){Fail("输入过长，请分段计算");return false;}return true;}
    void FindEntry(){entry_=0;for(size_t i=1;i<expression_.size();++i)if(Operator(expression_[i])&&!Operator(expression_[i-1])&&expression_[i-1]!='e'&&expression_[i-1]!='E')entry_=i+1;}
    std::string expression_="0",value_="0",history_;
    size_t entry_=0;
    bool error_=false,evaluated_=false;
};
