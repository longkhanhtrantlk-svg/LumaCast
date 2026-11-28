#include "ControlPage.h"
#include "core/I18n.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace lc {

namespace {
// Chu cua trang nam o controlPageHtml(): %LANG%, %TITLE%... la cho dien chu theo ngon ngu giao dien; T la bang chu cho JavaScript.
const char* const kPage = R"HTML(<!doctype html>
<html lang="%LANG%"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LumaCast — %TITLE%</title>
<style>
:root{color-scheme:dark;--bg:#12141a;--card:#1b1e27;--fg:#e6e8ee;--mut:#9aa0b4;--pgm:#e5322d;--pvw:#2fbf71;--acc:#2f7de1}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px system-ui,sans-serif;padding:12px;max-width:760px;margin:auto}
h1{font-size:18px;margin:0 0 8px}h2{font-size:15px;margin:14px 0 6px;color:var(--mut)}
.card{background:var(--card);border-radius:10px;padding:10px;margin-bottom:10px}
button{background:#2a2e3b;color:var(--fg);border:1px solid #3a3f50;border-radius:8px;padding:12px 14px;font-size:15px;min-height:46px}
button:active{background:#394056}button.on{background:var(--acc);border-color:var(--acc)}
button.pgm{background:var(--pgm);border-color:var(--pgm)}button.pvw{border-color:var(--pvw);color:var(--pvw)}
.row{display:flex;flex-wrap:wrap;gap:8px;margin:6px 0}.row>button{flex:1 1 90px}
#st{font-size:13px;color:var(--mut)}#err{color:#ff8a80;min-height:1.2em;font-size:13px}
input{background:#0e1015;color:var(--fg);border:1px solid #3a3f50;border-radius:8px;padding:10px;width:100%}
.lock{color:#f5c518;font-weight:bold}
</style></head><body>
<h1>LumaCast <span id="st">%CONNECTING%</span></h1>
<div id="err"></div>
<div id="auth" class="card" hidden><div>%NEEDTOKEN%</div><input id="tok" type="password" autocomplete="off" placeholder="Token"><div class="row"><button id="tokb">%SIGNIN%</button></div></div>
<div id="app"></div>
<script>
"use strict";
const T=%STRINGS%;
let token=sessionStorage.getItem("lc_token")||"";let ch=0,last=null;
const $=id=>document.getElementById(id);
function hdr(){const h={};if(token)h["Authorization"]="Bearer "+token;return h}
async function api(path,body){
  const h=hdr();if(body!==undefined)h["Content-Type"]="application/json";
  try{const r=await fetch(path,{method:body===undefined?"GET":"POST",headers:h,body:body===undefined?undefined:JSON.stringify(body)});
    if(r.status===401){$("auth").hidden=false;$("err").textContent=T.badToken;return null}
    const j=await r.json();$("err").textContent=j.ok===false?(j.message||j.error):"";return j}
  catch(e){$("err").textContent=T.offline;return null}}
function el(t,txt,cls,fn){const e=document.createElement(t);if(txt)e.textContent=txt;if(cls)e.className=cls;if(fn)e.onclick=fn;return e}
function render(s){last=s;const app=$("app");app.textContent="";
  $("st").textContent=(s.locked?T.locked:"")+s.engine.fps.toFixed(0)+" fps";
  if(ch>=s.channels.length)ch=0;const c=s.channels[ch];
  if(s.channels.length>1){const r=el("div",null,"row");s.channels.forEach((x,i)=>r.append(el("button",T.channel+x.index,i===ch?"on":"",()=>{ch=i;render(last)})));app.append(r)}
  const k=el("div",null,"card");k.append(el("h2",T.channel+c.index+": "+c.name));
  k.append(el("div","PROGRAM: "+(c.programName||"—")+(c.inTransition?T.switching:"")));
  k.append(el("div","PREVIEW: "+(c.previewName||"—")));
  const sc=el("div",null,"row");c.scenes.forEach((n,i)=>sc.append(el("button",(i+1)+" "+n,c.program===i+1?"pgm":c.preview===i+1?"pvw":"",()=>api("/api/channels/"+c.index+"/preview",{scene:i+1}))));k.append(sc);
  const t=el("div",null,"row");
  t.append(el("button","CUT","",()=>api("/api/channels/"+c.index+"/cut",{})));
  t.append(el("button","TAKE","",()=>api("/api/channels/"+c.index+"/take",{type:"fade",duration:1})));
  t.append(el("button","FTB",c.ftb?"on":"",()=>api("/api/channels/"+c.index+"/ftb",{})));k.append(t);
  const a=el("div",null,"row"),vol=c.audio?c.audio.volume:1;
  a.append(el("button",c.audio&&c.audio.mute?T.muted:T.sound,c.audio&&c.audio.mute?"on":"",()=>api("/api/channels/"+c.index+"/mute",{})));
  a.append(el("button","−","",()=>api("/api/channels/"+c.index+"/volume",{volume:Math.max(0,Math.round((vol-0.1)*100)/100)})));
  a.append(el("button",Math.round(vol*100)+"%","",()=>api("/api/channels/"+c.index+"/volume",{volume:1})));
  a.append(el("button","+","",()=>api("/api/channels/"+c.index+"/volume",{volume:Math.min(1.5,Math.round((vol+0.1)*100)/100)})));k.append(a);app.append(k);
  const o=el("div",null,"card");o.append(el("h2","Output"));
  s.outputs.forEach(x=>{o.append(el("div",x.name+" · "+x.width+"×"+x.height+" · "+x.fps+" fps"+(x.health!=="ok"?" · "+x.health:"")));
    const r=el("div",null,"row");
    [["black",T.black],["freeze",T.freeze],["pattern",T.pattern]].forEach(([f,l])=>r.append(el("button",l,x[f]?"on":"",()=>api("/api/outputs/"+x.index+"/"+f,{on:!x[f]}))));o.append(r)});
  o.append(el("div",null,"row")).append(el("button","BLACK ALL",s.outputs.length&&s.outputs.every(x=>x.black)?"on":"",()=>api("/api/black-all",{})));
  app.append(o);
  if(s.locked)app.append(el("div",T.lockNote,"lock"))}
async function stream(){
  for(;;){
    try{const r=await fetch("/api/events",{headers:hdr()});
      if(r.status===401){$("auth").hidden=false;await new Promise(f=>setTimeout(f,2000));continue}
      $("auth").hidden=true;const rd=r.body.getReader(),dec=new TextDecoder();let buf="";
      for(;;){const {done,value}=await rd.read();if(done)break;buf+=dec.decode(value,{stream:true});
        let i;while((i=buf.indexOf("\n\n"))>=0){const blk=buf.slice(0,i);buf=buf.slice(i+2);
          const m=/^data: (.*)$/m.exec(blk);if(m&&/^event: state/m.test(blk)){try{render(JSON.parse(m[1]))}catch(e){}}}}
    }catch(e){$("st").textContent=T.retrying}
    await new Promise(f=>setTimeout(f,2000))}}
$("tokb").onclick=()=>{token=$("tok").value;sessionStorage.setItem("lc_token",token)};
stream();
</script></body></html>
)HTML";
} // namespace

QByteArray controlPageHtml() {
    const QJsonObject strings{
        {"badToken", LC_TR("Cần token hợp lệ")},
        {"offline", LC_TR("Mất kết nối")},
        {"locked", LC_TR("🔒 KHOÁ · ")},
        {"channel", LC_TR("Kênh ")},
        {"switching", LC_TR(" (đang chuyển)")},
        {"muted", LC_TR("🔇 Tắt tiếng")},
        {"sound", LC_TR("🔊 Có tiếng")},
        {"black", LC_TR("Đen")},
        {"freeze", LC_TRC("Dừng", "output freeze")},
        {"pattern", LC_TR("Mẫu")},
        {"lockNote", LC_TR("Máy đang ở chế độ KHOÁ VẬN HÀNH (lệnh live vẫn hoạt động).")},
        {"retrying", LC_TR("mất kết nối, thử lại…")},
    };
    QString js = QString::fromUtf8(QJsonDocument(strings).toJson(QJsonDocument::Compact));
    js.replace("</", "<\\/");                                       // khong de chu nao dong the <script>
    QString html = QString::fromUtf8(kPage);
    html.replace("%LANG%", i18n::languageId(i18n::current()));
    html.replace("%TITLE%", LC_TR("Điều khiển").toHtmlEscaped());
    html.replace("%CONNECTING%", LC_TR("đang kết nối…").toHtmlEscaped());
    html.replace("%NEEDTOKEN%", LC_TR("Cần token để điều khiển:").toHtmlEscaped());
    html.replace("%SIGNIN%", LC_TR("Đăng nhập").toHtmlEscaped());
    html.replace("%STRINGS%", js);                                    // cuoi cung: chu khong bi thay tiep
    return html.toUtf8();
}

} // namespace lc
