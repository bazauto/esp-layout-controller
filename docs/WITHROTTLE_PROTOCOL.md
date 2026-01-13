# WiThrottle Protocol Reference

## Overview
WiThrottle is a text-based TCP/IP protocol for controlling model trains through JMRI. Commands and responses are newline-terminated ASCII strings.

**Protocol Version**: 2.0 (required minimum)  
**Transport**: TCP/IP socket  
**Discovery**: ZeroConf/Bonjour mDNS `_withrottle._tcp.local`  
**Newline**: Accept any of CR (0x0D), LF (0x0A), or CRLF (0x0D 0x0A)

## Array Delimiters
WiThrottle uses special delimiters for arrays:
- `]\[` - Between major elements (roster entries, turnouts, etc.)
- `}|{` - Between fields within an element
- `<;>` - Between parts of throttle commands
- `<:>` - In consist commands

Example roster with 2 locos:
```
RL2]\[RGS 41}|{41}|{L]\[Test Loco}|{1234}|{L
```

## Connection Sequence

### 1. Client Connects
Open TCP socket to JMRI (default port varies, typically 12090)

### 2. Server Sends Initial Info
```
VN2.0                              // Protocol version
RL2]\[RGS 41}|{41}|{L]\[...       // Roster list
PPA1                               // Track power state
PTT]\[...                          // Turnout captions (optional)
PTL]\[...                          // Turnout list (optional)
PRT]\[...                          // Route captions (optional)
PRL]\[...                          // Route list (optional)
RCC0                               // Consist count
PW12080                            // Web port
*10                                // Heartbeat seconds (after N command)
```

### 3. Client Identifies Itself
```
HU<deviceId>                       // Hardware/device ID (unique per device)
N<deviceName>                      // Device name (shown in JMRI)
```

### 4. Server Responds
```
*10                                // Heartbeat interval (seconds, 0=disabled)
```

### 5. Enable Heartbeat (Recommended)
```
*+                                 // Turn on heartbeat monitoring
```

## Roster Format

### Roster List (RL)
**Server → Client** on connection

`RL<count>]\[<entry>]\[<entry>...`

Each entry: `<name>}|{<address>}|{<length>`

Example:
```
RL3]\[RGS 41}|{41}|{L]\[SP 2101}|{2102}|{L]\[Switcher}|{3}|{S
```

Fields:
- **Name**: Display name (e.g., "RGS 41")
- **Address**: DCC address number (e.g., "41", "2102")
- **Length**: `S` = Short address, `L` = Long address

## Throttle Control

### Throttle IDs
Multi-throttle system uses single-character IDs:
- `0`-`9`: Ten throttle instances
- `T`, `S`, `G`: Aliases for `0`, `1`, `2`

### Add Locomotive to Throttle
**Client → Server**

`M<throttleId>+<locoKey><;><locoAddress>`

Examples:
```
M0+L41<;>L41                      // Add long address 41 by address
M0+S3<;>ES3                       // Add short address 3 by roster ("E" prefix)
M1+L2102<;>ESP 2101               // Add from roster entry "SP 2101"
```

**Server → Client** replies with loco info:
```
M0+L41<;>                          // Confirmation
M0LL41<;>]\[Headlight]\[Bell]\[Whistle]\[...]  // Function labels
M0AL41<;>F00                       // Function 0 state (0=off)
M0AL41<;>F10                       // Function 1 state (1=on)
... (all functions 0-28)
M0AL41<;>V0                        // Current speed (0-126)
M0AL41<;>R1                        // Direction (0=reverse, 1=forward)
M0AL41<;>s1                        // Speed step mode (1=128, 2=28, 4=27, 8=14)
```

### Remove Locomotive from Throttle
**Client → Server**

`M<throttleId>-<locoKey><;>r`

Release specific loco:
```
M0-L41<;>r
```

Release all locos on throttle:
```
M0-*<;>r
```

### Set Speed
**Client → Server**

`M<throttleId>A<locoKey><;>V<speed>`

- **Speed**: 0-126 (128 speed steps, 0=stop, 1=emergency stop in some systems)
- **LocoKey**: Specific loco address or `*` for all locos on throttle

Examples:
```
M0A*<;>V30                        // Set all locos on throttle 0 to speed 30
M0AL41<;>V65                      // Set loco 41 to speed 65
M0A*<;>V0                         // Stop all locos
```

### Set Direction
**Client → Server**

`M<throttleId>A<locoKey><;>R<direction>`

- **Direction**: `0` = Reverse, `1` = Forward

Examples:
```
M0A*<;>R1                         // Set all locos forward
M0AL41<;>R0                       // Set loco 41 reverse
```

### Emergency Stop
**Client → Server**

`M<throttleId>A<locoKey><;>X`

Examples:
```
M0A*<;>X                          // E-stop all locos on throttle 0
M0AL41<;>X                        // E-stop only loco 41
```

### Functions
**Client → Server**

`M<throttleId>A<locoKey><;>F<state><functionNum>`

- **State**: `0` = Release button, `1` = Press button
- **FunctionNum**: 0-31 (no leading zeros!)

Always send press AND release (JMRI handles momentary vs latching):
```
M0A*<;>F10                        // Press F0 (headlight)
M0A*<;>F00                        // Release F0
M0A*<;>F112                       // Press F12
M0A*<;>F012                       // Release F12
```

