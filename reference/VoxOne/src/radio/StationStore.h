#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <vector>

struct Station {
    uint16_t id;
    String name;
    String url;
    int8_t volumeTrim;
};

class StationStore {
public:
    static constexpr size_t MAX_STATIONS = 16;
    static constexpr size_t MAX_NAME_BYTES = 48;
    static constexpr size_t MAX_URL_BYTES = 192;
    static constexpr int8_t MIN_VOLUME_TRIM = -20;
    static constexpr int8_t MAX_VOLUME_TRIM = 20;

    bool begin();
    bool load();
    bool save();
    bool resetDefaults();

    size_t count() const { return _stations.size(); }
    const Station* at(size_t index) const;
    const Station* getById(uint16_t id) const;
    const Station* first() const;
    const Station* next(uint16_t id) const;
    const Station* previous(uint16_t id) const;
    bool isValidId(uint16_t id) const { return getById(id) != nullptr; }
    size_t indexOfId(uint16_t id) const;
    uint16_t positionOfId(uint16_t id) const;

    bool add(const String& name, const String& url, int volumeTrim,
             uint16_t& assignedId);
    bool update(uint16_t id, const String& name, const String& url,
                int volumeTrim);
    bool remove(uint16_t id);
    bool moveUp(uint16_t id);
    bool moveDown(uint16_t id);

    static bool validateFields(const String& name, const String& url,
                               int volumeTrim, String* error = nullptr);

private:
    Preferences _prefs;
    std::vector<Station> _stations;
    uint16_t _nextId = 1;
    bool _opened = false;

    bool persist(const std::vector<Station>& stations, uint16_t nextId);
    uint16_t allocateId(const std::vector<Station>& stations) const;
    static std::vector<Station> defaults();
};
