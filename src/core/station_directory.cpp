#include "station_directory.h"
#include "station_directory_format.h"
#include "station_directory_job.h"
#include "version.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/semphr.h>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {
constexpr char kMirrors[][32] = {
    "de1.api.radio-browser.info",
    "nl1.api.radio-browser.info"
};
constexpr uint32_t kConnectTimeoutMs = 2500;
constexpr uint16_t kReadTimeoutMs = 3500;
constexpr uint32_t kWorkerStackBytes = 8192;

SemaphoreHandle_t stateMutex = nullptr;
stationDirectory::JobState jobState;

struct SearchParams {
  std::string query;
  std::string country;
  unsigned limit;
  uint32_t job;
};

class DirectoryResponseStream : public Stream {
 public:
  explicit DirectoryResponseStream(unsigned limit) : parser_(limit) {}
  size_t write(uint8_t ch) override { return write(&ch, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (invalid_ || !parser_.write(reinterpret_cast<const char*>(data), length)) {
      invalid_ = true;
      return 0;
    }
    return length;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  bool invalid() const { return invalid_; }
  bool finish(std::string& output) { return !invalid_ && parser_.finish(output); }
  unsigned resultCount() const { return parser_.resultCount(); }
 private:
  stationDirectory::ResponseStream parser_;
  bool invalid_ = false;
};
struct SearchOutcome {
  int status = 502;
  std::string body = "{\"error\":\"upstream_unavailable\"}";
  unsigned results = 0;
};

SearchOutcome searchMirror(const char* host, const SearchParams& params) {
  SearchOutcome outcome;
  if (ESP.getFreeHeap() < 65536) {
    outcome.status = 503;
    outcome.body = "{\"error\":\"insufficient_memory\"}";
    return outcome;
  }
  WiFiClientSecure client;
  // Read-only public directory; embedded CA storage and clock validation are not configured.
  client.setInsecure();
  client.setTimeout(kReadTimeoutMs / 1000);
  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kReadTimeoutMs);
  http.setReuse(false);
  const std::string path =
      stationDirectory::searchPath(params.query, params.country, params.limit);
  const String url = String("https://") + host + path.c_str();
  if (!http.begin(client, url)) {
    outcome.body = "{\"error\":\"upstream_connection_failed\"}";
    Serial.printf("##[DIRECTORY]# mirror=%s status=begin_failed\n", host);
    http.end();
    client.stop();
    return outcome;
  }
  http.setUserAgent(String("VoxOne/") + VOXONE_VERSION);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  const int code = http.GET();
  Serial.printf("##[DIRECTORY]# mirror=%s status=%d\n", host, code);
  if (code == 200) {
    if (http.getSize() > static_cast<int>(stationDirectory::kMaxResponseBytes)) {
      outcome.body = "{\"error\":\"upstream_response_too_large\"}";
    } else {
      DirectoryResponseStream buffer(params.limit);
      const int received = http.writeToStream(&buffer);
      std::string mapped;
      if (buffer.invalid()) {
        outcome.body = "{\"error\":\"upstream_invalid_json\"}";
      } else if (received == HTTPC_ERROR_READ_TIMEOUT ||
                 received == HTTPC_ERROR_CONNECTION_LOST) {
        outcome.status = 504;
        outcome.body = "{\"error\":\"upstream_timeout\"}";
      } else if (received < 0 || !buffer.finish(mapped)) {
        outcome.body = "{\"error\":\"upstream_invalid_json\"}";
      } else {
        outcome.status = 200;
        outcome.results = buffer.resultCount();
        outcome.body.swap(mapped);
      }
    }
  } else if (code == HTTPC_ERROR_READ_TIMEOUT ||
             code == HTTPC_ERROR_CONNECTION_LOST) {
    outcome.status = 504;
    outcome.body = "{\"error\":\"upstream_timeout\"}";
  } else if (code < 0) {
    outcome.body = "{\"error\":\"upstream_connection_failed\"}";
  } else {
    outcome.body = "{\"error\":\"upstream_http_error\"}";
  }
  http.end();
  client.stop();
  return outcome;
}
void searchWorker(void* context) {
  {
    std::unique_ptr<SearchParams> params(static_cast<SearchParams*>(context));
    SearchOutcome outcome;
    if (WiFi.status() != WL_CONNECTED) {
      outcome.status = 503;
      outcome.body = "{\"error\":\"offline\"}";
    } else {
      for (const auto& host : kMirrors) {
        outcome = searchMirror(host, *params);
        if (outcome.status == 200 || outcome.status == 503 ||
            outcome.body == "{\"error\":\"upstream_response_too_large\"}") break;
      }
    }
    if (outcome.status == 200) {
      if (!stationDirectory::isSearchResultEnvelope(outcome.body.data(), outcome.body.size())) {
        outcome.status = 502;
        outcome.body = "{\"error\":\"invalid_directory_result\"}";
        outcome.results = 0;
      } else {
        Serial.printf("##[DIRECTORY]# results=%u\n", outcome.results);
      }
    }
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    jobState.finish(params->job, outcome.status, outcome.body);
    xSemaphoreGive(stateMutex);
  }  // Release response and request parameters before deleting the task.
  Serial.println("##[DIRECTORY]# task done");
  vTaskDelete(nullptr);
}
}  // namespace

bool stationDirectoryInit() {
  if (!stateMutex) stateMutex = xSemaphoreCreateMutex();
  return stateMutex != nullptr;
}

DirectoryStartResult stationDirectoryStart(const String& query, const String& country,
                                           unsigned limit, uint32_t& job) {
  if (!stationDirectoryInit()) return DirectoryStartResult::NO_MEMORY;
  if (WiFi.status() != WL_CONNECTED) return DirectoryStartResult::OFFLINE;
  SearchParams* params = new (std::nothrow) SearchParams{
      std::string(query.c_str()), std::string(country.c_str()), limit, 0};
  if (!params) return DirectoryStartResult::NO_MEMORY;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  const uint32_t startedJob = jobState.start();
  if (!startedJob) {
    xSemaphoreGive(stateMutex);
    delete params;
    return DirectoryStartResult::BUSY;
  }
  params->job = startedJob;
  job = startedJob;
  xSemaphoreGive(stateMutex);
  if (xTaskCreatePinnedToCore(searchWorker, "StationDirectory",
                              kWorkerStackBytes, params, 1, nullptr, 0) != pdPASS) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    jobState.cancel(startedJob);
    xSemaphoreGive(stateMutex);
    delete params;
    return DirectoryStartResult::NO_MEMORY;
  }
  return DirectoryStartResult::STARTED;
}

bool stationDirectoryPoll(uint32_t job, int& status, String& body, bool& ready) {
  if (!stateMutex || !job) return false;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  const char* responseText = nullptr;
  const bool found = jobState.poll(job, status, responseText, ready);
  if (found) {
    body = responseText;
    if (body.length() != strlen(responseText)) {
      status = 503;
      body = "{\"error\":\"insufficient_memory\"}";
    }
  }
  xSemaphoreGive(stateMutex);
  return found;
}
