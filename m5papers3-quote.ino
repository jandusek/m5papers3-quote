#include <epdiy.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "fonts/literata_regular_18pt7b.h"
#include "fonts/literata_bold_18pt7b.h"
#include "fonts/literata_regular_20pt7b.h"
#include "fonts/literata_bold_20pt7b.h"
#include "env.h"

// Configuration structure
struct QuoteDisplayConfig {
    M5GFX* display;
    const GFXfont* mainFont20;
    const GFXfont* boldFont20;
    const GFXfont* mainFont18;
    const GFXfont* boldFont18;
    float widthThreshold;
};

// Function declarations
void connectToWiFi();
bool fetchQuote(String& quote, String& followup, String& author, String& context);

// The actual M5GFX display and config
M5GFX display;
QuoteDisplayConfig quoteDisplay;
bool isWiFiConnected = false;

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
    qd->display->setFont(font);
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
    qd->display->setFont(font);
    
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
    
    qd->display->setFont(mainFont);
    int32_t quoteLines = calculateLines(qd, quote.c_str(), mainFont);
    int32_t followupLines = followup.length() > 0 ? calculateLines(qd, followup.c_str(), mainFont) : 0;
    int numNewlines = countNewlines(quote);
    
    int32_t mainFontHeight = qd->display->fontHeight();
    qd->display->setFont(boldFont);
    int32_t boldFontHeight = qd->display->fontHeight();
    
    const int32_t LINE_SPACING = 5;
    const int32_t NEWLINE_EXTRA = 15;
    const int32_t BLOCK_SPACING = mainFontHeight * 0.5;
    
    int32_t quoteHeight = (quoteLines - 1) * (mainFontHeight + LINE_SPACING) + 
                         mainFontHeight + (numNewlines * NEWLINE_EXTRA);
    int32_t followupHeight = followupLines > 0 ? 
        (followupLines - 1) * (mainFontHeight + LINE_SPACING) + mainFontHeight : 0;
    
    return quoteHeight + 
           (followupHeight > 0 ? BLOCK_SPACING + followupHeight : 0) + 
           BLOCK_SPACING + boldFontHeight;
}

void quoteDisplay_init(QuoteDisplayConfig* qd, M5GFX* display) {
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
    
    // Try 20pt first, fall back to 18pt if content doesn't fit
    bool use20pt = calculateTotalHeight(qd, normalizedQuote, normalizedFollowup, normalizedAuthor, true) <= 
                   qd->display->height();
    
    // Calculate final layout with chosen font
    int32_t totalHeight = calculateTotalHeight(qd, normalizedQuote, normalizedFollowup, 
                                             normalizedAuthor, use20pt);
    int32_t currentY = (qd->display->height() - totalHeight) / 2;
    
    // Draw content
    qd->display->startWrite();
    qd->display->fillScreen(TFT_WHITE);
    qd->display->setTextColor(TFT_BLACK);
    
    drawTextLines(qd, normalizedQuote.c_str(), &currentY, false, use20pt);
    
    if (followup && strlen(followup) > 0) {
        currentY += qd->display->fontHeight() * 0.5;
        drawTextLines(qd, normalizedFollowup.c_str(), &currentY, false, use20pt);
    }
    
    currentY += qd->display->fontHeight() * 0.5;
    drawTextLines(qd, normalizedAuthor.c_str(), &currentY, true, use20pt);
    
    qd->display->endWrite();
    qd->display->display();
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
    // Initialize display
    display.init();
    
    if (display.isEPD()) {
        display.setEpdMode(epd_mode_t::epd_quality);
    }
    
    if (display.width() < display.height()) {
        display.setRotation(display.getRotation() ^ 1);
    }
    
    quoteDisplay_init(&quoteDisplay, &display);
    
    // Connect and fetch quote
    connectToWiFi();
    String quote = "Unable to fetch quote";
    String followup = "";
    String author = "Check WiFi connection";
    String context = "";
    
    if (isWiFiConnected) {
        if (fetchQuote(quote, followup, author, context)) {
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
}

void loop() {
    delay(1000);
}