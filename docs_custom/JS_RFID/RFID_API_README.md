# 🏷️ RFID JavaScript API for Bruce Firmware

JavaScript bindings for RFID tag reading operations in Bruce firmware. Scripts can now access RFID readers directly via `require("rfid")` without launching the Tag-O-Matic UI.
Author:

---

## 📌 Overview

New features:

- ✅ Read **complete tag information** (UID, type, SAK, ATQA, memory dump, page count)
- ✅ Read **UID only** for fast identification
- ✅ Support for **PN532** (I2C/SPI) and **RC522/RFID2** modules
- ✅ **Headless mode** - no Tag-O-Matic UI interference
- ✅ **Configurable timeout** for tag detection
- ✅ Fully integrated into JS interpreter via `require("rfid")`

---

## 📚 JavaScript API

### Import Module

```javascript
const rfid = require("rfid");
```

### `rfid.read(timeoutSeconds)`

Reads complete tag information including all available fields.

**Signature:**
```typescript
rfid.read(timeoutSeconds?: number): object | null
```

**Parameters:**
- `timeoutSeconds` (optional) — Wait time in seconds. Default: `10`

**Returns:**
```javascript
{
  uid: string,         // Tag UID (hex)
  type: string,        // Tag type (e.g., "MIFARE Ultralight")
  sak: string,         // Select Acknowledge
  atqa: string,        // Answer To Request Type A
  bcc: string,         // Block Check Character
  pages: string,       // Raw memory dump
  totalPages: number   // Total memory pages
}
```

Returns `null` on timeout or error.

**Example:**
```javascript
const rfid = require("rfid");

const tag = rfid.read(10);

if (tag) {
    console.log("✓ Tag detected!");
    console.log("UID:", tag.uid);
    console.log("Type:", tag.type);
    console.log("Pages:", tag.totalPages);
} else {
    console.log("✗ No tag detected");
}
```

---

### `rfid.readUID(timeoutSeconds)`

Fast UID-only reading for quick tag identification.

**Signature:**
```typescript
rfid.readUID(timeoutSeconds?: number): string
```

**Parameters:**
- `timeoutSeconds` (optional) — Wait time in seconds. Default: `10`

**Returns:**
- UID string (hex) on success
- Empty string `""` on timeout or error

**Example:**
```javascript
const rfid = require("rfid");

const uid = rfid.readUID(5);

if (uid) {
    console.log("✓ Tag UID:", uid);
} else {
    console.log("✗ No tag detected");
}
```

---

## 🔧 How It Works

### Headless TagOMatic Integration

To avoid the interactive Tag-O-Matic loop, a **headless path** was added to `TagOMatic`:

**New Constructor:**
```cpp
TagOMatic(bool headless_mode);
```
- Initializes the RFID module (PN532/RFID2) without launching the UI loop
- Sets up `_rfid` via `set_rfid_module()` and calls `_rfid->begin()`

**New Methods:**
```cpp
String read_tag_headless(int timeout_seconds);
String read_uid_headless(int timeout_seconds);
RFIDInterface* getRFIDInterface();
```

These methods:
- Execute blocking read loops up to `timeout_seconds`
- Call `_rfid->read()` until success or timeout
- Populate `RFIDInterface` fields on success
- Return data without UI interaction

### RFIDInterface Reuse

The API reads from existing public `RFIDInterface` members:

| Member | Content |
|--------|---------|
| `printableUID.uid` | Tag UID (hex) |
| `printableUID.picc_type` | Tag type name |
| `printableUID.sak` | Select Acknowledge |
| `printableUID.atqa` | Answer To Request |
| `printableUID.bcc` | Block Check Character |
| `strAllPages` | Memory dump |
| `totalPages` | Page count |

All supported RFID modules benefit automatically from the JS API.

---

## 📝 Changes Made

### Modified Files

| File | Changes |
|------|---------|
| `src/modules/rfid/tag_o_matic.h` | • Headless constructor<br>• `read_tag_headless()` method<br>• `read_uid_headless()` method<br>• `getRFIDInterface()` accessor |
| `src/modules/rfid/tag_o_matic.cpp` | • Implemented headless constructor<br>• Implemented read methods |
| `src/modules/bjs_interpreter/interpreter.h` | • Added `#include "rfid_js.h"` |
| `src/modules/bjs_interpreter/interpreter.cpp` | • Added `registerRFID(ctx)` call<br>• Added RFID case in `require()` |

### Added Files

| File | Purpose |
|------|---------|
| `src/modules/bjs_interpreter/rfid_js.h` | JS binding declarations |
| `src/modules/bjs_interpreter/rfid_js.cpp` | JS binding implementation (Duktape bridge) |

---

## 💡 Usage Examples

### Quick UID Scanner

```javascript
const rfid = require("rfid");

console.log("🏷️  UID Scanner");

while (true) {
    console.log("\n📡 Place tag near reader...");
    const uid = rfid.readUID(10);

    if (uid) {
        console.log("✓ Detected:", uid);
    } else {
        console.log("✗ Timeout");
    }
}
```

### UI-Based Reader

```javascript
const display = require("display");
const keyboard = require("keyboard");
const rfid = require("rfid");

display.fill(0);
display.drawText("RFID Reader", 10, 10);
display.drawText("Press SELECT to scan", 10, 30);

var exitApp = false;

while (!exitApp) {
    if (keyboard.getEscPress()) {
        exitApp = true;
        break;
    }

    if (keyboard.getSelPress()) {
        display.fill(0);
        display.drawText("Scanning...", 10, 10);

        const tag = rfid.read(10);

        display.fill(0);
        if (tag) {
            display.drawText("UID: " + tag.uid, 10, 10);
            display.drawText("Type: " + tag.type, 10, 25);
        } else {
            display.drawText("No tag detected", 10, 10);
        }
    }

    delay(50);
}
```

### Multi-Tag Counter

```javascript
const rfid = require("rfid");
var tags = [];

console.log("🔍 Multi-Tag Counter\n");

while (true) {
    const uid = rfid.readUID(3);

    if (uid && tags.indexOf(uid) === -1) {
        tags.push(uid);
        console.log("[" + tags.length + "] New tag:", uid);
    }

    delay(500);
}
```

---

## ✅ Compatibility

### Supported Hardware

| Module | Protocol | Status |
|--------|----------|--------|
| PN532 | I2C/SPI | ✅ Tested |
| RC522/RFID2 | SPI/I2C | ✅ Supported (need test) |
| M5Stack RFID2 | I2C | ✅ Supported (need test) |

### Supported Tag Types

- MIFARE Classic (1K, 4K, Mini)
- MIFARE Ultralight / Ultralight C
- NTAG213/215/216
- FeliCa (PN532 only)

---

## 📌 Notes

- **Blocking API**: Reads block execution during timeout period
- **Backward Compatible**: Existing Tag-O-Matic UI unchanged
- **Architecture**: Reuses existing `RFIDInterface` + new headless path
- **Use Cases**: Access control, inventory, tag identification, cloning automation

---

## 🙏 Credits

**Author:** Senape3000
**Framework:** Bruce Firmware

---

## 🤝 License

Same as Bruce firmware - see main repository LICENSE file.
