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

inline const char* displayRadioFormatLabel(BitrateFormat format) {
  switch (format) {
    case BF_MP3: return "MP3";
    case BF_AAC: return "AAC";
    case BF_FLAC: return "FLAC";
    case BF_OGG: return "OGG";
    case BF_WAV: return "WAV";
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
  } else if (source.kind == DisplaySourceKind::Radio) {
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
  if (source.kind != DisplaySourceKind::Radio) {
    output[0] = '\0';
    return;
  }
  if (radioBitrate == 0) {
    output[0] = '\0';
    return;
  }
  const char* codec = displayRadioFormatLabel(radioFormat);
  if (codec[0])
    snprintf(output, capacity, "%u %s", radioBitrate, codec);
  else
    snprintf(output, capacity, "%u", radioBitrate);
}

#endif
