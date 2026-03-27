#pragma once

/*
 * Welcome!
 * You can use the file "my_config.h" to make changes to the way WLED is compiled!
 * It is possible to enable and disable certain features as well as set defaults for some runtime changeable settings.
 *
 * How to use:
 * PlatformIO: Just compile the unmodified code once! The file "my_config.h" will be generated automatically and now you can make your changes.
 *
 * ArduinoIDE: Make a copy of this file and name it "my_config.h". Go to wled.h and uncomment "#define WLED_USE_MY_CONFIG" in the top of the file.
 *
 * DO NOT make changes to the "my_config_sample.h" file directly! Your changes will not be applied.
 */

// uncomment to force the compiler to show a warning to confirm that this file is included
//#warning **** my_config.h: Settings from this file are honored ****

/* Uncomment to use your WIFI settings as defaults
  //WARNING: this will hardcode these as the default even after a factory reset
#define CLIENT_SSID "Your_SSID"
#define CLIENT_PASS "Your_Password"
// Fallback WiFi networks (WLED supports up to 3 SSIDs by default)
//#define CLIENT_SSID2 "Your_Fallback_SSID"
//#define CLIENT_PASS2 "Your_Fallback_Password"
//#define CLIENT_SSID3 "Your_Another_Fallback_SSID"
//#define CLIENT_PASS3 "Your_Another_Fallback_Password"
*/

//#define MAX_LEDS 1500       // Maximum total LEDs. More than 1500 might create a low memory situation on ESP8266.
//#define MDNS_NAME "wled"    // mDNS hostname, ie: *.local

/*
 * Traffic Lamp Usermod Configuration
 * Use this section if you're using the traffic_lamp usermod to display real-time traffic data
 * Get your TomTom API key from: https://developer.tomtom.com/
 * 
 * Uncomment and modify the settings below for your specific locations and traffic routes
 */

// LED Configuration for Traffic Lamp
// #define LEDPIN 33                   // GPIO pin for WS2812B data
// #define MAXLEDSPERBUS 98            // Total number of LEDs

// Traffic Lamp Routing - Start Address (where you're traveling FROM)
// #define TRAFFIC_START "37.7749,-122.4194"      // San Francisco (Ferry Building area)

// Traffic Lamp Destination 1 (will control Ring segment by default)
// #define TRAFFIC_DEST1 "37.8199,-122.4783"      // Golden Gate Bridge
// #define TRAFFIC_DEST1_LOW 15        // Expected minimum travel time (minutes)
// #define TRAFFIC_DEST1_HIGH 45       // Expected maximum travel time (minutes)
// #define TRAFFIC_DEST1_SEGMENT 1     // Which segment to control (0-6)

// Traffic Lamp Destination 2 (will control Head segment by default)
// #define TRAFFIC_DEST2 "37.4275,-122.1697"      // Stanford University
// #define TRAFFIC_DEST2_LOW 45        // Expected minimum travel time (minutes)
// #define TRAFFIC_DEST2_HIGH 90       // Expected maximum travel time (minutes)
// #define TRAFFIC_DEST2_SEGMENT 3     // Which segment to control (0-6)

// TomTom Maps API Key (required for traffic data)
// #define TOMTOM_API_KEY "your_api_key_here"
