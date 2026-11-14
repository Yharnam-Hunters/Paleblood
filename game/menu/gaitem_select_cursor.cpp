// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: moving the item select dialog's cursor (the game's GaitemSelectDialog). The dialog keeps
// a column; its list keeps the row, the first row shown and the scroll bar beside it.
//
// The scroll bar's thumb is placed at the bar's x and at its top plus the row's share of the
// list times the bar's length; its size is the share of the list from the first row shown.
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/scalar.h"

namespace {

// The scroll bar beside the list, only passed to its own functions here.
struct ScrollBar {
    uint8_t unknown_0x00[0xb0];
};
struct ItemList;

struct ItemListVtable {
    void *unknown_slot_0;
    void (*set_scrollable)(ItemList *list, bool scrollable);
};
static_assert(offsetof(ItemListVtable, set_scrollable) == 0x8);

struct ItemList {
    const ItemListVtable *vtable;
    uint8_t unknown_0x008[0xa8];
    ScrollBar scroll_bar;
    float bar_top;
    float bar_x;
    float bar_length;
    int32_t row;
    int32_t row_count;
    int32_t first_shown_row;
};
static_assert(offsetof(ItemList, scroll_bar) == 0xb0 && offsetof(ItemList, bar_top) == 0x160);
static_assert(offsetof(ItemList, bar_x) == 0x164 && offsetof(ItemList, bar_length) == 0x168);
static_assert(offsetof(ItemList, row) == 0x16c && offsetof(ItemList, row_count) == 0x170);
static_assert(offsetof(ItemList, first_shown_row) == 0x174);

struct SelectDialog;
struct SelectDialogVtable {
    void *unknown_slots[7];
    void (*refresh)(SelectDialog *dialog);
};
static_assert(offsetof(SelectDialogVtable, refresh) == 0x38);

struct SelectDialog {
    const SelectDialogVtable *vtable;
    uint8_t unknown_0x08[0x7c];
    int32_t column;
    uint8_t unknown_0x88[0x58];
    ItemList list;
};
static_assert(offsetof(SelectDialog, column) == 0x84 && offsetof(SelectDialog, list) == 0xe0);

// The highest column and row the cursor may take, {column, row} in one register (low and high
// half).
RT_ORIGINAL(0x01ed11f0, cursor_limits, uint64_t(SelectDialog *dialog));
RT_ORIGINAL(0x01edc580, list_set_row, void(ItemList *list, int32_t row));
RT_ORIGINAL(0x01ecb8c0, scroll_bar_place_thumb, void(ScrollBar *bar, int32_t x, int32_t y));
RT_ORIGINAL(0x01ecba50, scroll_bar_size_thumb, void(ScrollBar *bar, float scale, float share));
RT_GLOBAL(0x04d2898c, scroll_thumb_scale, const float);

struct CursorLimits {
    int32_t column;
    int32_t row;
};

constexpr unsigned row_shift = 32;   // the row is the register's high half

CursorLimits unpack(uint64_t limits)
{
    return {static_cast<int32_t>(limits), static_cast<int32_t>(limits >> row_shift)};
}

// `value` within 0 and `limit`, a negative limit counting as 0.
int32_t clamp_to_limit(int32_t value, int32_t limit)
{
    if (value < 0)
        return 0;
    const int32_t highest = limit < 0 ? 0 : limit;
    return value <= highest ? value : highest;
}

}  // namespace

// Moves the cursor by `columns` and `rows`, clamped to the dialog's limits. When the result is
// within the limits as read again and differs from where the cursor was, the column is stored,
// the list moves to the row and the dialog refreshes itself.
extern "C" void bb_menu_gaitem_select_move_cursor_01ed0dd0(SelectDialog *dialog, int32_t columns, int32_t rows)
{
    const int32_t old_column = dialog->column;
    const int32_t old_row = dialog->list.row;
    const CursorLimits limits = unpack(cursor_limits(dialog));
    const int32_t column = clamp_to_limit(columns + old_column, limits.column);
    const int32_t row = clamp_to_limit(rows + old_row, limits.row);
    const CursorLimits again = unpack(cursor_limits(dialog));
    if (row > again.row || column > again.column)
        return;
    if (row == old_row && column == old_column)
        return;
    dialog->column = column;
    list_set_row(&dialog->list, row);
    dialog->vtable->refresh(dialog);
}

// Sets the list's row (below 0: 0; past the last: the last, which is -1 for an empty list),
// tells the list whether it scrolls (first shown row before the end), and when it does, places
// and sizes the scroll bar's thumb. The row, the count and the first shown row are read again
// after the list's virtual call.
extern "C" void bb_menu_gaitem_select_list_set_row_01edc580(ItemList *list, int32_t row)
{
    const int32_t count = list->row_count;
    list->row = row < 0 ? 0 : (row >= count - 1 ? count - 1 : row);
    const int32_t first_shown = list->first_shown_row;
    list->vtable->set_scrollable(list, count > first_shown);
    if (count <= first_shown)
        return;
    const float row_share = static_cast<float>(list->row) / static_cast<float>(list->row_count);
    const float thumb_y = engine::add(list->bar_top, engine::multiply(row_share, list->bar_length));
    scroll_bar_place_thumb(&list->scroll_bar, static_cast<int32_t>(list->bar_x), static_cast<int32_t>(thumb_y));
    const float shown_share = static_cast<float>(list->first_shown_row) / static_cast<float>(list->row_count);
    scroll_bar_size_thumb(&list->scroll_bar, scroll_thumb_scale.get(), shown_share);
}
