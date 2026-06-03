#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- KONFIGURASI PIN ---
#define EDA_PIN 36               // Pin sensor EDA (ADC1_CH0 / GPIO36)
#define MAX30100_SDA 21          // I2C SDA untuk MAX30100
#define MAX30100_SCL 22          // I2C SCL untuk MAX30100

// --- PIN ECG AD8232 ---
#define ECG_OUTPUT 39            // OUTPUT pin (Analog ECG Signal)

// --- PIN I2C untuk Touchscreen ---
#define TOUCH_SDA 21
#define TOUCH_SCL 22

// --- PIN untuk Tensimeter (Blood Pressure) ---
#define BP_RX_PIN 16
#define BP_TX_PIN 17

// --- PIN untuk Body Position & Snore Sensor ---
#define MPU6050_SDA 25          // I2C SDA untuk MPU6050 (sama dengan yang lain)
#define MPU6050_SCL 26          // I2C SCL untuk MPU6050
#define SOUND_SENSOR_PIN 34     // GPIO34 untuk sound sensor

// --- KONFIGURASI PIN EMG ---
#define EMG_PIN 35

// --- KONFIGURASI FILE SYSTEM ---
#define CONFIG_PATH "/config.json"

// --- KONFIGURASI SERIAL ---
#define SERIAL_BAUD 115200

// --- KONFIGURASI UBIDOTS ---
#define UBIDOTS_SERVER "industrial.api.ubidots.com"
#define UBIDOTS_PORT 443
#define UBIDOTS_TOKEN ""  // Token default kosong, akan diisi per device
#define UBIDOTS_DEVICE_LABEL "" // Device label default, akan diisi per device
#define UBIDOTS_VARIABLE_LABEL "eda" // Nama variabel untuk EDA data
#define UBIDOTS_HR_VARIABLE "bpm_general" // Variabel untuk heart rate
#define UBIDOTS_SPO2_VARIABLE "spo2" // Variabel untuk SpO2
#define UBIDOTS_TEMP_VARIABLE "temperature" // Variabel untuk suhu tubuh
#define UBIDOTS_AMBIENT_TEMP_VARIABLE "ambient_temperature" // Variabel untuk suhu ruang
#define UBIDOTS_ECG_VARIABLE "ecg" // Variabel untuk ECG data (BARU)
#define UBIDOTS_BP_SYSTOLIC_VARIABLE "systolic"
#define UBIDOTS_BP_DIASTOLIC_VARIABLE "diastolic"
#define UBIDOTS_BP_HR_VARIABLE "bpm_general"
#define UBIDOTS_POSITION_VARIABLE "body_position"
#define UBIDOTS_SNORE_VARIABLE "snore_level"
#define UBIDOTS_EMG_VARIABLE "emg_amplitude"  // Variabel untuk data EMG (Windowed Range)
#define EMG_WINDOW_SIZE 50      // Jumlah sampel untuk window (50 sampel = 500ms @ 100Hz)
#define EMG_MV_PER_COUNT 0.8f   
#define EMG_MAX_MV 3000.0f      
#define UBIDOTS_MIN_INTERVAL_MS 15000UL

// --- KONFIGURASI THINGSBOARD ---
#define THINGSBOARD_SERVER "demo.thingsboard.io"  // Default demo server
#define THINGSBOARD_PORT 80                        // HTTP port (443 untuk HTTPS)
#define THINGSBOARD_TOKEN ""                       // Access token, akan diisi per device
#define THINGSBOARD_DEVICE_LABEL ""                // Optional device label

// --- KONFIGURASI NVS (MEMORY) ---
#define PREF_NAMESPACE "shp"

#endif