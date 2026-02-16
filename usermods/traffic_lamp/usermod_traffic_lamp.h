#pragma once

#include "wled.h"
#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#else
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

// ========== TRAFFIC LAMP USERMOD CONFIGURATION ==========
// These colors and effects are internal to this usermod
// Users should customize traffic routing in wled00/my_config.h

// Traffic Level Colors (RGB values) - Green to Red scale
#define TRAFFIC_COLOR_0 RGBW32(0,   255, 0,   0)  // Very Light    - Green
#define TRAFFIC_COLOR_1 RGBW32(153, 255, 0,   0)  // Light-Moderate - Lime Green
#define TRAFFIC_COLOR_2 RGBW32(255, 255, 0,   0)  // Moderate      - Yellow
#define TRAFFIC_COLOR_3 RGBW32(255, 191, 0,   0)  // Moderate-Heavy - Amber
#define TRAFFIC_COLOR_4 RGBW32(255, 128, 0,   0)  // Heavy         - Orange
#define TRAFFIC_COLOR_5 RGBW32(255, 0,   0,   0)  // Very Heavy    - Red

// Status Indicator Colors
#define STATUS_WIFI_CONNECTING RGBW32(0,   0,   255, 0)  // Deep Blue (WiFi connected)
#define STATUS_QUERYING RGBW32(255, 255, 0,   0)         // Yellow (querying API)
#define STATUS_ERROR RGBW32(255, 0,   0,   0)            // Red (query error)
#define STATUS_SUCCESS RGBW32(0,   255, 0,   0)          // Green (query success)

// Startup Effect IDs (standard WLED effect indices)
#define STARTUP_BODY_EFFECT 0      // Solid
#define STARTUP_RING_EFFECT 0      // Solid

// Segment IDs for different lamp parts
#define SEG_BODY_1 0
#define SEG_RING 1
#define SEG_BODY_2 2
#define SEG_HEAD 3
#define SEG_BODY_3 4
#define SEG_ARM 5
#define SEG_BODY_4 6

class TrafficLampUsermod : public Usermod {
private:
  unsigned long lastTrafficCheck = 0;
  unsigned long segmentSetupTime = 0;
  const unsigned long checkInterval = 300000; // 5 minutes
  bool wifiConnected = false;
  bool segmentsCreated = false;

  #ifndef TOMTOM_API_KEY
    #define TOMTOM_API_KEY "YOUR_API_KEY"
  #endif
  String apiKey = TOMTOM_API_KEY;

  // Helper function to calculate traffic level (0-5) based on predicted time
  // Takes low threshold, high threshold, and predicted minutes
  // Returns: 0-4 for levels, 5 for exceeding high threshold
  uint8_t getTrafficLevel(uint16_t lowMinutes, uint16_t highMinutes, uint16_t predictedMinutes) {
    // If below low threshold, very light traffic
    if (predictedMinutes <= lowMinutes) {
      return 0;
    }
    
    // If above high threshold, very heavy traffic
    if (predictedMinutes >= highMinutes) {
      return 5;
    }
    
    // Calculate level based on position between low and high
    uint16_t range = highMinutes - lowMinutes;
    uint32_t difference = predictedMinutes - lowMinutes;
    uint8_t level = (difference * 4) / range;
    
    // Clamp to 0-4
    if (level > 4) level = 4;
    
    return level;
  }

  // Set segment color and optionally effect
  void setSegmentColor(uint8_t segmentId, uint32_t color, int effectId = FX_MODE_STATIC) {
    if (segmentId < strip.getSegmentsNum()) {
      Segment& seg = strip.getSegment(segmentId);
      seg.setColor(0, color);
      if (effectId >= 0) {
        seg.mode = effectId;
      }
    }
  }

  // Set segment to pulse effect
  void setSegmentPulse(uint8_t segmentId, uint32_t color) {
    if (segmentId < strip.getSegmentsNum()) {
      Segment& seg = strip.getSegment(segmentId);
      seg.setColor(0, color);
      seg.mode = 2; // Blink/pulse effect
    }
  }

