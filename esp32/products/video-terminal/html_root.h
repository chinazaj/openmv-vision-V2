#ifndef OPENMV_VIDEO_TERMINAL_HTML_ROOT_H
#define OPENMV_VIDEO_TERMINAL_HTML_ROOT_H

// Minimal product landing page. Keep UI markup out of the .ino file.
static const char HTML_ROOT_PAGE[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="color-scheme" content="light">
  <title>OpenMV Video Terminal</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 560px; margin: 48px auto; padding: 0 20px; color: #222; }
    h1 { font-size: 28px; text-align: center; margin-bottom: 12px; }
    .ip { text-align: center; color: #555; margin: 0 0 32px; overflow-wrap: anywhere; }
    a { display: block; box-sizing: border-box; margin: 14px 0; padding: 14px 18px; border-radius: 8px; background: #1769d2; color: #fff; text-align: center; text-decoration: none; font-size: 16px; font-weight: 600; }
    a:focus, a:hover { background: #0e55b5; }
  </style>
</head>
<body>
  <h1>OpenMV Video Terminal</h1>
  <p class="ip">Current IP: <strong>{{IP_ADDRESS}}</strong></p>
  <a href="/wifi">Wi-Fi Configuration</a>
  <a href="/video" target="_blank" rel="noopener noreferrer">View Video</a>
</body>
</html>
)HTML";

#endif
