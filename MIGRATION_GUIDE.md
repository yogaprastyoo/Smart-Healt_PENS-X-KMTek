# 🚀 PANDUAN MIGRASI MULTI-CLOUD: Ubidots + ThingsBoard

## 📊 RINGKASAN

Proyek ini telah **berhasil di-upgrade** untuk mendukung **multi-cloud platform** dengan arsitektur yang **fleksibel** dan **backward compatible**.

### ✅ Apa yang Sudah Dibuat?

1. **CloudProvider Interface** - Abstraksi untuk semua cloud platforms
2. **UbidotsProvider** - Wrapper existing Ubidots API
3. **ThingsBoardProvider** - Support untuk ThingsBoard platform
4. **CloudBroker** - Manager untuk multi-cloud coordination
5. **DeviceManager Update** - Support per-user cloud preference
6. **Config.h Update** - Konfigurasi ThingsBoard

### 🎯 Fitur Utama

- ✅ **Backward Compatible** - Kode lama tetap jalan
- ✅ **Per-User Cloud Selection** - Setiap user bisa pilih platform berbeda
- ✅ **Dual Upload** - Kirim data ke Ubidots DAN ThingsBoard bersamaan
- ✅ **Easy Migration** - Tinggal update credentials
- ✅ **Extensible** - Mudah tambah provider baru (AWS, Azure, dll)

---

## 📁 FILE YANG DIBUAT

### 1. CloudProvider.h
**Lokasi:** `include/CloudProvider.h`
**Fungsi:** Interface abstrak untuk semua cloud provider

### 2. UbidotsProvider.h/cpp
**Lokasi:** `include/UbidotsProvider.h`, `src/UbidotsProvider.cpp`
**Fungsi:** Implementasi Ubidots API (sama seperti kode existing)

### 3. ThingsBoardProvider.h/cpp
**Lokasi:** `include/ThingsBoardProvider.h`, `src/ThingsBoardProvider.cpp`
**Fungsi:** Implementasi ThingsBoard REST API

### 4. CloudBroker.h/cpp
**Lokasi:** `include/CloudBroker.h`, `src/CloudBroker.cpp`
**Fungsi:** Manager untuk koordinasi multi-cloud

### 5. DeviceManager.h/cpp (Updated)
**Lokasi:** `include/DeviceManager.h`, `src/DeviceManager.cpp`
**Changes:** Tambah field `thingsboardToken`, `thingsboardDeviceLabel`, `cloudProvider`

### 6. Config.h (Updated)
**Lokasi:** `include/Config.h`
**Changes:** Tambah `THINGSBOARD_SERVER`, `THINGSBOARD_PORT`, `THINGSBOARD_TOKEN`

---

## 🔧 CARA MENGGUNAKAN

### A. SETUP THINGSBOARD

#### 1. Buat Akun ThingsBoard
- **Demo Server (Gratis):** https://demo.thingsboard.io
- **Cloud (Paid):** https://thingsboard.cloud
- **Self-Hosted:** Install sendiri

#### 2. Buat Device di ThingsBoard
1. Login ke ThingsBoard
2. Pergi ke **Devices** → **Add Device**
3. Masukkan Device Name (misal: "ESP32-Health-Monitor")
4. **Copy Access Token** (akan digunakan nanti)

#### 3. Setup Variable Keys
ThingsBoard menggunakan **Telemetry Keys**. Gunakan nama yang sama dengan Ubidots:
- `eda` - EDA value
- `bpm_general` - Heart rate
- `spo2` - Blood oxygen
- `temperature` - Body temperature
- `systolic` - Blood pressure systolic
- `diastolic` - Blood pressure diastolic
- `body_position` - Body position code
- `snore_level` - Snore amplitude
- `emg_amplitude` - EMG value
- `ecg_heart_rate` - ECG heart rate

---

### B. CARA MIGRASI (PILIHAN)

#### **OPSI 1: HANYA UBIDOTS (Tidak Ada Perubahan)**
Kode existing tetap berjalan normal. Tidak perlu modifikasi.

#### **OPSI 2: HANYA THINGSBOARD**
Migrasi penuh dari Ubidots ke ThingsBoard.

#### **OPSI 3: UBIDOTS + THINGSBOARD (RECOMMENDED)**
Kirim data ke kedua platform secara bersamaan.

---

## 📝 IMPLEMENTASI DI MAIN.CPP

### Step 1: Include Headers Baru

Tambahkan di bagian atas main.cpp:

```cpp
#include "CloudBroker.h"

// Global CloudBroker instance
CloudBroker cloudBroker;
```

---

### Step 2: Inisialisasi CloudBroker di setup()

Tambahkan setelah `deviceMgr.begin()`:

```cpp
void setup() {
    // ... existing code ...
    
    deviceMgr.begin();
    
    // Initialize CloudBroker setelah device manager ready
    if (!deviceMgr.isEmpty()) {
        Device& currentDevice = deviceMgr.devices[selectedIndex];
        
        // Cast cloudProvider int ke CloudProviderType enum
        CloudProviderType providerType = (CloudProviderType)currentDevice.cloudProvider;
        
        cloudBroker.begin(
            providerType,
            currentDevice.ubidotsToken,
            currentDevice.ubidotsDeviceLabel,
            currentDevice.thingsboardToken,
            currentDevice.thingsboardDeviceLabel
        );
        
        Serial.println("[Setup] CloudBroker initialized");
    }
    
    // ... rest of setup ...
}
```

---

### Step 3: Ganti sendToUbidots Functions

