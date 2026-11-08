"""Recent local chats and isolated per-chat controllers/storage."""
import hashlib,json,re,threading,time
from pathlib import PureWindowsPath
from desktop_client import THREAD,ScopedClient
from task_control import TaskController

class TaskCatalog:
    def __init__(self,primary,client,root,start=True):
        self.primary=primary;self.client=client;self.root=root;self.lock=threading.Lock()
        self.records={THREAD:{'id':THREAD,'title':primary.snapshot.get('title','开发板'),'hostId':'local'}}
        self.bridges={THREAD:primary};self.voices={}
        self.order=[];self.project_names={};self.project_refresh=0;self.attention={};self.cursors={}
        self.attention_scan=0
        self.snapshot={'connected':False,'tasks':[],'revision':''}
        if start:threading.Thread(target=self.loop,daemon=True).start()
    def refresh(self):
        try:
            if time.monotonic()-self.project_refresh>60:
                projects=self.client.call('list_projects',{})
                self.project_names={p['projectId']:p.get('label','未命名项目') for p in projects.get('projects',[]) if p.get('hostId')=='local' and p.get('projectKind')=='local'}
                self.project_refresh=time.monotonic()
            data=self.client.call('list_threads',{'mode':'recent','limit':50});records={}
            for row in data.get('pinnedThreads',[])+data.get('threads',[]):
                ident=row.get('id','')
                if row.get('kind')!='codex' or row.get('hostId')!='local' or not re.fullmatch('[a-f0-9-]{36}',ident):continue
                records[ident]=row
            if THREAD not in records:records[THREAD]=self.records[THREAD]
            self.client.allowed_threads.update(records)
            # Read compact attention snapshots without navigating the desktop or loading other chat bodies on the board.
            priority=[i for i,r in records.items() if i!=THREAD and (r.get('status') in ('active','needsAttention') or self.attention.get(i))]
            other=[i for i in records if i!=THREAD and i not in priority]
            scan=(other[self.attention_scan%len(other):]+other[:self.attention_scan%len(other)])[:2] if other else []
            self.attention_scan+=len(scan)
            watched=(priority[:6]+scan) if scan else priority[:8]
            for ident in watched:
                try:
                    target={'threadId':ident}
                    if ident in self.cursors:target['afterCursor']=self.cursors[ident]
                    state=self.client.call('wait_threads',{'targets':[target],'timeoutMs':0})
                    poll=next((p for p in state.get('polls',[]) if p.get('thread',{}).get('id')==ident),None)
                    wake=state.get('wake') or {}
                    if wake.get('threadId')==ident and wake.get('reason')=='needsAttention':self.attention[ident]=True
                    elif poll and (poll.get('changed') or poll.get('thread',{}).get('status',{}).get('type')!='active'):self.attention[ident]=False
                    if poll and poll.get('cursor'):self.cursors[ident]=poll['cursor']
                except Exception:pass  # Unknown stays unknown; never infer a pending question from a running label alone.
            tasks=[]
            with self.lock:
                self.records=records;self.client.allowed_threads.update(records)
                for ident,row in records.items():
                    status=row.get('status','unknown');label={'active':'执行中','idle':'空闲','notLoaded':'未运行'}.get(status,'状态未知')
                    scope=self.bridges.get(ident)
                    questions=-1
                    if scope and scope.questions and scope.questions.snapshot.get('available') and time.monotonic()-scope.questions.snapshot.get('observedAt',0)<10:
                        questions=len(scope.questions.snapshot.get('questions',[]))
                    if scope and scope.controller:scope.controller.title=row.get('title','未命名聊天')
                    pid=row.get('projectId') or 'unassigned'
                    project=self.project_names.get(pid) or (PureWindowsPath(row.get('cwd') or '').name if row.get('projectId') else '未分组聊天')
                    tasks.append({'id':ident,'title':row.get('title','未命名聊天')[:72],
                        'projectId':pid,'project':(project or '未命名项目')[:24],'status':status,'label':label,'questions':questions,'attention':questions>0 or self.attention.get(ident,False),'updatedAt':row.get('updatedAt',0)})
            # Preserve existing identities in place while status changes; new chats append.
            by_id={t['id']:t for t in tasks}
            initial=sorted(tasks,key=lambda t:(t['status']!='active',-t['updatedAt']))
            self.order=[i for i in self.order if i in by_id]+[t['id'] for t in initial if t['id'] not in self.order]
            tasks=[by_id[i] for i in self.order[:64]]
            # Keep activity timestamps for the board's independent recent view;
            # the project view retains its existing stable identity order.
            revision=hashlib.sha256(json.dumps(tasks,ensure_ascii=False,sort_keys=True).encode()).hexdigest()
            self.snapshot={'connected':True,'tasks':tasks,'revision':revision,'limit':64,'listedLocal':len(records)}
        except Exception:
            tasks=[{**t,'status':'unknown','label':'连接异常','questions':-1} for t in self.snapshot['tasks']]
            self.snapshot={'connected':False,'tasks':tasks,'revision':'offline'}
    def loop(self):
        while self.primary.running:self.refresh();time.sleep(5)
    def context(self,ident,default_voice):
        from server import Bridge
        from device_api import VoiceJobs
        with self.lock:
            if ident not in self.records:raise ValueError('Unknown local chat')
            if ident not in self.bridges:
                folder=self.root/'chats'/ident;folder.mkdir(parents=True,exist_ok=True)
                client=ScopedClient(self.client,ident);scope=Bridge(client,self.primary.token,folder/'requests.sqlite',TaskController(client,title=self.records[ident].get('title','未命名聊天')))
                scope.managed=True;self.bridges[ident]=scope
                threading.Thread(target=scope.poll,daemon=True).start()
                threading.Thread(target=scope.questions.loop,daemon=True).start()
            scope=self.bridges[ident];scope.last_access=time.monotonic()
            if ident not in self.voices:self.voices[ident]=default_voice if ident==THREAD else VoiceJobs(self.root/'chats'/ident/'voice')
            return scope,self.voices[ident]
