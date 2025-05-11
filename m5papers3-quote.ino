#include <M5EPD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "fonts/literata_regular_18pt7b.h"
#include "fonts/literata_bold_18pt7b.h"
#include "fonts/literata_regular_20pt7b.h"
#include "fonts/literata_bold_20pt7b.h"
#include "env.h"
#include <driver/adc.h>

// Configuration structure
struct QuoteDisplayConfig {
    M5EPD_Canvas* display;
    const GFXfont* mainFont20;
    const GFXfont* boldFont20;
    const GFXfont* mainFont18;
    const GFXfont* boldFont18;
    float widthThreshold;
    int32_t batteryLineY;  // Y position for battery indicator
};

// Function declarations
void connectToWiFi();
bool fetchQuote(String& quote, String& followup, String& author, String& context);

// The actual M5GFX display and config
M5EPD_Canvas display(&M5.EPD);
QuoteDisplayConfig quoteDisplay;
bool isWiFiConnected = false;

const int QUOTE_REFRESH_INTERVAL = 14400; // how often should a new quote be shown
const int MAX_RETRIES = 5; // how many times to retry fetching a quote when the first attempt fails
const int BASE_ERROR_SLEEP_SEC = 60; // how long to sleep when the first attempt fails
RTC_DATA_ATTR int bootCount = 0; // how many attempts were already made
const int BATT_IND_HEIGHT = 6; // height of the battery indicator
const int32_t MANUAL_Y_OFFSET = -6;  // Adjust this value to shift text up/down

// Helper function to count actual newlines in text
static int countNewlines(const String& text) {
    int count = 0;
    int pos = 0;
    while ((pos = text.indexOf('\n', pos)) != -1) {
        count++;
        pos++;
    }
    return count;
}

// Helper function to normalize quotes and line endings
static String normalizeQuotes(const String& input) {
    String output = input;
    
    // Replace fancy quotes with ASCII equivalents
    char leftSingleQuote[] = {0xE2, 0x80, 0x98, 0}; // U+2018
    char rightSingleQuote[] = {0xE2, 0x80, 0x99, 0}; // U+2019
    char leftDoubleQuote[] = {0xE2, 0x80, 0x9C, 0}; // U+201C
    char rightDoubleQuote[] = {0xE2, 0x80, 0x9D, 0}; // U+201D
    
    output.replace(leftSingleQuote, "'");
    output.replace(rightSingleQuote, "'");
    output.replace(leftDoubleQuote, "\"");
    output.replace(rightDoubleQuote, "\"");
    output.replace("\r\n", "\n");
    
    return output;
}

// Calculate number of lines needed for text with given font
static int32_t calculateLines(QuoteDisplayConfig* qd, const char* text, const GFXfont* font) {
    qd->display->setFreeFont(font);
    String remaining = text;
    int32_t maxWidth = qd->display->width() * qd->widthThreshold;
    int lineCount = 0;
    
    while (remaining.length() > 0) {
        int newlinePos = remaining.indexOf('\n');
        String paragraph;
        
        if (newlinePos >= 0) {
            paragraph = remaining.substring(0, newlinePos);
            remaining = remaining.substring(newlinePos + 1);
        } else {
            paragraph = remaining;
            remaining = "";
        }
        
        if (paragraph.length() == 0) continue;
        
        while (paragraph.length() > 0) {
            lineCount++;
            int breakPoint = paragraph.length();
            
            while (breakPoint > 0) {
                String testLine = paragraph.substring(0, breakPoint);
                int32_t lineWidth = qd->display->textWidth(testLine.c_str());
                
                if (lineWidth <= maxWidth) {
                    int nextSpace = paragraph.lastIndexOf(' ', breakPoint);
                    if (nextSpace > 0 && lineWidth > maxWidth * 0.7) {
                        breakPoint = nextSpace;
                    }
                    break;
                }
                
                breakPoint = paragraph.lastIndexOf(' ', breakPoint - 1);
                if (breakPoint <= 0) {
                    breakPoint = 1;
                    while (breakPoint < paragraph.length()) {
                        String testStr = paragraph.substring(0, breakPoint + 1);
                        if (qd->display->textWidth(testStr.c_str()) > maxWidth) break;
                        breakPoint++;
                    }
                    break;
                }
            }
            
            paragraph = paragraph.substring(breakPoint);
            paragraph.trim();
        }
    }
    
    return lineCount;
}

