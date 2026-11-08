"""Bounded, local-only proxy. The board authenticates at the HTTPS boundary."""
import json, urllib.request, urllib.error
from pathlib import Path

ROOT=Path(__file__).resolve().parent

def forward_monitor(method,path,body=None):
    if (method,path) not in {('GET','/api/metrics'),('POST','/api/control')}:
        return 404,{'ok':False}
    try:
        config=json.loads((ROOT.parent/'monitor/control-auth.local.json').read_text(encoding='utf-8'))
        request=urllib.request.Request('http://127.0.0.1:8788'+path,data=body if method=='POST' else None,
            headers={'Authorization':'Bearer '+config['token'],'Content-Type':'application/json'},method=method)
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:response=opener.open(request,timeout=4)
        except urllib.error.HTTPError as error:response=error
        with response:
            raw=response.read(16385)
            if len(raw)>16384:return 502,{'ok':False,'error':'monitor_response_too_large'}
            return response.status,json.loads(raw)
    except (OSError,ValueError,KeyError):
        return 503,{'ok':False,'error':'monitor_unavailable'}
