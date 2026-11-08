"""Paired HTTPS board endpoint. Voice originals and transcripts stay separate."""
import concurrent.futures,hashlib,hmac,io,json,re,ssl,threading,time,wave
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit,parse_qs
from desktop_client import THREAD
from local_settings import setting
from monitor_proxy import forward_monitor
from speech_text import NORMALIZER,simplified_transcript

ROOT=Path(__file__).resolve().parent
MODEL=setting('asr_model','small')
ASR_LOCK=threading.Lock()
ASR_MODEL=None
VOICE_MAX_SECONDS=600
VOICE_MAX_FRAMES=16000*VOICE_MAX_SECONDS
VOICE_MAX_BYTES=VOICE_MAX_FRAMES*2+44
VOICE_MAX_TEXT_CHARS=8000

def transcribe_shared(path):
    global ASR_MODEL
    with ASR_LOCK:
        if ASR_MODEL is None:
            from faster_whisper import WhisperModel
            ASR_MODEL=WhisperModel(str(MODEL),device='cpu',compute_type='int8',local_files_only=bool(setting('asr_local_only',False)))
        segments,_=ASR_MODEL.transcribe(str(path),language='zh',vad_filter=True,beam_size=5)
        return ''.join(s.text for s in segments).strip()

class VoiceJobs:
    def __init__(self,root=ROOT/'voice',transcriber=None):
        self.root=root;root.mkdir(exist_ok=True);self.lock=threading.Lock()
        self.executor=concurrent.futures.ThreadPoolExecutor(max_workers=1)
        self.transcriber=transcriber;self.model=None;self.pending=0
        for file in root.glob('*/job.json'):
            data=json.loads(file.read_text(encoding='utf-8'))
            if data.get('state') in ('queued','transcribing'):
                data.update(state='failed',error='电脑服务重启，原录音保留，请重新录音。')
                self.save(file.parent.name,data)
    def read(self,ident):
        if not re.fullmatch('[a-f0-9]{32}',ident):raise ValueError('invalid id')
        with self.lock:
            p=self.root/ident/'job.json'
            return json.loads(p.read_text(encoding='utf-8')) if p.exists() else {'state':'missing'}
    def save(self,ident,data):
        with self.lock:
            p=self.root/ident/'job.json';tmp=p.with_suffix('.tmp')
            tmp.write_text(json.dumps(data,ensure_ascii=False),encoding='utf-8');tmp.replace(p)
    def draft(self,ident,text):
        old=self.read(ident)
        if not isinstance(text,str) or len(text)>VOICE_MAX_TEXT_CHARS:raise ValueError('invalid draft')
        if old.get('state') in ('queued','transcribing'):raise ValueError('transcription busy')
        folder=self.root/ident;folder.mkdir(exist_ok=True)
        (folder/'draft.txt').write_text(text,encoding='utf-8')
        data={**old,'id':ident,'state':'ready' if text.strip() else 'empty','editedText':text}
        self.save(ident,data);return data
    def latest(self):
        paths=sorted(self.root.glob('*/job.json'),key=lambda p:p.stat().st_mtime,reverse=True)
        for file in paths[:10]:
            if file.parent.name.startswith('00000000'):continue
            job=self.read(file.parent.name)
            if job.get('state')=='ready':return job
        return None
    def submit(self,ident,raw):
        previous=self.read(ident)
        digest=hashlib.sha256(raw).hexdigest()
        if previous.get('sha256'):
            if previous['sha256']!=digest:raise ValueError('id conflict')
            return previous
        with wave.open(io.BytesIO(raw),'rb') as w:
            if (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getcomptype())!=(1,2,16000,'NONE') or not 6400<=w.getnframes()<=VOICE_MAX_FRAMES:raise ValueError('require 0.4-600 seconds PCM16 mono 16kHz')
            if len(w.readframes(w.getnframes()))!=w.getnframes()*2:raise ValueError('truncated WAV')
        with self.lock:
            if self.pending>=3:raise ValueError('queue full')
            folder=self.root/ident
            # Exclusive folder creation also prevents concurrent replacement.
            folder.mkdir();(folder/'original.wav').write_bytes(raw);self.pending+=1
        data={'id':ident,'state':'queued','sha256':digest,'text':''}
        self.save(ident,data);self.executor.submit(self.run,ident,data)
        return data
    def run(self,ident,data):
        try:
            data={**data,'state':'transcribing'};self.save(ident,data)
            if self.transcriber:text=self.transcriber(self.root/ident/'original.wav')
            else:
                text=transcribe_shared(self.root/ident/'original.wav')
            # Keep the recognizer's exact output for provenance. Only the new
            # generated draft is normalized; manual edits remain user-owned.
            (self.root/ident/'transcript.txt').write_text(text,encoding='utf-8')
            text=simplified_transcript(text)
            (self.root/ident/'normalized-transcript.txt').write_text(text,encoding='utf-8')
            data.update(textScript='Hans',textNormalizer=NORMALIZER)
            # Never send a shortened prefix as if it were the whole instruction.
            if len(text)>VOICE_MAX_TEXT_CHARS:
                data.update(state='too_long',text='',characters=len(text),error='转写超过8000字，完整文本已保存在电脑，请分段发送。')
            else:data.update(state='ready' if text else 'empty',text=text)
        except Exception:
            data.update(state='failed',error='本地转写失败，原始录音已保留。')
        finally:
            self.save(ident,data)
            with self.lock:self.pending-=1

