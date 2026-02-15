#pragma once

#include "wled.h"
#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#else
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

class TrafficLampUsermod : public Usermod {
private:
  unsigned long lastCheck = 0;
  const unsigned long checkInterval = 300000; // 5 minutes

  // Google Maps API configuration
  // Replace YOUR_API_KEY with your actual Google Maps API key
  String apiKey = "YOUR_API_KEY";
  String origin = "1%20N%20Santa%20Cruz%20Ave%2C%20Los%20Gatos%2C%20CA%2095030";
  String destination = "1405%20Martin%20Luther%20King%20Jr%20Way%2C%20Berkeley%2C%20CA%2094709";

  void updateFromTraffic() {
    if (!WLED_CONNECTED) return;

    String url = "https://maps.googleapis.com/maps/api/distancematrix/json?origins=" + origin +
                 "&destinations=" + destination +
                 "&departure_time=now&key=" + apiKey;

    uint32_t color = RGBW32(0, 0, 255, 0); // Default to Blue for error

    WiFiClientSecure client;
    client.setInsecure(); // No certificate verification

    HTTPClient http;
    bool beginOk = false;
    #ifdef ESP8266
    beginOk = http.begin(client, url);
    #else
    beginOk = http.begin(client, url);
    #endif

    if (beginOk) {
      int code = http.GET();
      if (code == 200) {
        String payload = http.getString();
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
          JsonObject element = doc["rows"][0]["elements"][0];
          const char* status = element["status"] | "";
          if (strcmp(status, "OK") == 0) {
            long travelSeconds = -1;
            if (element.containsKey("duration_in_traffic")) {
              travelSeconds = element["duration_in_traffic"]["value"];
            } else if (element.containsKey("duration")) {
              travelSeconds = element["duration"]["value"];
            }

            if (travelSeconds >= 0) {
              // Thresholds: 1:10 = 70 min = 4200s, 2:15 = 135 min = 8100s
              if (travelSeconds <= 4200) {
                color = RGBW32(0, 255, 0, 0); // Green
              } else if (travelSeconds >= 8100) {
                color = RGBW32(255, 0, 0, 0); // Red
              } else {
                // Gradient scale: Green -> Yellow -> Red
                float ratio = (float)(travelSeconds - 4200) / (8100 - 4200);
                if (ratio < 0.5f) {
                  // Green to Yellow (0,255,0) -> (255,255,0)
                  uint8_t r = 255 * (ratio * 2.0f);
                  color = RGBW32(r, 255, 0, 0);
                } else {
                  // Yellow to Red (255,255,0) -> (255,0,0)
                  uint8_t g = 255 * (1.0f - (ratio - 0.5f) * 2.0f);
                  color = RGBW32(255, g, 0, 0);
                }
              }
            }
          }
        }
      }
      http.end();
    }

    // Apply color to the primary color
    colPri[0] = R(color);
    colPri[1] = G(color);
    colPri[2] = B(color);
    colPri[3] = W(color);

    // Update all active segments
    for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) {
      Segment& seg = strip.getSegment(i);
      if (seg.isActive()) {
        seg.colors[0] = color;
      }
    }
    strip.show();
    colorUpdated(CALL_MODE_DIRECT_CHANGE);
  }

public:
  void setup() override {
    // Initial check will be done in loop() after connection
  }

  void loop() override {
    if (!WLED_CONNECTED) return;

    unsigned long now = millis();
    // Start first check 10 seconds after connection if not checked before
    if (lastCheck == 0) {
      lastCheck = now - checkInterval + 10000;
    }

    if (now - lastCheck > checkInterval) {
      lastCheck = now;
      updateFromTraffic();
    }
  }

  uint16_t getId() override {
    return 0x1001;
  }
};
