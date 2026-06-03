
## 🧪 TESTING & VERIFICATION

### 1. Compile & Upload

Tambahkan dependencies di `platformio.ini`:

```ini
# File ini sudah include semua library yang diperlukan
# Tidak perlu tambahan library untuk multi-cloud support
```

Compile:
```bash
pio run
```

Upload:
```bash
pio run --target upload
```

---

### 2. Test via Serial Monitor

Buka Serial Monitor (115200 baud):

```
menu
testcloud      # Test koneksi ke semua cloud yang aktif
cloudstatus    # Lihat status CloudBroker
addcloud       # Tambah user dengan multi-cloud
list           # Lihat semua user dengan cloud provider
```

**Expected Output:**
```
[CloudBroker] Initializing...
  Provider Type: 3
  Ubidots: Enabled
  ThingsBoard: Enabled
[CloudBroker] Initialization complete

[CloudBroker] Testing connection for 2 provider(s)
[CloudBroker] Testing Ubidots...
  Result: OK
  Message: Connection OK (Code: 200)
[CloudBroker] Testing ThingsBoard...
  Result: OK
  Message: Connection OK (Code: 200)
```

---

### 3. Test Data Upload

1. Masuk ke **Measurement Mode**
2. Lakukan pengukuran (EDA, HR, SpO2, etc)
3. Monitor Serial output:

```
[CloudBroker] Sending data to 2 provider(s)
[CloudBroker] -> Ubidots
[Ubidots] Upload successful: 200
[CloudBroker] -> ThingsBoard
[ThingsBoard] Upload successful: 200
[CloudBroker] Upload complete: All uploads successful
```

4. Verify di Dashboard:
   - **Ubidots:** https://industrial.ubidots.com/app/devices
   - **ThingsBoard:** https://demo.thingsboard.io/devices

---

## 🐛 TROUBLESHOOTING

### Problem: "CloudBroker not ready"
**Cause:** User tidak memiliki credentials untuk cloud yang dipilih
**Solution:**
```
addcloud    # Tambah user dengan credentials lengkap
```

### Problem: "Auth error (Code: 401)"
**Cause:** Token salah atau expired
**Solution:**
1. Verify token di dashboard cloud
2. Update credentials:
```
edit [nama] [ubidots_token] [ubidots_label]
```

### Problem: "Connection failed (Code: -1)"
**Cause:** WiFi tidak terhubung atau DNS issue
**Solution:**
1. Check WiFi: `status`
2. Reconnect: `wifi`
3. Ping test: `testcloud`

### Problem: Data hanya masuk ke satu platform
**Cause:** Cloud provider setting salah
**Solution:**
1. Check config: `list`
2. Pastikan `cloudProvider = 3` untuk dual upload
3. Edit user dengan `addcloud` atau manual edit config.json

### Problem: "Rate limited"
**Cause:** Upload terlalu cepat (<15 detik interval)
**Solution:** Normal behavior. Data akan dikirim setelah interval habis.

---

## 📈 MONITORING & ANALYTICS

### Ubidots Dashboard
1. Login: https://industrial.ubidots.com
2. Go to: **Devices** → Your Device
3. View variables: `eda`, `bpm_general`, `spo2`, `temperature`, etc
4. Create Dashboard: **Data** → **Dashboards** → **Add Widget**

### ThingsBoard Dashboard
1. Login: https://demo.thingsboard.io
2. Go to: **Devices** → Your Device → **Latest Telemetry**
3. View keys: `eda`, `bpm_general`, `spo2`, `temperature`, etc
4. Create Dashboard: **Dashboards** → **Add New Dashboard** → **Add Widget**

---

## 🔐 SECURITY NOTES

### Token Management
- ⚠️ Tokens disimpan di SPIFFS dalam **plaintext**
- 🔒 Gunakan **HTTPS** untuk semua upload (port 443)
- 🚫 **Jangan share** config.json atau token ke publik
- 🔄 **Rotate tokens** secara berkala

### ThingsBoard Security
- Gunakan **Access Token** untuk device authentication
- Jangan gunakan username/password di ESP32
- Set device permissions di ThingsBoard dashboard

---

## 🚀 NEXT STEPS

### Phase 1: Migration Complete ✅
- [x] Multi-cloud architecture
- [x] Backward compatibility
- [x] Dual upload support

### Phase 2: Advanced Features (Optional)
- [ ] MQTT support untuk ThingsBoard (lebih efisien)
- [ ] Local caching saat offline
- [ ] Batch upload untuk menghemat bandwidth
- [ ] Dashboard pada Display Manager
- [ ] Cloud provider selection via touchscreen

### Phase 3: Production Ready
- [ ] Token encryption
- [ ] OTA updates
- [ ] Error recovery & retry logic
- [ ] Telemetry compression

---

## 📞 SUPPORT

Jika ada pertanyaan atau issue:
1. Check logs via Serial Monitor
2. Verify credentials di cloud dashboard
3. Test connection: `testcloud`
4. Review troubleshooting section

**Happy Multi-Cloud Monitoring! 🎉**
