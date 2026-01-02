// RFID Reader - Bruce Firmware
// Script working - TESTED OK!
const display = require("display");
const keyboard = require("keyboard");
const rfid = require("rfid");

const colours = [
    display.color(0, 0, 0),       // black
    display.color(127, 127, 127), // grey
    display.color(255, 255, 255), // white
    display.color(0, 255, 0),     // green
    display.color(255, 0, 0),     // red
];

var exitApp = false;
var lastTagData = null;

const displayWidth = display.width();
const displayHeight = display.height();
const fontScale = (displayWidth > 300 ? 1 : 0);

function showScanScreen() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);
    display.setTextColor(BRUCE_PRICOLOR);
    display.drawText("RFID Reader", displayWidth / 2, 20);
    
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);
    display.drawText("Press SELECT to scan", displayWidth / 2, displayHeight / 2);
    
    display.setTextAlign('left', 'middle');
    display.setTextColor(colours[1]);
    display.drawText("Select: Scan | Exit: Close", 10, displayHeight - 15);
}

function showTagInfo(tagData) {
    display.fill(colours[0]);
    display.setTextAlign('left', 'middle');
    
    display.setTextSize(2 + fontScale);
    display.setTextColor(colours[3]);
    display.drawText("Tag Found!", 10, 12);
    
    display.setTextSize(1 + fontScale);
    var y = 35;
    
    display.setTextColor(colours[1]);
    display.drawText("UID:", 10, y);
    display.setTextColor(colours[2]);
    display.drawText(tagData.uid, 10, y + 12);
    
    y += 30;
    display.setTextColor(colours[1]);
    display.drawText("Type: ", 10, y);
    display.setTextColor(colours[2]);
    display.drawText(tagData.type, 60, y);
    
    y += 15;
    display.setTextColor(colours[1]);
    display.drawText("SAK: " + tagData.sak, 10, y);
    display.drawText("ATQA: " + tagData.atqa, displayWidth / 2, y);
    
    y += 15;
    display.setTextColor(colours[1]);
    display.drawText("Pages: " + tagData.totalPages, 10, y);
    
    display.setTextAlign('left', 'middle');
    display.setTextColor(colours[1]);
    display.drawText("Select: Scan Again | Exit: Close", 10, displayHeight - 15);
}

function showError() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    
    display.setTextSize(2 + fontScale);
    display.setTextColor(colours[4]);
    display.drawText("Timeout", displayWidth / 2, displayHeight / 2 - 20);
    
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);
    display.drawText("No tag detected", displayWidth / 2, displayHeight / 2 + 10);
    
    delay(1500);
    showScanScreen();
}

showScanScreen();

while (!exitApp) {
    if (keyboard.getEscPress()) {
        exitApp = true;
        break;
    }
    
    if (keyboard.getSelPress()) {
        display.fill(colours[0]);
        display.setTextAlign('center', 'middle');
        display.setTextSize(1 + fontScale);
        display.setTextColor(colours[2]);
        display.drawText("Scanning...", displayWidth / 2, displayHeight / 2);
        
        const tagData = rfid.read(10);
        
        if (tagData) {
            lastTagData = tagData;
            showTagInfo(tagData);
        } else {
            showError();
        }
    }
    
    delay(50);
}

display.fill(colours[0]);
