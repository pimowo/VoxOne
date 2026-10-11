#ifndef VOXONE_CONFIG_EEPROM_STORAGE_H
#define VOXONE_CONFIG_EEPROM_STORAGE_H

#include <cstddef>
#include <cstdint>

namespace voxone {
namespace config_storage {

constexpr std::size_t kEepromSize = 768;
constexpr std::size_t kConfigOffset = 500;

bool beginEeprom();
void endEeprom();
bool eraseEepromRecord();

class ConfigEepromStorage {
 public:
  bool writeRecord(const uint8_t* bytes, std::size_t size);
  bool commit();
  bool readRecord(uint8_t* bytes, std::size_t size);
  bool readRecord(uint8_t* bytes, std::size_t size, bool& found);
};

}  // namespace config_storage
}  // namespace voxone

#endif
