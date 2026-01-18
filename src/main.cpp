#include <Arduino.h>
#include <esp_wifi.h>
#include <soc/rtc_cntl_reg.h>
#include <driver/i2c.h>
#include <IotWebConf.h>
#include <IotWebConfTParameter.h>
#include <OV2640.h>
#include <ESPmDNS.h>
#include <rtsp_server.h>
#include <lookup_camera_effect.h>
#include <lookup_camera_frame_size.h>
#include <lookup_camera_gainceiling.h>
#include <lookup_camera_wb_mode.h>
#include <format_duration.h>
#include <format_number.h>
#include <moustache.h>
#include <settings.h>

// HTML files
extern const char index_html_min_start[] asm("_binary_html_index_min_html_start");
extern const char control_html_start[] asm("_binary_html_control_html_start");
extern const char control_html_end[] asm("_binary_html_control_html_end");

auto param_group_camera = iotwebconf::ParameterGroup("camera", "Camera settings");
auto param_frame_duration = iotwebconf::Builder<iotwebconf::UIntTParameter<unsigned long>>("fd").label("Frame duration (ms)").defaultValue(DEFAULT_FRAME_DURATION).min(10).build();
auto param_frame_size = iotwebconf::Builder<iotwebconf::SelectTParameter<sizeof(frame_sizes[0])>>("fs").label("Frame size").optionValues((const char *)&frame_sizes).optionNames((const char *)&frame_sizes).optionCount(sizeof(frame_sizes) / sizeof(frame_sizes[0])).nameLength(sizeof(frame_sizes[0])).defaultValue(DEFAULT_FRAME_SIZE).build();
auto param_jpg_quality = iotwebconf::Builder<iotwebconf::UIntTParameter<byte>>("q").label("JPG quality").defaultValue(DEFAULT_JPEG_QUALITY).min(1).max(100).build();
auto param_brightness = iotwebconf::Builder<iotwebconf::IntTParameter<int>>("b").label("Brightness").defaultValue(DEFAULT_BRIGHTNESS).min(-2).max(2).build();
auto param_contrast = iotwebconf::Builder<iotwebconf::IntTParameter<int>>("c").label("Contrast").defaultValue(DEFAULT_CONTRAST).min(-2).max(2).build();
auto param_saturation = iotwebconf::Builder<iotwebconf::IntTParameter<int>>("s").label("Saturation").defaultValue(DEFAULT_SATURATION).min(-2).max(2).build();
auto param_special_effect = iotwebconf::Builder<iotwebconf::SelectTParameter<sizeof(camera_effects[0])>>("e").label("Effect").optionValues((const char *)&camera_effects).optionNames((const char *)&camera_effects).optionCount(sizeof(camera_effects) / sizeof(camera_effects[0])).nameLength(sizeof(camera_effects[0])).defaultValue(DEFAULT_EFFECT).build();
auto param_whitebal = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("wb").label("White balance").defaultValue(DEFAULT_WHITE_BALANCE).build();
auto param_awb_gain = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("awbg").label("Automatic white balance gain").defaultValue(DEFAULT_WHITE_BALANCE_GAIN).build();
auto param_wb_mode = iotwebconf::Builder<iotwebconf::SelectTParameter<sizeof(camera_wb_modes[0])>>("wbm").label("White balance mode").optionValues((const char *)&camera_wb_modes).optionNames((const char *)&camera_wb_modes).optionCount(sizeof(camera_wb_modes) / sizeof(camera_wb_modes[0])).nameLength(sizeof(camera_wb_modes[0])).defaultValue(DEFAULT_WHITE_BALANCE_MODE).build();
auto param_exposure_ctrl = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("ec").label("Exposure control").defaultValue(DEFAULT_EXPOSURE_CONTROL).build();
auto param_aec2 = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("aec2").label("Auto exposure (dsp)").defaultValue(DEFAULT_AEC2).build();
auto param_ae_level = iotwebconf::Builder<iotwebconf::IntTParameter<int>>("ael").label("Auto Exposure level").defaultValue(DEFAULT_AE_LEVEL).min(-2).max(2).build();
auto param_aec_value = iotwebconf::Builder<iotwebconf::IntTParameter<int>>("aecv").label("Manual exposure value").defaultValue(DEFAULT_AEC_VALUE).min(9).max(1200).build();
auto param_gain_ctrl = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("gc").label("Gain control").defaultValue(DEFAULT_GAIN_CONTROL).build();
auto param_agc_gain = iotwebconf::Builder<iotwebconf::IntTParameter<int>>("agcg").label("AGC gain").defaultValue(DEFAULT_AGC_GAIN).min(0).max(30).build();
auto param_gain_ceiling = iotwebconf::Builder<iotwebconf::SelectTParameter<sizeof(camera_gain_ceilings[0])>>("gcl").label("Auto Gain ceiling").optionValues((const char *)&camera_gain_ceilings).optionNames((const char *)&camera_gain_ceilings).optionCount(sizeof(camera_gain_ceilings) / sizeof(camera_gain_ceilings[0])).nameLength(sizeof(camera_gain_ceilings[0])).defaultValue(DEFAULT_GAIN_CEILING).build();
auto param_bpc = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("bpc").label("Black pixel correct").defaultValue(DEFAULT_BPC).build();
auto param_wpc = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("wpc").label("White pixel correct").defaultValue(DEFAULT_WPC).build();
auto param_raw_gma = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("rg").label("Gamma correct").defaultValue(DEFAULT_RAW_GAMMA).build();
auto param_lenc = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("lenc").label("Lens correction").defaultValue(DEFAULT_LENC).build();
auto param_hmirror = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("hm").label("Horizontal mirror").defaultValue(DEFAULT_HORIZONTAL_MIRROR).build();
auto param_vflip = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("vm").label("Vertical mirror").defaultValue(DEFAULT_VERTICAL_MIRROR).build();
auto param_dcw = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("dcw").label("Downsize enable").defaultValue(DEFAULT_DCW).build();
auto param_colorbar = iotwebconf::Builder<iotwebconf::CheckboxTParameter>("cb").label("Colorbar").defaultValue(DEFAULT_COLORBAR).build();