// Draw text with automatic line breaks
static void drawTextLines(QuoteDisplayConfig* qd, const char* text, int32_t* yPos, bool isBold, bool use20pt) {
    const GFXfont* font = isBold ? 
        (use20pt ? qd->boldFont20 : qd->boldFont18) : 
        (use20pt ? qd->mainFont20 : qd->mainFont18);
    qd->display->setFreeFont(font);
    
    String remaining = text;
    int32_t maxWidth = qd->display->width() * qd->widthThreshold;
    
    while (remaining.length() > 0) {
        int newlinePos = remaining.indexOf('\n');
        String paragraph;
        
        if (newlinePos >= 0) {
            paragraph = remaining.substring(0, newlinePos);
            remaining = remaining.substring(newlinePos + 1);
        } else {
            paragraph = remaining;
            remaining = "";
        }
        
        if (paragraph.length() == 0) {
            *yPos += 15; // Extra space for explicit newlines
            continue;
        }
        
        while (paragraph.length() > 0) {
            int breakPoint = paragraph.length();
            int32_t lineWidth;
            
            while (breakPoint > 0) {
                String testLine = paragraph.substring(0, breakPoint);
                lineWidth = qd->display->textWidth(testLine.c_str());
                
                if (lineWidth <= maxWidth) {
                    int nextSpace = paragraph.lastIndexOf(' ', breakPoint);
                    if (nextSpace > 0 && lineWidth > maxWidth * 0.7) {
                        breakPoint = nextSpace;
                    }
                    break;
                }
                
                breakPoint = paragraph.lastIndexOf(' ', breakPoint - 1);
                if (breakPoint <= 0) {
                    breakPoint = 1;
                    while (breakPoint < paragraph.length()) {
                        String testStr = paragraph.substring(0, breakPoint + 1);
                        if (qd->display->textWidth(testStr.c_str()) > maxWidth) break;
                        breakPoint++;
                    }
                    break;
                }
            }
            
            String line = paragraph.substring(0, breakPoint);
            lineWidth = qd->display->textWidth(line.c_str());
            int32_t xPos = (qd->display->width() - lineWidth) / 2;
            
            qd->display->setCursor(xPos, *yPos);
            qd->display->print(line);
            
            *yPos += qd->display->fontHeight() + 5;
            
            paragraph = paragraph.substring(breakPoint);
            paragraph.trim();
        }
        
        if (remaining.length() > 0) {
            *yPos += 10; // Extra space between paragraphs
        }
    }
}

// Calculate total height needed for text
static int32_t calculateTotalHeight(QuoteDisplayConfig* qd, const String& quote, const String& followup, 
                                  const String& author, bool use20pt) {
    const GFXfont* mainFont = use20pt ? qd->mainFont20 : qd->mainFont18;
    const GFXfont* boldFont = use20pt ? qd->boldFont20 : qd->boldFont18;
    
    qd->display->setFreeFont(mainFont);
    int32_t quoteLines = calculateLines(qd, quote.c_str(), mainFont);
    int32_t followupLines = followup.length() > 0 ? calculateLines(qd, followup.c_str(), mainFont) : 0;
    int numNewlines = countNewlines(quote);
    
    int32_t mainFontHeight = qd->display->fontHeight();
    qd->display->setFreeFont(boldFont);
    int32_t boldFontHeight = qd->display->fontHeight();
    
    const int32_t LINE_SPACING = 5;
    const int32_t NEWLINE_EXTRA = 15;
    const int32_t BLOCK_SPACING = mainFontHeight * 0.5;
    
    int32_t quoteHeight = (quoteLines * mainFontHeight) + 
                         ((quoteLines - 1) * LINE_SPACING) +
                         (numNewlines * NEWLINE_EXTRA);
    int32_t followupHeight = followupLines > 0 ? 
        (followupLines * mainFontHeight) + ((followupLines - 1) * LINE_SPACING) : 0;
    
    return quoteHeight + 
           (followupHeight > 0 ? BLOCK_SPACING + followupHeight : 0) + 
           BLOCK_SPACING + boldFontHeight;
}

void quoteDisplay_init(QuoteDisplayConfig* qd, M5EPD_Canvas* display) {
    qd->display = display;
    qd->mainFont20 = &Literata_24pt_Regular20pt7b;
    qd->boldFont20 = &Literata_24pt_Bold20pt7b;
    qd->mainFont18 = &Literata_24pt_Regular18pt7b;
    qd->boldFont18 = &Literata_24pt_Bold18pt7b;
    qd->widthThreshold = 0.9f;
}

