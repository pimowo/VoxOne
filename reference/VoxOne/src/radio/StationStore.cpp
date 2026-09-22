#include "StationStore.h"

#include <algorithm>
#include <cstring>
#include "../diagnostics/Logger.h"

namespace {
constexpr char NVS_NAMESPACE[] = "stations";
constexpr char KEY_BLOB[] = "list";
constexpr uint32_t BLOB_MAGIC = 0x53585456; // "VTXS", little-endian marker.
constexpr uint16_t BLOB_VERSION = 1;
constexpr size_t HEADER_BYTES = 16;

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    for (uint8_t shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<uint8_t>(value >> shift));
}

bool readU16(const uint8_t*& cursor, const uint8_t* end, uint16_t& value) {
    if (end - cursor < 2) return false;
    value = static_cast<uint16_t>(cursor[0]) |
        (static_cast<uint16_t>(cursor[1]) << 8);
    cursor += 2;
    return true;
}

bool readU32(const uint8_t*& cursor, const uint8_t* end, uint32_t& value) {
    if (end - cursor < 4) return false;
    value = static_cast<uint32_t>(cursor[0]) |
        (static_cast<uint32_t>(cursor[1]) << 8) |
        (static_cast<uint32_t>(cursor[2]) << 16) |
        (static_cast<uint32_t>(cursor[3]) << 24);
    cursor += 4;
    return true;
}

uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool containsId(const std::vector<Station>& stations, uint16_t id) {
    for (const auto& station : stations)
        if (station.id == id) return true;
    return false;
}
}

std::vector<Station> StationStore::defaults() {
    return {
        {1, "Groove Salad", "http://ice5.somafm.com/groovesalad-128-mp3", 0},
        {2, "Drone Zone", "http://ice5.somafm.com/dronezone-128-mp3", 0},
        {3, "Secret Agent", "http://ice5.somafm.com/secretagent-128-mp3", 0},
    };
}

bool StationStore::begin() {
    if (!_opened) {
        if (!_prefs.begin(NVS_NAMESPACE, false)) {
            Logger::error("STATIONS", "NVS namespace open failed");
            return false;
        }
        _opened = true;
    }
    return load();
}

