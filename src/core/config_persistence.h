#ifndef VOXONE_CONFIG_PERSISTENCE_H
#define VOXONE_CONFIG_PERSISTENCE_H

#include "config_startup.h"

namespace voxone {
namespace config_format {

enum class ConfigStorageMode { UNAVAILABLE, PENDING, V7, WRITE_FAILED, FUTURE_READ_ONLY };
enum class ConfigWriteStatus {
  OK, BLOCKED, INVALID_RECORD, WRITE_FAILED, COMMIT_FAILED,
  READ_FAILED, INVALID_READBACK, READBACK_MISMATCH
};

inline const char* configWriteMessage(ConfigWriteStatus status) {
  switch (status) {
    case ConfigWriteStatus::OK: return "verified v7";
    case ConfigWriteStatus::BLOCKED: return "storage read-only/unavailable";
    case ConfigWriteStatus::INVALID_RECORD: return "invalid outgoing record";
    case ConfigWriteStatus::WRITE_FAILED: return "record write failed";
    case ConfigWriteStatus::COMMIT_FAILED: return "commit failed";
    case ConfigWriteStatus::READ_FAILED: return "persistent read-back failed";
    case ConfigWriteStatus::INVALID_READBACK: return "read-back format/CRC invalid";
    case ConfigWriteStatus::READBACK_MISMATCH: return "read-back differs from written record";
  }
  return "unknown failure";
}

// One active format. No legacy writer or automatic retry exists here.
// Backend contract: writeRecord(255 bytes), commit(), readRecord(255 bytes).
// readRecord MUST read durable storage, not a write cache.
class ConfigPersistence {
 public:
  void begin(ConfigRecordStatus status, bool storedBt) {
    storedBt_ = storedBt;
    dirty_ = false;
    last_ = ConfigWriteStatus::OK;
    if (status == ConfigRecordStatus::UNSUPPORTED_NEWER)
      mode_ = ConfigStorageMode::FUTURE_READ_ONLY;
    else if (status == ConfigRecordStatus::INVALID_ARGUMENT)
      mode_ = ConfigStorageMode::UNAVAILABLE;
    else if (status == ConfigRecordStatus::LOADED_V7)
      mode_ = ConfigStorageMode::V7;
    else {
      mode_ = ConfigStorageMode::PENDING;
      dirty_ = true; // Legacy migration or known corrupt/default record.
    }
  }
  ConfigStorageMode mode() const { return mode_; }
  ConfigWriteStatus lastStatus() const { return last_; }
  bool dirty() const { return dirty_; }
  bool storedBtEnabled() const { return storedBt_; }
  bool writable() const {
    return mode_ != ConfigStorageMode::FUTURE_READ_ONLY &&
           mode_ != ConfigStorageMode::UNAVAILABLE;
  }
  void markDirty() { dirty_ = true; }
  void setStoredBtEnabled(bool enabled) {
    if (storedBt_ != enabled) { storedBt_ = enabled; dirty_ = true; }
  }
  template <typename T>
  void update(T& field, const T& value, bool force = false) {
    if (field == value && !force) return;
    field = value;
    dirty_ = true;
  }
  void update(char* field, const char* value, std::size_t size, bool force = false) {
    if (!size || (std::strncmp(field, value, size) == 0 && !force)) return;
    // Also handles callers passing the field itself with force=true.
    if (field != value) copyStartupString(field, value, size);
    dirty_ = true;
  }
  template <typename Runtime, typename Backend>
  ConfigWriteStatus persist(const Runtime& runtime, Backend& backend) {
    if (!writable()) return last_ = ConfigWriteStatus::BLOCKED;
    config_v7_t model{};
    copyConfigFields(runtime, model.fields);
    model.fields.config_set = kConfigV7Magic;
    model.fields.version = kConfigV7;
    model.btEnabled = storedBt_ ? 1 : 0;
    model.crc32 = calculateConfigV7Crc(model);
    uint8_t bytes[kConfigV7SerializedSize]{};
    if (!serializeConfigV7(model, bytes, sizeof(bytes)))
      return failed(ConfigWriteStatus::INVALID_RECORD);
    if (!backend.writeRecord(bytes, sizeof(bytes)))
      return failed(ConfigWriteStatus::WRITE_FAILED);
    if (!backend.commit()) return failed(ConfigWriteStatus::COMMIT_FAILED);
    uint8_t actual[kConfigV7SerializedSize]{};
    if (!backend.readRecord(actual, sizeof(actual)))
      return failed(ConfigWriteStatus::READ_FAILED);
    config_v7_t checked{};
    if (parseConfigV7(actual, sizeof(actual), checked) != ConfigRecordStatus::LOADED_V7)
      return failed(ConfigWriteStatus::INVALID_READBACK);
    if (std::memcmp(bytes, actual, sizeof(bytes)) != 0)
      return failed(ConfigWriteStatus::READBACK_MISMATCH);
    mode_ = ConfigStorageMode::V7;
    dirty_ = false;
    return last_ = ConfigWriteStatus::OK;
  }
  template <typename Runtime, typename Backend>
  ConfigWriteStatus finishStartup(const Runtime& runtime, Backend& backend) {
    if (mode_ == ConfigStorageMode::V7) return last_ = ConfigWriteStatus::OK;
    if (mode_ != ConfigStorageMode::PENDING) return last_ = ConfigWriteStatus::BLOCKED;
    return persist(runtime, backend);
  }
  template <typename Runtime, typename Backend, typename Defaults>
  ConfigWriteStatus factoryReset(Runtime& runtime, bool supportsBt, Backend& backend,
                                Defaults defaults) {
    if (!writable()) return last_ = ConfigWriteStatus::BLOCKED;
    defaults(runtime);
    storedBt_ = supportsBt;
    dirty_ = true;
    return persist(runtime, backend);
  }
 private:
  ConfigWriteStatus failed(ConfigWriteStatus status) {
    mode_ = ConfigStorageMode::WRITE_FAILED;
    dirty_ = true;
    return last_ = status;
  }
  ConfigStorageMode mode_ = ConfigStorageMode::UNAVAILABLE;
  ConfigWriteStatus last_ = ConfigWriteStatus::BLOCKED;
  bool storedBt_ = false;
  bool dirty_ = false;
};

} // namespace config_format
} // namespace voxone
#endif