def compact(snapshot):
    msgs=snapshot.get('messages',[])
    replies=[m for m in msgs if m['role']=='assistant']
    text='\n\n'.join(m['text'] for m in replies[-2:])[-1600:]
    return {'connected':snapshot.get('connected',False),'status':snapshot.get('status','unknown'),
        'title':snapshot.get('title','聊天'),'threadId':snapshot.get('threadId',THREAD),'reply':text,'revision':snapshot.get('revision',''),
        'activeTurnId':snapshot.get('activeTurnId',''),
        'messages': [{'role':m['role'],'text':m['text'][-1000:]} for m in ([m for m in msgs if m['role']=='user' and not m['text'].lstrip().startswith('<send_user_message_question_reply>')][-1:]+replies[-1:])]}

def chat_tabs(snapshot,ident):
    active=[t for t in snapshot['tasks'] if t['id']==ident or t.get('status')=='active' or t.get('questions',-1)>0 or t.get('attention')]
    total=len(active);selected=next((t for t in active if t['id']==ident),None)
    if total>12:active=([selected]+[t for t in active if t['id']!=ident][:11]) if selected else active[:12]
    fields=('id','title','status','label','questions','attention')
    tabs=[{k:t[k] for k in fields if k in t} for t in active]
    revision=hashlib.sha256(json.dumps(tabs,ensure_ascii=False,sort_keys=True).encode()).hexdigest()
    return {'connected':snapshot.get('connected',False),'tabs':tabs,'revision':revision,'more':max(0,total-len(tabs))}

def send_continue(scope,data):
    if not isinstance(data,dict) or data.get('confirmed') is not True or not re.fullmatch('[a-f0-9]{32}',str(data.get('requestId',''))):
        return 400,{'ok':False,'state':'invalid'}
    return scope.send({'threadId':scope.thread_id,'requestId':'continue-'+data['requestId'],'message':'继续','confirmed':True})

