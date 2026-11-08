// User-owned adapter for the installed desktop UI. No coordinates or keyboard injection.
// Input is one JSON object on stdin; output is one JSON result. Fails closed on ambiguity.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Security.Cryptography;
using System.Windows.Automation;
using System.Web.Script.Serialization;

public class TaskControl {
    static JavaScriptSerializer json = new JavaScriptSerializer();
    static AutomationElement[] Find(AutomationElement root, ControlType type, string name) {
        var conditions = new List<Condition>();
        if (type != null) conditions.Add(new PropertyCondition(AutomationElement.ControlTypeProperty,type));
        if (name != null) conditions.Add(new PropertyCondition(AutomationElement.NameProperty,name));
        Condition condition=conditions.Count==0 ? Condition.TrueCondition : conditions.Count==1 ? conditions[0] : new AndCondition(conditions.ToArray());
        return root.FindAll(TreeScope.Descendants,condition).Cast<AutomationElement>().ToArray();
    }
    static AutomationElement Unique(IEnumerable<AutomationElement> values,string error) {
        var a=values.ToArray();if(a.Length!=1)throw new Exception(error);return a[0];
    }
    static AutomationElement[] Named(AutomationElement root,ControlType type,params string[] names) {
        return Find(root,type,null).Where(e=>names.Contains(e.Current.Name)).ToArray();
    }
    static bool Usable(AutomationElement e) {return e.Current.IsEnabled && !e.Current.IsOffscreen;}
    static bool Invokable(AutomationElement e) {object p;return e.TryGetCurrentPattern(InvokePattern.Pattern,out p);}
    static AutomationElement Document(string title) {
        var processes=Process.GetProcessesByName("ChatGPT").Where(p=>{
            try{return p.MainWindowHandle!=IntPtr.Zero && Regex.IsMatch(p.MainModule.FileName,@"\\OpenAI\.Codex_[^\\]+\\app\\ChatGPT\.exe$",RegexOptions.IgnoreCase);}catch{return false;}
        }).ToArray();
        if(processes.Length!=1)throw new Exception("CODEX_WINDOW_NOT_UNIQUE");
        var root=AutomationElement.FromHandle(processes[0].MainWindowHandle);
        // A question panel can disable its document while its own answer
        // controls remain usable. Input targets are checked separately below.
        var docs=Find(root,ControlType.Document,title);
        if(docs.Length==0)throw new Exception("BOUND_CHAT_NOT_VISIBLE");
        return Unique(docs.Where(e=>!e.Current.IsOffscreen),"BOUND_CHAT_NOT_VISIBLE matched="+docs.Length+" flags="+String.Join(",",docs.Select(e=>(e.Current.IsEnabled?"enabled":"disabled")+"/"+(e.Current.IsOffscreen?"offscreen":"visible"))));
    }
    static AutomationElement Composer(AutomationElement doc) {
        return Unique(Named(doc,ControlType.Edit,"和 Codex 一起工作","Work with Codex").Where(Usable),"COMPOSER_NOT_UNIQUE");
    }
    static AutomationElement StopButton(AutomationElement doc) {
        var editor=Composer(doc);var ancestor=editor;
        // Stop only inside the same small composer subtree, never a transcript/sidebar button.
        for(int depth=0;depth<5;depth++) {
            ancestor=TreeWalker.ControlViewWalker.GetParent(ancestor);
            if(ancestor==null || Automation.Compare(ancestor,doc))break;
            var buttons=Named(ancestor,ControlType.Button,"停止","Stop").Where(Usable).ToArray();
            if(buttons.Length>1)throw new Exception("STOP_NOT_UNIQUE");
            if(buttons.Length==1)return buttons[0];
        }
        throw new Exception("STOP_NOT_PRESENT");
    }
    class Question {
        public string id; public string title; public string[] options;
        public bool canWrite; public bool canSelect;
        [ScriptIgnore] public AutomationElement group;
        [ScriptIgnore] public AutomationElement editor;
        [ScriptIgnore] public AutomationElement[] radios;
    }
    static string Hash(string text){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(text))).Replace("-","").ToLowerInvariant().Substring(0,32);}
    static Question[] Questions(AutomationElement doc) {
        var result=new List<Question>();
        // Identify a single answer form by its structure; its input placeholder
        // is translated and must not be used as a mandatory protocol field.
        foreach(var editor in Find(doc,ControlType.Edit,null).Where(Usable)) {
            var group=TreeWalker.ControlViewWalker.GetParent(editor);bool found=false;
            for(int depth=0;depth<4 && group!=null;depth++,group=TreeWalker.ControlViewWalker.GetParent(group)) {
                if(Automation.Compare(group,doc))break;
                if(Named(group,ControlType.Button,"跳过","Skip").Length==1 && Named(group,ControlType.Button,"发送","Send").Length==1){found=true;break;}
            }
            if(!found || Find(group,ControlType.Edit,null).Count(Usable)!=1)continue;
            var texts=new List<string>();
            for(var child=TreeWalker.ControlViewWalker.GetFirstChild(group);child!=null;child=TreeWalker.ControlViewWalker.GetNextSibling(child))
                if(child.Current.ControlType==ControlType.Text && !new[]{"问题","Question","Questions"}.Contains(child.Current.Name) && !String.IsNullOrWhiteSpace(child.Current.Name))texts.Add(child.Current.Name);
            if(texts.Count!=1 || texts[0].Length>2000)continue;
            var radios=Find(group,ControlType.RadioButton,null).Where(Usable).ToArray();
            if(radios.Length>6 || radios.Any(r=>r.Current.Name.Length>300))continue;
            string title=texts[0];string[] options=radios.Select(r=>r.Current.Name).ToArray();object pattern;
            bool canSelect=radios.All(r=>r.TryGetCurrentPattern(SelectionItemPattern.Pattern,out pattern) || r.TryGetCurrentPattern(InvokePattern.Pattern,out pattern));
            string id=Hash(String.Join(",",group.GetRuntimeId())+"\n"+title+"\n"+json.Serialize(options));
            result.Add(new Question{id=id,title=title,options=options,group=group,editor=editor,radios=radios,
                canWrite=editor.TryGetCurrentPattern(ValuePattern.Pattern,out pattern),canSelect=canSelect});
        }
        return result.ToArray();
    }
    static void Output(object result){Console.WriteLine(json.Serialize(result));}
    static void Trace(object result){try{File.AppendAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"question-native-events.jsonl"),json.Serialize(result)+"\n",new UTF8Encoding(false));}catch{}}
    [STAThread] public static int Main() {
        Console.InputEncoding=new UTF8Encoding(false);Console.OutputEncoding=new UTF8Encoding(false);
        bool invoked=false;string traceAction="";
        try {
            var request=json.Deserialize<Dictionary<string,object>>(Console.ReadLine());
            string action=(string)request["action"],title=(string)request["title"];
            traceAction=action;
            if(String.IsNullOrWhiteSpace(title)||title.Length>1000 || !request.ContainsKey("threadId") || !Regex.IsMatch((string)request["threadId"],@"^[a-f0-9-]{36}$"))throw new Exception("UNBOUND_TITLE");
            if(action!="probe" && action!="stop" && action!="questions" && action!="question_probe" && action!="answer")throw new Exception("INVALID_ACTION");
            var doc=Document(title);
            if(action=="question_probe") {
                var states=new List<object>();
                foreach(var q in Questions(doc)) {
                    object p;string value="";if(q.editor.TryGetCurrentPattern(ValuePattern.Pattern,out p))value=((ValuePattern)p).Current.Value;
                    var selected=q.radios.Select(r=>r.TryGetCurrentPattern(SelectionItemPattern.Pattern,out p)&&((SelectionItemPattern)p).Current.IsSelected).ToArray();
                    states.Add(new{id=q.id,title=q.title,options=q.options,selected=selected,input=value,buttons=Find(q.group,ControlType.Button,null).Select(b=>new{name=b.Current.Name,enabled=b.Current.IsEnabled,offscreen=b.Current.IsOffscreen,invoke=Invokable(b)}).ToArray()});
                }
                Output(new{ok=true,states=states});return 0;
            }
            if(action=="questions") {
                Output(new {ok=true,questions=Questions(doc)});return 0;
            }
            if(action=="answer") {
                string id=(string)request["questionId"],answer=(string)request["answer"];
                var questions=Questions(doc).Where(q=>q.id==id).ToArray();
                if(questions.Length!=1)throw new Exception("QUESTION_CHANGED");
                var question=questions[0];object pattern;
                int index=Array.IndexOf(question.options,answer);
                if(index>=0) {
                    var radio=question.radios[index];
                    // Activate the option's real handler. Merely changing an
                    // accessibility selection can leave the React draft empty.
                    // The option handler also owns clearing its freeform input.
                    if(radio.TryGetCurrentPattern(InvokePattern.Pattern,out pattern)) {
                        // This desktop's immediate-response options dispatch on
                        // activation. A second Send click is not part of this
                        // transaction; only a native chat receipt proves delivery.
                        invoked=true;((InvokePattern)pattern).Invoke();
                        Trace(new{time=DateTime.UtcNow.ToString("o"),action="answer",questionId=id,result="option_activated"});
                        Output(new{ok=true,state="submitted",questionId=id,phase="option_activation"});return 0;
                    }
                    else if(radio.TryGetCurrentPattern(SelectionItemPattern.Pattern,out pattern))((SelectionItemPattern)pattern).Select();
                    else throw new Exception("OPTION_SELECTION_UNSUPPORTED");
                }else {
                    if(String.IsNullOrWhiteSpace(answer)||answer.Length>2000)throw new Exception("INVALID_ANSWER");
                    if(!question.editor.TryGetCurrentPattern(ValuePattern.Pattern,out pattern))throw new Exception("FREE_TEXT_UNSUPPORTED");
                    ((ValuePattern)pattern).SetValue(answer);
                }
                // Re-observe after selecting, and require the same card and an enabled submit button.
                System.Threading.Thread.Sleep(180);
                questions=Questions(Document(title)).Where(q=>q.id==id).ToArray();
                if(questions.Length!=1)throw new Exception("QUESTION_CHANGED_AFTER_SELECTION");
                if(index>=0) {
                    if(!questions[0].radios[index].TryGetCurrentPattern(SelectionItemPattern.Pattern,out pattern) || !((SelectionItemPattern)pattern).Current.IsSelected)throw new Exception("ANSWER_SELECTION_NOT_CONFIRMED");
                }else {
                    if(!questions[0].editor.TryGetCurrentPattern(ValuePattern.Pattern,out pattern) || ((ValuePattern)pattern).Current.Value!=answer)throw new Exception("ANSWER_TEXT_NOT_CONFIRMED");
                }
                var send=Unique(Named(questions[0].group,ControlType.Button,"发送","Send").Where(Usable),"ANSWER_SUBMIT_NOT_READY");
                if(!send.TryGetCurrentPattern(InvokePattern.Pattern,out pattern))throw new Exception("ANSWER_SUBMIT_UNSUPPORTED");
                invoked=true;((InvokePattern)pattern).Invoke();
                System.Threading.Thread.Sleep(250);
                bool stillVisible=Questions(Document(title)).Any(q=>q.id==id);
                Trace(new{time=DateTime.UtcNow.ToString("o"),action="answer",questionId=id,cardStillVisible=stillVisible,result="invoked"});
                Output(new {ok=true,state="submitted",questionId=id,cardStillVisible=stillVisible});return 0;
            }
            if(action=="probe") {
                bool stop=false,invoke=false;try{var b=StopButton(doc);stop=true;invoke=Invokable(b);}catch{}
                Output(new {ok=true,stopAvailable=stop,stopInvokable=invoke,guideCount=Find(doc,ControlType.Button,"引导").Count(Usable)});return 0;
            }
            Composer(doc);
            var button=StopButton(doc);
            object stopPattern;if(!button.TryGetCurrentPattern(InvokePattern.Pattern,out stopPattern))throw new Exception("INVOKE_UNSUPPORTED");
            // Re-check document immediately before invocation; no cached handles from older requests.
            Document(title);if(!Usable(button))throw new Exception("BUTTON_CHANGED");
            invoked=true;((InvokePattern)stopPattern).Invoke();
            Output(new {ok=true,state="invoked",action=action});return 0;
        }catch(Exception e){if(traceAction=="answer")Trace(new{time=DateTime.UtcNow.ToString("o"),action="answer",invoked=invoked,error=e.Message});Output(new {ok=false,state=invoked?"unknown":"not_invoked",error=e.Message});return 1;}
    }
}
