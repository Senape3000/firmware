// RFID Load File - Bruce Firmware
// SD priority with manual pagination

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
    display.color(0, 255, 255),   // cyan
];

var exitApp = false;
var loadedTag = null;
var filename = "";
var currentPage = 0;
var totalPages = 1;
var pageContent = [];

const displayWidth = display.width();
const displayHeight = display.height();
const fontScale = (displayWidth > 300 ? 1 : 0);
const linesPerPage = Math.floor((displayHeight - 80) / (12 + fontScale * 5));

function showLoadScreen() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);
    display.setTextColor(BRUCE_PRICOLOR);
    display.drawText("RFID Load", displayWidth / 2, 20);
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);

    if (loadedTag === null) {
        display.drawText("Select: Pick file to load", displayWidth / 2, displayHeight / 2);
    } else {
        display.setTextColor(colours[3]);
        display.drawText("File loaded!", displayWidth / 2, displayHeight / 2 - 10);
        display.setTextColor(colours[2]);
        display.drawText("Select: View data", displayWidth / 2, displayHeight / 2 + 10);
    }

    display.setTextAlign('left', 'middle');
    display.setTextColor(colours[1]);
    display.drawText("Select: Load/View | Exit: Close", 10, displayHeight - 15);
}

function showLoading() {
    display.fill(colours[0]);
    display.setTextAlign('center', 'middle');
    display.setTextSize(2 + fontScale);
    display.setTextColor(colours[5]);
    display.drawText("Loading...", displayWidth / 2, displayHeight / 2 - 10);
    display.setTextSize(1 + fontScale);
    display.setTextColor(colours[2]);
    display.drawText(filename, displayWidth / 2, displayHeight / 2 + 15);
}

function formatPages(pagesString) {
    var lines = [];
    var currentLine = "";

    for (var i = 0; i < pagesString.length; i++) {
        var char = pagesString.charAt(i);

        if (char === '\n' || i === pagesString.length - 1) {
            if (i === pagesString.length - 1 && char !== '\n') {
                currentLine += char;
            }

            if (currentLine.length > 0) {
                var pageIdx = currentLine.indexOf("Page ");
                if (pageIdx >= 0) {
                    var colonIdx = currentLine.indexOf(":", pageIdx);
                    if (colonIdx > 0) {
                        var pageNumStr = currentLine.substring(pageIdx + 5, colonIdx).trim();
                        var pageNum = parseInt(pageNumStr);
                        var bytesStr = currentLine.substring(colonIdx + 1).trim();

                        var bytesNoSpaces = "";
                        for (var j = 0; j < bytesStr.length; j++) {
                            if (bytesStr.charAt(j) !== ' ') {
                                bytesNoSpaces += bytesStr.charAt(j);
                            }
                        }

                        if (bytesNoSpaces.length > 0) {
                            lines.push("P" + pageNum + ":" + bytesNoSpaces);
                        }
                    }
                }
            }

            currentLine = "";
        } else {
            currentLine += char;
        }
    }

    return lines;
}

function formatTagData(tagData) {
    var lines = [];
    lines.push("=== RFID FILE LOADED ===");
    lines.push("");
    lines.push("Filename:");
    lines.push(filename);
    lines.push("");
    lines.push("UID:");
    lines.push(tagData.uid);
    lines.push("");
    lines.push("Type:");
    lines.push(tagData.type);
    lines.push("");
    lines.push("SAK: " + tagData.sak);
    lines.push("ATQA: " + tagData.atqa);
    lines.push("BCC: " + tagData.bcc);
    lines.push("");
    lines.push("Total Pages: " + tagData.totalPages);
    lines.push("Data Pages: " + tagData.dataPages);

    if (tagData.pages && tagData.pages.length > 0) {
        lines.push("");
        lines.push("=== PAGE DATA ===");
        var formattedPages = formatPages(tagData.pages);
        for (var i = 0; i < formattedPages.length; i++) {
            lines.push(formattedPages[i]);
        }
    }

    lines.push("");
    lines.push("=== STATUS ===");
    lines.push("Data is in memory!");
    lines.push("Use rfid.write() to clone");

    return lines;
}

