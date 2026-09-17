# Haptic Safety Belt for Deaf & Hard-of-Hearing Users

A wearable **haptic alert and health-monitoring belt** designed to convert important environmental and physiological events into **vibration-based feedback**.

The goal is simple: instead of relying on sound, the belt communicates important alerts through **localized vibration patterns**, allowing the wearer to recognize different events without needing to hear them.

> Built as a prototype combining embedded systems, sensors, haptic feedback, and wireless communication.

---

## 🚀 Features

### 🔔 Haptic Event Alerts
The belt uses vibration motors to communicate different types of alerts.

Examples:

| Event | Haptic Feedback |
|---|---|
| 🚨 Emergency alert | Strong/rapid vibration |
| ⚠️ Environmental warning | Repeating vibration pattern |
| ❤️ Abnormal health reading | Distinct pulse pattern |
| 📱 Phone notification | Short vibration sequence |
| 🆘 Manual SOS | Continuous/rapid vibration |

Different vibration patterns allow the user to distinguish between events without depending on audio.

---

### ❤️ Health Monitoring

The system is designed to support physiological sensors such as:

- **MAX30102**
  - Heart rate
  - Blood oxygen saturation (SpO₂)
- Additional sensors can be integrated as the project develops.

Health measurements can be processed by the ESP32 and used to trigger appropriate haptic alerts.

> Health readings from this prototype are intended for experimental/educational use and are **not medical diagnoses or a replacement for certified medical devices**.

---

### 📡 Wireless Connectivity

The belt uses an **ESP32-S3** as its main controller.

Potential communication methods include:

- Bluetooth Low Energy (BLE)
- Wi-Fi
- Smartphone connectivity
- Internet/cloud services

This allows the belt to communicate with a companion application or other connected devices.

---

## 🧠 System Architecture

```text
              ┌─────────────────────┐
              │      Sensors        │
              │                     │
              │  MAX30102           │
              │  Motion Sensors     │
              │  Other Sensors      │
              └──────────┬──────────┘
                         │
                         ▼
              ┌─────────────────────┐
              │      ESP32-S3       │
              │   Main Controller   │
              │                     │
              │ • Sensor Processing │
              │ • Event Detection   │
              │ • BLE / Wi-Fi       │
              │ • Alert Logic       │
              └──────┬───────┬──────┘
                     │       │
             ┌───────┘       └────────┐
             ▼                        ▼
    ┌─────────────────┐      ┌─────────────────┐
    │ Vibration Motors│      │ Smartphone/App  │
    │                 │      │                 │
    │ Haptic Alerts   │      │ Notifications   │
    └─────────────────┘      │ Data / Alerts   │
                             └─────────────────┘
