#ifndef VOXONE_DISPLAY_AUDIO_INFO_H
#define VOXONE_DISPLAY_AUDIO_INFO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "display.h"
#include "../displays/widgets/widgetsconfig.h"

inline const char* bluetoothSampleRateTop(uint32_t sampleRate) {
  switch (sampleRate) {
    case 44100: return "44.1";
    case 48000: return "48";
    default: return "";
  }
}

struct DisplayAudioInfo {
  bool bluetooth;
  uint16_t radioBitrate;
  BitrateFormat radioFormat;
  const char* top;
  const char* bottom;
};

inline DisplayAudioInfo selectDisplayAudioInfo(const DisplaySourceView& source,
                                               uint16_t radioBitrate,
                                               BitrateFormat radioFormat) {
  DisplayAudioInfo info{};
  info.bluetooth = source.kind == DisplaySourceKind::Bluetooth;
  if (info.bluetooth) {
    info.top = bluetoothSampleRateTop(source.sampleRate);
    info.bottom = info.top[0] ? "kHz" : "";
  } else {
    info.radioBitrate = radioBitrate;
    info.radioFormat = radioFormat;
    info.top = "";
    info.bottom = "";
  }
  return info;
}

inline const char* bluetoothSampleRateLabel(uint32_t sampleRate) {
  switch (sampleRate) {
    case 44100: return "44.1 kHz";
    case 48000: return "48 kHz";
    default: return "";
  }
}

inline void formatDisplayAudioInfo(char* output, size_t capacity,
                                   const DisplaySourceView& source,
                                   uint16_t radioBitrate, BitrateFormat radioFormat) {
  if (!output || capacity == 0) return;
  if (source.kind == DisplaySourceKind::Bluetooth) {
    snprintf(output, capacity, "%s", bluetoothSampleRateLabel(source.sampleRate));
    return;
  }
  if (radioBitrate == 0) {
    output[0] = '\0';
    return;
  }
  const char* codec = "";
  switch (radioFormat) {
    case BF_MP3: codec = "MP3"; break;
    case BF_AAC: codec = "AAC"; break;
    case BF_FLAC: codec = "FLAC"; break;
    case BF_OGG: codec = "OGG"; break;
    case BF_WAV: codec = "WAV"; break;
    default: break;
  }
  if (codec[0])
    snprintf(output, capacity, "%u %s", radioBitrate, codec);
  else
    snprintf(output, capacity, "%u", radioBitrate);
}

#endif