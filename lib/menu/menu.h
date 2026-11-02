
#ifndef MENU_H
#define MENU_H

#include <cstdint>

enum class MenuInitResult { SUCCESS, INVALID_ITEMS };

struct MenuItem {
  const char* label;
  void (*action)();          // non-null if this is a leaf action
  const MenuItem* children;  // non-null if this is a submenu
  uint8_t childCount;
};

MenuInitResult menuInit(const MenuItem* items, uint8_t count);

bool menuNext();

bool menuPrev();

bool menuSelect();

const MenuItem* menuGetItems();

uint8_t menuGetCount();

uint8_t menuGetSelectedIndex();

bool menuBack();

bool menuEnterSubmenu(const MenuItem* items, uint8_t count);

#endif