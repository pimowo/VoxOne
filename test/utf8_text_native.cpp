#include "../src/core/utf8_text.h"

#include <cassert>
#include <cstring>

int main() {
  using namespace voxone::utf8;
  const char* polish = "Zażółć gęślą jaźń";
  assert(count(polish) == 17);

  char output[32]{};
  assert(copy(output, sizeof(output), polish) == std::strlen(polish));
  assert(std::strcmp(output, polish) == 0);

  char shortOutput[12]{};
  assert(ellipsize(shortOutput, sizeof(shortOutput), polish, 8));
  assert(std::strcmp(shortOutput, "Zażół...") == 0);
  assert(count(shortOutput) == 8);

  const char split[] = {'A', static_cast<char>(0xc4),
                        static_cast<char>(0x85), 'B', '\0'};
  char bounded[3]{};
  copy(bounded, sizeof(bounded), split);
  assert(std::strcmp(bounded, "A") == 0);
  char tail[] = {'A', static_cast<char>(0xc4), '\0'};
  trimIncompleteTail(tail);
  assert(std::strcmp(tail, "A") == 0);

  char encoded[4]{};
  assert(encodeCodepoint(0x105, encoded) == 2);
  assert(static_cast<unsigned char>(encoded[0]) == 0xc4);
  assert(static_cast<unsigned char>(encoded[1]) == 0x85);
  assert(encodeCodepoint(0x1f3b5, encoded) == 4);
  assert(encodeCodepoint(0xd800, encoded) == 0);
  return 0;
}
