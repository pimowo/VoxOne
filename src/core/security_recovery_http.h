#ifndef VOXONE_SECURITY_RECOVERY_HTTP_H
#define VOXONE_SECURITY_RECOVERY_HTTP_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace security_recovery {

constexpr size_t kRecoveryHttpMaxRequestBytes = 512;
constexpr uint32_t kRecoveryHttpClientTimeoutMs = 2000U;
constexpr uint16_t kRecoveryHttpMaxRequests = 64U;

enum class RecoveryHttpDecision : uint8_t {
  Root,
  Status,
  BadRequest,
  HostRejected,
  MethodNotAllowed,
  NotFound,
  RateLimited,
};

RecoveryHttpDecision evaluateRecoveryHttpRequest(
    const char* request, size_t length, const char* expectedHost,
    bool requestAllowed);
const char* recoveryHttpDecisionName(RecoveryHttpDecision decision);

}  // namespace security_recovery
}  // namespace voxone

#endif
