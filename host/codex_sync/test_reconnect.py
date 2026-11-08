import os,unittest
from unittest.mock import patch
from desktop_client import DesktopClient,THREAD

class Process:
    def poll(self):return None

class Client(DesktopClient):
    def __init__(self):
        super().__init__();self.calls=[];self.failures=1
    def _connect(self):
        self.proc=Process();self.tools={'read_thread':{},'send_message_to_thread':{}}
        self.reconnections+=1
    def _disconnect(self):self.proc=None;self.tools={}
    def rpc(self,method,params):
        self.calls.append(params['name'])
        if self.failures:
            self.failures-=1;raise BrokenPipeError('connection interrupted after dispatch')
        return {'content':[{'type':'text','text':'{"ok":true}'}]}

class ReconnectTests(unittest.TestCase):
    def test_read_reconnects_and_retries(self):
        client=Client()
        self.assertTrue(client.call('read_thread',{'threadId':THREAD})['ok'])
        self.assertEqual(client.calls,['read_thread','read_thread'])
        self.assertEqual(client.reconnections,2)
    def test_write_is_not_replayed_after_lost_receipt(self):
        client=Client()
        with self.assertRaises(BrokenPipeError):client.call('send_message_to_thread',{'threadId':THREAD})
        self.assertEqual(client.calls,['send_message_to_thread'])
        self.assertIsNone(client.proc)
    def test_rejects_wrong_target_before_any_dispatch(self):
        client=Client()
        with self.assertRaises(ValueError):client.call('send_message_to_thread',{'threadId':'other'})
        self.assertEqual(client.calls,[])
    @patch.dict(os.environ,{'CODEX_APP_TOOLS_PIPE_PATH':'stale'})
    @patch('desktop_client.os.listdir',return_value=['codex-app-tools-new'])
    def test_discovers_new_pipe_instead_of_stale_saved_address(self,_):
        self.assertTrue(DesktopClient.discover_pipe().endswith('codex-app-tools-new'))
    @patch.dict(os.environ,{'CODEX_APP_TOOLS_PIPE_PATH':'stale'})
    @patch('desktop_client.os.listdir',return_value=['codex-app-tools-a','codex-app-tools-b'])
    def test_ambiguous_session_is_not_guessed(self,_):
        with self.assertRaises(RuntimeError):DesktopClient.discover_pipe()

if __name__=='__main__':unittest.main()
