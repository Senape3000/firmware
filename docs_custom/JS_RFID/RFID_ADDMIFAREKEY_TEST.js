// RFID addMifareKey Test - Bruce Firmware
// Tests validation with GUI display

const display = require("display");
const keyboard = require("keyboard");
const rfid = require("rfid");

const colours = [
    display.color(0, 0, 0),       // 0: black
    display.color(127, 127, 127), // 1: grey
    display.color(255, 255, 255), // 2: white
    display.color(0, 255, 0),     // 3: green
    display.color(255, 0, 0),     // 4: red
    display.color(255, 165, 0),   // 5: orange
    display.color(0, 255, 255),   // 6: cyan
];

var exitApp = false;
var currentTest = 0;
var testResults = [];

const displayWidth = display.width();
const displayHeight = display.height();
const fontScale = (displayWidth > 300 ? 1 : 0);

// Test cases
var testKeys = [
    {key: "FFFFFFFFFFFF", desc: "Valid 12 hex", expected: true},
    {key: "ABCDEF123456", desc: "Valid lowercase", expected: true},
    {key: "FFFFFFFFFFFF77", desc: "Too long (14)", expected: false},
    {key: "GGGGGGGGGGGG", desc: "Invalid hex (G)", expected: false}
];

function showMainScreen() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);
    display.setTextColor(BRUCE_PRICOLOR);
    display.drawText("MIFARE Key Test", displayWidth / 2, 15);

    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[6]); // cyan
    display.drawText("addMifareKey() Validation", displayWidth / 2, 35);

    // Test info
    display.setTextAlign('left', 'middle');
    display.setTextColor(colours[2]);
    display.drawText("Test " + (currentTest + 1) + "/" + testKeys.length, 10, 60);

    // Current test details
    var y = 80;
    var test = testKeys[currentTest];

    display.setTextColor(colours[1]);
    display.drawText("Description:", 10, y);
    y += 15;
    display.setTextColor(colours[2]);
    display.drawText(test.desc, 10, y);
    y += 20;

    display.setTextColor(colours[1]);
    display.drawText("Key to test:", 10, y);
    y += 15;
    display.setTextColor(colours[5]); // orange
    display.drawText(test.key, 10, y);
    y += 20;

    display.setTextColor(colours[1]);
    display.drawText("Expected:  ", 10, y);
    display.setTextColor(test.expected ? colours[3] : colours[4]);
    display.drawText(test.expected ? "VALID" : "INVALID", 100, y);

    // Instructions
    display.setTextAlign('center', 'middle');
    display.setTextColor(colours[2]);
    display.drawText("Press NEXT to test key", displayWidth / 2, displayHeight - 50);

    display.setTextAlign('left', 'middle');
    display.setTextColor(colours[1]);
    display.drawText("Next: Test | Esc: Exit", 10, displayHeight - 15);
}

function showTesting() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);
    display.setTextColor(colours[5]); // orange
    display.drawText("Testing...", displayWidth / 2, displayHeight / 2 - 20);

    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);
    display.drawText("Calling addMifareKey()", displayWidth / 2, displayHeight / 2 + 10);
}

function showResult(result, test) {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);

    var passed = result.success === test.expected;

    display.setTextColor(passed ? colours[3] : colours[4]); // green or red
    display.drawText(passed ? "PASS" : "FAIL", displayWidth / 2, 20);

    // Test details
    display.setTextSize(1 + fontScale);
    display.setTextAlign('left', 'middle');
    var y = 50;

    display.setTextColor(colours[1]);
    display.drawText("Key:", 10, y);
    display.setTextColor(colours[2]);
    display.drawText(test.key, 60, y);
    y += 20;

    display.setTextColor(colours[1]);
    display.drawText("Expected:", 10, y);
    display.setTextColor(test.expected ? colours[3] : colours[4]);
    display.drawText(test.expected ? "  VALID" : "  INVALID", 90, y);
    y += 20;

    display.setTextColor(colours[1]);
    display.drawText("Got:", 10, y);
    display.setTextColor(result.success ? colours[3] : colours[4]);
    display.drawText(result.success ? "SUCCESS" : "FAIL", 90, y);
    y += 20;

    display.setTextColor(colours[1]);
    display.drawText("Message:", 10, y);
    y += 15;
    display.setTextColor(colours[2]);

    // Word wrap long messages
    var msg = result.message;
    if (msg.length > 25) {
        display.drawText(msg.substring(0, 25), 10, y);
        y += 12;
        if (msg.length > 25) {
            display.drawText(msg.substring(25), 10, y);
        }
    } else {
        display.drawText(msg, 10, y);
    }

    y += 25;

    // Result
    display.setTextAlign('center', 'middle');
    display.setTextSize(1 + fontScale);
    if (passed) {
        display.setTextColor(colours[3]);
        display.drawText("✓ Test passed!", displayWidth / 2, y);
    } else {
        display.setTextColor(colours[4]);
        display.drawText("✗ Test failed!", displayWidth / 2, y);
    }

    // Instructions
    display.setTextAlign('left', 'middle');
    display.setTextColor(colours[1]);
    display.drawText("Next: Continue | Esc: Exit", 10, displayHeight - 15);

    // Store result
    testResults.push({
        test: test.desc,
        key: test.key,
        passed: passed,
        message: result.message
    });
}

