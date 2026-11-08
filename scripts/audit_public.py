"""Audit the exact Git index before committing/publishing; never prints matches."""
from pathlib import Path
import argparse,json,re,subprocess,sys

ROOT=Path(__file__).resolve().parents[1]
BAD_PARTS={'build','managed_components','__pycache__','.venv','voice','chats'}
PRIVATE_MODULES={'pocket_diary.cc','pocket_diary.h','pocket_library.cc','pocket_library.h','pocket_bundle.cc','pocket_bundle.h','pocket_wifi_sync.cc','pocket_wifi_sync.h'}
PATTERNS={
    'private-key':rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
    'GitHub-token':rb'\b(?:gh[pousr]_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{35,})',
    'OpenAI-key':rb'\bsk-(?:proj-)?[A-Za-z0-9_-]{30,}',
    'personal-Windows-path':rb'(?i)[CDH]:[/\\](?:Users[/\\]|Espressif[/\\]|PocketAI[/\\]|workbuddy[/\\])',
    'excluded-module-reference':rb'PocketDiary|PocketLibrary|PocketBundle|PocketWifiSync|PKOS/(?:DIARY|NOTES|RECORD)',
}

def main():
    output=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT)
    paths=[p for p in output.decode('utf-8').split('\0') if p]
    issues=[];total=0
    for rel in paths:
        parts=Path(rel).parts;name=parts[-1]
        if any(part in BAD_PARTS for part in parts) or name in PRIVATE_MODULES or '.local.' in name or name in {'device-key.pem','device-cert.pem','sdkconfig','TaskControl.exe'} or name.endswith(('.sqlite','.sqlite-wal','.log','.bin','.elf','.map')):
            issues.append({'file':rel,'kind':'excluded-file'});continue
        # Read the staged blob, not a different copy in the working tree.
        data=subprocess.check_output(['git','show',':'+rel],cwd=ROOT);total+=len(data)
        if rel==Path(__file__).relative_to(ROOT).as_posix():continue
        for kind,pattern in PATTERNS.items():
            if re.search(pattern,data):issues.append({'file':rel,'kind':kind})
    result={'files':len(paths),'bytes':total,'passed':not issues,'issues':issues}
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0 if not issues else 1

if __name__=='__main__':sys.exit(main())
