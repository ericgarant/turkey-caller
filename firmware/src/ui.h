// ui: the box's own screen and buttons (F15). An LCD1602 on I2C (SDA 21, SCL 22) and four big
// buttons to GND: yellow Up 32, blue Down 33, green Play 25, red Stop 26. Pick a call with Up and
// Down, play it with green, stop it with red. Uses the same play, stop, volume and Repeat code as
// the page (control), so the box and the phones always agree. Built only with TC_HAS_UI.
#pragma once

namespace ui {

void begin();  // after library, player and bt: finds the LCD and starts the ui task

}  // namespace ui
