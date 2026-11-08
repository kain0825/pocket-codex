import tempfile,threading,time,unittest
from pathlib import Path
from server import Bridge,THREAD,normalize
from task_control import TaskController,active_turn,contains_message

TURN='00000000-0000-0000-0000-000000000001'
def snapshot(turn=TURN,status='inProgress',items=None):
    return {'thread':{'id':THREAD,'status':{'type':'active' if status=='inProgress' else 'idle'}},
        'turns':[{'id':turn,'status':status,'items':items or []}]}
def message(text,id='new'):
    return {'type':'userMessage','id':id,'content':[{'type':'text','text':text}]}

class Client:
    def __init__(self):self.data=snapshot();self.calls=[]
    def read(self):return self.data
    def read_delivery(self):return self.data
    def call(self,name,args):self.calls.append((name,args));return {'threadId':THREAD}

class Tests(unittest.TestCase):
    def test_stale_stop_never_invokes(self):
        c=Client();actions=[];control=TaskController(c,lambda *a:actions.append(a))
        self.assertEqual(control.stop('different')['state'],'stale');self.assertFalse(actions);self.assertFalse(c.calls)
    def test_stop_checks_turn_after_navigation(self):
        c=Client();actions=[]
        c.call=lambda *a:setattr(c,'data',snapshot('other'))
        self.assertEqual(TaskController(c,lambda *a:actions.append(a)).stop(TURN)['state'],'stale')
        self.assertFalse(actions)
    def test_stop_verified_from_turn_status(self):
        c=Client();actions=[]
        def runner(*a):actions.append(a);c.data=snapshot(status='interrupted');return {'ok':True}
        self.assertEqual(TaskController(c,runner).stop(TURN)['state'],'interrupted');self.assertEqual(actions,[('stop',)])
    def test_stop_error_is_not_success(self):
        c=Client()
        result=TaskController(c,lambda *a:{'ok':False,'state':'not_invoked'}).stop(TURN)
        self.assertFalse(result['ok'])
    def test_steer_requires_new_message_in_same_turn(self):
        c=Client();c.data=snapshot(items=[message('hello','old'),message('hello')])
        result=TaskController(c,lambda *a:self.fail()).verify_delivery('hello',TURN,{'old'})
        self.assertEqual(result['delivery'],'steered')
    def test_natural_next_turn_is_not_called_steering(self):
        c=Client();c.data=snapshot('next',items=[message('hello')])
        result=TaskController(c,lambda *a:self.fail()).verify_delivery('hello',TURN)
        self.assertEqual(result['delivery'],'started')
    def test_queue_left_intact_if_turn_changes(self):
        c=Client();c.data=snapshot(status='completed')
        self.assertEqual(TaskController(c,lambda *a:self.fail()).verify_delivery('hello',TURN)['delivery'],'unknown')
    def test_live_delegation_receipt(self):
        item={'id':'receipt','type':'functionCallOutput','namespace':'codex_app','name':'send_message_to_thread',
            'output':{'text':'<codex_delegation>\n  <source_thread_id>'+THREAD+'</source_thread_id>\n  <input>hello</input>\n</codex_delegation>','truncated':False}}
        self.assertEqual(contains_message(snapshot(items=[item]),'hello'),TURN)
        self.assertEqual(contains_message(snapshot(items=[item]),'hello',{'receipt'}),'')
        item['output']['truncated']=True
        self.assertEqual(contains_message(snapshot(items=[item]),'hello'),'')
    def test_normalize_exposes_turn_not_internals(self):
        self.assertEqual(normalize(snapshot())['activeTurnId'],TURN)
        self.assertEqual(active_turn(snapshot(status='completed')),'')
    def test_control_dedup_and_wrong_turn(self):
        c=Client();calls=[];gate=threading.Event()
        class Controller:
            def stop(self,t):calls.append(t);gate.wait(1);return {'state':'interrupted','detail':'stopped'}
        with tempfile.TemporaryDirectory() as d:
            b=Bridge(c,'token',Path(d)/'db',Controller());b.snapshot={**normalize(c.data),'connected':True}
            body={'requestId':'a'*32,'expectedTurnId':TURN,'confirmed':True}
            self.assertEqual(b.stop_task({**body,'confirmed':False})[0],400)
            self.assertEqual(b.stop_task(body)[1]['state'],'pending')
            self.assertTrue(b.stop_task(body)[1]['duplicate'])
            self.assertEqual(b.stop_task({**body,'expectedTurnId':'f'*36})[0],409)
            gate.set()
            for _ in range(50):
                if b.control_result('a'*32)['state']!='pending':break
                time.sleep(.02)
            self.assertEqual(calls,[TURN]);self.assertEqual(b.control_result('a'*32)['state'],'interrupted');b.db.close()
    def test_delayed_receipt_and_no_duplicate_submission(self):
        c=Client()
        class Controller:
            def verify_delivery(self,*a):raise TimeoutError()
        with tempfile.TemporaryDirectory() as d:
            b=Bridge(c,'token',Path(d)/'db',Controller());b.snapshot={'connected':True}
            body={'requestId':'test-guide-01','threadId':THREAD,'message':'hello','confirmed':True}
            first=b.send(body)[1];second=b.send(body)[1]
            self.assertTrue(first['ok']);self.assertEqual(first['delivery'],'submitted');self.assertTrue(second['duplicate'])
            c.data=snapshot(items=[message('hello')]);b.refresh_deliveries()
            self.assertEqual(b.send(body)[1]['delivery'],'steered')
            self.assertEqual(len(c.calls),1);b.db.close()

if __name__=='__main__':unittest.main()
