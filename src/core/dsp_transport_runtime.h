#ifndef VOXONE_DSP_TRANSPORT_RUNTIME_H
#define VOXONE_DSP_TRANSPORT_RUNTIME_H

#if defined(VOXONE_PROFILE_SALON)
#include "dsp_model.h"

#include <cstddef>
#include <cstdint>

class AsyncWebServerRequest;

namespace voxone {
namespace dsp {

bool beginDspTransport();
void handleDspState(AsyncWebServerRequest* request);
bool handleDspWsFrame(const uint8_t* data, size_t length,
                      uint32_t clientId, bool complete);
void processDspTransportQueue();
void dspTransportToneChanged(const ToneSnapshot& tone);
bool lockDspToneMutation();
void unlockDspToneMutation();

}  // namespace dsp
}  // namespace voxone
#endif

#endif
