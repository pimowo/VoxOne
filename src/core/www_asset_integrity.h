#ifndef VOXONE_WWW_ASSET_INTEGRITY_H
#define VOXONE_WWW_ASSET_INTEGRITY_H

#include <cstddef>
#include <cstdint>

namespace voxone {
namespace www_integrity {

constexpr std::size_t kSha256Size = 32;
constexpr std::size_t kAssetPathCapacity = 64;
constexpr std::size_t kVerifyBufferSize = 512;

enum class Status : uint8_t {
  SpiffsUnavailable,
  FileMissing,
  SizeMismatch,
  HashMismatch,
  GzipInvalid,
  ContractMismatch,
  ContractUnverifiable,
  ManifestInvalid,
  ReadError,
  Ready,
};

enum class IntegrityStatus : uint8_t { Unavailable, Unverified, Failed, Verified };
enum class ContractStatus : uint8_t { Unverifiable, Mismatch, Compatible };

struct Report {
  Status status = Status::SpiffsUnavailable;
  IntegrityStatus integrity = IntegrityStatus::Unavailable;
  ContractStatus contract = ContractStatus::Unverifiable;
  uint16_t manifestSchema = 0;
  uint16_t wwwContract = 0;
  uint16_t firmwareContractMin = 0;
  uint16_t firmwareContractMax = 0;
  uint16_t filesChecked = 0;
  uint32_t bytesChecked = 0;
  uint32_t elapsedMs = 0;
  bool assetSetAvailable = false;
  bool wwwUsable = false;
  uint8_t assetSetId[kSha256Size]{};
  char problemAsset[kAssetPathCapacity]{};
};

class AssetFile {
 public:
  virtual ~AssetFile() {}
  virtual bool open(const char* path) = 0;
  virtual void close() = 0;
  virtual uint32_t size() const = 0;
  virtual bool seek(uint32_t offset) = 0;
  virtual bool read(uint8_t* output, std::size_t capacity,
                    std::size_t& bytesRead) = 0;
};

class AssetStore {
 public:
  virtual ~AssetStore() {}
  virtual bool mounted() const = 0;
  virtual bool exists(const char* path) const = 0;
  virtual AssetFile& manifestFile() = 0;
  virtual AssetFile& assetFile() = 0;
};

ContractStatus evaluateContract(uint16_t firmwareContract,
                                uint16_t requiredFirmwareMin,
                                uint16_t requiredFirmwareMax);
Report verify(AssetStore& store, uint16_t firmwareContract);

const char* statusName(Status status);
const char* integrityStatusName(IntegrityStatus status);
const char* contractStatusName(ContractStatus status);
bool formatJson(const Report& report, char* output, std::size_t capacity);

}  // namespace www_integrity
}  // namespace voxone

#endif
