"""Loopback-only, single-chat prototype. No firmware changes or auto-start."""
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import threading
import time
import logging
from logging.handlers import RotatingFileHandler
from desktop_client import DesktopClient, THREAD
from task_control import TaskController, active_turn, contains_message

ROOT=Path(__file__).resolve().parent
PORT=8790
ORIGIN=f'http://127.0.0.1:{PORT}'

def normalize(data,thread_id=THREAD):
    thread=data.get('thread',{})
    if thread.get('id')!=thread_id: raise RuntimeError('接口返回的聊天与绑定不一致。')
    messages=[]
    # Desktop API returns turns newest-first; items within a turn are ordered.
    for turn in reversed(data.get('turns',[])):
        for item in turn.get('items',[]):
            typ=item.get('type')
            if typ=='userMessage':
                text='\n'.join(x.get('text','') for x in item.get('content',[]) if x.get('type')=='text')
                role='user'
            elif typ=='agentMessage':
                text=item.get('text',''); role='assistant'
            else: continue
            if not text.strip(): continue
            messages.append({'id':item.get('id'),'turnId':turn.get('id'),
                'role':role,'text':text,'phase':item.get('phase')})
    result={'threadId':thread_id,'title':thread.get('title','开发板'),
        'status':thread.get('status',{}).get('type','unknown'),
        'messages':messages,'hasOlder':data.get('page',{}).get('hasMore',False)}
    result['activeTurnId']=active_turn(data,thread_id)
    result['revision']=hashlib.sha256(json.dumps(result,ensure_ascii=False,sort_keys=True).encode()).hexdigest()
    return result

