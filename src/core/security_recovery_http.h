#ifndef VOXONE_SECURITY_RECOVERY_HTTP_H
#define VOXONE_SECURITY_RECOVERY_HTTP_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace security_recovery {

constexpr size_t kRecoveryHttpMaxHeaderBytes = 768U;
constexpr size_t kRecoveryHttpMaxBodyBytes = 512U;
constexpr size_t kRecoveryHttpMaxRequestBytes =
    kRecoveryHttpMaxHeaderBytes + kRecoveryHttpMaxBodyBytes;
constexpr uint32_t kRecoveryHttpClientTimeoutMs = 2000U;
constexpr uint16_t kRecoveryHttpMaxRequests = 64U;

enum class RecoveryHttpFraming : uint8_t {
  Incomplete,
  Complete,
  Invalid,
  TooLarge,
};

enum class RecoveryHttpDecision : uint8_t {
  Root,
  Status,
  Prepare,
  BadRequest,
  HostRejected,
  OriginRejected,
  Unauthorized,
  UnsupportedMediaType,
  PayloadTooLarge,
  MethodNotAllowed,
  NotFound,
  RateLimited,
};

struct RecoveryHttpRequest {
  RecoveryHttpDecision decision;
  size_t bodyOffset;
  size_t bodyLength;
};

RecoveryHttpFraming inspectRecoveryHttpFraming(
    const char* request, size_t length, size_t& expectedLength);

RecoveryHttpDecision evaluateRecoveryHttpRequest(
    const char* request, size_t length, const char* expectedHost,
    bool requestAllowed);
RecoveryHttpDecision evaluateRecoveryHttpRequest(
    const char* request, size_t length, const char* expectedHost,
    const char* expectedSessionToken, bool requestAllowed,
    RecoveryHttpRequest& parsed);
const char* recoveryHttpDecisionName(RecoveryHttpDecision decision);

}  // namespace security_recovery
}  // namespace voxone

#endif
