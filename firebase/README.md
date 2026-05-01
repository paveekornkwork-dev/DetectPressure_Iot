# 🔥 คู่มือตั้งค่า Firebase — ระบบตรวจจับแรงกด

> คู่มือแบบ Step-by-Step สำหรับตั้งค่า Firebase Realtime Database ให้ระบบ Pressure Monitor

---

## 📝 สิ่งที่ต้องมีก่อนเริ่ม

- บัญชี Google (Gmail)
- Browser (Chrome แนะนำ)

---

## ขั้นตอนที่ 1: สร้าง Firebase Project

1. เปิดเว็บ **[https://console.firebase.google.com](https://console.firebase.google.com)**
2. Login ด้วย Gmail ของคุณ
3. คลิกปุ่ม **"Add project"** (เพิ่มโปรเจค)
4. ตั้งชื่อโปรเจค เช่น `pressure-monitor`
5. **Google Analytics** → ปิดก็ได้ (ไม่จำเป็นต้องใช้) → คลิก Toggle ปิด
6. คลิก **"Create Project"** → รอสักครู่
7. คลิก **"Continue"** เมื่อสร้างเสร็จ

---

## ขั้นตอนที่ 2: เปิดใช้ Realtime Database

1. ที่เมนูด้านซ้าย คลิก **Build → Realtime Database**
2. คลิกปุ่ม **"Create Database"**
3. เลือก Location:
   - แนะนำ **Singapore (`asia-southeast1`)** (ใกล้ประเทศไทยที่สุด)
4. เลือก Security Rules:
   - เลือก **"Start in test mode"** (เราจะมาตั้งค่า Rules เองทีหลัง)
5. คลิก **"Enable"**

✅ จะได้ URL ของ Database เช่น:
```
https://pressure-monitor-xxxxx-default-rtdb.asia-southeast1.firebasedatabase.app
```
> **คัดลอก URL นี้ไว้!** จะใช้ใส่ใน `config.h` (ค่า `FIREBASE_HOST`)

---

## ขั้นตอนที่ 3: เปิดใช้ Authentication (ยืนยันตัวตน)

เราต้องสร้าง "บัญชีผู้ใช้" สำหรับ ESP32 เพื่อให้เขียนข้อมูลเข้า Database ได้

1. ที่เมนูด้านซ้าย คลิก **Build → Authentication**
2. คลิก **"Get started"**
3. ในแท็บ **Sign-in method** → คลิก **"Email/Password"**
4. คลิก Toggle **"Enable"** ให้เปิด → คลิก **"Save"**
5. ไปที่แท็บ **"Users"** → คลิก **"Add user"**
6. กรอกข้อมูล:
   - **Email:** `esp32@pressure-monitor.com` (ใช้อีเมลอะไรก็ได้ ไม่ต้องมีจริง)
   - **Password:** ตั้งรหัสผ่าน เช่น `Esp32Secure!2024`
7. คลิก **"Add user"**

✅ **คัดลอก Email และ Password** → จะใช้ใส่ใน `config.h`:
```c
#define FIREBASE_USER_EMAIL    "esp32@pressure-monitor.com"
#define FIREBASE_USER_PASSWORD "Esp32Secure!2024"
```

---

## ขั้นตอนที่ 4: คัดลอก API Key

1. คลิก **ไอคอนรูปเฟือง ⚙️** ที่อยู่ข้างๆ "Project Overview" (มุมซ้ายบน)
2. เลือก **"Project settings"**
3. ในแท็บ **"General"** → เลื่อนลงหา **"Web API Key"**

✅ **คัดลอก Web API Key** → จะใช้ใส่ใน `config.h`:
```c
#define FIREBASE_API_KEY   "AIzaSyXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
```

---

## ขั้นตอนที่ 5: ตั้งค่า Security Rules

1. กลับไปที่ **Build → Realtime Database**
2. คลิกแท็บ **"Rules"**
3. **ลบ Rules เดิมทิ้งทั้งหมด** แล้ววาง Rules นี้แทน:

```json
{
  "rules": {
    "sensors": {
      "$deviceId": {
        "current": {
          ".read": true,
          ".write": "auth != null"
        },
        "history": {
          ".read": true,
          ".write": "auth != null",
          ".indexOn": ["timestamp"]
        },
        "status": {
          ".read": true,
          ".write": "auth != null"
        },
        "alerts": {
          ".read": true,
          ".write": "auth != null",
          ".indexOn": ["timestamp"]
        }
      }
    }
  }
}
```

4. คลิก **"Publish"**

> 💡 **อธิบาย:** Rules นี้หมายความว่า:
> - **ใครก็อ่านได้** (Dashboard ไม่ต้อง Login)
> - **ต้อง Login ถึงจะเขียนได้** (ESP32 ต้องยืนยันตัวตน)

---

## ขั้นตอนที่ 6: นำค่าไปใส่ใน config.h

เปิดไฟล์ `esp32/config.h` แล้วแก้ไขค่าเหล่านี้:

```c
// ==================== Firebase ====================
#define FIREBASE_HOST      "pressure-monitor-xxxxx-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_API_KEY   "AIzaSyXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define FIREBASE_USER_EMAIL    "esp32@pressure-monitor.com"
#define FIREBASE_USER_PASSWORD "Esp32Secure!2024"
```

> ⚠️ `FIREBASE_HOST` **ไม่ต้องใส่ `https://`** ที่หน้า URL

---

## ขั้นตอนที่ 7: นำค่าไปใส่ใน Dashboard

1. เปิด `dashboard/index.html` ใน Browser
2. คลิกไอคอน ⚙️ ที่มุมขวาบน
3. กรอก:
   - **API Key:** ค่า Web API Key เดียวกัน
   - **Database URL:** URL ของ Realtime Database (ใส่ `https://` ได้เลย)
   - **Device ID:** `esp32_node_01` (ต้องตรงกับใน `config.h`)
4. คลิก **"Connect"**

---

## 🗂️ โครงสร้างข้อมูลใน Database

เมื่อ ESP32 ส่งข้อมูลมาแล้ว ข้อมูลจะมีโครงสร้างแบบนี้:

```
/sensors
  /esp32_node_01
    /current            ← ค่าปัจจุบัน (อัปเดตทุก 2 วินาที)
      weight_kg: 1.25
      pressure_kpa: 122.6
      adc_raw: 1024
      timestamp: 1710612345000
    /status             ← สถานะอุปกรณ์ (อัปเดตทุก 10 วินาที)
      online: true
      last_seen: 1710612345000
      ip: "192.168.1.100"
      rssi: -45
      uptime_sec: 3600
    /history            ← ประวัติย้อนหลัง
      /-Nxyz123abc
        weight_kg: 1.25
        timestamp: 1710612345000
    /alerts             ← ประวัติการแจ้งเตือน
      /-Nxyz456def
        type: "CONTINUOUS_PRESSURE"
        weight_kg: 2.30
        timestamp: 1710612345000
        resolved: false
```

---

## ✅ สรุปค่าที่ต้องได้จาก Firebase

| ค่า | ใส่ที่ไหน | ตัวอย่าง |
|-----|----------|---------|
| **Database URL** | `config.h` → `FIREBASE_HOST` | `pressure-monitor-xxx.firebasedatabase.app` |
| **Web API Key** | `config.h` → `FIREBASE_API_KEY` | `AIzaSy...` |
| **Auth Email** | `config.h` → `FIREBASE_USER_EMAIL` | `esp32@pressure-monitor.com` |
| **Auth Password** | `config.h` → `FIREBASE_USER_PASSWORD` | `Esp32Secure!2024` |

> 🎉 เมื่อตั้งค่าครบแล้ว อัปโหลดโค้ดไปยัง ESP32 ได้เลย!
