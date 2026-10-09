#include "dsp_transport_runtime.h"

#if defined(VOXONE_PROFILE_A0)
#include "dsp_runtime.h"
#include "dsp_state_json.h"
#include "dsp_transport_protocol.h"
#include "options.h"
#include "config.h"
#include "netserver.h"
#include "network.h"

#include <Arduino.h>
#include <cstdlib>
#include <cstring>

namespace voxone {
namespace dsp {
namespace {

constexpr uint8_t kQueueLength = 12;
constexpr size_t kStateJsonCapacity = 4096;
constexpr uint16_t kMalformed = 100;
constexpr uint16_t kTooLarge = 101;
constexpr uint16_t kInvalidParameter = 102;
constexpr uint16_t kInvalidName = 103;
constexpr uint16_t kBusy = 104;
constexpr uint16_t kTonePersistFailed = 105;
constexpr uint16_t kUnavailable = 106;

QueueHandle_t commandQueue = nullptr;
SemaphoreHandle_t serviceMutex = nullptr;
SemaphoreHandle_t toneWriteMutex = nullptr;
portMUX_TYPE toneMux = portMUX_INITIALIZER_UNLOCKED;
ToneSnapshot pendingTone{};
bool tonePending = false;
portMUX_TYPE revisionMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t publishedRevision = 0;

void publishRevision(uint32_t revision) {
  portENTER_CRITICAL(&revisionMux);
  publishedRevision = revision;
  portEXIT_CRITICAL(&revisionMux);
}

uint32_t lastRevision() {
  portENTER_CRITICAL(&revisionMux);
  const uint32_t revision = publishedRevision;
  portEXIT_CRITICAL(&revisionMux);
  return revision;
}

ToneSnapshot currentTone() {
  return {config.store.bass, config.store.middle, config.store.trebble};
}

void sendError(uint32_t clientId, uint32_t requestId, uint16_t code) {
  char reply[128];
  const unsigned long revision = static_cast<unsigned long>(lastRevision());
  snprintf(reply, sizeof(reply),
           "{\"event\":\"dsp.error\",\"requestId\":%lu,\"code\":%u,"
           "\"revision\":%lu}",
           static_cast<unsigned long>(requestId), static_cast<unsigned>(code), revision);
  websocket.text(clientId, reply);
}

void sendChanged(const DspCommand& command, uint32_t revision,
                 uint32_t appliedRevision, PresetId activePreset, bool dirty) {
  char escapedName[kMaxDspPresetNameBytes * 2 + 1]{};
  size_t escapedLength = 0;
  if (command.name[0]) {
    for (const char* p = command.name; *p; ++p) {
      if (*p == '"' || *p == '\\') escapedName[escapedLength++] = '\\';
      escapedName[escapedLength++] = *p;
    }
  }
  char nameField[kMaxDspPresetNameBytes * 2 + 12]{};
  if (command.name[0])
    snprintf(nameField, sizeof(nameField), ",\"name\":\"%s\"", escapedName);
  char reply[256];
  snprintf(reply, sizeof(reply),
           "{\"event\":\"dsp.changed\",\"requestId\":%lu,"
           "\"operation\":%u,\"parameterId\":%u,\"index\":%u,"
           "\"value\":%.3f%s,\"activePresetId\":%u,\"revision\":%lu,"
           "\"appliedRevision\":%lu,\"dirty\":%s}",
           static_cast<unsigned long>(command.requestId),
           static_cast<unsigned>(command.operation),
           command.operation == DspOperation::Set
               ? static_cast<unsigned>(command.parameter) : 0U,
           static_cast<unsigned>(command.index),
           static_cast<double>(command.value), nameField,
           static_cast<unsigned>(activePreset),
           static_cast<unsigned long>(revision),
           static_cast<unsigned long>(appliedRevision), dirty ? "true" : "false");
  websocket.textAll(reply);
}

uint16_t parseCode(DspParseError error) {
  switch (error) {
    case DspParseError::TooLarge: return kTooLarge;
    case DspParseError::InvalidParameter: return kInvalidParameter;
    case DspParseError::InvalidName: return kInvalidName;
    default: return kMalformed;
  }
}

void jsonFailure(AsyncWebServerRequest* request, int code) {
  AsyncWebServerResponse* response = request->beginResponse(
      code, "application/json; charset=utf-8", "{\"error\":\"dsp_state_unavailable\"}");
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

DspServiceError executePreset(DspService& service, const DspCommand& command,
                              bool& tonePersistenceFailed) {
  switch (command.operation) {
    case DspOperation::Activate:
    case DspOperation::Restore: {
      if (!lockDspToneMutation()) return DspServiceError::NotInitialized;
      PresetActivation plan{};
      DspServiceError result = command.operation == DspOperation::Activate
          ? service.prepareActivation(command.presetId, plan)
          : service.prepareRestore(plan);
      if (result != DspServiceError::Ok) {
        unlockDspToneMutation();
        return result;
      }
      const ToneSnapshot previous = currentTone();
      if (!config.setTone(plan.toneToApply.bass, plan.toneToApply.middle,
                          plan.toneToApply.treble)) {
        // Best effort RAM/audio rollback; EEPROM cannot provide atomic recovery yet.
        config.setTone(previous.bass, previous.middle, previous.treble);
        tonePersistenceFailed = true;
        unlockDspToneMutation();
        return DspServiceError::InvalidValue;
      }
      result = service.commitActivation(plan, currentTone());
      unlockDspToneMutation();
      return result;
    }
    case DspOperation::Save: {
      if (!lockDspToneMutation()) return DspServiceError::NotInitialized;
      const DspServiceError result = service.saveUserPreset(
          command.presetId, currentTone(),
          command.name[0] ? command.name : nullptr);
      unlockDspToneMutation();
      return result;
    }
    case DspOperation::Rename:
      return service.renameUserPreset(command.presetId, command.name);
    default: return DspServiceError::InvalidValue;
  }
}

}  // namespace

bool beginDspTransport() {
  if (commandQueue && serviceMutex && toneWriteMutex) return true;
  if (!serviceMutex) serviceMutex = xSemaphoreCreateMutex();
  if (!toneWriteMutex) toneWriteMutex = xSemaphoreCreateRecursiveMutex();
  if (!commandQueue) commandQueue = xQueueCreate(kQueueLength, sizeof(DspCommand));
  if (!serviceMutex || !toneWriteMutex || !commandQueue) return false;
  publishRevision(activeDspService().runtime().revision);
  dspTransportToneChanged(currentTone());
  return true;
}

void dspTransportToneChanged(const ToneSnapshot& tone) {
  portENTER_CRITICAL(&toneMux);
  pendingTone = tone;
  tonePending = true;
  portEXIT_CRITICAL(&toneMux);
}

bool lockDspToneMutation() {
  return toneWriteMutex &&
         xSemaphoreTakeRecursive(toneWriteMutex, portMAX_DELAY) == pdTRUE;
}

void unlockDspToneMutation() {
  xSemaphoreGiveRecursive(toneWriteMutex);
}

bool handleDspWsFrame(const uint8_t* data, size_t length,
                      uint32_t clientId, bool complete) {
  if (!data || length < 4 || std::memcmp(data, "dsp.", 4) != 0) return false;
  if (!complete) { sendError(clientId, 0, kMalformed); return true; }
  DspCommand command{};
  const DspParseError parsed = parseDspWsFrame(
      reinterpret_cast<const char*>(data), length, command);
  if (parsed != DspParseError::Ok) {
    sendError(clientId, command.requestId, parseCode(parsed));
    return true;
  }
  command.clientId = clientId;
  if (!serviceMutex || !toneWriteMutex || !commandQueue ||
      xQueueSend(commandQueue, &command, 0) != pdTRUE)
    sendError(clientId, command.requestId, kBusy);
  return true;
}

void processDspTransportQueue() {
  if (!serviceMutex || !toneWriteMutex || !commandQueue ||
      xSemaphoreTake(serviceMutex, 0) != pdTRUE)
    return;
  if (!lockDspToneMutation()) {
    xSemaphoreGive(serviceMutex);
    return;
  }
  DspService& service = activeDspService();
  ToneSnapshot tone{};
  bool notifyTone = false;
  portENTER_CRITICAL(&toneMux);
  if (tonePending) {
    tone = pendingTone;
    tonePending = false;
    notifyTone = true;
  }
  portEXIT_CRITICAL(&toneMux);
  if (notifyTone) {
    const uint32_t previousRevision = service.runtime().revision;
    service.notifySharedToneChanged(tone);
    if (service.runtime().revision != previousRevision) {
      const DspRuntimeState& state = service.runtime();
      const uint32_t revision = state.revision;
      const uint32_t applied = state.appliedRevision;
      const bool dirty = service.dirty(tone);
      publishRevision(revision);
      unlockDspToneMutation();
      xSemaphoreGive(serviceMutex);
      char reply[128];
      snprintf(reply, sizeof(reply),
               "{\"event\":\"dsp.changed\",\"requestId\":0,"
               "\"parameterId\":0,\"revision\":%lu,\"appliedRevision\":%lu,"
               "\"dirty\":%s}",
               static_cast<unsigned long>(revision), static_cast<unsigned long>(applied),
               dirty ? "true" : "false");
      websocket.textAll(reply);
      return;
    }
  }
  DspCommand command{};
  if (xQueueReceive(commandQueue, &command, 0) != pdTRUE) {
    unlockDspToneMutation();
    xSemaphoreGive(serviceMutex);
    return;
  }
  bool tonePersistenceFailed = false;
  const uint32_t previousRevision = service.runtime().revision;
  const DspServiceError result = command.operation == DspOperation::Set
      ? applyDspSet(service, command)
      : executePreset(service, command, tonePersistenceFailed);
  const DspRuntimeState& state = service.runtime();
  const uint32_t revision = state.revision;
  const uint32_t applied = state.appliedRevision;
  const PresetId activePreset = service.global().activePresetId;
  const bool dirty = service.dirty(currentTone());
  publishRevision(revision);
  unlockDspToneMutation();
  xSemaphoreGive(serviceMutex);
  if (tonePersistenceFailed)
    sendError(command.clientId, command.requestId, kTonePersistFailed);
  else if (result == DspServiceError::BackendError && revision != previousRevision) {
    sendChanged(command, revision, applied, activePreset, dirty);
    sendError(command.clientId, command.requestId, static_cast<uint16_t>(result));
  } else if (result != DspServiceError::Ok)
    sendError(command.clientId, command.requestId, static_cast<uint16_t>(result));
  else sendChanged(command, revision, applied, activePreset, dirty);
}

void handleDspState(AsyncWebServerRequest* request) {
  if (!request) return;
#if defined(HTTP_USER) && defined(HTTP_PASS)
  if (network.status == CONNECTED &&
      !request->authenticate(HTTP_USER, HTTP_PASS)) {
    request->requestAuthentication();
    return;
  }
#endif
  if (!serviceMutex || !toneWriteMutex || !commandQueue) {
    jsonFailure(request, 503); return;
  }
  char* json = static_cast<char*>(std::malloc(kStateJsonCapacity));
  if (!json) { jsonFailure(request, 503); return; }
  if (xSemaphoreTake(serviceMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    std::free(json);
    jsonFailure(request, 503);
    return;
  }
  DspSharedAudioView shared{};
  shared.volume = config.userVolume;
  shared.maximumVolume = config.store.maximumVolume;
  shared.startupMode = config.store.startupMode;
  shared.startupFixedVolume = config.store.startupFixedVolume;
  if (!lockDspToneMutation()) {
    xSemaphoreGive(serviceMutex);
    std::free(json);
    jsonFailure(request, 503);
    return;
  }
  shared.tone = currentTone();
  unlockDspToneMutation();
  shared.balance = config.store.balance;
  const size_t length = formatDspStateJson(
      json, kStateJsonCapacity, activeDspService(), shared);
  xSemaphoreGive(serviceMutex);
  if (!length) {
    std::free(json);
    jsonFailure(request, 503);
    return;
  }
  String body;
  const bool reserved = body.reserve(length);
  if (reserved) body.concat(json, length);
  std::free(json);
  if (!reserved || body.length() != length) {
    jsonFailure(request, 503);
    return;
  }
  AsyncWebServerResponse* response = request->beginResponse(
      200, "application/json; charset=utf-8", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

}  // namespace dsp
}  // namespace voxone
#endif
