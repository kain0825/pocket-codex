"""Per-user settings are generated locally and are never committed."""
import json, os
from pathlib import Path

ROOT=Path(__file__).resolve().parent

def setting(name, default=None):
    env=os.environ.get('POCKET_'+name.upper())
    if env is not None:
        if isinstance(default,bool):return env.lower() in ('true','1','yes')
        return env
    path=ROOT/'bridge.local.json'
    if path.is_file():return json.loads(path.read_text(encoding='utf-8')).get(name,default)
    return default
