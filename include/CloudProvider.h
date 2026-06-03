#ifndef CLOUD_PROVIDER_H
#define CLOUD_PROVIDER_H

#include <Arduino.h>
#include <map>

// Enum untuk status upload
enum CloudStatus {
    CLOUD_SUCCESS = 0,
    CLOUD_ERROR_NETWORK = 1,
    CLOUD_ERROR_AUTH = 2,
    CLOUD_ERROR_TIMEOUT = 3,
    CLOUD_ERROR_INVALID_DATA = 4,
    CLOUD_ERROR_SERVER = 5
};

// Enum untuk tipe cloud provider
enum CloudProviderType {
    PROVIDER_NONE = 0,
    PROVIDER_UBIDOTS = 1,
    PROVIDER_THINGSBOARD = 2,
    PROVIDER_BOTH = 3  // Kirim ke kedua platform
};

// Struct untuk data sensor yang akan dikirim
struct SensorData {
    std::map<String, float> values;  // Key: variable name, Value: sensor value
    unsigned long timestamp;          // Unix timestamp (opsional)
    
    void addValue(const String& key, float value) {
        values[key] = value;
    }
    
    void clear() {
        values.clear();
        timestamp = 0;
    }
    
    bool isEmpty() const {
        return values.empty();
    }
};

// Interface abstrak untuk semua cloud provider
class CloudProvider {
public:
    virtual ~CloudProvider() {}
    
    // Inisialisasi provider (setup credentials, connection, dll)
    virtual bool begin(const String& token, const String& deviceLabel) = 0;
    
    // Kirim data sensor ke cloud
    virtual CloudStatus sendData(const SensorData& data) = 0;
    
    // Test koneksi ke cloud platform
    virtual bool testConnection() = 0;
    
    // Get status info (untuk display)
    virtual String getStatusMessage() = 0;
    
    // Get provider name
    virtual String getProviderName() = 0;
    
    // Update credentials
    virtual void updateCredentials(const String& token, const String& deviceLabel) = 0;
    
    // Check if provider is properly configured
    virtual bool isConfigured() = 0;
    
protected:
    String token;
    String deviceLabel;
    bool configured = false;
};

#endif
