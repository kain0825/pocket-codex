import json,threading,tempfile,time,unittest
from pathlib import Path
from questions import answer_receipt
from server import Bridge,THREAD

class Client:
    def __init__(self):self.data={'thread':{'id':THREAD},'turns':[{'items':[]}]}
    def read(self):return self.data
    def read_delivery(self):return self.data

class Controller:
    def __init__(self):self.lock=threading.Lock();self.calls=[];self.fail=False;self.gate=threading.Event()
    def navigate(self):pass
    def _run(self,action,**kwargs):
        self.calls.append((action,kwargs))
        if action=='answer':
            self.gate.wait(2)
            return {'ok':not self.fail,'state':'not_invoked' if self.fail else 'submitted'}
        return {'ok':True,'questions':[{'id':'a'*32,'title':'测试问题','options':['甲','乙'],'canWrite':True,'canSelect':True}]}

def receipt(title='测试问题',answer='乙',ident='new'):
    raw='<send_user_message_question_reply>'+json.dumps([{'questionItemId':'native-id','question':title,'answer':answer}],ensure_ascii=False)+'</send_user_message_question_reply>'
    return {'turns':[{'items':[{'type':'userMessage','id':ident,'content':[{'type':'text','text':raw}]}]}]}

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.client=Client();self.controller=Controller()
        self.bridge=Bridge(self.client,'token',Path(self.tmp.name)/'db',self.controller);self.q=self.bridge.questions;self.q.scan()
        self.body={'requestId':'b'*32,'questionId':'a'*32,'answer':'乙','confirmed':True}
    def tearDown(self):
        self.controller.gate.set()
        for _ in range(100):
            with self.bridge.lock:n=self.bridge.db.execute("SELECT COUNT(*) FROM question_answers WHERE state='pending'").fetchone()[0]
            if not n:break
            time.sleep(.01)
        self.bridge.db.close();self.tmp.cleanup()
    def wait_submit(self):
        self.controller.gate.set()
        for _ in range(100):
            if self.q.result('b'*32)['state']!='pending':break
            time.sleep(.01)
    def test_explicit_confirmation_and_stale_question(self):
        self.assertEqual(self.q.submit({**self.body,'confirmed':False})[0],400)
        self.assertEqual(self.q.submit({**self.body,'questionId':'f'*32})[1]['state'],'stale')
        self.assertFalse(any(c[0]=='answer' for c in self.controller.calls))
    def test_duplicate_request_and_card_not_submitted_twice(self):
        self.assertEqual(self.q.submit(self.body)[1]['state'],'pending')
        self.assertTrue(self.q.submit(self.body)[1]['duplicate'])
        self.assertTrue(self.q.submit({**self.body,'requestId':'c'*32})[1]['duplicate'])
        self.assertEqual(self.q.submit({**self.body,'answer':'甲'})[0],409)
        self.wait_submit();self.assertEqual(sum(c[0]=='answer' for c in self.controller.calls),1)
    def test_receipt_required_for_answered(self):
        self.q.submit(self.body);self.wait_submit();self.q.verify()
        self.assertEqual(self.q.result('b'*32)['state'],'submitted')
        self.client.data=receipt();self.q.verify();self.assertEqual(self.q.result('b'*32)['state'],'answered')
    def test_native_error_not_reported_as_answered(self):
        self.controller.fail=True;self.q.submit(self.body);self.wait_submit()
        self.assertEqual(self.q.result('b'*32)['state'],'failed')
    def test_matching_old_or_different_receipt_rejected(self):
        self.assertFalse(answer_receipt(receipt(),'测试问题','甲',set()))
        self.assertFalse(answer_receipt(receipt(),'另一个问题','乙',set()))
        self.assertFalse(answer_receipt(receipt(),'测试问题','乙',{'new'}))
        self.assertEqual(answer_receipt(receipt(),'测试问题','乙',set()),'native-id')
    def test_unconfirmed_action_expires_without_automatic_replay(self):
        self.q.submit(self.body);self.wait_submit()
        with self.bridge.lock:
            self.bridge.db.execute('UPDATE question_answers SET created=? WHERE id=?',(time.time()-31,'b'*32));self.bridge.db.commit()
        self.assertEqual(self.q.result('b'*32)['state'],'unknown')
        self.assertEqual(sum(c[0]=='answer' for c in self.controller.calls),1)
        self.client.data=receipt();self.q.verify()
        self.assertEqual(self.q.result('b'*32)['state'],'answered')
    def test_receipt_overrides_native_false_negative_without_resending(self):
        self.controller.fail=True;self.q.submit(self.body);self.wait_submit()
        self.assertEqual(self.q.result('b'*32)['state'],'failed')
        self.client.data=receipt();self.q.verify()
        self.assertEqual(self.q.result('b'*32)['state'],'answered')
        self.assertEqual(sum(c[0]=='answer' for c in self.controller.calls),1)

if __name__=='__main__':unittest.main()
