# JMRI JSON/HTTP API Reference

## Overview
JMRI provides a RESTful JSON API via HTTP and WebSockets for controlling layout elements. This complements the WiThrottle protocol and provides access to features not available in WiThrottle.

**Base URL**: `http://<jmri-ip>:12080` (default port)  
**Protocol**: HTTP REST and WebSocket  
**Format**: JSON  
**Discovery**: Same as WiThrottle (port from `PW` message + web server)

## Why Use JSON API vs WiThrottle?

**Use WiThrottle for:**
- ✅ Throttle control (speed, direction, functions)
- ✅ Roster management
- ✅ Bidirectional throttle sync
- ✅ Real-time notifications

**Use JSON API for:**
- ✅ **Track power control** (on/off/query)
- ✅ Turnout/Point control
- ✅ Sensor monitoring
- ✅ Route activation
- ✅ Memory variables
- ✅ Block status
- ✅ Signal states (future)

## HTTP REST API

### Track Power Control

#### Get Power State
**GET** `/json/power`
```json
Request: (none needed)
Response: {"type":"power","data":{"name":"LocoNet","state":2,"default":true}}
```

States:
- `0` = OFF
- `2` = ON
- `4` = UNKNOWN

#### Turn Power ON
**POST** `/json/power`
```json
Request: {"type":"power","data":{"state":2}}
Response: {"type":"power","data":{"name":"LocoNet","state":2,"default":true}}
```

#### Turn Power OFF
**POST** `/json/power`
```json
Request: {"type":"power","data":{"state":0}}
Response: {"type":"power","data":{"name":"LocoNet","state":0,"default":true}}
```

### Turnout/Point Control

#### Get Turnout State
**GET** `/json/turnout/<name>`
```json
Request: GET /json/turnout/IT99
Response: {
  "type":"turnout",
  "data":{
    "name":"IT99",
    "state":4,
    "userName":"Main Line Switch",
    "comment":null,
    "inverted":false,
    "feedbackMode":1
  }
}
```

States:
- `2` = CLOSED/NORMAL
- `4` = THROWN/REVERSE
- `0` = UNKNOWN

#### Set Turnout State
**POST** `/json/turnout/<name>`
```json
Request: {"type":"turnout","data":{"name":"IT99","state":2}}
Response: {"type":"turnout","data":{"name":"IT99","state":2,...}}
```

### Sensor Monitoring

#### Get Sensor State
**GET** `/json/sensor/<name>`
```json
Request: GET /json/sensor/IS2
Response: {
  "type":"sensor",
  "data":{
    "name":"IS2",
    "state":2,
    "userName":"Block 1 Occupied",
    "comment":null,
    "inverted":false
  }
}
```

States:
- `2` = ACTIVE
- `4` = INACTIVE
- `0` = UNKNOWN

### Route Control

#### Activate Route
**POST** `/json/route/<name>`
```json
Request: {"type":"route","data":{"name":"IR:AUTO:0001","state":2}}
Response: {"type":"route","data":{"name":"IR:AUTO:0001","state":2,...}}
```

### Memory Variables

#### Get Memory Value
**GET** `/json/memory/<name>`
```json
Request: GET /json/memory/IMCURRENTTIME
Response: {
  "type":"memory",
  "data":{
    "name":"IMCURRENTTIME",
    "value":"2:53 PM",
    "userName":null
  }
}
```

### List Available Objects

#### Get All Objects of a Type
**GET** `/json/<type>`
```json
Request: GET /json/sensors
Response: [
  {"type":"sensor","data":{"name":"IS1",...}},
  {"type":"sensor","data":{"name":"IS2",...}},
  ...
]
```

Valid types: sensors, turnouts, routes, lights, blocks, memories, reporters, etc.

#### Get All Valid Types
**GET** `/json/type`
```json
Response: [
  "block", "car", "consist", "engine", "layoutBlock", "light",
  "location", "memory", "metadata", "panel", "power", "railroad",
  "reporter", "roster", "route", "sensor", "signalHead", "signalMast",
  "systemConnection", "train", "track", "turnout", etc.
]
```

## WebSocket API (Real-time Updates)

### Connect
```
ws://<jmri-ip>:12080/json
```

### Message Format
All messages are JSON with `type`, optional `method`, and `data`:
```json
{
  "type": "power",
  "method": "get",  // get, post, put, delete (default: get)
  "data": {...}
}
```

