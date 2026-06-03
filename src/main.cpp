#include <Arduino.h>
#include <HTTPClient.h>
#include <vector>
#include "Config.h"
#include "Utils.h"
#include "DeviceManager.h"
#include "WifiManager.h"
#include "MedicalRecordSensor.h"
#include "ECGSensor.h"
#include "BloodPressureSensor.h"
#include "DisplayManager.h"
#include "BodyPositionSnoreSensor.h"
#include "EMGSensor.h"
#include "CloudBroker.h"

// --- Objects ---
DeviceManager deviceMgr;
WifiManager wifiMgr;
MedicalRecordSensor medSensor;
ECGSensor ecgSensor;
BloodPressureSensor bpSensor;
DisplayManager display;
BodyPositionSnoreSensor bodyPositionSnoreSensor;
EMGSensor emgSensor;
CloudBroker cloudBroker;

String tempUserName;
String tempUserToken;
String tempUserDeviceLabel;

// --- Global State ---
int selectedIndex = 0;
bool measuring = false;
bool inHomeScreen = true;
bool inMeasurementScreen = false;
bool inUserScreen = false;
bool inWifiScreen = false;
bool inEditUserScreen = false;
bool inKeyboardScreen = false;
bool inUserListScreen = false;
bool inModeSelectionScreen = false;
bool inWifiScanScreen = false;
unsigned long lastUbidotsMillis = 0;
unsigned long lastEDAPrint = 0;

// Mode pengukuran
int selectedMeasurementMode = 0; // 0-4 sesuai dengan 5 mode

// WiFi scan state
int selectedWifiIndex = -1;
String selectedWifiSSID = "";

// Variabel untuk edit via keyboard
String editingField = ""; // "name", "token", "deviceLabel", "wifi_password"
int editingUserIndex = 0;
bool addingNewUser = false;

// Variabel untuk data kesehatan
int lastHeartRate = 0;
int lastSpO2 = 0;
float lastObjectTemp = 0.0;

// Variabel untuk data ECG
int lastECGRaw = 0;
float lastECGVoltage = 0.0;
int lastECGHeartRate = 0;
String lastECGLeadStatus = "Leads OK";

// Variabel untuk data Blood Pressure
int lastSystolic = 0;
int lastDiastolic = 0;
int lastBPHeartRate = 0;

// Variabel untuk data Body Position & Snore
int lastBodyPosition = 0;
int lastSnoreAmplitude = 0;

// Variabel untuk data EMG
float lastEMGAmplitude_mV = 0.0f;    // Amplitude dalam mV
int lastEMGActivation_percent = 0;   // Activation dalam %
String lastEMGLevel = "Relaxed";

// Buffer data EMG untuk grafik sederhana (seperti ECG)
int emgSimpleBuffer[400]; // Buffer untuk 400 titik data EMG
int emgSimpleIndex = 0;

// --- Auto-sleep timeout untuk MAX30100 ---
static unsigned long lastActivityTime = 0;
static const unsigned long AUTO_SLEEP_TIMEOUT = 300000; // 5 menit

// --- Deklarasi Fungsi ---
void sendToUbidots(int edaValue, const String &token, const String &deviceLabel);
void sendMedicalRecordToCloud(int edaValue, int heartRate, int spo2, float objectTemp);
void sendECGToCloud(int ecgHeartRate);
void sendBloodPressureToCloud(int systolic, int diastolic, int heartRate);
void sendBodyPositionSnoreToCloud(int position, int snoreAmplitude);
void sendEMGToCloud(float emgAmplitude_mV);
void goToHomeScreen();
void goToMeasurementScreen();
void showCurrentUser();
void goToEditUserScreen();
void goToKeyboardScreen(String currentValue, String title, bool forName);
void showWifiConfig();
void refreshCurrentScreen();
void showUserListScreen();
void showWifiScanScreen();
void handleSerialCommands();
void showTemporaryMessage(String title, String message, uint16_t titleColor = TFT_YELLOW, uint16_t textColor = TFT_WHITE, int delayTime = 3000);

// Fungsi untuk mode selection
void showMeasurementModeSelection();
void goToMeasurementScreenWithMode(int modeIndex);

// Fungsi untuk MAX30100 dan MLX90614 sleep/wake management
void wakeUpMAX30100IfNeeded();
void sleepMAX30100IfIdle();
void resetActivityTimer();
void checkAutoSleep();
void sleepMAX30100IfLeavingMedicalRecord();

// FUNGSI BARU: Untuk memastikan I2C speed benar sebelum membaca MLX90614
void ensureI2CForMLX90614();
void initI2CForSensors();

// Fungsi wrapper untuk update display ECG
void updateECGDisplay(int ecgRaw, float ecgVoltage, int heartRate, String status, bool isSending);

// Fungsi wrapper untuk update display EMG dengan grafik sederhana
void updateEMGDisplay(float amplitude_mV, int activation_percent, String level, bool isSending);

// --- Fungsi Helper ---
void sendToUbidots(int edaValue, const String &token, const String &deviceLabel) {
    if (edaValue <= 0) {
        return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        display.updateStatus("WiFi Putus! Data tidak dikirim.");
        return;
    }
    
    if (millis() - lastUbidotsMillis > UBIDOTS_MIN_INTERVAL_MS) {
        display.updateSensorData(edaValue, medSensor.getStressLevel(edaValue), true);

        HTTPClient http;
        String url = "https://" + String(UBIDOTS_SERVER) + "/api/v1.6/devices/" + deviceLabel;
        
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-Auth-Token", token);
        
        String payload = "{\"" + String(UBIDOTS_VARIABLE_LABEL) + "\": " + String(edaValue) + "}";
        
        int code = http.POST(payload);
        
        if (code == 200 || code == 201) {
            display.updateStatus("Upload ke Ubidots OK (Code: " + String(code) + ")");
            Serial.println("Data EDA berhasil dikirim ke Ubidots");
        } else {
            display.updateStatus("Upload ke Ubidots Gagal! Code: " + String(code));
            Serial.printf("Error mengirim EDA ke Ubidots: %d\n", code);
        }
        
        http.end();
        lastUbidotsMillis = millis();

        delay(100);
        display.updateSensorData(edaValue, medSensor.getStressLevel(edaValue), false);
    }
}

void sendMedicalRecordToCloud(int edaValue, int heartRate, int spo2, float objectTemp) {
    if (!cloudBroker.isReady()) {
        Serial.println("[Cloud] CloudBroker not ready");
        display.updateStatus("Cloud not configured");
        return;
    }
    
    int validDataCount = 0;
    if (edaValue > 0) validDataCount++;
    if (heartRate > 30 && heartRate < 250) validDataCount++;
    if (spo2 > 70 && spo2 <= 100) validDataCount++;
    if (objectTemp > 25.0 && objectTemp < 45.0) validDataCount++;
    
    if (validDataCount < 2) {
        Serial.println("[Cloud] Medical Record: Data tidak lengkap, tidak dikirim");
        return;
    }
    
    if (millis() - lastUbidotsMillis < UBIDOTS_MIN_INTERVAL_MS) {
        return; // Rate limited
    }
    
    String stressLevel = medSensor.getStressLevel(edaValue);
    display.updateMedicalRecordData(edaValue, stressLevel, heartRate, spo2, objectTemp, true);
    
    SensorData data;
    if (edaValue > 0) data.addValue(UBIDOTS_VARIABLE_LABEL, edaValue);
    if (heartRate > 30 && heartRate < 250) data.addValue(UBIDOTS_HR_VARIABLE, heartRate);
    if (spo2 > 70 && spo2 <= 100) data.addValue(UBIDOTS_SPO2_VARIABLE, spo2);
    if (objectTemp > 25.0 && objectTemp < 45.0) data.addValue(UBIDOTS_TEMP_VARIABLE, objectTemp);
    
    auto results = cloudBroker.sendData(data);
    
    bool anySuccess = false;
    for (const auto& result : results) {
        if (result.status == CLOUD_SUCCESS) {
            anySuccess = true;
            Serial.println("[Cloud] " + result.providerName + ": Upload successful");
        } else {
            Serial.println("[Cloud] " + result.providerName + ": Upload failed - " + result.message);
        }
    }
    
    if (anySuccess) {
        display.updateStatus("Medical Record uploaded");
        lastUbidotsMillis = millis();
    } else {
        display.updateStatus("Upload failed");
    }
    
    delay(100);
    display.updateMedicalRecordData(edaValue, stressLevel, heartRate, spo2, objectTemp, false);
}

void sendECGToCloud(int ecgHeartRate) {
    if (!cloudBroker.isReady()) {
        Serial.println("[Cloud] CloudBroker not ready");
        return;
    }
    
    if (ecgHeartRate < 30 || ecgHeartRate > 250) {
        Serial.println("[Cloud] ECG: Invalid heart rate");
        return;
    }
    
    if (millis() - lastUbidotsMillis < UBIDOTS_MIN_INTERVAL_MS) {
        return; // Rate limited
    }
    
    display.updateStatus("Mengirim data ECG...");
    
    SensorData data;
    data.addValue("ecg_heart_rate", ecgHeartRate);
    
    auto results = cloudBroker.sendData(data);
    
    bool anySuccess = false;
    for (const auto& result : results) {
        if (result.status == CLOUD_SUCCESS) {
            anySuccess = true;
            Serial.println("[Cloud] " + result.providerName + ": ECG uploaded");
        } else {
            Serial.println("[Cloud] " + result.providerName + ": ECG failed - " + result.message);
        }
    }
    
    if (anySuccess) {
        display.updateStatus("ECG HR uploaded");
        lastUbidotsMillis = millis();
    } else {
        display.updateStatus("ECG upload failed");
    }
    
    delay(100);
}

void sendBloodPressureToCloud(int systolic, int diastolic, int heartRate) {
    if (!cloudBroker.isReady()) {
        Serial.println("[Cloud] CloudBroker not ready");
        return;
    }
    
    if (systolic <= 0 || diastolic <= 0 || systolic < diastolic) {
        Serial.println("[Cloud] BP: Invalid data");
        return;
    }
    
    if (millis() - lastUbidotsMillis < UBIDOTS_MIN_INTERVAL_MS) {
        return; // Rate limited
    }
    
    display.updateBloodPressureData(systolic, diastolic, heartRate, true);
    
    SensorData data;
    data.addValue(UBIDOTS_BP_SYSTOLIC_VARIABLE, systolic);
    data.addValue(UBIDOTS_BP_DIASTOLIC_VARIABLE, diastolic);
    data.addValue(UBIDOTS_BP_HR_VARIABLE, heartRate);
    
    auto results = cloudBroker.sendData(data);
    
    bool anySuccess = false;
    for (const auto& result : results) {
        if (result.status == CLOUD_SUCCESS) {
            anySuccess = true;
            Serial.println("[Cloud] " + result.providerName + ": BP uploaded");
        } else {
            Serial.println("[Cloud] " + result.providerName + ": BP failed - " + result.message);
        }
    }
    
    if (anySuccess) {
        display.updateStatus("Blood Pressure uploaded");
        lastUbidotsMillis = millis();
    } else {
        display.updateStatus("BP upload failed");
    }
    
    delay(100);
    display.updateBloodPressureData(systolic, diastolic, heartRate, false);
}

void sendBodyPositionSnoreToCloud(int position, int snoreAmplitude) {
    if (!cloudBroker.isReady()) {
        Serial.println("[Cloud] CloudBroker not ready");
        return;
    }
    
    if (position == 0 && snoreAmplitude == 0) {
        Serial.println("[Cloud] Body Position: No data");
        return;
    }
    
    if (millis() - lastUbidotsMillis < UBIDOTS_MIN_INTERVAL_MS) {
        return; // Rate limited
    }
    
    display.updateStatus("Mengirim data Body Position & Snore...");
    
    SensorData data;
    data.addValue(UBIDOTS_POSITION_VARIABLE, position);
    data.addValue(UBIDOTS_SNORE_VARIABLE, snoreAmplitude);
    
    auto results = cloudBroker.sendData(data);
    
    bool anySuccess = false;
    for (const auto& result : results) {
        if (result.status == CLOUD_SUCCESS) {
            anySuccess = true;
            Serial.println("[Cloud] " + result.providerName + ": Body Position uploaded");
        } else {
            Serial.println("[Cloud] " + result.providerName + ": Body Position failed - " + result.message);
        }
    }
    
    if (anySuccess) {
        display.updateStatus("Body Position & Snore uploaded");
        lastUbidotsMillis = millis();
    } else {
        display.updateStatus("Body Position upload failed");
    }
}

