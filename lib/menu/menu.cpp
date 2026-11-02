#include "menu.h"

static const MenuItem* currentItems = nullptr;
static uint8_t currentCount = 0;
static uint8_t selectedIndex = 0;

static const MenuItem* parentCurrentItems = nullptr;
static uint8_t parentCurrentCount = 0;
static uint8_t parentSelectedIndex = 0;

MenuInitResult menuInit(const MenuItem* items, uint8_t count) {
  if (items == nullptr || count == 0) {
    return MenuInitResult::INVALID_ITEMS;
  }
  if ((items->action == nullptr && items->children == nullptr) ||
      items->label == nullptr) {
    return MenuInitResult::INVALID_ITEMS;
  }
  currentItems = items;
  currentCount = count;
  selectedIndex = 0;
  parentCurrentItems = nullptr;
  parentCurrentCount = 0;
  parentSelectedIndex = 0;

  return MenuInitResult::SUCCESS;
}

bool menuNext() {
  if (currentItems == nullptr) {
    return false;
  }
  if (selectedIndex < currentCount - 1) {
    selectedIndex++;
    return true;
  }
  return false;
}
bool menuPrev() {
  if (currentItems == nullptr) {
    return false;
  }
  if (selectedIndex > 0) {
    selectedIndex--;
    return true;
  }

  return false;
}

bool menuSelect() {
  if (currentItems == nullptr) {
    return false;
  }

  MenuItem item = currentItems[selectedIndex];

  if (item.children == nullptr) {
    if (item.action) {
      item.action();
      return true;
    }
    return false;
  }
  menuEnterSubmenu(item.children, item.childCount);
  return true;
}

const MenuItem* menuGetItems() {
  if (currentItems == nullptr) {
    return nullptr;
  }
  return currentItems;
}

uint8_t menuGetCount() {
  if (currentItems == nullptr) {
    return 0;
  }
  return currentCount;
}

uint8_t menuGetSelectedIndex() {
  if (currentItems == nullptr) {
    return 0;
  }
  return selectedIndex;
}
bool menuBack() {
  if (parentCurrentItems == nullptr) {
    return false;
  }

  if (parentCurrentCount == 0) {
    return false;
  }

  currentItems = parentCurrentItems;
  currentCount = parentCurrentCount;
  selectedIndex = parentSelectedIndex;

  // reset so module knows it's back at the root
  parentCurrentItems = nullptr;
  parentCurrentCount = 0;
  parentSelectedIndex = 0;
  return true;
}

bool menuEnterSubmenu(const MenuItem* items, uint8_t count) {
  if (items == nullptr || count == 0) {
    return false;
  }
  if ((items->action == nullptr && items->children == nullptr) ||
      items->label == nullptr) {
    return false;
  }
  // saves current state so you can return later.
  parentCurrentItems = currentItems;
  parentCurrentCount = currentCount;
  parentSelectedIndex = selectedIndex;

  // Set to the new submenu items
  currentItems = items;
  currentCount = count;
  selectedIndex = 0;

  return true;
}