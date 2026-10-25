#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
}  // namespace

int main() {
  const std::string ini = readFile("platformio.ini");
  const std::string header = readFile("src/core/http_heap_trace.h");
  const std::string trace = readFile("src/core/http_heap_trace.cpp");
  const std::string request = readFile("src/AsyncWebServer/WebRequest.cpp");
  const std::string response = readFile("src/AsyncWebServer/WebResponses.cpp");
  const std::string tcp = readFile("src/AsyncWebServer/AsyncTCP.cpp");
  const std::string websocket =
      readFile("src/AsyncWebServer/AsyncWebSocket.cpp");
  const std::string server = readFile("src/core/netserver.cpp");

  assert(ini.find("[env:m0-heap-trace]") != std::string::npos);
  assert(ini.find("-DVOXONE_HEAP_TRACE=1") != std::string::npos);
  assert(header.find("#if defined(VOXONE_HEAP_TRACE)") != std::string::npos);
  assert(trace.find("Slot slots[kSlotCount]") != std::string::npos);
  assert(trace.find("heap_caps_check_integrity(kHeapCaps, false)") !=
         std::string::npos);
  assert(trace.find("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT") !=
         std::string::npos);
  assert(trace.find("##[HEAP-TRACE-FIRST]#") != std::string::npos);
  assert(trace.find("emitFirstFailure(snapshot);") <
         trace.find("heap_caps_check_integrity(kHeapCaps, true)"));
  assert(trace.find("requestReuseFrom") != std::string::npos);
  assert(trace.find("clientReuseFrom") != std::string::npos);
  assert(trace.find("peer=%u.%u.%u.%u:%u") != std::string::npos);
  assert(trace.find("slot->peerPort = peerPort") != std::string::npos);
  assert(trace.find("slot->peer[0] = peer0") != std::string::npos);
  assert(trace.find("<untracked>") != std::string::npos);
  for (const char* forbidden : {"malloc(", "calloc(", "realloc(",
                                "new ", "String", "Serial."})
    assert(trace.find(forbidden) == std::string::npos);

  for (const char* hook : {"requestCreated", "remoteIP()", "remotePort()",
                           "requestRouteKnown", "connection_disconnect",
                           "handler_begin", "handler_end",
                           "request_disconnect", "response_delete_before",
                           "request_delete_returned"})
    assert(request.find(hook) != std::string::npos);
  for (const char* hook : {"response_fill_before", "response_fill_after",
                           "response_write_after", "response_buffer_freed",
                           "basic_response_begin", "basic_response_end"})
    assert(response.find(hook) != std::string::npos);
  for (const char* hook : {"tcp_fin_async", "client_destructor_begin",
                           "client_destructor_body_end",
                           "tcp_recved_returned"})
    assert(tcp.find(hook) != std::string::npos);
  assert(tcp.find("tcp_recv_async") == std::string::npos);
  assert(tcp.find("_tcp_recved(_pcb, _closed_slot, b->len);") <
         tcp.find("\"tcp_recved_returned\""));
  for (const char* hook : {"websocket_connected", "websocket_disconnect",
                           "clientReleased"})
    assert(websocket.find(hook) != std::string::npos);
  for (const char* hook : {"config_backup_enter", "config_backup_ready",
                           "config_backup_before_send",
                           "config_backup_after_send"})
    assert(server.find(hook) != std::string::npos);

  // Diagnostic output must never include sensitive HTTP material.
  for (const char* forbidden : {"cookie", "Cookie", "token", "password",
                                "Authorization", "payload", "wifi", "mqtt"})
    assert(trace.find(forbidden) == std::string::npos);
  return 0;
}
