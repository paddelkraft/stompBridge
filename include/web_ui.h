#pragma once

#include <Arduino.h>
#include <WebServer.h>

class WebUi {
public:
  using StateProvider = String (*)(void *context);
  using ActionHandler = bool (*)(const char *action, void *context);

  WebUi();

  bool begin(StateProvider stateProvider, ActionHandler actionHandler,
             void *context = nullptr);
  void poll();
  const char *ssid() const { return ssid_; }
  const char *password() const { return password_; }

private:
  void serveAsset(const uint8_t *data, size_t length, const char *contentType);
  void handleState();
  void handleAction();
  static bool extractJsonString(const String &body, const char *key,
                                char *output, size_t outputSize);

  WebServer server_;
  StateProvider stateProvider_ = nullptr;
  ActionHandler actionHandler_ = nullptr;
  void *context_ = nullptr;
  char ssid_[32] = {};
  char password_[24] = {};
};