void sendEMGToCloud(float emgAmplitude_mV) {
    if (!cloudBroker.isReady()) {
        Serial.println("[Cloud] CloudBroker not ready");
        return;
    }
    
    if (emgAmplitude_mV < 10.0f) {
        Serial.println("[Cloud] EMG: Amplitude too low");
        return;
    }
    
    if (millis() - lastUbidotsMillis < UBIDOTS_MIN_INTERVAL_MS) {
        return; // Rate limited
    }
    
    display.updateStatus("Mengirim data EMG...");
    
    SensorData data;
    data.addValue(UBIDOTS_EMG_VARIABLE, emgAmplitude_mV);
    
    auto results = cloudBroker.sendData(data);
    
    bool anySuccess = false;
    for (const auto& result : results) {
        if (result.status == CLOUD_SUCCESS) {
            anySuccess = true;
            Serial.printf("[Cloud] %s: EMG (%.2f mV) uploaded\n", result.providerName.c_str(), emgAmplitude_mV);
        } else {
            Serial.println("[Cloud] " + result.providerName + ": EMG failed - " + result.message);
        }
    }
    
    if (anySuccess) {
        display.updateStatus("EMG Amplitude uploaded");
        lastUbidotsMillis = millis();
    } else {
        display.updateStatus("EMG upload failed");
    }
}

void showTemporaryMessage(String title, String message, uint16_t titleColor, uint16_t textColor, int delayTime) {
    display.clearContentArea();
    
    display.drawTextCentered(title, 240, 80, titleColor, TFT_BLACK, 2);
    
    int yPos = 120;
    int startPos = 0;
    int newlinePos;
    
    do {
        newlinePos = message.indexOf('\n', startPos);
        String line;
        if (newlinePos == -1) {
            line = message.substring(startPos);
        } else {
            line = message.substring(startPos, newlinePos);
            startPos = newlinePos + 1;
        }
        
        if (line.length() > 40) {
            line = "..." + line.substring(line.length() - 37);
        }
            
        display.drawTextCentered(line, 240, yPos, textColor, TFT_BLACK, 1);
        yPos += 20;
    } while (newlinePos != -1 && yPos < 240);
    
    display.drawTextCentered("Kembali otomatis...", 240, 270, TFT_CYAN, TFT_BLACK, 1);
    
    display.updateStatus(title);
    
    delay(delayTime);
}

// --- FUNGSI I2C MANAGEMENT ---
void ensureI2CForMLX90614() {
    Wire.setClock(100000);
    delay(5);
}

void initI2CForSensors() {
    Serial.println("Initializing I2C buses for sensors...");
    
    Wire.begin(21, 22);
    Wire.setClock(100000);
    delay(100);
    
    Serial.println("=== Scanning I2C Bus 1 (Wire: 21/22) ===");
    byte error, address;
    int nDevices1 = 0;
    
    for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.print("  I2C device found at 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            
            if (address == 0x57) Serial.println(" -> MAX30100");
            else if (address == 0x5A) Serial.println(" -> MLX90614");
            else if (address == 0x38) Serial.println(" -> FT6206 Touch");
            else if (address == 0x68) Serial.println(" -> MPU6050");
            else Serial.println(" -> Unknown");
            
            nDevices1++;
        }
    }
    
    if (nDevices1 == 0) {
        Serial.println("  No I2C devices found on Bus 1!");
    } else {
        Serial.printf("  Found %d device(s) on Bus 1\n", nDevices1);
    }
    
    Wire1.begin(25, 26);
    Wire1.setClock(100000);
    delay(100);
    
    Serial.println("=== Scanning I2C Bus 2 (Wire1: 25/26) ===");
    int nDevices2 = 0;
    
    for(address = 1; address < 127; address++) {
        Wire1.beginTransmission(address);
        error = Wire1.endTransmission();
        
        if (error == 0) {
            Serial.print("  I2C device found at 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            
            if (address == 0x68) Serial.println(" -> MPU6050");
            else if (address == 0x69) Serial.println(" -> MPU6050 (Alternate)");
            else Serial.println(" -> Unknown");
            
            nDevices2++;
        }
    }
    
    if (nDevices2 == 0) {
        Serial.println("  No I2C devices found on Bus 2!");
        Serial.println("  Check MPU6050 connection on Pin 25 (SDA) and 26 (SCL)");
    } else {
        Serial.printf("  Found %d device(s) on Bus 2\n", nDevices2);
    }
    
    Serial.println("=== I2C Initialization Complete ===");
}

// --- FUNGSI MAX30100 & MLX90614 Sleep/Wake Management ---
void wakeUpMAX30100IfNeeded() {
    if (!medSensor.isMAX30100Available()) return;
    
    if (medSensor.isMAX30100Sleeping()) {
        Serial.println("MAX30100: Waking up sensor for Medical Record...");
        display.updateStatus("Membangunkan sensor HR/SpO2...");
        medSensor.wakeUpMAX30100();
        delay(500);
        Serial.println("MAX30100: Sensor siap untuk Medical Record!");
        display.updateStatus("Sensor HR/SpO2 siap");
    }
    
    if (medSensor.isMLX90614Available() && medSensor.isMLX90614Sleeping()) {
        Serial.println("MLX90614: Waking up temperature sensor...");
        medSensor.wakeUpMLX90614();
        delay(200);
        Serial.println("MLX90614: Temperature sensor ready");
    }
}

void sleepMAX30100IfIdle() {
    if (!medSensor.isMAX30100Available()) return;
    
    if (selectedMeasurementMode != 0 && !measuring && !medSensor.isMAX30100Sleeping()) {
        Serial.println("MAX30100: Sleeping sensor (idle)...");
        medSensor.sleepMAX30100();
    }
}

void resetActivityTimer() {
    lastActivityTime = millis();
}

void checkAutoSleep() {
    if (!medSensor.isMAX30100Available()) return;
    
    if (!medSensor.isMAX30100Sleeping() && 
        (millis() - lastActivityTime > AUTO_SLEEP_TIMEOUT)) {
        
        if (!measuring && selectedMeasurementMode != 0) {
            Serial.println("MAX30100: Auto-sleep karena idle lama");
            medSensor.sleepMAX30100();
        }
    }
}

void sleepMAX30100IfLeavingMedicalRecord() {
    if (!medSensor.isMAX30100Available()) return;
    
    if (!medSensor.isMAX30100Sleeping() && selectedMeasurementMode == 0) {
        medSensor.sleepMAX30100();
        Serial.println("MAX30100: Sleep karena keluar dari Medical Record");
        display.updateStatus("Sensor HR/SpO2 sleep");
    }
    
    if (medSensor.isMLX90614Available() && !medSensor.isMLX90614Sleeping() && selectedMeasurementMode == 0) {
        medSensor.sleepMLX90614();
        Serial.println("MLX90614: Sleep karena keluar dari Medical Record");
    }
}

// Fungsi wrapper untuk update display ECG
void updateECGDisplay(int ecgRaw, float ecgVoltage, int heartRate, String status, bool isSending) {
    int* ecgBuffer = ecgSensor.getECGBuffer();
    int bufferSize = ecgSensor.getECGBufferSize();
    int threshold = ecgSensor.getUpperThreshold();
    
    display.updateECGData(ecgRaw, ecgVoltage, heartRate, status, 
                         isSending, ecgBuffer, bufferSize, threshold);
}

// Fungsi wrapper untuk update display EMG dengan grafik sederhana
void updateEMGDisplay(float amplitude_mV, int activation_percent, String level, bool isSending) {
    display.updateEMGData(amplitude_mV, activation_percent, level, isSending, 
                         emgSimpleBuffer, min(emgSimpleIndex, 400));
}

// --- Fungsi untuk Navigation ---
void goToHomeScreen() {
    inHomeScreen = true;
    inMeasurementScreen = false;
    inUserScreen = false;
    inWifiScreen = false;
    inEditUserScreen = false;
    inKeyboardScreen = false;
    inUserListScreen = false;
    inModeSelectionScreen = false;
    inWifiScanScreen = false;
    measuring = false;
    
    if (selectedMeasurementMode == 0 && medSensor.isMAX30100Available()) {
        sleepMAX30100IfLeavingMedicalRecord();
    }
    
    if (selectedMeasurementMode == 1) {
        bpSensor.stopMeasurement();
    }
    
    if (selectedMeasurementMode == 3) {
        emgSimpleIndex = 0;
    }
    
    String currentUser = deviceMgr.isEmpty() ? "BELUM ADA USER" : deviceMgr.devices[selectedIndex].name;
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    
    display.drawHomeScreen(currentUser, wifiConnected);
    display.updateStatus("Menu Utama - Pilih opsi");
    Serial.println("Navigasi ke Home Screen");
    
    resetActivityTimer();
}

void goToMeasurementScreen() {
    inHomeScreen = false;
    inMeasurementScreen = true;
    inUserScreen = false;
    inWifiScreen = false;
    inEditUserScreen = false;
    inKeyboardScreen = false;
    inUserListScreen = false;
    inModeSelectionScreen = false;
    inWifiScanScreen = false;
    
    if (selectedMeasurementMode == 0 && medSensor.isMAX30100Available()) {
        wakeUpMAX30100IfNeeded();
    }
    
    String currentUser = deviceMgr.isEmpty() ? "BELUM ADA USER" : deviceMgr.devices[selectedIndex].name;
    String modeName = display.getMeasurementModeName(selectedMeasurementMode);
    
    display.setMeasurementMode(selectedMeasurementMode);
    
    if (selectedMeasurementMode == 0) {
        display.drawMedicalRecordInterface(currentUser);
    } else if (selectedMeasurementMode == 2) {
        display.drawECGInterface(currentUser);
    } else if (selectedMeasurementMode == 3) {
        display.drawEMGInterface(currentUser);
    } else if (selectedMeasurementMode == 4) {
        display.drawBodyPositionSnoreInterface(currentUser);
    } else {
        display.drawInterface(currentUser);
    }
    
    display.updateStatus("Mode: " + modeName + " - Siap untuk pengukuran");
    Serial.println("Navigasi ke Measurement Screen dengan mode: " + modeName);
    
    resetActivityTimer();
}

void goToMeasurementScreenWithMode(int modeIndex) {
    if (selectedMeasurementMode == 1 && modeIndex != 1) {
        bpSensor.stopMeasurement();
        measuring = false;
    }
    
    if (selectedMeasurementMode == 0 && modeIndex != 0 && medSensor.isMAX30100Available()) {
        sleepMAX30100IfLeavingMedicalRecord();
    }
    
    if (selectedMeasurementMode == 3 && modeIndex != 3) {
        emgSimpleIndex = 0;
        Serial.println("Buffer EMG direset karena keluar dari mode EMG");
    }
    
    selectedMeasurementMode = modeIndex;
    display.setMeasurementMode(selectedMeasurementMode);
    inModeSelectionScreen = false;
    inMeasurementScreen = true;
    
    if (selectedMeasurementMode == 0 && medSensor.isMAX30100Available()) {
        wakeUpMAX30100IfNeeded();
    }
    
    String currentUser = deviceMgr.isEmpty() ? "BELUM ADA USER" : deviceMgr.devices[selectedIndex].name;
    String modeName = display.getMeasurementModeName(selectedMeasurementMode);
    
    display.clearContentArea();
    
    if (selectedMeasurementMode == 0) {
        display.drawMedicalRecordInterface(currentUser);
    } else if (selectedMeasurementMode == 1) {
        bpSensor.startMeasurement();
        measuring = true;
        display.drawBloodPressureInterface(currentUser);
        display.updateStatus("Mode: Blood Pressure - Siap menerima data dari alat");
        Serial.println("Blood Pressure mode: siap menerima data dari alat");
    } else if (selectedMeasurementMode == 2) {
        display.drawECGInterface(currentUser);
    } else if (selectedMeasurementMode == 3) {
        display.drawEMGInterface(currentUser);
        display.updateStatus("Mode: EMG - Kalibrasi sensor...");
        emgSensor.resetCalibration();
        emgSimpleIndex = 0;
        Serial.println("EMG mode: memulai kalibrasi dan reset buffer");
    } else if (selectedMeasurementMode == 4) {
        display.drawBodyPositionSnoreInterface(currentUser);
        display.updateStatus("Mode: Body Position & Snore - Siap untuk pengukuran");
        Serial.println("Body Position & Snore mode: siap untuk pengukuran");
    } else {
        display.drawInterface(currentUser);
    }
    
    Serial.println("Mode dipilih: " + modeName);
    
    resetActivityTimer();
}

void showCurrentUser() {
    inHomeScreen = false;
    inMeasurementScreen = false;
    inUserScreen = true;
    inWifiScreen = false;
    inEditUserScreen = false;
    inKeyboardScreen = false;
    inUserListScreen = false;
    inModeSelectionScreen = false;
    inWifiScanScreen = false;
    
    String userName = deviceMgr.isEmpty() ? "BELUM ADA USER" : deviceMgr.devices[selectedIndex].name;
    String ubidotsToken = deviceMgr.isEmpty() ? "" : deviceMgr.devices[selectedIndex].ubidotsToken;
    String deviceLabel = deviceMgr.isEmpty() ? "" : deviceMgr.devices[selectedIndex].ubidotsDeviceLabel;
    int totalUsers = deviceMgr.devices.size();
    
    display.showUserInfo(userName, ubidotsToken, deviceLabel, selectedIndex, totalUsers);
    display.updateStatus("Info User - Sentuh untuk kembali");
    Serial.println("Menampilkan info user");
    
    resetActivityTimer();
}

