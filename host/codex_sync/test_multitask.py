import tempfile,unittest
from types import SimpleNamespace
from pathlib import Path
from device_api import chat_tabs,send_continue
from server import Bridge
from task_catalog import TaskCatalog
from desktop_client import THREAD

class Tests(unittest.TestCase):
    def test_tabs_filter_cap_and_selected(self):
        tasks=[{'id':str(i),'title':str(i),'status':'active' if i<15 else 'idle','questions':-1,'messages':['private']} for i in range(20)]
        tasks[17]['questions']=2;tasks[18]['attention']=True
        r=chat_tabs({'tasks':tasks,'connected':True},'19')
        self.assertEqual(len(r['tabs']),12);self.assertEqual(r['more'],6)
        self.assertEqual(r['tabs'][0]['id'],'19');self.assertTrue(all('messages' not in t for t in r['tabs']))
        self.assertEqual(r['revision'],chat_tabs({'tasks':tasks},'19')['revision'])
        r=chat_tabs({'tasks':tasks[15:]},'19')
        self.assertEqual([t['id'] for t in r['tabs']],['17','18','19'])
    def test_continue_bound_dedup_preserves_draft(self):
        class Client:
            thread_id='other-local-chat'
            def __init__(self):self.calls=[]
            def call(self,name,args):self.calls.append((name,args));return {'threadId':self.thread_id}
        with tempfile.TemporaryDirectory() as d:
            c=Client();b=Bridge(c,'test',Path(d)/'db');b.snapshot={'connected':True}
            draft=Path(d)/'draft.txt';draft.write_text('keep my draft')
            body={'confirmed':True,'requestId':'a'*32,'message':'injected','threadId':'wrong'}
            self.assertEqual(send_continue(b,body)[0],202)
            self.assertTrue(send_continue(b,body)[1]['duplicate'])
            self.assertEqual(c.calls,[('send_message_to_thread',{'threadId':c.thread_id,'prompt':'继续'})])
            self.assertEqual(draft.read_text(),'keep my draft')
            for bad in [None,{},[],{'confirmed':True,'requestId':'bad'}, {'confirmed':False,'requestId':'b'*32}]:
                self.assertEqual(send_continue(b,bad)[0],400)
            self.assertEqual(len(c.calls),1);b.db.close()
    def test_unknown_delivery_never_replays(self):
        class Client:
            thread_id='local-chat'
            def __init__(self):self.count=0
            def call(self,*args):self.count+=1;raise TimeoutError('no receipt')
        with tempfile.TemporaryDirectory() as d:
            c=Client();b=Bridge(c,'test',Path(d)/'db');b.snapshot={'connected':True}
            body={'confirmed':True,'requestId':'b'*32}
            self.assertEqual(send_continue(b,body)[0],502)
            self.assertTrue(send_continue(b,body)[1]['duplicate']);self.assertEqual(c.count,1);b.db.close()
    def test_attention_cursor_and_clear(self):
        other='00000000-0000-0000-0000-000000000099';phase=[0];calls=[]
        def call(name,args):
            if name=='list_projects':return {'projects':[]}
            if name=='list_threads':return {'threads':[{'id':THREAD,'title':'a','kind':'codex','hostId':'local','status':'active'},{'id':other,'title':'b','kind':'codex','hostId':'local','status':'idle'}]}
            self.assertEqual(name,'wait_threads');calls.append(args)
            return {'wake':{'threadId':other,'reason':'needsAttention'} if phase[0]==0 else {},'polls':[{'thread':{'id':other,'status':{'type':'active' if phase[0]<2 else 'idle'}},'changed':phase[0]!=1,'cursor':'c'+str(phase[0])}]}
        with tempfile.TemporaryDirectory() as d:
            primary=SimpleNamespace(snapshot={},questions=None,controller=None)
            catalog=TaskCatalog(primary,SimpleNamespace(call=call,allowed_threads=set()),Path(d),start=False)
            catalog.refresh();self.assertTrue(catalog.snapshot['tasks'][1]['attention'])
            phase[0]=1;catalog.refresh();self.assertTrue(catalog.snapshot['tasks'][1]['attention'])
            self.assertEqual(calls[-1]['targets'][0]['afterCursor'],'c0')
            phase[0]=2;catalog.refresh();self.assertFalse(catalog.snapshot['tasks'][1]['attention'])

if __name__=='__main__':unittest.main()
