#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "SPIFFS.h"
#include "Config.h"

struct Device {
  String name;
  String ubidotsToken;           // Token untuk Ubidots
  String ubidotsDeviceLabel;     // Device label untuk Ubidots
  String thingsboardToken;       // Token untuk ThingsBoard
  String thingsboardDeviceLabel; // Device label untuk ThingsBoard (optional)
  int cloudProvider;             // 0=None, 1=Ubidots, 2=ThingsBoard, 3=Both
};

class DeviceManager {
public:
    std::vector<Device> devices;

    void begin();
    void loadConfig();
    void saveConfig();
    
    // Legacy method (backward compatible - Ubidots only)
    void addDevice(String name, String token, String deviceLabel);
    
    // New method with cloud provider support
    void addDeviceWithCloud(String name, 
                           String ubidotsToken, String ubidotsLabel,
                           String thingsboardToken, String thingsboardLabel,
                           int cloudProvider);
    
    void removeDevice(int index);
    
    // Legacy edit (backward compatible)
    void editDevice(int index, String newName, String newToken, String newDeviceLabel);
    
    // New edit with cloud provider
    void editDeviceWithCloud(int index, String newName,
                            String ubidotsToken, String ubidotsLabel,
                            String thingsboardToken, String thingsboardLabel,
                            int cloudProvider);
    
    void printDevices();
    bool isEmpty();

private:
    bool fileExists(const char* path);
    void createEmptyConfig();
};

#endif