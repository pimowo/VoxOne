#include "www_asset_integrity.h"

#include "www_readiness.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <mbedtls/sha256.h>
#endif

namespace voxone {
namespace www_integrity {
namespace {

constexpr uint8_t kMagic[4] = {'V', 'X', 'W', 'M'};
constexpr uint16_t kHeaderSize = 64;
constexpr uint16_t kEntrySize = 112;
constexpr uint8_t kCompressionGzip = 1;
constexpr uint8_t kAssetSetPrefix[] = {'V', 'X', 'A', 'S', 'S', 'E', 'T', 0};

uint16_t readLe16(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) |
         (static_cast<uint16_t>(input[1]) << 8);
}

uint32_t readLe32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         (static_cast<uint32_t>(input[1]) << 8) |
         (static_cast<uint32_t>(input[2]) << 16) |
         (static_cast<uint32_t>(input[3]) << 24);
}

void writeLe16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, std::size_t size) {
  while (size--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc;
}

uint32_t crc32(const uint8_t* data, std::size_t size) {
  return crc32Update(0xFFFFFFFFU, data, size) ^ 0xFFFFFFFFU;
}

bool equalBytes(const uint8_t* left, const uint8_t* right, std::size_t size) {
  uint8_t difference = 0;
  for (std::size_t i = 0; i < size; ++i) difference |= left[i] ^ right[i];
  return difference == 0;
}

#if defined(ARDUINO)
class Sha256 {
 public:
  Sha256() { mbedtls_sha256_init(&context_); }
  ~Sha256() { mbedtls_sha256_free(&context_); }
  bool begin() { return mbedtls_sha256_starts_ret(&context_, 0) == 0; }
  bool update(const uint8_t* input, std::size_t size) {
    return mbedtls_sha256_update_ret(&context_, input, size) == 0;
  }
  bool finish(uint8_t output[kSha256Size]) {
    return mbedtls_sha256_finish_ret(&context_, output) == 0;
  }
 private:
  mbedtls_sha256_context context_{};
};
#else
uint32_t rotateRight(uint32_t value, uint8_t count) {
  return (value >> count) | (value << (32U - count));
}

class Sha256 {
 public:
  bool begin() {
    const uint32_t initial[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::memcpy(state_, initial, sizeof(state_));
    totalBytes_ = 0;
    blockSize_ = 0;
    return true;
  }

  bool update(const uint8_t* input, std::size_t size) {
    if (!input && size) return false;
    totalBytes_ += size;
    while (size) {
      const std::size_t available = sizeof(block_) - blockSize_;
      const std::size_t copied = size < available ? size : available;
      std::memcpy(block_ + blockSize_, input, copied);
      blockSize_ += copied;
      input += copied;
      size -= copied;
      if (blockSize_ == sizeof(block_)) {
        transform(block_);
        blockSize_ = 0;
      }
    }
    return true;
  }

  bool finish(uint8_t output[kSha256Size]) {
    if (!output) return false;
    const uint64_t totalBits = totalBytes_ * 8U;
    block_[blockSize_++] = 0x80;
    if (blockSize_ > 56) {
      std::memset(block_ + blockSize_, 0, sizeof(block_) - blockSize_);
      transform(block_);
      blockSize_ = 0;
    }
    std::memset(block_ + blockSize_, 0, 56 - blockSize_);
    for (std::size_t i = 0; i < 8; ++i)
      block_[63 - i] = static_cast<uint8_t>(totalBits >> (i * 8));
    transform(block_);
    for (std::size_t i = 0; i < 8; ++i) {
      output[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
      output[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
      output[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
      output[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
    }
    return true;
  }

 private:
  void transform(const uint8_t block[64]) {
    static const uint32_t constants[64] = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,
        0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,
        0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,
        0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,
        0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,
        0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,
        0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,
        0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,
        0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
    uint32_t words[64];
    for (std::size_t i = 0; i < 16; ++i) {
      const std::size_t offset = i * 4;
      words[i] = (static_cast<uint32_t>(block[offset]) << 24) |
                 (static_cast<uint32_t>(block[offset + 1]) << 16) |
                 (static_cast<uint32_t>(block[offset + 2]) << 8) |
                 static_cast<uint32_t>(block[offset + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const uint32_t s0 = rotateRight(words[i - 15], 7) ^
                          rotateRight(words[i - 15], 18) ^
                          (words[i - 15] >> 3);
      const uint32_t s1 = rotateRight(words[i - 2], 17) ^
                          rotateRight(words[i - 2], 19) ^
                          (words[i - 2] >> 10);
      words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3];
    uint32_t e=state_[4],f=state_[5],g=state_[6],h=state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const uint32_t sum1 = rotateRight(e,6)^rotateRight(e,11)^rotateRight(e,25);
      const uint32_t choose = (e & f) ^ ((~e) & g);
      const uint32_t temp1 = h + sum1 + choose + constants[i] + words[i];
      const uint32_t sum0 = rotateRight(a,2)^rotateRight(a,13)^rotateRight(a,22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = sum0 + majority;
      h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
  }

  uint32_t state_[8]{};
  uint64_t totalBytes_ = 0;
  uint8_t block_[64]{};
  std::size_t blockSize_ = 0;
};
#endif

struct ManifestHeader {
  uint16_t schema;
  uint16_t wwwContract;
  uint16_t firmwareMin;
  uint16_t firmwareMax;
  uint16_t entryCount;
  uint32_t totalSize;
  uint8_t assetSet[kSha256Size];
  uint32_t recordsCrc;
};

struct AssetRecord {
  char path[kAssetPathCapacity];
  uint32_t size;
  uint8_t sha256[kSha256Size];
  uint8_t compression;
};

void setProblem(Report& report, const char* path) {
  if (!path) return;
  std::snprintf(report.problemAsset, sizeof(report.problemAsset), "%s", path);
}

Report failure(Status status, IntegrityStatus integrity,
               ContractStatus contract, const char* path = nullptr) {
  Report result;
  result.status = status;
  result.integrity = integrity;
  result.contract = contract;
  setProblem(result, path);
  return result;
}

bool readExact(AssetFile& file, uint8_t* output, std::size_t size) {
  std::size_t offset = 0;
  while (offset < size) {
    std::size_t received = 0;
    if (!file.read(output + offset, size - offset, received) || !received ||
        received > size - offset)
      return false;
    offset += received;
  }
  return true;
}

bool decodeHeader(const uint8_t raw[kHeaderSize], ManifestHeader& output) {
  if (std::memcmp(raw, kMagic, sizeof(kMagic)) != 0 ||
      readLe16(raw + 4) != kWwwManifestSchema ||
      readLe16(raw + 6) != kHeaderSize || readLe16(raw + 16) != kEntrySize ||
      readLe16(raw + 18) != 0 ||
      crc32(raw, kHeaderSize - 4) != readLe32(raw + 60))
    return false;
  output.schema = readLe16(raw + 4);
  output.wwwContract = readLe16(raw + 8);
  output.firmwareMin = readLe16(raw + 10);
  output.firmwareMax = readLe16(raw + 12);
  output.entryCount = readLe16(raw + 14);
  output.totalSize = readLe32(raw + 20);
  std::memcpy(output.assetSet, raw + 24, sizeof(output.assetSet));
  output.recordsCrc = readLe32(raw + 56);
  return output.entryCount == kCurrentWwwAssetCount &&
         output.firmwareMin <= output.firmwareMax &&
         output.totalSize == static_cast<uint32_t>(kHeaderSize) +
                                 static_cast<uint32_t>(output.entryCount) *
                                     static_cast<uint32_t>(kEntrySize);
}

bool decodeRecord(const uint8_t raw[kEntrySize], AssetRecord& output) {
  const void* terminator = std::memchr(raw, 0, kAssetPathCapacity);
  if (!terminator || raw[0] != '/' || raw[101] != 0) return false;
  for (std::size_t i = 101; i < kEntrySize; ++i)
    if (raw[i] != 0) return false;
  const std::size_t length = static_cast<const uint8_t*>(terminator) - raw;
  if (!length || length >= sizeof(output.path)) return false;
  std::memcpy(output.path, raw, length);
  output.path[length] = 0;
  output.size = readLe32(raw + 64);
  std::memcpy(output.sha256, raw + 68, sizeof(output.sha256));
  output.compression = raw[100];
  return output.size >= 18 && output.compression == kCompressionGzip;
}

Status validateGzipFraming(AssetFile& file) {
  uint8_t header[10];
  if (!file.seek(0) || !readExact(file, header, sizeof(header)))
    return Status::ReadError;
  return header[0] == 0x1f && header[1] == 0x8b && header[2] == 0x08 &&
                 (header[3] & 0xE0) == 0
             ? Status::Ready
             : Status::GzipInvalid;
}

Status hashAsset(AssetFile& file, const AssetRecord& record,
                 uint8_t output[kSha256Size], uint32_t& bytesChecked) {
  if (file.size() != record.size) return Status::SizeMismatch;
  const Status gzipStatus = validateGzipFraming(file);
  if (gzipStatus != Status::Ready) return gzipStatus;
  if (!file.seek(0)) return Status::ReadError;
  Sha256 sha;
  if (!sha.begin()) return Status::ReadError;
  uint8_t buffer[kVerifyBufferSize];
  uint32_t total = 0;
  while (total < record.size) {
    const std::size_t wanted =
        record.size - total < sizeof(buffer) ? record.size - total : sizeof(buffer);
    std::size_t received = 0;
    if (!file.read(buffer, wanted, received) || !received || received > wanted)
      return Status::ReadError;
    if (!sha.update(buffer, received)) return Status::ReadError;
    total += received;
  }
  if (total != record.size || !sha.finish(output)) return Status::ReadError;
  bytesChecked += total;
  return equalBytes(output, record.sha256, kSha256Size)
             ? Status::Ready : Status::HashMismatch;
}

bool legacyAssetsPresent(AssetStore& store, const char** missing) {
  for (std::size_t i = 0; i < kCurrentWwwAssetCount; ++i) {
    if (!store.exists(kCurrentWwwAssetPaths[i])) {
      if (missing) *missing = kCurrentWwwAssetPaths[i];
      return false;
    }
  }
  return true;
}

}  // namespace

ContractStatus evaluateContract(uint16_t firmwareContract,
                                uint16_t requiredFirmwareMin,
                                uint16_t requiredFirmwareMax) {
  if (requiredFirmwareMin > requiredFirmwareMax)
    return ContractStatus::Unverifiable;
  return firmwareContract >= requiredFirmwareMin &&
                 firmwareContract <= requiredFirmwareMax
             ? ContractStatus::Compatible : ContractStatus::Mismatch;
}

Report verify(AssetStore& store, uint16_t firmwareContract) {
  if (!store.mounted())
    return failure(Status::SpiffsUnavailable, IntegrityStatus::Unavailable,
                   ContractStatus::Unverifiable);

  if (!store.exists(kWwwAssetManifestPath)) {
    const char* missing = nullptr;
    if (!legacyAssetsPresent(store, &missing))
      return failure(Status::FileMissing, IntegrityStatus::Failed,
                     ContractStatus::Unverifiable, missing);
    Report legacy = failure(Status::ContractUnverifiable,
                            IntegrityStatus::Unverified,
                            ContractStatus::Unverifiable,
                            kWwwAssetManifestPath);
    legacy.wwwUsable = true;
    return legacy;
  }

  AssetFile& manifest = store.manifestFile();
  if (!manifest.open(kWwwAssetManifestPath))
    return failure(Status::ReadError, IntegrityStatus::Failed,
                   ContractStatus::Unverifiable, kWwwAssetManifestPath);
  uint8_t rawHeader[kHeaderSize];
  if (manifest.size() < kHeaderSize ||
      !readExact(manifest, rawHeader, sizeof(rawHeader))) {
    manifest.close();
    return failure(Status::ManifestInvalid, IntegrityStatus::Failed,
                   ContractStatus::Unverifiable, kWwwAssetManifestPath);
  }
  ManifestHeader header{};
  if (!decodeHeader(rawHeader, header) || manifest.size() != header.totalSize) {
    manifest.close();
    return failure(Status::ManifestInvalid, IntegrityStatus::Failed,
                   ContractStatus::Unverifiable, kWwwAssetManifestPath);
  }

  Report result;
  result.manifestSchema = header.schema;
  result.wwwContract = header.wwwContract;
  result.firmwareContractMin = header.firmwareMin;
  result.firmwareContractMax = header.firmwareMax;
  result.contract = evaluateContract(
      firmwareContract, header.firmwareMin, header.firmwareMax);
  result.assetSetAvailable = true;
  std::memcpy(result.assetSetId, header.assetSet, sizeof(result.assetSetId));

  Sha256 setHash;
  uint8_t canonical[12];
  writeLe16(canonical, header.schema);
  writeLe16(canonical + 2, header.wwwContract);
  writeLe16(canonical + 4, header.firmwareMin);
  writeLe16(canonical + 6, header.firmwareMax);
  writeLe16(canonical + 8, header.entryCount);
  writeLe16(canonical + 10, kEntrySize);
  if (!setHash.begin() ||
      !setHash.update(kAssetSetPrefix, sizeof(kAssetSetPrefix)) ||
      !setHash.update(canonical, sizeof(canonical))) {
    manifest.close();
    return failure(Status::ReadError, IntegrityStatus::Failed, result.contract,
                   kWwwAssetManifestPath);
  }
  uint32_t recordsCrc = 0xFFFFFFFFU;
  uint8_t rawRecord[kEntrySize];
  for (std::size_t index = 0; index < header.entryCount; ++index) {
    if (!readExact(manifest, rawRecord, sizeof(rawRecord))) {
      manifest.close();
      return failure(Status::ManifestInvalid, IntegrityStatus::Failed,
                     result.contract, kWwwAssetManifestPath);
    }
    AssetRecord record{};
    if (!decodeRecord(rawRecord, record) ||
        std::strcmp(record.path, kCurrentWwwAssetPaths[index]) != 0 ||
        !setHash.update(rawRecord, sizeof(rawRecord))) {
      manifest.close();
      return failure(Status::ManifestInvalid, IntegrityStatus::Failed,
                     result.contract, kWwwAssetManifestPath);
    }
    recordsCrc = crc32Update(recordsCrc, rawRecord, sizeof(rawRecord));
  }
  manifest.close();
  uint8_t calculatedSet[kSha256Size];
  recordsCrc ^= 0xFFFFFFFFU;
  if (recordsCrc != header.recordsCrc || !setHash.finish(calculatedSet) ||
      !equalBytes(calculatedSet, header.assetSet, sizeof(calculatedSet)))
    return failure(Status::ManifestInvalid, IntegrityStatus::Failed,
                   result.contract, kWwwAssetManifestPath);

  if (!manifest.open(kWwwAssetManifestPath) || !manifest.seek(kHeaderSize)) {
    manifest.close();
    return failure(Status::ReadError, IntegrityStatus::Failed, result.contract,
                   kWwwAssetManifestPath);
  }
  for (std::size_t index = 0; index < header.entryCount; ++index) {
    if (!readExact(manifest, rawRecord, sizeof(rawRecord))) {
      manifest.close();
      return failure(Status::ReadError, IntegrityStatus::Failed, result.contract,
                     kWwwAssetManifestPath);
    }
    AssetRecord record{};
    if (!decodeRecord(rawRecord, record)) {
      manifest.close();
      return failure(Status::ManifestInvalid, IntegrityStatus::Failed,
                     result.contract, kWwwAssetManifestPath);
    }
    AssetFile& asset = store.assetFile();
    if (!asset.open(record.path)) {
      manifest.close();
      return failure(store.exists(record.path) ? Status::ReadError
                                               : Status::FileMissing,
                     IntegrityStatus::Failed, result.contract, record.path);
    }
    uint8_t digest[kSha256Size];
    const Status assetStatus =
        hashAsset(asset, record, digest, result.bytesChecked);
    asset.close();
    if (assetStatus != Status::Ready) {
      manifest.close();
      Report failed = failure(assetStatus, IntegrityStatus::Failed,
                              result.contract, record.path);
      failed.manifestSchema = result.manifestSchema;
      failed.wwwContract = result.wwwContract;
      failed.firmwareContractMin = result.firmwareContractMin;
      failed.firmwareContractMax = result.firmwareContractMax;
      failed.assetSetAvailable = true;
      std::memcpy(failed.assetSetId, result.assetSetId, sizeof(failed.assetSetId));
      failed.filesChecked = result.filesChecked;
      failed.bytesChecked = result.bytesChecked;
      return failed;
    }
    ++result.filesChecked;
  }
  manifest.close();
  if (result.contract != ContractStatus::Compatible) {
    result.status = Status::ContractMismatch;
    result.integrity = IntegrityStatus::Verified;
    result.wwwUsable = false;
    return result;
  }
  result.status = Status::Ready;
  result.integrity = IntegrityStatus::Verified;
  result.wwwUsable = true;
  return result;
}

const char* statusName(Status status) {
  switch (status) {
    case Status::SpiffsUnavailable: return "SPIFFS_UNAVAILABLE";
    case Status::FileMissing: return "FILE_MISSING";
    case Status::SizeMismatch: return "SIZE_MISMATCH";
    case Status::HashMismatch: return "HASH_MISMATCH";
    case Status::GzipInvalid: return "GZIP_INVALID";
    case Status::ContractMismatch: return "CONTRACT_MISMATCH";
    case Status::ContractUnverifiable: return "CONTRACT_UNVERIFIABLE";
    case Status::ManifestInvalid: return "MANIFEST_INVALID";
    case Status::ReadError: return "READ_ERROR";
    case Status::Ready: return "READY";
  }
  return "READ_ERROR";
}

const char* integrityStatusName(IntegrityStatus status) {
  switch (status) {
    case IntegrityStatus::Unavailable: return "unavailable";
    case IntegrityStatus::Unverified: return "unverified";
    case IntegrityStatus::Failed: return "failed";
    case IntegrityStatus::Verified: return "verified";
  }
  return "failed";
}

const char* contractStatusName(ContractStatus status) {
  switch (status) {
    case ContractStatus::Unverifiable: return "unverifiable";
    case ContractStatus::Mismatch: return "mismatch";
    case ContractStatus::Compatible: return "compatible";
  }
  return "unverifiable";
}

bool formatJson(const Report& report, char* output, std::size_t capacity) {
  if (!output || !capacity) return false;
  char assetSet[kSha256Size * 2 + 1]{};
  if (report.assetSetAvailable) {
    static const char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < kSha256Size; ++i) {
      assetSet[i * 2] = hex[report.assetSetId[i] >> 4];
      assetSet[i * 2 + 1] = hex[report.assetSetId[i] & 0x0f];
    }
  }
  const char* error = report.status == Status::Ready ? "" : statusName(report.status);
  const int written = std::snprintf(
      output, capacity,
      "{\"status\":\"%s\",\"integrity\":\"%s\","
      "\"contract\":\"%s\",\"assetSet\":\"%s\","
      "\"error\":\"%s\",\"resource\":\"%s\",\"usable\":%s,"
      "\"manifestSchema\":%u,\"wwwContract\":%u,"
      "\"firmwareContract\":%u,\"firmwareContractMin\":%u,"
      "\"firmwareContractMax\":%u,\"filesChecked\":%u,"
      "\"bytesChecked\":%lu,\"verifyMs\":%lu,"
      "\"runtimeGzip\":\"framing-and-compressed-sha256\","
      "\"buildGzip\":\"full-decompression-and-trailer-crc\","
      "\"trust\":\"integrity-metadata-not-signature\"}",
      statusName(report.status), integrityStatusName(report.integrity),
      contractStatusName(report.contract), assetSet, error,
      report.problemAsset, report.wwwUsable ? "true" : "false",
      static_cast<unsigned>(report.manifestSchema),
      static_cast<unsigned>(report.wwwContract),
      static_cast<unsigned>(kFirmwareWwwContract),
      static_cast<unsigned>(report.firmwareContractMin),
      static_cast<unsigned>(report.firmwareContractMax),
      static_cast<unsigned>(report.filesChecked),
      static_cast<unsigned long>(report.bytesChecked),
      static_cast<unsigned long>(report.elapsedMs));
  return written >= 0 && static_cast<std::size_t>(written) < capacity;
}

}  // namespace www_integrity
}  // namespace voxone
