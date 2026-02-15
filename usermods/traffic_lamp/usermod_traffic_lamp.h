#pragma once

#include "wled.h"
#include <HTTPClient.h>


class TrafficLampUsermod : public Usermod {
private:
  unsigned long lastCheck = 0;
  const unsigned long checkInterval = 300000; // 5 minutes

  // TODO: put your API key and endpoints here
  String apiUrl = "https://example.com/traffic-api";

  void updateFromTraffic() {
    HTTPClient http;
    http.begin(apiUrl);
    int code = http.GET();
    if (code == 200) {
      String payload = http.getString();
      // TODO: parse JSON, extract travel time
      int travelMinutes = 25; // placeholder

      // Map travel time to color (simple example)
      uint32_t color;
      if (travelMinutes <= 20) {
        color = RGBW32(0, 255, 0);   // green
      } else if (travelMinutes <= 40) {
        color = RGBW32(255, 165, 0); // orange
      } else {
        color = RGBW32(255, 0, 0);   // red
      }

      strip.fill(color);
      strip.show();
    }
    http.end();
  }

public:
  void setup() override {
    // Called once at boot
  }

  void loop() override {
    if (!WLED_CONNECTED) return; // only run when WiFi is up

    unsigned long now = millis();
    if (now - lastCheck > checkInterval) {
      lastCheck = now;
      updateFromTraffic();
    }
  }

  uint16_t getId() override {
    // pick a unique ID (0x1000+ is usually safe for custom mods)
    return 0x1001;
  }
};

#ifdef USERMOD_TRAFFIC_LAMP
#include "wled.h"
#endif

class TrafficLampUsermod : public Usermod {
  // ... your code ...
};

#ifdef USERMOD_TRAFFIC_LAMP
Usermod* registerUsermod() {
  return new TrafficLampUsermod();
}
#endif
