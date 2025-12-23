
---

## 📂 Module Responsibilities

### `main.cpp`
- UI rendering and interaction
- State machine for vault lifecycle
- Popup and modal orchestration
- Thread-safe UI updates

---

### `core/SystemUtils`
- Process execution abstraction
- Secure file wiping
- Drive detection helpers
- Base directory resolution

---

### `SecureBuffer`
- Secure in-memory storage for secrets
- Automatic zeroing on destruction
- Prevents accidental password leaks

---

### `VaultRegistry`
- Persistent vault list storage
- Active vault tracking
- Drive letter association

---

### `Crypto`
- Log encryption
- Secure data serialization
- DPAPI-based protection (where applicable)

---

## 🔄 Asynchronous Design

Long-running operations (e.g. large vault creation) are executed in background
threads using atomic state flags.

This guarantees:
- No UI freezing
- User feedback during operations
- Clear success / failure reporting

---

## 🧨 Panic Flow

1. User presses CTRL + F12
2. Immediate VeraCrypt dismount
3. Optional secure wipe:
   - config.bin
   - vaults.dat
4. Process terminates safely

---

## 🧪 Future Improvements

- Progress parsing from VeraCrypt output
- Multi-vault simultaneous operations
- Plugin-based action hooks
- Cross-platform abstraction layer

---

**Author:** Zero  
**Project:** ZeroCrypto