function showSummary() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);
    display.setTextColor(BRUCE_PRICOLOR);
    display.drawText("Test Complete", displayWidth / 2, 15);

    // Calculate stats
    var passed = 0;
    var failed = 0;
    for (var i = 0; i < testResults.length; i++) {
        if (testResults[i].passed) {
            passed++;
        } else {
            failed++;
        }
    }

    display.setTextSize(1 + fontScale);
    display.setTextAlign('left', 'middle');
    var y = 45;

    display.setTextColor(colours[1]);
    display.drawText("Summary:", 10, y);
    y += 20;

    display.setTextColor(colours[3]);
    display.drawText("Passed: " + passed, 10, y);
    y += 15;

    display.setTextColor(colours[4]);
    display.drawText("Failed: " + failed, 10, y);
    y += 15;

    display.setTextColor(colours[2]);
    display.drawText("Total:  " + testResults.length, 10, y);
    y += 25;

    // Show each result
    display.setTextColor(colours[1]);
    display.drawText("Results:", 10, y);
    y += 15;

    for (var i = 0; i < testResults.length; i++) {
        var res = testResults[i];
        display.setTextColor(res.passed ? colours[3] : colours[4]);
        var icon = res.passed ? "✓" : "✗";
        display.drawText(icon + " Test " + (i + 1), 10, y);
        y += 12;
    }

    display.setTextAlign('center', 'middle');
    display.setTextColor(colours[2]);
    display.drawText("Press ESC to exit", displayWidth / 2, displayHeight - 30);
}

// Main program
console.log("=== MIFARE Key Test Started ===");

showMainScreen();

while (!exitApp) {
    if (keyboard.getEscPress()) {
        exitApp = true;
        break;
    }

    if (keyboard.getNextPress()) {
        if (currentTest < testKeys.length) {
            // Run test
            var test = testKeys[currentTest];

            showTesting();
            delay(500);

            console.log("\nTest " + (currentTest + 1) + ": " + test.desc);
            console.log("  Key: " + test.key);
            console.log("  Expected: " + (test.expected ? "VALID" : "INVALID"));

            // Call the API
            var result = rfid.addMifareKey(test.key);

            console.log("  Result: " + (result.success ? "SUCCESS" : "FAIL"));
            console.log("  Message: " + result.message);

            // Show result
            showResult(result, test);

            // Wait for key press
            var waiting = true;
            while (waiting && !exitApp) {
                if (keyboard.getEscPress()) {
                    exitApp = true;
                    waiting = false;
                }
                if (keyboard.getNextPress()) {
                    waiting = false;
                }
                delay(50);
            }

            currentTest++;

            if (currentTest < testKeys.length && !exitApp) {
                showMainScreen();
            }
        }

        // All tests done
        if (currentTest >= testKeys.length) {
            showSummary();

            // Wait for exit
            while (!exitApp) {
                if (keyboard.getEscPress()) {
                    exitApp = true;
                }
                delay(50);
            }
        }
    }

    delay(50);
}

// Exit message
display.fill(colours[0]);
display.setTextAlign('center', 'middle');
display.setTextSize(1 + fontScale);
display.setTextColor(colours[2]);
display.drawText("Test session ended", displayWidth / 2, displayHeight / 2);
delay(1000);

console.log("\n=== Test Summary ===");
var passed = 0;
for (var i = 0; i < testResults.length; i++) {
    if (testResults[i].passed) passed++;
}
console.log("Passed: " + passed + "/" + testResults.length);

display.fill(colours[0]);
