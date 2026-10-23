#ifndef OPENMV_HTML_COMMON_H
#define OPENMV_HTML_COMMON_H

// ESP-IDF's esp_http_server.h defines HTTP_ANY as an integer wildcard.
// Arduino WebServer expects HTTP_ANY to be an HTTPMethod. Restore the
// WebServer definition after the ESP-IDF header has been included.
#ifdef HTTP_ANY
#undef HTTP_ANY
#endif
#define HTTP_ANY (HTTPMethod)(255)

static const char HTML_HEAD[] = R"HTML(<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no'><style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;margin:0;padding:16px;background:#f2f2f7;color:#1c1c1e;}.card{background:#fff;padding:20px;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,0.06);margin-bottom:16px;}h2{margin-top:0;font-size:20px;color:#000;}p{font-size:14px;color:#666;line-height:1.5;margin:8px 0;}b{color:#007aff;}.btn{display:block;width:100%;padding:12px 0;background:#007aff;color:#fff;text-align:center;border-radius:8px;text-decoration:none;font-weight:600;margin-top:10px;border:none;font-size:16px;box-sizing:border-box;cursor:pointer;}.btn-red{background:#ff3b30;}.btn-green{background:#34c759;}.btn-purple{background:#af52de;}.btn-orange{background:#ff9500;}.btn-disabled{background:#c7c7cc!important;color:#777!important;pointer-events:none;opacity:.75;}.btn-small{font-size:13px;padding:9px 0;}.btn-group{display:flex;gap:10px;}.btn-group a{flex:1;}input[type=text],input[type=password]{width:100%;padding:12px;margin:8px 0 16px;border:1px solid #c7c7cc;border-radius:8px;box-sizing:border-box;font-size:16px;background:#f9f9f9;}</style></head><body>)HTML";
#endif
