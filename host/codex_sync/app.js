'use strict';
const $=id=>document.getElementById(id);
const fragment=new URLSearchParams(location.hash.slice(1));
if(fragment.get('key')){sessionStorage.setItem('pocketKey',fragment.get('key'));history.replaceState(null,'',location.pathname);}
const key=sessionStorage.getItem('pocketKey')||'';
const headers={Authorization:'Bearer '+key};
let chat=null,revision='',sending=false,pending=null;
function render(data){
  chat=data;
  $('connection').textContent=data.connected?({active:'桌面处理中',idle:'桌面已连接'}[data.status]||'桌面已连接'):'桌面连接中断';
  $('send').disabled=!data.connected||sending;
  if(!data.connected){$('feedback').textContent=data.error||'连接中断，旧内容保留；发送已禁用。';return;}
  $('title').textContent=data.title;
  $('outbox').replaceChildren(...(data.outbox||[]).filter(m=>m.message).map(m=>{const row=document.createElement('p');row.className='sent';row.textContent=({accepted:'已送入聊天',dispatching:'提交中',unknown:'结果未知'}[m.state]||m.state)+' · '+m.message;return row;}));
  if(revision===data.revision)return;
  revision=data.revision;
  const pane=$('messages'),atBottom=pane.scrollHeight-pane.scrollTop-pane.clientHeight<60;
  const nodes=data.messages.map(m=>{const box=document.createElement('article');box.className='message '+m.role;const label=document.createElement('span');label.className='speaker';label.textContent=m.role==='user'?'你':m.phase==='commentary'?'Codex · 进度':'Codex';const text=document.createElement('span');text.className='text';text.textContent=m.text;box.append(label,text);return box;});
  pane.replaceChildren(...nodes);if(atBottom)pane.scrollTop=pane.scrollHeight;
}
async function poll(){try{const res=await fetch('/api/chat',{headers,cache:'no-store'});if(!res.ok)throw new Error(res.status===401?'请使用启动脚本打开带本机配对信息的预览页。':'读取失败');render(await res.json());}catch(e){$('connection').textContent='未连接';$('send').disabled=true;$('feedback').textContent=e.message;}finally{setTimeout(poll,2000);}}
$('test').onclick=()=>{$('draft').value='【电脑端同步测试】请确认收到，并回复“同步成功”。这是连接验证，请不要执行其他操作。';};
$('send').onclick=async()=>{
  const message=$('draft').value.trim();if(!message||!chat?.connected||sending)return;
  if(!confirm('发送到「'+chat.title+'」？\n\n'+message))return;
  if(!pending||pending.message!==message)pending={requestId:crypto.randomUUID(),message};
  sending=true;$('send').disabled=true;$('feedback').textContent='正在提交，请勿重复点击…';
  try{const res=await fetch('/api/send',{method:'POST',headers:{...headers,'Content-Type':'application/json'},body:JSON.stringify({...pending,threadId:chat.threadId,confirmed:true})});const data=await res.json();$('feedback').textContent=data.detail||data.error||'提交结果未知';if(data.state==='accepted'){$('draft').value='';pending=null;}}
  catch(e){$('feedback').textContent='网络中断，发送结果未知。请检查原聊天；重复点击会沿用同一编号，不会自动重发。';}
  finally{sending=false;$('send').disabled=!chat?.connected;}
};
poll();
