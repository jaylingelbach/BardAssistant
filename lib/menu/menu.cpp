#include "menu.h"

static const MenuItem* currentItems = nullptr;
static uint8_t currentCount = 0;
static uint8_t selectedIndex = 0;

static const MenuItem* parentCurrentItems = nullptr;
static uint8_t parentCurrentCount = 0;
static uint8_t parentSelectedIndex = 0;

/**
 * @brief Initializes the root menu and selects its first item.
 *
 * The array is retained and must remain valid while the menu uses it. Only
 * the first item is checked: it must have a label and an action or children.
 * A successful initialization clears the saved parent; failure leaves the
 * current menu unchanged.
 *
 * @param items Array of menu items.
 * @param count Number of items in the array.
 * @return `SUCCESS`, or `INVALID_ITEMS` if the array is null, empty, or its
 *         first item fails validation.
 */
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

/**
 * @brief Moves the selection forward without wrapping.
 * @return `true` if the selection moved, or `false` if no menu is active or
 *         the last item is selected.
 */
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
/**
 * @brief Moves the selection backward without wrapping.
 * @return `true` if the selection moved, or `false` if no menu is active or
 *         the first item is selected.
 */
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

/**
 * @brief Selects the current item, entering its children or calling its action.
 *
 * Children take precedence over an action. A submenu entry can fail validation
 * even when this function returns `true`.
 *
 * @return `true` if the item has children or its action was called; `false`
 *         if no menu is active or the item has neither children nor an action.
 */
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

/**
 * @brief Gets the current menu's retained item array.
 * @return The array, or null if no menu is active.
 */
const MenuItem* menuGetItems() {
  if (currentItems == nullptr) {
    return nullptr;
  }
  return currentItems;
}

/**
 * @brief Gets the number of items in the current menu.
 * @return The count, or zero if no menu is active.
 */
uint8_t menuGetCount() {
  if (currentItems == nullptr) {
    return 0;
  }
  return currentCount;
}

/**
 * @brief Gets the current selection's zero-based index.
 * @return The index, or zero if no menu is active.
 */
uint8_t menuGetSelectedIndex() {
  if (currentItems == nullptr) {
    return 0;
  }
  return selectedIndex;
}
/**
 * @brief Restores the saved parent menu and its selection.
 * @return `true` if a parent was restored, or `false` if none is available.
 */
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

/**
 * @brief Enters a submenu, selecting its first item and saving the current menu.
 *
 * The array is retained and must remain valid while the menu uses it. Only
 * the first item is checked: it must have a label and an action or children.
 * Entering another submenu replaces the saved parent. Failure leaves the
 * current menu and saved parent unchanged.
 *
 * @param items Array of submenu items.
 * @param count Number of items in the array.
 * @return `true` on entry, or `false` if the array is null, empty, or its
 *         first item fails validation.
 */
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