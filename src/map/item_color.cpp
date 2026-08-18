// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "item_color.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include <common/database.hpp>
#include <common/nullpo.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>

#include "battle.hpp"
#include "clif.hpp"
#include "map.hpp"
#include "pc.hpp"

// ============================================================================
//  Database
// ============================================================================

static std::vector<s_item_color_entry> item_color_entries;

static constexpr size_t ITEM_COLOR_NAME_CAPACITY = 128;
static constexpr size_t ITEM_COLOR_PACKET_MAX = 4092;

static bool item_color_parse_rgb(std::string value, uint32& color) {
	if (value.size() >= 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
		value.erase(0, 2);

	if (value.empty() || value.size() > 6 ||
		!std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; }))
		return false;

	color = static_cast<uint32>(std::strtoul(value.c_str(), nullptr, 16));
	return true;
}

class ItemColorDatabase : public YamlDatabase {
public:
	ItemColorDatabase() : YamlDatabase("ITEM_COLOR_DB", 1) {}

	void clear() override {
		item_color_entries.clear();
	}

	const std::string getDefaultLocation() override {
		return std::string(db_path) + "/item_color_db.yml";
	}

	uint64 parseBodyNode(const ryml::NodeRef& node) override {
		std::string itemName;
		std::string colorText;
		uint32 color = 0;

		if (!this->asString(node, "Item", itemName) || itemName.empty() || itemName.size() >= ITEM_COLOR_NAME_CAPACITY) {
			this->invalidWarning(node, "Item must contain 1-%zu UTF-8 bytes.\n", ITEM_COLOR_NAME_CAPACITY - 1);
			return 0;
		}
		if (!this->asString(node, "Color", colorText) || !item_color_parse_rgb(colorText, color)) {
			this->invalidWarning(node, "Color must be a 1-6 digit RRGGBB hexadecimal value.\n");
			return 0;
		}

		for (auto& entry : item_color_entries) {
			if (entry.itemName == itemName) {
				entry.color = color;
				return 1;
			}
		}
		item_color_entries.push_back({ itemName, color & 0x00FFFFFF });
		return 1;
	}
};

static ItemColorDatabase item_color_db;

// ============================================================================
//  Enchant Grade Color Database (YAML → grade_color_entries)
// ============================================================================

static std::vector<s_grade_color_entry> grade_color_entries;

class EnchantGradeColorDatabase : public YamlDatabase {
public:
	EnchantGradeColorDatabase() : YamlDatabase("ENCHANTGRADE_COLOR_DB", 1) {}

	void clear() override {
		grade_color_entries.clear();
		yaml_has_entries = false;
	}

	const std::string getDefaultLocation() override {
		return std::string(db_path) + "/enchantgrade_color_db.yml";
	}

	uint64 parseBodyNode(const ryml::NodeRef& node) override {
		std::string label;
		uint32 color = 0;

		// Grade: numeric (1=Rare, 2=Epic, 3=Mythic, 4=Legendary, 5=S)
		uint32 gradeNum = 0;
		if (!this->asUInt32(node, "Grade", gradeNum) || gradeNum < 1 || gradeNum > 255) {
			ShowWarning("enchantgrade_color_db: missing or invalid Grade - skipping\n");
			return 0;
		}
		uint8 grade = static_cast<uint8>(gradeNum);

		// Label: required - display label without brackets, e.g. "Legendary"
		if (!this->asString(node, "Label", label) || label.empty()) {
			ShowWarning("enchantgrade_color_db: missing Label for grade %u - skipping\n", grade);
			return 0;
		}

		// Color: required - read as string, supports hex (0xFFD700, FFD700) and decimal
		std::string colorText;
		if (!this->asString(node, "Color", colorText) || !item_color_parse_rgb(colorText, color)) {
			ShowWarning("enchantgrade_color_db: invalid RRGGBB Color for grade %u - skipping\n", grade);
			return 0;
		}

		// First YAML entry in this load: ensure a clean slate
		if (!yaml_has_entries) {
			grade_color_entries.clear();
			yaml_has_entries = true;
		}

		// Update existing grade or add a new one
		for (auto& entry : grade_color_entries) {
			if (entry.grade == grade) {
				entry.label = label;
				entry.color = color;
				ShowInfo("enchantgrade_color_db: Grade %u \"%s\" → 0x%06X\n",
					grade, label.c_str(), color);
				return 1;
			}
		}

		grade_color_entries.push_back({ grade, label, color });
		//ShowInfo("enchantgrade_color_db: added Grade %u \"%s\" → 0x%06X\n",grade, label.c_str(), color);
		return 1;
	}

private:
	bool yaml_has_entries = false;
};

