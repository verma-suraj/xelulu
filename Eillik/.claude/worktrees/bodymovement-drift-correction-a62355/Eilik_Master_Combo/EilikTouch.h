// Enhanced capacitive-pad driver for the full-combo master. Reports press /
// release edges, which pads are currently held (for 2-sensor combos), and the
// hold duration on release (for long-press vs tap). Head pad + body/chest pad.
#ifndef EILIK_TOUCH_H
#define EILIK_TOUCH_H

#include <Arduino.h>

#define PAD_HEAD 0x01   // bit for the head pad
#define PAD_BODY 0x02   // bit for the body/chest pad

struct TouchEx {
  uint8_t  pressed;     // pads that went DOWN this call (edge, bitmask)
  uint8_t  released;    // pads that went UP this call (edge, bitmask)
  uint8_t  held;        // pads currently down (bitmask)
  uint16_t holdMs[2];   // [0]=head [1]=body — hold length, set on that pad's release
};

TouchEx touchUpdate();  // call once per loop()

#endif