function displayPage() {
    display.fill(colours[0]);
    display.setTextAlign('left', 'top');
    display.setTextSize(2 + fontScale);
    display.setTextColor(BRUCE_PRICOLOR);
    display.drawText("RFID Data", 10, 5);

    display.setTextSize(1 + fontScale);
    display.setTextAlign('right', 'top');
    display.setTextColor(colours[1]);
    display.drawText("Page " + (currentPage + 1) + "/" + totalPages, displayWidth - 10, 8);

    display.setTextAlign('left', 'top');
    var y = 30;
    var startLine = currentPage * linesPerPage;
    var endLine = Math.min(startLine + linesPerPage, pageContent.length);

    for (var i = startLine; i < endLine; i++) {
        var line = pageContent[i];

        var isPageDataLine = (line.indexOf("P") === 0 && line.indexOf(":") > 0 && line.length > 10);

        if (isPageDataLine) {
            display.setTextSize(1);
            display.setTextColor(colours[2]);
        } else if (line.indexOf("===") === 0) {
            display.setTextSize(1 + fontScale);
            display.setTextColor(colours[3]);
        } else if (line.indexOf(":") > 0 && line.length < 30) {
            display.setTextSize(1 + fontScale);
            display.setTextColor(colours[1]);
        } else {
            display.setTextSize(1 + fontScale);
            display.setTextColor(colours[2]);
        }

        display.drawText(line, 10, y);

        if (isPageDataLine) {
            y += 10;
        } else {
            y += 12 + fontScale * 5;
        }
    }

    display.setTextAlign('left', 'bottom');
    display.setTextColor(colours[1]);
    var navY = displayHeight - 15;
    if (totalPages > 1) {
        display.drawText("Next/Prev: Page | Esc: Back", 10, navY);
    } else {
        display.drawText("Esc: Back", 10, navY);
    }
}

showLoadScreen();

while (!exitApp) {
    if (keyboard.getEscPress()) {
        if (pageContent.length > 0) {
            pageContent = [];
            currentPage = 0;
            showLoadScreen();
        } else {
            exitApp = true;
            break;
        }
    }

    if (keyboard.getSelPress()) {
        if (loadedTag === null && pageContent.length === 0) {
            var filepath = dialog.pickFile({
                fs: "sd",
                path: "/BruceRFID"
            }, "rfid");

            if (!filepath || filepath.length === 0) {
                filepath = dialog.pickFile({
                    fs: "littlefs",
                    path: "/BruceRFID"
                }, "rfid");
            }

            if (filepath && filepath.length > 0) {
                var lastSlash = filepath.lastIndexOf("/");
                var fileNameOnly = filepath.substring(lastSlash + 1);
                if (fileNameOnly.endsWith(".rfid")) {
                    fileNameOnly = fileNameOnly.substring(0, fileNameOnly.length - 5);
                }

                showLoading();
                filename = fileNameOnly + ".rfid";

                var tagData = rfid.load(fileNameOnly);
                if (tagData) {
                    loadedTag = tagData;
                    dialog.success("File loaded!", true);
                    showLoadScreen();
                } else {
                    dialog.error("Failed to load file!", true);
                    showLoadScreen();
                }
            } else {
                showLoadScreen();
            }
        } else if (loadedTag !== null && pageContent.length === 0) {
            pageContent = formatTagData(loadedTag);
            totalPages = Math.ceil(pageContent.length / linesPerPage);
            currentPage = 0;
            displayPage();
        }
    }

    if (pageContent.length > 0) {
        if (keyboard.getNextPress()) {
            currentPage++;
            if (currentPage >= totalPages) {
                currentPage = 0;
            }
            displayPage();
        }

        if (keyboard.getPrevPress()) {
            currentPage--;
            if (currentPage < 0) {
                currentPage = totalPages - 1;
            }
            displayPage();
        }
    }

    delay(50);
}

rfid.clear();
display.fill(colours[0]);
