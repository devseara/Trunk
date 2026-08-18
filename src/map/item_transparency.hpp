// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// Item "Transparency" Ladder Cache
// (Counts how many of specific items exist across storages/inventory including compounded cards.)

#ifndef ITEM_TRANSPARENCY_HPP
#define ITEM_TRANSPARENCY_HPP

#include <cstdint>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/timer.hpp>

enum class e_item_transparency_group : uint8 {
	MVP_CARDS = 0,
	RARE_ITEMS = 1,
};

void do_init_item_transparency(void);
void do_final_item_transparency(void);
void do_reload_item_transparency(void);

int64 item_transparency_get_count(e_item_transparency_group group, uint32 item_id);
int32 item_transparency_get_age_sec(e_item_transparency_group group);
uint32 item_transparency_get_size(e_item_transparency_group group);
uint32 item_transparency_get_itemid(e_item_transparency_group group, uint32 index);
bool item_transparency_has_item(e_item_transparency_group group, uint32 item_id);
uint32 item_transparency_get_holder_count(e_item_transparency_group group, uint32 item_id);
int item_transparency_get_holder_refine(e_item_transparency_group group, uint32 item_id, uint32 index);
const char* item_transparency_get_holder_name(e_item_transparency_group group, uint32 item_id, uint32 index);
const char* item_transparency_get_holder_source(e_item_transparency_group group, uint32 item_id, uint32 index);
uint32 item_transparency_get_holder_nameid(e_item_transparency_group group, uint32 item_id, uint32 index);
uint32 item_transparency_get_holder_card(e_item_transparency_group group, uint32 item_id, uint32 index, uint8 slot);
void item_transparency_force_refresh(void);

TIMER_FUNC(item_transparency_timer);

#endif // ITEM_TRANSPARENCY_HPP