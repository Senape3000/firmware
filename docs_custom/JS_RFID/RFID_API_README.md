# 🏷️ RFID JavaScript API for Bruce Firmware

Complete JavaScript bindings for RFID tag operations in Bruce Firmware. Read, write, save, and load RFID tags directly from JavaScript scripts without UI interaction.

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-2.1-green.svg)](CHANGELOG.md)
[![Bruce Firmware](https://img.shields.io/badge/Bruce-Firmware-orange.svg)](https://github.com/pr3y/Bruce)

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Features](#-features)
- [Quick Start](#-quick-start)
- [API Reference](#-api-reference)
  - [rfid.read()](#rfidread)
  - [rfid.readUID()](#rfidreaduid)
  - [rfid.write()](#rfidwrite)
  - [rfid.save()](#rfidsave)
  - [rfid.load()](#rfidload)
  - [rfid.clear()](#rfidclear)
  - [rfid.addMifareKey()](#rfidaddmifarekey)
- [Architecture](#-architecture)
- [File Format](#-file-format)
- [Usage Examples](#-usage-examples)
- [Hardware Compatibility](#-hardware-compatibility)
- [Best Practices](#-best-practices)
- [Troubleshooting](#-troubleshooting)
- [Contributing](#-contributing)

---

## 🎯 Overview

The RFID JavaScript API provides headless access to RFID readers in Bruce Firmware, enabling developers to create custom RFID applications without dealing with the Tag-O-Matic UI. The API supports complete tag lifecycle operations: read, write, save to filesystem, and load from files.

### Key Capabilities

- **Read Operations**: Full tag dumps or fast UID-only reads
- **Write Operations**: Clone tags or write from loaded files
- **File Management**: Save/load tag dumps to/from filesystem
- **Key Management**: Add MIFARE authentication keys to config
- **Headless Mode**: No UI interference, perfect for automation
- **Multi-Module Support**: Works with PN532, RC522, and RFID2 modules
- **Memory Safe**: Optimized for ESP32 constraints

---

## ✨ Features

| Feature | Description |
|---------|-------------|
| **Complete Tag Reading** | UID, type, SAK, ATQA, BCC, full memory dump |
| **Fast UID Reading** | Quick identification without memory dump |
| **Tag Writing** | Clone tags or write data from files |
| **File Operations** | Save/load `.rfid` files to/from filesystem |
| **MIFARE Key Management** | Add custom authentication keys to config |
| **Timeout Control** | Configurable wait times for tag detection |
| **Error Handling** | Detailed success/failure messages |
| **Tag Validation** | Type checking before write operations |
| **Backward Compatible** | Existing Tag-O-Matic UI unchanged |

---

## 🚀 Quick Start

### Basic Read Example

```javascript
const rfid = require("rfid");

console.log("Place RFID tag near reader...");
const tag = rfid.read(10);

if (tag) {
    console.log("✓ Tag detected!");
    console.log("  UID:", tag.uid);
    console.log("  Type:", tag.type);
    console.log("  Pages:", tag.totalPages);
} else {
    console.log("✗ No tag detected");
}
```

### Complete Clone Workflow

```javascript
const rfid = require("rfid");

// Step 1: Read source tag
console.log("Place source tag...");
const source = rfid.read(10);
if (!source) exit();

// Step 2: Save backup
rfid.save("backup_" + source.uid);

// Step 3: Write to new tag
console.log("Place writable tag...");
const result = rfid.write(10);
console.log(result.success ? "✓ Cloned!" : "✗ Failed");
```

---

## 📚 API Reference

### Import Module

```javascript
const rfid = require("rfid");
```

---

### `rfid.read()`

Reads complete tag information including memory dump.

#### Signature

```typescript
rfid.read(timeoutSeconds?: number): TagData | null
```

#### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `timeoutSeconds` | `number` | `10` | Wait time for tag detection |

#### Returns

**On Success:**

```javascript
{
    uid: string,        // Tag UID (hex, space-separated)
    type: string,       // Tag type (e.g., "MIFARE Ultralight")
    sak: string,        // Select Acknowledge (hex)
    atqa: string,       // Answer To Request (hex)
    bcc: string,        // Block Check Character (hex)
    pages: string,      // Full memory dump (multi-line format)
    totalPages: number, // Total memory pages
    dataPages: number   // Writable data pages
}
```

**On Failure:** `null`

#### Example

```javascript
const tag = rfid.read(15);

if (tag) {
    console.log("UID:", tag.uid);
    console.log("Type:", tag.type);
    console.log("Memory:", tag.totalPages, "pages");
} else {
    console.log("Timeout - no tag detected");
}
```

---

### `rfid.readUID()`

Fast UID-only reading without memory dump. Ideal for quick identification.

#### Signature

```typescript
rfid.readUID(timeoutSeconds?: number): string
```

#### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `timeoutSeconds` | `number` | `10` | Wait time for tag detection |

#### Returns

- **String**: UID in hex format (e.g., `"21 3B 9A 30"`)
- **Empty string**: On timeout or error

#### Example

```javascript
const uid = rfid.readUID(5);

if (uid) {
    console.log("Tag UID:", uid);
} else {
    console.log("No tag detected");
}
```

---

### `rfid.write()`

Writes tag data from memory to a physical tag. Data must be loaded first via `read()` or `load()`.

#### Signature

```typescript
rfid.write(timeoutSeconds?: number): WriteResult
```

#### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `timeoutSeconds` | `number` | `10` | Wait time for tag detection |

#### Returns

```javascript
{
    success: boolean, // true if write succeeded
    message: string   // Descriptive result message
}
```

#### Error Codes

| Message | Cause |
|---------|-------|
| `"TAG_NOT_PRESENT"` | No tag detected within timeout |
| `"TAG_NOT_MATCH"` | Tag type incompatible with loaded data |
| `"FAILURE"` | Write operation failed |

#### Workflow

1. Load data: `rfid.read()` or `rfid.load()`
2. Place writable tag near reader
3. Call `rfid.write()`
4. Check `result.success`

#### Example

```javascript
// Read source
const source = rfid.read(10);

// Write to destination
console.log("Place writable tag...");
const result = rfid.write(15);

if (result.success) {
    console.log("✓ Tag written successfully!");
} else {
    console.log("✗ Write failed:", result.message);
}
```

---

### `rfid.save()`

Saves currently loaded tag data to filesystem.

#### Signature

```typescript
rfid.save(filename: string): SaveResult
```

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `filename` | `string` | Filename without `.rfid` extension |

#### Returns

```javascript
{
    success: boolean, // true if save succeeded
    message: string,  // Descriptive result
    filepath: string  // Full path to saved file
}
```

#### Behavior

- **Directory**: Files saved to `/BruceRFID/`
- **Extension**: `.rfid` added automatically
- **Prerequisite**: Tag data must be in memory (call `read()` first)
- **Filesystem**: Tries SD card first, falls back to LittleFS

#### Example

```javascript
const tag = rfid.read(10);

if (tag) {
    const result = rfid.save("my_tag_backup");

    if (result.success) {
        console.log("✓ Saved to:", result.filepath);
    } else {
        console.log("✗", result.message);
    }
}
```

---

### `rfid.load()`

Loads tag data from a saved file into memory.

#### Signature

```typescript
rfid.load(filename: string): TagData | null
```

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `filename` | `string` | Filename with or without `.rfid` extension |

#### Returns

Same as [`rfid.read()`](#rfidread) or `null` on error.

#### Behavior

- Searches `/BruceRFID/` directory
- Tries SD card first, then LittleFS
- Extension `.rfid` optional in filename

#### Example

```javascript
const tag = rfid.load("my_tag_backup");

if (tag) {
    console.log("✓ Loaded:", tag.uid);
    console.log("  Type:", tag.type);
} else {
    console.log("✗ File not found");
}
```

---

### `rfid.clear()`

Clears tag data from memory.

#### Signature

```typescript
rfid.clear(): void
```

#### Example

```javascript
rfid.read(10);
console.log("Tag in memory");

rfid.clear();
console.log("Memory cleared");
```

---

### `rfid.addMifareKey()`

Adds a MIFARE authentication key to the configuration file.

#### Signature

```typescript
rfid.addMifareKey(key: string): KeyResult
```

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | MIFARE key in hex format (12 characters) |

#### Returns

```javascript
{
    success: boolean,  // Always true (validation in BruceConfig)
    message: string,   // "Key processed"
    key: string        // Normalized key (uppercase)
}
```

#### Behavior

- **Validation**: Handled by `BruceConfig` (length, hex format)
- **Normalization**: Converts to uppercase automatically
- **Duplicates**: Silently ignored if key already exists
- **Storage**: Saved to `/BruceRFID/keys.conf`
- **Sync**: Automatically synced to SD card if available

#### Validation Rules

Keys are validated by `BruceConfig`:
- **Length**: Exactly 12 hexadecimal characters
- **Format**: Only 0-9, A-F (case insensitive)
- **Duplicates**: Not added if already in config

#### Error Handling

Invalid keys are logged to Serial but return `success: true` in JavaScript:

```
Serial: "Invalid MIFARE key format: must be 12 hex characters"
JS:     {success: true, message: "Key processed"}
```

Check Serial Monitor for validation errors.

#### Example

```javascript
// Add single key
const result = rfid.addMifareKey("FFFFFFFFFFFF");
console.log("Added:", result.key); // "FFFFFFFFFFFF"

// Batch import
var keys = ["FFFFFFFFFFFF", "A0A1A2A3A4A5", "D3F7D3F7D3F7"];
for (var i = 0; i < keys.length; i++) {
    rfid.addMifareKey(keys[i]);
}

// Keys are now in /BruceRFID/keys.conf
```

#### Use Cases

**1. Add keys from Tag-O-Matic successful authentications:**

```javascript
const tag = rfid.read(10);
if (tag && tag.type.indexOf("MIFARE Classic") >= 0) {
    // Extract authenticated keys (implementation specific)
    var extractedKeys = ["A0A1A2A3A4A5", "B0B1B2B3B4B5"];

    for (var i = 0; i < extractedKeys.length; i++) {
        rfid.addMifareKey(extractedKeys[i]);
        console.log("✓ Key saved:", extractedKeys[i]);
    }
}
```

**2. Batch import common dictionary:**

```javascript
var commonKeys = [
    "FFFFFFFFFFFF", // Default
    "A0A1A2A3A4A5", // MAD key
    "D3F7D3F7D3F7", // NDEF key
    "000000000000", // Null key
    "B0B1B2B3B4B5", // Custom
    "123456789ABC"  // Custom
];

console.log("Importing " + commonKeys.length + " keys...");
for (var i = 0; i < commonKeys.length; i++) {
    rfid.addMifareKey(commonKeys[i]);
}
console.log("✓ Import complete");
```

#### File Format

Keys are stored in `/BruceRFID/keys.conf`:

```
# BRUCE MIFARE KEYS FILE
# ADD YOUR KEYS ONE PER LINE

# STANDARD MIFARE KEYS
FFFFFFFFFFFF
A0A1A2A3A4A5
D3F7D3F7D3F7

# CUSTOM KEYS
123456789ABC
```

#### Notes

- Keys persist across firmware restarts
- Invalid keys are rejected silently (check Serial)
- Duplicate keys are not added again
- Maximum key count: Limited by filesystem space (~1000+ keys)

---

## 🏗️ Architecture

### System Overview

```
┌─────────────────┐
│  JavaScript     │
│  (Duktape VM)   │
└────────┬────────┘
         │ require("rfid")
┌────────▼────────┐
│  rfid_js.cpp    │ ← JavaScript bindings
└────────┬────────┘
         │
┌────────▼────────┐
│ tag_o_matic.cpp │ ← Headless operations
└────────┬────────┘
         │
┌────────▼────────┐
│ RFIDInterface   │ ← Abstract hardware layer
└────────┬────────┘
         │
    ┌────┴────┐
    ▼         ▼
┌───────┐  ┌───────┐
│ PN532 │  │ RFID2 │ ← Hardware drivers
└───────┘  └───────┘
```

### Headless Mode Implementation

The API adds a headless path to TagOMatic, bypassing the interactive UI loop:

**Key Components:**

1. **Headless Constructor**

```cpp
TagOMatic(bool headless_mode);
```

- Initializes RFID module without UI
- Sets up hardware via `set_rfid_module()`

2. **Blocking Read Methods**

```cpp
String read_tag_headless(int timeout_seconds);
String read_uid_headless(int timeout_seconds);
int write_tag_headless(int timeout_seconds);
```

- Loop until success or timeout
- No display updates
- Return data directly

3. **Data Access**

```cpp
RFIDInterface* getRFIDInterface();
```

- Exposes underlying hardware interface
- JavaScript reads data from `RFIDInterface` members

### Modified Files

| File | Changes |
|------|---------|
| `tag_o_matic.h` | Added headless constructor, read/write methods |
| `tag_o_matic.cpp` | Implemented headless operations |
| `rfid_js.h` | JavaScript binding declarations |
| `rfid_js.cpp` | Duktape bridge implementation |
| `interpreter.cpp` | Added `registerRFID()` call |
| `PN532.cpp` | Fixed `write_data_blocks()` loop termination |
| `RFID2.cpp` | Fixed `write_data_blocks()` loop termination |
| `config.h` | Added `addMifareKey()` method declaration |
| `config.cpp` | Implemented MIFARE key management with file I/O |

---

## 📄 File Format

### .rfid File Structure

```
# RFID Tag Dump
UID: 21 3B 9A 30
SAK: 00
ATQA: 00 44
BCC: B0
Type: MIFARE Ultralight
Total Pages: 64
Data Pages: 42
Page 0: 21 3B 9A 30 B0 08 04 00 62 63 64 65 66 67 68 69
Page 1: 00 00 00 00 00 00 FF 07 80 69 FF FF FF FF FF FF
...
Page 63: 00 00 00 00 FF 07 80 69 FF FF FF FF FF FF FF FF
```

### Format Details

| Field | Format | Description |
|-------|--------|-------------|
| `UID` | Hex bytes (space-separated) | Tag unique identifier |
| `SAK` | Hex byte | Select Acknowledge |
| `ATQA` | Hex bytes | Answer To Request |
| `BCC` | Hex byte | Block Check Character |
| `Type` | String | Tag type name |
| `Total Pages` | Integer | Memory page count |
| `Data Pages` | Integer | Writable pages |
| `Page N` | Hex bytes | 16 bytes per page |

---

## 💡 Usage Examples

### Example 1: Tag Inventory System

```javascript
const rfid = require("rfid");

var inventory = [];

console.log("📦 RFID Inventory System\n");

while (true) {
    console.log("Scan tag (ESC to exit)...");

    const uid = rfid.readUID(5);

    if (uid && inventory.indexOf(uid) === -1) {
        inventory.push(uid);

        const tag = rfid.read(2);
        const filename = "inventory_" + inventory.length;
        rfid.save(filename);

        console.log("[" + inventory.length + "] Added:", uid);
    }

    delay(500);
}
```

### Example 2: Batch Tag Writer

```javascript
const rfid = require("rfid");

var templates = ["employee_1", "employee_2", "employee_3"];

console.log("📝 Batch Writer: " + templates.length + " tags\n");

for (var i = 0; i < templates.length; i++) {
    console.log("\n[" + (i + 1) + "/" + templates.length + "] Loading:", templates[i]);

    const data = rfid.load(templates[i]);
    if (!data) {
        console.log("✗ File not found, skipping");
        continue;
    }

    console.log("✓ Loaded UID:", data.uid);
    console.log("Place writable tag (15s)...");

    const result = rfid.write(15);

    if (result.success) {
        console.log("✓ Written successfully!");
    } else {
        console.log("✗ Failed:", result.message);
        i--; // Retry
        delay(2000);
    }
}

console.log("\n✓ Batch complete!");
```

### Example 3: Access Control Logger

```javascript
const rfid = require("rfid");
const storage = require("storage");

var logFile = "/BruceRFID/access_log.txt";
var authorizedUIDs = ["21 3B 9A 30", "F4 E2 C1 B0"];

console.log("🔐 Access Control System\n");

while (true) {
    const uid = rfid.readUID(3);

    if (uid) {
        var timestamp = Date.now();
        var isAuthorized = authorizedUIDs.indexOf(uid) >= 0;

        var logEntry = timestamp + "," + uid + "," + (isAuthorized ? "GRANTED" : "DENIED") + "\n";
        storage.append(logFile, logEntry);

        console.log(isAuthorized ? "✓ Access granted" : "✗ Access denied");
        console.log("  UID:", uid);

        delay(2000);
    }

    delay(100);
}
```

### Example 4: Tag Comparison Tool

```javascript
const rfid = require("rfid");

console.log("🔍 RFID Tag Comparison Tool\n");

console.log("Step 1: Scan first tag...");
const tag1 = rfid.read(10);
if (!tag1) exit();

console.log("✓ Tag 1:", tag1.uid);

console.log("\nStep 2: Scan second tag...");
const tag2 = rfid.read(10);
if (!tag2) exit();

console.log("✓ Tag 2:", tag2.uid);

console.log("\n--- Comparison ---");
console.log("UID Match:", tag1.uid === tag2.uid ? "✓" : "✗");
console.log("Type Match:", tag1.type === tag2.type ? "✓" : "✗");
console.log("Data Match:", tag1.pages === tag2.pages ? "✓" : "✗");

if (tag1.pages !== tag2.pages) {
    console.log("\n⚠️ Memory contents differ!");
}
```

### Example 5: MIFARE Key Manager

```javascript
const rfid = require("rfid");
const keyboard = require("keyboard");

console.log("🔑 MIFARE Key Manager\n");

// Predefined dictionary
var keyDictionary = [
    "FFFFFFFFFFFF", // Factory default
    "A0A1A2A3A4A5", // MAD key
    "B0B1B2B3B4B5", // Custom
    "D3F7D3F7D3F7", // NDEF key
    "000000000000"  // Null key
];

console.log("1. Import dictionary keys");
console.log("2. Add custom key");
console.log("3. Extract from tag\n");

var choice = keyboard.getString("Select option:", 1);

if (choice === "1") {
    // Import dictionary
    console.log("\nImporting " + keyDictionary.length + " keys...");
    for (var i = 0; i < keyDictionary.length; i++) {
        rfid.addMifareKey(keyDictionary[i]);
        console.log("[" + (i + 1) + "] ✓ " + keyDictionary[i]);
    }
    console.log("\n✓ Dictionary imported!");

} else if (choice === "2") {
    // Add custom key
    var customKey = keyboard.getString("Enter 12 hex chars:", 12);
    if (customKey && customKey.length === 12) {
        var result = rfid.addMifareKey(customKey);
        console.log("✓ Added:", result.key);
    } else {
        console.log("✗ Invalid length");
    }

} else if (choice === "3") {
    // Extract from tag
    console.log("\nPlace MIFARE Classic tag...");
    const tag = rfid.read(10);

    if (tag && tag.type.indexOf("MIFARE Classic") >= 0) {
        console.log("✓ Tag detected:", tag.uid);
        console.log("\nSimulating key extraction...");

        // In real implementation, extract from successful auth
        var extractedKeys = ["A0A1A2A3A4A5"];

        for (var i = 0; i < extractedKeys.length; i++) {
            rfid.addMifareKey(extractedKeys[i]);
            console.log("✓ Saved:", extractedKeys[i]);
        }
    } else {
        console.log("✗ No MIFARE Classic tag");
    }
}

console.log("\nKeys saved to: /BruceRFID/keys.conf");
```

---

## 🔌 Hardware Compatibility

### Supported RFID Modules

| Module | Connection | Protocol | Status | Notes |
|--------|------------|----------|--------|-------|
| **PN532** | I2C | ISO14443A | ✅ Tested | Best compatibility |
| **PN532** | SPI | ISO14443A | ✅ Tested | High speed |
| **RC522** | SPI | ISO14443A | ✅ Supported | Common module |
| **RFID2 (M5Stack)** | I2C | ISO14443A | ✅ Supported | Integrated solution |

### Supported Tag Types

| Tag Family | Read | Write | UID Clone | Notes |
|------------|------|-------|-----------|-------|
| **MIFARE Classic 1K** | ✅ | ✅ | ⚠️ | UID clone requires Magic tags |
| **MIFARE Classic 4K** | ✅ | ✅ | ⚠️ | UID clone requires Magic tags |
| **MIFARE Ultralight** | ✅ | ✅ | ❌ | UID factory-locked |
| **MIFARE Ultralight C** | ✅ | ✅ | ❌ | UID factory-locked |
| **NTAG213/215/216** | ✅ | ✅ | ❌ | UID factory-locked |
| **FeliCa** | ✅ | ⚠️ | ❌ | PN532 only, limited support |

**Legend:**
- ✅ Fully supported
- ⚠️ Partial support / special requirements
- ❌ Not supported

---

## 🎯 Best Practices

### 1. Always Check Return Values

```javascript
const tag = rfid.read(10);
if (!tag) {
    console.log("Read failed - check tag placement");
    return;
}

const result = rfid.write(10);
if (!result.success) {
    console.log("Write failed:", result.message);
    return;
}
```

### 2. Use Appropriate Timeouts

```javascript
// Quick scans
const uid = rfid.readUID(3); // 3 seconds for UID only

// Full reads
const tag = rfid.read(10); // 10 seconds for complete dump

// Write operations
const result = rfid.write(15); // 15 seconds for writing
```

### 3. Save Backups Before Writing

```javascript
const source = rfid.read(10);
rfid.save("backup_" + Date.now()); // Timestamp backup

const result = rfid.write(10);
```

### 4. Clear Memory Between Operations

```javascript
rfid.read(10);
rfid.save("tag1");

rfid.clear(); // Clear before next operation

rfid.read(10);
rfid.save("tag2");
```

### 5. Handle Memory Constraints

```javascript
// ❌ BAD: Memory leak
while (true) {
    var tag = rfid.read(5);
    // tag never cleared
}

// ✅ GOOD: Clear when done
while (true) {
    var tag = rfid.read(5);
    if (tag) {
        // Process tag
    }
    rfid.clear(); // Free memory
    delay(1000);
}
```

### 6. Validate Tag Compatibility

```javascript
const source = rfid.read(10);
console.log("Source type:", source.type);

console.log("Place destination tag...");
const result = rfid.write(10);

if (result.message === "TAG_NOT_MATCH") {
    console.log("✗ Tag types incompatible!");
}
```

### 7. Manage MIFARE Keys Efficiently

```javascript
// ❌ BAD: Add same key repeatedly
for (var i = 0; i < 10; i++) {
    rfid.addMifareKey("FFFFFFFFFFFF"); // Duplicate ignored
}

// ✅ GOOD: Check before adding
var newKeys = ["FFFFFFFFFFFF", "A0A1A2A3A4A5"];
for (var i = 0; i < newKeys.length; i++) {
    rfid.addMifareKey(newKeys[i]);
}
console.log("Added " + newKeys.length + " unique keys");
```

```javascript
// ✅ GOOD: Validate format before adding
function addKeySafe(key) {
    if (key.length !== 12) {
        console.log("✗ Invalid length:", key);
        return false;
    }

    var hexRegex = /^[0-9A-Fa-f]{12}$/;
    if (!hexRegex.test(key)) {
        console.log("✗ Invalid hex:", key);
        return false;
    }

    rfid.addMifareKey(key);
    console.log("✓ Added:", key);
    return true;
}
```

---

## 🔧 Troubleshooting

### Common Issues

#### Issue: "TAG_NOT_PRESENT" on write()

**Causes:**
- No tag physically present
- Tag moved during write operation
- RFID reader malfunction

**Solutions:**
- Increase timeout: `rfid.write(20)`
- Hold tag steady during operation
- Check reader power/connections

---

#### Issue: "TAG_NOT_MATCH" error

**Cause:** Source and destination tags are different types.

**Solution:** Use same tag family for cloning:

```javascript
console.log("Source:", source.type);
console.log("Ensure destination is same type");
```

---

#### Issue: UID not cloned on write

**Cause:** Most tags have factory-locked UIDs.

**Solution:**
- Use "Magic" MIFARE Classic tags for UID cloning
- Or accept that only data pages will be cloned

---

#### Issue: Out of memory errors

**Causes:**
- Multiple large tag dumps in memory
- Memory not cleared between operations

**Solutions:**

```javascript
// Clear memory regularly
rfid.clear();

// Don't store multiple dumps
var tag1 = rfid.read(10);
rfid.save("tag1");
rfid.clear(); // Clear before next read

var tag2 = rfid.read(10);
```

---

#### Issue: File not found when loading

**Check:**

```javascript
// Try with .rfid extension
var tag = rfid.load("my_file.rfid");

// Try without extension
if (!tag) tag = rfid.load("my_file");

// Check file location
const result = rfid.save("test");
console.log("Files saved to:", result.filepath);
```

---

#### Issue: MIFARE key not working after adding

**Cause:** Keys are stored but Tag-O-Matic may need restart to reload them.

**Solution:**

```javascript
// Add keys
rfid.addMifareKey("FFFFFFFFFFFF");
rfid.addMifareKey("A0A1A2A3A4A5");

// Keys are now in /BruceRFID/keys.conf
console.log("✓ Keys saved. Restart to use in Tag-O-Matic.");
```

**Check file contents:**

```javascript
const storage = require("storage");
const keys = storage.read("/BruceRFID/keys.conf");
console.log(keys);
```

---

#### Issue: Invalid key not rejected in JavaScript

**Cause:** Validation happens in `BruceConfig`, not JavaScript layer.

**Solution:** Check Serial Monitor for validation errors:

```
Serial Output:
Invalid MIFARE key format: must be 12 hex characters
MIFARE key already exists: FFFFFFFFFFFF
```

**JavaScript validation (optional):**

```javascript
function validateKey(key) {
    if (key.length !== 12) return false;
    return /^[0-9A-Fa-f]{12}$/.test(key);
}

var key = "GGGGGGGGGGGG"; // Invalid
if (!validateKey(key)) {
    console.log("✗ Invalid format");
} else {
    rfid.addMifareKey(key);
}
```

---

### Debug Checklist

- [ ] RFID module properly connected and powered
- [ ] Tag within 5cm of reader antenna
- [ ] Correct tag type for operation
- [ ] Sufficient timeout for operation
- [ ] Memory cleared between operations
- [ ] File exists in `/BruceRFID/` directory
- [ ] SD card mounted (if using SD storage)

---

## 🤝 Contributing

Contributions are welcome! Please follow these guidelines:

### Reporting Issues

Include:
- Bruce firmware version
- RFID module type
- Tag type being used
- Complete error messages
- Minimal code to reproduce

### Pull Requests

- Follow existing code style
- Add tests for new features
- Update documentation
- Keep commits focused and atomic

---

## 📜 License

This code is part of Bruce Firmware and follows its license terms.

**Author:** Senape3000
**Version:** 2.1
**Last Updated:** January 2026

---

## 🙏 Acknowledgments

- **Bruce Firmware Team** for the excellent platform
- **PN532/RFID2 Library Authors** for hardware drivers
- **Community Contributors** for testing and feedback

---

## 📚 Additional Resources

- [Bruce Firmware GitHub](https://github.com/pr3y/Bruce)
- [PN532 Datasheet](https://www.nxp.com/docs/en/user-guide/141520.pdf)
- [MIFARE Classic Documentation](https://www.nxp.com/docs/en/data-sheet/MF1S50YYX_V1.pdf)
- [NTAG21x Documentation](https://www.nxp.com/docs/en/data-sheet/NTAG213_215_216.pdf)

---

**For questions or support, please open an issue on GitHub.**
