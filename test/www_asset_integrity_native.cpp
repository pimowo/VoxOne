#include "../src/core/www_asset_integrity.h"
#include "../src/core/www_readiness.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;

Bytes readFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return Bytes(std::istreambuf_iterator<char>(input),
               std::istreambuf_iterator<char>());
}

class MemoryFile : public voxone::www_integrity::AssetFile {
 public:
  MemoryFile(const std::map<std::string, Bytes>& files,
             const std::string& failPath)
      : files_(files), failPath_(failPath) {}

  bool open(const char* path) override {
    close();
    const auto found = files_.find(path ? path : "");
    if (found == files_.end()) return false;
    path_ = found->first;
    bytes_ = &found->second;
    return true;
  }
  void close() override {
    path_.clear();
    bytes_ = nullptr;
    position_ = 0;
  }
  uint32_t size() const override {
    return bytes_ ? static_cast<uint32_t>(bytes_->size()) : 0;
  }
  bool seek(uint32_t offset) override {
    if (!bytes_ || offset > bytes_->size()) return false;
    position_ = offset;
    return true;
  }
  bool read(uint8_t* output, std::size_t capacity,
            std::size_t& bytesRead) override {
    bytesRead = 0;
    if (!bytes_ || path_ == failPath_) return false;
    const std::size_t left = bytes_->size() - position_;
    const std::size_t count = left < capacity ? left : capacity;
    if (!count) return true;
    std::copy(bytes_->begin() + position_, bytes_->begin() + position_ + count,
              output);
    position_ += count;
    bytesRead = count;
    return true;
  }

 private:
  const std::map<std::string, Bytes>& files_;
  const std::string& failPath_;
  std::string path_;
  const Bytes* bytes_ = nullptr;
  std::size_t position_ = 0;
};

class MemoryStore : public voxone::www_integrity::AssetStore {
 public:
  explicit MemoryStore(std::map<std::string, Bytes> files, bool mounted = true,
                       std::string failPath = {})
      : files_(std::move(files)), mounted_(mounted), failPath_(std::move(failPath)),
        manifest_(files_, failPath_), asset_(files_, failPath_) {}
  bool mounted() const override { return mounted_; }
  bool exists(const char* path) const override {
    return files_.find(path ? path : "") != files_.end();
  }
  voxone::www_integrity::AssetFile& manifestFile() override { return manifest_; }
  voxone::www_integrity::AssetFile& assetFile() override { return asset_; }

 private:
  std::map<std::string, Bytes> files_;
  bool mounted_;
  std::string failPath_;
  MemoryFile manifest_;
  MemoryFile asset_;
};

std::map<std::string, Bytes> canonicalFiles() {
  std::map<std::string, Bytes> files;
  files[voxone::kWwwAssetManifestPath] = readFile("data/www/assets.vxwm");
  for (std::size_t i = 0; i < voxone::kCurrentWwwAssetCount; ++i) {
    const std::string path = voxone::kCurrentWwwAssetPaths[i];
    files[path] = readFile("data" + path);
  }
  return files;
}

voxone::www_integrity::Report verify(std::map<std::string, Bytes> files,
                                     bool mounted = true,
                                     const std::string& failPath = {}) {
  MemoryStore store(std::move(files), mounted, failPath);
  return voxone::www_integrity::verify(store, voxone::kFirmwareWwwContract);
}
}  // namespace

int main() {
  using namespace voxone::www_integrity;
  static_assert(kVerifyBufferSize == 512, "W0 verification buffer grew");

  const auto files = canonicalFiles();
  const auto started = std::chrono::steady_clock::now();
  const Report ready = verify(files);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  assert(ready.status == Status::Ready);
  assert(ready.integrity == IntegrityStatus::Verified);
  assert(ready.contract == ContractStatus::Compatible);
  assert(ready.wwwUsable && ready.assetSetAvailable);
  assert(ready.filesChecked == voxone::kCurrentWwwAssetCount);
  assert(ready.bytesChecked > 0);
  assert(elapsed < 5000);

  auto missing = files;
  missing.erase(voxone::kCurrentWwwAssetPaths[2]);
  assert(verify(std::move(missing)).status == Status::FileMissing);

  auto shortFile = files;
  shortFile[voxone::kCurrentWwwAssetPaths[0]].pop_back();
  assert(verify(std::move(shortFile)).status == Status::SizeMismatch);

  auto changed = files;
  changed[voxone::kCurrentWwwAssetPaths[1]][20] ^= 0x01;
  assert(verify(std::move(changed)).status == Status::HashMismatch);

  auto badGzip = files;
  badGzip[voxone::kCurrentWwwAssetPaths[3]][0] = 0;
  assert(verify(std::move(badGzip)).status == Status::GzipInvalid);

  auto badManifest = files;
  badManifest[voxone::kWwwAssetManifestPath].pop_back();
  assert(verify(std::move(badManifest)).status == Status::ManifestInvalid);

  assert(verify(files, false).status == Status::SpiffsUnavailable);
  assert(verify(files, true, voxone::kCurrentWwwAssetPaths[4]).status ==
         Status::ReadError);

  auto legacy = files;
  legacy.erase(voxone::kWwwAssetManifestPath);
  const Report legacyResult = verify(std::move(legacy));
  assert(legacyResult.status == Status::ContractUnverifiable);
  assert(legacyResult.integrity == IntegrityStatus::Unverified);
  assert(legacyResult.wwwUsable);

  assert(evaluateContract(2, 1, 3) == ContractStatus::Compatible);
  assert(evaluateContract(1, 1, 1) == ContractStatus::Compatible);
  assert(evaluateContract(1, 2, 3) == ContractStatus::Mismatch);
  assert(evaluateContract(1, 3, 2) == ContractStatus::Unverifiable);

  char json[768];
  assert(formatJson(ready, json, sizeof(json)));
  const std::string status(json);
  assert(status.find("\"status\":\"READY\"") != std::string::npos);
  assert(status.find("\"contract\":\"compatible\"") != std::string::npos);
  assert(status.find("not-signature") != std::string::npos);

  // Status reads only format the cached report. They cannot open or mutate
  // storage, so repeated API reads remain deterministic and write-free.
  char repeated[768];
  assert(formatJson(ready, repeated, sizeof(repeated)));
  assert(status == repeated);

  std::cout << "WWW asset integrity PASS; verified " << ready.bytesChecked
            << " bytes in " << elapsed << " ms; runtime buffer "
            << kVerifyBufferSize << " B\n";
}
