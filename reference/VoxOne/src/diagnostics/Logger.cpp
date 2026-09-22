#include "Logger.h"
#include "PerfDiagnostics.h"

static const char* levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

void Logger::begin(uint32_t baud) {
    Serial.begin(baud);
    delay(100);
}

void Logger::log(LogLevel level, const char* module, const String& message) {
#ifndef VOXONE_DEBUG
    if (level == LogLevel::Debug) return;
#endif
    PerfScope perf(PerfArea::Log);
    Serial.printf("[%10lu] %-5s %-10s %s\n",
                  (unsigned long)millis(),
                  levelName(level),
                  module,
                  message.c_str());
}

void Logger::debug(const char* m, const String& s) { log(LogLevel::Debug, m, s); }
void Logger::info(const char* m, const String& s)  { log(LogLevel::Info,  m, s); }
void Logger::warn(const char* m, const String& s)  { log(LogLevel::Warn,  m, s); }
void Logger::error(const char* m, const String& s) { log(LogLevel::Error, m, s); }