def start_device_server(bridge):
    config_path=ROOT/'device.local.json'
    if not config_path.exists():return None
    config=json.loads(config_path.read_text());jobs=VoiceJobs()
    class Handler(BaseHTTPRequestHandler):
        def setup(self):
            # TLS negotiation must happen in the request worker, not in accept().
            # A board reboot mid-handshake must not block all later connections.
            self.request.settimeout(15)
            if isinstance(self.request,ssl.SSLSocket):self.request.do_handshake()
            super().setup()
        def log_message(self,*args):pass
        def reply(self,status,obj):
            raw=json.dumps(obj,ensure_ascii=False).encode()
            self.send_response(status);self.send_header('Content-Type','application/json; charset=utf-8')
            self.send_header('Content-Length',str(len(raw)));self.send_header('Cache-Control','no-store')
            self.end_headers();self.wfile.write(raw)
        def auth(self):return hmac.compare_digest(self.headers.get('Authorization',''),'Bearer '+config['token'])
        def context(self,part):
            ident=parse_qs(part.query).get('thread',[THREAD])[0]
            if hasattr(bridge,'catalog'):return bridge.catalog.context(ident,jobs)
            if ident!=THREAD:raise ValueError('Unbound chat')
            return bridge,jobs
        def do_GET(self):
            if not self.auth():return self.reply(401,{'ok':False})
            part=urlsplit(self.path)
            if part.path=='/device/monitor/metrics':
                status,data=forward_monitor('GET','/api/metrics');return self.reply(status,data)
            if part.path=='/device/time':return self.reply(200,{'epoch':int(time.time()),'usage':bridge.usage.snapshot() if hasattr(bridge,'usage') else {}})
            if part.path=='/device/usage':return self.reply(200,{'usage':bridge.usage.snapshot() if hasattr(bridge,'usage') else {}})
            if part.path=='/device/tasks':return self.reply(200,{**bridge.catalog.snapshot,'usage':bridge.usage.snapshot() if hasattr(bridge,'usage') else {}})
            if part.path=='/device/tabs':
                ident=parse_qs(part.query).get('thread',[THREAD])[0]
                snapshot=bridge.catalog.snapshot
                if ident not in bridge.catalog.records:return self.reply(403,{'ok':False})
                return self.reply(200,chat_tabs(snapshot,ident))
            try:scope,voice=self.context(part)
            except ValueError:return self.reply(403,{'ok':False,'error':'目标聊天不在当前本机任务目录'})
            if part.path=='/device/questions':
                return self.reply(200,scope.questions.snapshot if scope.questions else {'available':False,'questions':[]})
            if part.path=='/device/answer':
                ident=parse_qs(part.query).get('id',[''])[0]
                if not re.fullmatch('[a-f0-9]{32}',ident):return self.reply(400,{'ok':False})
                return self.reply(200,scope.questions.result(ident) if scope.questions else {'state':'unavailable'})
            if part.path=='/device/control':
                ident=parse_qs(part.query).get('id',[''])[0]
                if not re.fullmatch('[a-f0-9]{32}',ident):return self.reply(400,{'ok':False})
                return self.reply(200,scope.control_result(ident))
            if part.path=='/device/state':
                state=compact(scope.snapshot);state['usage']=bridge.usage.snapshot() if hasattr(bridge,'usage') else {};job=voice.latest()
                if job:
                    digest=hashlib.sha256(job.get('editedText',job.get('text','')).encode()).hexdigest()
                    with scope.lock:sent=scope.db.execute('SELECT state FROM requests WHERE (id=? OR id=?) AND digest=? ORDER BY created DESC LIMIT 1',('board-'+job['id'],'board-'+job['id']+'-'+digest[:16],digest)).fetchone()
                    if not sent or sent[0]!='accepted':state['draftId']=job['id']
                return self.reply(200,state)
            if part.path=='/device/job':
                try:
                    ident=parse_qs(part.query).get('id',[''])[0];job=voice.read(ident)
                    message=job.get('editedText',job.get('text',''))
                    return self.reply(200,{**job,'text':message,**scope.delivery_result(ident,message)})
                except ValueError:return self.reply(400,{'state':'failed'})
            return self.reply(404,{'ok':False})
        def do_POST(self):
            if not self.auth():return self.reply(401,{'ok':False})
            try:
                size=int(self.headers.get('Content-Length','0'))
                part=urlsplit(self.path)
                limit=VOICE_MAX_BYTES if part.path=='/device/audio' else 40000
                if not 0<size<=limit:return self.reply(413,{'ok':False})
                self.connection.settimeout(15);raw=self.rfile.read(size)
                if len(raw)!=size:raise ValueError('incomplete body')
                if part.path=='/device/monitor/control':
                    status,data=forward_monitor('POST','/api/control',raw);return self.reply(status,data)
                scope,voice=self.context(part)
                if part.path=='/device/select':
                    if scope.questions:scope.questions.refresh()
                    return self.reply(202,{'ok':True,'threadId':scope.thread_id})
                if part.path=='/device/questions/refresh':
                    if scope.questions:scope.questions.refresh()
                    return self.reply(202,{'ok':bool(scope.questions)})
                if part.path=='/device/answer':
                    data=json.loads(raw)
                    if not isinstance(data,dict):raise ValueError('invalid answer')
                    status,result=scope.questions.submit(data) if scope.questions else (503,{'ok':False,'state':'unavailable'})
                    return self.reply(status,result)
                if part.path=='/device/stop':
                    data=json.loads(raw)
                    if not isinstance(data,dict):raise ValueError('invalid request')
                    status,result=scope.stop_task(data)
                    return self.reply(status,result)
                if part.path=='/device/continue':
                    # A literal message, isolated from voice jobs and unsent drafts. Existing send() provides deduplication.
                    status,result=send_continue(scope,json.loads(raw))
                    return self.reply(status,result)
                if part.path=='/device/audio':
                    ident=parse_qs(part.query).get('id',[''])[0]
                    return self.reply(202,voice.submit(ident,raw))
                if part.path=='/device/draft':
                    data=json.loads(raw);job=voice.draft(data.get('id',''),data.get('text'))
                    return self.reply(200,{'ok':True,'state':job['state']})
                if part.path=='/device/send':
                    data=json.loads(raw);ident=data.get('id','');job=voice.read(ident)
                    if data.get('confirmed') is not True:raise ValueError('draft not confirmed')
                    if 'text' in data:job=voice.draft(ident,data['text'])
                    if job.get('state')!='ready' or data.get('confirmed') is not True:raise ValueError('draft not confirmed')
                    message=job.get('editedText',job.get('text',''))
                    with scope.lock:legacy=scope.db.execute('SELECT state,digest FROM requests WHERE id=?',('board-'+ident,)).fetchone()
                    if legacy and legacy==('accepted',hashlib.sha256(message.encode()).hexdigest()):return self.reply(200,{'ok':True,'state':'accepted','duplicate':True})
                    request_id='board-'+ident+'-'+hashlib.sha256(message.encode()).hexdigest()[:16]
                    status,result=scope.send({'threadId':scope.thread_id,'requestId':request_id,'message':message,'confirmed':True})
                    return self.reply(status,result)
                return self.reply(404,{'ok':False})
            except (ValueError,OSError,wave.Error,EOFError):return self.reply(400,{'ok':False,'state':'failed','error':'无效录音或请求，请重试录音。'})
    server=ThreadingHTTPServer((config['bind'],config['port']),Handler)
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(ROOT/'device-cert.pem',ROOT/'device-key.pem')
    server.socket=context.wrap_socket(server.socket,server_side=True,do_handshake_on_connect=False)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    return server
