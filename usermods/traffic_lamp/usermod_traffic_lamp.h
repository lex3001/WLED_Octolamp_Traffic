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

// Traffic Level Colors (RGB values) - Green to Magenta scale
#define TRAFFIC_COLOR_0 RGBW32(0,   255, 0,   0)  // All Clear           - Pure Green
#define TRAFFIC_COLOR_1 RGBW32(255, 255, 0,   0)  // Starting to build   - Yellow
#define TRAFFIC_COLOR_2 RGBW32(255, 165, 0,   0)  // Noticeable delay    - Orange
#define TRAFFIC_COLOR_3 RGBW32(255, 69,  0,   0)  // Frustrating traffic - Deep Orange
#define TRAFFIC_COLOR_4 RGBW32(255, 0,   0,   0)  // Heavy Congestion    - Pure Red
#define TRAFFIC_COLOR_5 RGBW32(139, 0,   0,   0)  // Severe Delay        - Dark Red
#define TRAFFIC_COLOR_6 RGBW32(255, 0,   255, 0)  // Total Gridlock      - Magenta

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
  const uint32_t minFreeHeapForQuery = 45000; // conservative TLS safety margin for ESP32
  const uint8_t dnsRetryCount = 3;
  const uint16_t dnsRetryDelayMs = 250;
  const char* tomTomHost = "api.tomtom.com";
  bool wifiConnected = false;
  bool segmentsCreated = false;
  bool startupAnimationActive = true;
  bool startupAnimationCompleted = false;
  bool hasCachedTomTomIp = false;
  IPAddress cachedTomTomIp;
  unsigned long lastStartupFrame = 0;
  uint8_t startupPhase = 0;
  const unsigned long startupStepMs = 350;

  #ifndef TOMTOM_API_KEY
    #define TOMTOM_API_KEY "YOUR_API_KEY"
  #endif
  String apiKey = TOMTOM_API_KEY;

  uint32_t getMaxAllocBlock() {
    #ifdef ESP32
      return ESP.getMaxAllocHeap();
    #else
      return ESP.getFreeHeap();
    #endif
  }

  void logWiFiState(const char* tag) {
    Serial.printf("[TrafficLamp] %s WiFi.status=%d, connected=%d, RSSI=%d, IP=%s\n",
      tag,
      WiFi.status(),
      WLED_CONNECTED ? 1 : 0,
      WiFi.RSSI(),
      WiFi.localIP().toString().c_str());

    Serial.printf("[TrafficLamp] DNS servers: %s, %s\n",
      WiFi.dnsIP(0).toString().c_str(),
      WiFi.dnsIP(1).toString().c_str());
  }

  bool resolveTomTomHost(IPAddress& tomtomIp) {
    for (uint8_t attempt = 1; attempt <= dnsRetryCount; attempt++) {
      if (WiFi.hostByName(tomTomHost, tomtomIp)) {
        if (attempt > 1) {
          Serial.printf("[TrafficLamp] DNS recovered on retry %u\n", attempt);
        }
        return true;
      }

      Serial.printf("[TrafficLamp] DNS resolve attempt %u/%u failed\n", attempt, dnsRetryCount);
      delay(dnsRetryDelayMs);
    }

    return false;
  }

  void cacheTomTomIp(const IPAddress& ip) {
    cachedTomTomIp = ip;
    hasCachedTomTomIp = true;
    Serial.printf("[TrafficLamp] Cached TomTom IP: %s\n", cachedTomTomIp.toString().c_str());
  }

  bool getTomTomRequestHost(char* outHost, size_t outLen, bool& forceTomTomHostHeader) {
    IPAddress tomtomIp;
    forceTomTomHostHeader = false;

    if (resolveTomTomHost(tomtomIp)) {
      cacheTomTomIp(tomtomIp);
      snprintf(outHost, outLen, "%s", tomTomHost);
      return true;
    }

    Serial.println("[TrafficLamp] DNS failed; attempting WiFi reconnect");
    WiFi.reconnect();
    delay(1000);

    if (resolveTomTomHost(tomtomIp)) {
      cacheTomTomIp(tomtomIp);
      logWiFiState("DNS recovered after reconnect:");
      snprintf(outHost, outLen, "%s", tomTomHost);
      return true;
    }

    if (hasCachedTomTomIp) {
      forceTomTomHostHeader = true;
      String ipString = cachedTomTomIp.toString();
      snprintf(outHost, outLen, "%s", ipString.c_str());
      Serial.printf("[TrafficLamp] DNS unavailable; using cached TomTom IP fallback: %s\n", outHost);
      return true;
    }

    // Do not hard-fail on DNS precheck: let HTTPClient try normal hostname resolution.
    // This avoids false negatives from transient hostByName() failures.
    Serial.println("[TrafficLamp] DNS precheck failed and no cache; proceeding with hostname request");
    logWiFiState("DNS precheck warning:");
    snprintf(outHost, outLen, "%s", tomTomHost);
    return true;
  }

  bool networkReadyForQuery() {
    if (!WLED_CONNECTED || WiFi.status() != WL_CONNECTED) {
      logWiFiState("Network check failed:");
      return false;
    }

    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t maxAlloc = getMaxAllocBlock();
    if (freeHeap < minFreeHeapForQuery || maxAlloc < (minFreeHeapForQuery / 2)) {
      Serial.printf("[TrafficLamp] Heap too low for TLS query. free=%u maxBlock=%u\n", freeHeap, maxAlloc);
      return false;
    }

    Serial.printf("[TrafficLamp] Network OK. freeHeap=%u, maxBlock=%u\n", freeHeap, maxAlloc);
    return true;
  }

  void getDateTime(char* out, size_t outLen) {
    char timeBuf[32] = {0};
    getTimeString(timeBuf);
    snprintf(out, outLen, "%s", timeBuf);
  }

  void logLoopTimestamp(const char* phase) {
    char dt[32] = {0};
    getDateTime(dt, sizeof(dt));
    Serial.printf("[TrafficLamp] [%s] %s\n", dt, phase);
  }

  const char* httpErrorToText(int code) {
    switch (code) {
      case -1:  return "CONNECTION_REFUSED";
      case -2:  return "SEND_HEADER_FAILED";
      case -3:  return "SEND_PAYLOAD_FAILED";
      case -4:  return "NOT_CONNECTED";
      case -5:  return "CONNECTION_LOST";
      case -6:  return "NO_STREAM";
      case -7:  return "NO_HTTP_SERVER";
      case -8:  return "TOO_LESS_RAM";
      case -9:  return "ENCODING";
      case -10: return "STREAM_WRITE";
      case -11: return "READ_TIMEOUT";
      default:  return "UNKNOWN";
    }
  }

  void logTransportFailure(int httpCode, const char* phaseTag) {
    char dt[32] = {0};
    getDateTime(dt, sizeof(dt));
    Serial.printf("[TrafficLamp] [%s] %s transport error: %d (%s)\n",
      dt, phaseTag, httpCode, httpErrorToText(httpCode));
    logWiFiState("Transport failure state:");
    Serial.printf("[TrafficLamp] Heap at failure: free=%u maxBlock=%u\n", ESP.getFreeHeap(), getMaxAllocBlock());
  }

  // Helper function to calculate traffic level (0-6) based on predicted time
  // Takes low threshold, high threshold, and predicted minutes
  // Returns: 0-5 for in-range levels, 6 for exceeding high threshold
  uint8_t getTrafficLevel(uint16_t lowMinutes, uint16_t highMinutes, uint16_t predictedMinutes) {
    // If below low threshold, very light traffic
    if (predictedMinutes <= lowMinutes) {
      return 0;
    }
    
    // If above high threshold, very heavy traffic
    if (predictedMinutes >= highMinutes) {
      return 6;
    }
    
    // Calculate level based on position between low and high
    uint16_t range = highMinutes - lowMinutes;
    uint32_t difference = predictedMinutes - lowMinutes;
    uint8_t level = (difference * 6) / range;
    
    // Clamp to 0-5
    if (level > 5) level = 5;
    
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

  void applyStartupSequenceFrame(uint8_t phase) {
    const uint32_t dimBlue = RGBW32(0, 0, 16, 0);
    const uint32_t dimAmber = RGBW32(20, 8, 0, 0);
    const uint32_t brightYellow = RGBW32(255, 190, 0, 0);

    setSegmentColor(SEG_BODY_1, dimAmber);
    setSegmentColor(SEG_BODY_2, dimAmber);
    setSegmentColor(SEG_BODY_3, dimAmber);
    setSegmentColor(SEG_BODY_4, dimAmber);
    setSegmentColor(SEG_RING, dimBlue);
    setSegmentColor(SEG_HEAD, dimBlue);
    setSegmentColor(SEG_ARM, dimAmber);

    switch (phase % 4) {
      case 0:
        setSegmentColor(SEG_RING, brightYellow);
        break;
      case 1:
        setSegmentColor(SEG_HEAD, brightYellow);
        break;
      case 2:
        setSegmentColor(SEG_ARM, brightYellow);
        break;
      case 3:
        setSegmentColor(SEG_BODY_2, brightYellow);
        setSegmentColor(SEG_BODY_3, brightYellow);
        break;
    }
  }

  void runStartupSequence(unsigned long now) {
    if (!startupAnimationActive || !segmentsCreated) return;
    if (lastStartupFrame != 0 && (now - lastStartupFrame) < startupStepMs) return;

    lastStartupFrame = now;
    applyStartupSequenceFrame(startupPhase);
    startupPhase = (startupPhase + 1) % 4;
  }

  // Internal: Query traffic using pre-allocated client (avoids repeated TLS heap pressure)
  uint16_t queryTrafficWithClient(WiFiClientSecure* client, const char* requestHost, bool forceTomTomHostHeader, const char* origin, const char* destination) {
    if (!WLED_CONNECTED) return 0;

    if (apiKey == "YOUR_API_KEY") {
      Serial.println("[TrafficLamp] ERROR: TOMTOM_API_KEY not set");
      return 0;
    }

    char dt[32] = {0};
    getDateTime(dt, sizeof(dt));
    Serial.printf("[TrafficLamp] [%s] Querying: %s -> %s\n", dt, origin, destination);

    String url = "https://";
    url.reserve(256);
    url += requestHost;
    url += "/routing/1/calculateRoute/";
    url += origin;
    url += ":";
    url += destination;
    url += "/json?key=";
    url += apiKey;
    url += "&traffic=true&travelMode=car&routeType=eco&avoid=carpools&routeRepresentation=summaryOnly";

    uint16_t travelMinutes = 0;

    // Scoping block: HTTPClient destructor MUST run before client is reused
    // (fixes ESP32 Arduino core 2.0.x bug where HTTPClient doesn't fully release connection)
    {
      HTTPClient http;
      http.setTimeout(15000);
      http.setReuse(false); // close socket after request; avoids stale keep-alive states
      http.begin(*client, url);
      if (forceTomTomHostHeader) {
        http.addHeader("Host", tomTomHost);
      }

      int httpCode = http.GET();

      // One-shot transport retry: recover from stale socket / transient net stack issues
      if (httpCode < 0) {
        logTransportFailure(httpCode, "First attempt");
        Serial.println("[TrafficLamp] Reconnecting WiFi and retrying once...");
        http.end();
        WiFi.reconnect();
        delay(1000);

        http.begin(*client, url);
        if (forceTomTomHostHeader) {
          http.addHeader("Host", tomTomHost);
        }
        httpCode = http.GET();
        if (httpCode < 0) {
          logTransportFailure(httpCode, "Retry attempt");
        }
      }

      Serial.printf("[TrafficLamp] HTTP code: %d\n", httpCode);

      if (httpCode == 200) {
        WiFiClient* stream = http.getStreamPtr();
        const char* token = "\"travelTimeInSeconds\":";
        const int tokenLen = strlen(token);
        const int chunkSize = 64;
        char buffer[chunkSize * 2 + 1] = {0};
        int bufferLen = 0;
        bool found = false;
        unsigned long streamStart = millis();

        while (http.connected() && !found) {
          // 10 second hard timeout to prevent infinite loop
          if (millis() - streamStart > 10000) {
            Serial.println("[TrafficLamp] Stream timeout");
            break;
          }

          int avail = stream->available();
          if (avail <= 0) {
            delay(1);
            continue;
          }

          int carry = min(bufferLen, tokenLen);
          if (carry > 0) {
            memmove(buffer, buffer + bufferLen - carry, carry);
          }
          bufferLen = carry;

          int bytesRead = stream->readBytes(buffer + bufferLen, min(avail, chunkSize));
          bufferLen += bytesRead;
          buffer[bufferLen] = '\0';

          char* pos = strstr(buffer, token);
          if (pos) {
            char* valueStart = pos + tokenLen;
            while (*valueStart == ' ') valueStart++;
            uint32_t travelSeconds = (uint32_t)atoi(valueStart);
            travelMinutes = (uint16_t)((travelSeconds + 30) / 60);
            Serial.printf("[TrafficLamp] Travel time: %u min (%u sec)\n", travelMinutes, travelSeconds);
            found = true;
          }
        }

        if (!found) {
          Serial.println("[TrafficLamp] ERROR: travelTimeInSeconds not found");
        }
      } else {
        Serial.printf("[TrafficLamp] HTTP error: %d\n", httpCode);
      }

      http.end();
    } // HTTPClient destructor runs HERE, fully releasing the connection

    return travelMinutes;
  }

  // Public wrapper for backward compatibility - allocates fresh client
  uint16_t queryTraffic(const char* requestHost, bool forceTomTomHostHeader, const char* origin, const char* destination) {
    WiFiClientSecure client;
    client.setInsecure();
    uint16_t result = queryTrafficWithClient(&client, requestHost, forceTomTomHostHeader, origin, destination);
    return result;
  }

  uint32_t getColorForLevel(uint8_t level) {
    switch(level) {
      case 0: return TRAFFIC_COLOR_0;  // All Clear - Pure Green
      case 1: return TRAFFIC_COLOR_1;  // Starting to build - Yellow
      case 2: return TRAFFIC_COLOR_2;  // Noticeable delay - Orange
      case 3: return TRAFFIC_COLOR_3;  // Frustrating traffic - Deep Orange
      case 4: return TRAFFIC_COLOR_4;  // Heavy Congestion - Pure Red
      case 5: return TRAFFIC_COLOR_5;  // Severe Delay - Dark Red
      case 6: return TRAFFIC_COLOR_6;  // Total Gridlock - Magenta
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
    startupAnimationActive = !startupAnimationCompleted;
    startupPhase = 0;
    lastStartupFrame = 0;
    runStartupSequence(millis());
  }

  void loop() override {
    unsigned long now = millis();

    // Create segments shortly after boot, then keep startup sequence active until WiFi connects
    if (!segmentsCreated && now > 500) {
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
      logLoopTimestamp("WiFi transition: connected");
      logWiFiState("WiFi connected state:");
      startupAnimationActive = false;
      startupAnimationCompleted = true;
      setSegmentColor(SEG_BODY_1, STATUS_WIFI_CONNECTING);
      setSegmentColor(SEG_BODY_2, STATUS_WIFI_CONNECTING);
      setSegmentColor(SEG_BODY_3, STATUS_WIFI_CONNECTING);
      setSegmentColor(SEG_BODY_4, STATUS_WIFI_CONNECTING);
      setSegmentColor(SEG_RING, STATUS_WIFI_CONNECTING);
      wifiConnected = true;
    } else if (!nowConnected && wifiConnected) {
      logLoopTimestamp("WiFi transition: disconnected");
      logWiFiState("WiFi disconnected state:");
      wifiConnected = false;
    }

    if (!WLED_CONNECTED) {
      runStartupSequence(now);
      return;
    }

    // Initialize timing on first run
    if (lastTrafficCheck == 0) {
      lastTrafficCheck = now - checkInterval + 10000; // First check in 10 seconds
    }

    // Check traffic every 5 minutes
    if (now - lastTrafficCheck > checkInterval) {
      lastTrafficCheck = now;
      logLoopTimestamp("Traffic check loop");
      
      // Show querying state on Arm (pulsing yellow)
      setSegmentPulse(SEG_ARM, STATUS_QUERYING);

      if (!networkReadyForQuery()) {
        setSegmentColor(SEG_ARM, STATUS_ERROR);
        return;
      }

      // Log heap state before traffic queries
      Serial.printf("[TrafficLamp] Free heap before queries: %u, max block: %u\n", 
          ESP.getFreeHeap(), getMaxAllocBlock());

      char requestHost[48] = {0};
      bool forceTomTomHostHeader = false;
      if (!getTomTomRequestHost(requestHost, sizeof(requestHost), forceTomTomHostHeader)) {
        setSegmentColor(SEG_ARM, STATUS_ERROR);
        return;
      }

      // Query with short-lived clients to guarantee full cleanup per request
      uint16_t dest1Minutes = queryTraffic(requestHost, forceTomTomHostHeader, TRAFFIC_START, TRAFFIC_DEST1);
      uint16_t dest2Minutes = queryTraffic(requestHost, forceTomTomHostHeader, TRAFFIC_START, TRAFFIC_DEST2);

      // Log heap state after queries
      Serial.printf("[TrafficLamp] Free heap after queries: %u, max block: %u\n", 
          ESP.getFreeHeap(), getMaxAllocBlock());

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
