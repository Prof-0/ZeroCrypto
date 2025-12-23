# ZeroCrypto

**ZeroCrypto** is a lightweight, security-focused Windows vault manager built around **VeraCrypt**, designed for **personal, portable, and high-risk environments**.

It provides a modern GUI, automation, and safety features on top of VeraCrypt without modifying VeraCrypt itself.

---

## ✨ Features

### 🔐 Vault Management
- Mount / Unmount VeraCrypt containers silently
- Automatic vault detection on application startup
- Support for **multiple vaults**
- Auto-mount environment scripts (`StartEnv.bat`)
- Drive letter control per vault

### ⚙️ Vault Creation
- Create new VeraCrypt containers directly from ZeroCrypto
- Size selector with **MB / GB units**
- Asynchronous creation (UI never freezes)
- Background formatting using VeraCrypt Format
- Completion notification with:
  - **Mount Vault**
  - **View Logs**

### 🖱 Drag & Drop Support
- Drag `.hc` / `.vc` files directly into the app
- Auto-detect vault name from filename
- Instant configuration popup

### 🛑 Kill Switch & Safety
- Global **Panic Hotkey (CTRL + F12)**
- USB removal dead-man switch
- Auto-unmount on exit (optional)
- Optional secure wipe of config & vault registry

### 🔒 Security Design
- Passwords stored only in `SecureBuffer`
- No password persistence
- Encrypted system logs
- No plaintext credentials in memory longer than required

### 🎨 UI / UX
- Custom borderless ImGui interface
- Responsive layout
- Professional dark theme
- Background-safe async operations
- Developer follow button

---

## 🌐 Developer
**Follow the developer:**  
👉 https://Prof-0.github.io/protofolio/#contact

The link opens automatically on:
- Vault mount
- Vault unmount  
and is also accessible via a UI button.

---

## 🧠 Philosophy

ZeroCrypto is **not a replacement for VeraCrypt**.

It is:
- A **secure orchestrator**
- A **session controller**
- A **human-friendly interface** for advanced workflows

VeraCrypt remains untouched and fully trusted.

---

## 🏗 Build

### Requirements
- Windows 10 / 11
- MinGW (GCC, C++17)
- DirectX 11
- VeraCrypt portable (bundled in `assets/`)

### Build
```powershell
.\build.ps1
```
---

## 📚 Project Documentation 

Read more about the project details here:

* 🏗️ **[System Architecture](ARCHITECTURE.md)**
* ⚖️ **[Disclaimer](Disclaimer.md)** 
* 👥 **[Responsibilities](Module-Responsibilities.md)** 

---

# 👤 Author
## ***Zero***

---


