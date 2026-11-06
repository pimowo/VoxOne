#include "../src/core/bt_link_command_queue.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  BoundedBtLinkCommandQueue<4> queue;
  assert(queue.push(BtLinkCommand(BtLinkCommandKind::Play)));
  assert(queue.push(BtLinkCommand(BtLinkCommandKind::Pause)));
  assert(queue.push(BtLinkCommand(BtLinkCommandKind::Next)));
  assert(queue.push(BtLinkCommand(BtLinkCommandKind::Previous)));
  assert(!queue.push(BtLinkCommand(BtLinkCommandKind::Ping)));
  BtLinkCommand command;
  assert(queue.pop(command) && command.kind == BtLinkCommandKind::Play);
  assert(queue.pop(command) && command.kind == BtLinkCommandKind::Pause);
  assert(queue.pop(command) && command.kind == BtLinkCommandKind::Next);
  assert(queue.pop(command) && command.kind == BtLinkCommandKind::Previous);
  assert(!queue.pop(command));

  BoundedBtLinkCommandQueue<4096> concurrent;
  std::mutex mutex;
  std::vector<std::thread> producers;
  for (int producer = 0; producer < 4; ++producer) {
    producers.emplace_back([&] {
      for (int i = 0; i < 1000; ++i) {
        std::lock_guard<std::mutex> lock(mutex);
        assert(concurrent.push(BtLinkCommand(BtLinkCommandKind::Next)));
      }
    });
  }
  for (auto& producer : producers) producer.join();
  assert(concurrent.size() == 4000);
  for (int i = 0; i < 4000; ++i) {
    std::lock_guard<std::mutex> lock(mutex);
    assert(concurrent.pop(command));
  }
  assert(concurrent.size() == 0);

  const std::string header = readFile("src/core/bt_link.h");
  const std::string source = readFile("src/core/bt_link.cpp");
  assert(header.find("BoundedBtLinkCommandQueue") != std::string::npos);
  assert(header.find("publishedState_") != std::string::npos);
  assert(header.find("std::atomic<bool> commandsAccepted_") !=
         std::string::npos);
  assert(source.find("bool BtLink::play() { return enqueue(BtLinkCommand(") !=
         std::string::npos);
  assert(source.find("void BtLink::processCommands()") != std::string::npos);
  assert(source.find("protocol_.play();") != std::string::npos);
  assert(source.find("BtLinkState BtLink::state() const") !=
         std::string::npos);
  assert(source.find("portENTER_CRITICAL(&stateMux_)") != std::string::npos);
  assert(source.find("commandsAccepted_.load(std::memory_order_relaxed)") !=
         std::string::npos);
}
