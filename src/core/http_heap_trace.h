#ifndef VOXONE_HTTP_HEAP_TRACE_H
#define VOXONE_HTTP_HEAP_TRACE_H

#include <stddef.h>
#include <stdint.h>

#if defined(VOXONE_HEAP_TRACE)

namespace voxone {
namespace http_heap_trace {

// Diagnostic-only HTTP lifecycle tracing.  The implementation uses fixed
// storage, logs no request/response payloads or headers, and checks only the
// internal 8-bit heap that contained the observed corrupt allocation.
void requestCreated(const void* request, const void* client, uint8_t peer0,
                    uint8_t peer1, uint8_t peer2, uint8_t peer3,
                    uint16_t peerPort);
void requestRouteKnown(const void* request, const char* method,
                       const char* route);
void requestPoint(const void* request, const char* stage,
                  bool checkIntegrity = true, size_t value1 = 0,
                  size_t value2 = 0, size_t value3 = 0);
void clientPoint(const void* client, const char* stage,
                 bool checkIntegrity = true, size_t value1 = 0,
                 size_t value2 = 0, size_t value3 = 0);
void requestReleased(const void* request);
void clientReleased(const void* client);

}  // namespace http_heap_trace
}  // namespace voxone

#endif  // VOXONE_HEAP_TRACE

#endif  // VOXONE_HTTP_HEAP_TRACE_H
