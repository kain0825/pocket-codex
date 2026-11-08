const assert=require('assert/strict');
const {createService}=require('./server.cjs');
(async()=>{
 let calls=[];
 const actions=Object.fromEntries(['single','extend','sleep','shutdown'].map(action=>[action,async()=>{calls.push(action);return {ok:true};}]));
 const app=createService({collector:{collect:async()=>({cpu:{usagePercent:1}})},actions,token:'test-token'});
 await new Promise(resolve=>app.listen(0,'127.0.0.1',resolve));
 const url='http://127.0.0.1:'+app.address().port;
 const post=(body,token='test-token')=>fetch(url+'/api/control',{method:'POST',headers:{Authorization:'Bearer '+token},body:JSON.stringify(body)});
 try{
  assert.equal((await post({action:'shutdown',requestId:'test0001'},'wrong')).status,401);assert.equal(calls.length,0);
  assert.equal((await post({action:'arbitrary',requestId:'test0001'})).status,400);
  assert.equal((await post({action:'shutdown',requestId:'test0001'})).status,202);
  assert.equal((await post({action:'shutdown',requestId:'test0001'})).status,200);
  assert.equal((await post({action:'sleep',requestId:'test0002'})).status,409);
  await new Promise(r=>setTimeout(r,350));assert.deepEqual(calls,['shutdown']);
  const data=await (await fetch(url+'/api/metrics')).json();assert.equal(data.control.state,'completed');
  assert.equal((await post({action:'sleep',requestId:'test0001'})).status,409);
  console.log('PASS: auth, whitelist, deduplication, pending guard, result; all Windows actions mocked');
 }finally{app.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
