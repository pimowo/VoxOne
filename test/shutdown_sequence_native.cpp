#include "../src/core/shutdown_sequence.h"

#include <cassert>
#include <cstdint>

int main() {
  voxone::shutdown_sequence::Timeline sequence;
  assert(!sequence.active());
  assert(!sequence.screenOffDue(2500));

  sequence.begin(1000);
  assert(sequence.active());
  assert(!sequence.screenOffDue(3499));
  assert(sequence.screenOffDue(3500));

  sequence.cancel();
  assert(!sequence.active());
  assert(!sequence.screenOffDue(4000));

  // Unsigned elapsed-time arithmetic keeps the deadline valid over rollover.
  sequence.begin(UINT32_MAX - 1000U);
  assert(!sequence.screenOffDue(1498));
  assert(sequence.screenOffDue(1499));
  return 0;
}