void showMeasurementModeSelection() {
    inHomeScreen = false;
    inMeasurementScreen = false;
    inUserScreen = false;
    inWifiScreen = false;
    inEditUserScreen = false;
    inKeyboardScreen = false;
    inUserListScreen = false;
    inModeSelectionScreen = true;
    inWifiScanScreen = false;
    
    display.showMeasurementModeSelection(selectedMeasurementMode);
    display.updateStatus("Pilih mode pengukuran");
    Serial.println("Menampilkan pilihan mode pengukuran");
    
    resetActivityTimer();
}

void goToEditUserScreen() {
    inHomeScreen = false;
    inMeasurementScreen = false;
    inUserScreen = false;
    inWifiScreen = false;
    inEditUserScreen = true;
    inKeyboardScreen = false;
    inUserListScreen = false;
    inModeSelectionScreen = false;
    inWifiScanScreen = false;
    
    if (!deviceMgr.isEmpty()) {
        String userName = deviceMgr.devices[selectedIndex].name;
        String ubidotsToken = deviceMgr.devices[selectedIndex].ubidotsToken;
        String deviceLabel = deviceMgr.devices[selectedIndex].ubidotsDeviceLabel;
        
        display.showEditUser(selectedIndex, userName, ubidotsToken, deviceLabel);
        display.updateStatus("Pilih field untuk diedit");
        Serial.println("Menampilkan edit user screen");
    } else {
        goToHomeScreen();
    }
    
    resetActivityTimer();
}

void goToKeyboardScreen(String currentValue, String title, bool forName) {
    inHomeScreen = false;
    inMeasurementScreen = false;
    inUserScreen = false;
    inWifiScreen = false;
    inEditUserScreen = false;
    inKeyboardScreen = true;
    inUserListScreen = false;
    inModeSelectionScreen = false;
    inWifiScanScreen = false;

    display.setKeyboardMode(forName);
    
    display.showKeyboard(currentValue, title, forName);
    Serial.println("Menampilkan keyboard virtual");
    
    resetActivityTimer();
}

void showWifiConfig() {
    inHomeScreen = false;
    inMeasurementScreen = false;
    inUserScreen = false;
    inWifiScreen = true;
    inEditUserScreen = false;
    inKeyboardScreen = false;
    inUserListScreen = false;
    inModeSelectionScreen = false;
    inWifiScanScreen = false;
    
    String ssid = wifiMgr.getSSID();
    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "0.0.0.0";
    int rssi = WiFi.RSSI();
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    
    display.showWifiConfig(ssid, ip, rssi, isConnected);
    display.updateStatus("Info WiFi - Sentuh untuk kembali");
    Serial.println("Menampilkan konfigurasi WiFi");
    
    resetActivityTimer();
}

void showUserListScreen() {
    inHomeScreen = false;
    inMeasurementScreen = false;
    inUserScreen = false;
    inWifiScreen = false;
    inEditUserScreen = false;
    inKeyboardScreen = false;
    inUserListScreen = true;
    inModeSelectionScreen = false;
    inWifiScanScreen = false;

    if (deviceMgr.isEmpty()) {
        display.updateStatus("Tidak ada user!");
        goToHomeScreen();
        return;
    }

    std::vector<String> userNames;
    for (const auto& device : deviceMgr.devices) {
        userNames.push_back(device.name);
    }

    display.showUserList(userNames, selectedIndex, true);
    display.updateStatus("Pilih user dari daftar");
    Serial.println("Menampilkan daftar user");
    
    resetActivityTimer();
}

void showWifiScanScreen() {
    inHomeScreen = false;
    inMeasurementScreen = false;
    inUserScreen = false;
    inWifiScreen = false;
    inEditUserScreen = false;
    inKeyboardScreen = false;
    inUserListScreen = false;
    inModeSelectionScreen = false;
    inWifiScanScreen = true;
    
    Serial.println("Mematikan WiFi untuk scanning yang bersih...");
    WiFi.disconnect(true);
    delay(500);
    
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    
    delay(100);
    
    display.updateStatus("Scanning WiFi networks...");
    Serial.println("Memulai scan WiFi...");
    
    int n = wifiMgr.scanNetworks();
    
    if (n == 0) {
        display.updateStatus("No networks found");
        Serial.println("Tidak ada jaringan WiFi ditemukan");
    } else {
        display.updateStatus(String(n) + " networks found");
        Serial.println(String(n) + " jaringan WiFi ditemukan");
    }
    
    std::vector<String> ssids;
    std::vector<int> rssis;
    std::vector<String> encTypes;
    
    for (int i = 0; i < n; i++) {
        ssids.push_back(wifiMgr.getScannedSSID(i));
        rssis.push_back(wifiMgr.getScannedRSSI(i));
        encTypes.push_back(wifiMgr.getScannedEncryption(i));
        
        Serial.printf("%d: %s (%d dBm) - %s\n", 
                     i, 
                     wifiMgr.getScannedSSID(i).c_str(),
                     wifiMgr.getScannedRSSI(i),
                     wifiMgr.getScannedEncryption(i).c_str());
    }
    
    display.showWifiScanList(ssids, rssis, encTypes, 0);
    
    resetActivityTimer();
}

void refreshCurrentScreen() {
    if (inHomeScreen) {
        String currentUser = deviceMgr.isEmpty() ? "BELUM ADA USER" : deviceMgr.devices[selectedIndex].name;
        bool wifiConnected = (WiFi.status() == WL_CONNECTED);
        display.drawHomeScreen(currentUser, wifiConnected);
        display.updateStatus("User: " + currentUser);
    } 
    else if (inMeasurementScreen) {
        String currentUser = deviceMgr.isEmpty() ? "BELUM ADA USER" : deviceMgr.devices[selectedIndex].name;
        String modeName = display.getMeasurementModeName(selectedMeasurementMode);
        
        if (selectedMeasurementMode == 0) {
            display.drawMedicalRecordInterface(currentUser);
        } else if (selectedMeasurementMode == 2) {
            display.drawECGInterface(currentUser);
        } else if (selectedMeasurementMode == 3) {
            display.drawEMGInterface(currentUser);
        } else if (selectedMeasurementMode == 4) {
            display.drawBodyPositionSnoreInterface(currentUser);
        } else {
            display.drawInterface(currentUser);
        }
        
        display.updateStatus("User: " + currentUser + " | Mode: " + modeName);
    }
}

