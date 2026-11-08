"""Local MCP adapter to the installed Codex desktop tools.

Uses the bundled MCP server without patching it. This is a desktop-version-
dependent integration, not a public standalone Codex API. Never launches a
second inference engine, resumes a thread, or writes Codex databases.
"""
import json
import os
import shutil
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
import logging

from local_settings import setting
THREAD = setting('thread_id', '')
NODE = os.environ.get('POCKET_NODE') or shutil.which('node') or 'node'
PLUGIN_ROOT = Path.home()/'.codex/plugins/cache/openai-bundled/codex-app-tools'

class DesktopClient:
    def __init__(self):
        self.thread_id=THREAD;self.allowed_threads={THREAD}
        self.lock=threading.RLock();self.counter=0
        self.proc=None;self.tools={};self.pipe=None;self.reconnections=0;self.caller_turn=None

    @staticmethod
    def discover_pipe():
        prefix='\\\\.\\pipe\\'
        candidates=[prefix+name for name in os.listdir(prefix) if name.startswith('codex-app-tools-')]
        if len(candidates)==1:return candidates[0]
        selected=os.environ.get('CODEX_APP_TOOLS_PIPE_PATH')
        if selected in candidates:return selected
        raise RuntimeError(f'Codex 桌面连接尚未就绪，或无法唯一选择连接（{len(candidates)} 个）。')

    def _connect(self):
        self._disconnect()
        pipe=self.discover_pipe()
        versions = sorted(p for p in PLUGIN_ROOT.iterdir() if (p/'server.mjs').is_file())
        if not versions:
            raise RuntimeError('没有找到已安装的 Codex 桌面工具插件。')
        env={**os.environ,'CODEX_APP_TOOLS_PIPE_PATH':pipe}
        self.proc = subprocess.Popen([NODE,str(versions[-1]/'server.mjs')],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,
            text=True,encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW,env=env)
        self.queue = queue.Queue()
        threading.Thread(target=self._read,args=(self.proc,self.queue),daemon=True).start()
        self.rpc('initialize',{'protocolVersion':'2024-11-05','capabilities':{},
            'clientInfo':{'name':'pocket-codex-sync','version':'0.1.0'}})
        self._write({'jsonrpc':'2.0','method':'notifications/initialized'})
        self.tools = {t['name']:t for t in self.rpc('tools/list',{}).get('tools',[])}
        self.pipe=pipe;self.reconnections+=1
        logging.info('Desktop transport connected (generation=%s)',self.reconnections)

    def _read(self,proc,messages):
        try:
            for line in proc.stdout:
                try:messages.put(json.loads(line))
                except ValueError:pass
        except (OSError,ValueError):pass
        finally:messages.put({'closed':True})

    def _write(self,obj):
        self.proc.stdin.write(json.dumps(obj,ensure_ascii=False)+'\n')
        self.proc.stdin.flush()

    def rpc(self,method,params):
        with self.lock:
            self.counter += 1
            ident = self.counter
            self._write({'jsonrpc':'2.0','id':ident,'method':method,'params':params})
            deadline = time.monotonic()+25
            while time.monotonic()<deadline:
                try: message=self.queue.get(timeout=1)
                except queue.Empty: continue
                if message.get('closed'): raise RuntimeError('桌面连接已关闭。')
                if message.get('id') != ident: continue
                if 'error' in message: raise RuntimeError(message['error'].get('message','MCP error'))
                return message.get('result',{})
            raise TimeoutError('桌面接口超时；未自动重试写操作。')

    def call(self,name,args):
        if name not in ('read_thread','send_message_to_thread','navigate_to_codex_page','list_threads','list_projects','wait_threads','get_usage_limits'):
            raise ValueError('工具不在连接器白名单中。')
        if name=='wait_threads':
            targets=args.get('targets',[])
            if args.get('timeoutMs')!=0 or not 1<=len(targets)<=8 or any(t.get('threadId') not in self.allowed_threads or t.get('threadId')==THREAD or t.get('hostId','local')!='local' for t in targets):
                raise ValueError('仅允许读取其他已列出本机聊天的即时状态。')
        elif name not in ('list_threads','list_projects','get_usage_limits') and args.get('threadId') not in self.allowed_threads:
            raise ValueError('目标聊天不在本机任务目录中。')
        # Reads can be repeated after a broken pipe. Writes have exactly one dispatch
        # attempt: their outcome may be unknown even when the connection fails.
        attempts=2 if name in ('read_thread','list_threads','list_projects','wait_threads','get_usage_limits') else 1
        with self.lock:
            for attempt in range(attempts):
                try:
                    if self.proc is None or self.proc.poll() is not None:self._connect()
                    if name not in self.tools:raise RuntimeError('当前桌面插件缺少所需接口。')
                    if name=='wait_threads' and not self.caller_turn:
                        self.call('read_thread',{'threadId':THREAD,'turnLimit':1,'includeOutputs':False})
                    meta={'codexThreadId':THREAD} if THREAD else {}
                    if name=='wait_threads' and self.caller_turn:meta['codexTurnId']=self.caller_turn
                    result=self.rpc('tools/call',{'name':name,'arguments':args,'_meta':meta})
                    if result.get('isError'):
                        raise RuntimeError('; '.join(i.get('text','') for i in result.get('content',[])))
                    break
                except Exception as exc:
                    self._disconnect()
                    logging.warning('Desktop operation failed: tool=%s error=%s retry_read=%s',name,type(exc).__name__,attempt+1<attempts)
                    if attempt+1==attempts:raise
        texts = [i['text'] for i in result.get('content',[]) if i.get('type')=='text']
        if len(texts)==1:
            try:
                value=json.loads(texts[0])
                if name=='read_thread' and args.get('threadId')==THREAD and value.get('turns'):self.caller_turn=value['turns'][0]['id']
                return value
            except ValueError: pass
        return {'text':'\n'.join(texts)}

    def read(self):
        return self.call('read_thread',{'threadId':THREAD,'turnLimit':6,
            'includeOutputs':False,'maxOutputCharsPerItem':12000})

    def read_delivery(self):
        return self.call('read_thread',{'threadId':THREAD,'turnLimit':1,
            'includeOutputs':True,'maxOutputCharsPerItem':18000})

    def _disconnect(self):
        proc=self.proc;self.proc=None;self.tools={}
        if proc is None:return
        try:
            proc.stdin.close()
            try:proc.wait(timeout=1)
            except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=3)
        except OSError:pass
        finally:
            if proc.stdout:proc.stdout.close()

    def close(self):
        with self.lock:self._disconnect()

class ScopedClient:
    def __init__(self,base,thread_id):
        if thread_id not in base.allowed_threads:raise ValueError('Unknown thread')
        self.base=base;self.thread_id=thread_id
    def call(self,name,args):
        if args.get('threadId')!=self.thread_id:raise ValueError('Wrong chat target')
        return self.base.call(name,args)
    def read(self):
        return self.call('read_thread',{'threadId':self.thread_id,'turnLimit':6,'includeOutputs':False,'maxOutputCharsPerItem':12000})
    def read_delivery(self):
        return self.call('read_thread',{'threadId':self.thread_id,'turnLimit':1,'includeOutputs':True,'maxOutputCharsPerItem':18000})

if __name__=='__main__':
    client=DesktopClient()
    try:
        data=client.read()
        print(json.dumps({'ok':True,'threadId':data.get('thread',{}).get('id'),
            'status':data.get('thread',{}).get('status'),
            'turns':len(data.get('turns',[])),
            'sendAvailable':'send_message_to_thread' in client.tools},ensure_ascii=False))
    finally: client.close()
