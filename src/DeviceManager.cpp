#include "DeviceManager.h"

void DeviceManager::begin() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Gagal mount SPIFFS!");
        delay(2000);
    } else {
        loadConfig();
    }
}

bool DeviceManager::fileExists(const char* path) {
    return SPIFFS.exists(path);
}

void DeviceManager::createEmptyConfig() {
    StaticJsonDocument<256> doc;
    doc.createNestedArray("devices");
    File f = SPIFFS.open(CONFIG_PATH, FILE_WRITE);
    if (!f) {
        Serial.println("Gagal membuat config.json");
        return;
    }
    serializeJson(doc, f);
    f.close();
}

void DeviceManager::loadConfig() {
    devices.clear();
    if (!fileExists(CONFIG_PATH)) {
        Serial.println("config.json tidak ada, membuat...");
        createEmptyConfig();
    }

    File f = SPIFFS.open(CONFIG_PATH, FILE_READ);
    if (!f) {
        Serial.println("Gagal buka config.json");
        return;
    }

    String content = f.readString();
    f.close();

    if (content.length() == 0) {
        createEmptyConfig();
        return;
    }

    StaticJsonDocument<4096> doc;
    auto err = deserializeJson(doc, content);
    if (err) {
        Serial.printf("Gagal parse config.json: %s\n", err.c_str());
        return;
    }

    JsonArray arr = doc["devices"].as<JsonArray>();
    for (JsonObject obj : arr) {
        Device d;
        d.name = obj["name"].as<String>();
        d.ubidotsToken = obj["ubidotsToken"].as<String>();
        d.ubidotsDeviceLabel = obj["ubidotsDeviceLabel"].as<String>();
        
        // Load ThingsBoard credentials (backward compatible)
        d.thingsboardToken = obj["thingsboardToken"] | "";
        d.thingsboardDeviceLabel = obj["thingsboardDeviceLabel"] | "";
        d.cloudProvider = obj["cloudProvider"] | 1; // Default to Ubidots for old configs
        
        devices.push_back(d);
    }
}

void DeviceManager::saveConfig() {
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.createNestedArray("devices");
    for (auto &d : devices) {
        JsonObject o = arr.createNestedObject();
        o["name"] = d.name;
        o["ubidotsToken"] = d.ubidotsToken;
        o["ubidotsDeviceLabel"] = d.ubidotsDeviceLabel;
        o["thingsboardToken"] = d.thingsboardToken;
        o["thingsboardDeviceLabel"] = d.thingsboardDeviceLabel;
        o["cloudProvider"] = d.cloudProvider;
    }

    File f = SPIFFS.open(CONFIG_PATH, FILE_WRITE);
    if (!f) {
        Serial.println("Gagal tulis config.json");
        return;
    }
    serializeJson(doc, f);
    f.close();
}

void DeviceManager::addDevice(String name, String token, String deviceLabel) {
    // Backward compatible: add device with Ubidots only
    Device d;
    d.name = name;
    d.ubidotsToken = token;
    d.ubidotsDeviceLabel = deviceLabel;
    d.thingsboardToken = "";
    d.thingsboardDeviceLabel = "";
    d.cloudProvider = 1; // Ubidots only
    
    devices.push_back(d);
    saveConfig();
}

void DeviceManager::removeDevice(int index) {
    if (index >= 0 && index < devices.size()) {
        devices.erase(devices.begin() + index);
        saveConfig();
    }
}

void DeviceManager::editDevice(int index, String newName, String newToken, String newDeviceLabel) {
    if (index >= 0 && index < devices.size()) {
        devices[index].name = newName;
        devices[index].ubidotsToken = newToken;
        devices[index].ubidotsDeviceLabel = newDeviceLabel;
        saveConfig();
        Serial.printf("User %d berhasil diupdate!\n", index);
    } else {
        Serial.println("Index tidak valid!");
    }
}

void DeviceManager::printDevices() {
    Serial.println("\n=== DAFTAR USER (Token Lengkap) ===");
    if (devices.empty()) {
        Serial.println("(kosong)");
        return;
    }
    for (size_t i = 0; i < devices.size(); i++) {
        Serial.printf("%d: %s\n", (int)i, devices[i].name.c_str());
        
        // Tampilkan token lengkap dengan format yang mudah dibaca
        String token = devices[i].ubidotsToken;
        Serial.print("   Token: ");
        
        // Tampilkan per 16 karakter per baris
        for (int j = 0; j < token.length(); j++) {
            Serial.print(token[j]);
            if ((j + 1) % 16 == 0 && j != token.length() - 1) {
                Serial.print("\n          "); // Indent untuk baris berikutnya
            }
        }
        Serial.println();
        
        Serial.printf("   Device Label: %s\n", devices[i].ubidotsDeviceLabel.c_str());
        Serial.printf("   EDA Variable: %s\n", UBIDOTS_VARIABLE_LABEL);
        Serial.println("   " + String(40, '-'));
    }
}

bool DeviceManager::isEmpty() {
    return devices.empty();
}
// New method with full multi-cloud support
void DeviceManager::addDeviceWithCloud(String name, 
                                      String ubidotsToken, String ubidotsLabel,
                                      String thingsboardToken, String thingsboardLabel,
                                      int cloudProvider) {
    Device d;
    d.name = name;
    d.ubidotsToken = ubidotsToken;
    d.ubidotsDeviceLabel = ubidotsLabel;
    d.thingsboardToken = thingsboardToken;
    d.thingsboardDeviceLabel = thingsboardLabel;
    d.cloudProvider = cloudProvider;
    
    devices.push_back(d);
    saveConfig();
    
    Serial.println("[DeviceManager] Device added with multi-cloud support:");
    Serial.println("  Name: " + name);
    Serial.println("  Cloud Provider: " + String(cloudProvider));
}

// New edit method with full multi-cloud support
void DeviceManager::editDeviceWithCloud(int index, String newName,
                                       String ubidotsToken, String ubidotsLabel,
                                       String thingsboardToken, String thingsboardLabel,
                                       int cloudProvider) {
    if (index >= 0 && index < devices.size()) {
        devices[index].name = newName;
        devices[index].ubidotsToken = ubidotsToken;
        devices[index].ubidotsDeviceLabel = ubidotsLabel;
        devices[index].thingsboardToken = thingsboardToken;
        devices[index].thingsboardDeviceLabel = thingsboardLabel;
        devices[index].cloudProvider = cloudProvider;
        
        saveConfig();
        
        Serial.printf("[DeviceManager] User %d updated with multi-cloud support!\n", index);
        Serial.println("  Name: " + newName);
        Serial.println("  Cloud Provider: " + String(cloudProvider));
    } else {
        Serial.println("[DeviceManager] Index tidak valid!");
    }
}
