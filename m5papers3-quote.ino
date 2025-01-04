#include <epdiy.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "fonts/literata_regular_20pt7b.h"
#include "fonts/literata_bold_20pt7b.h"
#include "env.h"

// Configuration structure
struct QuoteDisplayConfig {
    M5GFX* display;
    const GFXfont* mainFont;
    const GFXfont* boldFont;
    float widthThreshold;
};

// Function declarations
static int32_t calculateVerticalSpacing(QuoteDisplayConfig* qd);
static void drawTextLines(QuoteDisplayConfig* qd, const char* text, int32_t* yPos, bool isBold);
void connectToWiFi();
bool fetchQuote(String& quote, String& followup, String& author, String& context);

// The actual M5GFX display
M5GFX display;
QuoteDisplayConfig quoteDisplay;

// WiFi connection status
bool isWiFiConnected = false;

// Implementation
void quoteDisplay_init(QuoteDisplayConfig* qd, M5GFX* display) {
    qd->display = display;
    qd->widthThreshold = 0.9f;
}

void quoteDisplay_setFonts(QuoteDisplayConfig* qd, const GFXfont* regularFont, const GFXfont* boldFont) {
    qd->mainFont = regularFont;
    qd->boldFont = boldFont;
}

void connectToWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ENV_WIFI_SSID, ENV_WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    isWiFiConnected = (WiFi.status() == WL_CONNECTED);
    if (isWiFiConnected) {
        Serial.println("\nConnected to WiFi");
    } else {
        Serial.println("\nFailed to connect to WiFi");
    }
}

bool fetchQuote(String& quote, String& followup, String& author, String& context) {
    if (!isWiFiConnected) {
        Serial.println("WiFi not connected");
        return false;
    }
    
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate verification
    
    HTTPClient https;
    Serial.println("Fetching quote from API...");
    
    if (https.begin(client, ENV_QUOTE_URL)) {
        int httpCode = https.GET();
        Serial.printf("HTTP Response code: %d\n", httpCode);
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = https.getString();
            https.end();
            
            // Use a larger buffer for JSON parsing
            DynamicJsonDocument doc(2048);
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                quote = doc["quote"].as<String>();
                followup = doc["followup"].isNull() ? "" : doc["followup"].as<String>();
                author = doc["author"].as<String>();
                context = doc["context"].isNull() ? "" : doc["context"].as<String>();
                Serial.println("Quote fetched successfully");
                return true;
            } else {
                Serial.print("JSON parsing failed: ");
                Serial.println(error.c_str());
            }
        }
        https.end();
    } else {
        Serial.println("HTTPS connection failed");
    }
    return false;
}

static int32_t calculateVerticalSpacing(QuoteDisplayConfig* qd) {
    return qd->display->fontHeight() * 0.5;
}

static void drawTextLines(QuoteDisplayConfig* qd, const char* text, int32_t* yPos, bool isBold) {
    qd->display->setFont(isBold ? qd->boldFont : qd->mainFont);
    
    String remaining = text;
    int32_t maxWidth = qd->display->width() * qd->widthThreshold;
    
    while (remaining.length() > 0) {
        int breakPoint = remaining.length();
        int32_t lineWidth;
        
        while (breakPoint > 0) {
            String testLine = remaining.substring(0, breakPoint);
            lineWidth = qd->display->textWidth(testLine.c_str());
            
            if (lineWidth <= maxWidth) {
                int nextSpace = remaining.lastIndexOf(' ', breakPoint);
                if (nextSpace > 0 && lineWidth > maxWidth * 0.7) {
                    breakPoint = nextSpace;
                }
                break;
            }
            
            breakPoint = remaining.lastIndexOf(' ', breakPoint - 1);
            if (breakPoint <= 0) {
                breakPoint = 1;
                while (breakPoint < remaining.length()) {
                    String testStr = remaining.substring(0, breakPoint + 1);
                    if (qd->display->textWidth(testStr.c_str()) > maxWidth) {
                        break;
                    }
                    breakPoint++;
                }
                break;
            }
        }
        
        String line = remaining.substring(0, breakPoint);
        lineWidth = qd->display->textWidth(line.c_str());
        int32_t xPos = (qd->display->width() - lineWidth) / 2;
        
        qd->display->setCursor(xPos, *yPos);
        qd->display->print(line);
        
        *yPos += qd->display->fontHeight() + 5;
        
        remaining = remaining.substring(breakPoint);
        remaining.trim();
    }
}

// Function to normalize fancy quotes and apostrophes to ASCII
static String normalizeQuotes(const String& input) {
    String output = input;
    
    // Create a buffer for UTF-8 characters
    char leftSingleQuote[] = {0xE2, 0x80, 0x98, 0}; // U+2018 Left single quote
    char rightSingleQuote[] = {0xE2, 0x80, 0x99, 0}; // U+2019 Right single quote
    char leftDoubleQuote[] = {0xE2, 0x80, 0x9C, 0}; // U+201C Left double quote
    char rightDoubleQuote[] = {0xE2, 0x80, 0x9D, 0}; // U+201D Right double quote
    
    // Replace quotes with ASCII equivalents
    output.replace(leftSingleQuote, "'");
    output.replace(rightSingleQuote, "'");
    output.replace(leftDoubleQuote, "\"");
    output.replace(rightDoubleQuote, "\"");
    
    return output;
}

