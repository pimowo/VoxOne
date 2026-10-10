#include "../src/core/local_input_backend.h"
#include "../src/core/ui_input.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

using voxone::hardware::RuntimeInput;
using voxone::input::Backend;

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

UiInputContext radio() { return {PLAYER, true, true, false, false, false}; }
}  // namespace

int main() {
  const auto none = voxone::input::activationFor(RuntimeInput::None);
  const auto ec11 = voxone::input::activationFor(RuntimeInput::Ec11);
  const auto buttons3 = voxone::input::activationFor(RuntimeInput::Buttons3);
  const auto invalid = voxone::input::activationFor(
      static_cast<RuntimeInput>(0xff));

  assert(none.backend == Backend::None && !none.ec11 && !none.buttons3);
  assert(!voxone::input::configuresGpio(none));
  assert(!voxone::input::attachesIsr(none));
  assert(!voxone::input::usesOneButton(none));
  assert(ec11.backend == Backend::Ec11 && ec11.ec11 && !ec11.buttons3);
  assert(voxone::input::configuresGpio(ec11));
  assert(voxone::input::attachesIsr(ec11));
  assert(voxone::input::usesOneButton(ec11));
  assert(buttons3.backend == Backend::Buttons3 && !buttons3.ec11 &&
         buttons3.buttons3);
  assert(voxone::input::configuresGpio(buttons3));
  assert(!voxone::input::attachesIsr(buttons3));
  assert(voxone::input::usesOneButton(buttons3));
  assert(invalid.backend == Backend::None && !invalid.ec11 &&
         !invalid.buttons3);

  assert(uiInputEvent(Buttons3Input::Left) == UiInputEvent::Left);
  assert(uiInputEvent(Buttons3Input::Right) == UiInputEvent::Right);
  assert(uiInputEvent(Buttons3Input::OkClick) == UiInputEvent::Ok);
  assert(uiInputEvent(Buttons3Input::OkDoubleClick) == UiInputEvent::Double);
  assert(uiInputEvent(Buttons3Input::OkLongPress) == UiInputEvent::Long);
  assert(uiInputEvent(Buttons3Input::OkVeryLongPress) ==
         UiInputEvent::VeryLong);

  const UiInputEvent events[] = {
      UiInputEvent::Left, UiInputEvent::Right, UiInputEvent::Ok,
      UiInputEvent::Double, UiInputEvent::Long, UiInputEvent::VeryLong};
  const EncoderInput encoderEvents[] = {
      EncoderInput::CounterClockwise, EncoderInput::Clockwise,
      EncoderInput::Click, EncoderInput::DoubleClick,
      EncoderInput::LongPress, EncoderInput::VeryLongPress};
  const Buttons3Input buttonEvents[] = {
      Buttons3Input::Left, Buttons3Input::Right, Buttons3Input::OkClick,
      Buttons3Input::OkDoubleClick, Buttons3Input::OkLongPress,
      Buttons3Input::OkVeryLongPress};
  for (unsigned i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
    assert(uiInputEvent(encoderEvents[i]) == events[i]);
    assert(uiInputEvent(buttonEvents[i]) == events[i]);
    assert(resolveUiInput(radio(), uiInputEvent(encoderEvents[i])).action ==
           resolveUiInput(radio(), uiInputEvent(buttonEvents[i])).action);
  }

  const std::string controls = readFile("src/core/controls.cpp");
  assert(controls.find("effectiveRuntimeInput(") != std::string::npos);
  assert(controls.find("if (activeLocalInput.ec11)") != std::string::npos);
  assert(controls.find("if (activeLocalInput.buttons3)") != std::string::npos);
  assert(controls.find("localInputButtons[0].tick()") != std::string::npos);
  assert(controls.find("localInputButtons[1].tick()") != std::string::npos);
  assert(controls.find("attachVeryLongPressStart") != std::string::npos);
  assert(controls.find("button.setup(pin, true, false)") != std::string::npos);
  assert(controls.find("keyInternalPullup") == std::string::npos);
  assert(controls.find("abInternalPullup") == std::string::npos);

  const std::string encoder = readFile("src/yoEncoder/yoEncoder.cpp");
  const size_t constructor = encoder.find("yoEncoder::yoEncoder");
  const size_t begin = encoder.find("void yoEncoder::begin");
  assert(constructor != std::string::npos && begin != std::string::npos);
  assert(encoder.substr(constructor, begin - constructor).find("pinMode") ==
         std::string::npos);
  assert(encoder.substr(begin).find("pinMode") != std::string::npos);
  assert(encoder.find("INPUT_PULLUP") == std::string::npos);
  assert(encoder.find("pinMode(this->encoderAPin, INPUT)") !=
         std::string::npos);
  assert(encoder.find("pinMode(this->encoderBPin, INPUT)") !=
         std::string::npos);

  const std::string cli = readFile("src/core/serialcli.cpp");
  assert(cli.find("Runtime HW input: %s") != std::string::npos);
  assert(cli.find("hw display none") != std::string::npos);
  assert(cli.find("hw input ec11") != std::string::npos);
  assert(cli.find("hw input buttons3") != std::string::npos);
  assert(cli.find("hw input none") != std::string::npos);
  assert(cli.find("Restart required to activate input backend.") !=
         std::string::npos);
}
