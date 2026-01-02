text
# 🏷️ RFID JavaScript API for Bruce Firmware

> **New Feature**: JavaScript bindings for RFID tag reading operations in Bruce firmware

This document describes the new RFID JavaScript API integrated into the Bruce firmware JS interpreter, enabling scripts to interact with RFID readers directly.

---

## 📋 Table of Contents

- [Overview](#-overview)
- [JavaScript API](#-javascript-api)
  - [Importing the Module](#importing-the-module)
  - [rfid.read()](#rfidreadtimeoutseconds)
  - [rfid.readUID()](#rfidreaduidtimeoutseconds)
- [Implementation Details](#-implementation-details)
- [Code Changes](#-code-changes)
- [Usage Examples](#-usage-examples)
- [Compatibility](#-compatibility)

---

## 🎯 Overview

The new **RFID module** exposes JavaScript bindings for RFID operations, allowing scripts to:

- ✅ Read **complete tag information** (UID, type, SAK, ATQA, memory dump)
- ✅ Read **UID only** for fast identification
- ✅ Work in **headless mode** without launching Tag-O-Matic UI
- ✅ Support **PN532** and **RC522/RFID2** modules
- ✅ Use **configurable timeouts** for tag detection

The implementation reuses the existing RFID stack via `TagOMatic` and `RFIDInterface`, with a new headless execution path optimized for scripting.

---

## 🔌 JavaScript API

### Importing the Module

```javascript
const rfid = require("rfid");
This returns an object exposing RFID reading functions.

rfid.read(timeoutSeconds)
Reads complete tag information including UID, type, and memory contents.

Signature
typescript
rfid.read(timeoutSeconds?: number): object | null
Parameters
timeoutSeconds (optional): Maximum wait time in seconds. Default: 10

Returns
On Success - Object containing:

javascript
{
  uid: string,         // Tag UID (hex format)
  type: string,        // Tag type (e.g., "MIFARE Ultralight")
  sak: string,         // Select Acknowledge
  atqa: string,        // Answer To Request Type A
  bcc: string,         // Block Check Character
  pages: string,       // Raw memory dump
  totalPages: number   // Total memory pages
}
On Failure - null (timeout or read error)

Example
javascript
const rfid = require("rfid");

const tagData = rfid.read(10);

if (tagData) {
    console.log("✓ Tag detected!");
    console.log("UID:", tagData.uid);
    console.log("Type:", tagData.type);
    console.log("SAK:", tagData.sak);
    console.log("ATQA:", tagData.atqa);
    console.log("Pages:", tagData.totalPages);
} else {
    console.log("✗ No tag detected");
}
rfid.readUID(timeoutSeconds)
Fast UID-only reading for quick tag identification.

Signature
typescript
rfid.readUID(timeoutSeconds?: number): string
Parameters
timeoutSeconds (optional): Maximum wait time in seconds. Default: 10

Returns
On Success: UID string (hex format)

On Failure: Empty string ""

Example
javascript
const rfid = require("rfid");

const uid = rfid.readUID(5);

if (uid && uid.length > 0) {
    console.log("✓ Tag UID:", uid);
} else {
    console.log("✗ No tag detected");
}
🔧 Implementation Details
Headless TagOMatic Integration
To avoid launching the interactive Tag-O-Matic UI, new headless components were added:

New Constructor
cpp
TagOMatic(bool headless_mode)
Initializes RFID module (PN532/RFID2) without UI

Skips setup()/loop() interactive flow

Enables non-blocking script execution

New Methods
cpp
String read_tag_headless(int timeout_seconds);
String read_uid_headless(int timeout_seconds);
RFIDInterface* getRFIDInterface();
These methods:

Run blocking read loops with configurable timeout

Call _rfid->read() internally

Populate RFIDInterface fields on success

Return data without UI interaction

RFIDInterface Reuse
The API reads from existing RFIDInterface fields:

Field	Description
printableUID.uid	Tag UID (hex)
printableUID.picc_type	Tag type name
printableUID.sak	Select Acknowledge
printableUID.atqa	Answer To Request
printableUID.bcc	Block Check Character
strAllPages	Memory dump
totalPages	Page count
All supported RFID modules automatically benefit from the JS API.

📝 Code Changes
Modified Files
<table> <tr> <th>File</th> <th>Changes</th> </tr> <tr> <td><code>src/modules/rfid/tag_o_matic.h</code></td> <td> <ul> <li>➕ <code>TagOMatic(bool headless_mode)</code> constructor</li> <li>➕ <code>read_tag_headless()</code> method</li> <li>➕ <code>read_uid_headless()</code> method</li> <li>➕ <code>getRFIDInterface()</code> accessor</li> </ul> </td> </tr> <tr> <td><code>src/modules/rfid/tag_o_matic.cpp</code></td> <td> <ul> <li>✏️ Implemented headless constructor</li> <li>✏️ Implemented <code>read_tag_headless()</code></li> <li>✏️ Implemented <code>read_uid_headless()</code></li> </ul> </td> </tr> <tr> <td><code>src/modules/bjs_interpreter/interpreter.h</code></td> <td> <ul> <li>➕ <code>#include "rfid_js.h"</code></li> </ul> </td> </tr> <tr> <td><code>src/modules/bjs_interpreter/interpreter.cpp</code></td> <td> <ul> <li>➕ <code>registerRFID(ctx)</code> in initialization</li> <li>➕ RFID case in <code>require()</code> implementation</li> </ul> </td> </tr> </table>
Added Files
<table> <tr> <th>File</th> <th>Description</th> </tr> <tr> <td><code>src/modules/bjs_interpreter/rfid_js.h</code></td> <td> Declares RFID JS API functions: <ul> <li><code>putPropRFIDFunctions()</code></li> <li><code>registerRFID()</code></li> <li><code>native_rfidRead()</code></li> <li><code>native_rfidReadUID()</code></li> </ul> </td> </tr> <tr> <td><code>src/modules/bjs_interpreter/rfid_js.cpp</code></td> <td> Implements JS bindings: <ul> <li>Duktape C++ ↔ JS bridge</li> <li>Headless TagOMatic instantiation</li> <li>Object creation from RFIDInterface data</li> <li>Timeout handling</li> </ul> </td> </tr> </table>
💡 Usage Examples
🚀 Quick UID Scanner
javascript
const rfid = require("rfid");

console.log("🏷️  UID Scanner Ready");

while (true) {
    console.log("\n📡 Place tag near reader...");
    const uid = rfid.readUID(10);

    if (uid) {
        console.log("✓ Detected:", uid);
    } else {
        console.log("✗ Timeout");
    }
}
🖥️ UI-Based Tag Reader
javascript
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

        const tagData = rfid.read(10);

        display.fill(0);
        if (tagData) {
            display.drawText("UID: " + tagData.uid, 10, 10);
            display.drawText("Type: " + tagData.type, 10, 25);
            display.drawText("SAK: " + tagData.sak, 10, 40);
            display.drawText("ATQA: " + tagData.atqa, 10, 55);
        } else {
            display.drawText("No tag detected", 10, 10);
        }
    }

    delay(50);
}
📊 Multi-Tag Logger
javascript
const rfid = require("rfid");
var scannedTags = [];

console.log("🔍 Multi-Tag Scanner");
console.log("Press ESC to stop\n");

while (true) {
    const uid = rfid.readUID(5);

    if (uid && scannedTags.indexOf(uid) === -1) {
        scannedTags.push(uid);
        console.log("[" + scannedTags.length + "] New tag:", uid);
    }

    delay(500);
}
✅ Compatibility
Supported RFID Modules
Module	Protocol	Status
PN532	I2C/SPI	✅ Tested
RC522/RFID2	SPI/I2C	✅ Tested
M5Stack RFID2	I2C	✅ Supported
Tag Types
Supports all tag types handled by the underlying drivers:

MIFARE Classic (1K, 4K, Mini)

MIFARE Ultralight / Ultralight C

NTAG213/215/216

FeliCa (PN532 only)

📌 Notes
⏱️ Blocking API: Reads block script execution during timeout period

🔄 Backward Compatible: Existing Tag-O-Matic UI unchanged

🏗️ Architecture: Uses existing RFIDInterface + new headless path

🎯 Use Cases: Tag scanning, access control, inventory, cloning automation

🤝 Contributing
Found a bug or want to add features? Contributions are welcome!

Test your changes with both read() and readUID()

Verify on both PN532 and RC522 if possible

Update documentation for new features

📄 License
Same as Bruce firmware - check main repository LICENSE file.

Made with ❤️ for the Bruce community
