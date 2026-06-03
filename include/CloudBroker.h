#ifndef CLOUD_BROKER_H
#define CLOUD_BROKER_H

#include "CloudProvider.h"
#include "UbidotsProvider.h"
#include "ThingsBoardProvider.h"
#include <vector>

// Struct untuk menyimpan hasil upload per provider
struct CloudUploadResult {
    String providerName;
    CloudStatus status;
    String message;
};

/**
 * CloudBroker - Manager untuk multi-cloud provider
 * Mengelola pengiriman data ke multiple cloud platforms secara bersamaan
 */
class CloudBroker {
public:
    CloudBroker();
    ~CloudBroker();
    
    // Setup broker dengan provider type
    void begin(CloudProviderType type, 
               const String& ubidotsToken, const String& ubidotsDeviceLabel,
               const String& thingsboardToken, const String& thingsboardDeviceLabel = "");
    
    // Update provider type (switch between providers)
    void setProviderType(CloudProviderType type);
    CloudProviderType getProviderType() { return currentType; }
    
    // Update credentials untuk specific provider
    void updateUbidotsCredentials(const String& token, const String& deviceLabel);
    void updateThingsBoardCredentials(const String& token, const String& deviceLabel);
    
    // Kirim data ke cloud (otomatis ke provider yang aktif)
    std::vector<CloudUploadResult> sendData(const SensorData& data);
    
    // Test connection untuk provider yang aktif
    bool testConnection();
    bool testUbidots();
    bool testThingsBoard();
    
    // Get status info
    String getStatusMessage();
    String getUbidotsStatus();
    String getThingsBoardStatus();
    
    // Check if ready to send
    bool isReady();
    
    // Get provider instances (untuk advanced usage)
    UbidotsProvider* getUbidotsProvider() { return &ubidotsProvider; }
    ThingsBoardProvider* getThingsBoardProvider() { return &thingsboardProvider; }
    
private:
    CloudProviderType currentType;
    UbidotsProvider ubidotsProvider;
    ThingsBoardProvider thingsboardProvider;
    
    String statusMessage;
    
    // Helper untuk get active providers
    std::vector<CloudProvider*> getActiveProviders();
};

#endif