class Bridge:
    def __init__(self,client,token,db_path,controller=None):
        self.client=client; self.token=token; self.lock=threading.Lock()
        self.controller=controller
        self.thread_id=getattr(client,'thread_id',THREAD);self.last_access=time.monotonic();self.managed=False
        self.db=sqlite3.connect(db_path,check_same_thread=False)
        self.db.execute('CREATE TABLE IF NOT EXISTS requests (id TEXT PRIMARY KEY, digest TEXT, state TEXT, detail TEXT)')
        columns={row[1] for row in self.db.execute('PRAGMA table_info(requests)')}
        if 'message' not in columns:self.db.execute("ALTER TABLE requests ADD COLUMN message TEXT DEFAULT ''")
        if 'created' not in columns:self.db.execute('ALTER TABLE requests ADD COLUMN created REAL DEFAULT 0')
        if 'delivery' not in columns:self.db.execute("ALTER TABLE requests ADD COLUMN delivery TEXT DEFAULT ''")
        if 'expected_turn' not in columns:self.db.execute("ALTER TABLE requests ADD COLUMN expected_turn TEXT DEFAULT ''")
        if 'baseline' not in columns:self.db.execute("ALTER TABLE requests ADD COLUMN baseline TEXT DEFAULT '[]'")
        self.db.execute('CREATE TABLE IF NOT EXISTS controls (id TEXT PRIMARY KEY, turn_id TEXT, state TEXT, detail TEXT, created REAL)')
        self.db.execute("UPDATE controls SET state='unknown',detail='服务重启，停止结果待确认；不会重发。' WHERE state='pending'")
        # A crash during submission is uncertain; never replay such requests.
        self.db.execute("UPDATE requests SET state='unknown',detail='连接器在提交期间重启；请检查原聊天，未自动重发。' WHERE state='dispatching'")
        self.db.commit()
        self.snapshot={'connected':False,'error':'正在连接桌面…','threadId':self.thread_id}
        self.running=True
        from questions import Questions
        self.questions=Questions(self,controller) if controller else None

    def poll(self):
        while self.running:
            if self.managed and time.monotonic()-self.last_access>15:
                time.sleep(2);continue
            try:
                value=normalize(self.client.read(),self.thread_id)
                value.update(connected=True,observedAt=time.time())
                self.snapshot=value
                self.refresh_deliveries()
            except Exception as error:
                self.snapshot={**self.snapshot,'connected':False,'error':str(error),'failedAt':time.time()}
            time.sleep(2)

    def send(self,body):
        request_id=body.get('requestId',''); message=body.get('message','')
        if body.get('threadId')!=self.thread_id or body.get('confirmed') is not True:
            return 400,{'ok':False,'error':'请明确确认发送到绑定的开发板聊天。'}
        if not re.fullmatch(r'[A-Za-z0-9_-]{8,80}',str(request_id)) or not isinstance(message,str) or not message.strip() or len(message)>8000:
            return 400,{'ok':False,'error':'消息或请求编号无效。'}
        digest=hashlib.sha256(message.encode()).hexdigest()
        with self.lock:
            old=self.db.execute('SELECT digest,state,detail FROM requests WHERE id=?',(request_id,)).fetchone()
            if old:
                if old[0]!=digest:return 409,{'ok':False,'error':'同一请求编号不能用于不同内容。'}
                delivery=self.db.execute('SELECT delivery FROM requests WHERE id=?',(request_id,)).fetchone()[0]
                return 200,{'ok':old[1]=='accepted','state':old[1],'detail':old[2],'delivery':delivery,'duplicate':True}
            if not self.snapshot.get('connected'):
                return 503,{'ok':False,'error':'桌面未连接；没有发送。'}
            self.db.execute('INSERT INTO requests (id,digest,state,detail,message,created) VALUES (?,?,?,?,?,?)',(request_id,digest,'dispatching','',message,time.time()))
            self.db.commit()
        delivery='';submitted=False
        try:
            before=self.client.read() if self.controller else {}
            expected=active_turn(before,self.thread_id) if before else ''
            previous_ids={i.get('id') for t in before.get('turns',[]) for i in t.get('items',[])}
            with self.lock:
                self.db.execute('UPDATE requests SET expected_turn=?,baseline=? WHERE id=?',(expected,json.dumps(list(previous_ids)),request_id));self.db.commit()
            result=self.client.call('send_message_to_thread',{'threadId':self.thread_id,'prompt':message})
            if result.get('threadId')!=self.thread_id: raise RuntimeError('提交结果未确认目标聊天。')
            submitted=True
            state='accepted'; detail='已作为外部终端消息送入当前聊天，等待 Codex 回复。'
            delivery='submitted' if self.controller else 'started'
            detail='已提交，正在确认是否送达当前任务。' if self.controller else detail
        except Exception as error:
            state='accepted' if submitted else 'unknown'
            delivery='unknown';detail='消息已提交，引导待确认；不会重复发送。' if submitted else f'提交结果未知，请先检查原聊天，未自动重发。{error}'
        with self.lock:
            self.db.execute('UPDATE requests SET state=?,detail=?,delivery=? WHERE id=?',(state,detail,delivery,request_id)); self.db.commit()
        return (202 if state=='accepted' else 502),{'ok':state=='accepted','state':state,'detail':detail,'delivery':delivery,'requestId':request_id}

    def refresh_deliveries(self):
        if not self.controller:return
        with self.lock:
            rows=self.db.execute("SELECT id,message,expected_turn,baseline FROM requests WHERE state='accepted' AND delivery IN ('submitted','unknown') AND created>? ORDER BY created DESC LIMIT 5",(time.time()-300,)).fetchall()
        if not rows:return
        data=self.client.read_delivery()
        for ident,message,expected,baseline in rows:
            found=contains_message(data,message,set(json.loads(baseline)))
            if not found:continue
            delivery='steered' if expected and found==expected else 'started'
            detail='新要求已送入正在执行的任务。' if delivery=='steered' else '消息已进入新一轮任务。'
            with self.lock:
                self.db.execute('UPDATE requests SET delivery=?,detail=? WHERE id=?',(delivery,detail,ident));self.db.commit()

    def delivery_result(self,ident,text):
        digest=hashlib.sha256(text.encode()).hexdigest();request_id='board-'+ident+'-'+digest[:16]
        with self.lock:row=self.db.execute('SELECT state,delivery,detail FROM requests WHERE id=?',(request_id,)).fetchone()
        return dict(zip(('sendState','delivery','detail'),row)) if row else {}

    def stop_task(self,body):
        ident=body.get('requestId','');turn=body.get('expectedTurnId','')
        if body.get('confirmed') is not True or not re.fullmatch(r'[A-Za-z0-9_-]{8,80}',str(ident)) or not re.fullmatch(r'[a-f0-9-]{36}',str(turn)):
            return 400,{'ok':False,'state':'invalid'}
        if not self.controller:return 503,{'ok':False,'state':'unavailable'}
        with self.lock:
            old=self.db.execute('SELECT turn_id,state,detail FROM controls WHERE id=?',(ident,)).fetchone()
            if old:
                if old[0]!=turn:return 409,{'ok':False,'state':'conflict'}
                return 200,{'ok':old[1] in ('pending','interrupted','finished'),'state':old[1],'detail':old[2],'duplicate':True}
            if not self.snapshot.get('connected') or self.snapshot.get('activeTurnId')!=turn:
                return 200,{'ok':False,'state':'stale','detail':'任务已变化或结束，请刷新后再操作。'}
            self.db.execute('INSERT INTO controls VALUES (?,?,?,?,?)',(ident,turn,'pending','正在停止当前任务…',time.time()));self.db.commit()
        threading.Thread(target=self._stop_worker,args=(ident,turn),daemon=True).start()
        return 202,{'ok':True,'state':'pending','detail':'正在停止当前任务…'}

    def _stop_worker(self,ident,turn):
        try:result=self.controller.stop(turn)
        except Exception:result={'state':'unknown','detail':'停止结果未确认，请检查电脑；不会自动重试。'}
        with self.lock:
            self.db.execute('UPDATE controls SET state=?,detail=? WHERE id=?',(result['state'],result['detail'],ident));self.db.commit()

    def control_result(self,ident):
        with self.lock:row=self.db.execute('SELECT state,detail FROM controls WHERE id=?',(ident,)).fetchone()
        return dict(zip(('state','detail'),row)) if row else {'state':'missing','detail':'未找到停止请求'}

    def outbox(self):
        with self.lock:
            rows=self.db.execute('SELECT id,state,message,created FROM requests ORDER BY created DESC LIMIT 4').fetchall()
        return [dict(zip(('id','state','message','created'),row)) for row in rows]

