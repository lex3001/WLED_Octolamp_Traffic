# Traffic Lamp Usermod

This usermod queries the TomTom Routing API and maps two route travel times to lamp colors.

It is currently implemented for an ESP32 lamp layout with **98 LEDs** and **7 segments**.  
If your lamp has a different LED count or physical layout, you will need to adjust segment geometry in `usermod_traffic_lamp.h`.

## What to put in `my_config.h`

Copy the Traffic Lamp section from `wled00/my_config_sample.h` into your `wled00/my_config.h`, then uncomment and edit values.

Example:

```c
// LED Configuration for Traffic Lamp
#define LEDPIN 33
#define MAXLEDSPERBUS 98

// Route start (origin)
#define TRAFFIC_START "37.7749,-122.4194"

// Destination 1
#define TRAFFIC_DEST1 "37.8199,-122.4783"
#define TRAFFIC_DEST1_LOW 15
#define TRAFFIC_DEST1_HIGH 45
#define TRAFFIC_DEST1_SEGMENT 1

// Destination 2
#define TRAFFIC_DEST2 "37.4275,-122.1697"
#define TRAFFIC_DEST2_LOW 45
#define TRAFFIC_DEST2_HIGH 90
#define TRAFFIC_DEST2_SEGMENT 3

// TomTom API key (required)
#define TOMTOM_API_KEY "your_api_key_here"
```

## Important notes

- Do **not** edit `wled00/my_config_sample.h` directly.
- Put your real values in `wled00/my_config.h`.
- `TOMTOM_API_KEY` is required.
- This usermod currently updates:
  - Destination 1 -> Ring segment (`SEG_RING`, id 1)
  - Destination 2 -> Head segment (`SEG_HEAD`, id 3)
- `TRAFFIC_DEST1_SEGMENT` and `TRAFFIC_DEST2_SEGMENT` are present in config samples, but the current usermod code uses fixed segment IDs above.

## How it works

- On boot, the usermod creates lamp segments (hardcoded for this lamp geometry).
- First traffic check happens shortly after startup, then every 5 minutes.
- For each destination:
  - Query TomTom route travel time
  - Convert minutes to traffic level (0-5) using your LOW/HIGH thresholds
  - Apply color from green (fast) to red (slow)
- Status segment (`SEG_ARM`) shows state:
  - Blue: Wi-Fi connected
  - Yellow pulse: querying
  - Green: success
  - Red: error

## If your lamp is different

This repository’s traffic lamp is a custom layout (98 LEDs / 7 segments).  
If your lamp differs, update these areas in `usermod_traffic_lamp.h`:

- Segment LED ranges in `createSegments()`
- Segment IDs (`SEG_RING`, `SEG_HEAD`, etc.)
- Any startup/default colors or effects you want to change

After edits, rebuild and upload as usual.
