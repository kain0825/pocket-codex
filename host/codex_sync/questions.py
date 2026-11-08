"""Read visible native question cards; submit only the explicitly chosen answer.

This adapter does not handle system permission/command approval dialogs.
"""
import hashlib,json,re,threading,time,logging

def answer_receipt(data,title,answer,baseline):
    canonical=lambda s:' '.join(s.split())
    for turn in data.get('turns',[]):
        for item in turn.get('items',[]):
            if item.get('type')!='userMessage' or item.get('id') in baseline:continue
            raw='\n'.join(c.get('text','') for c in item.get('content',[]) if c.get('type')=='text').strip()
            prefix='<send_user_message_question_reply>';suffix='</send_user_message_question_reply>'
            if not (raw.startswith(prefix) and raw.endswith(suffix)):continue
            try:values=json.loads(raw[len(prefix):-len(suffix)].strip())
            except ValueError:continue
            if not isinstance(values,list):continue
            for value in values:
                if isinstance(value,dict) and canonical(value.get('question',''))==canonical(title) and canonical(value.get('answer',''))==canonical(answer):
                    return value.get('questionItemId','')
    return ''

class Questions:
    def __init__(self,bridge,controller):
        self.bridge=bridge;self.controller=controller
        self.snapshot={'available':False,'questions':[],'detail':'正在检查待回答问题'}
        self.refresh_lock=threading.Lock()
        with bridge.lock:
            bridge.db.execute('CREATE TABLE IF NOT EXISTS question_answers (id TEXT PRIMARY KEY, question_id TEXT, digest TEXT, title TEXT, answer TEXT, state TEXT, detail TEXT, baseline TEXT, created REAL)')
            bridge.db.execute("UPDATE question_answers SET state='unknown',detail='服务重启，回答结果待确认，不会重复提交' WHERE state IN ('pending','submitted')")
            bridge.db.commit()

    def scan(self,navigate=False):
        if not self.refresh_lock.acquire(blocking=False):return
        try:
            with self.controller.lock:
                if navigate:self.controller.navigate()
                result=self.controller._run('questions')
            self.snapshot={'available':bool(result.get('ok')),'questions':result.get('questions',[]) if result.get('ok') else [],'observedAt':time.monotonic(),
                'detail':'已同步待回答问题' if result.get('ok') else '电脑未打开绑定聊天，点击状态栏可重新同步'}
        except Exception:self.snapshot={'available':False,'questions':[],'detail':'问题同步暂不可用，请检查电脑'}
        finally:self.refresh_lock.release()

    def loop(self):
        while self.bridge.running:
            if self.bridge.managed and time.monotonic()-self.bridge.last_access>15:
                time.sleep(3);continue
            self.scan()
            try:self.verify()
            except Exception:pass
            time.sleep(3)

    def refresh(self):
        threading.Thread(target=self.scan,kwargs={'navigate':True},daemon=True).start()

    def submit(self,body):
        ident=body.get('requestId','');qid=body.get('questionId','');answer=body.get('answer','')
        if body.get('confirmed') is not True or not re.fullmatch('[a-f0-9]{32}',str(ident)) or not re.fullmatch('[a-f0-9]{32}',str(qid)) or not isinstance(answer,str) or not answer.strip() or len(answer)>2000:
            return 400,{'ok':False,'state':'invalid','detail':'无效回答'}
        digest=hashlib.sha256((qid+'\n'+answer).encode()).hexdigest();b=self.bridge
        with b.lock:
            old=b.db.execute('SELECT digest,state,detail FROM question_answers WHERE id=?',(ident,)).fetchone()
            if old:
                if old[0]!=digest:return 409,{'ok':False,'state':'conflict','detail':'请求内容冲突'}
                return 200,{'ok':old[1] in ('pending','submitted','answered'),'state':old[1],'detail':old[2],'requestId':ident,'duplicate':True}
            existing=b.db.execute("SELECT id,state,detail FROM question_answers WHERE question_id=? AND state IN ('pending','submitted','answered','unknown') ORDER BY created DESC LIMIT 1",(qid,)).fetchone()
            if existing:return 200,{'ok':False,'state':existing[1],'detail':'这张问题已有提交记录，请核对电脑','requestId':existing[0],'duplicate':True}
            question=next((q for q in self.snapshot['questions'] if q['id']==qid),None)
            if not self.snapshot['available'] or not question:return 200,{'ok':False,'state':'stale','detail':'问题已变化，请重新打开'}
            if answer not in question['options'] and not question['canWrite']:return 400,{'ok':False,'state':'invalid','detail':'这张问题不支持自定义回答'}
            b.db.execute('INSERT INTO question_answers VALUES (?,?,?,?,?,?,?,?,?)',(ident,qid,digest,question['title'],answer,'pending','正在提交回答…','[]',time.time()));b.db.commit()
        threading.Thread(target=self._submit,args=(ident,qid,answer),daemon=True).start()
        return 202,{'ok':True,'state':'pending','requestId':ident,'detail':'正在提交回答…'}

    def _submit(self,ident,qid,answer):
        b=self.bridge
        try:
            with self.controller.lock:
                before=b.client.read();baseline=[i.get('id') for t in before.get('turns',[]) for i in t.get('items',[])]
                with b.lock:b.db.execute('UPDATE question_answers SET baseline=? WHERE id=?',(json.dumps(baseline),ident));b.db.commit()
                self.controller.navigate()
                result=self.controller._run('answer',questionId=qid,answer=answer)
                logging.info('Question native submit request=%s question=%s result=%s',ident,qid,json.dumps(result,ensure_ascii=False))
            state='submitted' if result.get('ok') else 'unknown' if result.get('state')=='unknown' else 'failed'
            detail='回答已提交，等待聊天确认' if state=='submitted' else '提交未确认，请检查电脑；不会自动重发'
        except Exception:state='unknown';detail='回答结果未确认，请检查电脑；不会重复提交'
        with b.lock:b.db.execute("UPDATE question_answers SET state=?,detail=? WHERE id=? AND state='pending'",(state,detail,ident));b.db.commit()
        self.scan()

    def verify(self):
        b=self.bridge
        self.expire_unconfirmed()
        with b.lock:rows=b.db.execute("SELECT id,title,answer,baseline FROM question_answers WHERE state IN ('submitted','unknown','failed') AND created>?",(time.time()-300,)).fetchall()
        if not rows:return
        data=b.client.read_delivery()
        for ident,title,answer,baseline in rows:
            if answer_receipt(data,title,answer,set(json.loads(baseline))):
                with b.lock:b.db.execute("UPDATE question_answers SET state='answered',detail='回答已送达当前聊天' WHERE id=?",(ident,));b.db.commit()

    def result(self,ident):
        self.expire_unconfirmed()
        with self.bridge.lock:row=self.bridge.db.execute('SELECT state,detail FROM question_answers WHERE id=?',(ident,)).fetchone()
        return dict(zip(('state','detail'),row)) if row else {'state':'missing','detail':'未找到回答请求'}

    def expire_unconfirmed(self):
        # An invoked UI action is not a delivery receipt. Release the board's
        # waiting gate without replaying a write whose outcome is uncertain.
        with self.bridge.lock:
            self.bridge.db.execute("UPDATE question_answers SET state='unknown',detail='未收到Codex送达确认，请重新打开当前问题；不会自动重发旧答案' WHERE state IN ('pending','submitted') AND created<?",(time.time()-30,))
            self.bridge.db.commit()