def create_server(bridge,port=PORT):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args): pass
        def reply(self,status,value,content_type='application/json; charset=utf-8'):
            raw=value if isinstance(value,bytes) else json.dumps(value,ensure_ascii=False).encode()
            self.send_response(status)
            self.send_header('Content-Type',content_type); self.send_header('Content-Length',str(len(raw)))
            self.send_header('Cache-Control','no-store'); self.send_header('X-Content-Type-Options','nosniff')
            self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'")
            self.end_headers(); self.wfile.write(raw)
        def valid_host(self):
            return self.headers.get('Host')==f'127.0.0.1:{self.server.server_port}'
        def authorized(self):
            return hmac.compare_digest(self.headers.get('Authorization',''),f'Bearer {bridge.token}')
        def do_GET(self):
            if not self.valid_host():return self.reply(403,{'ok':False})
            if self.path=='/health':
                return self.reply(200,{'ok':True,'service':'pocket-codex-sync','version':56,'voiceTextScript':'Hans','pid':os.getpid(),'desktopConnected':bool(bridge.snapshot.get('connected')),'desktopGeneration':getattr(bridge.client,'reconnections',0),'observedAt':bridge.snapshot.get('observedAt')})
            assets={'/':'index.html','/app.js':'app.js','/style.css':'style.css'}
            if self.path in assets:
                file=ROOT/assets[self.path]
                mime={'html':'text/html','js':'text/javascript','css':'text/css'}[file.suffix[1:]]
                return self.reply(200,file.read_bytes(),mime+'; charset=utf-8')
            if not self.authorized(): return self.reply(401,{'ok':False})
            if self.path=='/api/chat': return self.reply(200,{**bridge.snapshot,'outbox':bridge.outbox()})
            return self.reply(404,{'ok':False})
        def do_POST(self):
            origin=f'http://127.0.0.1:{self.server.server_port}'
            if not self.valid_host() or not self.authorized() or self.headers.get('Origin')!=origin:
                return self.reply(403,{'ok':False})
            if self.path!='/api/send':return self.reply(404,{'ok':False})
            try:
                length=int(self.headers.get('Content-Length','0'))
                if not 0<length<=40000: return self.reply(413,{'ok':False})
                self.connection.settimeout(5)
                body=json.loads(self.rfile.read(length))
                if not isinstance(body,dict):raise ValueError()
            except (ValueError,OSError):return self.reply(400,{'ok':False})
            status,result=bridge.send(body); self.reply(status,result)
    return ThreadingHTTPServer(('127.0.0.1',port),Handler)

def main():
    if not THREAD:
        raise RuntimeError('Run scripts/setup.py and configure a local Codex thread first.')
    config_path=ROOT/'connection.local.json'
    if config_path.exists(): config=json.loads(config_path.read_text(encoding='utf-8'))
    else: config={'token':secrets.token_urlsafe(32)}
    config_path.write_text(json.dumps(config),encoding='utf-8')
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(levelname)s %(message)s',handlers=[RotatingFileHandler(ROOT/'connection-events.log',maxBytes=1048576,backupCount=3,encoding='utf-8')])
    logging.info('Service starting pid=%s',os.getpid())
    client=DesktopClient()
    bridge=Bridge(client,config['token'],ROOT/'requests.sqlite',TaskController(client))
    from usage_limits import UsageCache
    bridge.usage=UsageCache();bridge.usage.start()
    from task_catalog import TaskCatalog
    bridge.catalog=TaskCatalog(bridge,client,ROOT)
    server=create_server(bridge)
    from device_api import start_device_server
    device_server=start_device_server(bridge)
    threading.Thread(target=bridge.poll,daemon=True).start()
    if bridge.questions:threading.Thread(target=bridge.questions.loop,daemon=True).start()
    (ROOT/'runtime.json').write_text(json.dumps({'pid':os.getpid(),'port':PORT,'threadId':THREAD,'startedAt':time.time()}),encoding='utf-8')
    logging.info('HTTP and paired TLS listeners ready pid=%s',os.getpid())
    try:server.serve_forever()
    finally:
        logging.info('Service stopping pid=%s',os.getpid())
        bridge.running=False;bridge.usage.running=False;server.server_close();device_server.shutdown();device_server.server_close();client.close();bridge.usage.client.close()

if __name__=='__main__':main()
