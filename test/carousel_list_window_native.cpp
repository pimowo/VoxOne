#include "../src/core/carousel_list_window.h"

#include <cassert>

int main() {
  using voxone::carousel::listWindow;

  auto window = listWindow(0, 0, 3);
  assert(window.first == 0 && window.count == 0);

  window = listWindow(5, 1, 3);
  assert(window.first == 0 && window.count == 3);
  window = listWindow(5, 3, 3);
  assert(window.first == 1 && window.count == 3);
  window = listWindow(5, 5, 3);
  assert(window.first == 2 && window.count == 3);

  window = listWindow(4, 2, 6);
  assert(window.first == 0 && window.count == 4);
  window = listWindow(4, 99, 3);
  assert(window.first == 1 && window.count == 3);
  return 0;
}
