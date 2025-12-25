#ifndef LED_H
#define LED_H

#include <stdint.h>

// Initialize LED hardware
void ledInit();

// Semantic LED states
void ledShowBoot();
void ledShowIdle();
void ledShowUpdating();

// Turn LED completely off
void ledOff();

#endif // LED_H