  // Query traffic for a specific destination
  uint16_t queryTraffic(const char* origin, const char* destination) {
    if (!WLED_CONNECTED) return 0;

    if (apiKey == "YOUR_API_KEY") {
      Serial.println("[TrafficLamp] ERROR: TOMTOM_API_KEY not set");
      return 0;
    }

    Serial.printf("[TrafficLamp] Querying: %s -> %s\n", origin, destination);

    String url = "https://api.tomtom.com/routing/1/calculateRoute/";
    url += origin;
    url += ":";
    url += destination;
    url += "/json?key=";
    url += apiKey;
    url += "&traffic=true&travelMode=car";

    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();
    uint16_t travelMinutes = 0;

    Serial.printf("[TrafficLamp] HTTP code: %d\n", httpCode);

    if (httpCode == 200) {
      String payload = http.getString();
      Serial.printf("[TrafficLamp] Response size: %d bytes\n", payload.length());
      
      // Use a filter to only parse the fields we need, saving memory
      StaticJsonDocument<200> filter;
      filter["routes"][0]["summary"]["travelTimeInSeconds"] = true;
      
      DynamicJsonDocument doc(2048);  // Much smaller since we're filtering
      DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

      if (!error && doc["routes"].size() > 0) {
        JsonObject summary = doc["routes"][0]["summary"];
        if (!summary.isNull()) {
          long travelSeconds = summary["travelTimeInSeconds"] | 0;
          travelMinutes = travelSeconds / 60;
          Serial.printf("[TrafficLamp] Travel time: %d min (%ld sec)\n", travelMinutes, travelSeconds);
        } else {
          Serial.println("[TrafficLamp] ERROR: No summary in response");
        }
      } else {
        if (error) {
          Serial.printf("[TrafficLamp] JSON error: %s\n", error.c_str());
        } else {
          Serial.println("[TrafficLamp] ERROR: No routes in response");
        }
      }
      http.end();
    } else {
      Serial.printf("[TrafficLamp] HTTP error: %d\n", httpCode);
      http.end();
    }

    return travelMinutes;
  }

  uint32_t getColorForLevel(uint8_t level) {
    switch(level) {
      case 0: return TRAFFIC_COLOR_0;  // Very Light - Green
      case 1: return TRAFFIC_COLOR_1;  // Light-Moderate - Lime Green
      case 2: return TRAFFIC_COLOR_2;  // Moderate - Yellow
      case 3: return TRAFFIC_COLOR_3;  // Moderate-Heavy - Amber
      case 4: return TRAFFIC_COLOR_4;  // Heavy - Orange
      case 5: return TRAFFIC_COLOR_5;  // Very Heavy - Red
      default: return TRAFFIC_COLOR_0;
    }
  }

public:
  void setup() override {
    // Empty - segments will be created in loop() after boot completes
  }

