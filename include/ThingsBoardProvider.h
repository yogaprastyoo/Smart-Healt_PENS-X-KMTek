#ifndef THINGSBOARD_PROVIDER_H
#define THINGSBOARD_PROVIDER_H

#include "CloudProvider.h"
#include <HTTPClient.h>
#include <WiFi.h>

class ThingsBoardProvider : public CloudProvider {
public:
    ThingsBoardProvider();
    ~ThingsBoardProvider();
    
    // Implement CloudProvider interface
    bool begin(const String& token, const String& deviceLabel) override;
    CloudStatus sendData(const SensorData& data) override;
    bool testConnection() override;
    String getStatusMessage() override;
    String getProviderName() override;
    void updateCredentials(const String& token, const String& deviceLabel) override;
    bool isConfigured() override;
    
    // ThingsBoard specific configurations
    void setServer(const String& serverUrl);
    void setPort(int port);
    
private:
    String server = "demo.thingsboard.io";  // Default demo server
    int port = 80;  // HTTP port (443 for HTTPS)
    bool useHttps = false;
    String statusMessage = "";
    int lastHttpCode = 0;
    unsigned long lastSendTime = 0;
    const unsigned long minInterval = 15000; // 15 seconds
    
    // Helper functions
    String buildJsonPayload(const SensorData& data);
    bool canSend();
};

#endif
