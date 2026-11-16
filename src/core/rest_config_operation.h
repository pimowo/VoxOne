#pragma once

#include <cstdint>
#include <cstring>

namespace voxone { namespace rest_config {

enum class Kind : uint8_t { HaName, Theme, PowerMode };
enum class Status : uint8_t { Pending, Saved, AppliedNotPersisted, Failed, Cancelled };
inline const char* statusName(Status status) {
  switch (status) {
    case Status::Pending: return "pending";
    case Status::Saved: return "saved";
    case Status::AppliedNotPersisted: return "applied_but_not_persisted";
    case Status::Failed: return "failed";
    case Status::Cancelled: return "cancelled";
  }
  return "failed";
}
constexpr uint32_t kPendingMs = 30000;
constexpr uint32_t kRetentionMs = 60000;
constexpr unsigned kCapacity = 4;

template<class Owner> bool sameOwner(const Owner& a, const Owner& b) {
  unsigned difference = static_cast<unsigned>(a.mode) ^ static_cast<unsigned>(b.mode);
  difference |= a.securityGeneration ^ b.securityGeneration;
  for (unsigned i = 0; i < sizeof(a.tokenHash); ++i)
    difference |= a.tokenHash[i] ^ b.tokenHash[i];
  return difference == 0;
}

// Result storage, not a second queue. Only the existing 01A executor consumes
// tickets. All access is serialized by configurationMutationMutex.
template<class Owner> class Store {
 public:
  struct Operation {
    uint32_t ticket = 0;
    uint32_t created = 0;
    uint32_t completed = 0;
    Owner owner{};
    char id[33]{}; // 128 random bits; never a cookie/session token
    char value[33]{};
    Kind kind = Kind::HaName;
    Status status = Status::Pending;
  };
  Operation* find(uint32_t ticket) {
    for (auto& entry : entries_)
      if (ticket && entry.ticket == ticket) return &entry;
    return nullptr;
  }
  void expire(uint32_t now) {
    for (auto& entry : entries_) {
      if (entry.ticket && entry.status == Status::Pending &&
          uint32_t(now - entry.created) >= kPendingMs) {
        entry.status = Status::Cancelled;
        entry.completed = now;
      }
      if (entry.ticket && entry.status != Status::Pending &&
          uint32_t(now - entry.completed) >= kRetentionMs) entry = Operation{};
    }
  }
  Operation* create(const char* id, Kind kind, const char* value,
                    const Owner& owner, uint32_t now) {
    if (!validId(id) || !value || std::strlen(value) > 32) return nullptr;
    expire(now);
    for (const auto& entry : entries_)
      if (entry.ticket && !std::strcmp(entry.id, id)) return nullptr;
    for (auto& entry : entries_) {
      if (entry.ticket) continue;
      do { ++nextTicket_; } while (!nextTicket_ || find(nextTicket_));
      entry.ticket = nextTicket_;
      entry.created = now;
      entry.owner = owner;
      entry.kind = kind;
      std::strcpy(entry.id, id);
      std::strcpy(entry.value, value);
      return &entry;
    }
    return nullptr;
  }
  Operation* read(const char* id, const Owner& owner, uint32_t now) {
    if (!validId(id)) return nullptr;
    expire(now);
    for (auto& entry : entries_)
      if (entry.ticket && !std::strcmp(entry.id, id) && sameOwner(entry.owner, owner))
        return &entry;
    return nullptr;
  }
  void discard(uint32_t ticket) {
    if (auto* entry = find(ticket)) *entry = Operation{};
  }
  void finish(Operation& entry, Status status, uint32_t now) {
    entry.status = status;
    entry.completed = now;
  }
  static bool validId(const char* id) {
    if (!id || std::strlen(id) != 32) return false;
    for (unsigned i = 0; i < 32; ++i)
      if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
        return false;
    return true;
  }
 private:
  Operation entries_[kCapacity]{};
  uint32_t nextTicket_ = 0;
};
} }
