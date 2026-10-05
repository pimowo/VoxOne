#include "../src/core/config_persistence.h"
#include "../src/core/bt_runtime.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace voxone::config_format;
using S = ConfigRecordStatus;
using W = ConfigWriteStatus;
using M = ConfigStorageMode;

// Cache and durable bytes are separate: a successful cache read cannot hide
// a failed commit or an incorrect durable record.
struct FakeEeprom {
  uint8_t durable[768];
  uint8_t cache[768];
  unsigned writes = 0, commits = 0, reads = 0;
  bool failWrite = false, failCommit = false, failRead = false;
  bool corruptCrc = false, validMismatch = false, invalidBool = false;
  FakeEeprom() {
    std::memset(durable, 0xa5, sizeof(durable));
    std::memcpy(cache, durable, sizeof(cache));
  }
  bool writeRecord(const uint8_t* bytes, std::size_t size) {
    assert(size == 255);
    ++writes;
    if (failWrite) return false;
    // The only permitted config write covers the whole record.
    std::memcpy(cache + 500, bytes, size);
    return true;
  }
  bool commit() {
    ++commits;
    if (failCommit) return false;
    std::memcpy(durable, cache, sizeof(cache));
    return true;
  }
  bool readRecord(uint8_t* bytes, std::size_t size) {
    assert(size == 255);
    ++reads;
    if (failRead) return false;
    std::memcpy(bytes, durable + 500, size);
    if (validMismatch) {
      config_v7_t other{};
      assert(parseConfigV7(bytes, size, other) == S::LOADED_V7);
      ++other.fields.volume;
      other.crc32 = calculateConfigV7Crc(other);
      assert(serializeConfigV7(other, bytes, size));
    }
    if (corruptCrc) bytes[251] ^= 1;
    if (invalidBool) bytes[250] = 2;
    return true;
  }
  void assertOutsideUntouched() const {
    for (unsigned i = 0; i < 500; ++i) assert(durable[i] == 0xa5);
    for (unsigned i = 755; i < 768; ++i) assert(durable[i] == 0xa5);
  }
};

void defaults(config_v6_t& runtime) {
  buildConfigDefaults(runtime, "0.pl.pool.ntp.org", "1.pl.pool.ntp.org", "VoxOne-test", 7);
}
config_v7_t stored(FakeEeprom& backend) {
  config_v7_t value{};
  assert(parseConfigV7(backend.durable + 500, 255, value) == S::LOADED_V7);
  backend.assertOutsideUntouched();
  return value;
}
void legacy(FakeEeprom& backend, uint8_t version) {
  uint8_t* area = backend.durable + 500;
  std::memset(area, 0, version == 5 ? 250 : 254);
  area[0] = 0xa6; area[1] = 0x10; area[2] = version;
  area[4] = 103; area[10] = 19; area[188] = 200;
  if (version == 6) {
    area[250] = 80; area[251] = 1; area[252] = 25; area[253] = 50;
  }
  std::memcpy(backend.cache, backend.durable, sizeof(backend.cache));
}
ConfigStartupResult startup(FakeEeprom& backend, config_v6_t& runtime,
                             ConfigPersistence& persistence, bool supports) {
  const auto loaded = loadStartupConfig(backend.durable + 500, 268, supports, runtime, defaults);
  persistence.begin(loaded.status, loaded.storedBtEnabled);
  persistence.finishStartup(runtime, backend);
  return loaded;
}
template <typename T>
void save(ConfigPersistence& state, config_v6_t& runtime, FakeEeprom& backend,
          T& field, T value, bool commit = true, bool force = false) {
  // Same update/flush contract as Config::saveValue (including pending fields).
  state.update(field, value, force);
  if (commit && state.dirty()) state.persist(runtime, backend);
}