// Add this new function to calculate number of lines
static int32_t calculateLines(QuoteDisplayConfig* qd, const char* text) {
    String remaining = text;
    int32_t maxWidth = qd->display->width() * qd->widthThreshold;
    int lineCount = 0;
    
    while (remaining.length() > 0) {
        lineCount++;
        int breakPoint = remaining.length();
        
        while (breakPoint > 0) {
            String testLine = remaining.substring(0, breakPoint);
            int32_t lineWidth = qd->display->textWidth(testLine.c_str());
            
            if (lineWidth <= maxWidth) {
                int nextSpace = remaining.lastIndexOf(' ', breakPoint);
                if (nextSpace > 0 && lineWidth > maxWidth * 0.7) {
                    breakPoint = nextSpace;
                }
                break;
            }
            
            breakPoint = remaining.lastIndexOf(' ', breakPoint - 1);
            if (breakPoint <= 0) {
                breakPoint = 1;
                while (breakPoint < remaining.length()) {
                    String testStr = remaining.substring(0, breakPoint + 1);
                    if (qd->display->textWidth(testStr.c_str()) > maxWidth) {
                        break;
                    }
                    breakPoint++;
                }
                break;
            }
        }
        
        remaining = remaining.substring(breakPoint);
        remaining.trim();
    }
    
    return lineCount;
}

// Modified quoteDisplay_show function
void quoteDisplay_show(QuoteDisplayConfig* qd, const char* quote, const char* followup, const char* author) {
    qd->display->startWrite();
    qd->display->fillScreen(TFT_WHITE);
    qd->display->setTextColor(TFT_BLACK);
    
    // Normalize quotes in all text
    String normalizedQuote = normalizeQuotes(quote);
    String normalizedFollowup = followup ? normalizeQuotes(followup) : "";
    String normalizedAuthor = normalizeQuotes(author);
    
    // Calculate line counts using normalized text
    qd->display->setFont(qd->mainFont);
    int32_t quoteLines = calculateLines(qd, normalizedQuote.c_str());
    int32_t followupLines = followup ? calculateLines(qd, normalizedFollowup.c_str()) : 0;
    
    // Define spacing constants
    const int32_t LINE_SPACING = 5;  // Space between lines within same text block
    const int32_t BLOCK_SPACING = calculateVerticalSpacing(qd);  // Space between different text blocks
    
    // Calculate individual component heights including internal line spacing
    int32_t quoteHeight = (quoteLines - 1) * (qd->display->fontHeight() + LINE_SPACING) + qd->display->fontHeight();
    int32_t followupHeight = followupLines > 0 ? 
        (followupLines - 1) * (qd->display->fontHeight() + LINE_SPACING) + qd->display->fontHeight() : 0;
    
    // Switch to bold font to get correct author height
    qd->display->setFont(qd->boldFont);
    int32_t authorHeight = qd->display->fontHeight();
    qd->display->setFont(qd->mainFont);  // Switch back
    
    // Calculate total height including all spacing
    int32_t totalHeight = quoteHeight +                          // Quote block
                         (followupHeight > 0 ? BLOCK_SPACING + followupHeight : 0) +  // Followup block with spacing
                         BLOCK_SPACING + authorHeight;            // Author block with spacing
    
    // Calculate starting Y position for true vertical centering
    int32_t currentY = (qd->display->height() - totalHeight) / 2;
    
    // Draw the text using normalized versions
    drawTextLines(qd, normalizedQuote.c_str(), &currentY, false);
    
    if (followup && strlen(followup) > 0) {
        currentY += BLOCK_SPACING;
        drawTextLines(qd, normalizedFollowup.c_str(), &currentY, false);
    }
    
    currentY += BLOCK_SPACING;
    drawTextLines(qd, normalizedAuthor.c_str(), &currentY, true);
    
    qd->display->endWrite();
    qd->display->display();
}

void setup(void) {
    Serial.begin(115200);
    delay(1000);  // Give serial time to initialize
    Serial.println("Starting up...");
    
    // Initialize display
    display.init();
    Serial.println("Display initialized");
    
    if (display.isEPD()) {
        display.setEpdMode(epd_mode_t::epd_quality);  // Changed from epd_fastest to epd_quality
        Serial.println("EPD mode set to quality");
    }
    
    if (display.width() < display.height()) {
        display.setRotation(display.getRotation() ^ 1);
    }
    
    quoteDisplay_init(&quoteDisplay, &display);
    quoteDisplay_setFonts(&quoteDisplay, &Literata_24pt_Regular20pt7b, &Literata_24pt_Bold20pt7b);
    
    // Connect to WiFi
    connectToWiFi();
    
    // Default quote in case of connection failure
    String quote = "Unable to fetch quote";
    String followup = "";
    String author = "Check WiFi connection";
    String context = "";
    
    if (isWiFiConnected) {
        if (!fetchQuote(quote, followup, author, context)) {
            quote = "Failed to fetch quote";
            author = "Check API connection";
        }
        // If context exists, append it to followup
        if (context.length() > 0) {
            if (followup.length() > 0) {
                followup += " - " + context;
            } else {
                followup = context;
            }
        }
    }
    
    quoteDisplay_show(&quoteDisplay, quote.c_str(), 
                     followup.length() > 0 ? followup.c_str() : NULL, 
                     author.c_str());
                     
    Serial.println("Setup complete");
}

void loop(void) {
    delay(1000);
}