#ifndef VOXONE_STATION_DIRECTORY_JOB_H
#define VOXONE_STATION_DIRECTORY_JOB_H

#include <cstdint>
#include <string>

namespace stationDirectory {

class JobState {
 public:
  uint32_t start() {
    if (running_) return 0;
    activeJob_ = ++nextJob_;
    if (!activeJob_) activeJob_ = ++nextJob_;
    running_ = true;
    completed_ = false;
    std::string().swap(body_);
    return activeJob_;
  }

  void cancel(uint32_t job) {
    if (job != activeJob_ || !running_) return;
    activeJob_ = 0;
    running_ = false;
  }

  void finish(uint32_t job, int status, std::string& body) {
    if (job != activeJob_ || !running_) return;
    status_ = status;
    body_.swap(body);
    completed_ = true;
    running_ = false;
  }

  bool poll(uint32_t job, int& status, const char*& body, bool& ready) const {
    if (!job || job != activeJob_ || (!running_ && !completed_)) return false;
    ready = completed_;
    status = ready ? status_ : 202;
    body = ready ? body_.c_str() : "{\"status\":\"pending\"}";
    return true;
  }

  bool workerActive() const { return running_; }

 private:
  uint32_t nextJob_ = 0;
  uint32_t activeJob_ = 0;
  bool running_ = false;
  bool completed_ = false;
  int status_ = 0;
  std::string body_;
};

}  // namespace stationDirectory

#endif
