#include "AudioOutputManager.h"
#include <cmath>
#include <cstring>

#include "../diagnostics/Logger.h"
#include "../diagnostics/PerfDiagnostics.h"

namespace {
constexpr uint32_t OUTPUT_GAIN_LIMIT_Q15 = 16384; // 0.5 amplitude, about -6.02 dB.
}

void PcmGainOutput::setVolume(int logicalVolume) {
    const int v = constrain(logicalVolume, 0, 100);
    // One perceptual curve for every PCM source; evaluated only on changes.
    const float amplitude = powf(static_cast<float>(v) / 100.0f, 2.2f);
    _gainQ15.store(
        static_cast<uint32_t>(amplitude * OUTPUT_GAIN_LIMIT_Q15 + 0.5f),
        std::memory_order_relaxed);
}

size_t PcmGainOutput::write(const uint8_t* data, size_t size) {
    if (!_target || !data || (size & 1U)) return 0; // 16-bit PCM only.
    const uint32_t started = micros();
    uint8_t samples[256];
    size_t sent = 0;
    while (sent < size) {
        const size_t count = min(size - sent, sizeof(samples));
        const uint32_t gain = _gainQ15.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; i += sizeof(int16_t)) {
            int16_t sample;
            memcpy(&sample, data + sent + i, sizeof(sample));
            const int16_t scaled = static_cast<int32_t>(sample) * static_cast<int32_t>(gain) / 32768;
            memcpy(samples + i, &scaled, sizeof(scaled));
        }
        const size_t written = _target->write(samples, count);
        sent += written;
        if (written != count) {
            PerfDiagnostics::recordI2sWrite(micros() - started, size, sent);
            return sent;
        }
    }
    PerfDiagnostics::recordI2sWrite(micros() - started, size, sent);
    return sent;
}

namespace {
const char* ownerName(AudioOutputOwner owner) {
    switch (owner) {
        case AudioOutputOwner::None: return "NONE";
        case AudioOutputOwner::Bluetooth: return "BLUETOOTH";
        case AudioOutputOwner::Radio: return "RADIO";
        case AudioOutputOwner::PlayMedia: return "PLAY_MEDIA";
        default: return "INVALID";
    }
}

bool validOwner(AudioOutputOwner owner) {
    return owner == AudioOutputOwner::Bluetooth ||
        owner == AudioOutputOwner::Radio ||
        owner == AudioOutputOwner::PlayMedia;
}
}

bool AudioOutputManager::begin(int bclk, int lrclk, int dout) {
    // I2S is created only when a source successfully acquires the lease.
    _bclk = bclk;
    _lrclk = lrclk;
    _dout = dout;
    _begun = true;
    return true;
}

bool AudioOutputManager::acquire(AudioOutputOwner requested) {
    if (!_begun || !validOwner(requested) || _fault) {
        Logger::warn("AUDIO", String("Acquire rejected: ") + ownerName(requested));
        return false;
    }
    if (_owner == requested) return true;
    if (_owner != AudioOutputOwner::None) {
        Logger::warn("AUDIO", String("Acquire denied: ") + ownerName(requested) +
            "; held by " + ownerName(_owner));
        return false;
    }
    if (!_output.begin(_bclk, _lrclk, _dout)) {
        // ESP_I2S may retain a partially initialized channel on failure.
        // Do not let another source retry against uncertain driver state.
        _fault = true;
        Logger::error("AUDIO", "Acquire failed: I2S init; reboot required");
        return false;
    }
    _owner = requested;
    Logger::info("AUDIO", String("Acquired: ") + ownerName(_owner));
    return true;
}

bool AudioOutputManager::release(AudioOutputOwner requested) {
    if (!validOwner(requested) || !isOwnedBy(requested)) {
        Logger::warn("AUDIO", String("Invalid release: ") + ownerName(requested));
        return false;
    }
    if (_attached) {
        Logger::warn("AUDIO", "Release denied: producer still attached");
        return false;
    }
    if (!_output.end()) {
        // Keep ownership on teardown failure; another source must not start.
        _fault = true;
        Logger::error("AUDIO", "Release failed: I2S teardown; lease retained");
        return false;
    }
    _owner = AudioOutputOwner::None;
    _fault = false;
    Logger::info("AUDIO", String("Released: ") + ownerName(requested));
    return true;
}

Print* AudioOutputManager::attach(AudioOutputOwner requested) {
    if (!isOwnedBy(requested) || _attached || _fault || !_output.ready()) {
        Logger::warn("AUDIO", "Attach rejected: invalid or busy lease");
        return nullptr;
    }
    _gain.attach(_output.stream());
    _attached = true;
    return &_gain;
}

bool AudioOutputManager::detach(AudioOutputOwner requested) {
    if (!isOwnedBy(requested) || !_attached) {
        Logger::warn("AUDIO", "Detach rejected: invalid lease");
        return false;
    }
    _gain.detach();
    _attached = false;
    return true;
}

bool AudioOutputManager::configureStereo16(AudioOutputOwner requested, uint32_t sampleRate) {
    if (!isOwnedBy(requested) || !_attached || _fault || !_output.ready() ||
        sampleRate < 8000 || sampleRate > 48000) return false;
    if (!_output.stream().configureTX(sampleRate, I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        _fault = true;
        Logger::error("AUDIO", "PCM configuration failed; output blocked");
        return false;
    }
    _output.stream().setTimeout(100);
    return true;
}
