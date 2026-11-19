#include "web_ui.h"

#include <WiFi.h>

#include "web_ui_assets.h"

namespace {

constexpr uint16_t kHttpPort = 80;
constexpr uint8_t kSoftApChannel = 6;
constexpr uint8_t kMaxClients = 2;

} // namespace

WebUi::WebUi() : server_(kHttpPort) {}

bool WebUi::begin(StateProvider stateProvider, ActionHandler actionHandler,
                  void *context) {
  stateProvider_ = stateProvider;
  actionHandler_ = actionHandler;
  context_ = context;

  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(ssid_, sizeof(ssid_), "StompBridge-%04X",
           static_cast<unsigned>(chipId & 0xffff));
  snprintf(password_, sizeof(password_), "midi%04x",
           static_cast<unsigned>(chipId & 0xffff));

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  if (!WiFi.softAP(ssid_, password_, kSoftApChannel, false, kMaxClients)) {
    return false;
  }

  server_.on("/", HTTP_GET, [this]() {
    serveAsset(WebUiAssets::kIndexHtml, WebUiAssets::kIndexHtmlLength,
               "text/html; charset=utf-8");
  });
  server_.on("/styles.css", HTTP_GET, [this]() {
    serveAsset(WebUiAssets::kStylesCss, WebUiAssets::kStylesCssLength,
               "text/css; charset=utf-8");
  });
  server_.on("/ble_transport.js", HTTP_GET, [this]() {
    serveAsset(WebUiAssets::kBleTransportJs, WebUiAssets::kBleTransportJsLength,
               "application/javascript; charset=utf-8");
  });
  server_.on("/app.js", HTTP_GET, [this]() {
    serveAsset(WebUiAssets::kAppJs, WebUiAssets::kAppJsLength,
               "application/javascript; charset=utf-8");
  });
  server_.on("/api/v1/state", HTTP_GET, [this]() { handleState(); });
  server_.on("/api/v1/action", HTTP_POST, [this]() { handleAction(); });
  server_.onNotFound([this]() {
    server_.send(404, "application/json", "{\"error\":\"not_found\"}");
  });
  server_.begin();
  return true;
}

void WebUi::poll() { server_.handleClient(); }

void WebUi::serveAsset(const uint8_t *data, size_t length,
                       const char *contentType) {
  server_.sendHeader("Content-Encoding", "gzip");
  // The UI is still evolving rapidly. Never let a phone keep JavaScript from
  // an older firmware image, since its API/state shape may no longer match.
  server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server_.sendHeader("Pragma", "no-cache");
  server_.send_P(200, contentType, reinterpret_cast<PGM_P>(data), length);
}

void WebUi::handleState() {
  if (stateProvider_ == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"state_unavailable\"}");
    return;
  }
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "application/json", stateProvider_(context_));
}

void WebUi::handleAction() {
  char action[32];
  if (actionHandler_ == nullptr ||
      !extractJsonString(server_.arg("plain"), "action", action,
                         sizeof(action))) {
    server_.send(400, "application/json",
                 "{\"accepted\":false,\"error\":\"invalid_request\"}");
    return;
  }
  if (!actionHandler_(action, context_)) {
    server_.send(409, "application/json",
                 "{\"accepted\":false,\"error\":\"queue_full_or_unknown_action\"}");
    return;
  }
  server_.send(202, "application/json", "{\"accepted\":true}");
}

bool WebUi::extractJsonString(const String &body, const char *key,
                              char *output, size_t outputSize) {
  if (output == nullptr || outputSize < 2) {
    return false;
  }
  const String needle = String('"') + key + '"';
  int position = body.indexOf(needle);
  if (position < 0) {
    return false;
  }
  position = body.indexOf(':', position + needle.length());
  if (position < 0) {
    return false;
  }
  position = body.indexOf('"', position + 1);
  if (position < 0) {
    return false;
  }
  const int end = body.indexOf('"', position + 1);
  if (end < 0 || end == position + 1 ||
      static_cast<size_t>(end - position) > outputSize) {
    return false;
  }
  const String value = body.substring(position + 1, end);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= 'a' && c <= 'z') || c == '_')) {
      return false;
    }
  }
  value.toCharArray(output, outputSize);
  return true;
}