void quoteDisplay_show(QuoteDisplayConfig* qd, const char* quote, const char* followup, const char* author) {
    String normalizedQuote = normalizeQuotes(quote);
    String normalizedFollowup = followup ? normalizeQuotes(followup) : "";
    String normalizedAuthor = normalizeQuotes(author);
    
    bool use20pt = calculateTotalHeight(qd, normalizedQuote, normalizedFollowup, normalizedAuthor, true) <= 
                   qd->display->height();
    
    int32_t totalHeight = calculateTotalHeight(qd, normalizedQuote, normalizedFollowup, 
                                             normalizedAuthor, use20pt);
    const GFXfont* mainFont = use20pt ? qd->mainFont20 : qd->mainFont18;
    qd->display->setFreeFont(mainFont);
    
    int32_t visualOffset = qd->display->fontHeight() * 0.2;
    int32_t currentY = ((qd->display->height() - totalHeight) / 2) - visualOffset + MANUAL_Y_OFFSET;
    
    M5.EPD.Clear(true);
    delay(500);
    
    qd->display->fillCanvas(0);
    qd->display->setTextColor(15);
    
    drawTextLines(qd, normalizedQuote.c_str(), &currentY, false, use20pt);
    
    if (followup && strlen(followup) > 0) {
        currentY += qd->display->fontHeight() * 0.5;
        drawTextLines(qd, normalizedFollowup.c_str(), &currentY, false, use20pt);
    }
    
    currentY += qd->display->fontHeight() * 0.5;
    drawTextLines(qd, normalizedAuthor.c_str(), &currentY, true, use20pt);
    
    // Draw battery indicator
    int batteryPercent = getBatteryPercentage();
    int32_t batteryWidth = (qd->display->width() * batteryPercent) / 100;
    qd->display->fillRect(0, qd->display->height() - BATT_IND_HEIGHT, batteryWidth, BATT_IND_HEIGHT, 15);
    
    // Push to display with enhanced refresh sequence
    qd->display->pushCanvas(0, 0, UPDATE_MODE_GC16);
    delay(500);
    M5.EPD.UpdateFull(UPDATE_MODE_GC16);
    delay(500);
}

void connectToWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ENV_WIFI_SSID, ENV_WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    
    isWiFiConnected = (WiFi.status() == WL_CONNECTED);
}

bool fetchQuote(String& quote, String& followup, String& author, String& context) {
    if (!isWiFiConnected) return false;
    
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    
    if (https.begin(client, ENV_QUOTE_URL)) {
        int httpCode = https.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = https.getString();
            https.end();
            
            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                quote = doc["quote"].as<String>();
                followup = doc["followup"].isNull() ? "" : doc["followup"].as<String>();
                author = doc["author"].as<String>();
                context = doc["context"].isNull() ? "" : doc["context"].as<String>();
                return true;
            }
        }
        https.end();
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up...");
    
    // Initialize display for M5Paper
    M5.begin();
    M5.EPD.SetRotation(0);
    
    // Add thorough refresh sequence
    M5.EPD.Clear(true);  // Full clear
    delay(500);  // Give the display time to finish clearing
    M5.EPD.UpdateFull(UPDATE_MODE_GC16);  // Additional full refresh
    delay(500);
    
    // Create and initialize the canvas
    display.createCanvas(960, 540);
    display.setTextDatum(MC_DATUM);
    quoteDisplay_init(&quoteDisplay, &display);
    
    // Connect and fetch quote
    connectToWiFi();
    String quote = "Unable to fetch quote";
    String followup = "";
    String author = "Check WiFi connection";
    String context = "";
    
    bool success = false;
    if (isWiFiConnected) {
        if (fetchQuote(quote, followup, author, context)) {
            success = true;
            if (context.length() > 0) {
                followup = followup.length() > 0 ? followup + " - " + context : context;
            }
        } else {
            quote = "Failed to fetch quote";
            author = "Check API connection";
        }
    }
    
    quoteDisplay_show(&quoteDisplay, quote.c_str(), 
                     followup.length() > 0 ? followup.c_str() : NULL, 
                     author.c_str());

    // Calculate sleep duration based on success/failure
    uint64_t sleepTime;
    if (success) {
        bootCount = 0;  // Reset retry counter on success
        sleepTime = QUOTE_REFRESH_INTERVAL * 1000000ULL;  // 60 seconds
    } else {
        bootCount++;
        sleepTime = (uint64_t)BASE_ERROR_SLEEP_SEC * bootCount * 1000000ULL;  // Linear backoff
        if (bootCount >= MAX_RETRIES) {
            sleepTime = 60 * 60 * 1000000ULL;  // Sleep for 1 hour after max retries
            bootCount = 0;  // Reset counter
        }
    }

    esp_sleep_enable_timer_wakeup(sleepTime);
    esp_deep_sleep_start();
}

void loop() {
    // Loop is never reached due to deep sleep
}

// Simplify battery reading function for M5Paper
static int getBatteryPercentage() {
    // Convert raw battery reading to percentage
    int raw = M5.getBatteryRaw();
    // Battery voltage range is typically 3.2V (empty) to 4.2V (full)
    // Raw values are approximately 1900 (empty) to 2500 (full)
    int percentage = map(raw, 1900, 2500, 0, 100);
    percentage = constrain(percentage, 0, 100);
    return percentage;
}