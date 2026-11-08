import json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import monitor_proxy

class Reply:
    status=200
    def __init__(self,raw):self.raw=raw
    def __enter__(self):return self
    def __exit__(self,*args):pass
    def read(self,count):return self.raw[:count]

class Opener:
    def __init__(self,raw):self.raw=raw;self.requests=[]
    def open(self,request,timeout):
        self.requests.append(request);return Reply(self.raw)

class MonitorProxyTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)/'codex_sync';self.root.mkdir()
        m=self.root.parent/'monitor';m.mkdir();(m/'control-auth.local.json').write_text(json.dumps({'token':'synthetic-test-token'}),encoding='utf-8')
        self.patch=patch.object(monitor_proxy,'ROOT',self.root);self.patch.start();self.addCleanup(self.patch.stop)

    def test_metrics_only_uses_loopback_and_adds_auth(self):
        op=Opener(b'{"cpu":{"usagePercent":12}}')
        with patch.object(monitor_proxy.urllib.request,'build_opener',return_value=op):
            status,data=monitor_proxy.forward_monitor('GET','/api/metrics')
        self.assertEqual(status,200);self.assertEqual(data['cpu']['usagePercent'],12)
        self.assertEqual(op.requests[0].full_url,'http://127.0.0.1:8788/api/metrics')
        self.assertEqual(op.requests[0].get_header('Authorization'),'Bearer synthetic-test-token')

    def test_other_paths_are_rejected_without_network(self):
        with patch.object(monitor_proxy.urllib.request,'build_opener') as mocked:
            self.assertEqual(monitor_proxy.forward_monitor('GET','http://example.com/')[0],404)
        mocked.assert_not_called()

    def test_oversized_response_rejected(self):
        with patch.object(monitor_proxy.urllib.request,'build_opener',return_value=Opener(b'x'*20000)):
            self.assertEqual(monitor_proxy.forward_monitor('GET','/api/metrics')[0],502)

    def test_control_body_forwarded_once_to_local_service(self):
        op=Opener(b'{"ok":true,"state":"pending"}')
        body=b'{"action":"extend","requestId":"synthetic-test"}'
        with patch.object(monitor_proxy.urllib.request,'build_opener',return_value=op):
            self.assertEqual(monitor_proxy.forward_monitor('POST','/api/control',body)[0],200)
        self.assertEqual(len(op.requests),1);self.assertEqual(op.requests[0].data,body)