// --- Fungsi Admin via Serial ---
void handleSerialCommands() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "menu") {
        Serial.println("\n=== MENU ADMIN ===");
        Serial.println("home   - Kembali ke home screen");
        Serial.println("add    - Tambah user baru");
        Serial.println("edit   - Edit user (format: edit [nama] [token] [deviceLabel])");
        Serial.println("wifi   - Setup WiFi");
        Serial.println("del    - Hapus user");
        Serial.println("list   - Lihat semua user");
        Serial.println("listui - Tampilkan daftar user di layar");
        Serial.println("cal    - Calibrate touchscreen");
        Serial.println("status - Lihat status sistem");
        Serial.println("keyboard - Test keyboard virtual");
        Serial.println("mode   - Pilih mode pengukuran");
        Serial.println("test   - Test koneksi Ubidots");
        Serial.println("eda    - Test pembacaan sensor EDA");
        Serial.println("hr     - Test sensor Heart Rate & SpO2");
        Serial.println("temp   - Test sensor suhu MLX90614");
        Serial.println("ecg    - Test sensor ECG AD8232");
        Serial.println("ecgdebug - Debug ECG untuk tuning (Serial Plotter)");
        Serial.println("max    - Test sensor MAX30100 langsung");
        Serial.println("scan   - Scan I2C Bus 1 (Pin 21/22 - Default)");
        Serial.println("scan2  - Scan I2C Bus 2 (Pin 25/26 - MPU6050)");
        Serial.println("sleep  - Sleep MAX30100");
        Serial.println("wake   - Wake MAX30100");
        Serial.println("i2c    - Reset I2C bus");
        Serial.println("bp    - Test sensor Blood Pressure");
        Serial.println("bpdebug    - menampilkan data RAW dari tensimeter");
        Serial.println("threshold - Ubah threshold ECG (contoh: threshold 2600)");
        Serial.println("body   - Test sensor Body Position & Snore (MPU6050 + Sound)");
        Serial.println("pos    - Test hanya posisi tubuh (MPU6050)");
        Serial.println("snore  - Test hanya sensor dengkur (Sound)");
        Serial.println("emg    - Test sensor EMG dengan grafik sederhana");
    }
    else if (cmd == "home") {
        goToHomeScreen();
    }
    else if (cmd == "cal") {
        Serial.println("Starting touchscreen calibration...");
        display.startCalibration();
        Serial.println("Calibration complete. Restarting...");
        ESP.restart();
    }
    else if (cmd == "list") {
        deviceMgr.printDevices();
    }
    else if (cmd == "status") {
        Serial.println("\n=== SYSTEM STATUS ===");
        Serial.printf("Screen: %s\n", inHomeScreen ? "Home" : 
                     inMeasurementScreen ? "Measurement" : 
                     inUserScreen ? "User Info" : 
                     inWifiScreen ? "WiFi Config" : 
                     inEditUserScreen ? "Edit User" : 
                     inUserListScreen ? "User List" : 
                     inModeSelectionScreen ? "Mode Selection" : 
                     inWifiScanScreen ? "WiFi Scan" : "Keyboard");
        Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        }
        Serial.printf("Users: %d\n", deviceMgr.devices.size());
        Serial.printf("Measuring: %s\n", measuring ? "YES" : "NO");
        Serial.printf("Selected User: %d - %s\n", selectedIndex, 
                     deviceMgr.isEmpty() ? "None" : deviceMgr.devices[selectedIndex].name.c_str());
        Serial.printf("Ubidots Token: %s\n", deviceMgr.isEmpty() ? "None" : 
                     deviceMgr.devices[selectedIndex].ubidotsToken.substring(0, 10).c_str());
        Serial.printf("Device Label: %s\n", deviceMgr.isEmpty() ? "None" : 
                     deviceMgr.devices[selectedIndex].ubidotsDeviceLabel.c_str());
        Serial.printf("Measurement Mode: %d - %s\n", selectedMeasurementMode,
                     display.getMeasurementModeName(selectedMeasurementMode).c_str());
        Serial.printf("MAX30100 Available: %s\n", medSensor.isMAX30100Available() ? "YES" : "NO");
        if (medSensor.isMAX30100Available()) {
            Serial.printf("MAX30100 Measuring: %s\n", medSensor.isHRMeasuring() ? "YES" : "NO");
            Serial.printf("MAX30100 Sleeping: %s\n", medSensor.isMAX30100Sleeping() ? "YES" : "NO");
            Serial.printf("MAX30100 Status: %s\n", medSensor.getMAX30100Status().c_str());
        }
        Serial.printf("MLX90614 Available: %s\n", medSensor.isMLX90614Available() ? "YES" : "NO");
        if (medSensor.isMLX90614Available()) {
            Serial.printf("MLX90614 Sleeping: %s\n", medSensor.isMLX90614Sleeping() ? "YES" : "NO");
            Serial.printf("MLX90614 Status: %s\n", medSensor.getMLX90614Status().c_str());
        }
        Serial.printf("ECG AD8232 Available: %s\n", ecgSensor.isSensorAvailable() ? "YES" : "NO");
        if (ecgSensor.isSensorAvailable()) {
            Serial.printf("ECG Lead Status: %s\n", ecgSensor.getLeadOffStatus().c_str());
            Serial.printf("ECG Heart Rate: %d BPM\n", ecgSensor.getHeartRateBPM());
            Serial.printf("ECG Threshold: %d\n", ecgSensor.getUpperThreshold());
        }
        Serial.printf("MPU6050 Available: %s\n", bodyPositionSnoreSensor.isMPU6050Available() ? "YES" : "NO");
        if (bodyPositionSnoreSensor.isMPU6050Available()) {
            Serial.printf("Current Position: %d (%s)\n", 
                         bodyPositionSnoreSensor.getPosition(),
                         bodyPositionSnoreSensor.getPositionName().c_str());
            Serial.printf("Current Snore Amplitude: %d\n",
                         bodyPositionSnoreSensor.getSnoreAmplitude());
        }
        Serial.printf("EMG Sensor Available: %s\n", emgSensor.isSensorAvailable() ? "YES" : "NO");
        if (emgSensor.isSensorAvailable()) {
            Serial.printf("EMG Calibrated: %s\n", emgSensor.isCalibrated() ? "YES" : "NO");
            Serial.printf("EMG Buffer Index: %d/400\n", min(emgSimpleIndex, 400));
        }
        Serial.printf("Activity Timer: %lu ms\n", millis() - lastActivityTime);
        Serial.printf("Auto-sleep in: %lu ms\n", 
                     AUTO_SLEEP_TIMEOUT - (millis() - lastActivityTime));
    }
    else if (cmd == "add") {
        Serial.println("\n=== TAMBAH USER BARU ===");
        Serial.println("(Kosongkan untuk batal)");
        
        Serial.print("Nama User: ");
        String name = readLineFromSerial(30000);
        if (name.length() == 0) {
            Serial.println("✗ Dibatalkan.");
            return;
        }
        
        Serial.print("Ubidots Token: ");
        String token = readLineFromSerial(30000);
        if (token.length() == 0) {
            Serial.println("✗ Dibatalkan.");
            return;
        }
        
        Serial.print("Ubidots Device Label: ");
        String deviceLabel = readLineFromSerial(30000);
        if (deviceLabel.length() == 0) {
            Serial.println("✗ Dibatalkan.");
            return;
        }
        
        Serial.println("\n=== KONFIRMASI ===");
        Serial.println("Nama: " + name);
        Serial.println("Token: " + token);
        Serial.println("Device Label: " + deviceLabel);
        Serial.print("Tambahkan user? (y/n): ");
        
        String confirm = readLineFromSerial(10000);
        if (confirm.equalsIgnoreCase("y")) {
            deviceMgr.addDevice(name, token, deviceLabel);
            Serial.println("✓ User berhasil ditambahkan!");
            
            if (deviceMgr.devices.size() == 1) {
                selectedIndex = 0;
            }
            
            refreshCurrentScreen();
        } else {
            Serial.println("✗ Dibatalkan.");
        }
    }
    else if (cmd.startsWith("edit ")) {
        if (deviceMgr.isEmpty()) {
            Serial.println("✗ Tidak ada user untuk diedit!");
            return;
        }
        
        String params = cmd.substring(5);
        params.trim();
        
        int firstSpace = params.indexOf(' ');
        if (firstSpace == -1) {
            Serial.println("✗ Format salah! Gunakan: edit [nama] [token] [deviceLabel]");
            return;
        }
        
        String newName = params.substring(0, firstSpace);
        String rest = params.substring(firstSpace + 1);
        
        int secondSpace = rest.indexOf(' ');
        if (secondSpace == -1) {
            Serial.println("✗ Format salah! Gunakan: edit [nama] [token] [deviceLabel]");
            return;
        }
        
        String newToken = rest.substring(0, secondSpace);
        String newDeviceLabel = rest.substring(secondSpace + 1);
        
        if (newName.startsWith("\"") && newName.endsWith("\"")) {
            newName = newName.substring(1, newName.length() - 1);
        }
        
        if (newName.length() == 0 || newToken.length() == 0 || newDeviceLabel.length() == 0) {
            Serial.println("✗ Nama, token, atau device label tidak boleh kosong!");
            return;
        }
        
        String currentName = deviceMgr.devices[selectedIndex].name;
        String currentToken = deviceMgr.devices[selectedIndex].ubidotsToken;
        String currentDeviceLabel = deviceMgr.devices[selectedIndex].ubidotsDeviceLabel;
        
        Serial.printf("\n=== EDIT USER: %s ===\n", currentName.c_str());
        Serial.printf("Nama: %s → %s\n", currentName.c_str(), newName.c_str());
        Serial.printf("Token: %s... → %s...\n", 
                     currentToken.substring(0, min(10, (int)currentToken.length())).c_str(),
                     newToken.substring(0, min(10, (int)newToken.length())).c_str());
        Serial.printf("Device Label: %s → %s\n", currentDeviceLabel.c_str(), newDeviceLabel.c_str());
        
        String confirmMsg = "Edit user:\n" + currentName;
        confirmMsg += "\n→ " + newName;
        
        bool confirmed = display.showConfirmationDialogSerial("EDIT USER", confirmMsg);
        
        if (confirmed) {
            deviceMgr.editDevice(selectedIndex, newName, newToken, newDeviceLabel);
            Serial.println("✓ User berhasil diedit!");
            refreshCurrentScreen();
        } else {
            Serial.println("✗ Dibatalkan.");
            refreshCurrentScreen();
        }
    }
    else if (cmd == "keyboard") {
        Serial.println("Testing keyboard virtual...");
        goToKeyboardScreen("Test Input", "Keyboard Test", true);
    }
    else if (cmd == "wifi") {
        Serial.println("Scanning WiFi networks...");
        int n = wifiMgr.scanNetworks();
        
        if (n == 0) {
            Serial.println("No networks found!");
            return;
        }
        
        Serial.println("\nAvailable Networks:");
        for (int i = 0; i < n; i++) {
            Serial.printf("%2d: %s\n", i, WiFi.SSID(i).c_str());
        }
        
        Serial.print("\nSelect network index: ");
        String idxStr = readLineFromSerial();
        int idx = idxStr.toInt();
        
        if (idx >= 0 && idx < n) {
            Serial.print("Password: ");
            String pass = readLineFromSerial();
            
            Serial.println("\n=== Konfirmasi WiFi ===");
            Serial.println("SSID: " + WiFi.SSID(idx));
            Serial.print("Password: ");
            for (int i = 0; i < pass.length(); i++) Serial.print("*");
            Serial.println("\nSimpan dan connect? (y/n)");
            
            String confirmation = readLineFromSerial();
            if (confirmation.equalsIgnoreCase("y")) {
                wifiMgr.saveCredentials(WiFi.SSID(idx), pass);
                Serial.println("Credentials saved. Connecting...");
                wifiMgr.connectSaved();
                
                delay(3000);
                
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("WiFi Connected: " + WiFi.localIP().toString());
                } else {
                    Serial.println("WiFi connection failed!");
                }
            } else {
                Serial.println("✗ Dibatalkan.");
            }
        } else {
            Serial.println("Invalid index!");
        }
    }
    else if (cmd == "del") {
        if (deviceMgr.isEmpty()) {
            Serial.println("✗ Tidak ada user untuk dihapus!");
            return;
        }
        
        deviceMgr.printDevices();
        Serial.print("\nPilih index untuk dihapus: ");
        String idxStr = readLineFromSerial(15000);
        int idx = idxStr.toInt();
        
        if (idx >= 0 && idx < deviceMgr.devices.size()) {
            String userName = deviceMgr.devices[idx].name;
            
            String confirmMsg = "Hapus user:\n" + userName;
            
            bool confirmed = display.showConfirmationDialogSerial("HAPUS USER", confirmMsg);
            
            if (confirmed) {
                deviceMgr.removeDevice(idx);
                Serial.printf("✓ User '%s' berhasil dihapus!\n", userName.c_str());
                
                if (deviceMgr.isEmpty()) {
                    selectedIndex = 0;
                } else if (selectedIndex >= deviceMgr.devices.size()) {
                    selectedIndex = deviceMgr.devices.size() - 1;
                }
                
                if (inUserListScreen) {
                    showUserListScreen();
                } else {
                    refreshCurrentScreen();
                }
            } else {
                Serial.println("✗ Dibatalkan.");
                refreshCurrentScreen();
            }
        } else {
            Serial.println("✗ Index tidak valid!");
        }
    }
    else if (cmd == "listui") {
        showUserListScreen();
    }
    else if (cmd == "mode") {
        Serial.println("\n=== PILIH MODE PENGUKURAN ===");
        Serial.println("0 - Medical Record (EDA + BPM + SpO2 + Suhu)");
        Serial.println("1 - Blood Pressure");
        Serial.println("2 - ECG (AD8232)");
        Serial.println("3 - EMG");
        Serial.println("4 - Body Position & Snore");
        
        Serial.print("Pilih mode (0-4): ");
        String modeStr = readLineFromSerial(15000);
        int modeIdx = modeStr.toInt();
        
        if (modeIdx >= 0 && modeIdx <= 4) {
            if (selectedMeasurementMode == 0 && modeIdx != 0 && medSensor.isMAX30100Available()) {
                sleepMAX30100IfLeavingMedicalRecord();
            }
            
            selectedMeasurementMode = modeIdx;
            display.setMeasurementMode(selectedMeasurementMode);
            Serial.println("Mode berhasil diubah ke: " + display.getMeasurementModeName(modeIdx));
            
            if (inMeasurementScreen) {
                refreshCurrentScreen();
            }
        } else {
            Serial.println("Mode tidak valid!");
        }
    }
    else if (cmd == "test") {
        if (deviceMgr.isEmpty()) {
            Serial.println("✗ Tidak ada user untuk test!");
            return;
        }
        
        Serial.println("\n=== TEST UBIDOTS CONNECTION ===");
        Serial.printf("User: %s\n", deviceMgr.devices[selectedIndex].name.c_str());
        Serial.printf("Token: %s...\n", deviceMgr.devices[selectedIndex].ubidotsToken.substring(0, 10).c_str());
        Serial.printf("Device Label: %s\n", deviceMgr.devices[selectedIndex].ubidotsDeviceLabel.c_str());
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("✗ WiFi tidak terhubung!");
            return;
        }
        
        Serial.println("Menguji koneksi ke Ubidots...");
        HTTPClient http;
        String url = "https://" + String(UBIDOTS_SERVER) + "/api/v1.6/devices/" + 
                    deviceMgr.devices[selectedIndex].ubidotsDeviceLabel;
        
        http.begin(url);
        http.addHeader("X-Auth-Token", deviceMgr.devices[selectedIndex].ubidotsToken);
        
        int code = http.GET();
        if (code == 200) {
            Serial.println("✓ Koneksi Ubidots BERHASIL!");
            String response = http.getString();
            Serial.println("Response: " + response);
        } else {
            Serial.printf("✗ Koneksi Ubidots GAGAL! Code: %d\n", code);
            String response = http.getString();
            Serial.println("Response: " + response);
        }
        
        http.end();
    }
    else if (cmd == "eda") {
        Serial.println("\n=== TEST SENSOR EDA ===");
        Serial.println("Membaca nilai sensor EDA...");
        
        int edaValue = medSensor.readEDARaw();
        String stressLevel = medSensor.getStressLevel(edaValue);
        float conductance = medSensor.getEDAConductance(edaValue);
        
        Serial.printf("Nilai ADC EDA: %d\n", edaValue);
        Serial.printf("Konduktansi: %.2f µS\n", conductance);
        Serial.printf("Level Stres: %s\n", stressLevel.c_str());
        
        display.updateSensorData(edaValue, stressLevel, false);
        display.updateStatus("Test EDA: " + String(edaValue) + " (" + String(conductance, 2) + " µS)");
        
        delay(3000);
        refreshCurrentScreen();
    }
    else if (cmd == "hr" || cmd == "max") {
        Serial.println("\n=== TEST SENSOR HEART RATE & SPO2 ===");
        
        if (!medSensor.isMAX30100Available()) {
            Serial.println("✗ Sensor MAX30100 tidak terdeteksi!");
            return;
        }
        
        if (medSensor.isMAX30100Sleeping()) {
            Serial.println("Membangunkan MAX30100...");
            medSensor.wakeUpMAX30100();
            delay(500);
        }
        
        Serial.println("MAX30100 is available. Starting test...");
        medSensor.startHRMeasurement();
        
        unsigned long startTime = millis();
        while (millis() - startTime < 15000) {
            float hr = medSensor.getHeartRate();
            int spo2 = medSensor.getSpO2();
            
            Serial.printf("Heart Rate: %.1f BPM, SpO2: %d%%\n", hr, spo2);
            
            display.updateMedicalRecordData(0, "Test", (int)hr, spo2, 0.0, false);
            display.updateStatus("Test HR: " + String(hr, 1) + " BPM, SpO2: " + String(spo2) + "%");
            
            delay(1000);
        }
        
        medSensor.sleepMAX30100();
        Serial.println("Test selesai. Sensor kembali sleep.");
        
        refreshCurrentScreen();
    }
    else if (cmd == "temp") {
        Serial.println("\n=== TEST SENSOR SUHU MLX90614 ===");
        
        if (!medSensor.isMLX90614Available()) {
            Serial.println("✗ Sensor MLX90614 tidak terdeteksi!");
            return;
        }
        
        if (medSensor.isMLX90614Sleeping()) {
            Serial.println("Membangunkan MLX90614...");
            medSensor.wakeUpMLX90614();
            delay(200);
        }
        
        Serial.println("MLX90614 is available. Reading temperature...");
        
        for (int i = 0; i < 10; i++) {
            float objTemp = medSensor.readObjectTemp();
            
            Serial.printf("Reading %d: Object=%.2f°C\n", i+1, objTemp);
            
            display.updateMedicalRecordData(0, "Test", 0, 0, objTemp, false);
            display.updateStatus("Test Temp: " + String(objTemp, 1) + "°C");
            
            delay(1000);
        }
        
        medSensor.sleepMLX90614();
        Serial.println("Test selesai. Sensor kembali sleep.");
        
        refreshCurrentScreen();
    }
    else if (cmd == "emg") {
        Serial.println("\n=== TEST SENSOR EMG DENGAN GRAFIK SEDERHANA ===");
        
        if (!emgSensor.isSensorAvailable()) {
            Serial.println("✗ Sensor EMG tidak terdeteksi!");
            return;
        }
        
        Serial.println("Kalibrasi sensor EMG selama 5 detik...");
        Serial.println("Harap jangan gerakkan sensor selama kalibrasi.");
        
        emgSensor.resetCalibration();
        
        while (!emgSensor.isCalibrated()) {
            emgSensor.update();
            delay(100);
        }
        
        Serial.println("Kalibrasi selesai. Sekarang uji dengan gerakan otot:");
        Serial.println("1. Kencangkan otot lengan");
        Serial.println("2. Lemaskan otot");
        Serial.println("3. Gerakkan jari/jari");
        Serial.println("Test selama 30 detik...");
        
        // Reset buffer sebelum test
        emgSimpleIndex = 0;
        memset(emgSimpleBuffer, 0, sizeof(emgSimpleBuffer));
        
        unsigned long startTime = millis();
        while (millis() - startTime < 30000) {
            emgSensor.update();
            float amplitude_mV = emgSensor.getAmplitudemV();
            int activation_percent = emgSensor.getActivationPercent();
            
            // Simpan data ke buffer untuk grafik
            int emgGraphValue = constrain((int)amplitude_mV, 0, 3000);
            emgSimpleBuffer[emgSimpleIndex % 400] = emgGraphValue;
            emgSimpleIndex++;
            
            Serial.printf("EMG: Amp=%.2f mV, Act=%d%%, Level=%s\n", 
                        amplitude_mV, activation_percent, emgSensor.getEMGLevel().c_str());
            
            updateEMGDisplay(amplitude_mV, activation_percent, 
                           emgSensor.getEMGLevel(), false);
            display.updateStatus("Test EMG: " + String(amplitude_mV, 1) + " mV");
            
            delay(10);
        }
        
        Serial.println("Test selesai.");
        emgSimpleIndex = 0;
        refreshCurrentScreen();
    }
    else if (cmd == "ecg") {
        Serial.println("\n=== TEST SENSOR ECG (AD8232 SIMPLE) ===");
        
        if (!ecgSensor.isSensorAvailable()) {
            Serial.println("✗ Sensor ECG (AD8232) tidak terdeteksi!");
            return;
        }
        
        Serial.println("Membaca data ECG selama 10 detik...");
        
        for (int i = 0; i < 10; i++) {
            int ecgRaw = ecgSensor.readRawECG();
            ecgSensor.updateFilter();
            float ecgFiltered = ecgSensor.getFilteredValue();
            float ecgVoltage = ecgSensor.getECGVoltage();
            bool heartbeat = ecgSensor.detectHeartbeat();
            int heartRate = ecgSensor.getHeartRateBPM();
            int threshold = ecgSensor.getUpperThreshold();
            
            int* ecgBuffer = ecgSensor.getECGBuffer();
            int bufferSize = ecgSensor.getECGBufferSize();
            
            Serial.printf("Second %d: Raw=%d, Filtered=%.1f, Voltage=%.2fV, HR=%d BPM\n", 
                        i+1, ecgRaw, ecgFiltered, ecgVoltage, heartRate);
            Serial.printf("  Threshold: %d, Heartbeat: %s\n", 
                        threshold, heartbeat ? "DETECTED" : "NO");
            
            updateECGDisplay(ecgRaw, ecgVoltage, heartRate, "Test", false);
            display.updateStatus("Test ECG: " + String(ecgRaw) + " (" + String(ecgVoltage, 2) + "V)");
            
            delay(1000);
        }
        
        Serial.println("Test selesai.");
        refreshCurrentScreen();
    }
    else if (cmd == "ecgdebug") {
        Serial.println("\n=== ECG DEBUG MODE (UNTUK SERIAL PLOTTER) ===");
        Serial.println("Mengirim data ke Serial Plotter untuk tuning threshold...");
        Serial.println("Format data: Raw:[value],Filter:[value],Threshold:[value],BPM:[value]");
        Serial.println("Perintah yang tersedia:");
        Serial.println("  threshold [nilai]  - Ubah threshold ECG (contoh: threshold 2600)");
        Serial.println("  exit               - Keluar dari debug mode");
        
        bool debugMode = true;
        unsigned long lastPrint = 0;
        
        while (debugMode) {
            if (Serial.available()) {
                String debugCmd = Serial.readStringUntil('\n');
                debugCmd.trim();
                
                if (debugCmd == "exit") {
                    debugMode = false;
                    Serial.println("Keluar dari ECG debug mode");
                    break;
                } else if (debugCmd.startsWith("threshold ")) {
                    String valStr = debugCmd.substring(10);
                    int newThresh = valStr.toInt();
                    if (newThresh > 0 && newThresh < 4096) {
                        ecgSensor.setUpperThreshold(newThresh);
                        Serial.printf("Threshold ECG diubah ke: %d\n", newThresh);
                    } else {
                        Serial.println("Threshold harus antara 1-4095");
                    }
                } else {
                    Serial.println("Perintah tidak dikenal. Ketik 'exit' untuk keluar.");
                }
            }
            
            if (millis() - lastPrint >= 10) {
                lastPrint = millis();
                
                int raw = ecgSensor.readRawECG();
                ecgSensor.updateFilter();
                float filtered = ecgSensor.getFilteredValue();
                int threshold = ecgSensor.getUpperThreshold();
                int bpm = ecgSensor.getHeartRateBPM();
                
                Serial.printf("Raw:%d,Filter:%.1f,Threshold:%d,BPM:%d\n",
                             raw, filtered, threshold, bpm);
            }
        }
        
        Serial.println("Kembali ke menu utama.");
    }
    else if (cmd == "bp") {
        Serial.println("\n=== TEST SENSOR BLOOD PRESSURE ===");
        Serial.println("Membaca data dari tensimeter...");
        
        bpSensor.startMeasurement();
        measuring = true;
        
        unsigned long startTime = millis();
        int readings = 0;
        
        while (millis() - startTime < 30000) {
            bpSensor.update();
            
            if (bpSensor.isDataAvailable()) {
                int systolic = bpSensor.getSystolic();
                int diastolic = bpSensor.getDiastolic();
                int bpm = bpSensor.getBPM();
                
                Serial.printf("Reading %d: %d/%d mmHg, HR: %d bpm\n", 
                            ++readings, systolic, diastolic, bpm);
                
                display.updateBloodPressureData(systolic, diastolic, bpm, false);
                display.updateStatus("Test BP: " + String(systolic) + "/" + 
                                    String(diastolic) + " mmHg, " + String(bpm) + " bpm");
                
                bpSensor.resetDataFlag();
            }
            
            delay(100);
        }
        
        bpSensor.stopMeasurement();
        measuring = false;
        Serial.println("Test selesai.");
        
        delay(3000);
        refreshCurrentScreen();
    }
    else if (cmd == "bpdebug") {
        Serial.println("\n=== BLOOD PRESSURE DEBUG MODE ===");
        Serial.println("Menampilkan data raw dari Serial2...");
        Serial.println("Format: Karakter ASCII langsung");
        Serial.println("Perintah: 'exit' untuk keluar");
        
        bpSensor.startMeasurement();
        bool debugMode = true;
        
        while (debugMode) {
            if (Serial.available()) {
                String debugCmd = Serial.readStringUntil('\n');
                debugCmd.trim();
                
                if (debugCmd == "exit") {
                    debugMode = false;
                    Serial.println("Keluar dari BP debug mode");
                    break;
                }
            }
            
            while (Serial2.available()) {
                char c = (char)Serial2.read();
                if (c >= 32 && c <= 126) {
                    Serial.print(c);
                } else {
                    Serial.printf("[%02X]", c);
                }
            }
            
            delay(10);
        }
        
        bpSensor.stopMeasurement();
        Serial.println("Kembali ke menu utama.");
    }
    else if (cmd.startsWith("threshold ")) {
        String valStr = cmd.substring(10);
        int newThresh = valStr.toInt();
        if (newThresh > 0 && newThresh < 4096) {
            ecgSensor.setUpperThreshold(newThresh);
            Serial.printf("Threshold ECG diubah ke: %d\n", newThresh);
            Serial.println("Gunakan 'ecgdebug' untuk melihat hasil di Serial Plotter");
        } else {
            Serial.println("Threshold harus antara 1-4095");
        }
    }
    else if (cmd == "scan") {
        Serial.println("\n=== I2C SCAN ===");
        Serial.println("Scanning I2C bus...");
        
        Wire.end();
        delay(100);
        Wire.begin(21, 22);
        Wire.setClock(100000);
        delay(100);
        
        byte error, address;
        int nDevices = 0;
        
        for(address = 1; address < 127; address++) {
            Wire.beginTransmission(address);
            error = Wire.endTransmission();
            
            if (error == 0) {
                Serial.print("I2C device found at address 0x");
                if (address < 16) {
                    Serial.print("0");
                }
                Serial.print(address, HEX);
                
                switch(address) {
                    case 0x57:
                        Serial.println(" -> MAX30100/MAX30102");
                        break;
                    case 0x5A:
                        Serial.println(" -> MLX90614 Temperature Sensor");
                        break;
                    case 0x29:
                        Serial.println(" -> VL53L0X/APDS9960");
                        break;
                    case 0x68:
                        Serial.println(" -> MPU6050/DS3231");
                        break;
                    case 0x3C:
                    case 0x3D:
                        Serial.println(" -> OLED Display");
                        break;
                    case 0x38:
                        Serial.println(" -> FT6206 Touch Controller");
                        break;
                    case 0x40:
                        Serial.println(" -> HTU21D/SHT21");
                        break;
                    case 0x76:
                    case 0x77:
                        Serial.println(" -> BMP280/BME280");
                        break;
                    default:
                        Serial.println(" -> Unknown device");
                        break;
                }
                nDevices++;
            }
            else if (error == 4) {
                Serial.print("Unknown error at address 0x");
                if (address < 16) {
                    Serial.print("0");
                }
                Serial.println(address, HEX);
            }
        }
        
        if (nDevices == 0) {
            Serial.println("No I2C devices found!");
        } else {
            Serial.print("Found ");
            Serial.print(nDevices);
            Serial.println(" device(s).");
        }
        
        Wire.setClock(100000);
    }
    else if (cmd == "scan2") {
        Serial.println("\n=== I2C BUS 2 SCAN (Wire1: Pin 25, 26) ===");
        Serial.println("Scanning...");
        
        Wire1.begin(25, 26); 
        Wire1.setClock(100000);
        
        byte error, address;
        int nDevices = 0;
        
        for(address = 1; address < 127; address++) {
            Wire1.beginTransmission(address);
            error = Wire1.endTransmission();
            
            if (error == 0) {
                Serial.print("I2C device found at address 0x");
                if (address < 16) Serial.print("0");
                Serial.print(address, HEX);
                
                if (address == 0x68) {
                    Serial.println(" -> MPU6050 (Address Awal)");
                } else if (address == 0x69) {
                    Serial.println(" -> MPU6050 (Alternative Address)");
                } else {
                    Serial.println(" -> Unknown device");
                }
                nDevices++;
            }
            else if (error == 4) {
                Serial.print("Unknown error at address 0x");
                if (address < 16) Serial.print("0");
                Serial.println(address, HEX);
            }
        }
        
        if (nDevices == 0) {
            Serial.println("No I2C devices found on Bus 2!");
            Serial.println("Check wiring on Pin 25 (SDA) and 26 (SCL)");
        } else {
            Serial.printf("Found %d device(s) on Bus 2.\n", nDevices);
        }
    }
    else if (cmd == "sleep") {
        if (!medSensor.isMAX30100Available()) {
            Serial.println("MAX30100 tidak tersedia!");
            return;
        }
        
        if (medSensor.isMAX30100Sleeping()) {
            Serial.println("MAX30100 sudah dalam keadaan sleep!");
        } else {
            medSensor.sleepMAX30100();
            Serial.println("MAX30100 dimatikan (sleep)");
        }
    }
    else if (cmd == "wake") {
        if (!medSensor.isMAX30100Available()) {
            Serial.println("MAX30100 tidak tersedia!");
            return;
        }
        
        if (!medSensor.isMAX30100Sleeping()) {
            Serial.println("MAX30100 sudah dalam keadaan bangun!");
        } else {
            medSensor.wakeUpMAX30100();
            Serial.println("MAX30100 dibangunkan (wake up)");
        }
    }
    else if (cmd == "i2c") {
        Serial.println("Resetting I2C bus...");
        medSensor.restartI2C();
        Serial.println("I2C bus reset complete");
    }
    else if (cmd == "body") {
        Serial.println("\n=== TEST SENSOR BODY POSITION & SNORE ===");
        
        if (!bodyPositionSnoreSensor.isMPU6050Available()) {
            Serial.println("✗ Sensor MPU6050 tidak terdeteksi!");
            return;
        }
        
        Serial.println("Membaca data selama 30 detik...");
        
        unsigned long startTime = millis();
        while (millis() - startTime < 30000) {
            bodyPositionSnoreSensor.update();
            
            int pos = bodyPositionSnoreSensor.getPosition();
            int snoreAmplitude = bodyPositionSnoreSensor.getSnoreAmplitude();
            
            Serial.printf("Position: %d (%s), Snore Amplitude: %d\n", 
                        pos,
                        bodyPositionSnoreSensor.getPositionName().c_str(),
                        snoreAmplitude);
            
            display.updateStatus("Test: Pos=" + String(pos) + ", Snore Amp=" + String(snoreAmplitude));
            
            delay(1000);
        }
        
        Serial.println("Test selesai.");
        refreshCurrentScreen();
    }
    else if (cmd == "pos") {
        Serial.println("\n=== TEST HANYA POSISI TUBUH (MPU6050) ===");
        
        if (!bodyPositionSnoreSensor.isMPU6050Available()) {
            Serial.println("✗ Sensor MPU6050 tidak terdeteksi!");
            return;
        }
        
        Serial.println("Membaca posisi tubuh selama 30 detik...");
        Serial.println("Ubah posisi sensor untuk melihat perubahan:");
        Serial.println("0: Transisi, 1: Berdiri, 2: Duduk, 3: Telentang");
        Serial.println("4: Tengkurap, 5: Miring Kanan, 6: Miring Kiri");
        
        unsigned long startTime = millis();
        int lastPos = -1;
        
        while (millis() - startTime < 30000) {
            bodyPositionSnoreSensor.update();
            int pos = bodyPositionSnoreSensor.getPosition();
            
            if (pos != lastPos) {
                Serial.printf("Posisi berubah: %d (%s)\n", 
                            pos, 
                            bodyPositionSnoreSensor.getPositionName().c_str());
                lastPos = pos;
            }
            
            delay(100);
        }
        
        Serial.println("Test selesai.");
        refreshCurrentScreen();
    }
    else if (cmd == "snore") {
        Serial.println("\n=== TEST HANYA SENSOR DENGKUR (SOUND) ===");
        Serial.println("Membaca data amplitude...");
        Serial.println("Test selama 30 detik...");
        
        unsigned long startTime = millis();
        while (millis() - startTime < 30000) {
            bodyPositionSnoreSensor.update();
            int snoreAmplitude = bodyPositionSnoreSensor.getSnoreAmplitude();
            
            Serial.printf("Snore Amplitude: %d\n", snoreAmplitude);
            
            delay(1000);
        }
        
        Serial.println("Test selesai.");
        refreshCurrentScreen();
    }
    else if (cmd == "testcloud") {
        Serial.println("\n=== TEST MULTI-CLOUD CONNECTION ===");
        
        if (!cloudBroker.isReady()) {
            Serial.println("✗ CloudBroker not ready");
            return;
        }
        
        bool result = cloudBroker.testConnection();
        
        Serial.println("\n--- Results ---");
        Serial.println("Overall: " + String(result ? "SUCCESS" : "FAILED"));
        Serial.println("Ubidots: " + cloudBroker.getUbidotsStatus());
        Serial.println("ThingsBoard: " + cloudBroker.getThingsBoardStatus());
    }
    else if (cmd == "cloudstatus") {
        Serial.println("\n=== CLOUD BROKER STATUS ===");
        Serial.print("Provider Type: ");
        switch(cloudBroker.getProviderType()) {
            case PROVIDER_NONE: Serial.println("0 (None)"); break;
            case PROVIDER_UBIDOTS: Serial.println("1 (Ubidots only)"); break;
            case PROVIDER_THINGSBOARD: Serial.println("2 (ThingsBoard only)"); break;
            case PROVIDER_BOTH: Serial.println("3 (Both platforms)"); break;
        }
        Serial.println("Status: " + cloudBroker.getStatusMessage());
        Serial.println("Ubidots: " + cloudBroker.getUbidotsStatus());
        Serial.println("ThingsBoard: " + cloudBroker.getThingsBoardStatus());
    }
    else if (cmd == "addcloud") {
        Serial.println("\n=== TAMBAH USER DENGAN MULTI-CLOUD ===");
        Serial.println("(Kosongkan untuk batal)");
        
        Serial.print("Nama User: ");
        String name = readLineFromSerial(30000);
        if (name.length() == 0) {
            Serial.println("✗ Dibatalkan.");
            return;
        }
        
        Serial.println("\nCloud Provider:");
        Serial.println("1 - Ubidots only");
        Serial.println("2 - ThingsBoard only");
        Serial.println("3 - Both (Ubidots + ThingsBoard)");
        Serial.print("Pilih (1-3): ");
        String provStr = readLineFromSerial(15000);
        int cloudProvider = provStr.toInt();
        
        if (cloudProvider < 1 || cloudProvider > 3) {
            Serial.println("✗ Pilihan tidak valid!");
            return;
        }
        
        String ubidotsToken = "";
        String ubidotsLabel = "";
        String thingsboardToken = "";
        String thingsboardLabel = "";
        
        if (cloudProvider == 1 || cloudProvider == 3) {
            Serial.print("Ubidots Token: ");
            ubidotsToken = readLineFromSerial(30000);
            if (ubidotsToken.length() == 0) {
                Serial.println("✗ Token tidak boleh kosong!");
                return;
            }
            Serial.print("Ubidots Device Label: ");
            ubidotsLabel = readLineFromSerial(30000);
            if (ubidotsLabel.length() == 0) {
                Serial.println("✗ Device label tidak boleh kosong!");
                return;
            }
        }
        
        if (cloudProvider == 2 || cloudProvider == 3) {
            Serial.print("ThingsBoard Token: ");
            thingsboardToken = readLineFromSerial(30000);
            if (thingsboardToken.length() == 0) {
                Serial.println("✗ Token tidak boleh kosong!");
                return;
            }
            Serial.print("ThingsBoard Device Label (optional): ");
            thingsboardLabel = readLineFromSerial(30000);
        }
        
        Serial.println("\n=== KONFIRMASI ===");
        Serial.println("Nama: " + name);
        Serial.print("Cloud Provider: ");
        switch(cloudProvider) {
            case 1: Serial.println("Ubidots only"); break;
            case 2: Serial.println("ThingsBoard only"); break;
            case 3: Serial.println("Both platforms"); break;
        }
        if (cloudProvider == 1 || cloudProvider == 3) {
            Serial.println("Ubidots Token: " + ubidotsToken.substring(0, min(10, (int)ubidotsToken.length())) + "...");
            Serial.println("Ubidots Label: " + ubidotsLabel);
        }
        if (cloudProvider == 2 || cloudProvider == 3) {
            Serial.println("ThingsBoard Token: " + thingsboardToken.substring(0, min(10, (int)thingsboardToken.length())) + "...");
            if (thingsboardLabel.length() > 0) {
                Serial.println("ThingsBoard Label: " + thingsboardLabel);
            }
        }
        Serial.print("Tambahkan user? (y/n): ");
        
        String confirm = readLineFromSerial(10000);
        if (confirm.equalsIgnoreCase("y")) {
            deviceMgr.addDeviceWithCloud(name, 
                                        ubidotsToken, ubidotsLabel,
                                        thingsboardToken, thingsboardLabel,
                                        cloudProvider);
            Serial.println("✓ User dengan multi-cloud berhasil ditambahkan!");
            
            if (deviceMgr.devices.size() == 1) {
                selectedIndex = 0;
                
                cloudBroker.begin(
                    (CloudProviderType)cloudProvider,
                    ubidotsToken, ubidotsLabel,
                    thingsboardToken, thingsboardLabel
                );
                Serial.println("CloudBroker initialized for new user");
            }
            
            refreshCurrentScreen();
        } else {
            Serial.println("✗ Dibatalkan.");
        }
    }
    else if (cmd.length() > 0) {
        Serial.printf("Unknown command: %s\n", cmd.c_str());
        Serial.println("Type 'menu' for available commands.");
    }
}

