# 🩺 ระบบตรวจจับแรงกด (Pressure Monitoring System)

> **ระบบ IoT สำหรับเฝ้าระวังแผลกดทับผู้ป่วยแบบ Real-time**
> ESP32 + RFP-602 Sensor → Firebase → Web Dashboard + Telegram แจ้งเตือน

🌐 [English Version](./README.md)

---

## 📸 Demo

<div align="center">
  <img src="Demo/dashboard_demo.png" alt="Dashboard Demo" width="100%">
</div>

---

## 📁 โครงสร้างโปรเจค

```
Pressure_Iot/
├── 📟 esp32/
│   ├── esp32_pressure_monitor.ino   # โค้ดหลักสำหรับ ESP32
│   └── config.h                      # ไฟล์ตั้งค่าทั้งหมด
├── 🔥 firebase/
│   ├── database.rules.json           # กฎความปลอดภัย Firebase
│   └── README.md                     # คู่มือตั้งค่า Firebase
├── 📊 dashboard/
│   ├── index.html                    # หน้า Dashboard
│   ├── css/style.css                 # สไตล์ชีท (ธีมมืด)
│   └── js/app.js                     # โค้ด JavaScript
├── 📸 Demo/                          # รูป Demo
├── 🔌 WIRING_DIAGRAM.md             # แผนผังการต่อสาย
└── 📖 README.md                      # เอกสาร (English)
```

---

## 🔌 1. การต่อสายฮาร์ดแวร์

ดูรายละเอียดเพิ่มเติมที่ **[WIRING_DIAGRAM.md](./WIRING_DIAGRAM.md)**

### ตารางการต่อขาพิน

| ขาพินโมดูล | ขาพิน ESP32 | หน้าที่ |
|:----------:|:-----------:|---------|
| VCC | **3V3** | ⚡ จ่ายไฟ (3.3V เท่านั้น!) |
| GND | **GND** | ⏚ กราวด์ร่วม |
| AO | **GPIO 34** | 📊 อ่านค่า Analog จาก Sensor |
| DO | **GPIO 35** | 🔢 Digital Output (สำรอง) |

### 💡 LED เพิ่มเติม

| LED | ขาพิน ESP32 | หน้าที่ |
|:---:|:-----------:|---------|
| 🔵 LED สีฟ้า | **GPIO 18** | แสดงสถานะ WiFi (ติด = เชื่อมต่อแล้ว) |
| 🟡 LED แสดงการกด | **GPIO 19** | แสดงสถานะแรงกด (ติด = มีแรงกด) |

> ⚠️ **สำคัญ:** ใช้ไฟ **3.3V เท่านั้น!** ห้ามต่อ 5V เข้าขา AO เพราะจะทำให้ ESP32 เสียหาย

---

## 📟 2. ตั้งค่า ESP32 (Arduino IDE)

### 📥 ติดตั้ง Board Support
1. เปิด Arduino IDE → **File → Preferences**
2. เพิ่มลงใน "Additional Board Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. **Tools → Board → Boards Manager** → ค้นหา "esp32" → ติดตั้ง
4. เลือกบอร์ด: **ESP32 Dev Module**

### 📚 ไลบรารีที่ต้องติดตั้ง

| ไลบรารี | ผู้พัฒนา | เวอร์ชัน |
|---------|---------|:-------:|
| 🔥 **Firebase ESP32 Client** | Mobizt | 4.x+ |
| 📋 **ArduinoJson** | Benoit Blanchon | 7.x+ |

### ⚡ โหมด Inverted Logic
- **ADC ค่าสูง (4095)** = ไม่มีแรงกด
- **ADC ค่าต่ำ (0)** = กดเต็มกำลัง (5 kg)
- Threshold ตรวจจับ = ADC < 2000 ถือว่ามีแรงกด

---

## 🔥 3. ตั้งค่า Firebase

