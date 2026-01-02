// RFID UID Scanner - Bruce Firmware
// Uses readUID() function (faster, UID only)

const display = require("display");
const keyboard = require("keyboard");
const rfid = require("rfid");

const colours = [
    display.color(0, 0, 0),       // black
    display.color(127, 127, 127), // grey
    display.color(255, 255, 255), // white
    display.color(0, 255, 0),     // green
    display.color(255, 0, 0),     // red
    display.color(255, 255, 0),   // yellow
];

var exitApp = false;
var scannedUIDs = [];
var scanCount = 0;

const displayWidth = display.width();
const displayHeight = display.height();
const fontScale = (displayWidth > 300 ? 1 : 0);

function showScanScreen() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    
    display.setTextSize(2 + fontScale);
    display.setTextColor(BRUCE_PRICOLOR);
    display.drawText("UID Scanner", displayWidth / 2, 15);
    
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[5]); // yellow
    display.drawText("Fast Mode (UID only)", displayWidth / 2, 35);
    
    display.setTextColor(colours[2]);
    display.setTextAlign('left', 'middle');
    display.drawText("Scanned: " + scanCount, 10, 55);
    
    // Show last 3 UIDs
    var y = 75;
    if (scannedUIDs.length > 0) {
        display.setTextColor(colours[1]);
        display.drawText("Recent UIDs:", 10, y);
        y += 15;
        
        for (var i = Math.max(0, scannedUIDs.length - 3); i < scannedUIDs.length; i++) {
            display.setTextColor(colours[2]);
            display.drawText((i + 1) + ": " + scannedUIDs[i], 10, y);
            y += 12;
        }
    }
    
    // Instructions
    display.setTextAlign('center', 'middle');
    display.setTextColor(colours[2]);
    display.drawText("Press SELECT to scan", displayWidth / 2, displayHeight / 2 + 20);
    
    display.setTextAlign('left', 'middle');
    display.setTextColor(colours[1]);
    display.drawText("Select: Scan | Exit: Close", 10, displayHeight - 15);
}

function showScanning() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    
    display.setTextSize(2 + fontScale);
    display.setTextColor(colours[5]); // yellow
    display.drawText("Scanning...", displayWidth / 2, displayHeight / 2 - 20);
    
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);
    display.drawText("Place tag near reader", displayWidth / 2, displayHeight / 2 + 10);
    display.drawText("Timeout: 10s", displayWidth / 2, displayHeight / 2 + 30);
}

function showUIDFound(uid) {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    
    display.setTextSize(2 + fontScale);
    display.setTextColor(colours[3]); // green
    display.drawText("UID Found!", displayWidth / 2, 20);
    
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[1]);
    display.drawText("UID:", displayWidth / 2, displayHeight / 2 - 20);
    
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);
    display.drawText(uid, displayWidth / 2, displayHeight / 2);
    
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[1]);
    display.drawText("Scan #" + scanCount, displayWidth / 2, displayHeight / 2 + 25);
    
    delay(2000);
}

function showError() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    
    display.setTextSize(2 + fontScale);
    display.setTextColor(colours[4]); // red
    display.drawText("Timeout", displayWidth / 2, displayHeight / 2 - 20);
    
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);
    display.drawText("No tag detected", displayWidth / 2, displayHeight / 2 + 10);
    
    delay(1500);
}

// Main program
showScanScreen();

while (!exitApp) {
    if (keyboard.getEscPress()) {
        exitApp = true;
        break;
    }
    
    if (keyboard.getSelPress()) {
        showScanning();
        
        // Use readUID() - faster, returns only UID string
        const uid = rfid.readUID(10);
        
        if (uid && uid.length > 0) {
            scanCount++;
            scannedUIDs.push(uid);
            
            // Keep only last 10 UIDs to save memory
            if (scannedUIDs.length > 10) {
                scannedUIDs.shift();
            }
            
            showUIDFound(uid);
            showScanScreen();
        } else {
            showError();
            showScanScreen();
        }
    }
    
    delay(50);
}

// Exit message
display.fill(colours[0]);
display.setTextAlign('center', 'middle');
display.setTextSize(1 + fontScale);
display.setTextColor(colours[2]);
display.drawText("Total scanned: " + scanCount, displayWidth / 2, displayHeight / 2);
delay(1500);
display.fill(colours[0]);
