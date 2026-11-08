const http = require('http');
const fs = require('fs');
const crypto = require('crypto');
const path = require('path');
const {createCollector} = require('./live-metrics.js');
const controls = require('./controls.cjs');
function createService({collector=createCollector(), actions=controls, token=JSON.parse(fs.readFileSync(path.join(__dirname,'control-auth.local.json'),'utf8')).token}={}) {
  const jobs=new Map(); let pending=null, last=null;
  function reply(res,status,data){const body=JSON.stringify(data);res.writeHead(status,{'Content-Type':'application/json','Content-Length':Buffer.byteLength(body),'Cache-Control':'no-store','Connection':'close'});res.end(body);}
  return http.createServer(async(req,res)=>{
    try {
      if(req.method==='GET' && req.url==='/api/health')return reply(res,200,{ok:true,service:'pocket-monitor',version:2,authenticatedControl:true});
      if(req.method==='GET' && req.url==='/api/metrics')return reply(res,200,{...await collector.collect(null),control:last});
      if(req.method!=='POST'||req.url!=='/api/control')return reply(res,404,{ok:false});
      const received=Buffer.from(req.headers.authorization||''); const expected=Buffer.from('Bearer '+token);
      if(received.length!==expected.length||!crypto.timingSafeEqual(received,expected))return reply(res,401,{ok:false,error:'unauthorized'});
      let body='';for await(const chunk of req){body+=chunk;if(body.length>1024)return reply(res,413,{ok:false});}
      let input;try{input=JSON.parse(body);}catch{return reply(res,400,{ok:false,error:'bad_json'});}
      const {action,requestId}=input;
      if(!['single','extend','sleep','shutdown'].includes(action)||typeof requestId!=='string'||!/^[a-zA-Z0-9_-]{8,64}$/.test(requestId))return reply(res,400,{ok:false,error:'invalid_action'});
      if(jobs.has(requestId)){const old=jobs.get(requestId);return reply(res,old.action===action?200:409,old);}
      if(pending)return reply(res,409,{ok:false,error:'busy'});
      const job={ok:true,action,requestId,state:'pending'};pending=job;last=job;jobs.set(requestId,job);
      if(jobs.size>128)jobs.delete(jobs.keys().next().value);
      reply(res,202,job);
      setTimeout(async()=>{
        try{job.result=await actions[action]();job.state='completed';}
        catch(_){job.ok=false;job.state='failed';job.error='Windows action failed';}
        finally{pending=null;}
      },250);
    } catch(_){if(!res.headersSent)reply(res,503,{ok:false,error:'service_unavailable'});}
  });
}
module.exports={createService};
if(require.main===module)createService().listen(8788,'127.0.0.1');