// --- SETUP ---
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    
    Serial.println("\n" + String(80, '='));
    Serial.println("        SMART HEALTH PENS SYSTEM (UBIDOTS)");
    Serial.println("        WITH MEDICAL RECORD + ECG SENSOR (AD8232 SIMPLE)");
    Serial.println("        + EMG WITH SIMPLE GRAPH + BODY POSITION & SNORE SENSOR");
    Serial.println(String(80, '='));
    
    initI2CForSensors();
    
    Serial.println("\nInitializing display...");
    display.begin();
    
    Serial.println("\nInitializing Medical Record Sensor...");
    medSensor.begin();

    Serial.println("\nInitializing ECG Sensor (AD8232 Simple)...");
    ecgSensor.begin();

    Serial.println("\nInitializing Blood Pressure Sensor...");
    bpSensor.begin();
    
    Serial.println("\nInitializing Body Position & Snore Sensor...");
    bodyPositionSnoreSensor.begin();
    
    Serial.println("\nInitializing EMG Sensor...");
    emgSensor.begin();
    
    emgSimpleIndex = 0;
    memset(emgSimpleBuffer, 0, sizeof(emgSimpleBuffer));

    display.setMeasurementMode(selectedMeasurementMode);
    
    Serial.println("\n=== ALL SENSORS STATUS ===");
    Serial.printf("EDA Sensor: Ready\n");
    Serial.printf("MAX30100: %s\n", medSensor.isMAX30100Available() ? "Available (Sleep Mode)" : "Not Available");
    Serial.printf("MLX90614: %s\n", medSensor.isMLX90614Available() ? "Available" : "Not Available");
    Serial.printf("ECG AD8232: %s\n", ecgSensor.isSensorAvailable() ? "Available" : "Not Available");
    Serial.printf("EMG Sensor: %s\n", emgSensor.isSensorAvailable() ? "Available" : "Not Available");
    if (emgSensor.isSensorAvailable()) {
        Serial.printf("  Calibrated: %s\n", emgSensor.isCalibrated() ? "Yes" : "No");
        Serial.printf("  Buffer Size: %d samples\n", 400);
    }
    Serial.printf("MPU6050: %s\n", bodyPositionSnoreSensor.isMPU6050Available() ? "Available" : "Not Available");
    Serial.println("=== ================== ===");
    
    Serial.println("\nInitializing device manager...");
    deviceMgr.begin();
    
    // Initialize CloudBroker after deviceMgr is ready
    if (!deviceMgr.isEmpty()) {
        Device& currentDevice = deviceMgr.devices[selectedIndex];
        CloudProviderType providerType = (CloudProviderType)currentDevice.cloudProvider;
        
        cloudBroker.begin(
            providerType,
            currentDevice.ubidotsToken,
            currentDevice.ubidotsDeviceLabel,
            currentDevice.thingsboardToken,
            currentDevice.thingsboardDeviceLabel
        );
        
        Serial.println("[CloudBroker] Initialized successfully");
    } else {
        Serial.println("[CloudBroker] No users configured, skipping initialization");
    }
    
    Serial.println("Initializing WiFi manager...");
    wifiMgr.begin();
    
    if (wifiMgr.hasCredentials()) {
        Serial.println("Connecting to WiFi...");
        wifiMgr.connectSaved();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi Connected: " + WiFi.localIP().toString());
        } else {
            Serial.println("WiFi connection failed!");
        }
    } else {
        Serial.println("WiFi not configured. Use 'wifi' command.");
    }
    
    if (!deviceMgr.isEmpty()) {
        selectedIndex = 0;
        Serial.printf("Default user: %s\n", deviceMgr.devices[selectedIndex].name.c_str());
        Serial.printf("Ubidots Token: %s...\n", deviceMgr.devices[selectedIndex].ubidotsToken.substring(0, 10).c_str());
        Serial.printf("Device Label: %s\n", deviceMgr.devices[selectedIndex].ubidotsDeviceLabel.c_str());
    } else {
        Serial.println("No users configured. Use 'add' command.");
    }
    
    lastActivityTime = millis();
    
    goToHomeScreen();
    
    Serial.println("\n=== SYSTEM READY ===");
    Serial.println("Sensors: Medical Record (EDA + HR + SpO2 + Temp) + ECG (AD8232 Simple)");
    Serial.println("         + EMG with Simple Graph + Body Position & Snore (MPU6050 + Sound Sensor)");
    Serial.println("Cloud Platform: Ubidots");
    Serial.println("Navigation: Home Screen dengan 4 menu");
    Serial.println("Commands: 'menu' untuk opsi Serial");
    Serial.println("Edit User: Gunakan layar edit dengan keyboard virtual");
    Serial.println("WiFi Scan: Tersedia di menu WiFi Config");
    Serial.println("Measurement Modes: 5 mode pengukuran tersedia");
    Serial.println("Medical Record Mode: EDA + Heart Rate + SpO2 + Temperature");
    Serial.println("ECG Mode: AD8232 ECG Sensor (Simple Version) dengan grafik");
    Serial.println("EMG Mode: EMG Sensor dengan grafik sederhana seperti ECG");
    Serial.println("Body Position & Snore Mode: MPU6050 + Sound Sensor");
    Serial.println("ECG Debug: 'ecgdebug' untuk tuning threshold via Serial Plotter");
    Serial.println("Auto-sleep: MAX30100 sleep setelah 5 menit idle");
    Serial.println(String(80, '=') + "\n");
}

