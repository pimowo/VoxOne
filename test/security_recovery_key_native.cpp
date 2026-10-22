#include "../src/core/security_recovery_key.h"
#include "../src/hardware/hardware_descriptor.h"
#include <cassert>
using namespace voxone::security_recovery;
int main() {
  BootKeyDetector high; assert(high.begin(false,0)==KeyState::Cancelled); assert(high.finished());
  BootKeyDetector low; assert(low.begin(true,100)==KeyState::Debounce); assert(low.update(true,149)==KeyState::Debounce); assert(low.update(true,150)==KeyState::Accepted); assert(low.finished());
  BootKeyDetector held; held.begin(true,0); assert(held.update(true,500000)==KeyState::Accepted); assert(held.update(false,500001)==KeyState::Accepted);
  BootKeyDetector bounce; bounce.begin(true,0); assert(bounce.update(false,20)==KeyState::Cancelled);
  BootKeyDetector wrap; wrap.begin(true,UINT32_MAX-20U); assert(wrap.update(true,28U)==KeyState::Debounce); assert(wrap.update(true,29U)==KeyState::Accepted);
  const auto& hardware=voxone::hardware::currentHardware();
#if defined(VOXONE_PROFILE_W0)
  assert(hardware.localInput.key==32);
#elif defined(VOXONE_PROFILE_L0)
  assert(hardware.localInput.key==39);
#else
  assert(hardware.localInput.key==4);
#endif
}
