import json
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from server import Bridge, create_server, normalize, THREAD

class FakeClient:
    def __init__(self): self.calls=[]; self.fail=False
    def call(self,name,args):
        self.calls.append((name,args))
        if self.fail: raise TimeoutError('test timeout')
        return {'threadId':THREAD}

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.client=FakeClient()
        self.bridge=Bridge(self.client,'local-test-token',Path(self.tmp.name)/'ledger.sqlite')
        self.bridge.snapshot={'connected':True}
        self.body={'requestId':'test-request-001','message':'测试','threadId':THREAD,'confirmed':True}
    def tearDown(self): self.bridge.db.close(); self.tmp.cleanup()
    def test_bound_thread_and_confirmation(self):
        for change in ({'threadId':'other'},{'confirmed':False},{'message':''}):
            self.assertEqual(self.bridge.send({**self.body,**change})[0],400)
        self.assertEqual(self.client.calls,[])
    def test_dedup_and_conflict(self):
        self.assertEqual(self.bridge.send(self.body)[0],202)
        self.assertTrue(self.bridge.send(self.body)[1]['duplicate'])
        self.assertEqual(self.bridge.send({**self.body,'message':'different'})[0],409)
        self.assertEqual(len(self.client.calls),1)
    def test_unknown_not_retried(self):
        self.client.fail=True
        self.assertEqual(self.bridge.send(self.body)[1]['state'],'unknown')
        self.assertEqual(self.bridge.send(self.body)[1]['state'],'unknown')
        self.assertEqual(len(self.client.calls),1)
    def test_disconnected(self):
        self.bridge.snapshot['connected']=False
        self.assertEqual(self.bridge.send(self.body)[0],503)
        self.assertEqual(self.client.calls,[])
    def test_private_items_filtered(self):
        result=normalize({'thread':{'id':THREAD,'status':{'type':'active'}},'turns':[
            {'id':'new','items':[{'type':'agentMessage','id':'a','text':'new'},{'type':'reasoning','text':'private'},{'type':'commandExecution','text':'secret'}]},
            {'id':'old','items':[{'type':'userMessage','id':'u','content':[{'type':'text','text':'old'}]}]}]})
        self.assertEqual([m['text'] for m in result['messages']],['old','new'])
        self.assertEqual(result['status'],'active')
    def test_http_auth_origin_and_single_dispatch(self):
        server=create_server(self.bridge,0)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        origin=f'http://127.0.0.1:{server.server_port}'
        def request(path,body=None,auth=True,request_origin=origin):
            headers={'Origin':request_origin}
            if auth:headers['Authorization']='Bearer local-test-token'
            data=None if body is None else json.dumps(body).encode()
            req=urllib.request.Request(origin+path,data=data,headers=headers)
            try:
                with urllib.request.urlopen(req) as r:return r.status,json.loads(r.read())
            except urllib.error.HTTPError as e:return e.code,json.loads(e.read())
        try:
            self.assertEqual(request('/api/chat',auth=False)[0],401)
            self.assertEqual(request('/api/send',self.body,request_origin='https://unrelated.example')[0],403)
            self.assertEqual(request('/api/chat')[0],200)
            self.assertEqual(request('/api/send',self.body)[0],202)
            self.assertTrue(request('/api/send',self.body)[1]['duplicate'])
            self.assertEqual(len(self.client.calls),1)
        finally:server.shutdown();server.server_close();thread.join()

if __name__=='__main__':unittest.main()
