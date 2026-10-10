#include "Arduino.h"
#include "options.h"
#include "controls.h"
#include "config.h"
#include "player.h"
#include "display.h"
#include "network.h"
#include "update_progress.h"
#include "ui_input.h"
#include "ui_state.h"
#include "../hardware/hardware_descriptor.h"

long encOldPosition  = 0;
int lpId = -1;

#if DSP_MODEL==DSP_DUMMY
#define DUMMYDISPLAY
#endif

#define ISPUSHBUTTONS BTN_LEFT!=255 || BTN_CENTER!=255 || BTN_RIGHT!=255 || ENC_BTNB!=255 || BTN_UP!=255 || BTN_DOWN!=255 || BTN_MODE!=255
#if ISPUSHBUTTONS
#include "../OneButton/OneButton.h"
struct ButtonBinding {
  OneButton button;
  controlEvt_e event;
};

ButtonBinding buttons[] {
#if BTN_LEFT!=255
  {{BTN_LEFT, true, BTN_INTERNALPULLUP}, EVT_BTNLEFT},
#endif
#if BTN_CENTER!=255
  {{BTN_CENTER, true, BTN_INTERNALPULLUP}, EVT_BTNCENTER},
#endif
#if BTN_RIGHT!=255
  {{BTN_RIGHT, true, BTN_INTERNALPULLUP}, EVT_BTNRIGHT},
#endif
#if ENC_BTNB!=255
  {{voxone::hardware::currentHardware().encoder.button, true,
    voxone::hardware::currentHardware().encoder.buttonInternalPullup}, EVT_ENCBTNB},
#endif
#if BTN_UP!=255
  {{BTN_UP, true, BTN_INTERNALPULLUP}, EVT_BTNUP},
#endif
#if BTN_DOWN!=255
  {{BTN_DOWN, true, BTN_INTERNALPULLUP}, EVT_BTNDOWN},
#endif
#if BTN_MODE!=255
  {{BTN_MODE, true, BTN_INTERNALPULLUP}, EVT_BTNMODE},
#endif
};
constexpr uint8_t nrOfButtons = sizeof(buttons) / sizeof(buttons[0]);
#endif

#if ENC_BTNL!=255 && ENC_BTNR!=255
  #include "../yoEncoder/yoEncoder.h"
  yoEncoder encoder = yoEncoder(voxone::hardware::currentHardware().encoder.a,
                                voxone::hardware::currentHardware().encoder.b,
                                voxone::hardware::currentHardware().encoder.stepsPerDetent,
                                voxone::hardware::currentHardware().encoder.internalPullup);
#endif

#if ENC_BTNL!=255
void IRAM_ATTR readEncoderISR()
{
  encoder.readEncoder_ISR();
}
#endif
void initControls() {
  
#if ENC_BTNL!=255
  encoder.begin();
  encoder.setup(readEncoderISR);
  encoder.setBoundaries(0, 254, true);
  encoder.setAcceleration(config.store.encacc);
#endif
#if ISPUSHBUTTONS
  for (int i = 0; i < nrOfButtons; i++)
  {
    void* event = &buttons[i].event;
    buttons[i].button.attachClick([](void* p) {
      onBtnClick(*static_cast<controlEvt_e*>(p));
    }, event);
    buttons[i].button.attachDoubleClick([](void* p) {
      onBtnDoubleClick(*static_cast<controlEvt_e*>(p));
    }, event);
    buttons[i].button.attachLongPressStart([](void* p) {
      onBtnLongPressStart(*static_cast<controlEvt_e*>(p));
    }, event);
    buttons[i].button.attachLongPressStop([](void* p) {
      onBtnLongPressStop(*static_cast<controlEvt_e*>(p));
    }, event);
    buttons[i].button.setClickTicks(BTN_CLICK_TICKS);
    buttons[i].button.setPressTicks(BTN_PRESS_TICKS);
  }
#endif
}

void loopControls() {
  if(updateLockActive() || uiState.mode()==UPDATING || uiState.mode()==LOST) {
#if ENC_BTNL!=255
    // The ISR is hardware-only. Drain accumulated motion while core input is
    // locked so it cannot become a delayed UI event after the lock ends.
    (void)encoder.encoderChanged();
#endif
    return;
  }
  if(ctrls_on_loop) ctrls_on_loop();
#if ENC_BTNL!=255
  encoder1Loop();
#endif
#if ISPUSHBUTTONS
  for (unsigned i = 0; i < nrOfButtons; i++)
  {
    buttons[i].button.tick();
    if (lpId >= 0) {
      if (DSP_MODEL == DSP_DUMMY && (lpId == 4 || lpId == 5)) continue;
      onBtnDuringLongPress(lpId);
    }
  }
#endif
}
#if ENC_BTNL!=255
void encoder1Loop() {
  int8_t encoderDelta = encoder.encoderChanged();
  if (encoderDelta!=0)
  {
    dispatchUiInput(uiInputEvent(encoderDelta > 0
                                     ? EncoderInput::Clockwise
                                     : EncoderInput::CounterClockwise),
                    encoderDelta);
  }
}
#endif

