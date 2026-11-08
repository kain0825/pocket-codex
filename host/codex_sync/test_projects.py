import unittest,tempfile,time
from pathlib import Path
from types import SimpleNamespace
from task_catalog import TaskCatalog
from desktop_client import THREAD
OTHER='00000000-0000-0000-0000-000000000034'

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.rows=[{'id':THREAD,'kind':'codex','hostId':'local','projectId':'p1','cwd':'D:\\same','title':'甲','status':'active','updatedAt':2},
                   {'id':OTHER,'kind':'codex','hostId':'local','projectId':'p2','cwd':'E:\\same','title':'乙','status':'idle','updatedAt':1}]
        self.client=SimpleNamespace(allowed_threads=set(),call=self.call)
        self.primary=SimpleNamespace(snapshot={'title':'甲'},controller=None,questions=None)
        self.catalog=TaskCatalog(self.primary,self.client,Path(self.tmp.name),start=False)
    def tearDown(self):self.tmp.cleanup()
    def call(self,name,args):
        if name=='list_projects':return {'projects':[{'projectId':'p1','projectKind':'local','hostId':'local','label':'真实项目甲'}, {'projectId':'p2','projectKind':'local','hostId':'local','label':'真实项目乙'}]}
        return {'threads':self.rows}
    def test_project_identity_not_same_folder_basename(self):
        self.catalog.refresh();a,b=self.catalog.snapshot['tasks'];self.assertNotEqual(a['projectId'],b['projectId']);self.assertEqual(a['project'],'真实项目甲');self.assertEqual(b['project'],'真实项目乙')
    def test_status_change_does_not_reorder_cards(self):
        self.catalog.refresh();before=[t['id'] for t in self.catalog.snapshot['tasks']]
        self.rows[0]['status']='idle';self.rows[1].update(status='active',updatedAt=900)
        self.catalog.refresh();self.assertEqual(before,[t['id'] for t in self.catalog.snapshot['tasks']])
        self.assertEqual(self.catalog.snapshot['tasks'][1]['updatedAt'],900)
    def test_running_and_question_are_independent(self):
        self.primary.questions=SimpleNamespace(snapshot={'available':True,'observedAt':time.monotonic(),'questions':[{'id':'q'}]})
        self.catalog.refresh();task=self.catalog.snapshot['tasks'][0]
        self.assertEqual(task['status'],'active');self.assertEqual(task['questions'],1)
    def test_unobserved_and_stale_questions_are_unknown(self):
        self.catalog.refresh();self.assertEqual(self.catalog.snapshot['tasks'][0]['questions'],-1)
        self.primary.questions=SimpleNamespace(snapshot={'available':True,'observedAt':time.monotonic()-11,'questions':[]})
        self.catalog.refresh();self.assertEqual(self.catalog.snapshot['tasks'][0]['questions'],-1)
    def test_unassigned_is_not_guessed_from_title(self):
        self.rows[0]['projectId']=None;self.rows[0]['title']='看起来像真实项目乙'
        self.catalog.refresh();task=self.catalog.snapshot['tasks'][0]
        self.assertEqual(task['projectId'],'unassigned');self.assertEqual(task['project'],'未分组聊天')

if __name__=='__main__':unittest.main()