### Subscribe to Changes
**GET** requests automatically subscribe to changes:
```json
Send: {"type":"power"}
Receive: {"type":"power","data":{"state":2,...}}
// Future power changes automatically sent:
Receive: {"type":"power","data":{"state":0,...}}
```

### Power Control via WebSocket
```json
// Get power state (subscribes to changes)
Send: {"type":"power"}
Receive: {"type":"power","data":{"state":2,"default":true}}

// Turn power on
Send: {"type":"power","method":"post","data":{"state":2}}
Receive: {"type":"power","data":{"state":2,"default":true}}

// Turn power off
Send: {"type":"power","method":"post","data":{"state":0}}
Receive: {"type":"power","data":{"state":0,"default":true}}
```

### Sensor Monitoring via WebSocket
```json
// Subscribe to sensor
Send: {"type":"sensor","data":{"name":"IS2"}}
Receive: {"type":"sensor","data":{"name":"IS2","state":4,...}}
// Future changes automatically sent:
Receive: {"type":"sensor","data":{"name":"IS2","state":2,...}}
```

### Ping/Pong
```json
Send: {"type":"ping"}
Receive: {"type":"pong"}
```

## Implementation Strategy for ESP32

### Two-Protocol Approach
1. **WiThrottle TCP Socket** - Primary throttle control
   - Locomotive speed/direction/functions
   - Roster retrieval
   - Multi-throttle sync
   - Heartbeat

2. **JMRI JSON HTTP** - Layout control
   - Track power on/off
   - Turnout control (future)
   - Sensor monitoring (future)
   - Route activation (future)

### Connection Details
Both use same JMRI server IP:
- **WiThrottle**: Discover port via mDNS or use standard port (12090)
- **JSON/HTTP**: Use port from `PW` message in WiThrottle handshake (typically 12080)

### Architecture
```
ESP32
  ├── WiThrottleClient (TCP socket)
  │   ├── Throttle control
  │   ├── Roster management
  │   └── Real-time sync
  │
  └── JMRIHttpClient (HTTP)
      ├── Track power control
      ├── Turnout control (future)
      └── Sensor queries (future)
```

### HTTP vs WebSocket Choice
**Recommendation: Start with HTTP, add WebSocket later if needed**

**HTTP Advantages:**
- ✅ Simpler implementation
- ✅ No additional connection to manage
- ✅ Request/response model matches UI pattern
- ✅ Sufficient for infrequent operations (power on/off)
- ✅ Less memory overhead

**WebSocket Advantages:**
- Real-time push notifications
- Better for continuous sensor monitoring
- Single persistent connection
- More efficient for frequent updates

**For this project:**
- Track power control is **infrequent** → HTTP is perfect
- WiThrottle already handles real-time throttle updates
- Can add WebSocket later for sensors if needed (Phase 7)

## Example HTTP Implementation (ESP32)

### Track Power ON
```cpp
// Using ESP-IDF HTTP client
esp_http_client_config_t config = {
    .url = "http://192.168.1.100:12080/json/power",
    .method = HTTP_METHOD_POST,
};
esp_http_client_handle_t client = esp_http_client_init(&config);

const char* post_data = "{\"type\":\"power\",\"data\":{\"state\":2}}";
esp_http_client_set_post_field(client, post_data, strlen(post_data));
esp_http_client_set_header(client, "Content-Type", "application/json");

esp_err_t err = esp_http_client_perform(client);
// Parse JSON response...
esp_http_client_cleanup(client);
```

### Query Power State
```cpp
esp_http_client_config_t config = {
    .url = "http://192.168.1.100:12080/json/power",
    .method = HTTP_METHOD_GET,
};
esp_http_client_handle_t client = esp_http_client_init(&config);
esp_err_t err = esp_http_client_perform(client);
// Read response and parse JSON...
esp_http_client_cleanup(client);
```

## JSON Parsing
Use a lightweight JSON library for ESP32:
- **cJSON** (included with ESP-IDF) - Simple and efficient
- **ArduinoJson** (if using Arduino framework)

## References
- Official JMRI JSON Documentation: https://www.jmri.org/help/en/html/web/JsonServlet.shtml
- Protocol Details: https://www.jmri.org/JavaDoc/doc/jmri/server/json/package-summary.html
- Power Demo Example: http://localhost:12080/web/power.html
- JSON Console (for testing): http://localhost:12080/json/

## Testing
1. Start JMRI Web Server (Tools → Servers → Start JMRI Web Server)
2. Open browser to http://localhost:12080/json/ for WebSocket console
3. Test commands before implementing in firmware
4. Use browser dev tools to see actual JSON messages
