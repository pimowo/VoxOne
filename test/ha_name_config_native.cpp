#include "../src/core/ha_name_config.h"

#include <cassert>
#include <cstring>

using namespace voxone::ha_name;

int main() {
  uint8_t storage[kStorageBytes]{};
  char output[kRuntimeCapacity]{};

  assert(!decode(storage, sizeof(storage), output, sizeof(output)));
  assert(output[0] == '\0');

  const char* name = "Salon VoxOne";
  assert(encode(name, std::strlen(name), storage, sizeof(storage)));
  assert(decode(storage, sizeof(storage), output, sizeof(output)));
  assert(std::strcmp(output, name) == 0);

  const char* utf8 = "G\xC5\x82o\xC5\x9Bnik";
  assert(valid(utf8, std::strlen(utf8)));
  assert(encode(utf8, std::strlen(utf8), storage, sizeof(storage)));
  assert(decode(storage, sizeof(storage), output, sizeof(output)));
  assert(std::strcmp(output, utf8) == 0);

  const char maximum[] = "12345678901234567890123456789012";
  assert(std::strlen(maximum) == kMaximumBytes);
  assert(valid(maximum, std::strlen(maximum)));
  assert(!valid("123456789012345678901234567890123", 33));
  assert(!valid("line\nbreak", 10));

  storage[4] = 33;
  assert(!decode(storage, sizeof(storage), output, sizeof(output)));
  assert(output[0] == '\0');
  return 0;
}
