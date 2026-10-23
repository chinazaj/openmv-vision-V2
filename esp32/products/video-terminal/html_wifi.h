#ifndef OPENMV_HTML_WIFI_H
#define OPENMV_HTML_WIFI_H
static const char HTML_WIFI_BODY[] = R"HTML(<div class='card'>
<h2>Wi-Fi 网络配置</h2>
<p>配置设备要连接的无线局域网：</p>
<form action='/save_wifi' method='POST'>
<label>Wi-Fi 名称 (SSID):</label><input type='text' name='ssid' placeholder='输入 Wi-Fi 名称' required>
<label>Wi-Fi 密码:</label><input type='password' name='pass' placeholder='输入 Wi-Fi 密码' required>
<input type='submit' class='btn' value='保存配置并重启'>
</form>
<form action='/clear_wifi' method='POST' style='margin-top:15px' onsubmit='return confirm("确定清除Wi-Fi配置吗？");'>
<input type='submit' class='btn btn-red' value='清除预存 Wi-Fi 信息'>
</form>
<a href='/' class='btn btn-gray'>返回控制界面</a>
</div>)HTML";
#endif