static EnchantGradeColorDatabase enchantgrade_color_db;

// ============================================================================
//  Grade Color System
// ============================================================================
// All grade→color+label mappings come from enchantgrade_color_db.yml.
// The YAML is the single source of truth - no compiled-in fallbacks.
//
// Sent as _GRADE_<N>_<Label> sentinel entries (e.g. "_GRADE_4_Legendary")
// via ZC_ITEM_COLOR_LIST (0x0BDD). The WARP QJS patch extracts both the
// grade number and the display label, eliminating hardcoded grade tables.

const std::vector<s_grade_color_entry>& grade_color_get_entries(void) {
	return grade_color_entries;
}

/// Build a wire-format sentinel name from a grade entry.
/// Format: _GRADE_<N>_<Label>  (e.g. "_GRADE_4_Legendary")
/// The WARP QJS patch extracts both the grade number and the display
/// label from this name, eliminating hardcoded grade tables on both sides.
static std::string grade_sentinel_name(const s_grade_color_entry& entry) {
	return "_GRADE_" + std::to_string(entry.grade) + "_" + entry.label;
}

static void grade_color_init(void) {
	grade_color_entries.clear();
}

uint32 item_color_get_grade_color(uint8 grade) {
	for (const auto& entry : grade_color_entries) {
		if (entry.grade == grade)
			return entry.color;
	}
	return 0xFFFFFF; // fallback white for unknown grades
}

/// Inject grade sentinel entries into the item_color_entries vector.
/// These travel inside the existing 0x0BDD packet alongside YAML name entries.
/// Sentinels use _GRADE_<N>_<Label> format so the QJS patch learns both the
/// grade number and the display label dynamically.
static void grade_color_inject_entries(void) {
	if (!battle_config.feature_custom_grade_color_packet)
		return;

	for (const auto& entry : grade_color_entries) {
		std::string sentinel = grade_sentinel_name(entry);
		if (sentinel.size() >= ITEM_COLOR_NAME_CAPACITY) {
			ShowWarning("item_color: grade %u sentinel is too long and was skipped.\n", entry.grade);
			continue;
		}

		// Remove any previous sentinel entry with this name
		item_color_entries.erase(
			std::remove_if(item_color_entries.begin(), item_color_entries.end(),
				[&](const s_item_color_entry& e) { return e.itemName == sentinel; }),
			item_color_entries.end());

		item_color_entries.push_back({ sentinel, entry.color });
		//ShowInfo("item_color: injected sentinel '%s' -> 0x%06X (grade=%u label=%s)\n",sentinel.c_str(), entry.color, entry.grade, entry.label.c_str());
	}
}

// ============================================================================
//  Lifecycle
// ============================================================================

void do_init_item_color(void) {
	item_color_db.load();
	//ShowNotice("item_color: loaded %u name-based entries from item_color_db.yml\n", (unsigned)item_color_entries.size());
	grade_color_init();
	enchantgrade_color_db.load();     // populate grade_color_entries from YAML
	//ShowNotice("item_color: %u grade entries loaded from enchantgrade_color_db.yml\n", (unsigned)grade_color_entries.size());
	grade_color_inject_entries();
	//ShowNotice("item_color: %u total entries ready to send (names + sentinels)\n", (unsigned)item_color_entries.size());
}

void do_final_item_color(void) {
	item_color_entries.clear();
	grade_color_entries.clear();
}

void do_reload_item_color(void) {
	item_color_entries.clear();
	item_color_db.reload();
	grade_color_init();               // reset to empty; YAML will re-populate
	enchantgrade_color_db.reload();
	ShowNotice("item_color: reloaded %u grade entries from enchantgrade_color_db.yml\n", (unsigned)grade_color_entries.size());
	grade_color_inject_entries();
	ShowNotice("item_color: reload complete - %u total entries\n", (unsigned)item_color_entries.size());
}

