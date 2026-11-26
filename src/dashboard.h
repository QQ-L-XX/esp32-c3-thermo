#pragma once
static const char dashboard[] = R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>家里的温湿度</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#102c30;color:#e9f5f2;font:16px/1.45 system-ui,sans-serif}main{max-width:680px;margin:auto;padding:24px 16px 40px}h1{font-size:1.6rem;margin:0}h2{font-size:1.2rem;margin:26px 0 4px}p{margin:4px 0 12px;color:#bad4ce;font-size:.9rem}.current{display:grid;grid-template-columns:1fr 1fr;gap:10px}article,section{background:#193b40;border-radius:14px;padding:16px;margin-top:10px}strong{font-size:clamp(2rem,8vw,3.3rem);font-variant-numeric:tabular-nums}small{color:#bcd4cf}h3{margin:0;font-size:1rem}.range{display:flex;gap:16px;flex-wrap:wrap;font-size:.94rem;margin:6px 0 10px}.chart{display:block;width:100%;height:auto}svg text{fill:#bed6d1;font:13px system-ui}svg line{stroke:#406165}.temperature{color:#ffc28b}.humidity{color:#7ee1d6}
</style><main><h1>家里的温湿度</h1><p id="live" role="status">正在读取…</p>
<div class="current"><article>温度<br><strong id="t" class="temperature">—</strong> °C</article><article>相对湿度<br><strong id="h" class="humidity">—</strong> %</article></div>
<h2>最近 24 小时</h2><p id="summary">正在读取历史记录…</p>
<section><h3>温度趋势 · °C</h3><div id="tr" class="range">最低 —　最高 —</div><svg id="tc" class="chart" viewBox="0 0 360 130" role="img" aria-label="过去24小时温度曲线"></svg></section>
<section><h3>湿度趋势 · %</h3><div id="hr" class="range">最低 —　最高 —</div><svg id="hc" class="chart" viewBox="0 0 360 130" role="img" aria-label="过去24小时湿度曲线"></svg></section>
<p>每 5 分钟取一次有效读数，最高、最低按图中记录统计。设备重启后重新记录。</p></main><script>
const el=id=>document.getElementById(id);
async function read(path){
 const ctrl=new AbortController(), timer=setTimeout(()=>ctrl.abort(),7000);
 try{const r=await fetch(path,{cache:'no-store',signal:ctrl.signal});if(!r.ok)throw Error();return await r.json();}
 finally{clearTimeout(timer);}
}
async function live(){
 try{const d=await read('/api/latest');el('t').textContent=d.temperature_c?.toFixed(1)??'—';el('h').textContent=d.humidity_pct?.toFixed(1)??'—';el('live').textContent=d.online?'实时读数已更新':d.status==='waiting'?'等待首次采样':'传感器暂无有效读数';}
 catch{el('t').textContent=el('h').textContent='—';el('live').textContent='与温湿度计的连接中断';}
 finally{setTimeout(live,2500);}
}
function plot(points,col,rangeId,chartId,unit,color){
 const values=points.map(row=>row[col]), min=Math.min(...values), max=Math.max(...values);
 const range=el(rangeId);
 range.textContent=values.length?'最低 '+min.toFixed(1)+unit+'　　最高 '+max.toFixed(1)+unit:'最低 —　　最高 —';
 const lo=values.length?Math.floor(min)-1:0, hi=values.length?Math.ceil(max)+1:1;
 const x=age=>42+312*(1-age/86400), y=v=>98-82*(v-lo)/(hi-lo);
 let svg='';
 for(const v of [lo,(lo+hi)/2,hi]){
   svg+='<line x1="42" x2="354" y1="'+y(v)+'" y2="'+y(v)+'"/>';
   if(values.length)svg+='<text x="36" y="'+(y(v)+4)+'" text-anchor="end">'+v+'</text>';
 }
 for(const [age,label,align] of [[86400,'24小时前','start'],[43200,'12小时前','middle'],[0,'现在','end']]){
   svg+='<text x="'+x(age)+'" y="124" text-anchor="'+align+'">'+label+'</text>';
 }
 let line='';
 points.forEach((p,i)=>{
   const connected=i>0 && p[1]===points[i-1][1]+1;
   const X=x(p[0]).toFixed(2), Y=y(p[col]).toFixed(2);
   line+=(connected?'L':'M')+X+' '+Y+' ';
   if(!connected || i===points.length-1)svg+='<circle cx="'+X+'" cy="'+Y+'" r="2.6" fill="'+color+'"/>';
 });
 el(chartId).innerHTML=svg+'<path d="'+line+'" fill="none" stroke="'+color+'" stroke-width="2" stroke-linejoin="round"/>';
}
async function refreshHistory(){
 try{const {points}=await read('/api/history');plot(points,2,'tr','tc',' °C','#ffc28b');plot(points,3,'hr','hc',' %','#7ee1d6');
 el('summary').textContent=points.length?'已记录 '+points.length+' 个采样点':'正在等待第一笔有效记录';}
 catch{el('summary').textContent='历史记录更新失败，稍后自动重试';}
 finally{setTimeout(refreshHistory,15000);}
}
live();refreshHistory();</script></html>)HTML";
