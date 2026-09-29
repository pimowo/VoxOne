#include "../src/core/station_directory_format.h"
#include "../src/core/station_directory_job.h"
#include "../src/core/station_directory_routes.h"
#include "../src/core/station_format.h"
#include <cassert>
#include <string>
#include <vector>

struct StationRow {
  uint64_t id;
};

int main() {
  using namespace stationDirectory;
  assert(resolveReadRoute("/api/stations") == ReadRoute::STATIONS_SNAPSHOT);
  assert(resolveReadRoute("/api/directory/search") == ReadRoute::DIRECTORY_SEARCH);
  // Reproduces the old prefix collision and guards the new disjoint endpoint.
  assert(resolveReadRoute("/api/stations/search") == ReadRoute::STATIONS_SNAPSHOT);
  assert(classifySearchRequest(false, true) == SearchRequest::START);
  assert(classifySearchRequest(true, false) == SearchRequest::POLL);
  assert(classifySearchRequest(true, true) == SearchRequest::POLL);
  assert(classifySearchRequest(false, false) == SearchRequest::MISSING_QUERY);
  JobState jobs;
  const uint32_t job = jobs.start();
  assert(job != 0);
  assert(jobs.workerActive());
  assert(jobs.start() == 0);  // A second start cannot create a task while this job runs.
  for (int i = 0; i < 100; ++i) {
    int status = 0;
    const char* body = nullptr;
    bool ready = true;
    assert(jobs.poll(job, status, body, ready));
    assert(!ready && status == 202 && std::string(body) == R"({"status":"pending"})");
  }
  std::string results = R"({"results":[{"directoryId":"browser-a"}]})";
  jobs.finish(job, 200, results);
  assert(!jobs.workerActive());
  for (int i = 0; i < 100; ++i) {
    int status = 0;
    const char* body = nullptr;
    bool ready = false;
    assert(jobs.poll(job, status, body, ready));
    assert(ready && status == 200 &&
           std::string(body) == R"({"results":[{"directoryId":"browser-a"}]})");
  }
  const uint32_t failedJob = jobs.start();
  assert(failedJob == job + 1);  // Polling did not create or advance a job.
  assert(jobs.workerActive());
  std::string error = R"({"error":"upstream_timeout"})";
  jobs.finish(failedJob, 504, error);
  assert(!jobs.workerActive());
  int errorStatus = 0;
  const char* errorBody = nullptr;
  bool errorReady = false;
  assert(jobs.poll(failedJob, errorStatus, errorBody, errorReady));
  assert(errorReady && errorStatus == 504 &&
         std::string(errorBody) == R"({"error":"upstream_timeout"})");
  assert(jobs.start() == failedJob + 1);
  assert(validQuery("Radio 357"));
  assert(!validQuery(""));
  assert(!validQuery("   "));
  assert(!validQuery(std::string(81, 'x')));
  assert(!validQuery("radio\n"));
  assert(validCountry("PL") && validCountry("ALL"));
  assert(!validCountry("DE") && !validCountry("PL/../"));
  assert(validLimit(1) && validLimit(20));
  assert(!validLimit(0) && !validLimit(21));
  assert(urlEncode("Radio 357 / Ś") == "Radio%20357%20%2F%20%C5%9A");
  assert(searchPath("Radio 357", "PL", 20) ==
         "/json/stations/search?name=Radio%20357&countrycode=PL&limit=20&hidebroken=true");
  assert(searchPath("Jazz", "ALL", 3) ==
         "/json/stations/search?name=Jazz&limit=3&hidebroken=true");

  std::string output;
  const char* response = R"([
    {"stationuuid":"browser-a","name":"Radio 357","url":"http://old.example/playlist.m3u",
     "url_resolved":"https://stream.example/live","country":"Poland","countrycode":"PL",
     "codec":"MP3","bitrate":192,"hls":0,"favicon":"https://example/icon.png",
     "lastcheckok":1,"tags":["rock","pop"]},
    {"stationuuid":"browser-b","name":"\u015aląskie Radio","url":"http://fallback.example/live",
     "url_resolved":"ftp://invalid","countrycode":"PL","bitrate":0,"hls":false,"lastcheckok":true},
    {"stationuuid":"browser-c","name":"Broken","url":"http://broken.example/live","lastcheckok":0}
  ])";
  assert(parseResponse(response, std::char_traits<char>::length(response), 20, output));
  assert(isSearchResultEnvelope(output.data(), output.size()));
  assert(output.find("\"directoryId\":\"browser-a\"") != std::string::npos);
  assert(output.find("\"url\":\"https://stream.example/live\"") != std::string::npos);
  assert(output.find("\"codec\":\"MP3\",\"bitrate\":192") != std::string::npos);
  assert(output.find("\"name\":\"Śląskie Radio\"") != std::string::npos);
  assert(output.find("\"url\":\"http://fallback.example/live\"") != std::string::npos);
  assert(output.find("\"codec\":\"\",\"bitrate\":0") != std::string::npos);
  assert(output.find("browser-c") == std::string::npos);

  const std::string expected = output;
  ResponseStream streamed(20);
  const size_t responseLength = std::char_traits<char>::length(response);
  for (size_t pos = 0; pos < responseLength; pos += 7) {
    const size_t chunk = responseLength - pos < 7 ? responseLength - pos : 7;
    assert(streamed.write(response + pos, chunk));
  }
  std::string streamedOutput;
  assert(streamed.finish(streamedOutput) && streamedOutput == expected);
  assert(streamed.resultCount() == 2);
  ResponseStream emptyStream(20);
  assert(emptyStream.write("[", 1) && emptyStream.write("]", 1));
  assert(emptyStream.finish(streamedOutput) && streamedOutput == "{\"results\":[]}");
  assert(emptyStream.resultCount() == 0);
  const std::string localSnapshot =
      R"({"revision":"474A357C","current":3,"count":13,"stations":[]})";
  assert(!isSearchResultEnvelope(localSnapshot.data(), localSnapshot.size()));
  const std::string mixedEnvelope = R"({"results":[],"revision":"x"})";
  assert(!isSearchResultEnvelope(mixedEnvelope.data(), mixedEnvelope.size()));
  ResponseStream incomplete(20);
  assert(incomplete.write("[{}", 3));
  assert(!incomplete.finish(streamedOutput));
  ResponseStream oversized(20);
  const std::string tooLarge = "[{\"name\":\"" + std::string(5000, 'x') + "\"}]";
  assert(!oversized.write(tooLarge.data(), tooLarge.size()));
  std::string limited;
  assert(parseResponse(response, std::char_traits<char>::length(response), 1, limited));
  assert(limited.find("browser-a") != std::string::npos);
  assert(limited.find("browser-b") == std::string::npos);
  assert(parseResponse("[]", 2, 20, output) && output == "{\"results\":[]}");
  assert(!parseResponse("[{\"name\":\"bad\"}", 15, 20, output));
  assert(!parseResponse("[{\"name\":", 10, 20, output));
  assert(!parseResponse("not json", 8, 20, output));
  assert(!parseResponse("[{},]", 5, 20, output));
  assert(!parseResponse(response, std::char_traits<char>::length(response), 0, output));
  const std::string invalidName =
      std::string("[{\"stationuuid\":\"bad\",\"name\":\"") + char(0xC3) +
      "\",\"url\":\"http://example/live\"}]";
  assert(parseResponse(invalidName.data(), invalidName.size(), 20, output));
  assert(output == "{\"results\":[]}");

  // The directory UUID is display-only; ADD uses VoxOne's own stable ID.
  std::vector<StationRow> rows;
  const uint64_t id = stationGenerateId(rows, []() -> uint32_t { return 0x12345678; });
  assert(id != 0);
  assert(playlistValidField("Radio 357", 9, true));
  assert(playlistValidField("https://stream.example/live", 27, false));
  assert(playlistValidOvol(0));
  assert(STATION_META_NORMAL == 0);
}
