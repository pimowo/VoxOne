#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "../src/core/sntp_server_lifetime.h"

namespace {

struct FakeSntp {
  const char* primary = nullptr;
  const char* secondary = nullptr;
  bool running = false;
  unsigned stops = 0;
  unsigned starts = 0;

  void stop() {
    if (running) {
      assert(primary != nullptr);
      assert(secondary != nullptr);
    }
    running = false;
    ++stops;
  }

  void start(const char* first, const char* second) {
    assert(!running);
    primary = first;
    secondary = second;
    running = true;
    ++starts;
  }
};

void overwriteStack() {
  volatile char noise[4096];
  for (size_t i = 0; i < sizeof(noise); ++i)
    noise[i] = static_cast<char>((i * 37U) & 0x7fU);
}

void configureFromStack(voxone::sntp::ServerNames& names, FakeSntp& client,
                        const char* first, const char* second) {
  char primary[voxone::sntp::ServerNames::kCapacity]{};
  char secondary[voxone::sntp::ServerNames::kCapacity]{};
  snprintf(primary, sizeof(primary), "%s", first);
  snprintf(secondary, sizeof(secondary), "%s", second);
  names.replace(primary, secondary,
                [&]() { client.stop(); },
                [&](const char* a, const char* b) { client.start(a, b); });
}

void assertPair(const FakeSntp& client, const char* first, const char* second) {
  assert(client.running);
  assert(strcmp(client.primary, first) == 0);
  assert(strcmp(client.secondary, second) == 0);
}

}  // namespace

int main() {
  voxone::sntp::ServerNames names;
  FakeSntp client;

  configureFromStack(names, client, "0.pl.pool.ntp.org", "1.pl.pool.ntp.org");
  overwriteStack();
  assertPair(client, "0.pl.pool.ntp.org", "1.pl.pool.ntp.org");
  assert(client.primary == names.primary());
  assert(client.secondary == names.secondary());

  const char* oldPrimary = client.primary;
  const char* oldSecondary = client.secondary;
  bool stoppedWithOldBankIntact = false;
  names.replace(
      "time.cloudflare.com", "time.google.com",
      [&]() {
        assert(strcmp(oldPrimary, "0.pl.pool.ntp.org") == 0);
        assert(strcmp(oldSecondary, "1.pl.pool.ntp.org") == 0);
        client.stop();
        stoppedWithOldBankIntact = true;
      },
      [&](const char* first, const char* second) {
        assert(stoppedWithOldBankIntact);
        client.start(first, second);
      });
  overwriteStack();
  assertPair(client, "time.cloudflare.com", "time.google.com");
  assert(client.primary != oldPrimary);
  assert(client.secondary != oldSecondary);

  // GOT_IP reinitialization uses a fresh bank even when the names are equal.
  const char* beforeGotIp = client.primary;
  names.replace(names.primary(), names.secondary(),
                [&]() { client.stop(); },
                [&](const char* first, const char* second) {
                  client.start(first, second);
                });
  assertPair(client, "time.cloudflare.com", "time.google.com");
  assert(client.primary != beforeGotIp);

  // Model Config's recursive mutex: a writer and a reinitializer can only see
  // complete primary/secondary pairs, never a cross-core torn update.
  std::recursive_mutex configMutex;
  char configuredPrimary[voxone::sntp::ServerNames::kCapacity] = "a0.example";
  char configuredSecondary[voxone::sntp::ServerNames::kCapacity] = "a1.example";
  std::atomic<bool> failed{false};
  std::thread writer([&]() {
    for (unsigned i = 0; i < 2000; ++i) {
      std::lock_guard<std::recursive_mutex> lock(configMutex);
      const char tag = (i & 1U) ? 'a' : 'b';
      snprintf(configuredPrimary, sizeof(configuredPrimary), "%c0.example", tag);
      snprintf(configuredSecondary, sizeof(configuredSecondary), "%c1.example", tag);
    }
  });
  for (unsigned i = 0; i < 2000; ++i) {
    std::lock_guard<std::recursive_mutex> lock(configMutex);
    names.replace(configuredPrimary, configuredSecondary,
                  [&]() { client.stop(); },
                  [&](const char* first, const char* second) {
                    client.start(first, second);
                  });
    if (client.primary[0] != client.secondary[0] ||
        strcmp(client.primary + 1, "0.example") != 0 ||
        strcmp(client.secondary + 1, "1.example") != 0)
      failed = true;
  }
  writer.join();
  assert(!failed);
  assert(client.starts == client.stops);

  puts("PASS sntp_server_lifetime_native");
  return 0;
}