// Camera
OV2640 cam;
// DNS Server
DNSServer dnsServer;
// RTSP Server
std::unique_ptr<rtsp_server> camera_server;
// Web server
WebServer web_server(80);

auto thingName = String(WIFI_SSID) + "-" + String(ESP.getEfuseMac(), 16);
IotWebConf iotWebConf(thingName.c_str(), &dnsServer, &web_server, WIFI_PASSWORD, CONFIG_VERSION);

// Camera initialization result
esp_err_t camera_init_result;

// Forward declarations
void update_camera_settings();

void handle_root()
{
  log_v("Handle root");
  // Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
    return;

  // Format hostname
  auto hostname = "esp32-" + WiFi.macAddress() + ".local";
  hostname.replace(":", "");
  hostname.toLowerCase();

  // Wifi Modes
  const char *wifi_modes[] = {"NULL", "STA", "AP", "STA+AP"};
  auto ipv4 = WiFi.getMode() == WIFI_MODE_AP ? WiFi.softAPIP() : WiFi.localIP();
  auto ipv6 = WiFi.getMode() == WIFI_MODE_AP ? WiFi.softAPIPv6() : WiFi.localIPv6();

  auto initResult = esp_err_to_name(camera_init_result);
  if (initResult == nullptr)
    initResult = "Unknown reason";

  moustache_variable_t substitutions[] = {
      // Version / CPU
      {"AppTitle", APP_TITLE},
      {"AppVersion", APP_VERSION},
      {"BoardType", BOARD_NAME},
      {"ThingName", iotWebConf.getThingName()},
      {"SDKVersion", ESP.getSdkVersion()},
      {"ChipModel", ESP.getChipModel()},
      {"ChipRevision", String(ESP.getChipRevision())},
      {"CpuFreqMHz", String(ESP.getCpuFreqMHz())},
      {"CpuCores", String(ESP.getChipCores())},
      {"FlashSize", format_memory(ESP.getFlashChipSize(), 0)},
      {"HeapSize", format_memory(ESP.getHeapSize())},
      {"PsRamSize", format_memory(ESP.getPsramSize(), 0)},
      // Diagnostics
      {"Uptime", String(format_duration(millis() / 1000))},
      {"FreeHeap", format_memory(ESP.getFreeHeap())},
      {"MaxAllocHeap", format_memory(ESP.getMaxAllocHeap())},
      {"NumRTSPSessions", camera_server != nullptr ? String(camera_server->num_connected()) : "RTSP server disabled"},
      // Network
      {"HostName", hostname},
      {"MacAddress", WiFi.macAddress()},
      {"AccessPoint", WiFi.SSID()},
      {"SignalStrength", String(WiFi.RSSI())},
      {"WifiMode", wifi_modes[WiFi.getMode()]},
      {"IPv4", ipv4.toString()},
      {"IPv6", ipv6.toString()},
      {"NetworkState.ApMode", String(iotWebConf.getState() == iotwebconf::NetworkState::ApMode)},
      {"NetworkState.OnLine", String(iotWebConf.getState() == iotwebconf::NetworkState::OnLine)},
      // Camera
      {"FrameSize", String(param_frame_size.value())},
      {"FrameDuration", String(param_frame_duration.value())},
      {"FrameFrequency", String(1000.0 / param_frame_duration.value(), 1)},
      {"JpegQuality", String(param_jpg_quality.value())},
      {"CameraInitialized", String(camera_init_result == ESP_OK)},
      {"CameraInitResult", String(camera_init_result)},
      {"CameraInitResultText", initResult},
      // Settings
      {"Brightness", String(param_brightness.value())},
      {"Contrast", String(param_contrast.value())},
      {"Saturation", String(param_saturation.value())},
      {"SpecialEffect", String(param_special_effect.value())},
      {"WhiteBal", String(param_whitebal.value())},
      {"AwbGain", String(param_awb_gain.value())},
      {"WbMode", String(param_wb_mode.value())},
      {"ExposureCtrl", String(param_exposure_ctrl.value())},
      {"Aec2", String(param_aec2.value())},
      {"AeLevel", String(param_ae_level.value())},
      {"AecValue", String(param_aec_value.value())},
      {"GainCtrl", String(param_gain_ctrl.value())},
      {"AgcGain", String(param_agc_gain.value())},
      {"GainCeiling", String(param_gain_ceiling.value())},
      {"Bpc", String(param_bpc.value())},
      {"Wpc", String(param_wpc.value())},
      {"RawGma", String(param_raw_gma.value())},
      {"Lenc", String(param_lenc.value())},
      {"HMirror", String(param_hmirror.value())},
      {"VFlip", String(param_vflip.value())},
      {"Dcw", String(param_dcw.value())},
      {"ColorBar", String(param_colorbar.value())},
      // RTSP
      {"RtspPort", String(RTSP_PORT)}};

  web_server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  auto html = moustache_render(index_html_min_start, substitutions);
  web_server.send(200, "text/html", html);
}

