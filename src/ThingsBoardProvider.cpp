#include "ThingsBoardProvider.h"

ThingsBoardProvider::ThingsBoardProvider() {
    statusMessage = "Not initialized";
}

ThingsBoardProvider::~ThingsBoardProvider() {
    // Cleanup if needed
}

bool ThingsBoardProvider::begin(const String& token, const String& deviceLabel) {
    // ThingsBoard menggunakan access token sebagai credential utama
    // deviceLabel optional (bisa digunakan untuk device name/label)
    if (token.length() == 0) {
        statusMessage = "Invalid access token";
        configured = false;
        return false;
    }
    
    this->token = token;
    this->deviceLabel = deviceLabel;
    this->configured = true;
    this->statusMessage = "ThingsBoard configured";
    
    Serial.println("[ThingsBoard] Provider initialized");
    Serial.println("  Server: " + server);
    Serial.println("  Token: " + token.substring(0, 10) + "...");
    if (deviceLabel.length() > 0) {
        Serial.println("  Device Label: " + deviceLabel);
    }
    
    return true;
}

void ThingsBoardProvider::updateCredentials(const String& token, const String& deviceLabel) {
    begin(token, deviceLabel);
}

bool ThingsBoardProvider::isConfigured() {
    return configured && token.length() > 0;
}

String ThingsBoardProvider::getProviderName() {
    return "ThingsBoard";
}

String ThingsBoardProvider::getStatusMessage() {
    return statusMessage;
}

void ThingsBoardProvider::setServer(const String& serverUrl) {
    this->server = serverUrl;
    Serial.println("[ThingsBoard] Server updated: " + serverUrl);
}

void ThingsBoardProvider::setPort(int port) {
    this->port = port;
    this->useHttps = (port == 443);
    Serial.println("[ThingsBoard] Port updated: " + String(port) + (useHttps ? " (HTTPS)" : " (HTTP)"));
}

bool ThingsBoardProvider::canSend() {
    unsigned long now = millis();
    if (now - lastSendTime < minInterval) {
        statusMessage = "Rate limited (wait " + String((minInterval - (now - lastSendTime)) / 1000) + "s)";
        return false;
    }
    return true;
}

String ThingsBoardProvider::buildJsonPayload(const SensorData& data) {
    if (data.isEmpty()) {
        return "{}";
    }
    
    // ThingsBoard Telemetry API format: {"key1": value1, "key2": value2}
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

CloudStatus ThingsBoardProvider::sendData(const SensorData& data) {
    if (!isConfigured()) {
        statusMessage = "Provider not configured";
        Serial.println("[ThingsBoard] Error: Not configured");
        return CLOUD_ERROR_AUTH;
    }
    
    if (data.isEmpty()) {
        statusMessage = "No data to send";
        Serial.println("[ThingsBoard] Error: Empty data");
        return CLOUD_ERROR_INVALID_DATA;
    }
    
    if (!canSend()) {
        Serial.println("[ThingsBoard] Rate limited, skipping send");
        return CLOUD_ERROR_TIMEOUT;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        statusMessage = "WiFi not connected";
        Serial.println("[ThingsBoard] Error: No WiFi");
        return CLOUD_ERROR_NETWORK;
    }
    
    HTTPClient http;
    
    // ThingsBoard Telemetry API endpoint
    String protocol = useHttps ? "https://" : "http://";
    String url = protocol + server + "/api/v1/" + token + "/telemetry";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String payload = buildJsonPayload(data);
    
    Serial.println("[ThingsBoard] Sending data:");
    Serial.println("  URL: " + url);
    Serial.println("  Payload: " + payload);
    
    int httpCode = http.POST(payload);
    lastHttpCode = httpCode;
    lastSendTime = millis();
    
    CloudStatus result;
    
    if (httpCode == 200) {
        statusMessage = "Success (Code: " + String(httpCode) + ")";
        Serial.println("[ThingsBoard] Upload successful: " + String(httpCode));
        result = CLOUD_SUCCESS;
    } else if (httpCode == 401 || httpCode == 403) {
        statusMessage = "Auth error (Code: " + String(httpCode) + ")";
        Serial.println("[ThingsBoard] Auth error: " + String(httpCode));
        result = CLOUD_ERROR_AUTH;
    } else if (httpCode == 400) {
        statusMessage = "Invalid data (Code: " + String(httpCode) + ")";
        Serial.println("[ThingsBoard] Invalid data: " + String(httpCode));
        result = CLOUD_ERROR_INVALID_DATA;
    } else if (httpCode >= 500) {
        statusMessage = "Server error (Code: " + String(httpCode) + ")";
        Serial.println("[ThingsBoard] Server error: " + String(httpCode));
        result = CLOUD_ERROR_SERVER;
    } else if (httpCode < 0) {
        statusMessage = "Connection failed (Code: " + String(httpCode) + ")";
        Serial.println("[ThingsBoard] Connection failed: " + String(httpCode));
        result = CLOUD_ERROR_NETWORK;
    } else {
        statusMessage = "Unknown error (Code: " + String(httpCode) + ")";
        Serial.println("[ThingsBoard] Unknown error: " + String(httpCode));
        result = CLOUD_ERROR_SERVER;
    }
    
    http.end();
    return result;
}

bool ThingsBoardProvider::testConnection() {
    if (!isConfigured()) {
        statusMessage = "Provider not configured";
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        statusMessage = "WiFi not connected";
        return false;
    }
    
    HTTPClient http;
    
    // Test dengan attributes API (lebih ringan dari telemetry)
    String protocol = useHttps ? "https://" : "http://";
    String url = protocol + server + "/api/v1/" + token + "/attributes";
    
    http.begin(url);
    http.setTimeout(5000);
    
    Serial.println("[ThingsBoard] Testing connection...");
    Serial.println("  URL: " + url);
    
    // Send empty JSON as test
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST("{}");
    
    bool success = (httpCode == 200);
    
    if (success) {
        statusMessage = "Connection OK (Code: " + String(httpCode) + ")";
        Serial.println("[ThingsBoard] Connection test passed: " + String(httpCode));
    } else {
        statusMessage = "Connection failed (Code: " + String(httpCode) + ")";
        Serial.println("[ThingsBoard] Connection test failed: " + String(httpCode));
    }
    
    http.end();
    return success;
}