ดูคู่มือเต็มที่ **[firebase/README.md](./firebase/README.md)** 📖

### ขั้นตอนย่อ:
1. 🌐 สร้างโปรเจค Firebase ที่ [console.firebase.google.com](https://console.firebase.google.com)
2. 🗄️ เปิดใช้ **Realtime Database** (เลือก Singapore)
3. 🔑 เปิดใช้ **Authentication → Email/Password** → สร้าง User
4. 📋 คัดลอก **API Key** และ **Database URL** ไปใส่ใน `config.h`
5. 🔒 วาง Rules จากไฟล์ `database.rules.json`

---

## 📊 4. เปิด Dashboard

```bash
# วิธีที่ 1: ดับเบิลคลิกเปิดเลย (ง่ายสุด!)
เปิดไฟล์ dashboard/index.html ด้วย Browser

# วิธีที่ 2: Local Server
cd dashboard && npx serve .
```

### ⚙️ ใช้งานครั้งแรก:
1. คลิกไอคอน ⚙️ ที่มุมขวาบน
2. ใส่ **Firebase API Key** + **Database URL**
3. ตั้ง **Device ID** = `esp32_node_01`
4. กด **Connect** ✅

---

## 📲 5. ตั้งค่า Telegram Bot

1. 🤖 เปิด Telegram → ค้นหา **@BotFather** → ส่ง `/newbot`
2. 📋 คัดลอก **Bot Token** → ใส่ `config.h`
3. 👤 ค้นหา **@userinfobot** → ได้ **Chat ID** → ใส่ `config.h`

### 🚨 ประเภทการแจ้งเตือน

| การแจ้งเตือน | เงื่อนไข | ข้อความ |
|:-----------:|---------|---------|
| 🚨 **แรงกดผิดปกติ** | น้ำหนัก ≥ 4.5 kg | แจ้งเตือนทันที |
| ⏱️ **กดค้างนาน** | กดค้างเกิน 10 วินาที (Demo) | แจ้งเตือนทุกรอบ |
| ✅ **เปิดเครื่อง** | ESP32 เริ่มทำงาน | ระบบออนไลน์ |

---

## ⚙️ ค่าที่ปรับได้ (ใน `config.h`)

| ค่า | ค่าเริ่มต้น | คำอธิบาย |
|-----|:---------:|---------|
| `PRESSURE_THRESHOLD_KG` | 0.5 kg | น้ำหนักขั้นต่ำที่ถือว่ามีแรงกด |
| `CONTINUOUS_PRESSURE_SECONDS` | 10 วินาที (Demo) | เวลากดค้างก่อนแจ้งเตือน |
| `ABNORMAL_PRESSURE_KG` | 4.5 kg | แจ้งเตือนทันทีถ้าเกินค่านี้ |
| `ALERT_COOLDOWN_SECONDS` | 15 วินาที (Demo) | ช่วงเวลาขั้นต่ำระหว่างแจ้งเตือน |
| `ADC_PRESS_THRESHOLD` | 2000 | ADC ต่ำกว่านี้ = มีแรงกด |
| `MOVING_AVG_SAMPLES` | 20 | จำนวนตัวอย่างสำหรับกรองค่า |

> 💡 **โหมดจริง:** เปลี่ยน `CONTINUOUS_PRESSURE_SECONDS` เป็น `1800` (30 นาที) และ `ALERT_COOLDOWN_SECONDS` เป็น `300` (5 นาที)

---

## 🔒 หมายเหตุความปลอดภัย

- 🔐 Firebase ต้องยืนยันตัวตนก่อนเขียนข้อมูล
- 👁️ Dashboard อ่านข้อมูลได้โดยไม่ต้อง Login
- 🤖 เก็บ Telegram Token ให้เป็นความลับ

---

<div align="center">

⭐ **ถ้าโปรเจคนี้มีประโยชน์ อย่าลืมกด Star!** ⭐

</div>
