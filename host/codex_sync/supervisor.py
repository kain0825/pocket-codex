"""Keep the local bridge running; never replays application writes."""
from pathlib import Path
import json,logging,msvcrt,os,subprocess,sys,time,urllib.request
from logging.handlers import RotatingFileHandler

ROOT=Path(__file__).resolve().parent
def main():
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(levelname)s %(message)s',handlers=[RotatingFileHandler(ROOT/'supervisor.log',maxBytes=1048576,backupCount=3,encoding='utf-8')])
    with (ROOT/'supervisor.lock').open('a+b') as lock:
        lock.seek(0)
        if not lock.read(1):lock.write(b'1');lock.flush()
        lock.seek(0)
        try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:return
        (ROOT/'supervisor-runtime.json').write_text(json.dumps({'pid':os.getpid(),'startedAt':time.time()}),encoding='utf-8')
        delay=1
        while True:
            try:
                with urllib.request.urlopen('http://127.0.0.1:8790/health',timeout=3) as response:data=json.load(response)
                if data.get('service')=='pocket-codex-sync':time.sleep(3);continue
            except (OSError,ValueError):pass
            started=time.monotonic()
            with (ROOT/'server.log').open('a',encoding='utf-8') as output,(ROOT/'server-error.log').open('a',encoding='utf-8') as error:
                child=subprocess.Popen([sys.executable,'-u',str(ROOT/'server.py')],cwd=ROOT,stdout=output,stderr=error,creationflags=subprocess.CREATE_NO_WINDOW,env={**os.environ,'PYTHONUTF8':'1','PYTHONIOENCODING':'utf-8'})
                logging.info('Started bridge pid=%s',child.pid)
                code=child.wait()
            uptime=time.monotonic()-started
            logging.warning('Bridge exited pid=%s exit=%s uptime=%.1fs restart_in=%ss',child.pid,code,uptime,delay)
            time.sleep(delay)
            delay=1 if uptime>30 else min(delay*2,30)

if __name__=='__main__':main()
