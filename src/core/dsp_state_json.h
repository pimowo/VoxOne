#ifndef VOXONE_DSP_STATE_JSON_H
#define VOXONE_DSP_STATE_JSON_H

#include "dsp_service.h"

#include <cstddef>
#include <cstdint>

namespace voxone {
namespace dsp {

struct DspSharedAudioView {
  uint8_t volume = 0;
  uint8_t maximumVolume = 0;
  uint8_t startupMode = 0;
  uint8_t startupFixedVolume = 0;
  ToneSnapshot tone{};
  int8_t balance = 0;
};

// Returns bytes written, excluding NUL; 0 means capacity/format failure.
size_t formatDspStateJson(char* buffer, size_t capacity,
                          const DspService& service,
                          const DspSharedAudioView& shared);

}  // namespace dsp
}  // namespace voxone

#endif
