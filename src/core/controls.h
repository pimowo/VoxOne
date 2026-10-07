#ifndef controls_h
#define controls_h
#include "common.h"

boolean checklpdelay(int m, unsigned long &tstamp);

void initControls();
void loopControls();
void encoder1Loop();
void controlsEvent(bool toRight, int8_t volDelta = 0);

void onBtnClick(int id);
void onBtnDoubleClick(int id);
void onBtnDuringLongPress(int id);
void onBtnLongPressStart(int id);
void onBtnLongPressStop(int id);

void setEncAcceleration(uint16_t acc);

extern __attribute__((weak)) void ctrls_on_loop();

#endif