bool StationStore::validateFields(const String& name, const String& url,
                                  int volumeTrim, String* error) {
    auto fail = [error](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (name.isEmpty() || name.length() > MAX_NAME_BYTES)
        return fail("Nazwa stacji jest wymagana i moze miec maks. 48 bajtow.");
    const bool http = url.startsWith("http://");
    const bool https = url.startsWith("https://");
    const size_t prefixLength = https ? 8 : 7;
    if ((!http && !https) || url.length() <= prefixLength ||
        url.length() > MAX_URL_BYTES)
        return fail("URL musi zaczynac sie od http:// lub https:// (maks. 192 bajty).");
    if (volumeTrim < MIN_VOLUME_TRIM || volumeTrim > MAX_VOLUME_TRIM)
        return fail("Korekta glosnosci musi byc w zakresie -20..20.");
    for (size_t i = 0; i < name.length(); ++i) {
        const uint8_t ch = static_cast<uint8_t>(name[i]);
        if (ch < 32 || ch == 127) return fail("Nazwa zawiera niedozwolony znak.");
    }
    for (size_t i = 0; i < url.length(); ++i) {
        const uint8_t ch = static_cast<uint8_t>(url[i]);
        if (ch <= 32 || ch == 127) return fail("URL zawiera niedozwolony znak.");
    }
    return true;
}

bool StationStore::load() {
    if (!_opened) return false;
    const size_t length = _prefs.getBytesLength(KEY_BLOB);
    bool valid = length >= HEADER_BYTES && length <= 4096;
    std::vector<uint8_t> blob;
    if (valid) {
        blob.resize(length);
        valid = _prefs.getBytes(KEY_BLOB, blob.data(), blob.size()) == blob.size();
    }

    std::vector<Station> loaded;
    uint16_t nextId = 1;
    if (valid) {
        const uint8_t* cursor = blob.data();
        const uint8_t* end = cursor + blob.size();
        uint32_t magic = 0, storedCrc = 0;
        uint16_t version = 0, count = 0, payloadBytes = 0;
        valid = readU32(cursor, end, magic) && readU16(cursor, end, version) &&
            readU16(cursor, end, count) && readU16(cursor, end, nextId) &&
            readU16(cursor, end, payloadBytes) && readU32(cursor, end, storedCrc) &&
            magic == BLOB_MAGIC && version == BLOB_VERSION &&
            count <= MAX_STATIONS && nextId != 0 &&
            payloadBytes == static_cast<size_t>(end - cursor) &&
            crc32(cursor, payloadBytes) == storedCrc;
        loaded.reserve(count);
        for (uint16_t i = 0; valid && i < count; ++i) {
            uint16_t id = 0, urlLength = 0;
            if (!readU16(cursor, end, id) || end - cursor < 2) {
                valid = false;
                break;
            }
            const int8_t trim = static_cast<int8_t>(*cursor++);
            const uint8_t nameLength = *cursor++;
            if (!readU16(cursor, end, urlLength) || id == 0 ||
                nameLength == 0 || nameLength > MAX_NAME_BYTES ||
                urlLength == 0 || urlLength > MAX_URL_BYTES ||
                end - cursor < nameLength + urlLength || containsId(loaded, id)) {
                valid = false;
                break;
            }
            String name;
            String url;
            name.reserve(nameLength);
            url.reserve(urlLength);
            for (uint8_t n = 0; n < nameLength; ++n) name += char(*cursor++);
            for (uint16_t u = 0; u < urlLength; ++u) url += char(*cursor++);
            if (!validateFields(name, url, trim)) {
                valid = false;
                break;
            }
            loaded.push_back({id, name, url, trim});
        }
        valid = valid && cursor == end;
    }

    if (!valid) {
        Logger::warn("STATIONS", length == 0
            ? "fallback to defaults: no saved list"
            : "fallback to defaults: invalid station blob");
        loaded = defaults();
        nextId = 4;
        if (!persist(loaded, nextId)) return false;
    }
    _stations = std::move(loaded);
    _nextId = nextId;
    Logger::info("STATIONS", String("loaded count=") + _stations.size() +
        " version=" + BLOB_VERSION);
    return true;
}

bool StationStore::persist(const std::vector<Station>& stations,
                           uint16_t nextId) {
    if (!_opened || stations.size() > MAX_STATIONS || nextId == 0) return false;
    std::vector<uint8_t> payload;
    payload.reserve(stations.size() * 128);
    for (size_t i = 0; i < stations.size(); ++i) {
        const auto& station = stations[i];
        if (station.id == 0 ||
            !validateFields(station.name, station.url, station.volumeTrim)) return false;
        for (size_t previous = 0; previous < i; ++previous)
            if (stations[previous].id == station.id) return false;
        appendU16(payload, station.id);
        payload.push_back(static_cast<uint8_t>(station.volumeTrim));
        payload.push_back(static_cast<uint8_t>(station.name.length()));
        appendU16(payload, static_cast<uint16_t>(station.url.length()));
        payload.insert(payload.end(), station.name.c_str(),
                       station.name.c_str() + station.name.length());
        payload.insert(payload.end(), station.url.c_str(),
                       station.url.c_str() + station.url.length());
    }
    if (payload.size() > 0xffffu) return false;
    std::vector<uint8_t> blob;
    blob.reserve(HEADER_BYTES + payload.size());
    appendU32(blob, BLOB_MAGIC);
    appendU16(blob, BLOB_VERSION);
    appendU16(blob, static_cast<uint16_t>(stations.size()));
    appendU16(blob, nextId);
    appendU16(blob, static_cast<uint16_t>(payload.size()));
    appendU32(blob, crc32(payload.data(), payload.size()));
    blob.insert(blob.end(), payload.begin(), payload.end());
    if (_prefs.putBytes(KEY_BLOB, blob.data(), blob.size()) != blob.size() ||
        _prefs.getBytesLength(KEY_BLOB) != blob.size()) return false;
    std::vector<uint8_t> verify(blob.size());
    const bool verified =
        _prefs.getBytes(KEY_BLOB, verify.data(), verify.size()) == verify.size() &&
        memcmp(blob.data(), verify.data(), blob.size()) == 0;
    if (verified)
        Logger::info("STATIONS", String("saved count=") + stations.size());
    return verified;
}

bool StationStore::save() { return persist(_stations, _nextId); }

bool StationStore::resetDefaults() {
    auto candidate = defaults();
    if (!persist(candidate, 4)) return false;
    _stations = std::move(candidate);
    _nextId = 4;
    Logger::info("STATIONS", "factory defaults restored");
    return true;
}

const Station* StationStore::at(size_t index) const {
    return index < _stations.size() ? &_stations[index] : nullptr;
}

const Station* StationStore::getById(uint16_t id) const {
    if (id == 0) return nullptr;
    for (const auto& station : _stations)
        if (station.id == id) return &station;
    return nullptr;
}

const Station* StationStore::first() const { return at(0); }

size_t StationStore::indexOfId(uint16_t id) const {
    for (size_t i = 0; i < _stations.size(); ++i)
        if (_stations[i].id == id) return i;
    return _stations.size();
}

uint16_t StationStore::positionOfId(uint16_t id) const {
    const size_t index = indexOfId(id);
    return index < _stations.size() ? static_cast<uint16_t>(index + 1) : 0;
}

const Station* StationStore::next(uint16_t id) const {
    if (_stations.empty()) return nullptr;
    const size_t index = indexOfId(id);
    return index < _stations.size() ? at((index + 1) % _stations.size()) : first();
}

const Station* StationStore::previous(uint16_t id) const {
    if (_stations.empty()) return nullptr;
    const size_t index = indexOfId(id);
    return index < _stations.size()
        ? at((index + _stations.size() - 1) % _stations.size()) : first();
}

uint16_t StationStore::allocateId(const std::vector<Station>& stations) const {
    uint16_t candidate = _nextId ? _nextId : 1;
    for (uint32_t attempts = 0; attempts < 65535; ++attempts) {
        if (!containsId(stations, candidate)) return candidate;
        if (++candidate == 0) candidate = 1;
    }
    return 0;
}

bool StationStore::add(const String& name, const String& url, int volumeTrim,
                       uint16_t& assignedId) {
    assignedId = 0;
    if (_stations.size() >= MAX_STATIONS ||
        !validateFields(name, url, volumeTrim)) return false;
    auto candidate = _stations;
    const uint16_t id = allocateId(candidate);
    if (!id) return false;
    candidate.push_back({id, name, url, static_cast<int8_t>(volumeTrim)});
    uint16_t nextId = id + 1;
    if (nextId == 0) nextId = 1;
    if (!persist(candidate, nextId)) return false;
    _stations = std::move(candidate);
    _nextId = nextId;
    assignedId = id;
    Logger::info("STATIONS", String("added id=") + id + " name=" + name);
    return true;
}

bool StationStore::update(uint16_t id, const String& name, const String& url,
                          int volumeTrim) {
    const size_t index = indexOfId(id);
    if (index >= _stations.size() || !validateFields(name, url, volumeTrim)) return false;
    auto candidate = _stations;
    candidate[index] = {id, name, url, static_cast<int8_t>(volumeTrim)};
    if (!persist(candidate, _nextId)) return false;
    _stations = std::move(candidate);
    Logger::info("STATIONS", String("updated id=") + id);
    return true;
}

bool StationStore::remove(uint16_t id) {
    const size_t index = indexOfId(id);
    if (index >= _stations.size()) return false;
    auto candidate = _stations;
    candidate.erase(candidate.begin() + index);
    if (!persist(candidate, _nextId)) return false;
    _stations = std::move(candidate);
    Logger::info("STATIONS", String("removed id=") + id);
    return true;
}

bool StationStore::moveUp(uint16_t id) {
    const size_t index = indexOfId(id);
    if (index == 0 || index >= _stations.size()) return false;
    auto candidate = _stations;
    std::swap(candidate[index - 1], candidate[index]);
    if (!persist(candidate, _nextId)) return false;
    _stations = std::move(candidate);
    Logger::info("STATIONS", String("moved up id=") + id);
    return true;
}

bool StationStore::moveDown(uint16_t id) {
    const size_t index = indexOfId(id);
    if (index >= _stations.size() || index + 1 >= _stations.size()) return false;
    auto candidate = _stations;
    std::swap(candidate[index], candidate[index + 1]);
    if (!persist(candidate, _nextId)) return false;
    _stations = std::move(candidate);
    Logger::info("STATIONS", String("moved down id=") + id);
    return true;
}