// --- LOOP ---
void loop() {
    static unsigned long lastWifiCheck = 0;
    static unsigned long lastSensorUpdate = 0;
    static bool wasConnected = false;
    
    checkAutoSleep();

    if (!inWifiScanScreen) {
        if (millis() - lastWifiCheck > 5000) {
            bool isConnected = (WiFi.status() == WL_CONNECTED);
            
            if (wasConnected && !isConnected) {
                Serial.println("WiFi terputus!");
                display.updateStatus("WiFi terputus! Sentuh menu WiFi untuk reconnect");
                
                if (inHomeScreen) {
                    String currentUser = deviceMgr.isEmpty() ? "BELUM ADA USER" : deviceMgr.devices[selectedIndex].name;
                    display.drawHomeScreen(currentUser, false);
                }
            }
            else if (!wasConnected && isConnected) {
                Serial.println("WiFi terhubung kembali");
                display.updateStatus("WiFi terhubung: " + WiFi.localIP().toString());
                
                if (inHomeScreen) {
                    String currentUser = deviceMgr.isEmpty() ? "BELUM ADA USER" : deviceMgr.devices[selectedIndex].name;
                    display.drawHomeScreen(currentUser, true);
                }
            }
            
            wasConnected = isConnected;
            lastWifiCheck = millis();
        }
    }

    if (measuring && selectedMeasurementMode == 0 && 
        medSensor.isMAX30100Available() && medSensor.isHRMeasuring()) {
        
        if (millis() - lastSensorUpdate > 100) {
            lastSensorUpdate = millis();
        }
    }

    int btn = 0;
    
    if (inHomeScreen) {
        btn = display.checkTouch(0);
    }
    else if (inMeasurementScreen) {
        btn = display.checkTouch(1);
    }
    else if (inUserScreen) {
        display.setHasUsers(!deviceMgr.isEmpty());
        btn = display.checkTouch(2);
    }
    else if (inWifiScreen) {
        btn = display.checkTouch(3);
    }
    else if (inEditUserScreen) {
        btn = display.checkTouch(4);
    }
    else if (inKeyboardScreen) {
        btn = display.checkTouch(5);
    }
    else if (inUserListScreen) {
        btn = display.checkTouch(6);
    }
    else if (inWifiScanScreen) {
        btn = display.checkTouch(7);
    }
    else if (inModeSelectionScreen) {
        btn = display.checkTouch(8);
    }
    
    if (inHomeScreen) {
        switch (btn) {
            case 10:
                showCurrentUser();
                break;
            case 20:
                showMeasurementModeSelection();
                break;
            case 30:
                showUserListScreen();
                break;
            case 40:
                showWifiConfig();
                break;
        }
    }
    else if (inMeasurementScreen) {
        if (btn == 1) {
            if (deviceMgr.isEmpty()) {
                display.updateStatus("ERROR: Tambah User via Serial!");
                Serial.println("ERROR: No users configured!");
            } else {
                measuring = !measuring;
                
                if (measuring) {
                    if (selectedMeasurementMode == 0 && medSensor.isMAX30100Available()) {
                        wakeUpMAX30100IfNeeded();
                        medSensor.startHRMeasurement();
                        display.updateStatus("Medical Record aktif...");
                        Serial.println("Medical Record mode activated");
                    } 
                    else if (selectedMeasurementMode == 2) {
                        display.updateStatus("ECG Monitor aktif...");
                        Serial.println("ECG mode activated");
                    }
                    else if (selectedMeasurementMode == 3) {
                        emgSimpleIndex = 0;
                        display.updateStatus("EMG Monitor aktif...");
                        Serial.println("EMG mode activated - buffer reset");
                    }
                    else if (selectedMeasurementMode == 4) {
                        display.updateStatus("Body Position & Snore aktif...");
                        Serial.println("Body Position & Snore mode activated");
                    }
                    
                    if (selectedMeasurementMode != 1 && selectedMeasurementMode != 2 && selectedMeasurementMode != 4) {
                        display.updateStatus("Kalibrasi Sensor EDA...");
                        Serial.println("Starting measurement - calibrating EDA...");
                        medSensor.calibrateEDA();
                    }
                    
                    display.updateStatus("Mengukur...");
                    Serial.println("Measurement started.");
                } else {
                    if (selectedMeasurementMode == 0) {
                        medSensor.stopHRMeasurement();
                        Serial.println("MAX30100 measurement paused");
                    } else if (selectedMeasurementMode == 1) {
                        bpSensor.stopMeasurement();
                        Serial.println("Blood Pressure measurement stopped");
                    }
                    display.updateStatus("Pengukuran Dihentikan.");
                    
                    if (selectedMeasurementMode == 0) {
                        display.updateMedicalRecordData(0, "OFF", 0, 0, 0.0, false);
                    } else if (selectedMeasurementMode == 1) {
                        display.updateBloodPressureData(0, 0, 0, false);
                    } else if (selectedMeasurementMode == 2) {
                        updateECGDisplay(0, 0.0, 0, "OFF", false);
                    } else if (selectedMeasurementMode == 3) {
                        updateEMGDisplay(0, 0, "OFF", false);
                    } else if (selectedMeasurementMode == 4) {
                        display.updateBodyPositionSnoreData(0, 0, false);
                    } else {
                        display.updateSensorData(0, "OFF", false);
                    }
                    Serial.println("Measurement stopped.");
                }
                
                resetActivityTimer();
            }
        }
        else if (btn == 3) {
            if (selectedMeasurementMode == 0 && medSensor.isMAX30100Available()) {
                sleepMAX30100IfLeavingMedicalRecord();
            }
            showMeasurementModeSelection();
        }
    }
    else if (inUserScreen) {
        if (btn == 90) {
            goToHomeScreen();
        }
        else if (btn == 91) {
            goToEditUserScreen();
        }
    }
    else if (inEditUserScreen) {
        if (btn == 92) {
            editingField = "name";
            editingUserIndex = selectedIndex;
            goToKeyboardScreen(deviceMgr.devices[selectedIndex].name, 
                             "Edit Nama User", true);
        }
        else if (btn == 93) {
            editingField = "token";
            editingUserIndex = selectedIndex;
            goToKeyboardScreen(deviceMgr.devices[selectedIndex].ubidotsToken, 
                             "Edit Ubidots Token", false);
        }
        else if (btn == 94) {
            editingField = "deviceLabel";
            editingUserIndex = selectedIndex;
            goToKeyboardScreen(deviceMgr.devices[selectedIndex].ubidotsDeviceLabel, 
                             "Edit Device Label", true);
        }
        else if (btn == 95) {
            showCurrentUser();
            inEditUserScreen = false;
            inUserScreen = true;
        }
    }
    else if (inKeyboardScreen) {
        if (!display.isKeyboardActive()) {
            String result = display.getKeyboardInput();
            
            if (display.isKeyboardCancelled()) {
                display.clearKeyboard();
                inKeyboardScreen = false;
                
                if (addingNewUser) {
                    addingNewUser = false;
                    tempUserName = "";
                    tempUserToken = "";
                    tempUserDeviceLabel = "";
                    showUserListScreen();
                } else if (editingField == "name" || editingField == "token" || editingField == "deviceLabel") {
                    goToEditUserScreen();
                } else if (editingField == "wifi_password") {
                    showWifiScanScreen();
                } else {
                    goToHomeScreen();
                }
            } else {
                if (editingField == "name") {
                    if (result.length() > 0) {
                        deviceMgr.editDevice(editingUserIndex, result, 
                                            deviceMgr.devices[editingUserIndex].ubidotsToken,
                                            deviceMgr.devices[editingUserIndex].ubidotsDeviceLabel);
                        display.updateStatus("Nama user berhasil diupdate!");
                        Serial.printf("Nama diupdate: %s\n", result.c_str());
                    }
                }
                else if (editingField == "token") {
                    if (result.length() > 0) {
                        deviceMgr.editDevice(editingUserIndex, 
                                            deviceMgr.devices[editingUserIndex].name, 
                                            result,
                                            deviceMgr.devices[editingUserIndex].ubidotsDeviceLabel);
                        display.updateStatus("Token berhasil diupdate!");
                        Serial.printf("Token diupdate: %s\n", result.c_str());
                    }
                }
                else if (editingField == "deviceLabel") {
                    if (result.length() > 0) {
                        deviceMgr.editDevice(editingUserIndex, 
                                            deviceMgr.devices[editingUserIndex].name, 
                                            deviceMgr.devices[editingUserIndex].ubidotsToken,
                                            result);
                        display.updateStatus("Device label berhasil diupdate!");
                        Serial.printf("Device label diupdate: %s\n", result.c_str());
                    }
                }
                else if (editingField == "newUser") {
                    if (result.length() > 0) {
                        bool confirm = display.showOnScreenConfirmation("LANJUT KE TOKEN?", 
                            "Nama: " + result + "\n\n" + "Lanjut ke input Ubidots Token?");

                        if (confirm) {
                            editingField = "newToken";
                            addingNewUser = true;
                            tempUserName = result;
                            goToKeyboardScreen("", "Ubidots Token untuk " + result, false);
                            return;
                        } else {
                            addingNewUser = false;
                            tempUserName = "";
                            tempUserToken = "";
                            tempUserDeviceLabel = "";
                            showUserListScreen();
                        }
                    }
                }
                else if (editingField == "newToken") {
                    if (result.length() > 0) {
                        bool confirm = display.showOnScreenConfirmation("LANJUT KE DEVICE LABEL?", 
                            "Nama: " + tempUserName + "\n" + 
                            "Token: " + result.substring(0, min(15, (int)result.length())) + 
                            "...\n\n" + "Lanjut ke input Device Label?");

                        if (confirm) {
                            editingField = "newDeviceLabel";
                            tempUserToken = result;
                            goToKeyboardScreen("", "Device Label untuk " + tempUserName, true);
                            return;
                        } else {
                            addingNewUser = false;
                            tempUserName = "";
                            tempUserToken = "";
                            tempUserDeviceLabel = "";
                            showUserListScreen();
                        }
                    }
                }
                else if (editingField == "newDeviceLabel") {
                    if (result.length() > 0) {
                        bool confirm = display.showOnScreenConfirmation("TAMBAH USER?", 
                            "Nama: " + tempUserName + "\n" + 
                            "Token: " + tempUserToken.substring(0, min(15, (int)tempUserToken.length())) + 
                            "...\n" +
                            "Device: " + result + "\n\n" + 
                            "Tambah user ini?");
                        
                        if (confirm) {
                            deviceMgr.addDevice(tempUserName, tempUserToken, result);
                            selectedIndex = deviceMgr.devices.size() - 1;
                            display.updateStatus("User berhasil ditambahkan!");
                            Serial.printf("User baru: %s\n", tempUserName.c_str());

                            tempUserName = "";
                            tempUserToken = "";
                            tempUserDeviceLabel = "";
                            addingNewUser = false;

                            showUserListScreen();
                        } else {
                            tempUserName = "";
                            tempUserToken = "";
                            tempUserDeviceLabel = "";
                            addingNewUser = false;
                            showUserListScreen();
                        }
                    }
                }
                else if (editingField == "wifi_password") {
                    if (result.length() > 0) {
                        wifiMgr.saveCredentials(selectedWifiSSID, result);
                        
                        display.updateStatus("Connecting to " + selectedWifiSSID + "...");
                        
                        bool connectionSuccess = false;
                        unsigned long connectionStartTime = millis();
                        unsigned long connectionTimeout = 10000;
                        
                        wifiMgr.connectSaved();
                        
                        while (millis() - connectionStartTime < connectionTimeout) {
                            if (WiFi.status() == WL_CONNECTED) {
                                connectionSuccess = true;
                                break;
                            }
                            delay(500);
                            Serial.print(".");
                        }
                        
                        if (connectionSuccess) {
                            display.updateStatus("Connected to " + selectedWifiSSID);
                            Serial.println("\nWiFi Connected: " + WiFi.localIP().toString());
                            
                            display.clearKeyboard();
                            inKeyboardScreen = false;
                            editingField = "";
                            
                            String successMsg = "WiFi Terhubung!\n";
                            successMsg += "SSID: " + selectedWifiSSID + "\n";
                            successMsg += "IP: " + WiFi.localIP().toString();
                            
                            showTemporaryMessage("WI-FI TERHUBUNG", successMsg, TFT_GREEN, TFT_WHITE, 3000);
                            
                            showWifiConfig();
                        } else {
                            display.updateStatus("Koneksi gagal! Password salah?");
                            Serial.println("\nWiFi connection failed!");
                            
                            display.clearKeyboard();
                            inKeyboardScreen = false;
                            editingField = "";
                            
                            String errorMsg = "Gagal terhubung ke:\n" + 
                                            selectedWifiSSID + "\n\n" +
                                            "Kemungkinan penyebab:\n" +
                                            "1. Password salah\n" +
                                            "2. Jaringan tidak ada\n" +
                                            "3. Signal lemah";
                            
                            showTemporaryMessage("KONEKSI GAGAL", errorMsg, TFT_RED, TFT_WHITE, 3000);
                            
                            showWifiScanScreen();
                        }
                    } else {
                        display.updateStatus("Password tidak boleh kosong!");
                        display.clearKeyboard();
                        inKeyboardScreen = false;
                        editingField = "";
                        delay(1000);
                        showWifiScanScreen();
                    }
                    
                    return;
                }
                display.clearKeyboard();
                inKeyboardScreen = false;
                
                if (editingField != "wifi_password") {
                    if (addingNewUser) {
                        addingNewUser = false;
                        showUserListScreen();
                    } else {
                        goToEditUserScreen();
                    }
                }
            }
        }
    }
    else if (inWifiScreen) {
        if (btn == 41) {
            showWifiScanScreen();
        }
        else if (btn == 90) {
            goToHomeScreen();
        }
    }
    else if (inWifiScanScreen) {
        if (btn >= 200 && btn < 300) {
            selectedWifiIndex = btn - 200;
            selectedWifiSSID = wifiMgr.getScannedSSID(selectedWifiIndex);
            
            String msg = "SSID: " + selectedWifiSSID + "\n";
            msg += "RSSI: " + String(wifiMgr.getScannedRSSI(selectedWifiIndex)) + " dBm\n";
            msg += "Enc: " + wifiMgr.getScannedEncryption(selectedWifiIndex) + "\n\n";
            msg += "Connect to this network?";
            
            bool confirm = display.showOnScreenConfirmation("CONNECT TO WiFi", msg);
            
            if (confirm) {
                if (wifiMgr.getScannedEncryption(selectedWifiIndex) == "Open") {
                    wifiMgr.saveCredentials(selectedWifiSSID, "");
                    display.updateStatus("Connecting to open network...");
                    wifiMgr.connectSaved();
                    
                    delay(3000);
                    
                    if (WiFi.status() == WL_CONNECTED) {
                        display.updateStatus("Connected to open network!");
                        showWifiConfig();
                    } else {
                        display.updateStatus("Failed to connect!");
                        showWifiScanScreen();
                    }
                } else {
                    editingField = "wifi_password";
                    goToKeyboardScreen("", "Password for: " + selectedWifiSSID, false);
                }
            } else {
                showWifiScanScreen();
            }
        }
        else if (btn == 71) {
            display.updateStatus("Scanning WiFi networks...");
            showWifiScanScreen();
        }
        else if (btn == 90) {
            Serial.println("Kembali dari WiFi Scan Screen");
            
            inWifiScanScreen = false;
            
            if (wifiMgr.hasCredentials()) {
                Serial.println("Mencoba reconnect ke WiFi yang tersimpan...");
                display.updateStatus("Reconnecting to saved WiFi...");
                
                WiFi.mode(WIFI_STA);
                WiFi.setAutoReconnect(true);
                
                delay(1000);
                
                wifiMgr.connectSaved();
                
                unsigned long startTime = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
                    delay(500);
                    Serial.print(".");
                }
                
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("\nWiFi reconnected!");
                    display.updateStatus("WiFi reconnected: " + WiFi.localIP().toString());
                } else {
                    Serial.println("\nWiFi reconnect failed!");
                    display.updateStatus("WiFi reconnect failed");
                }
            }
            
            showWifiConfig();
        }
        else if (btn == 81 || btn == 82) {
        }
    }
    else if (inUserListScreen) {
        if (btn >= 100) {
            int newIndex = btn - 100;
            if (newIndex >= 0 && newIndex < deviceMgr.devices.size()) {
                selectedIndex = newIndex;
                Serial.printf("User dipilih: %s (Index: %d)\n", 
                             deviceMgr.devices[selectedIndex].name.c_str(), 
                             selectedIndex);

                goToHomeScreen();
            }
        }
        else if (btn == 90) {
            goToHomeScreen();
        }
        else if (btn == 80 || btn == 81) {
        }
        else if (btn == 95) {
            addingNewUser = true;
            editingField = "newUser";
            goToKeyboardScreen("", "Nama User Baru", true);
        }
        else if (btn == 96) {
            if (deviceMgr.devices.size() > 0) {
                String userName = deviceMgr.devices[selectedIndex].name;
                if (userName.length() > 20) {
                    userName = userName.substring(0, 17) + "...";
                }
                
                bool confirmed = display.showOnScreenConfirmation("HAPUS USER?", "Yakin hapus user:\n" + userName + "?\n\n" + "Aksi ini tidak dapat dibatalkan!");

                if (confirmed) {
                    deviceMgr.removeDevice(selectedIndex);
                    display.updateStatus("User berhasil dihapus!");
                    Serial.println("User dihapus!");

                    if (deviceMgr.isEmpty()) {
                        selectedIndex = 0;
                        goToHomeScreen();
                    } else {
                        if (selectedIndex >= deviceMgr.devices.size()) {
                            selectedIndex = deviceMgr.devices.size() - 1;
                        }
                        std::vector<String> userNames;
                        for (const auto& device : deviceMgr.devices) {
                            userNames.push_back(device.name);
                        }
                        display.showUserList(userNames, selectedIndex, false);
                    }
                } else {
                    display.updateStatus("Penghapusan dibatalkan");
                    Serial.println("Penghapusan dibatalkan");
                    std::vector<String> userNames;
                    for (const auto& device : deviceMgr.devices) {
                        userNames.push_back(device.name);
                    }
                    display.showUserList(userNames, selectedIndex, false);
                }
            }
        }
    }
    else if (inModeSelectionScreen) {
        if (btn >= 200 && btn <= 204) {
            int modeIndex = btn - 200;
            goToMeasurementScreenWithMode(modeIndex);
        }
        else if (btn == 90) {
            inModeSelectionScreen = false;
            goToHomeScreen();
        }
    }
    
    // Measurement logic untuk semua mode
    if (measuring && inMeasurementScreen) {
        if (selectedMeasurementMode == 0) {
            int edaValue = medSensor.readEDARaw();
            String stressLevel = medSensor.getStressLevel(edaValue);
            
            if (medSensor.isMAX30100Available() && medSensor.isMAX30100Sleeping()) {
                wakeUpMAX30100IfNeeded();
            }
            
            float heartRate = 0.0;
            int spo2 = 0;
            
            if (medSensor.isMAX30100Available() && !medSensor.isMAX30100Sleeping()) {
                float hrTemp = medSensor.getHeartRate();
                int spo2Temp = medSensor.getSpO2();
                
                if (hrTemp > 30.0 && hrTemp < 250.0) {
                    heartRate = hrTemp;
                }
                if (spo2Temp > 70 && spo2Temp <= 100) {
                    spo2 = spo2Temp;
                }
            }
            
            float objectTemp = 0.0;
            
            if (medSensor.isMLX90614Available() && !medSensor.isMLX90614Sleeping()) {
                float temp = medSensor.readObjectTemp();
                
                if (temp > 25.0 && temp < 45.0) {
                    objectTemp = temp;
                    lastObjectTemp = objectTemp;
                }
            }
            
            lastHeartRate = (int)heartRate;
            lastSpO2 = spo2;
            
            display.updateMedicalRecordData(edaValue, stressLevel, lastHeartRate, lastSpO2,
                                        lastObjectTemp, false);
            
            static unsigned long lastDebug = 0;
            if (millis() - lastDebug > 5000) {
                float conductance = medSensor.getEDAConductance(edaValue);
                Serial.printf("Medical Record: EDA=%d (%.2f µS), HR=%.1f BPM, SpO2=%d%%, Temp=%.2f°C\n", 
                            edaValue, conductance, heartRate, spo2, objectTemp);
                lastDebug = millis();
            }
            
            if (!deviceMgr.isEmpty()) {
                sendMedicalRecordToCloud(edaValue, lastHeartRate, lastSpO2, lastObjectTemp);
            }
            
            delay(100);
        } 
        else if (selectedMeasurementMode == 1) {
            bpSensor.update();
            
            if (bpSensor.isDataAvailable()) {
                if (bpSensor.hasValidData()) {
                    lastSystolic = bpSensor.getSystolic();
                    lastDiastolic = bpSensor.getDiastolic();
                    lastBPHeartRate = bpSensor.getBPM();
                    
                    display.updateBloodPressureData(lastSystolic, lastDiastolic, lastBPHeartRate, false);
                    
                    Serial.printf("Blood Pressure: %d/%d mmHg, HR: %d bpm\n", 
                                lastSystolic, lastDiastolic, lastBPHeartRate);
                    
                    if (!deviceMgr.isEmpty()) {
                        sendBloodPressureToCloud(lastSystolic, lastDiastolic, lastBPHeartRate);
                    }
                }
                
                bpSensor.resetDataFlag();
            }
            
            delay(50);
        }
        else if (selectedMeasurementMode == 2) {
            int ecgRaw = ecgSensor.readRawECG();
            
            ecgSensor.updateFilter();
            float ecgFiltered = ecgSensor.getFilteredValue();
            
            int ecgHeartRate = ecgSensor.getHeartRateBPM();
            
            lastECGRaw = ecgRaw;
            lastECGVoltage = ecgSensor.getECGVoltage();
            lastECGHeartRate = ecgHeartRate;
            lastECGLeadStatus = "Leads OK";
            
            updateECGDisplay(ecgRaw, lastECGVoltage, lastECGHeartRate, "Leads OK", false);
            
            static unsigned long lastECGDebug = 0;
            if (millis() - lastECGDebug > 2000) {
                Serial.printf("ECG: Raw=%d, Filtered=%.1f, Voltage=%.2fV, HR=%d BPM, Threshold=%d\n",
                            ecgRaw, ecgFiltered, lastECGVoltage, ecgHeartRate, 
                            ecgSensor.getUpperThreshold());
                lastECGDebug = millis();
            }
            
            if (ecgHeartRate > 0 && !deviceMgr.isEmpty()) {
                sendECGToCloud(ecgHeartRate);
            } else if (ecgHeartRate == 0) {
                display.updateStatus("Tunggu detak jantung...");
            }
            
            delay(10);
        }
        else if (selectedMeasurementMode == 3) {
            emgSensor.update();
            
            lastEMGAmplitude_mV = emgSensor.getAmplitudemV();
            lastEMGActivation_percent = emgSensor.getActivationPercent();
            lastEMGLevel = emgSensor.getEMGLevel();
            
            // Konversi amplitudo mV ke nilai untuk grafik (0-3000 mV -> 0-3000)
            int emgGraphValue = constrain((int)lastEMGAmplitude_mV, 0, 3000);
            
            // Simpan ke buffer circular (400 titik) - sama seperti ECG
            emgSimpleBuffer[emgSimpleIndex % 400] = emgGraphValue;
            emgSimpleIndex++;
            
            updateEMGDisplay(lastEMGAmplitude_mV, lastEMGActivation_percent, 
                           lastEMGLevel, false);
            
            static unsigned long lastEMGDebug = 0;
            if (millis() - lastEMGDebug > 2000) {
                Serial.printf("EMG: Amp=%.2f mV, Act=%d%%, Level=%s, Buffer=%d/400\n", 
                            lastEMGAmplitude_mV, lastEMGActivation_percent, 
                            lastEMGLevel.c_str(), min(emgSimpleIndex, 400));
                lastEMGDebug = millis();
            }
            
            if (!deviceMgr.isEmpty() && lastEMGAmplitude_mV >= 10.0f) {
                sendEMGToCloud(lastEMGAmplitude_mV);
            }
            
            if (!emgSensor.isCalibrated()) {
                display.updateStatus("Kalibrasi EMG... Jangan gerakkan sensor");
            } else {
                display.updateStatus(String("EMG: ") + 
                                String(lastEMGAmplitude_mV, 1) + " mV | " +
                                String(lastEMGActivation_percent) + "% Act");
            }
            
            delay(10);
        }
        else if (selectedMeasurementMode == 4) {
            bodyPositionSnoreSensor.update();
            
            lastBodyPosition = bodyPositionSnoreSensor.getPosition();
            lastSnoreAmplitude = bodyPositionSnoreSensor.getSnoreAmplitude();
            
            display.updateBodyPositionSnoreData(lastBodyPosition, lastSnoreAmplitude, false);
            
            static unsigned long lastDebug = 0;
            if (millis() - lastDebug > 2000) {
                Serial.printf("Body Position: %d (%s), Snore Amplitude: %d\n", 
                            lastBodyPosition, 
                            bodyPositionSnoreSensor.getPositionName().c_str(),
                            lastSnoreAmplitude);
                lastDebug = millis();
            }
            
            if (!deviceMgr.isEmpty() && (lastBodyPosition > 0 || lastSnoreAmplitude > 0)) {
                sendBodyPositionSnoreToCloud(lastBodyPosition, lastSnoreAmplitude);
            }
            
            delay(100);
        }
        else {
            delay(100);
        }
        
        resetActivityTimer();
    } else {
        delay(50);
    }
    
    handleSerialCommands();
}