#ifdef FLASH_LED_GPIO
void handle_flash()
{
  log_v("handle_flash");
  // If no value present, use off, otherwise convert v to integer. Depends on analog resolution for max value
  auto v = web_server.hasArg("v") ? web_server.arg("v").toInt() : 0;
  // If conversion fails, v = 0
  analogWrite(FLASH_LED_GPIO, v);

  web_server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  web_server.send(200);
}
#endif

void handle_snapshot()
{
  log_v("handle_snapshot");
  if (camera_init_result != ESP_OK)
  {
    web_server.send(404, "text/plain", "Camera is not initialized");
    return;
  }

  // Capture latest frame (CAMERA_GRAB_LATEST mode already provides freshest frame)
  cam.run();

  auto fb_len = cam.getSize();
  auto fb = (const char *)cam.getfb();
  if (fb == nullptr)
  {
    web_server.send(404, "text/plain", "Unable to obtain frame buffer from the camera");
    return;
  }

  web_server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  web_server.setContentLength(fb_len);
  web_server.send(200, "image/jpeg", "");
  web_server.sendContent(fb, fb_len);
}

#define STREAM_CONTENT_BOUNDARY "123456789000000000000987654321"

void handle_stream()
{
  log_v("handle_stream");
  if (camera_init_result != ESP_OK)
  {
    web_server.send(404, "text/plain", "Camera is not initialized");
    return;
  }

  log_v("starting streaming");
  // Blocks further handling of HTTP server until stopped
  char size_buf[12];
  auto client = web_server.client();
  client.write("HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: multipart/x-mixed-replace; boundary=" STREAM_CONTENT_BOUNDARY "\r\n");
  while (client.connected())
  {
    client.write("\r\n--" STREAM_CONTENT_BOUNDARY "\r\n");
    cam.run();
    client.write("Content-Type: image/jpeg\r\nContent-Length: ");
    sprintf(size_buf, "%d\r\n\r\n", cam.getSize());
    client.write(size_buf);
    client.write(cam.getfb(), cam.getSize());
  }

  log_v("client disconnected");
  client.stop();
  log_v("stopped streaming");
}

// JSON API for single-page settings interface
void handle_api_get_settings()
{
  log_v("handle_api_get_settings");

  String json = "{";
  json += "\"fd\":" + String(param_frame_duration.value()) + ",";
  json += "\"fs\":\"" + String(param_frame_size.value()) + "\",";
  json += "\"q\":" + String(param_jpg_quality.value()) + ",";
  json += "\"b\":" + String(param_brightness.value()) + ",";
  json += "\"c\":" + String(param_contrast.value()) + ",";
  json += "\"s\":" + String(param_saturation.value()) + ",";
  json += "\"e\":\"" + String(param_special_effect.value()) + "\",";
  json += "\"wb\":" + String(param_whitebal.value() ? 1 : 0) + ",";
  json += "\"awbg\":" + String(param_awb_gain.value() ? 1 : 0) + ",";
  json += "\"wbm\":\"" + String(param_wb_mode.value()) + "\",";
  json += "\"ec\":" + String(param_exposure_ctrl.value() ? 1 : 0) + ",";
  json += "\"aec2\":" + String(param_aec2.value() ? 1 : 0) + ",";
  json += "\"ael\":" + String(param_ae_level.value()) + ",";
  json += "\"aecv\":" + String(param_aec_value.value()) + ",";
  json += "\"gc\":" + String(param_gain_ctrl.value() ? 1 : 0) + ",";
  json += "\"agcg\":" + String(param_agc_gain.value()) + ",";
  json += "\"gcl\":\"" + String(param_gain_ceiling.value()) + "\",";
  json += "\"bpc\":" + String(param_bpc.value() ? 1 : 0) + ",";
  json += "\"wpc\":" + String(param_wpc.value() ? 1 : 0) + ",";
  json += "\"rg\":" + String(param_raw_gma.value() ? 1 : 0) + ",";
  json += "\"lenc\":" + String(param_lenc.value() ? 1 : 0) + ",";
  json += "\"hm\":" + String(param_hmirror.value() ? 1 : 0) + ",";
  json += "\"vm\":" + String(param_vflip.value() ? 1 : 0) + ",";
  json += "\"dcw\":" + String(param_dcw.value() ? 1 : 0) + ",";
  json += "\"cb\":" + String(param_colorbar.value() ? 1 : 0);
  json += "}";

  web_server.sendHeader("Access-Control-Allow-Origin", "*");
  web_server.send(200, "application/json", json);
}

