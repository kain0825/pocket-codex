"use strict";
const os = require("os");
const path = require("path");
const {execFile} = require("child_process");

function numeric(value) {
  if (value === null || value === undefined || String(value).trim() === "") return null;
  const n = Number(value); return Number.isFinite(n) ? n : null;
}
function cpuTicks(cpus) {
  return cpus.reduce((sum,cpu)=>({idle:sum.idle+cpu.times.idle,
    total:sum.total+Object.values(cpu.times).reduce((a,b)=>a+b,0)}),{idle:0,total:0});
}
function cpuLoad(before,after) {
  const total=after.total-before.total, idle=after.idle-before.idle;
  return total>0 && idle>=0 ? Math.max(0,Math.min(100,Math.round((1-idle/total)*100))) : null;
}
function parseGpu(raw) {
  // This console displays the first NVIDIA adapter; do not mix unrelated GPUs.
  const row=String(raw).trim().split(/\r?\n/)[0].split(",").map(x=>x.trim());
  if(row.length<6) throw new Error("NVIDIA response is incomplete");
  const [name,usage,used,total,temp,power]=row;
  const percent=numeric(usage), usedMb=numeric(used), totalMb=numeric(total);
  if(percent===null || usedMb===null || totalMb===null || percent<0 || percent>100 || totalMb<=0) throw new Error("NVIDIA sample unavailable");
  return {name,usagePercent:percent,usageKind:"compute",usedVramBytes:usedMb*1024*1024,
    totalVramBytes:totalMb*1024*1024,temperatureC:numeric(temp),powerWatts:numeric(power)};
}
function createCollector(options={}) {
  const clock=options.now || Date.now, host=options.os || os, execute=options.execFile || execFile;
  const executable=options.nvidiaPath || path.join(process.env.SystemRoot || "C:\\Windows","System32","nvidia-smi.exe");
  let ticks=cpuTicks(host.cpus()), cpuValue=null, cpuAt=null, gpu=null, gpuAt=null;
  let gpuError=null, pending=null, lastAttempt=null;
  function sampleCpu() {
    const next=cpuTicks(host.cpus()); cpuValue=cpuLoad(ticks,next); ticks=next; cpuAt=clock();
  }
  const timer=options.manual ? null : setInterval(sampleCpu,1000);
  if(timer) timer.unref();
  function readGpu() {
    if(pending) return pending;
    if(lastAttempt!==null && clock()-lastAttempt<1000) return Promise.resolve();
    lastAttempt=clock();
    pending=new Promise(resolve=>{
      execute(executable,["--query-gpu=name,utilization.gpu,memory.used,memory.total,temperature.gpu,power.draw","--format=csv,noheader,nounits"],
        {windowsHide:true,timeout:2500,maxBuffer:64*1024},(error,stdout)=>{
          try {
            if(error) throw new Error("NVIDIA read failed");
            gpu=parseGpu(stdout); gpuAt=clock(); gpuError=null;
          } catch(e) {gpuError=e.message;}
          resolve();
        });
    }).finally(()=>{pending=null;});
    return pending;
  }
  async function collect(cached=null) {
    await readGpu();
    const now=clock(), total=host.totalmem(), used=total-host.freemem();
    return {
      source:"windows-live + nvidia-smi"+(cached ? " + mythcool-cpu" : ""),updatedAt:new Date(now).toISOString(),
      cpu:{frequencyMhz:cached?.cpu?.frequencyMhz ?? null,temperatureC:cached?.cpu?.temperatureC ?? null,
        powerWatts:cached?.cpu?.powerWatts ?? null,usagePercent:cpuValue},
      memory:{usedBytes:used,totalBytes:total,usagePercent:Math.round(used/total*100)},
      gpu:gpu || {usageKind:"compute",usagePercent:null,usedVramBytes:null,totalVramBytes:null,temperatureC:null,powerWatts:null},
      diagnostics:{...(cached?.diagnostics || {}),liveSampling:true,
        cpuUsageUpdatedAt:cpuAt===null ? null : new Date(cpuAt).toISOString(),cpuUsageAgeMs:cpuAt===null ? null : now-cpuAt,
        gpuSource:"nvidia-smi",gpuUpdatedAt:gpuAt===null ? null : new Date(gpuAt).toISOString(),
        gpuAgeMs:gpuAt===null ? null : now-gpuAt,gpuError,
        cpuSensorAgeMs:cached?.diagnostics?.mythCoolAgeMs ?? null,
        cpuSensorAgeBasis:cached ? "cache-file-mtime" : "unavailable"}
    };
  }
  return {collect,sampleCpu,close:()=>{if(timer)clearInterval(timer);}};
}
module.exports={createCollector,cpuTicks,cpuLoad,parseGpu};
