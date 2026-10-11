#include "../src/core/radio_url_resolution.h"

#include <cassert>
#include <cstring>

using voxone::radio::UrlHopBudget;
using voxone::radio::UrlHopResult;
using voxone::radio::UrlResolutionResult;
using voxone::radio::HttpResponseAction;
using voxone::radio::httpResponseAction;
using voxone::radio::isRedirectStatus;
using voxone::radio::parseM3uEntry;
using voxone::radio::parsePlsEntry;
using voxone::radio::resolveRelativeUrl;

namespace {

constexpr size_t kUrlSize = voxone::radio::kMaxResolvedUrlLength;

void expectResolved(const char* base, const char* reference,
                    const char* expected) {
  char output[kUrlSize];
  assert(resolveRelativeUrl(base, reference, output, sizeof(output)) ==
         UrlResolutionResult::Ok);
  assert(std::strcmp(output, expected) == 0);
}

void testM3u() {
  char output[kUrlSize];
  const char* base = "https://radio.example/lists/main.m3u";
  assert(parseM3uEntry("https://stream.example/live.mp3", base, output,
                       sizeof(output)) == UrlResolutionResult::Ok);
  assert(std::strcmp(output, "https://stream.example/live.mp3") == 0);
  assert(parseM3uEntry("#EXTM3U", base, output, sizeof(output)) ==
         UrlResolutionResult::Empty);
  assert(parseM3uEntry("#EXTINF:-1,Station", base, output, sizeof(output)) ==
         UrlResolutionResult::Empty);
  assert(parseM3uEntry("   # comment", base, output, sizeof(output)) ==
         UrlResolutionResult::Empty);
  assert(parseM3uEntry("   ", base, output, sizeof(output)) ==
         UrlResolutionResult::Empty);
  assert(parseM3uEntry("../streams/live.aac", base, output, sizeof(output)) ==
         UrlResolutionResult::Ok);
  assert(std::strcmp(output, "https://radio.example/streams/live.aac") == 0);
  assert(parseM3uEntry(nullptr, base, output, sizeof(output)) ==
         UrlResolutionResult::Empty);
}

void testPls() {
  char output[kUrlSize];
  uint16_t number = 0;
  const char* base = "http://radio.example/playlists/list.pls";
  assert(parsePlsEntry("File1=http://one.example/live", base, &number,
                       output, sizeof(output)) == UrlResolutionResult::Ok);
  assert(number == 1);
  assert(std::strcmp(output, "http://one.example/live") == 0);
  assert(parsePlsEntry("File2=../two.aac", base, &number, output,
                       sizeof(output)) == UrlResolutionResult::Ok);
  assert(number == 2);
  assert(std::strcmp(output, "http://radio.example/two.aac") == 0);

  // Selection is by the lowest FileN, independent of source line order.
  const char* unordered[] = {"Version=2", "File2=http://two.example/live",
                             "NumberOfEntries=2",
                             "File1=http://one.example/live", "[playlist]"};
  uint16_t selectedNumber = UINT16_MAX;
  char selected[kUrlSize] = {};
  for (const char* line : unordered) {
    char candidate[kUrlSize];
    uint16_t candidateNumber = 0;
    if (parsePlsEntry(line, base, &candidateNumber, candidate,
                      sizeof(candidate)) == UrlResolutionResult::Ok &&
        candidateNumber < selectedNumber) {
      selectedNumber = candidateNumber;
      std::strcpy(selected, candidate);
    }
  }
  assert(selectedNumber == 1);
  assert(std::strcmp(selected, "http://one.example/live") == 0);
  assert(parsePlsEntry("File1=", base, &number, output, sizeof(output)) ==
         UrlResolutionResult::Empty);
  assert(parsePlsEntry("FileX=http://bad.example", base, &number, output,
                       sizeof(output)) == UrlResolutionResult::Malformed);
  assert(parsePlsEntry("Title1=Station", base, &number, output,
                       sizeof(output)) == UrlResolutionResult::Empty);
  assert(parsePlsEntry("", base, &number, output, sizeof(output)) ==
         UrlResolutionResult::Empty);
}

void testRedirectsAndUrls() {
  assert(isRedirectStatus(301));
  assert(isRedirectStatus(302));
  assert(isRedirectStatus(303));
  assert(isRedirectStatus(307));
  assert(isRedirectStatus(308));
  assert(!isRedirectStatus(300));
  assert(!isRedirectStatus(304));
  assert(!isRedirectStatus(404));
  assert(httpResponseAction(200) == HttpResponseAction::Continue);
  assert(httpResponseAction(301) == HttpResponseAction::Redirect);
  assert(httpResponseAction(308) == HttpResponseAction::Redirect);
  assert(httpResponseAction(304) == HttpResponseAction::Error);
  assert(httpResponseAction(404) == HttpResponseAction::Error);

  expectResolved("http://a.example/path/start", "https://b.example/live",
                 "https://b.example/live");
  expectResolved("https://a.example/path/start", "../live.mp3",
                 "https://a.example/live.mp3");
  expectResolved("https://a.example/path/start", "/radio/live.mp3",
                 "https://a.example/radio/live.mp3");
  expectResolved("https://a.example/path/start", "//cdn.example/live.aac",
                 "https://cdn.example/live.aac");
  expectResolved("https://a.example/path/start?old=1", "?new=2",
                 "https://a.example/path/start?new=2");

  char output[kUrlSize];
  assert(resolveRelativeUrl("https://a.example/start", "ftp://b.example/x",
                            output, sizeof(output)) ==
         UrlResolutionResult::UnsupportedScheme);
  assert(resolveRelativeUrl("not-a-url", "/next", output, sizeof(output)) ==
         UrlResolutionResult::Malformed);
  assert(resolveRelativeUrl("https://a.example/start", " ", output,
                            sizeof(output)) == UrlResolutionResult::Empty);
  // A redirect status plus an empty Location is a controlled missing-target
  // error for the caller.
  assert(httpResponseAction(302) == HttpResponseAction::Redirect);
  assert(resolveRelativeUrl("https://a.example/start", nullptr, output,
                            sizeof(output)) == UrlResolutionResult::Empty);
}

void testHopBudgetAndNestedResolution() {
  UrlHopBudget selfLoop;
  selfLoop.reset("http://a.example/start");
  assert(selfLoop.follow("http://a.example/start") == UrlHopResult::Loop);

  UrlHopBudget twoNodeLoop;
  twoNodeLoop.reset("http://a.example/start");
  assert(twoNodeLoop.follow("http://b.example/next") ==
         UrlHopResult::Accepted);
  assert(twoNodeLoop.follow("http://a.example/start") == UrlHopResult::Loop);

  UrlHopBudget limit;
  limit.reset("http://limit.example/0");
  assert(limit.follow("http://limit.example/1") == UrlHopResult::Accepted);
  assert(limit.follow("http://limit.example/2") == UrlHopResult::Accepted);
  assert(limit.follow("http://limit.example/3") == UrlHopResult::Accepted);
  assert(limit.follow("http://limit.example/4") == UrlHopResult::Accepted);
  assert(limit.follow("http://limit.example/5") == UrlHopResult::Accepted);
  assert(limit.hops() == 5);
  assert(limit.follow("http://limit.example/6") ==
         UrlHopResult::LimitExceeded);

  // M3U -> redirect -> final stream shares one budget.
  const char configured[] = "https://station.example/list.m3u";
  UrlHopBudget m3uChain;
  m3uChain.reset(configured);
  char m3uUrl[kUrlSize];
  assert(parseM3uEntry("next", configured, m3uUrl, sizeof(m3uUrl)) ==
         UrlResolutionResult::Ok);
  assert(m3uChain.follow(m3uUrl) == UrlHopResult::Accepted);
  char finalUrl[kUrlSize];
  assert(resolveRelativeUrl(m3uUrl, "/live.mp3", finalUrl,
                            sizeof(finalUrl)) == UrlResolutionResult::Ok);
  assert(m3uChain.follow(finalUrl) == UrlHopResult::Accepted);
  assert(std::strcmp(configured, "https://station.example/list.m3u") == 0);
  assert(std::strcmp(finalUrl, "https://station.example/live.mp3") == 0);

  // PLS -> M3U -> stream uses that same chain and catches recursion.
  UrlHopBudget nested;
  nested.reset("https://station.example/list.pls");
  uint16_t entry = 0;
  char nestedM3u[kUrlSize];
  assert(parsePlsEntry("File2=nested/list.m3u",
                       "https://station.example/list.pls", &entry, nestedM3u,
                       sizeof(nestedM3u)) == UrlResolutionResult::Ok);
  assert(nested.follow(nestedM3u) == UrlHopResult::Accepted);
  char stream[kUrlSize];
  assert(parseM3uEntry("../live.aac", nestedM3u, stream, sizeof(stream)) ==
         UrlResolutionResult::Ok);
  assert(nested.follow(stream) == UrlHopResult::Accepted);
  assert(nested.follow(nestedM3u) == UrlHopResult::Loop);
}

}  // namespace

int main() {
  testM3u();
  testPls();
  testRedirectsAndUrls();
  testHopBudgetAndNestedResolution();
}
