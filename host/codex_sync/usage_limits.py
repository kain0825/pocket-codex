"""Read-only account quota cache. No reset, purchase, auth token or billing writes."""
import datetime,math,threading,time
from desktop_client import DesktopClient

def normalize_usage(data,now=None):
 now=time.time() if now is None else now;windows=[]
 buckets=data.get('rateLimitsByLimitId')
 if not isinstance(buckets,dict) or not buckets:buckets={'codex':data.get('rateLimits') or {}}
 for ident,bucket in buckets.items():
  if not isinstance(bucket,dict):continue
  for field in ('primary','secondary'):
   window=bucket.get(field)
   if not isinstance(window,dict):continue
   used=window.get('usedPercent');mins=window.get('windowDurationMins');reset=window.get('resetsAt')
   if isinstance(used,bool) or not isinstance(used,(int,float)) or not math.isfinite(used):continue
   if isinstance(mins,bool) or not isinstance(mins,(int,float)) or not math.isfinite(mins) or mins<=0:continue
   label='周' if mins==10080 else '日' if mins==1440 else f'{mins/60:g}小时' if mins%60==0 else f'{mins:g}分钟'
   if ident!='codex':label=f'{bucket.get("limitName") or ident} {label}'
   remaining=max(0,min(100,100-used));reset=int(reset) if isinstance(reset,(int,float)) and math.isfinite(reset) and reset>0 else None
   windows.append({'id':str(ident),'window':field,'label':label,'remainingPercent':remaining,'windowDurationMins':mins,'resetsAt':reset})
 text='额度 '+ ' · '.join(f'{w["label"]}余{w["remainingPercent"]:g}%' for w in windows) if windows else '额度暂不可用'
 detail=[]
 for w in windows:
  line=f'{w["label"]}额度剩余 {w["remainingPercent"]:g}%'
  if w['resetsAt']:
   stamp=datetime.datetime.fromtimestamp(w['resetsAt'],datetime.timezone(datetime.timedelta(hours=8)))
   line+=f'\n重置：{stamp:%m-%d %H:%M}（北京时间）'
  detail.append(line)
 return {'available':bool(windows),'text':text,'details':'\n\n'.join(detail) or '电脑尚未返回可用的额度信息。','windows':windows,'observedAt':int(now),'stale':False}

class UsageCache:
 def __init__(self,client=None):
  # A separate read-only transport prevents a slow quota query holding the chat lock.
  self.client=client or DesktopClient();self.lock=threading.Lock();self.value=normalize_usage({});self.running=True
 def refresh(self):
  try:
   value=normalize_usage(self.client.call('get_usage_limits',{}))
   with self.lock:self.value=value
  except Exception:
   with self.lock:self.value={**self.value,'stale':True}
 def snapshot(self):
  with self.lock:value=dict(self.value)
  if value.get('stale') or time.time()-value.get('observedAt',0)>180:
   value['stale']=True
   if value.get('available'):value['text']='上次'+value['text'];value['details']+='\n\n当前未刷新，以上为上次成功读取。'
  return value
 def run(self):
  while self.running:self.refresh();time.sleep(60)
 def start(self):threading.Thread(target=self.run,name='account-quota-cache',daemon=True).start()
