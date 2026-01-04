// RFID Save to File - Bruce Firmware
// Reads a tag and saves it to rfid_js_save_test.rfid

const display = require("display");
const keyboard = require("keyboard");
const rfid = require("rfid");
const dialog = require("dialog");

const colours = [
    display.color(0, 0, 0),       // black
    display.color(127, 127, 127), // grey
    display.color(255, 255, 255), // white
    display.color(0, 255, 0),     // green
    display.color(255, 0, 0),     // red
    display.color(255, 255, 0),   // yellow
];

var exitApp = false;
var readTag = null;
var savedFile = null;

const displayWidth = display.width();
const displayHeight = display.height();
const fontScale = (displayWidth > 300 ? 1 : 0);

function showMainScreen() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);
    display.setTextColor(BRUCE_PRICOLOR);
    display.drawText("RFID Save Test", displayWidth / 2, 20);

    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[5]);
    display.drawText("Save to: rfid_js_save_test.rfid", displayWidth / 2, 45);

    display.setTextColor(colours[2]);
    if (readTag === null) {
        display.drawText("Press SELECT to scan", displayWidth / 2, displayHeight / 2);
    } else if (savedFile === null) {
        display.drawText("Tag read!", displayWidth / 2, displayHeight / 2 - 10);
        display.drawText("Press SELECT to save", displayWidth / 2, displayHeight / 2 + 10);
    } else {
        display.setTextColor(colours[3]);
        display.drawText("Saved!", displayWidth / 2, displayHeight / 2 - 10);
        display.setTextColor(colours[2]);
        display.drawText("Select: View | Next: New scan", displayWidth / 2, displayHeight / 2 + 10);
    }

    display.setTextAlign('left', 'middle');
    display.setTextColor(colours[1]);
    display.drawText("Select: Scan/Save | Exit: Close", 10, displayHeight - 15);
}

function showScanning() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);
    display.setTextColor(colours[5]);
    display.drawText("Scanning...", displayWidth / 2, displayHeight / 2 - 20);
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);
    display.drawText("Place tag near reader", displayWidth / 2, displayHeight / 2 + 10);
    display.drawText("Timeout: 10s", displayWidth / 2, displayHeight / 2 + 30);
}

function formatTagInfo(tagData) {
    var text = "";
    text += "=== TAG READ ===\n\n";
    text += "UID:\n" + tagData.uid + "\n\n";
    text += "Type:\n" + tagData.type + "\n\n";
    text += "SAK: " + tagData.sak + "\n";
    text += "ATQA: " + tagData.atqa + "\n";
    text += "BCC: " + tagData.bcc + "\n\n";
    text += "Total Pages: " + tagData.totalPages + "\n";
    text += "Data Pages: " + tagData.dataPages;

    if (savedFile) {
        text += "\n\n=== SAVED TO ===\n";
        text += savedFile;
    }

    return text;
}

function showSaving() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);
    display.setTextColor(colours[5]);
    display.drawText("Saving...", displayWidth / 2, displayHeight / 2 - 10);
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);
    display.drawText("rfid_js_save_test.rfid", displayWidth / 2, displayHeight / 2 + 15);
}

// Main program
showMainScreen();

while (!exitApp) {
    if (keyboard.getEscPress()) {
        exitApp = true;
        break;
    }

    if (keyboard.getNextPress()) {
        // Reset for new scan
        if (savedFile !== null) {
            rfid.clear();
            readTag = null;
            savedFile = null;
            showMainScreen();
        }
    }

    if (keyboard.getSelPress()) {
        if (readTag === null) {
            // Read tag
            showScanning();
            const tagData = rfid.read(10);

            if (tagData) {
                readTag = tagData;
                dialog.success("Tag detected!", true);
                showMainScreen();
            } else {
                dialog.error("No tag detected!", true);
                showMainScreen();
            }
        } else if (savedFile === null) {
            // Save tag
            showSaving();
            const result = rfid.save("rfid_js_save_test");

            if (result && result.success) {
                savedFile = result.filepath;
                dialog.success("File saved!", true);
                showMainScreen();
            } else {
                dialog.error("Failed to save file!", true);
                showMainScreen();
            }
        } else {
            // View saved data
            var text = formatTagInfo(readTag);
            dialog.viewText(text, "Saved Tag");
            showMainScreen();
        }
    }

    delay(50);
}

// Cleanup
rfid.clear();
display.fill(colours[0]);