  void createSegments() {
    if (segmentsCreated) return;
    
    strip.suspend(); // Required before changing geometry
    
    // Ensure we have 7 segments - append segments if needed
    // Segment 0 already exists, append 6 more
    while (strip.getSegmentsNum() < 7) {
      strip.appendSegment(0, strip.getLengthTotal());
    }
    
    // Now configure each segment with its proper LED range
    
    // Segment 0: Body (LEDs 0-2, length 3)
    Segment& seg0 = strip.getSegment(0);
    seg0.setGeometry(0, 3, 1, 0, 0, 0, 1, 0);
    seg0.setColor(0, TRAFFIC_COLOR_0);
    seg0.mode = STARTUP_BODY_EFFECT;

    // Segment 1: Ring (LEDs 3-66, length 64)
    Segment& seg1 = strip.getSegment(1);
    seg1.setGeometry(3, 67, 1, 0, 0, 0, 1, 0);
    seg1.setColor(0, TRAFFIC_COLOR_0);
    seg1.mode = STARTUP_RING_EFFECT;

    // Segment 2: Body (LEDs 67-69, length 3)
    Segment& seg2 = strip.getSegment(2);
    seg2.setGeometry(67, 70, 1, 0, 0, 0, 1, 0);
    seg2.setColor(0, TRAFFIC_COLOR_0);
    seg2.mode = STARTUP_BODY_EFFECT;

    // Segment 3: Head (LEDs 70-88, length 19)
    Segment& seg3 = strip.getSegment(3);
    seg3.setGeometry(70, 89, 1, 0, 0, 0, 1, 0);
    seg3.setColor(0, TRAFFIC_COLOR_0);
    seg3.mode = STARTUP_BODY_EFFECT;

    // Segment 4: Body (LEDs 89, length 1)
    Segment& seg4 = strip.getSegment(4);
    seg4.setGeometry(89, 90, 1, 0, 0, 0, 1, 0);
    seg4.setColor(0, TRAFFIC_COLOR_0);
    seg4.mode = STARTUP_BODY_EFFECT;

    // Segment 5: Arm (LEDs 90-94, length 5)
    Segment& seg5 = strip.getSegment(5);
    seg5.setGeometry(90, 95, 1, 0, 0, 0, 1, 0);
    seg5.setColor(0, STATUS_SUCCESS);
    seg5.mode = STARTUP_BODY_EFFECT;

    // Segment 6: Body (LEDs 95-97, length 3)
    Segment& seg6 = strip.getSegment(6);
    seg6.setGeometry(95, 98, 1, 0, 0, 0, 1, 0);
    seg6.setColor(0, TRAFFIC_COLOR_0);
    seg6.mode = STARTUP_BODY_EFFECT;

    strip.resume(); // Resume strip operations
    segmentsCreated = true;
  }

  void loop() override {
    unsigned long now = millis();

    // Create segments 2 seconds after boot (after WLED finishes initialization)
    if (!segmentsCreated && now > 2000) {
      Serial.println("[TrafficLamp] Creating segments...");
      Serial.flush();
      createSegments();
      Serial.println("[TrafficLamp] Segments created!");
      Serial.flush();
    }

    // Check if WiFi connected state changed
    bool nowConnected = WLED_CONNECTED;
    if (nowConnected && !wifiConnected) {
      // WiFi just connected - set ring to deep blue/violet
      setSegmentColor(SEG_RING, STATUS_WIFI_CONNECTING);
      wifiConnected = true;
    } else if (!nowConnected && wifiConnected) {
      wifiConnected = false;
    }

    if (!WLED_CONNECTED) return;

    // Initialize timing on first run
    if (lastTrafficCheck == 0) {
      lastTrafficCheck = now - checkInterval + 10000; // First check in 10 seconds
    }

    // Check traffic every 5 minutes
    if (now - lastTrafficCheck > checkInterval) {
      lastTrafficCheck = now;
      
      // Show querying state on Arm (pulsing yellow)
      setSegmentPulse(SEG_ARM, STATUS_QUERYING);

      // Query both destinations
      uint16_t dest1Minutes = queryTraffic(TRAFFIC_START, TRAFFIC_DEST1);
      uint16_t dest2Minutes = queryTraffic(TRAFFIC_START, TRAFFIC_DEST2);

      if (dest1Minutes > 0 && dest2Minutes > 0) {
        // Both queries succeeded
        uint8_t dest1Level = getTrafficLevel(TRAFFIC_DEST1_LOW, TRAFFIC_DEST1_HIGH, dest1Minutes);
        uint8_t dest2Level = getTrafficLevel(TRAFFIC_DEST2_LOW, TRAFFIC_DEST2_HIGH, dest2Minutes);

        // Get colors for each level
        uint32_t dest1Color = getColorForLevel(dest1Level);
        uint32_t dest2Color = getColorForLevel(dest2Level);

        // Apply colors to destinations
        setSegmentColor(SEG_RING, dest1Color);  // Destination 1 -> Ring
        setSegmentColor(SEG_HEAD, dest2Color);  // Destination 2 -> Head

        // Show success on Arm
        setSegmentColor(SEG_ARM, STATUS_SUCCESS);
      } else {
        // One or both queries failed
        setSegmentColor(SEG_ARM, STATUS_ERROR);
      }
    }
  }

  uint16_t getId() override {
    return 0x1001;
  }
};
