#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_timer.h>
#include <Preferences.h>
#include <time.h>
#include "sharing.h"


namespace {
struct Sample {float temperature, humidity; int64_t acquired; bool ok;};
QueueHandle_t samples;
WebServer portal(80);
Preferences prefs;
String ssid, pass, setupName, setupPassword, hostName;
bool setupActive=false, trying=false;
uint32_t lastAttempt=0;
const char* page=R"HTML(<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>温湿度计配网</title><style>body{max-width:480px;margin:12vh auto;padding:24px;font:18px system-ui;color:#163d44}input,button{font:inherit;padding:12px;width:100%;box-sizing:border-box;margin:8px 0 18px}button{background:#14585b;color:white;border:0;border-radius:8px}</style><h1>温湿度计配网</h1><form method="post" action="/connect"><label>家里 2.4 GHz Wi-Fi 名称<input required name="ssid" maxlength="32"></label><label>Wi-Fi 密码<input type="password" name="pass" maxlength="64"></label><button>连接家里的 Wi-Fi</button></form></html>)HTML";
const char* dashboard=R"HTML(<!doctype html><html lang="zh-CN"><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>家里的温湿度</title><style>body{max-width:30rem;margin:10vh auto;padding:20px;background:#12343a;color:#eef9f6;font:18px system-ui}section{padding:20px;margin:14px 0;background:#205056;border-radius:14px}b{font-size:3rem}p{color:#bce4de}</style><h1>家里的温湿度</h1><section>温度<br><b id="t">—</b> °C</section><section>相对湿度<br><b id="h">—</b> %</section><p id="status" role="status">正在读取…</p><script>
const el=id=>document.getElementById(id);
async function update(){
 const ctrl=new AbortController(),timer=setTimeout(()=>ctrl.abort(),4000);
 try{const r=await fetch('/api/latest',{signal:ctrl.signal,cache:'no-store'}),d=await r.json();
 el('t').textContent=d.temperature_c?.toFixed(1)??'—';el('h').textContent=d.humidity_pct?.toFixed(1)??'—';
 el('status').textContent=d.online?'刚刚更新':d.status==='waiting'?'等待首次采样':'传感器暂无有效读数';
 }catch{el('t').textContent=el('h').textContent='—';el('status').textContent='与温湿度计的连接中断';}
 finally{clearTimeout(timer);setTimeout(update,2500);}
}update();</script></html>)HTML";
void sendLatest(){
  Sample s{};
  const bool sampled=xQueuePeek(samples,&s,0)==pdTRUE;
  const int64_t age=sampled?(esp_timer_get_time()-s.acquired)/1000:0;
  const bool valid=sampled && s.ok && age<10000;
  char body[180];
  if(valid){
    snprintf(body,sizeof(body),"{\"temperature_c\":%.1f,\"humidity_pct\":%.1f,\"online\":true,\"status\":\"ok\",\"age_ms\":%lld}",s.temperature,s.humidity,(long long)age);
  }else{
    snprintf(body,sizeof(body),"{\"temperature_c\":null,\"humidity_pct\":null,\"online\":false,\"status\":\"%s\",\"age_ms\":null}",!sampled?"waiting":s.ok?"stale":"sensor_error");
  }
  portal.sendHeader("Cache-Control","no-store");
  portal.send(200,"application/json",body);
}
void connectHome(){
  WiFi.mode(setupActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(ssid.c_str(),pass.c_str());
  lastAttempt=millis();
}
void openPortal(){
  WiFi.mode(ssid.isEmpty() ? WIFI_AP : WIFI_AP_STA);
  delay(120);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  if(!WiFi.softAP(setupName.c_str(),setupPassword.c_str())){Serial.println("SETUP hotspot failed");return;}
  Serial.printf("SETUP hotspot started on %s\n", WiFi.softAPIP().toString().c_str());
  setupActive=true;
}
void configureRoutes(){
  portal.on("/", HTTP_GET, []{
    portal.sendHeader("Cache-Control","no-store");
    portal.send(200,"text/html;charset=utf-8",setupActive && portal.client().localIP()==WiFi.softAPIP() ? page : dashboard);
  });
  portal.on("/api/latest", HTTP_GET, sendLatest);
  portal.on("/connect",HTTP_POST,[]{
    // Only the setup hotspot accepts network credentials.
    if(!setupActive || portal.client().localIP()!=WiFi.softAPIP()){portal.send(403);return;}
    String name=portal.arg("ssid"),secret=portal.arg("pass");
    if(name.isEmpty()||name.length()>32||secret.length()>64){portal.send(400);return;}
    ssid=name;pass=secret;trying=true;
    portal.send(200,"text/plain;charset=utf-8","正在连接。成功后配网热点会关闭，请让手机连回家里的 Wi-Fi，再打开温湿度计局域网页面。如果热点一直在，请返回重新输入。");
    connectHome();
  });

}
void networkTask(void*){
  prefs.begin("climatewifi",false);
  uint8_t mac[6];
  esp_read_mac(mac,ESP_MAC_WIFI_STA);
  char suffix[5];
  snprintf(suffix,sizeof(suffix),"%02x%02x",mac[4],mac[5]);
  hostName=String("thermo-")+suffix;
  setupName=String("Thermo-Setup-")+suffix;
  setupPassword=prefs.getString("setupKey","");
  if(setupPassword.length()!=16){
    WiFi.mode(WIFI_STA);
    char generated[17];
    snprintf(generated,sizeof(generated),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
    setupPassword=generated;
    prefs.putString("setupKey",setupPassword);
  }
  ssid=prefs.getString("ssid","");pass=prefs.getString("pass","");
  WiFi.persistent(false);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info){Serial.printf("WiFi disconnect reason=%u\n",info.wifi_sta_disconnected.reason);},ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if(ssid.isEmpty())openPortal();else connectHome();
  configureRoutes();
  portal.begin();
  bool announced=false;
  uint32_t diagnostic=0;
  uint32_t offlineSince=millis();
  for(;;){
    if(millis()-diagnostic>=5000){
      diagnostic=millis();
      if(setupActive)Serial.printf("Setup Wi-Fi: %s  Password: %s\n",setupName.c_str(),setupPassword.c_str());
      Serial.printf("WIFI mode=%d ap=%d clients=%d sta=%d ip=%s heap=%u\n", WiFi.getMode(),setupActive,setupActive?WiFi.softAPgetStationNum():0,WiFi.status(),WiFi.localIP().toString().c_str(),ESP.getFreeHeap());
    }
    portal.handleClient();
    if(WiFi.status()!=WL_CONNECTED){
      if(announced){MDNS.end();announced=false;}
      if(!setupActive && (ssid.isEmpty() || millis()-offlineSince>90000) && millis()-lastAttempt>5000){openPortal();lastAttempt=millis();}
      if(ssid.length() && millis()-lastAttempt>30000)connectHome();
    }else{
      offlineSince=millis();

      if(setupActive && (trying || ssid.length())){
        if(trying){prefs.putString("ssid",ssid);prefs.putString("pass",pass);trying=false;}
        WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);setupActive=false;
      }
      if(!announced){
        announced=true;
        if(MDNS.begin(hostName.c_str()))MDNS.addService("http","tcp",80);
        Serial.printf("Local name: %s.local\n",hostName.c_str());
        Serial.printf("LAN http://%s/api/latest\n",WiFi.localIP().toString().c_str());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}
}
void startSharing(){
  samples=xQueueCreate(1,sizeof(Sample));
  if(!samples || xTaskCreate(networkTask,"climate-net",8192,nullptr,1,nullptr)!=pdPASS)Serial.println("WIFI task allocation failed");
}
void queueSample(float temperature,float humidity){
  Sample s{temperature,humidity,esp_timer_get_time(),true};
  if(samples)xQueueOverwrite(samples,&s);
}

void markSampleFailed(){
  Sample s{0,0,esp_timer_get_time(),false};
  if(samples)xQueueOverwrite(samples,&s);
}
