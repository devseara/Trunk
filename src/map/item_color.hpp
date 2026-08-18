// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// Server-side item color database.
// Loads db/item_color_db.yml and sends colors to clients via ZC_ITEM_COLOR_LIST.

#ifndef ITEM_COLOR_HPP
#define ITEM_COLOR_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/cbasetypes.hpp>

class map_session_data;

struct s_item_color_entry {
	std::string itemName;
	uint32 color; // 0x00RRGGBB
};

// ============================================================================
//  Grade Color System
// ============================================================================
// Grade-to-color mapping for enchant-grade-based item name coloring.
// Colors travel inside ZC_ITEM_COLOR_LIST (0x0BDD) as _GRADE_* sentinel
// entries detected by the WARP QJS item-color patch.

struct s_grade_color_entry {
	uint16 grade;       // e_enchantgrade enum value
	std::string label;  // display label without brackets, e.g. "Legendary"
	uint32 color;       // 0x00RRGGBB
};

/// Returns the built-in grade color table (read-only).
const std::vector<s_grade_color_entry>& grade_color_get_entries(void);

/// Look up the display color for an enchant grade value (0=ENCHANTGRADE_NONE → white).
/// Returns the configured color, or 0xFFFFFF (white) if the grade is unknown.
uint32 item_color_get_grade_color(uint8 grade);

// ============================================================================
//  Item Color System (name-based)
// ============================================================================

void do_init_item_color(void);
void do_final_item_color(void);
void do_reload_item_color(void);

/// Access the loaded color map (read-only).
const std::vector<s_item_color_entry>& item_color_get_entries(void);

/// Set or update an item color at runtime and broadcast to all online players.
void item_color_set(const std::string& itemName, uint32 color);

#endif // ITEM_COLOR_HPP
