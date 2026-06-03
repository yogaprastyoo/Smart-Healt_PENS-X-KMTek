#include "CloudBroker.h"

CloudBroker::CloudBroker() {
    currentType = PROVIDER_NONE;
    statusMessage = "Not initialized";
}

CloudBroker::~CloudBroker() {
    // Cleanup if needed
}

void CloudBroker::begin(CloudProviderType type, 
                        const String& ubidotsToken, const String& ubidotsDeviceLabel,
                        const String& thingsboardToken, const String& thingsboardDeviceLabel) {
    currentType = type;
    
    Serial.println("\n[CloudBroker] Initializing...");
    Serial.println("  Provider Type: " + String(type));
    
    // Initialize providers based on type
    if (type == PROVIDER_UBIDOTS || type == PROVIDER_BOTH) {
        if (ubidotsToken.length() > 0 && ubidotsDeviceLabel.length() > 0) {
            ubidotsProvider.begin(ubidotsToken, ubidotsDeviceLabel);
            Serial.println("  Ubidots: Enabled");
        } else {
            Serial.println("  Ubidots: Disabled (missing credentials)");
        }
    }
    
    if (type == PROVIDER_THINGSBOARD || type == PROVIDER_BOTH) {
        if (thingsboardToken.length() > 0) {
            thingsboardProvider.begin(thingsboardToken, thingsboardDeviceLabel);
            Serial.println("  ThingsBoard: Enabled");
        } else {
            Serial.println("  ThingsBoard: Disabled (missing credentials)");
        }
    }
    
    if (type == PROVIDER_NONE) {
        statusMessage = "No cloud provider selected";
        Serial.println("  No provider enabled");
    } else {
        statusMessage = "CloudBroker initialized";
    }
    
    Serial.println("[CloudBroker] Initialization complete\n");
}

void CloudBroker::setProviderType(CloudProviderType type) {
    if (currentType != type) {
        Serial.println("[CloudBroker] Switching provider type: " + String(currentType) + " -> " + String(type));
        currentType = type;
    }
}

void CloudBroker::updateUbidotsCredentials(const String& token, const String& deviceLabel) {
    Serial.println("[CloudBroker] Updating Ubidots credentials");
    ubidotsProvider.updateCredentials(token, deviceLabel);
}

void CloudBroker::updateThingsBoardCredentials(const String& token, const String& deviceLabel) {
    Serial.println("[CloudBroker] Updating ThingsBoard credentials");
    thingsboardProvider.updateCredentials(token, deviceLabel);
}

std::vector<CloudProvider*> CloudBroker::getActiveProviders() {
    std::vector<CloudProvider*> providers;
    
    if (currentType == PROVIDER_UBIDOTS && ubidotsProvider.isConfigured()) {
        providers.push_back(&ubidotsProvider);
    } else if (currentType == PROVIDER_THINGSBOARD && thingsboardProvider.isConfigured()) {
        providers.push_back(&thingsboardProvider);
    } else if (currentType == PROVIDER_BOTH) {
        if (ubidotsProvider.isConfigured()) {
            providers.push_back(&ubidotsProvider);
        }
        if (thingsboardProvider.isConfigured()) {
            providers.push_back(&thingsboardProvider);
        }
    }
    
    return providers;
}

std::vector<CloudUploadResult> CloudBroker::sendData(const SensorData& data) {
    std::vector<CloudUploadResult> results;
    
    if (data.isEmpty()) {
        Serial.println("[CloudBroker] No data to send");
        statusMessage = "No data to send";
        return results;
    }
    
    auto providers = getActiveProviders();
    
    if (providers.empty()) {
        Serial.println("[CloudBroker] No active providers configured");
        statusMessage = "No active providers";
        return results;
    }
    
    Serial.println("[CloudBroker] Sending data to " + String(providers.size()) + " provider(s)");
    
    for (auto provider : providers) {
        CloudUploadResult result;
        result.providerName = provider->getProviderName();
        
        Serial.println("[CloudBroker] -> " + result.providerName);
        result.status = provider->sendData(data);
        result.message = provider->getStatusMessage();
        
        results.push_back(result);
        
        // Small delay between providers
        delay(100);
    }
    
    // Update status message based on results
    int successCount = 0;
    for (const auto& result : results) {
        if (result.status == CLOUD_SUCCESS) {
            successCount++;
        }
    }
    
    if (successCount == results.size()) {
        statusMessage = "All uploads successful";
    } else if (successCount > 0) {
        statusMessage = String(successCount) + "/" + String(results.size()) + " uploads successful";
    } else {
        statusMessage = "All uploads failed";
    }
    
    Serial.println("[CloudBroker] Upload complete: " + statusMessage);
    
    return results;
}

bool CloudBroker::testConnection() {
    auto providers = getActiveProviders();
    
    if (providers.empty()) {
        statusMessage = "No active providers";
        return false;
    }
    
    Serial.println("[CloudBroker] Testing connection for " + String(providers.size()) + " provider(s)");
    
    bool allSuccess = true;
    
    for (auto provider : providers) {
        Serial.println("[CloudBroker] Testing " + provider->getProviderName() + "...");
        bool success = provider->testConnection();
        
        if (!success) {
            allSuccess = false;
        }
        
        Serial.println("  Result: " + String(success ? "OK" : "FAILED"));
        Serial.println("  Message: " + provider->getStatusMessage());
    }
    
    if (allSuccess) {
        statusMessage = "All connections OK";
    } else {
        statusMessage = "Some connections failed";
    }
    
    return allSuccess;
}

bool CloudBroker::testUbidots() {
    if (!ubidotsProvider.isConfigured()) {
        Serial.println("[CloudBroker] Ubidots not configured");
        return false;
    }
    
    Serial.println("[CloudBroker] Testing Ubidots connection...");
    bool result = ubidotsProvider.testConnection();
    Serial.println("  Result: " + String(result ? "OK" : "FAILED"));
    
    return result;
}

bool CloudBroker::testThingsBoard() {
    if (!thingsboardProvider.isConfigured()) {
        Serial.println("[CloudBroker] ThingsBoard not configured");
        return false;
    }
    
    Serial.println("[CloudBroker] Testing ThingsBoard connection...");
    bool result = thingsboardProvider.testConnection();
    Serial.println("  Result: " + String(result ? "OK" : "FAILED"));
    
    return result;
}

String CloudBroker::getStatusMessage() {
    return statusMessage;
}

String CloudBroker::getUbidotsStatus() {
    if (!ubidotsProvider.isConfigured()) {
        return "Not configured";
    }
    return ubidotsProvider.getStatusMessage();
}

String CloudBroker::getThingsBoardStatus() {
    if (!thingsboardProvider.isConfigured()) {
        return "Not configured";
    }
    return thingsboardProvider.getStatusMessage();
}

bool CloudBroker::isReady() {
    auto providers = getActiveProviders();
    return !providers.empty();
}
