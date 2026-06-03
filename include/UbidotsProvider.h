#ifndef UBIDOTS_PROVIDER_H
#define UBIDOTS_PROVIDER_H

#include "CloudProvider.h"
#include <HTTPClient.h>
#include <WiFi.h>

class UbidotsProvider : public CloudProvider {
public:
    UbidotsProvider();
    ~UbidotsProvider();
    
    // Implement CloudProvider interface
    bool begin(const String& token, const String& deviceLabel) override;
    CloudStatus sendData(const SensorData& data) override;
    bool testConnection() override;
    String getStatusMessage() override;
    String getProviderName() override;
    void updateCredentials(const String& token, const String& deviceLabel) override;
    bool isConfigured() override;
    
private:
    String server = "industrial.api.ubidots.com";
    String statusMessage = "";
    int lastHttpCode = 0;
    unsigned long lastSendTime = 0;
    const unsigned long minInterval = 15000; // 15 seconds
    
    // Helper functions
    String buildJsonPayload(const SensorData& data);
    bool canSend();
};

#endif