const std::vector<s_item_color_entry>& item_color_get_entries(void) {
	return item_color_entries;
}

void item_color_set(const std::string& itemName, uint32 color) {
	if (!battle_config.feature_custom_item_color_packet)
		return;
	if (itemName.empty() || itemName.size() >= ITEM_COLOR_NAME_CAPACITY) {
		ShowWarning("item_color_set: item name must contain 1-%zu UTF-8 bytes.\n", ITEM_COLOR_NAME_CAPACITY - 1);
		return;
	}
	color &= 0x00FFFFFF;
	for (auto& entry : item_color_entries) {
		if (entry.itemName == itemName) {
			entry.color = color;
			ShowInfo("item_color_set: updated '%s' -> 0x%06X\n", itemName.c_str(), color);
			clif_item_color_list_all();
			return;
		}
	}
	item_color_entries.push_back({ itemName, color });
	ShowInfo("item_color_set: added '%s' -> 0x%06X\n", itemName.c_str(), color);
	clif_item_color_list_all();
}

// ============================================================================
//  Packet sending
// ============================================================================

void clif_item_color_list(map_session_data* sd) {
	nullpo_retv(sd);
	if (!battle_config.feature_custom_item_color_packet)
		return;

	const auto& entries = item_color_get_entries();
	if (entries.empty()) {
		ShowInfo("item_color: clif_item_color_list for '%s' - no entries, skipping\n", sd->status.name);
		return;
	}

	std::vector<const s_item_color_entry*> wireEntries;
	size_t packetSize = sizeof(struct PACKET_ZC_ITEM_COLOR_LIST);
	for (const auto& entry : entries) {
		if (entry.itemName.empty() || entry.itemName.size() >= ITEM_COLOR_NAME_CAPACITY)
			continue;
		const size_t encodedSize = sizeof(uint16) + entry.itemName.size() + 1 + sizeof(uint32);
		if (packetSize + encodedSize > ITEM_COLOR_PACKET_MAX) {
			ShowWarning("item_color: packet reached the %zu-byte client transport limit; remaining entries were not sent.\n", ITEM_COLOR_PACKET_MAX);
			break;
		}
		wireEntries.push_back(&entry);
		packetSize += encodedSize;
	}
	if (wireEntries.empty())
		return;

	const uint16 packetLen = static_cast<uint16>(packetSize);

	// Build packet buffer using rAthena WBUF helpers
	std::vector<uint8> buf(packetLen, 0);
	uint8* p = buf.data();
	uint16 offset = 0;

	// Header. Client dispatcher transport uses server->client 0x0BDD because
	// stock 0x0BB6 receive metadata is fixed-length on the 2025-07-16 client.
	WBUFW(p, offset) = HEADER_ZC_ITEM_COLOR_LIST; offset += 2;
	WBUFW(p, offset) = packetLen;                 offset += 2;
	WBUFL(p, offset) = static_cast<uint32>(wireEntries.size()); offset += 4;

	// Entries: nameLen (uint16) + name + color (uint32)
	for (const s_item_color_entry* entry : wireEntries) {
		uint16 nameLen = static_cast<uint16>(entry->itemName.length() + 1);
		WBUFW(p, offset) = nameLen;  offset += 2;
		memcpy(p + offset, entry->itemName.c_str(), nameLen);
		offset += nameLen;
		WBUFL(p, offset) = entry->color;  offset += 4;
	}

	//ShowInfo("item_color: sending packet to '%s' - %u entries, %u bytes\n",sd->status.name, (unsigned)entries.size(), packetLen);
	clif_send(buf.data(), packetLen, sd, SELF);
}

void clif_item_color_list_all(void) {
	if (!battle_config.feature_custom_item_color_packet)
		return;

	struct s_mapiterator* iter = mapit_getallusers();
	map_session_data* sd;

	for (sd = (map_session_data*)mapit_first(iter); mapit_exists(iter); sd = (map_session_data*)mapit_next(iter))
		clif_item_color_list(sd);

	mapit_free(iter);
}
