#ifndef VOXONE_OWNED_POINTER_LIST_H
#define VOXONE_OWNED_POINTER_LIST_H

namespace voxone {
namespace display {

template <typename Container>
void deleteOwnedPointers(Container& pointers) {
  while (!pointers.empty()) {
    auto* pointer = pointers.front();
    pointers.pop_front();
    delete pointer;
  }
}

}  // namespace display
}  // namespace voxone

#endif
