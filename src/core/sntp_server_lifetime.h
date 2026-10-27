#ifndef VOXONE_SNTP_SERVER_LIFETIME_H
#define VOXONE_SNTP_SERVER_LIFETIME_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace sntp {

class ServerNames {
 public:
  static constexpr size_t kCapacity = 35;

  template <typename Stop, typename Start>
  void replace(const char* primary, const char* secondary,
               Stop stop, Start start) {
    const uint8_t next = active_ ^ 1U;
    copy(names_[next][0], primary);
    copy(names_[next][1], secondary);

    // lwIP retains the supplied pointers. Keep the active bank untouched until
    // the old client has stopped, then publish the fully prepared other bank.
    stop();
    active_ = next;
    start(names_[active_][0], names_[active_][1]);
  }

  const char* primary() const { return names_[active_][0]; }
  const char* secondary() const { return names_[active_][1]; }

 private:
  static void copy(char (&destination)[kCapacity], const char* source) {
    size_t index = 0;
    if (source) {
      while (index + 1 < kCapacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
      }
    }
    destination[index] = '\0';
    while (++index < kCapacity) destination[index] = '\0';
  }

  char names_[2][2][kCapacity]{};
  uint8_t active_ = 0;
};

}  // namespace sntp
}  // namespace voxone

#endif
