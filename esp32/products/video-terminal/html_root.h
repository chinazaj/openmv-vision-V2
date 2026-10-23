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
static const char HTML_ROOT_RELEASE_BODY[] = R"HTML(<div class='card'>
<h2>ESP32-C3 控制终端 V3.3</h2>
<p>网络模式: <b id='net_mode'>{{NETWORK_MODE}}</b></p>
<p>IP 地址: <b>{{IP_ADDRESS}}</b></p>
<p>视频状态: <b id='video_state'>{{VIDEO_STATE}}</b></p>
</div>
<div class='card'>
<a id='watch_btn' href='http://openmv-wifi.local:81/stream' target='_blank' rel='noopener noreferrer' class='btn btn-green'>观看视频</a>
<a href='/config' class='btn btn-purple'>设备配置</a>
</div>
<div class='card'><h2>网络设置</h2><a href='/wifi' class='btn btn-purple'>Wi-Fi 配网</a></div>)HTML";
#endif
