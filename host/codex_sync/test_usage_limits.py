import unittest,time
from usage_limits import normalize_usage,UsageCache
class UsageTests(unittest.TestCase):
 def test_remaining_not_used(self):
  r=normalize_usage({'rateLimits':{'primary':{'usedPercent':77,'windowDurationMins':10080,'resetsAt':1794396330}}});self.assertEqual(r['windows'][0]['remainingPercent'],23);self.assertEqual(r['text'],'额度 周余23%');self.assertIn('北京时间',r['details'])
 def test_missing_not_zero(self):
  for x in [{},{'rateLimits':None},{'rateLimits':{'primary':None}},{'rateLimits':{'primary':{'usedPercent':None,'windowDurationMins':300}}}]:self.assertFalse(normalize_usage(x)['available'])
 def test_both_windows(self):
  r=normalize_usage({'rateLimitsByLimitId':{'codex':{'primary':{'usedPercent':10,'windowDurationMins':300},'secondary':{'usedPercent':40,'windowDurationMins':10080}}}});self.assertEqual(r['text'],'额度 5小时余90% · 周余60%')
 def test_multiple_buckets_preferred(self):
  r=normalize_usage({'rateLimits':{'primary':{'usedPercent':99,'windowDurationMins':60}},'rateLimitsByLimitId':{'codex':{'primary':{'usedPercent':25,'windowDurationMins':1440}},'other':{'limitName':'其他模型','primary':{'usedPercent':50,'windowDurationMins':30}}}});self.assertEqual([w['remainingPercent'] for w in r['windows']],[75,50]);self.assertIn('其他模型',r['text'])
 def test_clamp_and_invalid(self):
  for used,want in [(-10,100),(120,0)]:self.assertEqual(normalize_usage({'rateLimits':{'primary':{'usedPercent':used,'windowDurationMins':60}}})['windows'][0]['remainingPercent'],want)
  for invalid in [True,float('nan'),float('inf'),'7']:
   self.assertFalse(normalize_usage({'rateLimits':{'primary':{'usedPercent':invalid,'windowDurationMins':60}}})['available'])
 def test_isolated_read_cache_and_failure(self):
  class Fake:
   fail=False
   def call(self,name,args):
    assert name=='get_usage_limits' and args=={}
    if self.fail:raise RuntimeError('offline')
    return {'rateLimits':{'primary':{'usedPercent':77,'windowDurationMins':10080}}}
  fake=Fake();c=UsageCache(fake);c.refresh();self.assertTrue(c.snapshot()['available']);fake.fail=True;c.refresh();self.assertEqual(c.snapshot()['text'],'上次额度 周余23%');self.assertTrue(c.snapshot()['stale']);self.assertNotIn('accountId',c.snapshot())
 def test_expired_cache_marked(self):
  c=UsageCache(object());c.value=normalize_usage({'rateLimits':{'primary':{'usedPercent':77,'windowDurationMins':10080}}},time.time()-181);self.assertTrue(c.snapshot()['text'].startswith('上次'))
if __name__=='__main__':unittest.main()