void onBtnLongPressStart(int id) {
  switch ((controlEvt_e)id) {
    case EVT_BTNLEFT:
    case EVT_BTNRIGHT:
    case EVT_BTNUP:
    case EVT_BTNDOWN: {
        lpId = id;
        break;
      }
    case EVT_BTNCENTER: {
        dispatchUiInput(uiInputEvent(Buttons3Input::OkLongPress));
        break;
      }
    case EVT_ENCBTNB: {
        dispatchUiInput(uiInputEvent(EncoderInput::LongPress));
        break;
      }
    case EVT_BTNMODE: {
        //config.doSleepW();
        transitionUiMode(SLEEPING);
        break;
      }
    default: break;
  }
}

void onBtnLongPressStop(int id) {
  switch ((controlEvt_e)id) {
    case EVT_BTNLEFT:
    case EVT_BTNRIGHT:
    case EVT_BTNUP:
    case EVT_BTNDOWN: {
        lpId = -1;
        break;
      }
    case EVT_BTNMODE: {
        config.doSleepW();
        break;
      }
    default:
        break;
  }
}

unsigned long lpdelay;
boolean checklpdelay(int m, unsigned long &tstamp) {
  if (millis() - tstamp > m) {
    tstamp = millis();
    return true;
  } else {
    return false;
  }
}

void onBtnDuringLongPress(int id) {
  if (network.status != CONNECTED) return;
  if (checklpdelay(BTN_LONGPRESS_LOOP_DELAY, lpdelay)) {
    switch ((controlEvt_e)id) {
      case EVT_BTNLEFT: {
          controlsEvent(false);
          break;
        }
      case EVT_BTNRIGHT: {
          controlsEvent(true);
          break;
        }
      case EVT_BTNUP:
      case EVT_BTNDOWN: {
          if (uiState.mode() == PLAYER) {
            transitionUiMode(STATIONS);
          }
          if (uiState.mode() == STATIONS) {
            controlsEvent(id == EVT_BTNDOWN);
          }
          break;
        }
      default:
          break;
    }
  }
}

void controlsEvent(bool toRight, int8_t volDelta) {
  const bool right = volDelta != 0 ? volDelta > 0 : toRight;
  dispatchUiInput(right ? UiInputEvent::Right : UiInputEvent::Left,
                  volDelta == 0 ? 1 : volDelta);
}

void onBtnClick(int id) {
  controlEvt_e btnid = static_cast<controlEvt_e>(id);
  switch (btnid) {
    case EVT_BTNLEFT: {
        dispatchUiInput(uiInputEvent(Buttons3Input::Left));
        break;
      }
    case EVT_BTNCENTER: {
        dispatchUiInput(uiInputEvent(Buttons3Input::OkClick));
        break;
      }
    case EVT_ENCBTNB: {
        dispatchUiInput(uiInputEvent(EncoderInput::Click));
        break;
      }
    case EVT_BTNRIGHT: {
        dispatchUiInput(uiInputEvent(Buttons3Input::Right));
        break;
      }
    case EVT_BTNUP:
    case EVT_BTNDOWN: {
        if (network.status != CONNECTED) return;
        if (DSP_MODEL == DSP_DUMMY) {
          if (id == EVT_BTNUP) {
            player.next();
          } else {
            player.prev();
          }
        } else {
          if (uiState.mode() == PLAYER) {
            if(config.store.skipPlaylistUpDown){
              if (id == EVT_BTNUP) {
                player.prev();
              } else {
                player.next();
              }
            }else{
              transitionUiMode(STATIONS);
            }
          }
          if (uiState.mode() == STATIONS) {
            controlsEvent(id == EVT_BTNDOWN);
          }
        }
        break;
      }
    default: break;
  }
}

void onBtnDoubleClick(int id) {
  switch ((controlEvt_e)id) {
    case EVT_BTNLEFT: {
        if (uiState.mode() == SCREENSAVER || uiState.mode() == SCREENBLANK) {
          transitionUiMode(PLAYER);
          return;
        }
        if (uiState.mode() != PLAYER) return;
        if (network.status != CONNECTED) return;
        player.prev();
        break;
      }
    case EVT_BTNCENTER: {
        dispatchUiInput(uiInputEvent(Buttons3Input::OkDoubleClick));
        break;
      }
    case EVT_ENCBTNB: {
        dispatchUiInput(uiInputEvent(EncoderInput::DoubleClick));
        break;
      }
    case EVT_BTNRIGHT: {
        if (uiState.mode() == SCREENSAVER || uiState.mode() == SCREENBLANK) {
          transitionUiMode(PLAYER);
          return;
        }
        if (uiState.mode() != PLAYER) return;
        if (network.status != CONNECTED) return;
        player.next();
        break;
      }
    default:
        break;
  }
}
void setEncAcceleration(uint16_t acc){
  config.saveValue(&config.store.encacc, acc);
#if ENC_BTNL!=255
  encoder.setAcceleration(config.store.encacc);
#endif
}
