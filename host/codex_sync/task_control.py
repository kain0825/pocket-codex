"""Single-chat stop/steer adapter. UI invocation is followed by desktop turn verification.

No prompt injection into a second inference process, no private database writes,
no guessed screen coordinates, and no automatic replay of uncertain writes.
"""
import json
from pathlib import Path
import subprocess
import threading
import time
from desktop_client import THREAD

TITLE='ESP32-P4 + ESP32-C6 屏幕开发板开发'
ROOT=Path(__file__).resolve().parent

UI_LOCK=threading.Lock()

def active_turn(data,thread_id=THREAD):
    if data.get('thread',{}).get('id')!=thread_id:raise ValueError('聊天不匹配')
    turns=[t for t in data.get('turns',[]) if t.get('status')=='inProgress']
    return turns[0]['id'] if len(turns)==1 else ''

def contains_message(data,text,previous_ids=()):
    canonical=lambda s:' '.join(s.split())
    for turn in data.get('turns',[]):
        for item in turn.get('items',[]):
            if item.get('id') in previous_ids:continue
            if item.get('type')=='userMessage' and item.get('id') not in previous_ids:
                actual='\n'.join(c.get('text','') for c in item.get('content',[]) if c.get('type')=='text')
                if canonical(actual)==canonical(text):return turn['id']
            if item.get('type')=='functionCallOutput' and item.get('namespace')=='codex_app' and item.get('name')=='send_message_to_thread':
                output=item.get('output',{})
                raw=output.get('text','')
                prefix='<codex_delegation>\n  <source_thread_id>'+THREAD+'</source_thread_id>\n  <input>'
                suffix='</input>\n</codex_delegation>'
                if not output.get('truncated') and raw.startswith(prefix) and raw.endswith(suffix):
                    if canonical(raw[len(prefix):-len(suffix)])==canonical(text):return turn['id']
    return ''

class TaskController:
    def __init__(self,client,runner=None,title=TITLE):
        self.client=client;self.thread_id=getattr(client,'thread_id',THREAD);self.title=title;self.lock=UI_LOCK;self.runner=runner or self._run

    def _run(self,action,text='',**extra):
        raw=json.dumps({'action':action,'title':self.title,'threadId':self.thread_id,'text':text,**extra},ensure_ascii=False)+'\n'
        result=subprocess.run([str(ROOT/'TaskControl.exe')],input=raw,capture_output=True,
            text=True,encoding='utf-8',timeout=6,creationflags=subprocess.CREATE_NO_WINDOW)
        return json.loads(result.stdout.lstrip('\ufeff'))

    def navigate(self):
        return self.client.call('navigate_to_codex_page',{'threadId':self.thread_id})

    def stop(self,expected_turn):
        with self.lock:
            before=self.client.read()
            if not expected_turn or active_turn(before,self.thread_id)!=expected_turn:
                return {'ok':False,'state':'stale','detail':'任务已变化或已经结束，没有停止其他任务。'}
            self.navigate()
            if active_turn(self.client.read(),self.thread_id)!=expected_turn:
                return {'ok':False,'state':'stale','detail':'任务已变化，没有执行停止。'}
            result=self.runner('stop')
            if not result.get('ok'):
                return {'ok':False,'state':result.get('state','unknown'),'detail':'停止未确认，请查看电脑。','error':result.get('error','')}
            # Wait for actual turn termination, never treat a click as a completed interrupt.
            for _ in range(6):
                current=self.client.read()
                turn=next((t for t in current.get('turns',[]) if t.get('id')==expected_turn),{})
                status=turn.get('status')
                if status=='interrupted':return {'ok':True,'state':'interrupted','detail':'当前任务已停止。'}
                if status in ('completed','failed'):return {'ok':True,'state':'finished','detail':'当前任务已经结束。'}
                time.sleep(.3)
            return {'ok':False,'state':'unknown','detail':'停止请求已发出，结束状态尚未确认；不会自动重试。'}

    def verify_delivery(self,text,expected_turn,previous_ids=()):
        # The installed desktop sends a live codex_app delegation into an active
        # turn. Verify that actual receipt; no queue-button click is needed.
        with self.lock:
            for _ in range(6):
                data=self.client.read_delivery()
                found=contains_message(data,text,previous_ids)
                if found:return {'delivery':'steered' if found==expected_turn else 'started','detail':'消息已进入当前执行任务。'}
                if active_turn(data,self.thread_id)!=expected_turn:
                    return {'delivery':'unknown','detail':'消息已提交，原任务已结束，等待新一轮接收确认。'}
                time.sleep(.25)
            return {'delivery':'unknown','detail':'消息已提交，等待当前任务接收确认；不会重复发送。'}