int main() {
  config_v6_t runtime{}; defaults(runtime);
  FakeEeprom backend;
  ConfigPersistence state;
  state.begin(S::DEFAULTS_REQUIRED, false);
  assert(state.finishStartup(runtime, backend) == W::OK);
  assert(backend.writes == 1 && backend.commits == 1 && backend.reads == 1);
  assert(state.mode() == M::V7 && !state.dirty());
  assert(stored(backend).fields.config_set == 0xc7a7);
  assert(stored(backend).fields.version == 7);

  for (bool enabled : {false, true}) {
    state.setStoredBtEnabled(enabled);
    assert(state.persist(runtime, backend) == W::OK);
    for (bool supports : {false, true}) {
      config_v6_t reloaded{};
      ConfigPersistence restart;
      const unsigned before = backend.writes;
      const auto result = startup(backend, reloaded, restart, supports);
      assert(result.status == S::LOADED_V7);
      assert(result.storedBtEnabled == enabled && restart.storedBtEnabled() == enabled);
      assert(result.btEnabled == (supports && enabled));
      assert(backend.writes == before);
      BtRuntime bt(supports);
      assert(bt.configureBeforeStart(result.btEnabled));
      assert(bt.start() == (supports && enabled));
      // A later ordinary save must preserve stored true even on DESK.
      assert(restart.persist(reloaded, backend) == W::OK);
      assert(stored(backend).btEnabled == enabled);
    }
  }
  for (uint8_t version : {5, 6}) {
    FakeEeprom old;
    legacy(old, version);
    ConfigPersistence migrated;
    auto result = startup(old, runtime, migrated, true);
    assert(result.status == (version == 5 ? S::MIGRATED_V5 : S::MIGRATED_V6));
    assert(migrated.mode() == M::V7);
    assert(old.writes == 1 && old.commits == 1 && old.reads == 1);
    auto value = stored(old);
    assert(value.fields.lastStation == 19 && value.fields.volume == 103);
    assert(value.fields.maximumVolume == (version == 5 ? 100 : 80));
    assert(value.fields.startupMode == (version == 5 ? 0 : 1));
    assert(value.fields.startupFixedVolume == (version == 5 ? 20 : 25));
    assert(value.fields.lastUserVolume == 50 && value.btEnabled == 1);
  }
  for (uint8_t version : {1, 2, 3, 4, 255}) {
    FakeEeprom old;
    legacy(old, version);
    ConfigPersistence reset;
    assert(startup(old, runtime, reset, false).status == S::DEFAULTS_REQUIRED);
    assert(old.commits == 1 && stored(old).fields.volume == 12);
    assert(stored(old).btEnabled == 0);
  }
  for (bool badBool : {false, true}) {
    FakeEeprom corrupt;
    ConfigPersistence seeded;
    defaults(runtime);
    seeded.begin(S::DEFAULTS_REQUIRED, true);
    assert(seeded.persist(runtime, corrupt) == W::OK);
    if (badBool) corrupt.durable[750] = 2;
    else corrupt.durable[751] ^= 1;
    ConfigPersistence reset;
    const auto result = startup(corrupt, runtime, reset, true);
    assert(result.status == (badBool ? S::INVALID_BOOL : S::INVALID_CRC));
    assert(corrupt.commits == 2 && stored(corrupt).fields.volume == 12);
  }
  FakeEeprom future;
  future.durable[500] = 0xa7; future.durable[501] = 0xc7;
  future.durable[502] = 8; future.durable[503] = 0;
  uint8_t preserved[768]; std::memcpy(preserved, future.durable, sizeof(preserved));
  ConfigPersistence readOnly;
  assert(startup(future, runtime, readOnly, true).status == S::UNSUPPORTED_NEWER);
  assert(readOnly.mode() == M::FUTURE_READ_ONLY);
  save(readOnly, runtime, future, runtime.volume, uint8_t(88));
  readOnly.setStoredBtEnabled(false);
  assert(readOnly.persist(runtime, future) == W::BLOCKED);
  assert(readOnly.factoryReset(runtime, true, future, defaults) == W::BLOCKED);
  assert(future.writes == 0 && future.commits == 0);
  assert(std::memcmp(preserved, future.durable, sizeof(preserved)) == 0);

  for (unsigned fault = 0; fault < 6; ++fault) {
    FakeEeprom broken;
    legacy(broken, 6);
    const auto loaded = loadStartupConfig(broken.durable + 500, 268, true, runtime, defaults);
    ConfigPersistence failed;
    failed.begin(loaded.status, loaded.storedBtEnabled);
    broken.failWrite = fault == 0;
    broken.failCommit = fault == 1;
    broken.failRead = fault == 2;
    broken.validMismatch = fault == 3;
    broken.corruptCrc = fault == 4;
    broken.invalidBool = fault == 5;
    const W statuses[] = {W::WRITE_FAILED, W::COMMIT_FAILED, W::READ_FAILED,
      W::READBACK_MISMATCH, W::INVALID_READBACK, W::INVALID_READBACK};
    assert(failed.finishStartup(runtime, broken) == statuses[fault]);
    assert(failed.mode() == M::WRITE_FAILED && failed.dirty());
    assert(runtime.lastStation == 19); // Loaded RAM survives a failed migration.
    assert(broken.writes == 1 && broken.commits == (fault == 0 ? 0u : 1u));
    if (fault <= 1) assert(broken.durable[502] == 6);
    // No implicit retry; an explicit later save can recover with a full record.
    assert(failed.finishStartup(runtime, broken) == W::BLOCKED);
    assert(broken.writes == 1);
    broken.failWrite = broken.failCommit = broken.failRead = false;
    broken.validMismatch = broken.corruptCrc = broken.invalidBool = false;
    assert(failed.persist(runtime, broken) == W::OK);
    assert(stored(broken).fields.lastStation == 19);
  }

  defaults(runtime);
  state.begin(S::LOADED_V7, true);
  assert(state.persist(runtime, backend) == W::OK);
  const uint32_t previousCrc = stored(backend).crc32;
  save(state, runtime, backend, runtime.lastStation, uint16_t(77));
  assert(stored(backend).fields.lastStation == 77);
  assert(stored(backend).crc32 != previousCrc);
  const unsigned beforeDeferred = backend.commits;
  for (uint8_t volume = 20; volume < 40; ++volume) {
    save(state, runtime, backend, runtime.volume, volume, false);
    save(state, runtime, backend, runtime.lastUserVolume, volume, false);
  }
  assert(backend.commits == beforeDeferred);
  assert(state.persist(runtime, backend) == W::OK);
  assert(backend.commits == beforeDeferred + 1);
  assert(stored(backend).fields.volume == 39 && stored(backend).fields.lastUserVolume == 39);
  // A final unchanged field still flushes earlier changes in a partial reset.
  save(state, runtime, backend, runtime.vumeter, false, false);
  save(state, runtime, backend, runtime.bass, int8_t(0), false);
  save(state, runtime, backend, runtime.contrast, uint8_t(55), false, true);
  const unsigned beforeFlush = backend.commits;
  save(state, runtime, backend, runtime.watchdog, true);
  assert(backend.commits == beforeFlush + 1 && !state.dirty());
  state.update(runtime.mdnsname, "VoxOne-custom", sizeof(runtime.mdnsname));
  assert(state.persist(runtime, backend) == W::OK);
  assert(std::strcmp(stored(backend).fields.mdnsname, "VoxOne-custom") == 0);
  // Screen reset with only brightness changed must reach the next flush.
  runtime.brightness = 17;
  save(state, runtime, backend, runtime.brightness, uint8_t(100), false);
  save(state, runtime, backend, runtime.numplaylist, false);
  assert(stored(backend).fields.brightness == 100);
  // Timezone/controls batches retain unrelated BT and station settings.
  const bool btBeforePartial = state.storedBtEnabled();
  state.update(runtime.sntp1, "0.pl.pool.ntp.org", sizeof(runtime.sntp1), true);
  state.update(runtime.sntp2, "1.pl.pool.ntp.org", sizeof(runtime.sntp2), true);
  save(state, runtime, backend, runtime.timeSyncInterval, uint16_t(60));
  save(state, runtime, backend, runtime.volsteps, uint8_t(1), false, true);
  save(state, runtime, backend, runtime.encacc, uint16_t(200));
  const auto partial = stored(backend);
  assert(partial.fields.lastStation == 77 && partial.btEnabled == btBeforePartial);
  assert(partial.fields.timeSyncInterval == 60 && partial.fields.encacc == 200);
  for (bool supports : {false, true}) {
    const unsigned before = backend.commits;
    assert(state.factoryReset(runtime, supports, backend, defaults) == W::OK);
    const auto value = stored(backend);
    assert(backend.commits == before + 1);
    assert(value.fields.lastStation == 0 && value.fields.volume == 12);
    assert(value.fields.maximumVolume == 100 && value.fields.startupFixedVolume == 20);
    assert(value.btEnabled == supports);
  }
  ConfigPersistence unavailable;
  unavailable.begin(S::INVALID_ARGUMENT, false);
  const unsigned beforeUnavailable = backend.writes;
  assert(unavailable.persist(runtime, backend) == W::BLOCKED);
  assert(backend.writes == beforeUnavailable);
  std::puts("PASS config_persistence_native");
}
