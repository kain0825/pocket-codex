import tempfile,unittest,json
from pathlib import Path
from unittest.mock import patch
from desktop_client import THREAD,ScopedClient
from server import Bridge,normalize
from task_control import TaskController
from task_catalog import TaskCatalog
from device_api import VoiceJobs

OTHER='00000000-0000-0000-0000-000000000032'
class Client:
    def __init__(self,ident=THREAD):self.thread_id=ident;self.calls=[];self.allowed_threads={THREAD,OTHER};self.state='inProgress'
    def call(self,name,args):self.calls.append((name,args));return {'threadId':args.get('threadId')}
    def read(self):return {'thread':{'id':self.thread_id,'status':{'type':'active'}},'turns':[{'id':'turn-'+self.thread_id,'status':self.state,'items':[]}]}

class Tests(unittest.TestCase):
    def test_scoped_client_rejects_other_target(self):
        base=Client();scope=ScopedClient(base,OTHER)
        with self.assertRaises(ValueError):scope.call('send_message_to_thread',{'threadId':THREAD,'prompt':'x'})
        self.assertEqual(base.calls,[])
        scope.read();self.assertEqual(base.calls[0][1]['threadId'],OTHER)
    def test_normalization_bound_to_selected_chat(self):
        c=Client(OTHER);self.assertEqual(normalize(c.read(),OTHER)['threadId'],OTHER)
        with self.assertRaises(RuntimeError):normalize(c.read(),THREAD)
    def test_same_request_id_separate_chat_ledgers(self):
        with tempfile.TemporaryDirectory() as d:
            a=Client();b=Client(OTHER)
            first=Bridge(a,'token',Path(d)/'a.db');second=Bridge(b,'token',Path(d)/'b.db')
            first.snapshot=second.snapshot={'connected':True}
            body={'requestId':'shared-request-id','message':'hello','confirmed':True}
            try:
                self.assertEqual(first.send({**body,'threadId':THREAD})[0],202)
                self.assertEqual(second.send({**body,'threadId':OTHER})[0],202)
                self.assertEqual(first.send({**body,'threadId':OTHER})[0],400)
                self.assertEqual(a.calls[0][1]['threadId'],THREAD);self.assertEqual(b.calls[0][1]['threadId'],OTHER)
                self.assertTrue(second.send({**body,'threadId':OTHER})[1]['duplicate'])
            finally:first.db.close();second.db.close()
    def test_stop_navigates_and_checks_selected_chat(self):
        c=Client(OTHER)
        def runner(action):c.state='interrupted';return {'ok':True}
        result=TaskController(c,runner,title='另一条任务').stop('turn-'+OTHER)
        self.assertEqual(result['state'],'interrupted');self.assertEqual(c.calls[0][1]['threadId'],OTHER)
    def test_voice_drafts_not_shared(self):
        with tempfile.TemporaryDirectory() as d:
            a=VoiceJobs(Path(d)/'a');b=VoiceJobs(Path(d)/'b');ident='a'*32
            a.draft(ident,'A的草稿');self.assertEqual(b.read(ident)['state'],'missing')
            b.draft(ident,'B的草稿');self.assertEqual(a.read(ident)['editedText'],'A的草稿')
            a.executor.shutdown();b.executor.shutdown()
    def test_catalog_filters_remote_and_chatgpt(self):
        with tempfile.TemporaryDirectory() as d:
            c=Client();bridge=Bridge(c,'token',Path(d)/'db');bridge.snapshot={'title':'开发板'}
            c.call=lambda *a:{'threads':[
                {'id':THREAD,'kind':'codex','hostId':'local','title':'开发板','status':'active'},
                {'id':OTHER,'kind':'codex','hostId':'local','title':'另一条','status':'notLoaded'},
                {'id':'f'*36,'kind':'codex','hostId':'remote','title':'远程','status':'active'},
                {'id':'e'*36,'kind':'chatgpt','hostId':'local','title':'ChatGPT'}]}
            catalog=TaskCatalog(bridge,c,Path(d),start=False);catalog.refresh()
            self.assertEqual({x['id'] for x in catalog.snapshot['tasks']},{THREAD,OTHER})
            self.assertEqual(catalog.snapshot['tasks'][0]['label'],'执行中')
            self.assertEqual(catalog.snapshot['tasks'][1]['label'],'未运行')
            with self.assertRaises(ValueError):catalog.context('f'*36,None)
            bridge.db.close()

if __name__=='__main__':unittest.main()
