"""Generate fresh local credentials. Run once per workstation/board pairing."""
import argparse, ipaddress, json, os, secrets, uuid
from pathlib import Path
from datetime import datetime,timedelta,timezone

ROOT=Path(__file__).resolve().parents[1]

def create(pc_host,thread_id,model='small',local_only=False):
    ipaddress.IPv4Address(pc_host)
    uuid.UUID(thread_id)
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes,serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    bridge=ROOT/'host/codex_sync';monitor=ROOT/'host/monitor'
    if (bridge/'device-key.pem').exists():
        raise SystemExit('Pairing already exists. Back up/remove your local pairing files deliberately before creating a different pairing.')
    key=rsa.generate_private_key(public_exponent=65537,key_size=2048)
    name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'pocket-codex.local')])
    now=datetime.now(timezone.utc)
    cert=(x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
        .serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(minutes=5)).not_valid_after(now+timedelta(days=365))
        .add_extension(x509.SubjectAlternativeName([x509.DNSName('pocket-codex.local'),x509.IPAddress(ipaddress.ip_address(pc_host))]),critical=False)
        .add_extension(x509.BasicConstraints(ca=True,path_length=0),critical=True).sign(key,hashes.SHA256()))
    pem=cert.public_bytes(serialization.Encoding.PEM).decode('ascii')
    token=secrets.token_urlsafe(32);monitor_token=secrets.token_urlsafe(32)
    def config(path,data):path.write_text(json.dumps(data,indent=2),encoding='utf-8')
    config(bridge/'bridge.local.json',{'thread_id':thread_id,'asr_model':model,'asr_local_only':local_only})
    config(bridge/'connection.local.json',{'token':secrets.token_urlsafe(32)})
    config(bridge/'device.local.json',{'token':token,'bind':'0.0.0.0','port':8791})
    config(monitor/'control-auth.local.json',{'token':monitor_token})
    (bridge/'device-cert.pem').write_text(pem,encoding='ascii')
    (bridge/'device-key.pem').write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
    try:os.chmod(bridge/'device-key.pem',0o600)
    except OSError:pass
    # Only the public certificate is embedded on the board; its private key stays on the PC.
    header='#pragma once\n#define POCKET_PC_HOST '+json.dumps(pc_host)+'\n'
    header+='inline constexpr const char* kCodexToken='+json.dumps(token)+';\n'
    header+='inline constexpr const char* kCodexCert=R"PCERT('+pem+')PCERT";\n'
    (ROOT/'firmware/main/pocket_pairing.local.h').write_text(header,encoding='utf-8')
    return {'configured':True,'certificateExpires':(now+timedelta(days=365)).date().isoformat(),'next':'Build firmware and start host services; local credentials are ignored by Git.'}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pc-host',required=True,help='LAN IPv4 address reachable by the board')
    parser.add_argument('--thread',required=True,help='Local Codex chat UUID used as initial context')
    parser.add_argument('--model',default='small',help='faster-whisper model name or local model directory')
    parser.add_argument('--local-model-only',action='store_true')
    args=parser.parse_args()
    print(json.dumps(create(args.pc_host,args.thread,args.model,args.local_model_only)))
