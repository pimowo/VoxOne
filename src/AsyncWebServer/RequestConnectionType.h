#ifndef ASYNCWEBSERVER_REQUEST_CONNECTION_TYPE_H
#define ASYNCWEBSERVER_REQUEST_CONNECTION_TYPE_H

#include <stddef.h>

inline char asyncWebAsciiLower(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32) : value;
}

inline bool asyncWebEqualsIgnoreCase(const char* left, const char* right) {
  if (!left || !right) return false;
  while (*left && *right) {
    if (asyncWebAsciiLower(*left++) != asyncWebAsciiLower(*right++))
      return false;
  }
  return *left == '\0' && *right == '\0';
}

// WebRequest.cpp uses this exact predicate when deriving requestedConnType().
// Keeping it allocation-free and Arduino-independent lets the security tests
// exercise the real header classification rather than a hand-set RCT_WS flag.
inline bool asyncWebHeaderRequestsWebSocket(const char* name,
                                            const char* value) {
  return asyncWebEqualsIgnoreCase(name, "Upgrade") &&
         asyncWebEqualsIgnoreCase(value, "websocket");
}

#endif
