#ifndef OPENMV_HTML_ROOT_H
#define OPENMV_HTML_ROOT_H
// Dynamic values are replaced by handle_root(); routes and API contract stay in the sketch.
static const char HTML_ROOT_DEV_BODY[] = R"HTML(<div class='card'>
<h2>ESP32-C3 控制终端 V3.3</h2>
<p>网络模式: <b id='net_mode'>{{NETWORK_MODE}}</b></p>
<p>IP 地址: <b>{{IP_ADDRESS}}</b></p>
<p>视频状态: <b id='video_state'>{{VIDEO_STATE}}</b></p>
<p>LED: <b id='led_state'>{{LED_STATE}}</b></p>
<p>接收成功帧: <b id='rx_ok_frames'>{{RX_OK_FRAMES}}</b></p>
<p>CRC/协议错误: <b id='rx_bad_frames'>{{RX_BAD_FRAMES}}</b></p>
<p>丢弃旧帧: <b id='rx_dropped_frames'>{{RX_DROPPED_FRAMES}}</b></p>
<p>HTTP发送帧: <b id='http_sent_frames'>{{HTTP_SENT_FRAMES}}</b></p>
<p>HTTP发送错误: <b id='http_send_errors'>{{HTTP_SEND_ERRORS}}</b></p>
<p>视频重连次数: <b id='http_stream_reconnects'>{{HTTP_STREAM_RECONNECTS}}</b></p>
</div>
<div class='card'><h2>视频流控制</h2><div class='btn-group'>
<a id='start_btn' href='/start' class='btn {{START_CLASS}}' aria-disabled='{{START_DISABLED}}'>开启视频</a>
<a id='stop_btn' href='/stop' class='btn btn-red {{STOP_CLASS}}' aria-disabled='{{STOP_DISABLED}}'>停止视频</a></div>
<a id='watch_btn' href='http://openmv-wifi.local:81/stream' target='_blank' rel='noopener noreferrer' class='btn btn-green'>观看视频</a>
<a href='/toggle_led' class='btn btn-orange'>切换指示灯（开发）</a>
<a href='/config' class='btn btn-purple'>设备配置</a></div>
<div class='card'><h2>网络设置</h2><a href='/wifi' class='btn btn-purple'>Wi-Fi 配网</a></div>
<script>
let currentVideoActive = {{VIDEO_ACTIVE}};
function setVideoButtons(active){
  currentVideoActive=!!active;
  const start=document.getElementById('start_btn'),stop=document.getElementById('stop_btn');
  start.classList.toggle('btn-disabled',currentVideoActive);
  stop.classList.toggle('btn-disabled',!currentVideoActive);
  start.setAttribute('aria-disabled',currentVideoActive?'true':'false');
  stop.setAttribute('aria-disabled',!currentVideoActive?'true':'false');
}
document.getElementById('start_btn').addEventListener('click',function(e){if(currentVideoActive){e.preventDefault();e.stopPropagation();}});
document.getElementById('stop_btn').addEventListener('click',function(e){if(!currentVideoActive){e.preventDefault();e.stopPropagation();}});
async function updateStatus(){
  try{
    const r=await fetch('/api/status?t='+Date.now(),{cache:'no-store'});
    if(!r.ok)throw new Error('status HTTP '+r.status);
    const s=await r.json();
    const ids=['rx_ok_frames','rx_bad_frames','rx_dropped_frames','http_sent_frames','http_send_errors','http_stream_reconnects'];
    ids.forEach(id=>{const el=document.getElementById(id);if(el&&s[id]!==undefined)el.textContent=s[id];});
    document.getElementById('video_state').textContent=s.video_active?'传输中':'已停止';
    document.getElementById('led_state').textContent=s.led_enabled?'开启':'关闭';
    setVideoButtons(s.video_active);
  }catch(e){/* Keep the last status and retry next second. */}
}
setVideoButtons(currentVideoActive);updateStatus();setInterval(updateStatus,1000);
</script>)HTML";
static const char HTML_ROOT_RELEASE_BODY[] = R"HTML(
<div class='card'>
<h2>ESP32-C3 视频终端</h2>
<p>网络模式：<b id='net_mode'>{{NETWORK_MODE}}</b></p>
<p>IP 地址：<b>{{IP_ADDRESS}}</b></p>
<p>OpenMV 视频状态：<b id='video_state'>{{VIDEO_STATE}}</b></p>
<p>Wi-Fi 信号：<b id='wifi_rssi'>--</b></p>
<p>运行时间：<b id='uptime'>--</b></p>
<a id='watch_btn' href='http://openmv-wifi.local:81/stream' target='_blank' rel='noopener noreferrer' class='btn btn-green'>观看实时视频</a>
</div>
<div class='card'>
<h2>视频接收状态</h2>
<div class='stats-grid'>
<div class='stat'><span>成功帧</span><b id='rx_ok_frames'>--</b></div>
<div class='stat'><span>当前帧率</span><b id='rx_fps'>--</b></div>
<div class='stat'><span>协议/CRC错误</span><b id='rx_bad_frames'>--</b></div>
<div class='stat'><span>丢弃帧</span><b id='rx_dropped_frames'>--</b></div>
<div class='stat'><span>SPI接收超时</span><b id='rx_timeout_count'>--</b></div>
<div class='stat'><span>帧序号缺口</span><b id='rx_frame_id_gap_count'>--</b></div>
<div class='stat'><span>超长帧</span><b id='rx_oversize_count'>--</b></div>
<div class='stat'><span>最近一帧距今</span><b id='last_frame_age'>--</b></div>
</div>
<p class='hint'>数据每秒自动刷新。成功帧表示 ESP32 已完成接收、组帧和校验；丢弃帧表示最新帧优先策略主动丢弃旧帧。</p>
</div>
<div class='card'>
<h2>设备资源</h2>
<p>当前可用堆内存：<b id='free_heap'>--</b></p>
<p>启动以来最低可用堆内存：<b id='min_free_heap'>--</b></p>
<p>Wi-Fi 客户端数：<b id='wifi_clients'>--</b></p>
<p id='status_updated' class='hint'>等待状态数据...</p>
</div>
<div class='card'>
<h2>设备功能</h2>
<a href='/config' class='btn btn-purple'>设备配置</a>
<a href='/wifi' class='btn btn-purple'>Wi-Fi 配网</a>
</div>
<style>
.stats-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:12px}
.stat{background:#f2f2f7;border-radius:9px;padding:12px;min-width:0}
.stat span{display:block;font-size:12px;color:#666;margin-bottom:6px}
.stat b{font-size:20px;overflow-wrap:anywhere}
.hint{font-size:12px;color:#8e8e93}
</style>
<script>
let previousRxFrames=null,previousRxAt=0;
const fmtBytes=n=>{if(n===undefined||n===null)return '--';return (n/1024).toFixed(1)+' KB';};
const fmtDuration=s=>{s=Math.max(0,Number(s)||0);const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;return h+'h '+m+'m '+sec+'s';};
async function updateStatus(){
  try{
    const r=await fetch('/api/status?t='+Date.now(),{cache:'no-store'});
    if(!r.ok)throw new Error('HTTP '+r.status);
    const s=await r.json();
    const put=(id,value)=>{const el=document.getElementById(id);if(el)el.textContent=value;};
    put('video_state',s.video_active?'运行中':'已停止');
    put('wifi_rssi',s.wifi_mode==='STA'?(s.wifi_rssi+' dBm'):'AP模式');
    put('uptime',fmtDuration(s.uptime_s));
    put('rx_ok_frames',s.rx_ok_frames);
    put('rx_bad_frames',s.rx_bad_frames);
    put('rx_dropped_frames',s.rx_dropped_frames);
    put('rx_timeout_count',s.rx_timeout_count);
    put('rx_frame_id_gap_count',s.rx_frame_id_gap_count);
    put('rx_oversize_count',s.rx_oversize_count);
    put('free_heap',fmtBytes(s.free_heap));
    put('min_free_heap',fmtBytes(s.min_free_heap));
    put('wifi_clients',s.wifi_clients);
    put('last_frame_age',s.last_frame_age_ms===null?'尚未收到':(s.last_frame_age_ms+' ms'));
    const now=Date.now();
    if(previousRxFrames!==null&&previousRxAt>0&&now>previousRxAt){
      const fps=(s.rx_ok_frames-previousRxFrames)*1000/(now-previousRxAt);
      put('rx_fps',Math.max(0,fps).toFixed(1)+' fps');
    }else{put('rx_fps','计算中');}
    previousRxFrames=s.rx_ok_frames;previousRxAt=now;
    put('status_updated','最后更新：'+new Date().toLocaleTimeString());
  }catch(e){putStatusError();}
}
function putStatusError(){const el=document.getElementById('status_updated');if(el)el.textContent='状态读取失败，正在重试...';}
updateStatus();setInterval(updateStatus,1000);
</script>
)HTML";
#endif
