#include "UbidotsProvider.h"

UbidotsProvider::UbidotsProvider() {
    statusMessage = "Not initialized";
}

UbidotsProvider::~UbidotsProvider() {
    // Cleanup if needed
}

bool UbidotsProvider::begin(const String& token, const String& deviceLabel) {
    if (token.length() == 0 || deviceLabel.length() == 0) {
        statusMessage = "Invalid credentials";
        configured = false;
        return false;
    }
    
    this->token = token;
    this->deviceLabel = deviceLabel;
    this->configured = true;
    this->statusMessage = "Ubidots configured";
    
    Serial.println("[Ubidots] Provider initialized");
    Serial.println("  Device: " + deviceLabel);
    Serial.println("  Token: " + token.substring(0, 10) + "...");
    
    return true;
}

void UbidotsProvider::updateCredentials(const String& token, const String& deviceLabel) {
    begin(token, deviceLabel);
}

bool UbidotsProvider::isConfigured() {
    return configured && token.length() > 0 && deviceLabel.length() > 0;
}

String UbidotsProvider::getProviderName() {
    return "Ubidots";
}

String UbidotsProvider::getStatusMessage() {
    return statusMessage;
}

bool UbidotsProvider::canSend() {
    unsigned long now = millis();
    if (now - lastSendTime < minInterval) {
        statusMessage = "Rate limited (wait " + String((minInterval - (now - lastSendTime)) / 1000) + "s)";
        return false;
    }
    return true;
}

String UbidotsProvider::buildJsonPayload(const SensorData& data) {
    if (data.isEmpty()) {
        return "{}";
    }
    
    String payload = "{";
    bool first = true;
    
    for (const auto& pair : data.values) {
        if (!first) {
            payload += ",";
        }
        payload += "\"" + pair.first + "\":" + String(pair.second, 2);
        first = false;
    }
    
    payload += "}";
    return payload;
}

CloudStatus UbidotsProvider::sendData(const SensorData& data) {
    if (!isConfigured()) {
        statusMessage = "Provider not configured";
        Serial.println("[Ubidots] Error: Not configured");
        return CLOUD_ERROR_AUTH;
    }
    
    if (data.isEmpty()) {
        statusMessage = "No data to send";
        Serial.println("[Ubidots] Error: Empty data");
        return CLOUD_ERROR_INVALID_DATA;
    }
    
    if (!canSend()) {
        Serial.println("[Ubidots] Rate limited, skipping send");
        return CLOUD_ERROR_TIMEOUT;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        statusMessage = "WiFi not connected";
        Serial.println("[Ubidots] Error: No WiFi");
        return CLOUD_ERROR_NETWORK;
    }
    
    HTTPClient http;
    String url = "https://" + server + "/api/v1.6/devices/" + deviceLabel;
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Auth-Token", token);
    
    String payload = buildJsonPayload(data);
    
    Serial.println("[Ubidots] Sending data:");
    Serial.println("  URL: " + url);
    Serial.println("  Payload: " + payload);
    
    int httpCode = http.POST(payload);
    lastHttpCode = httpCode;
    lastSendTime = millis();
    
    CloudStatus result;
    
    if (httpCode == 200 || httpCode == 201) {
        statusMessage = "Success (Code: " + String(httpCode) + ")";
        Serial.println("[Ubidots] Upload successful: " + String(httpCode));
        result = CLOUD_SUCCESS;
    } else if (httpCode == 401 || httpCode == 403) {
        statusMessage = "Auth error (Code: " + String(httpCode) + ")";
        Serial.println("[Ubidots] Auth error: " + String(httpCode));
        result = CLOUD_ERROR_AUTH;
    } else if (httpCode == 400) {
        statusMessage = "Invalid data (Code: " + String(httpCode) + ")";
        Serial.println("[Ubidots] Invalid data: " + String(httpCode));
        result = CLOUD_ERROR_INVALID_DATA;
    } else if (httpCode >= 500) {
        statusMessage = "Server error (Code: " + String(httpCode) + ")";
        Serial.println("[Ubidots] Server error: " + String(httpCode));
        result = CLOUD_ERROR_SERVER;
    } else if (httpCode < 0) {
        statusMessage = "Connection failed (Code: " + String(httpCode) + ")";
        Serial.println("[Ubidots] Connection failed: " + String(httpCode));
        result = CLOUD_ERROR_NETWORK;
    } else {
        statusMessage = "Unknown error (Code: " + String(httpCode) + ")";
        Serial.println("[Ubidots] Unknown error: " + String(httpCode));
        result = CLOUD_ERROR_SERVER;
    }
    
    http.end();
    return result;
}

bool UbidotsProvider::testConnection() {
    if (!isConfigured()) {
        statusMessage = "Provider not configured";
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        statusMessage = "WiFi not connected";
        return false;
    }
    
    HTTPClient http;
    String url = "https://" + server + "/api/v1.6/devices/" + deviceLabel;
    
    http.begin(url);
    http.addHeader("X-Auth-Token", token);
    http.setTimeout(5000);
    
    Serial.println("[Ubidots] Testing connection...");
    Serial.println("  URL: " + url);
    
    int httpCode = http.GET();
    
    bool success = (httpCode == 200 || httpCode == 201 || httpCode == 404);
    
    if (success) {
        statusMessage = "Connection OK (Code: " + String(httpCode) + ")";
        Serial.println("[Ubidots] Connection test passed: " + String(httpCode));
    } else {
        statusMessage = "Connection failed (Code: " + String(httpCode) + ")";
        Serial.println("[Ubidots] Connection test failed: " + String(httpCode));
    }
    
    http.end();
    return success;
}