**Force Function** (set state without toggle):
```
M0A*<;>f112                       // Force F12 ON (stays on even if already on)
M0A*<;>f012                       // Force F12 OFF
```

**Set Momentary/Latching**:
```
M0A*<;>m112                       // Make F12 momentary
M0A*<;>m012                       // Make F12 latching
```

### Query Current State
**Client → Server**

```
M0A*<;>qV                         // Query speed
M0A*<;>qR                         // Query direction
```

**Server → Client** responds:
```
M0AL41<;>V25                      // Speed is 25
M0AL41<;>R1                       // Direction is forward
```

## Throttle Change Notifications
**Server → Client** - Sent automatically when properties change (from JMRI UI or other throttles)

`M<throttleId><type><locoId><;><property>`

Types:
- `+` = Loco added to throttle
- `-` = Loco removed from throttle
- `A` = Action/property changed
- `L` = Function list (after adding loco)
- `S` = Steal required

Examples:
```
M0AL41<;>F10                      // Function 0 turned on
M0AL41<;>V50                      // Speed changed to 50
M0AL41<;>R0                       // Direction changed to reverse
M0+L41<;>                         // Loco added
M0-L41<;>                         // Loco removed
```

## Heartbeat System

### Initial Setup
After sending `N<deviceName>`, server responds:
```
*10                               // Send heartbeat every 10 seconds
```

### Enable Monitoring
**Client → Server**
```
*+                                // Enable heartbeat monitoring
```

### Send Heartbeats
**Client → Server** - Must send within heartbeat interval or JMRI will E-stop locos
```
*                                 // Heartbeat (any command also counts)
```

### Disable Monitoring
```
*-                                // Disable heartbeat (not recommended)
```

## Track Power

### Initial State
**Server → Client** on connection
```
PPA<state>
```
- **State**: `0` = Off, `1` = On, `2` = Unknown

### Power Change Notification
**Server → Client** when power changes
```
PPA1                              // Power turned on
PPA0                              // Power turned off
```

### Request Power Change
(Not standard in WiThrottle protocol - typically done via JMRI Panel/Button commands or separate interface)

## Error Messages

### Alert Message
**Server → Client**
```
HM<message>                       // Error alert (show to user)
Hm<message>                       // Info message (informational)
```

Example:
```
HMJMRI: address 'L23' not allowed as Long
```

### Server Type
**Server → Client** on connection
```
HT<type>                          // Server type: "JMRI", "Digitrax", "MRC"
Ht<description>                   // Full description string
```

Example:
```
HTJMRI
HtJMRI v4.19.8 My Railroad
```

## Implementation Notes for This Project

### Speed Steps
- Use **128 speed steps** (speed values 0-126)
- JMRI will handle downscaling for locos that need 28/14 steps
- Speed step mode from roster: `s1` = 128 steps

### Roster Management
- Parse roster on connection: `RL<count>]\[<entries>`
- Store up to **50 locos** (command station max)
- Navigate roster with rotary encoder during loco selection

### Multiple Throttles
- Implement **4 throttles** (use IDs `0`, `1`, `2`, `3`)
- Each can control one loco at a time
- Bi-directional sync: watch for `M<id>A` notifications from server

### Heartbeat
- **CRITICAL**: Must implement heartbeat or locos will emergency stop
- Default interval: 10 seconds
- Send `*` command or any other command to keep alive
- Implement in background task, not UI thread

### Function Labels
- Server sends function labels after adding loco: `M<id>LL<addr><;>]\[<labels>]\[`
- Parse array (delimited by `]\[`)
- Empty labels are just `]\[]\[` (blank function slot)
- Display in function button UI on detail panel

### Bidirectional Updates
- Subscribe to all `M<throttleId>A` messages for throttles you control
- Update local UI immediately when server sends notifications
- Handles cases where another throttle (physical or software) controls same loco

## Example Session

```
// Client connects
<- VN2.0
<- RL2]\[RGS 41}|{41}|{L]\[SP 2101}|{2102}|{L
<- PPA1
<- RCC0
<- PW12080

// Client identifies itself
-> HUmac-aa-bb-cc-dd-ee
-> NModel Railway Controller

// Server responds with heartbeat interval
<- *10

// Client enables heartbeat
-> *+

// Client adds loco to throttle 0
-> M0+L41<;>L41

// Server sends loco info
<- M0+L41<;>
<- M0LL41<;>]\[Lights]\[Bell]\[Whistle]\[]\[...
<- M0AL41<;>F00
<- M0AL41<;>F10
... (functions 2-28)
<- M0AL41<;>V0
<- M0AL41<;>R1
<- M0AL41<;>s1

// Client sets speed to 50
-> M0A*<;>V50
<- M0AL41<;>V50

// Client turns on headlight (F0)
-> M0A*<;>F10
-> M0A*<;>F00
<- M0AL41<;>F10

// Another throttle changes speed (JMRI or hardware)
<- M0AL41<;>V25

// Client sends heartbeat every ~9 seconds
-> *
-> *
...

// Client releases loco
-> M0-*<;>r
<- M0-L41<;>
```

## References
- Official JMRI Documentation: https://www.jmri.org/help/en/package/jmri/jmrit/withrottle/Protocol.shtml
- DeviceServer.java: Main command dispatcher
- ThrottleController.java: Throttle command handler
- MultiThrottle.java: Multi-loco support