**SEBELUM (OLD CODE):**
```cpp
void sendMedicalRecordToUbidots(int edaValue, int heartRate, int spo2, 
                                float objectTemp,
                                const String &token, const String &deviceLabel) {
    // ... HTTP client code ...
}
```

**SESUDAH (NEW CODE):**
```cpp
void sendMedicalRecordToCloud(int edaValue, int heartRate, int spo2, float objectTemp) {
    if (!cloudBroker.isReady()) {
        Serial.println("[Cloud] CloudBroker not ready");
        return;
    }
    
    // Build sensor data
    SensorData data;
    if (edaValue > 0) {
        data.addValue("eda", edaValue);
    }
    if (heartRate > 30 && heartRate < 250) {
        data.addValue("bpm_general", heartRate);
    }
    if (spo2 > 70 && spo2 <= 100) {
        data.addValue("spo2", spo2);
    }
    if (objectTemp > 25.0 && objectTemp < 45.0) {
        data.addValue("temperature", objectTemp);
    }
    
    // Send to cloud(s)
    auto results = cloudBroker.sendData(data);
    
    // Display results
    for (const auto& result : results) {
        if (result.status == CLOUD_SUCCESS) {
            display.updateStatus(result.providerName + ": OK");
            Serial.println("[Cloud] " + result.providerName + " upload successful");
        } else {
            display.updateStatus(result.providerName + ": Failed");
            Serial.println("[Cloud] " + result.providerName + " upload failed: " + result.message);
        }
    }
}
```

---

### Step 4: Update All sendTo* Function Calls

Ganti semua pemanggilan fungsi lama:

**LAMA:**
```cpp
sendMedicalRecordToUbidots(edaValue, heartRate, spo2, objectTemp, 
                           deviceMgr.devices[selectedIndex].ubidotsToken,
                           deviceMgr.devices[selectedIndex].ubidotsDeviceLabel);
```

**BARU:**
```cpp
sendMedicalRecordToCloud(edaValue, heartRate, spo2, objectTemp);
```

---

### Step 5: Tambah Serial Command untuk Test

Tambahkan di `handleSerialCommands()`:

```cpp
else if (cmd == "testcloud") {
    Serial.println("\n=== TEST MULTI-CLOUD CONNECTION ===");
    
    if (!cloudBroker.isReady()) {
        Serial.println("CloudBroker not ready");
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
    Serial.println("Provider Type: " + String(cloudBroker.getProviderType()));
    Serial.println("Status: " + cloudBroker.getStatusMessage());
    Serial.println("Ubidots: " + cloudBroker.getUbidotsStatus());
    Serial.println("ThingsBoard: " + cloudBroker.getThingsBoardStatus());
}
```

---

## 🔄 UPDATE USER MANAGEMENT

### Tambah Command untuk Multi-Cloud User

Tambahkan di `handleSerialCommands()`:

```cpp
else if (cmd == "addcloud") {
    Serial.println("\n=== TAMBAH USER DENGAN MULTI-CLOUD ===");
    
    Serial.print("Nama User: ");
    String name = readLineFromSerial(30000);
    if (name.length() == 0) return;
    
    Serial.println("\nCloud Provider:");
    Serial.println("1 - Ubidots only");
    Serial.println("2 - ThingsBoard only");
    Serial.println("3 - Both (Ubidots + ThingsBoard)");
    Serial.print("Pilih (1-3): ");
    String provStr = readLineFromSerial(15000);
    int cloudProvider = provStr.toInt();
    
    String ubidotsToken = "";
    String ubidotsLabel = "";
    String thingsboardToken = "";
    String thingsboardLabel = "";
    
    if (cloudProvider == 1 || cloudProvider == 3) {
        Serial.print("Ubidots Token: ");
        ubidotsToken = readLineFromSerial(30000);
        Serial.print("Ubidots Device Label: ");
        ubidotsLabel = readLineFromSerial(30000);
    }
    
    if (cloudProvider == 2 || cloudProvider == 3) {
        Serial.print("ThingsBoard Token: ");
        thingsboardToken = readLineFromSerial(30000);
        Serial.print("ThingsBoard Device Label (optional): ");
        thingsboardLabel = readLineFromSerial(30000);
    }
    
    deviceMgr.addDeviceWithCloud(name, 
                                 ubidotsToken, ubidotsLabel,
                                 thingsboardToken, thingsboardLabel,
                                 cloudProvider);
    
    Serial.println("✓ User dengan multi-cloud berhasil ditambahkan!");
}
```

---

## 📊 CONTOH CONFIG.JSON

Setelah migrasi, file `config.json` akan terlihat seperti ini:

```json
{
  "devices": [
    {
      "name": "User Ubidots Only",
      "ubidotsToken": "BBFF-xxxxxxxxxxxxxx",
      "ubidotsDeviceLabel": "esp32-health-001",
      "thingsboardToken": "",
      "thingsboardDeviceLabel": "",
      "cloudProvider": 1
    },
    {
      "name": "User ThingsBoard Only",
      "ubidotsToken": "",
      "ubidotsDeviceLabel": "",
      "thingsboardToken": "A1B2C3D4E5F67890",
      "thingsboardDeviceLabel": "ESP32-Monitor",
      "cloudProvider": 2
    },
    {
      "name": "User Multi-Cloud",
      "ubidotsToken": "BBFF-xxxxxxxxxxxxxx",
      "ubidotsDeviceLabel": "esp32-health-002",
      "thingsboardToken": "A1B2C3D4E5F67890",
      "thingsboardDeviceLabel": "ESP32-Monitor-2",
      "cloudProvider": 3
    }
  ]
}
```