void handle_api_set_settings()
{
  log_v("handle_api_set_settings");

  // Parse form data and update settings
  bool changed = false;

  if (web_server.hasArg("fd")) { param_frame_duration.value() = web_server.arg("fd").toInt(); changed = true; }
  if (web_server.hasArg("fs")) { strncpy(param_frame_size.value(), web_server.arg("fs").c_str(), sizeof(param_frame_size.value())-1); changed = true; }
  if (web_server.hasArg("q")) { param_jpg_quality.value() = web_server.arg("q").toInt(); changed = true; }
  if (web_server.hasArg("b")) { param_brightness.value() = web_server.arg("b").toInt(); changed = true; }
  if (web_server.hasArg("c")) { param_contrast.value() = web_server.arg("c").toInt(); changed = true; }
  if (web_server.hasArg("s")) { param_saturation.value() = web_server.arg("s").toInt(); changed = true; }
  if (web_server.hasArg("e")) { strncpy(param_special_effect.value(), web_server.arg("e").c_str(), sizeof(param_special_effect.value())-1); changed = true; }
  if (web_server.hasArg("wb")) { param_whitebal.value() = web_server.arg("wb") == "1"; changed = true; }
  if (web_server.hasArg("awbg")) { param_awb_gain.value() = web_server.arg("awbg") == "1"; changed = true; }
  if (web_server.hasArg("wbm")) { strncpy(param_wb_mode.value(), web_server.arg("wbm").c_str(), sizeof(param_wb_mode.value())-1); changed = true; }
  if (web_server.hasArg("ec")) { param_exposure_ctrl.value() = web_server.arg("ec") == "1"; changed = true; }
  if (web_server.hasArg("aec2")) { param_aec2.value() = web_server.arg("aec2") == "1"; changed = true; }
  if (web_server.hasArg("ael")) { param_ae_level.value() = web_server.arg("ael").toInt(); changed = true; }
  if (web_server.hasArg("aecv")) { param_aec_value.value() = web_server.arg("aecv").toInt(); changed = true; }
  if (web_server.hasArg("gc")) { param_gain_ctrl.value() = web_server.arg("gc") == "1"; changed = true; }
  if (web_server.hasArg("agcg")) { param_agc_gain.value() = web_server.arg("agcg").toInt(); changed = true; }
  if (web_server.hasArg("gcl")) { strncpy(param_gain_ceiling.value(), web_server.arg("gcl").c_str(), sizeof(param_gain_ceiling.value())-1); changed = true; }
  if (web_server.hasArg("bpc")) { param_bpc.value() = web_server.arg("bpc") == "1"; changed = true; }
  if (web_server.hasArg("wpc")) { param_wpc.value() = web_server.arg("wpc") == "1"; changed = true; }
  if (web_server.hasArg("rg")) { param_raw_gma.value() = web_server.arg("rg") == "1"; changed = true; }
  if (web_server.hasArg("lenc")) { param_lenc.value() = web_server.arg("lenc") == "1"; changed = true; }
  if (web_server.hasArg("hm")) { param_hmirror.value() = web_server.arg("hm") == "1"; changed = true; }
  if (web_server.hasArg("vm")) { param_vflip.value() = web_server.arg("vm") == "1"; changed = true; }
  if (web_server.hasArg("dcw")) { param_dcw.value() = web_server.arg("dcw") == "1"; changed = true; }
  if (web_server.hasArg("cb")) { param_colorbar.value() = web_server.arg("cb") == "1"; changed = true; }

  if (changed)
  {
    // Apply to camera immediately
    update_camera_settings();
    // Only save to flash if requested
    if (web_server.hasArg("save") && web_server.arg("save") == "1")
    {
      iotWebConf.saveConfig();
    }
  }

  web_server.sendHeader("Access-Control-Allow-Origin", "*");
  web_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

esp_err_t initialize_camera()
{
  log_v("initialize_camera");

  log_i("Frame size: %s", param_frame_size.value());
  auto frame_size = lookup_frame_size(param_frame_size.value());
  log_i("JPEG quality: %d", param_jpg_quality.value());
  auto jpeg_quality = param_jpg_quality.value();
  log_i("Frame duration: %d ms", param_frame_duration.value());
  const camera_config_t camera_config = {
      .pin_pwdn = CAMERA_CONFIG_PIN_PWDN,         // GPIO pin for camera power down line
      .pin_reset = CAMERA_CONFIG_PIN_RESET,       // GPIO pin for camera reset line
      .pin_xclk = CAMERA_CONFIG_PIN_XCLK,         // GPIO pin for camera XCLK line
      .pin_sccb_sda = CAMERA_CONFIG_PIN_SCCB_SDA, // GPIO pin for camera SDA line
      .pin_sccb_scl = CAMERA_CONFIG_PIN_SCCB_SCL, // GPIO pin for camera SCL line
      .pin_d7 = CAMERA_CONFIG_PIN_Y9,             // GPIO pin for camera D7 line
      .pin_d6 = CAMERA_CONFIG_PIN_Y8,             // GPIO pin for camera D6 line
      .pin_d5 = CAMERA_CONFIG_PIN_Y7,             // GPIO pin for camera D5 line
      .pin_d4 = CAMERA_CONFIG_PIN_Y6,             // GPIO pin for camera D4 line
      .pin_d3 = CAMERA_CONFIG_PIN_Y5,             // GPIO pin for camera D3 line
      .pin_d2 = CAMERA_CONFIG_PIN_Y4,             // GPIO pin for camera D2 line
      .pin_d1 = CAMERA_CONFIG_PIN_Y3,             // GPIO pin for camera D1 line
      .pin_d0 = CAMERA_CONFIG_PIN_Y2,             // GPIO pin for camera D0 line
      .pin_vsync = CAMERA_CONFIG_PIN_VSYNC,       // GPIO pin for camera VSYNC line
      .pin_href = CAMERA_CONFIG_PIN_HREF,         // GPIO pin for camera HREF line
      .pin_pclk = CAMERA_CONFIG_PIN_PCLK,         // GPIO pin for camera PCLK line
      .xclk_freq_hz = CAMERA_CONFIG_CLK_FREQ_HZ,  // Frequency of XCLK signal, in Hz. EXPERIMENTAL: Set to 16MHz on ESP32-S2 or ESP32-S3 to enable EDMA mode
      .ledc_timer = CAMERA_CONFIG_LEDC_TIMER,     // LEDC timer to be used for generating XCLK
      .ledc_channel = CAMERA_CONFIG_LEDC_CHANNEL, // LEDC channel to be used for generating XCLK
      .pixel_format = PIXFORMAT_JPEG,             // Format of the pixel data: PIXFORMAT_ + YUV422|GRAYSCALE|RGB565|JPEG
      .frame_size = frame_size,                   // Size of the output image: FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
      .jpeg_quality = jpeg_quality,               // Quality of JPEG output. 0-63 lower means higher quality
      .fb_count = CAMERA_CONFIG_FB_COUNT,         // Number of frame buffers to be allocated. If more than one, then each frame will be acquired (double speed)
      .fb_location = CAMERA_CONFIG_FB_LOCATION,   // The location where the frame buffer will be allocated
      .grab_mode = CAMERA_GRAB_LATEST,            // When buffers should be filled
#if CONFIG_CAMERA_CONVERTER_ENABLED
      conv_mode = CONV_DISABLE, // RGB<->YUV Conversion mode
#endif
      .sccb_i2c_port = SCCB_I2C_PORT // If pin_sccb_sda is -1, use the already configured I2C bus by number
  };

  return cam.init(camera_config);
}

void update_camera_settings()
{
  auto camera = esp_camera_sensor_get();
  if (camera == nullptr)
  {
    log_e("Unable to get camera sensor");
    return;
  }

  // Frame size and quality can be changed at runtime
  auto frame_size = lookup_frame_size(param_frame_size.value());
  camera->set_framesize(camera, frame_size);
  camera->set_quality(camera, param_jpg_quality.value());

  camera->set_brightness(camera, param_brightness.value());
  camera->set_contrast(camera, param_contrast.value());
  camera->set_saturation(camera, param_saturation.value());
  camera->set_special_effect(camera, lookup_camera_effect(param_special_effect.value()));
  camera->set_whitebal(camera, param_whitebal.value());
  camera->set_awb_gain(camera, param_awb_gain.value());
  camera->set_wb_mode(camera, lookup_camera_wb_mode(param_wb_mode.value()));
  camera->set_exposure_ctrl(camera, param_exposure_ctrl.value());
  camera->set_aec2(camera, param_aec2.value());
  camera->set_ae_level(camera, param_ae_level.value());
  camera->set_aec_value(camera, param_aec_value.value());
  camera->set_gain_ctrl(camera, param_gain_ctrl.value());
  camera->set_agc_gain(camera, param_agc_gain.value());
  camera->set_gainceiling(camera, lookup_camera_gainceiling(param_gain_ceiling.value()));
  camera->set_bpc(camera, param_bpc.value());
  camera->set_wpc(camera, param_wpc.value());
  camera->set_raw_gma(camera, param_raw_gma.value());
  camera->set_lenc(camera, param_lenc.value());
  camera->set_hmirror(camera, param_hmirror.value());
  camera->set_vflip(camera, param_vflip.value());
  camera->set_dcw(camera, param_dcw.value());
  camera->set_colorbar(camera, param_colorbar.value());
}

void start_rtsp_server()
{
  log_v("start_rtsp_server");
  camera_server = std::unique_ptr<rtsp_server>(new rtsp_server(cam, param_frame_duration.value(), RTSP_PORT));
  // Add RTSP service to mDNS
  // HTTP is already set by iotWebConf
  MDNS.addService("rtsp", "tcp", RTSP_PORT);
}

void on_connected()
{
  log_v("on_connected");

  // Disable WiFi power saving for low-latency streaming
  WiFi.setSleep(WIFI_PS_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Start the RTSP Server if initialized
  if (camera_init_result == ESP_OK)
    start_rtsp_server();
  else
    log_e("Not starting RTSP server: camera not initialized");
}

void on_config_saved()
{
  log_v("on_config_saved");
  update_camera_settings();
}

void setup()
{
  // Disable brownout
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.setDebugOutput(true);
#ifdef CAMERA_POWER_GPIO
  pinMode(CAMERA_POWER_GPIO, OUTPUT);
  digitalWrite(CAMERA_POWER_GPIO, CAMERA_POWER_ON_LEVEL);
#endif

#ifdef USER_LED_GPIO
  pinMode(USER_LED_GPIO, OUTPUT);
  digitalWrite(USER_LED_GPIO, !USER_LED_ON_LEVEL);
#endif

#ifdef FLASH_LED_GPIO
  pinMode(FLASH_LED_GPIO, OUTPUT);
  // Set resolution to 8 bits
  analogWriteResolution(8);
  // Turn flash led off
  analogWrite(FLASH_LED_GPIO, 0);
#endif

#ifdef ARDUINO_USB_CDC_ON_BOOT
  // Delay for USB to connect/settle
  delay(5000);
#endif

  log_i("Core debug level: %d", CORE_DEBUG_LEVEL);
  log_i("CPU Freq: %d Mhz, %d core(s)", getCpuFrequencyMhz(), ESP.getChipCores());
  log_i("Free heap: %d bytes", ESP.getFreeHeap());
  log_i("SDK version: %s", ESP.getSdkVersion());
  log_i("Board: %s", BOARD_NAME);
  log_i("Starting " APP_TITLE "...");

  if (CAMERA_CONFIG_FB_LOCATION == CAMERA_FB_IN_PSRAM && !psramInit())
    log_e("Failed to initialize PSRAM");

  param_group_camera.addItem(&param_frame_duration);
  param_group_camera.addItem(&param_frame_size);
  param_group_camera.addItem(&param_jpg_quality);
  param_group_camera.addItem(&param_brightness);
  param_group_camera.addItem(&param_contrast);
  param_group_camera.addItem(&param_saturation);
  param_group_camera.addItem(&param_special_effect);
  param_group_camera.addItem(&param_whitebal);
  param_group_camera.addItem(&param_awb_gain);
  param_group_camera.addItem(&param_wb_mode);
  param_group_camera.addItem(&param_exposure_ctrl);
  param_group_camera.addItem(&param_aec2);
  param_group_camera.addItem(&param_ae_level);
  param_group_camera.addItem(&param_aec_value);
  param_group_camera.addItem(&param_gain_ctrl);
  param_group_camera.addItem(&param_agc_gain);
  param_group_camera.addItem(&param_gain_ceiling);
  param_group_camera.addItem(&param_bpc);
  param_group_camera.addItem(&param_wpc);
  param_group_camera.addItem(&param_raw_gma);
  param_group_camera.addItem(&param_lenc);
  param_group_camera.addItem(&param_hmirror);
  param_group_camera.addItem(&param_vflip);
  param_group_camera.addItem(&param_dcw);
  param_group_camera.addItem(&param_colorbar);
  iotWebConf.addParameterGroup(&param_group_camera);

  iotWebConf.setConfigSavedCallback(on_config_saved);
  iotWebConf.setWifiConnectionCallback(on_connected);
#ifdef USER_LED_GPIO
  iotWebConf.setStatusPin(USER_LED_GPIO, USER_LED_ON_LEVEL);
#endif
  iotWebConf.getApTimeoutParameter()->visible = true;

  // Skip AP mode if WiFi credentials are already configured
  // (device will still enter AP mode if no credentials are saved)
  iotWebConf.skipApStartup();

  iotWebConf.init();

  // Try to initialize 3 times
  for (auto i = 0; i < 3; i++)
  {
    camera_init_result = initialize_camera();
    if (camera_init_result == ESP_OK)
    {
      update_camera_settings();
      break;
    }

    esp_camera_deinit();
    log_e("Failed to initialize camera. Error: 0x%0x. Frame size: %s, frame rate: %d ms, jpeg quality: %d", camera_init_result, param_frame_size.value(), param_frame_duration.value(), param_jpg_quality.value());
    delay(500);
  }

  // Set up URL handlers
  web_server.on("/", HTTP_GET, handle_root);
  web_server.on("/control", HTTP_GET, []() {
    web_server.send(200, "text/html", control_html_start);
  });
  web_server.on("/snapshot", HTTP_GET, handle_snapshot);
  web_server.on("/stream", HTTP_GET, handle_stream);
#ifdef FLASH_LED_GPIO
  web_server.on("/flash", HTTP_GET, handle_flash);
#endif
  // JSON API for single-page interface
  web_server.on("/api/settings", HTTP_GET, handle_api_get_settings);
  web_server.on("/api/settings", HTTP_POST, handle_api_set_settings);
  // IotWebConf config page for WiFi settings
  web_server.on("/config", []
                { iotWebConf.handleConfig(); });
  web_server.onNotFound([]()
                        { iotWebConf.handleNotFound(); });
}

// Serial command interface
String serialBuffer;

// X-macro parameter table - define all camera parameters once
// ULONG: unsigned long parameters
#define PARAMS_ULONG(X) \
  X(fd, param_frame_duration)

// INT: signed int parameters
#define PARAMS_INT(X) \
  X(b, param_brightness) \
  X(c, param_contrast) \
  X(s, param_saturation) \
  X(ael, param_ae_level) \
  X(aecv, param_aec_value) \
  X(agcg, param_agc_gain)

// BYTE: byte parameters (printed as int)
#define PARAMS_BYTE(X) \
  X(q, param_jpg_quality)

// SELECT: string/select parameters
#define PARAMS_SELECT(X) \
  X(fs, param_frame_size) \
  X(e, param_special_effect) \
  X(wbm, param_wb_mode) \
  X(gcl, param_gain_ceiling)

// BOOL: checkbox/boolean parameters
#define PARAMS_BOOL(X) \
  X(wb, param_whitebal) \
  X(awbg, param_awb_gain) \
  X(ec, param_exposure_ctrl) \
  X(aec2, param_aec2) \
  X(gc, param_gain_ctrl) \
  X(bpc, param_bpc) \
  X(wpc, param_wpc) \
  X(rg, param_raw_gma) \
  X(lenc, param_lenc) \
  X(hm, param_hmirror) \
  X(vm, param_vflip) \
  X(dcw, param_dcw) \
  X(cb, param_colorbar)

void listSerialParams()
{
  Serial.println("System parameters:");
  Serial.printf("  iwcWifiSsid = %s\n", iotWebConf.getWifiParameterGroup()->_wifiSsid);
  Serial.println("  iwcWifiPassword = <hidden>");
  Serial.println("  iwcApPassword = <hidden>");
  Serial.println("Camera parameters:");

  // Generate list output using X-macros
  #define LIST_ULONG(id, p) Serial.printf("  " #id " = %lu\n", p.value());
  #define LIST_INT(id, p)   Serial.printf("  " #id " = %d\n", p.value());
  #define LIST_BYTE(id, p)  Serial.printf("  " #id " = %d\n", (int)p.value());
  #define LIST_SELECT(id, p) Serial.printf("  " #id " = %s\n", p.value());
  #define LIST_BOOL(id, p)  Serial.printf("  " #id " = %d\n", p.value() ? 1 : 0);

  PARAMS_ULONG(LIST_ULONG)
  PARAMS_BYTE(LIST_BYTE)
  PARAMS_INT(LIST_INT)
  PARAMS_SELECT(LIST_SELECT)
  PARAMS_BOOL(LIST_BOOL)

  #undef LIST_ULONG
  #undef LIST_INT
  #undef LIST_BYTE
  #undef LIST_SELECT
  #undef LIST_BOOL
}

void processSerialCommand(const String& cmd)
{
  String trimmed = cmd;
  trimmed.trim();

  if (trimmed.length() == 0)
    return;

  if (trimmed == "help" || trimmed == "?")
  {
    Serial.println("Serial commands:");
    Serial.println("  list           - List all parameters");
    Serial.println("  get <id>       - Get parameter value");
    Serial.println("  set <id> <val> - Set parameter value");
    Serial.println("  save           - Save config to flash");
    Serial.println("  reboot         - Reboot device");
  }
  else if (trimmed == "list")
  {
    listSerialParams();
  }
  else if (trimmed.startsWith("get "))
  {
    String id = trimmed.substring(4);
    id.trim();

    // System parameters
    if (id == "iwcWifiSsid") { Serial.printf("iwcWifiSsid = %s\n", iotWebConf.getWifiParameterGroup()->_wifiSsid); return; }
    if (id == "iwcWifiPassword" || id == "iwcApPassword") { Serial.printf("%s = <hidden>\n", id.c_str()); return; }

    // Camera parameters - generate handlers using X-macros
    #define GET_ULONG(pid, p) if (id == #pid) { Serial.printf(#pid " = %lu\n", p.value()); return; }
    #define GET_INT(pid, p)   if (id == #pid) { Serial.printf(#pid " = %d\n", p.value()); return; }
    #define GET_BYTE(pid, p)  if (id == #pid) { Serial.printf(#pid " = %d\n", (int)p.value()); return; }
    #define GET_SELECT(pid, p) if (id == #pid) { Serial.printf(#pid " = %s\n", p.value()); return; }
    #define GET_BOOL(pid, p)  if (id == #pid) { Serial.printf(#pid " = %d\n", p.value() ? 1 : 0); return; }

    PARAMS_ULONG(GET_ULONG)
    PARAMS_BYTE(GET_BYTE)
    PARAMS_INT(GET_INT)
    PARAMS_SELECT(GET_SELECT)
    PARAMS_BOOL(GET_BOOL)

    #undef GET_ULONG
    #undef GET_INT
    #undef GET_BYTE
    #undef GET_SELECT
    #undef GET_BOOL

    Serial.printf("Parameter '%s' not found\n", id.c_str());
  }
  else if (trimmed.startsWith("set "))
  {
    String rest = trimmed.substring(4);
    int spaceIdx = rest.indexOf(' ');
    if (spaceIdx < 0)
    {
      Serial.println("Usage: set <id> <value>");
      return;
    }
    String id = rest.substring(0, spaceIdx);
    String value = rest.substring(spaceIdx + 1);
    id.trim();
    value.trim();

    // System parameters
    if (id == "iwcWifiSsid")
    {
      strncpy(iotWebConf.getWifiParameterGroup()->_wifiSsid, value.c_str(), IOTWEBCONF_WORD_LEN);
      Serial.printf("Set iwcWifiSsid = %s\n", value.c_str());
      return;
    }
    if (id == "iwcWifiPassword")
    {
      strncpy(iotWebConf.getWifiParameterGroup()->_wifiPassword, value.c_str(), IOTWEBCONF_PASSWORD_LEN);
      Serial.println("Set iwcWifiPassword = <hidden>");
      return;
    }
    if (id == "iwcApPassword")
    {
      strncpy(iotWebConf.getApPasswordParameter()->valueBuffer, value.c_str(), IOTWEBCONF_PASSWORD_LEN);
      Serial.println("Set iwcApPassword = <hidden>");
      return;
    }

    // Camera parameters - generate handlers using X-macros
    #define SET_ULONG(pid, p) if (id == #pid) { p.value() = value.toInt(); Serial.printf("Set " #pid " = %lu\n", p.value()); return; }
    #define SET_INT(pid, p)   if (id == #pid) { p.value() = value.toInt(); Serial.printf("Set " #pid " = %d\n", p.value()); return; }
    #define SET_BYTE(pid, p)  if (id == #pid) { p.value() = value.toInt(); Serial.printf("Set " #pid " = %d\n", (int)p.value()); return; }
    #define SET_SELECT(pid, p) if (id == #pid) { strncpy(p.value(), value.c_str(), sizeof(p.value())-1); Serial.printf("Set " #pid " = %s\n", p.value()); return; }
    #define SET_BOOL(pid, p)  if (id == #pid) { p.value() = (value == "1" || value == "true"); Serial.printf("Set " #pid " = %d\n", p.value() ? 1 : 0); return; }

    PARAMS_ULONG(SET_ULONG)
    PARAMS_BYTE(SET_BYTE)
    PARAMS_INT(SET_INT)
    PARAMS_SELECT(SET_SELECT)
    PARAMS_BOOL(SET_BOOL)

    #undef SET_ULONG
    #undef SET_INT
    #undef SET_BYTE
    #undef SET_SELECT
    #undef SET_BOOL

    Serial.printf("Parameter '%s' not found\n", id.c_str());
  }
  else if (trimmed == "save")
  {
    iotWebConf.saveConfig();
    Serial.println("Config saved");
  }
  else if (trimmed == "reboot")
  {
    Serial.println("Rebooting...");
    delay(100);
    ESP.restart();
  }
  else
  {
    Serial.printf("Unknown command: %s (type 'help' for commands)\n", trimmed.c_str());
  }
}

void handleSerial()
{
  while (Serial.available())
  {
    char c = Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (serialBuffer.length() > 0)
      {
        processSerialCommand(serialBuffer);
        serialBuffer = "";
      }
    }
    else
    {
      serialBuffer += c;
    }
  }
}

void loop()
{
  iotWebConf.doLoop();
  handleSerial();

  if (camera_server)
    camera_server->doLoop();

  // Yield to other tasks (WiFi, etc.) for better scheduling
  vTaskDelay(1);
}