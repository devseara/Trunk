// Copyright (c) Shakto Scripts - https://ronovelty.com/

#include "autoattack.hpp"
#include "fake_players.hpp"
#include "battle.hpp"
#include "log.hpp"
#include "map.hpp" // mmysql_handle
#include "npc.hpp"
#include "party.hpp"
#include "pc.hpp"
#include "skill.hpp"
#include "unit.hpp"

#include <random>
#include <queue>
#include <cmath>
#include <tuple>
#include <unordered_set>
#include <set>
#include <chrono>
#include <vector>
//Multi thread
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

#include <common/cbasetypes.hpp>
#include <common/database.hpp>
#include <common/malloc.hpp>
#include <common/nullpo.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>
#include <common/utilities.hpp>
#include <common/utils.hpp>

using namespace rathena;

std::vector<t_itemid> AA_ITEMIDS = { 14316 }; // Important here, define the item on which you can start autoattack from rental item

void aa_save(map_session_data* sd) {
	int i;

	//aa_common_config
	if (SQL_ERROR == Sql_Query(mmysql_handle, "INSERT INTO `aa_common_config` (`char_id`,`stopmelee`,`pickup_item_config`,`prio_item_config`,`aggressive_behavior`,`autositregen_conf`,`autositregen_maxhp`,`autositregen_minhp`,`autositregen_maxsp`,`autositregen_minsp`,`tp_use_teleport`,`tp_use_flywing`,`tp_min_hp`,`tp_delay_nomobmeet`,`tp_mvp`,`tp_miniboss`,`accept_party_request`,`token_siegfried`,`return_to_savepoint`,`map_mob_selection`,`action_on_end`,`monster_surround`) VALUES (%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d) ON DUPLICATE KEY UPDATE `stopmelee` = %d, `pickup_item_config` = %d, `prio_item_config` = %d, `aggressive_behavior` = %d, `autositregen_conf` = %d, `autositregen_maxhp` = %d, `autositregen_minhp` = %d, `autositregen_maxsp` = %d, `autositregen_minsp` = %d, `tp_use_teleport` = %d, `tp_use_flywing` = %d, `tp_min_hp` = %d, `tp_delay_nomobmeet` = %d, `tp_mvp` = %d, `tp_miniboss` = %d, `accept_party_request` = %d, `token_siegfried` = %d, `return_to_savepoint` = %d, `map_mob_selection` = %d, `action_on_end` = %d, `monster_surround` = %d ", sd->status.char_id, sd->aa.stopmelee, sd->aa.pickup_item_config, sd->aa.prio_item_config, sd->aa.mobs.aggressive_behavior, sd->aa.autositregen.is_active, sd->aa.autositregen.max_hp, sd->aa.autositregen.min_hp, sd->aa.autositregen.max_sp, sd->aa.autositregen.min_sp, sd->aa.teleport.use_teleport, sd->aa.teleport.use_flywing, sd->aa.teleport.min_hp, sd->aa.teleport.delay_nomobmeet, sd->aa.teleport.tp_mvp, sd->aa.teleport.tp_miniboss, sd->aa.accept_party_request, sd->aa.token_siegfried, sd->aa.return_to_savepoint, sd->aa.mobs.map, sd->aa.action_on_end, sd->aa.monster_surround, sd->aa.stopmelee, sd->aa.pickup_item_config, sd->aa.prio_item_config, sd->aa.mobs.aggressive_behavior, sd->aa.autositregen.is_active, sd->aa.autositregen.max_hp, sd->aa.autositregen.min_hp, sd->aa.autositregen.max_sp, sd->aa.autositregen.min_sp, sd->aa.teleport.use_teleport, sd->aa.teleport.use_flywing, sd->aa.teleport.min_hp, sd->aa.teleport.delay_nomobmeet, sd->aa.teleport.tp_mvp, sd->aa.teleport.tp_miniboss, sd->aa.accept_party_request, sd->aa.token_siegfried, sd->aa.return_to_savepoint, sd->aa.mobs.map, sd->aa.action_on_end, sd->aa.monster_surround)) {
		Sql_ShowDebug(mmysql_handle);
	}

	//clean aa_items
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE FROM `aa_items` WHERE `char_id` = %d", sd->status.char_id)) {
		Sql_ShowDebug(mmysql_handle);
	}
	//insert aa_items - 0 - autobuffitems
	if (!sd->aa.autobuffitems.empty()) {
		for (auto& itAutobuffitem : sd->aa.autobuffitems) {
			if (SQL_ERROR == Sql_Query(mmysql_handle, "INSERT INTO `aa_items` (`char_id`,`type`,`item_id`,`status`) VALUES (%d, 0, %d, %d)", sd->status.char_id, itAutobuffitem.item_id, itAutobuffitem.status)) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
	}
	//insert aa_items - 1 - autopotion
	if (!sd->aa.autopotion.empty()) {
		for (auto& itAutopotion : sd->aa.autopotion) {
			if (SQL_ERROR == Sql_Query(mmysql_handle, "INSERT INTO `aa_items` (`char_id`,`type`,`item_id`,`min_hp`,`min_sp`) VALUES (%d, 1, %d, %d, %d)", sd->status.char_id, itAutopotion.item_id, itAutopotion.min_hp, itAutopotion.min_sp)) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
	}
	//insert aa_items - 2 - pickup_item_id
	if (!sd->aa.pickup_item_id.empty()) {
		for (i = 0; i < sd->aa.pickup_item_id.size(); i++) {
			if (SQL_ERROR == Sql_Query(mmysql_handle, "INSERT INTO `aa_items` (`char_id`,`type`,`item_id`) VALUES (%d, 2, %d)", sd->status.char_id, sd->aa.pickup_item_id.at(i))) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
	}

	//clean aa_mobs
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE FROM `aa_mobs` WHERE `char_id` = %d", sd->status.char_id)) {
		Sql_ShowDebug(mmysql_handle);
	}
	//insert aa_mobs
	if (!sd->aa.mobs.id.empty()) {
		for (i = 0; i < sd->aa.mobs.id.size(); i++) {
			if (SQL_ERROR == Sql_Query(mmysql_handle, "INSERT INTO `aa_mobs` (`char_id`,`mob_id`) VALUES (%d, %d)", sd->status.char_id, sd->aa.mobs.id.at(i))) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
	}

	//clean aa_skills
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE FROM `aa_skills` WHERE `char_id` = %d", sd->status.char_id)) {
		Sql_ShowDebug(mmysql_handle);
	}
	//insert aa_skills - 0 - autoheal
	if (!sd->aa.autoheal.empty()) {
		for (auto& itAutoheal : sd->aa.autoheal) {
			if (SQL_ERROR == Sql_Query(mmysql_handle, "INSERT INTO `aa_skills` (`char_id`,`type`,`skill_id`,`skill_lv`,`min_hp`) VALUES (%d, 0, %d, %d, %d)", sd->status.char_id, itAutoheal.skill_id, itAutoheal.skill_lv, itAutoheal.min_hp)) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
	}
	//insert aa_skills - 1 - autobuffskills
	if (!sd->aa.autobuffskills.empty()) {
		for (auto& itAutobuffskills : sd->aa.autobuffskills) {
			if (SQL_ERROR == Sql_Query(mmysql_handle, "INSERT INTO `aa_skills` (`char_id`,`type`,`skill_id`,`skill_lv`) VALUES (%d, 1, %d, %d)", sd->status.char_id, itAutobuffskills.skill_id, itAutobuffskills.skill_lv)) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
	}
	//insert aa_skills - 2 - autoattackskills
	if (!sd->aa.autoattackskills.empty()) {
		for (auto& itAutoattackskills : sd->aa.autoattackskills) {
			if (SQL_ERROR == Sql_Query(mmysql_handle, "INSERT INTO `aa_skills` (`char_id`,`type`,`skill_id`,`skill_lv`) VALUES (%d, 2, %d, %d)", sd->status.char_id, itAutoattackskills.skill_id, itAutoattackskills.skill_lv)) {
				Sql_ShowDebug(mmysql_handle);
			}
		}
	}
}

void aa_load(map_session_data* sd) {
	int type;
	t_tick tick = gettick();

	// aa_common_config
	if (Sql_Query(mmysql_handle,
		"SELECT `stopmelee`,`pickup_item_config`,`prio_item_config`,`aggressive_behavior`,`autositregen_conf`,`autositregen_maxhp`,`autositregen_minhp`,`autositregen_maxsp`,`autositregen_minsp`,`tp_use_teleport`,`tp_use_flywing`,`tp_min_hp`,`tp_delay_nomobmeet`,`tp_mvp`,`tp_miniboss`,`accept_party_request`,`token_siegfried`,`return_to_savepoint`,`map_mob_selection`,`action_on_end`,`monster_surround` "
		"FROM `aa_common_config` "
		"WHERE `char_id` = %d ",
		sd->status.char_id) != SQL_SUCCESS)
	{
		Sql_ShowDebug(mmysql_handle);
		return;
	}

	if (Sql_NumRows(mmysql_handle) > 0) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data;
			Sql_GetData(mmysql_handle, 0, &data, NULL); sd->aa.stopmelee = atoi(data);
			Sql_GetData(mmysql_handle, 1, &data, NULL); sd->aa.pickup_item_config = atoi(data);
			Sql_GetData(mmysql_handle, 2, &data, NULL); sd->aa.prio_item_config = atoi(data);
			Sql_GetData(mmysql_handle, 3, &data, NULL); sd->aa.mobs.aggressive_behavior = atoi(data);
			Sql_GetData(mmysql_handle, 4, &data, NULL); sd->aa.autositregen.is_active = atoi(data);
			Sql_GetData(mmysql_handle, 5, &data, NULL); sd->aa.autositregen.max_hp = atoi(data);
			Sql_GetData(mmysql_handle, 6, &data, NULL); sd->aa.autositregen.min_hp = atoi(data);
			Sql_GetData(mmysql_handle, 7, &data, NULL); sd->aa.autositregen.max_sp = atoi(data);
			Sql_GetData(mmysql_handle, 8, &data, NULL); sd->aa.autositregen.min_sp = atoi(data);
			Sql_GetData(mmysql_handle, 9, &data, NULL); sd->aa.teleport.use_teleport = atoi(data);
			Sql_GetData(mmysql_handle, 10, &data, NULL); sd->aa.teleport.use_flywing = atoi(data);
			Sql_GetData(mmysql_handle, 11, &data, NULL); sd->aa.teleport.min_hp = atoi(data);
			Sql_GetData(mmysql_handle, 12, &data, NULL); sd->aa.teleport.delay_nomobmeet = atoi(data);
			Sql_GetData(mmysql_handle, 13, &data, NULL); sd->aa.teleport.tp_mvp = atoi(data);
			Sql_GetData(mmysql_handle, 14, &data, NULL); sd->aa.teleport.tp_miniboss = atoi(data);
			Sql_GetData(mmysql_handle, 15, &data, NULL); sd->aa.accept_party_request = atoi(data);
			Sql_GetData(mmysql_handle, 16, &data, NULL); sd->aa.token_siegfried = atoi(data);
			Sql_GetData(mmysql_handle, 17, &data, NULL); sd->aa.return_to_savepoint = atoi(data);
			Sql_GetData(mmysql_handle, 18, &data, NULL); sd->aa.mobs.map = atoi(data);
			Sql_GetData(mmysql_handle, 19, &data, NULL); sd->aa.action_on_end = atoi(data);
			Sql_GetData(mmysql_handle, 20, &data, NULL); sd->aa.monster_surround = atoi(data);
		}
	}
	else {
		sd->aa.stopmelee = 0;
		sd->aa.pickup_item_config = 0;
		sd->aa.prio_item_config = 0;
		sd->aa.mobs.aggressive_behavior = 0;
		sd->aa.autositregen.is_active = 0;
		sd->aa.autositregen.max_hp = 0;
		sd->aa.autositregen.min_hp = 0;
		sd->aa.autositregen.max_sp = 0;
		sd->aa.autositregen.min_sp = 0;
		sd->aa.teleport.use_teleport = 0;
		sd->aa.teleport.use_flywing = 0;
		sd->aa.teleport.min_hp = 0;
		sd->aa.teleport.delay_nomobmeet = 0;
		sd->aa.teleport.tp_mvp = 0;
		sd->aa.teleport.tp_miniboss = 0;
		sd->aa.accept_party_request = 1;
		sd->aa.token_siegfried = 1;
		sd->aa.return_to_savepoint = 1;
		sd->aa.mobs.map = 0;
		sd->aa.action_on_end = 0;
		sd->aa.monster_surround = 0;
		sd->aa.duration_ = 0;
		sd->aa.pickup_lock_until = 0;
		sd->aa.attack_lock_until = 0;
	}

	Sql_FreeResult(mmysql_handle);

	sd->aa.path_index = 0;
	sd->aa.duration_ = 0;
	sd->aa.pickup_lock_until = 0;
	sd->aa.attack_lock_until = 0;
	sd->aa.easy_fail_until = 0;
	sd->aa.defer_on_end = 0;

	// aa_items
	if (Sql_Query(mmysql_handle,
		"SELECT `type`,`item_id`,`min_hp`,`min_sp`,`status` "
		"FROM `aa_items` "
		"WHERE `char_id` = %d ",
		sd->status.char_id) != SQL_SUCCESS)
	{
		Sql_ShowDebug(mmysql_handle);
		return;
	}

	if (Sql_NumRows(mmysql_handle) > 0) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data;
			Sql_GetData(mmysql_handle, 0, &data, NULL); type = atoi(data);
			switch (type) {
			case 0:
				struct s_autobuffitems autobuffitems;
				autobuffitems.is_active = 1;
				Sql_GetData(mmysql_handle, 1, &data, NULL); autobuffitems.item_id = atoi(data);
				Sql_GetData(mmysql_handle, 4, &data, NULL); autobuffitems.status = atoi(data);
				sd->aa.autobuffitems.push_back(autobuffitems);
				break;
			case 1:
				struct s_autopotion autopotion;
				autopotion.is_active = 1;
				Sql_GetData(mmysql_handle, 1, &data, NULL); autopotion.item_id = atoi(data);
				Sql_GetData(mmysql_handle, 2, &data, NULL); autopotion.min_hp = atoi(data);
				Sql_GetData(mmysql_handle, 3, &data, NULL); autopotion.min_sp = atoi(data);
				sd->aa.autopotion.push_back(autopotion);
				break;
			case 2:
				t_itemid nameid;
				Sql_GetData(mmysql_handle, 1, &data, NULL); nameid = atoi(data);
				sd->aa.pickup_item_id.push_back(nameid);
				break;
			}
		}
	}
	Sql_FreeResult(mmysql_handle);

	// aa_mobs
	if (sd->aa.mobs.map == sd->mapindex) {
		if (Sql_Query(mmysql_handle,
			"SELECT `mob_id` "
			"FROM `aa_mobs` "
			"WHERE `char_id` = %d ",
			sd->status.char_id) != SQL_SUCCESS)
		{
			Sql_ShowDebug(mmysql_handle);
			return;
		}

		if (Sql_NumRows(mmysql_handle) > 0) {
			while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
				char* data;
				uint32 mob_id;
				Sql_GetData(mmysql_handle, 0, &data, NULL); mob_id = atoi(data);
				sd->aa.mobs.id.push_back(mob_id);
			}
		}
		Sql_FreeResult(mmysql_handle);
	}
	else
		sd->aa.mobs.map = sd->mapindex;

	// aa_skills
	sd->aa.skill_range = -1;
	if (Sql_Query(mmysql_handle,
		"SELECT `type`,`skill_id`,`skill_lv`,`min_hp`"
		"FROM `aa_skills` "
		"WHERE `char_id` = %d ",
		sd->status.char_id) != SQL_SUCCESS)
	{
		Sql_ShowDebug(mmysql_handle);
		return;
	}

	if (Sql_NumRows(mmysql_handle) > 0) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data;
			Sql_GetData(mmysql_handle, 0, &data, NULL); type = atoi(data);
			switch (type) {
			case 0:
				struct s_autoheal autoheal;
				autoheal.is_active = 1;
				Sql_GetData(mmysql_handle, 1, &data, NULL); autoheal.skill_id = atoi(data);
				Sql_GetData(mmysql_handle, 2, &data, NULL); autoheal.skill_lv = atoi(data);
				Sql_GetData(mmysql_handle, 3, &data, NULL); autoheal.min_hp = atoi(data);
				autoheal.last_use = 1;
				sd->aa.autoheal.push_back(autoheal);
				break;
			case 1:
				struct s_autobuffskills autobuffskills;
				autobuffskills.is_active = 1;
				Sql_GetData(mmysql_handle, 1, &data, NULL); autobuffskills.skill_id = atoi(data);
				Sql_GetData(mmysql_handle, 2, &data, NULL); autobuffskills.skill_lv = atoi(data);
				autobuffskills.last_use = 1;
				sd->aa.autobuffskills.push_back(autobuffskills);
				break;
			case 2:
				struct s_autoattackskills autoattackskills;
				autoattackskills.is_active = 1;
				Sql_GetData(mmysql_handle, 1, &data, NULL); autoattackskills.skill_id = atoi(data);
				Sql_GetData(mmysql_handle, 2, &data, NULL); autoattackskills.skill_lv = atoi(data);
				autoattackskills.last_use = 1;
				if (sd->aa.skill_range < 0)
					sd->aa.skill_range = skill_get_range2(sd, autoattackskills.skill_id, autoattackskills.skill_lv, true);
				else
					sd->aa.skill_range = max(skill_get_range2(sd, autoattackskills.skill_id, autoattackskills.skill_lv, true), sd->aa.skill_range);
				sd->aa.autoattackskills.push_back(autoattackskills);
				break;
			}
		}
	}
	Sql_FreeResult(mmysql_handle);

	aa_changestate_autoattack(sd, 0);
}

static inline bool aa_too_far(int sx, int sy, int tx, int ty, int max_dist) {
	int dx = std::abs(tx - sx);
	int dy = std::abs(ty - sy);
	return (dx + dy) > max_dist;
}

static inline int aa_pending_find_slot(map_session_data* sd, unsigned int tid, t_tick now) {
	int free_idx = -1;
	for (int i = 0; i < AA_PENDING_SLOTS; ++i) {
		if (sd->aa.pending[i].target_id == tid)
			return i;
		if (sd->aa.pending[i].target_id == 0 || sd->aa.pending[i].until <= now)
			free_idx = i; // on garde le dernier libre/expire vu
	}
	return (free_idx >= 0) ? free_idx : 0; // si plein, on ecrase le slot 0
}

static inline int64 aa_pending_get(map_session_data* sd, unsigned int tid, t_tick now) {
	for (int i = 0; i < AA_PENDING_SLOTS; ++i) {
		struct aa_pending_entry* e = &sd->aa.pending[i];
		if (e->target_id == tid && DIFF_TICK(e->until, now) > 0)
			return e->damage;
	}
	return 0;
}

static int aa_pending_clear_cb(struct block_list* bl, va_list ap)
{
	map_session_data* sd = BL_CAST(BL_PC, bl);
	uint32 tid = va_arg(ap, uint32);

	if (!sd || !tid) return 0;

	for (int i = 0; i < AA_PENDING_SLOTS; ++i) {
		if (sd->aa.pending[i].target_id == tid) {
			sd->aa.pending[i].target_id = 0;
			sd->aa.pending[i].damage = 0;
			sd->aa.pending[i].until = 0;
		}
	}
	return 0;
}

void aa_pending_flush_map_for_target(struct mob_data* md)
{
	if (!md) return;
	map_foreachinmap(aa_pending_clear_cb, md->m, BL_PC, md->id);
}

void aa_pending_add(struct block_list* src, unsigned int tid, int64 dmg, t_tick until)
{
	map_session_data* sd = BL_CAST(BL_PC, src);
	if (!sd || !tid || dmg <= 0) return;

	const t_tick now = gettick();

	// Clamp du TTL: n’allonge jamais au-dela de now + AA_PEND_TTL_MS
	const t_tick hard_max = now + AA_PEND_TTL_MS;
	if (DIFF_TICK(until, hard_max) > 0)
		until = hard_max;

	// 1) Cherche un slot valide pour la meme cible OU un slot libre
	int same_i = -1, free_i = -1;

	for (int i = 0; i < AA_PENDING_SLOTS; ++i) {
		struct aa_pending_entry* e = &sd->aa.pending[i];

		const bool valid = (e->target_id != 0 && DIFF_TICK(e->until, now) > 0);
		if (valid && e->target_id == tid) {
			same_i = i;
			break;
		}
		if (!valid && free_i < 0)
			free_i = i;
	}

	int idx = (same_i >= 0) ? same_i : (free_i >= 0 ? free_i : 0);

	// 2) Merge (SUM capee) ou MAX cape
	if (same_i >= 0) {
		int64 sum = sd->aa.pending[idx].damage + dmg;
		if (sum > AA_PEND_CAP) sum = AA_PEND_CAP;
		if (sum < 0) sum = 0;
		sd->aa.pending[idx].damage = sum;

		// etend l’echeance a la plus tardive (utile multi-hit en rafale)
		if (DIFF_TICK(until, sd->aa.pending[idx].until) > 0)
			sd->aa.pending[idx].until = until;
	}
	else {
		// 3) Initialise un slot (overwrites si tout plein)
		sd->aa.pending[idx].target_id = tid;
		sd->aa.pending[idx].damage = dmg > AA_PEND_CAP ? AA_PEND_CAP : dmg;
		sd->aa.pending[idx].until = until;
	}
}


void aa_pending_consume(map_session_data* sd, unsigned int tid, int64 dmg, t_tick now) {
	for (int i = 0; i < AA_PENDING_SLOTS; ++i) {
		if (sd->aa.pending[i].target_id == tid) {
			sd->aa.pending[i].damage -= dmg;
			if (sd->aa.pending[i].damage <= 0 || sd->aa.pending[i].until <= now) {
				sd->aa.pending[i].target_id = 0;
				sd->aa.pending[i].damage = 0;
				sd->aa.pending[i].until = 0;
			}
			return;
		}
	}
}

void aa_skill_range_calc(map_session_data* sd) {
	auto& skills = sd->aa.autoattackskills;

	if (skills.empty()) {
		sd->aa.skill_range = -1;
		return;
	}

	auto best_skill = std::min_element(skills.begin(), skills.end(),
		[&sd](const s_autoattackskills& a, const s_autoattackskills& b) {
			return skill_get_range2(sd, a.skill_id, a.skill_lv, true) <
				skill_get_range2(sd, b.skill_id, b.skill_lv, true);
		});

	sd->aa.skill_range = skill_get_range2(sd, best_skill->skill_id, best_skill->skill_lv, true);
}

void aa_mob_ai_search_mvpcheck(struct block_list* bl, struct mob_data* md) {
	TBL_PC* sd = NULL;

	if (!battle_config.feature_autoattack_teleport_mvp)
		return;

	if (bl->type == BL_PC) {
		sd = BL_CAST(BL_PC, bl);
		if (sd && sd->state.autoattack) {
			e_mob_bosstype bosstype = md->get_bosstype();

			//if (sd->aa.teleport.tp_mvp && bosstype == BOSSTYPE_MVP)
			if (bosstype == BOSSTYPE_MVP)
				aa_teleport(sd);
			//else if (sd->aa.teleport.tp_miniboss && bosstype == BOSSTYPE_MINIBOSS)
			else if (bosstype == BOSSTYPE_MINIBOSS) {
				static const std::unordered_set<std::string> AA_MAPNAME_MINIBOSS = { "thor_v03" }; // Maps to exclude from teleport
				std::string current_map = map_mapid2mapname(sd->m); // Convert char* to std::string

				if (AA_MAPNAME_MINIBOSS.find(current_map) == AA_MAPNAME_MINIBOSS.end()) {
					aa_teleport(sd);
				}
			}
		}
	}
}

void aa_priority_on_hit(map_session_data* sd, struct block_list* src) {
	struct block_list* target = nullptr;
	struct status_data* status = status_get_status_data(*sd);
	int target_distance = 0, src_distance = 0;

	if (sd->state.autoattack) {

		// Teleport condition if hp is bellow limit
		if ((!sd->aa.teleport.use_teleport || !sd->aa.teleport.use_flywing) // player myst allow teleport or flywing
			&& sd->aa.teleport.min_hp // player must have set min hp value to tp
			&& (status->hp * 100 / sd->aa.teleport.min_hp) < sd->status.max_hp) { // player hp is bellow min hp

			if (aa_teleport(sd))
				return;
		}

		// Change if no target and priority to defend player and attacker is a mob
		if (!sd->aa.mobs.aggressive_behavior // 0 attack
			&& src->type == BL_MOB) {
			if (!sd->aa.target_id) {

				// if player have an item to pick, remove it
				if (sd->aa.itempick_id)
					aa_item_change(sd, 0);

				aa_target_change(sd, src->id);
			}
			else if (sd->aa.target_id) { // priority to the mob who hit if closest
				target = map_id2bl(sd->aa.target_id);
				if (target != nullptr) {
					target_distance = distance(sd->x - target->x, sd->y - target->y);
					src_distance = distance(sd->x - src->x, sd->y - src->y);

					if (src_distance < target_distance)
						aa_target_change(sd, src->id);
				}
			}
		}

		sd->aa.last_hit = gettick();
	}
}

void aa_item_change(map_session_data* sd, int id) {
	if (sd->aa.prio_item_config && id == 0 && sd->aa.itempick_id != id) // loot first
		sd->aa.pickup_lock_until = gettick() + battle_config.feature_autoattack_pickup_delay;

	sd->aa.itempick_id = id;
}

void aa_target_change(map_session_data* sd, int id) {
	struct unit_data* ud = unit_bl2ud(sd);
	if (ud)
		ud->target = id;

	if (!sd->aa.prio_item_config && id == 0 && sd->aa.target_id != id) // attack first
		sd->aa.attack_lock_until = gettick() + battle_config.feature_autoattack_timer;

	sd->aa.target_id = id;

	if (id > 0) {
		struct mob_data* md_target = (struct mob_data*)map_id2bl(sd->aa.target_id);
		if (md_target) {
			switch (sd->status.weapon) {
			case W_BOW:
			case W_WHIP:
			case W_MUSICAL:
				aa_arrowchange(sd, md_target);
				break;
			case W_REVOLVER:
			case W_RIFLE:
			case W_GATLING:
			case W_SHOTGUN:
			case W_GRENADE:
				aa_bulletchange(sd, md_target);
				break;
			}
		}
	}
}

void aa_reset_ondead(map_session_data* sd) {
}

bool aa_canuseskill(map_session_data* sd, uint16 skill_id, uint16 skill_lv) {
	// Ensure the session data is valid
	if (sd == nullptr)
		return false;

	//Special fix for EDP
	if (skill_id == ASC_EDP && sd->sc.getSCE(SC_EDP))
		return false;

	map_data* mapdata = map_getmapdata(sd->m);
	if (mapdata->getMapFlag(MF_NOSKILL))
		return false;

	const int inf = skill_get_inf(skill_id); // Skill information flags
	const t_tick tick = gettick();          // Current tick time

	// Check if the player has the required skill level
	if (pc_checkskill(sd, skill_id) < skill_lv)
		return false;

	// Check if the player has enough SP to use the skill
	if (skill_get_sp(skill_id, skill_lv) > sd->battle_status.sp)
		return false;

	// Reset idle time if applicable
	if (battle_config.idletime_option & IDLE_USESKILLTOID)
		sd->idletime = time(NULL);

	// Check for conditions that prevent skill usage
	if ((pc_cant_act2(sd) || sd->chatID) &&
		skill_id != RK_REFRESH &&
		!(skill_id == SR_GENTLETOUCH_CURE && (sd->sc.opt1 == OPT1_STONE || sd->sc.opt1 == OPT1_FREEZE || sd->sc.opt1 == OPT1_STUN)) &&
		sd->state.storage_flag &&
		!(inf & INF_SELF_SKILL)) {
		return false;
	}

	// Cannot use skills while sitting
	if (pc_issit(sd))
		return false;

	// Check if the skill is restricted for the player
	if (skill_isNotOk(skill_id, *sd))
		return false;

	// Verify skill conditions at the beginning and end of the cast
	// Special fix for EDP
	if (skill_id != ASC_EDP) {
		if (!skill_check_condition_castbegin(*sd, skill_id, skill_lv) ||
			!skill_check_condition_castend(*sd, skill_id, skill_lv)) {
			return false;
		}
	}

	// Ensure no active skill timer unless it's a valid exception
	if (sd->ud.skilltimer != INVALID_TIMER) {
		if (skill_id != SA_CASTCANCEL && skill_id != SO_SPELLFIST)
			return false;
	}
	else if (DIFF_TICK(tick, sd->ud.canact_tick) < 0) {
		if (sd->skillitem != skill_id)
			return false;
	}

	// Costume option disables skill usage
	if (sd->sc.option & OPTION_COSTUME)
		return false;

	// Basilica restrictions
	if (sd->sc.getSCE(SC_BASILICA) &&
		(skill_id != HP_BASILICA || sd->sc.getSCE(SC_BASILICA)->val4 != sd->id)) {
		return false; // Only the caster can stop Basilica
	}

	// Check if a skill menu is open
	if (sd->menuskill_id) {
		if (sd->menuskill_id == SA_TAMINGMONSTER) {
			clif_menuskill_clear(sd); // Cancel pet capture
		}
		else if (sd->menuskill_id != SA_AUTOSPELL) {
			return false; // Cannot use skills while a menu is open
		}
	}

	// Ensure the skill level does not exceed the player's known level
	skill_lv = min(pc_checkskill(sd, skill_id), skill_lv);

	// Remove invincibility timer if applicable
	pc_delinvincibletimer(sd);

	return true;
}

// --- Validateurs "bridge" vers tes verifications actuelles ---
static bool aa_validate_mob_bl(map_session_data* sd, struct block_list* bl) {
	// on garde **exactement** tes regles via aa_check_target(sd, id)
	return (bl && bl->type == BL_MOB) ? aa_check_target(sd, bl->id) : false;
}

static bool aa_validate_item_bl(map_session_data* sd, struct block_list* bl) {
	// idem, on delegue a ta logique existante
	return (bl && bl->type == BL_ITEM) ? aa_check_item_pickup(sd, bl) : false;
}

// --- API unifiee pour ton code ---
static unsigned int aa_find_first_mob_centripetal(map_session_data* sd, int max_radius, bool wall_check) {
	return aa_find_first_bl_centripetal(sd, max_radius, wall_check, BL_MOB, AA_BUCKET_MOB, &aa_validate_mob_bl);
}

static unsigned int aa_find_first_item_centripetal(map_session_data* sd, int max_radius, bool wall_check) {
	return aa_find_first_bl_centripetal(sd, max_radius, wall_check, BL_ITEM, AA_BUCKET_GENERIC, &aa_validate_item_bl);
}

//sub routine to search item by item on floor
int buildin_autopick_sub(struct block_list* bl, va_list ap) {
	// Extract arguments from va_list
	int* itempick_id = va_arg(ap, int*);
	int src_id = va_arg(ap, int);

	if (*itempick_id > 0)
		return 0;

	if (!bl || bl->type != BL_ITEM)
		return 0;

	// Retrieve sd from player
	map_session_data* sd = map_id2sd(src_id);
	if (!sd || !bl) // Validate both source and target block lists
		return 0;

	// If itempick_id is already set, skip processing
	if (sd->aa.itempick_id != 0)
		return 0;

	// Check item pickup conditions and update itempick_id if valid
	if (!aa_check_item_pickup(sd, bl))
		return 0;

	*itempick_id = bl->id;
	return 1;
}

// ---------- Noyau : recherche centripete generique ----------
static unsigned int aa_find_first_bl_centripetal(
	map_session_data* sd,
	int max_radius,
	bool wall_check,
	int32 required_type,            // ex: BL_MOB ou BL_ITEM
	AABucketKind bucket_kind,       // bucket a parcourir
	AAValidateBlFn validate_bl      // ex: wrapper vers aa_check_target / aa_check_item_pickup
) {
	if (!sd || !validate_bl) return 0;

#define BLOCK_SIZE 8
	const int16 m = sd->m;
	const int16 cx = sd->x;
	const int16 cy = sd->y;

	struct map_data* md = map_getmapdata(m);
	if (!md) return 0;

	// Choix du bucket
	struct block_list** buckets = nullptr;
	if (bucket_kind == AA_BUCKET_MOB) {
		if (!md->block_mob) return 0;
		buckets = md->block_mob;
	}
	else {
		if (!md->block) return 0;
		buckets = md->block;
	}

	const int16 xmin = 0, ymin = 0;
	const int16 xmax = (int16)(md->xs - 1);
	const int16 ymax = (int16)(md->ys - 1);

	if (max_radius < 0) max_radius = 0;
	const int16 rmax = (int16)cap_value(
		max_radius, 0,
		max(cx, md->xs - 1 - cx) + max(cy, md->ys - 1 - cy)
	);

	// Helper local : itere les BL d'une cellule (x,y)
	auto scan_cell = [&](int16 x, int16 y) -> unsigned int {
		const int bx = x / BLOCK_SIZE;
		const int by = y / BLOCK_SIZE;
		for (struct block_list* bl = buckets[bx + by * md->bxs]; bl; bl = bl->next) {
			if (required_type && bl->type != required_type) continue;
			if (bl->x != x || bl->y != y) continue; // stricte centripete par cellule

			// Verif LOS murale optionnelle (meme logique que map_foreachinareaV)
			if (wall_check && !path_search_long(nullptr, m, cx, cy, bl->x, bl->y, CELL_CHKWALL))
				continue;

			// IMPORTANT : on delegue **toutes** les regles au validateur existant
			if (validate_bl(sd, bl)) {
				return bl->id; // premier match
			}
		}
		return 0u;
		};

	map_freeblock_lock();

	for (int16 r = 0; r <= rmax; ++r) {
		// r == 0 : cellule centrale
		if (r == 0) {
			if (cx >= xmin && cx <= xmax && cy >= ymin && cy <= ymax) {
				unsigned int found = scan_cell(cx, cy);
				if (found) { map_freeblock_unlock(); return found; }
			}
			continue;
		}

		// Perimetre de l anneau r
		const int16 x0 = (int16)cap_value(cx - r, xmin, xmax);
		const int16 y0 = (int16)cap_value(cy - r, ymin, ymax);
		const int16 x1 = (int16)cap_value(cx + r, xmin, xmax);
		const int16 y1 = (int16)cap_value(cy + r, ymin, ymax);

		// Bord haut (y=y0), x: x0->x1
		for (int16 x = x0; x <= x1; ++x) {
			unsigned int found = scan_cell(x, y0);
			if (found) { map_freeblock_unlock(); return found; }
		}
		// Bord droit (x=x1), y: y0+1 -> y1-1
		if (x1 >= x0 && y1 - y0 >= 2) {
			for (int16 y = y0 + 1; y <= y1 - 1; ++y) {
				unsigned int found = scan_cell(x1, y);
				if (found) { map_freeblock_unlock(); return found; }
			}
		}
		// Bord bas (y=y1), x: x1->x0
		if (y1 != y0) {
			for (int16 x = x1; x >= x0; --x) {
				unsigned int found = scan_cell(x, y1);
				if (found) { map_freeblock_unlock(); return found; }
			}
		}
		// Bord gauche (x=x0), y: y1-1 -> y0+1
		if (x1 != x0 && y1 - y0 >= 2) {
			for (int16 y = y1 - 1; y >= y0 + 1; --y) {
				unsigned int found = scan_cell(x0, y);
				if (found) { map_freeblock_unlock(); return found; }
			}
		}
	}

	map_freeblock_unlock();
	return 0;
}

bool can_pick_item(const map_session_data* sd, const block_list* bl, const flooritem_data* fitem) {
	// 1) Distance d'abord (cheap) : si trop loin, inutile d'aller plus loin
	if (distance_xy(sd->x, sd->y, bl->x, bl->y) >= 11)
		return false;

	// 2) Filtre d'items si active : si l'item n'est pas whitelisté, stop
	if (sd->aa.pickup_item_config == 1 && !sd->aa.pickup_item_id.empty()) {
		const auto& v = sd->aa.pickup_item_id;
		if (std::find(v.begin(), v.end(), fitem->item.nameid) == v.end())
			return false;
	}

	// 3) Seulement maintenant on paye l'A* (cher)
	return path_search(nullptr, sd->m, sd->x, sd->y, bl->x, bl->y, 1, CELL_CHKNOREACH);
}

//Check if an item can be pick up around
bool aa_check_item_pickup(map_session_data* sd, struct block_list* bl) {
	struct flooritem_data* fitem = (struct flooritem_data*)bl;

	if (!bl)
		return false;

	if (sd->aa.pickup_item_config == 3 && itemdb_type(fitem->item.nameid) != IT_CARD)
		return false;

	struct party_data* p = (sd->status.party_id) ? party_search(sd->status.party_id) : nullptr;
	t_tick tick = gettick();

	// Validate ownership and party conditions
	if (fitem->first_get_charid > 0 && fitem->first_get_charid != sd->status.char_id) {
		map_session_data* first_sd = map_charid2sd(fitem->first_get_charid);
		if (DIFF_TICK(tick, fitem->first_get_tick) < 0) {
			if (!(p && p->party.item & 1 &&
				first_sd && first_sd->status.party_id == sd->status.party_id
				))
				return false;
		}
		else if (fitem->second_get_charid > 0 && fitem->second_get_charid != sd->status.char_id) {
			map_session_data* second_sd = map_charid2sd(fitem->second_get_charid);
			if (DIFF_TICK(tick, fitem->second_get_tick) < 0) {
				if (!(p && p->party.item & 1 &&
					((first_sd && first_sd->status.party_id == sd->status.party_id) ||
						(second_sd && second_sd->status.party_id == sd->status.party_id))
					))
					return false;
			}
			else if (fitem->third_get_charid > 0 && fitem->third_get_charid != sd->status.char_id) {
				map_session_data* third_sd = map_charid2sd(fitem->third_get_charid);
				if (DIFF_TICK(tick, fitem->third_get_tick) < 0) {
					if (!(p && p->party.item & 1 &&
						((first_sd && first_sd->status.party_id == sd->status.party_id) ||
							(second_sd && second_sd->status.party_id == sd->status.party_id) ||
							(third_sd && third_sd->status.party_id == sd->status.party_id))
						))
						return false;
				}
			}
		}
	}

	// Check custom item pickup configuration
	if (can_pick_item(sd, bl, fitem) && fc_item_is_available_for_bot(fitem, sd, tick))
		return true;

	return false;
}

unsigned int aa_check_item_pickup_onfloor(map_session_data* sd) {
	// Verifie si l'item en cours est toujours ok
	struct block_list* bl = map_id2bl(sd->aa.itempick_id);

	if (!bl || bl->type != BL_ITEM || !aa_check_item_pickup(sd, bl)) {
		// invalide -> reset et chercher un nouveau
		aa_item_change(sd, 0);

		unsigned int found = aa_find_first_item_centripetal(
			sd,
			battle_config.feature_autoattack_pdetection,   // rayon max
			battle_config.skill_wall_check > 0             // LOS optionnelle
		);

		if (found) {
			aa_item_change(sd, found);
		}
	}

	return sd->aa.itempick_id;
}

bool aa_check_target(map_session_data* sd, unsigned int id) {
	if (id == 0)
		return false;

	struct block_list* bl = map_id2bl(id); // Retrieve the block list for the target ID
	if (!bl) {
		sd->aa.mob_dead_delay = gettick() + battle_config.feature_autoattack_mobdead_delay;
		return false;
	}

	// Check kill-steal protection
	if (battle_config.ksprotection && mob_ksprotected(sd, bl))
		return false;

	//target dead
	if (status_isdead(*bl))
		return false;

	//not enemy
	if (battle_check_target(sd, bl, BCT_ENEMY) <= 0 || !status_check_skilluse(sd, bl, 0, 0))
		return false;

	//can't attack
	if (!unit_can_attack(sd, bl->id))
		return false;

	t_tick now = gettick();
	unsigned tid = bl ? bl->id : 0;
	int64 pend = (tid ? aa_pending_get(sd, tid, now) : 0);

	if (pend > 0) {
		status_data* md_status = status_get_status_data(*bl);
		int64 hp = (int64)md_status->hp;
		if (pend >= hp || hp <= 0) {
			sd->aa.mob_dead_delay = gettick() + battle_config.feature_autoattack_mobdead_delay;
			return false;
		}
	}

	status_data* sstatus = status_get_status_data(*sd);
	int32 range = (sd->aa.stopmelee == 0 || (sd->aa.stopmelee == 2 && sstatus->sp < 100))
		? (sd->aa.skill_range >= 0 ? min(sstatus->rhw.range, sd->aa.skill_range) : sstatus->rhw.range)
		: (sd->aa.skill_range >= 0 ? sd->aa.skill_range : 1);

	// Verification du chemin et de la distance
	if (!path_search(nullptr, sd->m, sd->x, sd->y, bl->x, bl->y, 0, CELL_CHKNOPASS))
		return false;

	// Verification du chemin et de la distance
	if (distance_bl(sd, bl) > battle_config.feature_autoattack_mdetection)
		return false;

	// Verification de l'etat de la cible
	TBL_MOB* md = map_id2md(bl->id);
	if (!md || md->special_state.ai)
		return false;

	if (md->status.hp <= 0) {
		sd->aa.mob_dead_delay = gettick() + battle_config.feature_autoattack_mobdead_delay;
		return false;
	}

	// Check for hidden or cloaked state
	if (md->sc.option & (OPTION_HIDE | OPTION_CLOAK))
		return false;

	if (!sd->aa.mobs.id.empty() &&
		std::find(sd->aa.mobs.id.begin(), sd->aa.mobs.id.end(), md->mob_id) != sd->aa.mobs.id.end()) {
		if (sd->aa.mobs.aggressive_behavior || (!sd->aa.mobs.aggressive_behavior && md->target_id != sd->id)) // check if aggressive and target the bot
			return false;
	}

	if (!battle_check_range(sd, bl, battle_config.feature_autoattack_mdetection))
		return false;

	if (!fc_target_mob_is_available_for_bot(md, sd, now))
		return false;

	// Default to valid target if all checks pass
	return true;
}

int buildin_autoattack_sub(struct block_list* bl, va_list ap) {
	// Retrieve arguments passed via the va_list
	int* target_id = va_arg(ap, int*);
	int src_id = va_arg(ap, int);

	if (*target_id > 0) {
		return 0;
	}

	if (!bl || bl->type != BL_MOB)
		return 0;

	// Retrieve sd
	map_session_data* sd = map_id2sd(src_id);

	// Validate source and target blocks
	if (!sd)
		return 0;

	// Verify target eligibility
	if (!aa_check_target(sd, bl->id)) {
		return 0;
	}

	*target_id = bl->id;
	return 1;
}

int buildin_autoattack_monsters_sub(struct block_list* bl, va_list ap) {
	// Retrieve arguments passed via the va_list
	std::unordered_set<int>* counted_monsters = va_arg(ap, std::unordered_set<int>*);
	int src_id = va_arg(ap, int);

	if (!bl || bl->type != BL_MOB)
		return 0;

	// Retrieve sd
	map_session_data* sd = map_id2sd(src_id);
	TBL_MOB* md = map_id2md(bl->id);

	// Validate source and target blocks
	if (!sd || !md)
		return 0;

	//md->target_id=bl->id;
	if (sd->id == md->target_id && counted_monsters->find(md->id) == counted_monsters->end())
		counted_monsters->insert(md->id);  // Ajouter l'ID du monstre dans le set

	return 1;
}

unsigned int aa_check_target_alive(map_session_data* sd) {
	// Validate current target
	if (!aa_check_target(sd, sd->aa.target_id)) {
		if (sd->aa.mobs.map != sd->mapindex) {
			sd->aa.mobs.map = sd->mapindex;
			sd->aa.mobs.id.clear();
		}

		aa_target_change(sd, 0);

		// Recherche centripete : premier mob valide
		unsigned int found = aa_find_first_mob_centripetal(
			sd,
			battle_config.feature_autoattack_mdetection, /* rayon max */
			battle_config.skill_wall_check > 0           /* wall_check optionnel */
		);

		if (found) {
			aa_target_change(sd, found);
		}
	}
	return sd->aa.target_id;
}

int aa_check_surround_monster(map_session_data* sd) {
	std::unordered_set<int> counted_monsters;  // Set pour stocker les ID des monstres deja comptes

	aa_target_change(sd, 0);

	// Search for a new target within detection radius
	for (int radius = 0; radius < battle_config.feature_autoattack_mdetection; ++radius) {
		map_foreachinarea(
			buildin_autoattack_monsters_sub,
			sd->m,
			sd->x - radius,
			sd->y - radius,
			sd->x + radius,
			sd->y + radius,
			BL_MOB,
			&counted_monsters,  // Passer le set en parametre
			sd->id
		);
	}

	return static_cast<int>(counted_monsters.size());
}

bool aa_teleport(map_session_data* sd) {
	// Early exit if teleportation is disabled globally or via configuration
	if (!sd->state.autoattack || !battle_config.feature_autoattack_teleport)
		return false;

	if (sd->state.changemap || sd->state.rewarp || sd->state.debug_remove_map)
		return false;

	map_data* mapdata = map_getmapdata(sd->m);
	if (mapdata->getMapFlag(MF_NOTELEPORT))
		return false;

	if (sd->sc.getSCE(SC_STONE) || sd->sc.getSCE(SC_FREEZE) || sd->sc.getSCE(SC_STUN) || sd->sc.getSCE(SC_SLEEP) || sd->sc.getSCE(SC_HIDING) ||
		sd->sc.getSCE(SC_TRICKDEAD) || sd->sc.getSCE(SC_NOCHAT) || sd->sc.getSCE(SC_BERSERK) || sd->sc.getSCE(SC_GRAVITATION) ||
		sd->sc.getSCE(SC_CRYSTALIZE) || sd->sc.getSCE(SC_DEEPSLEEP)) {
		return false;
	}

	bool flywing_used = false;
	struct status_data* status = status_get_status_data(*sd);

	// Check if teleport skill can be used
	if (!sd->aa.teleport.use_teleport && status->sp > 20 && !mapdata->getMapFlag(MF_NOSKILL)) {
		if (pc_checkskill(sd, AL_TELEPORT) > 0) {
			skill_consume_requirement(sd, AL_TELEPORT, 1, 2);
			pc_randomwarp(sd, CLR_TELEPORT);
			status_heal(sd, 0, -skill_get_sp(AL_TELEPORT, 1), 1);
			flywing_used = true;
		}
	}

	// Check for Fly Wing usage if teleport was not used
	if (!flywing_used && !sd->aa.teleport.use_flywing) {
		static const int flywing_item_ids[] = { 12887, 12323, 601 }; // Prioritized Fly Wing item IDs
		int inventory_index = -1;
		bool requires_consumption = false;

		for (int item_id : flywing_item_ids) {
			inventory_index = pc_search_inventory(sd, item_id);
			if (inventory_index >= 0) {
				if (item_id == 601) {
					requires_consumption = true; // Fly Wing (601) requires consumption
				}
				break;
			}
		}

		if (inventory_index >= 0) {
			if (requires_consumption) {
				pc_delitem(sd, inventory_index, 1, 0, 0, LOG_TYPE_OTHER);
			}
			pc_randomwarp(sd, CLR_TELEPORT);
			flywing_used = true;
		}
	}

	// Finalize teleportation actions
	if (flywing_used) {
		// Reset value
		sd->aa.target_id = 0;
		aa_target_change(sd, 0);
		sd->aa.last_teleport = gettick();
		sd->aa.last_attack = gettick();
		sd->aa.last_move = gettick();
		sd->aa.last_hit = gettick();
		sd->aa.lastposition.x = sd->x;
		sd->aa.lastposition.y = sd->y;

		// Action after tp
		aa_status_checkmapchange(sd);
		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);
	}

	return flywing_used;
}

int aa_ammochange(map_session_data* sd, struct mob_data* md,
	const unsigned short* ammoIds,     // Liste des ID des munitions
	const unsigned short* ammoElements, // elements associes
	const unsigned short* ammoAtk,     // Puissances d'attaque des munitions
	size_t ammoCount,                  // Nombre de types de munitions
	int rqAmount = 0,                  // Quantite requise (0 si non applicable)
	const unsigned short* ammoLevels = nullptr // Niveaux minimum requis (facultatif)
) {
	if (DIFF_TICK(sd->canequip_tick, gettick()) > 0)
		return 0; // Cooldown

	int bestIndex = -1;
	int bestPriority = -1;
	int bestElement = -1;
	bool isEquipped = false;

	for (size_t i = 0; i < ammoCount; ++i) {
		int16 index = pc_search_inventory(sd, ammoIds[i]);
		if (index < 0) continue; // Munition non trouvee

		// Check qty (only for kunai atm)
		if (rqAmount > 0 && sd->inventory.u.items_inventory[index].amount < rqAmount)
			continue;

		// Check required level
		if (ammoLevels && sd->status.base_level < ammoLevels[i])
			continue;

		int priority = ammoAtk[i];
		if (aa_elemstrong(md, ammoElements[i]))
			priority += 500; // Bonus for the strong elem

		if (aa_elemallowed(md, ammoElements[i]) && priority > bestPriority) {
			bestPriority = priority;
			bestIndex = index;
			isEquipped = pc_checkequip2(sd, ammoIds[i], EQI_AMMO, EQI_AMMO + 1);
			bestElement = ammoElements[i];
		}
	}

	if (bestIndex > -1) {
		if (!isEquipped)
			pc_equipitem(sd, bestIndex, EQP_AMMO);
		return bestElement; // return the best elem
	}

	clif_displaymessage(sd->fd, "No suitable ammunition left!");
	return -1;
}

template <typename T, std::size_t N>
constexpr std::size_t array_size(T(&)[N]) noexcept {
	return N;
}

void aa_arrowchange(map_session_data* sd, struct mob_data* md) {
	constexpr unsigned short arrows[] = {
		1750, 1751, 1752, 1753, 1754, 1755, 1756, 1757, 1762, 1765, 1766, 1767, 1770, 1772, 1773, 1774
	};
	constexpr unsigned short arrowElements[] = {
		ELE_NEUTRAL, ELE_HOLY, ELE_FIRE, ELE_NEUTRAL, ELE_WATER, ELE_WIND, ELE_EARTH, ELE_GHOST,
		ELE_NEUTRAL, ELE_POISON, ELE_HOLY, ELE_DARK, ELE_NEUTRAL, ELE_HOLY, ELE_NEUTRAL, ELE_NEUTRAL
	};
	constexpr unsigned short arrowAtk[] = {
		25, 30, 30, 40, 30, 30, 30, 30, 30, 50, 50, 30, 30, 50, 45, 35
	};

	aa_ammochange(sd, md, arrows, arrowElements, arrowAtk, array_size(arrows));
}

void aa_bulletchange(map_session_data* sd, mob_data* md) {
	constexpr unsigned short bullets[] = {
		13200, 13201, 13215, 13216, 13217, 13218, 13219, 13220, 13221, 13228, 13229, 13230, 13231, 13232
	};
	constexpr unsigned short bulletElements[] = {
		ELE_NEUTRAL, ELE_HOLY, ELE_NEUTRAL, ELE_FIRE, ELE_WATER, ELE_WIND, ELE_EARTH, ELE_HOLY,
		ELE_HOLY, ELE_FIRE, ELE_WIND, ELE_WATER, ELE_POISON, ELE_DARK
	};
	constexpr unsigned short bulletAtk[] = {
		25, 15, 50, 40, 40, 40, 40, 40, 15, 20, 20, 20, 20, 20
	};
	constexpr unsigned short bulletLevels[] = {
		1, 1, 100, 100, 100, 100, 100, 100, 1, 1, 1, 1, 1, 1
	};

	aa_ammochange(sd, md, bullets, bulletElements, bulletAtk, array_size(bullets), 0, bulletLevels);
}

void aa_kunaichange(map_session_data* sd, struct mob_data* md, int rqamount) {
	constexpr unsigned short kunaiIds[] = {
		13255, 13256, 13257, 13258, 13259, 13294
	};
	constexpr unsigned short kunaiElements[] = {
		ELE_WATER, ELE_EARTH, ELE_WIND, ELE_FIRE, ELE_POISON, ELE_NEUTRAL
	};
	constexpr unsigned short kunaiAtk[] = {
		30, 30, 30, 30, 30, 50
	};

	aa_ammochange(sd, md, kunaiIds, kunaiElements, kunaiAtk, array_size(kunaiIds), rqamount);
}

void aa_cannonballchange(map_session_data* sd, struct mob_data* md) {
	constexpr unsigned short cannonballIds[] = {
		18000, 18001, 18002, 18003, 18004
	};
	constexpr unsigned short cannonballElements[] = {
		ELE_NEUTRAL, ELE_HOLY, ELE_DARK, ELE_GHOST, ELE_NEUTRAL
	};
	constexpr unsigned short cannonballAtk[] = {
		100, 120, 120, 120, 250
	};

	aa_ammochange(sd, md, cannonballIds, cannonballElements, cannonballAtk, array_size(cannonballIds));
}

// Determines if an element is strong against the target mob's defense element
bool aa_elemstrong(const mob_data* md, int ele) {
	if (md == nullptr)
		return false;

	const int def_ele = md->status.def_ele;
	const int ele_lv = md->status.ele_lv;

	// Define rules for each element
	switch (ele) {
	case ELE_GHOST:
		return (def_ele == ELE_UNDEAD && ele_lv >= 2) || (def_ele == ELE_GHOST);

	case ELE_FIRE:
		return def_ele == ELE_UNDEAD || def_ele == ELE_EARTH;

	case ELE_WATER:
		return (def_ele == ELE_UNDEAD && ele_lv >= 3) || (def_ele == ELE_FIRE);

	case ELE_WIND:
		return def_ele == ELE_WATER;

	case ELE_EARTH:
		return def_ele == ELE_WIND;

	case ELE_HOLY:
		return (def_ele == ELE_POISON && ele_lv >= 3) ||
			(def_ele == ELE_DARK) ||
			(def_ele == ELE_UNDEAD);

	case ELE_DARK:
		return def_ele == ELE_HOLY;

	case ELE_POISON:
		return (def_ele == ELE_UNDEAD && ele_lv >= 2) ||
			(def_ele == ELE_GHOST) ||
			(def_ele == ELE_NEUTRAL);

	case ELE_UNDEAD:
		return (def_ele == ELE_HOLY && ele_lv >= 2);

	case ELE_NEUTRAL:
		return false;

	default:
		return false;
	}
}



// Determines if an element is allowed against the target mob's defense element
bool aa_elemallowed(struct mob_data* md, int ele) {
	if (md == nullptr)
		return true; // Default to allowed if the mob data is invalid

	const int def_ele = md->status.def_ele;
	const int ele_lv = md->status.ele_lv;

	// Check for White Imprison status, applicable to most elements
	if (md->sc.getSCE(SC_WHITEIMPRISON)) {
		if (ele != ELE_GHOST) // Exception for Ghost element
			return false;
	}

	switch (ele) {
	case ELE_GHOST:
		return !((def_ele == ELE_NEUTRAL && ele_lv >= 2) ||
			(def_ele == ELE_FIRE && ele_lv >= 3) ||
			(def_ele == ELE_WATER && ele_lv >= 3) ||
			(def_ele == ELE_WIND && ele_lv >= 3) ||
			(def_ele == ELE_EARTH && ele_lv >= 3) ||
			(def_ele == ELE_POISON && ele_lv >= 3) ||
			(def_ele == ELE_HOLY && ele_lv >= 2) ||
			(def_ele == ELE_DARK && ele_lv >= 2));

	case ELE_FIRE:
	case ELE_WATER:
	case ELE_WIND:
	case ELE_EARTH:
		if (def_ele == ele || // Same element
			(def_ele == ELE_HOLY && ele_lv >= 2) ||
			(def_ele == ELE_DARK && ele_lv >= 3))
			return false;

		if (ele == ELE_EARTH && def_ele == ELE_UNDEAD && ele_lv >= 4)
			return false;

		return true;

	case ELE_HOLY:
		return def_ele != ELE_HOLY;

	case ELE_DARK:
		return !(def_ele == ELE_POISON ||
			def_ele == ELE_DARK ||
			def_ele == ELE_UNDEAD);

	case ELE_POISON:
		return !((def_ele == ELE_WATER && ele_lv >= 3) ||
			(def_ele == ELE_GHOST && ele_lv >= 3) ||
			(def_ele == ELE_POISON) ||
			(def_ele == ELE_UNDEAD) ||
			(def_ele == ELE_HOLY && ele_lv >= 2) ||
			(def_ele == ELE_DARK));

	case ELE_UNDEAD:
		return !((def_ele == ELE_WATER && ele_lv >= 3) ||
			(def_ele == ELE_FIRE && ele_lv >= 3) ||
			(def_ele == ELE_WIND && ele_lv >= 3) ||
			(def_ele == ELE_EARTH && ele_lv >= 3) ||
			(def_ele == ELE_POISON && ele_lv >= 1) ||
			(def_ele == ELE_UNDEAD) ||
			(def_ele == ELE_DARK));

	case ELE_NEUTRAL:
		return !(def_ele == ELE_GHOST && ele_lv >= 2);

	default:
		return true; // Default to allowed for unsupported elements
	}
}

static bool aa_stopmelee_must_reset(map_session_data* sd, struct block_list* target)
{
	// Parcourt strictement la liste AA existante (pas de nouveaux checks)
	for (auto& sk : sd->aa.autoattackskills) {
		const int skill_id = sk.skill_id;
		const int skill_lv = sk.skill_lv;

		// conditions d’usage (SP, cd, états, mapflags, etc.) – déjà gérées chez toi
		//if (!aa_canuseskill(sd, skill_id, skill_lv))
		//	continue;

		const int s_range = skill_get_range2(sd, skill_id, skill_lv, true);

		if (!battle_check_range(sd, target, s_range))
			continue;

		// obstacle check : le même que dans tes traces [CHK_TGT] path_search ... -> 1
		// (utilise exactement la variante que tu appelles déjà côté AA)
		if (path_search(nullptr, sd->m, sd->x, sd->y, target->x, target->y, 1, CELL_CHKWALL)) {
			// au moins UNE skill est jouable à portée et sans obstacle depuis la case actuelle
			return false;
		}
	}

	// aucune skill ne passe portée + path_search -> mieux vaut lâcher la cible
	return true;
}

// 0 nothing - 1 pick up - 2 heal
int aa_status(map_session_data* sd) {
	if (!sd) return -1;

	struct map_data* mapdata = map_getmapdata(sd->m);
	if (sd->fp.is_fake_player) {
		if (mapdata->getMapFlag(MF_TOWN)) {
			fakeplayer_town_behavior(sd);
			return 20;
		}
		else if (sd->fp.behavior == BEHAVIOR_TOWN) {
			fake_warp_to_town(sd);
			return 22;
		}
		else if (sd->fp.behavior == BEHAVIOR_TRAINING) {
			tg_process(sd);

			if (sd->fp.tg.step == TGStep::DONE_2)
				return -1;

			if(sd->fp.tg.step != TGStep::COMBAT1_2 && sd->fp.tg.step != TGStep::COMBAT1_4)
				return 23;
		}
	}

	if (sd->state.storage_flag) {
		std::string msg = "Automessage - Storage open, close it first!";
		aa_message(sd, "Storage", msg.data(), 300, nullptr);
		status_change_end(sd, SC_AUTOATTACK);
		return 0;
	}

	if (battle_config.feature_autoattack_duration_type && !sd->fp.is_fake_player) {
		if (sd->aa.duration_ <= 0) {
			std::string msg = "Automessage - You don't have timer left on autoattack system!";
			aa_message(sd, "TimerOut", msg.data(), 5, nullptr);
			return -1;
		}

		sd->aa.duration_ = sd->aa.duration_ - battle_config.feature_autoattack_timer;
		pc_setaccountreg(sd, add_str("#aa_duration"), sd->aa.duration_);
	}

	struct party_data* p = (sd->status.party_id) ? party_search(sd->status.party_id) : nullptr;

	uint16 overweight_percent = pc_getpercentweight(*sd);
	uint8 new_overweight = (overweight_percent >= battle_config.major_overweight_rate) ? 2 : (overweight_percent >= battle_config.natural_heal_weight_rate) ? 1 : 0;

	// can be changed to
	//if (status_get_regen_data(sd)->state.overweight) {
	if (new_overweight == 2) {
		std::string msg = "Automessage - I'm overweight - System Off!";
		aa_message(sd, "Overweight", msg.data(), 300, p);
		status_change_end(sd, SC_AUTOATTACK);
		return 0;
	}

	if (pc_isdead(sd))
		return 11;

	struct unit_data* ud = unit_bl2ud(sd);
	if (ud && ud->skilltimer != INVALID_TIMER) // casting
		return 21;

	struct status_data* status = status_get_status_data(*sd);
	t_tick last_tick = gettick();

	//if surrounded by too much monsters
	if (sd->aa.monster_surround && aa_check_surround_monster(sd) > sd->aa.monster_surround) {
		if (aa_teleport(sd)) {
			return 2;
		}
	}

	if (sd->mapindex != sd->aa.lastposition.map && !sd->fp.is_fake_player) {
		status_change_end(sd, SC_AUTOATTACK, INVALID_TIMER);
		return 0;
	}

	// Brain, what the bot need to do during the loop
	// Priority 1 - rest (sit / stand)
	aa_status_rest(sd, status, last_tick);
	if (pc_issit(sd)) return 1;

	// Priority 2 - Buff item
	aa_status_buffitem(sd, last_tick);

	// Priority 3 - potion
	aa_status_potion(sd, status);

	// Priority 4 - heal
	if (aa_status_heal(sd, status, last_tick)) return 3;

	// Priority 5 - Buffs
	if (aa_status_buffs(sd, last_tick)) return 5;

	// Intermediate priority
	aa_status_checkteleport_delay(sd, last_tick);
	aa_status_check_reset(sd, last_tick);

	if (DIFF_TICK(sd->aa.mob_dead_delay, gettick()) > 0) // Delay after mob kill
		return 15;

	// Check targets
	if (sd->aa.target_id && !aa_check_target(sd, sd->aa.target_id))
		aa_target_change(sd, 0);

	if (sd->aa.itempick_id) // Already an item id to pick up
		aa_check_item_pickup(sd, map_id2bl(sd->aa.itempick_id)); // Check the validity of it

	// Priority 6 - Pick up
	if (battle_config.feature_autoattack_pickup && aa_status_pickup(sd, last_tick))
		return 6;

	if (sd->aa.itempick_id)
		sd->aa.pickup_lock_until = last_tick + battle_config.feature_autoattack_timer;

	if (!sd->aa.target_id) // no item to pick up so lf for a target to attack
		aa_check_target_alive(sd);

	// Priority 7 - Attack skill and melee
	if (sd->aa.target_id) {
		if (sd->aa.prio_item_config && last_tick < sd->aa.pickup_lock_until) { //1 == loot
			// On gele l attaque tant qu’on est en fenetre pickup.
			return 11;
		}

		sd->aa.attack_lock_until = last_tick + battle_config.feature_autoattack_timer;

		struct mob_data* md_target = (struct mob_data*)map_id2bl(sd->aa.target_id);

		/* === [SKIP GUARD — version allegee, seulement pour la melee] === */
		int __wr = 1;
		if (status && status->rhw.range > 0) __wr = status->rhw.range;
		if (__wr > 5) __wr--; else if (__wr < 1) __wr = 1;

		bool __skip_attack = false;
		if (__wr == 1) { // n’applique le garde-fou QUE pour la melee
			struct unit_data* __ud_guard = unit_bl2ud(sd);
			if (__ud_guard && __ud_guard->walktimer != INVALID_TIMER &&
				(__ud_guard->target_to == 0 ||
					(__ud_guard->to_x == sd->x && __ud_guard->to_y == sd->y))) {
				//unit_stop_walking(sd, 1); // stop + reset dest/target_to selon rAthena
				__ud_guard->target_to = 0;
				__skip_attack = true;
			}
		}

		/* ==== RANGED ANCHOR : si arme a distance ET a portee, on annule la marche ==== */
		int __wr_guard = 1;
		if (status && status->rhw.range > 0) __wr_guard = status->rhw.range;
		if (__wr_guard > 5) __wr_guard--; else if (__wr_guard < 1) __wr_guard = 1;

		if (md_target && __wr_guard > 1) {
			// pour les armes a distance, on teste la vraie portee “fleche” (ligne de tir)
			if (battle_check_range(sd, (struct block_list*)md_target, __wr_guard)) {
				struct unit_data* __ud_anchor = unit_bl2ud(sd);
				if (__ud_anchor && __ud_anchor->walktimer != INVALID_TIMER) {
					// On fige la marche si on est deja a portee de tir
					//unit_stop_walking(sd, USW_FIXPOS);
					__ud_anchor->state.change_walk_target = 0;
					// Log facultatif:
					// ShowError("[AA][Anchor] ranged in-range -> stop walking\n");
				}
			}
		}

		if (!__skip_attack && aa_status_attack(sd, md_target, last_tick)) {
			sd->aa.last_attack = last_tick;
			return 7;
		}
		else if (aa_status_melee(sd, md_target, last_tick, status)) {
			return 8;
		}
		if (md_target && (sd->aa.stopmelee == 1 || (sd->aa.stopmelee == 2 && status && status->sp >= 100)) && aa_stopmelee_must_reset(sd, md_target))
			aa_target_change(sd, 0);
		else
			return 9;
	}

	if (sd->fp.is_fake_player && battle_config.fake_allow_teleport) {
		// tirage aleatoire entre 45000 et 120000
		if (sd->fp.last_tp < last_tick && sd->aa.last_attack + 2000 < last_tick) {
			int delay = battle_config.fake_teleport_min + (rand() % (battle_config.fake_teleport_max - battle_config.fake_teleport_min + 1));
			sd->fp.last_tp = last_tick + delay;

			status_heal(sd, sd->status.max_hp, sd->status.max_sp, 2);
			pc_randomwarp(sd, CLR_TELEPORT);
			pc_delinvincibletimer(sd);
			clif_parse_LoadEndAck(0, sd);
		}
	}

	// Priority 8 - Move
	if (battle_config.feature_autoattack_movetype == 2)
		aa_move_path(sd);
	else
		aa_move_short(sd, last_tick);

	aa_status_checkmapchange(sd);

	return 10;
}

bool aa_status_checkteleport_delay(map_session_data* sd, t_tick last_tick) {
	t_tick attack_ = DIFF_TICK(last_tick, sd->aa.last_attack);
	t_tick pick_ = DIFF_TICK(last_tick, sd->aa.last_pick);

	if (!sd->aa.teleport.delay_nomobmeet)
		return false;

	if (sd->aa.target_id || sd->aa.itempick_id)
		return false;

	if (sd->aa.teleport.use_teleport && sd->aa.teleport.use_flywing)
		return false;

	if (pick_ < 2000 || attack_ < 2000)
		return false;

	if (attack_ > sd->aa.teleport.delay_nomobmeet) {
		struct unit_data* ud;
		if ((ud = unit_bl2ud(sd)) == nullptr)
			return false;

		if (ud->skilltimer != INVALID_TIMER)
			return false; // Can't teleport while casting

		return aa_teleport(sd);
	}

	return false;
}

// Check if reset of item or target is need
bool aa_status_check_reset(map_session_data* sd, t_tick last_tick) {
	if (unit_is_walking(sd))
		return false;

	if (sd->aa.target_id) {
		t_tick attack_ = DIFF_TICK(last_tick, sd->aa.last_attack);
		if (attack_ > 15000) {
			sd->aa.target_id = 0;
			aa_move_short(sd, last_tick); // Force move
			sd->aa.last_attack = last_tick;
			return true;
		}
	}
	else if (sd->aa.itempick_id) {
		t_tick pick_ = DIFF_TICK(last_tick, sd->aa.last_pick);
		if (pick_ > 5000) {
			aa_item_change(sd, 0); // If not walking
			aa_move_short(sd, last_tick); // Force move
			sd->aa.last_pick = last_tick;
			return true;
		}
	}

	return false;
}

bool aa_status_pickup(map_session_data* sd, t_tick last_tick) {
	if (sd->aa.pickup_item_config == 2) // don't loot anything
		return false;


	if (!sd->aa.prio_item_config && last_tick < sd->aa.attack_lock_until) { //0 == attack
		// On gele le pick up tant qu on est en fenetre attaque.
		return false;
	}

	t_tick pick_ = DIFF_TICK(last_tick, sd->aa.last_pick);
	if (pick_ < battle_config.feature_autoattack_pickup_delay)
		return true; // wait for the delay but do nothing else

	if (sd->aa.prio_item_config) { // - 0 Fight - 1 Loot
		if (sd->aa.itempick_id) {
			aa_target_change(sd, 0); // Remove target for fight
		}
		else {
			aa_check_item_pickup_onfloor(sd); // lf an item on the ground
			if (sd->aa.itempick_id) {
				aa_target_change(sd, 0); // Remove target for fight
				sd->aa.last_pick = last_tick;
			}
			else {
				return false;
			}
		}
	}
	else {
		if (sd->aa.target_id) { // player have a target and must ignore loot
			aa_item_change(sd, 0);
			return false;
		}
		else {
			aa_check_target_alive(sd);
			if (!sd->aa.target_id) { // no target found, so prio to loot
				if (!sd->aa.itempick_id) {
					aa_check_item_pickup_onfloor(sd); // lf an item on the ground
					if (!sd->aa.itempick_id) {
						return false;
					}
					else
						sd->aa.last_pick = last_tick;
				}
			}
		}
	}

	if (sd->aa.itempick_id) { // Item found, order must to be to pick it up
		struct block_list* fitem_bl = map_id2bl(sd->aa.itempick_id);
		if (fitem_bl) {
			struct flooritem_data* fitem = (struct flooritem_data*)fitem_bl;
			if (check_distance_bl(sd, fitem_bl, 2)) { // Distance is bellow 2 cells, pick up
				if (pc_takeitem(sd, fitem)) {
					aa_item_change(sd, 0);
					sd->aa.last_pick = last_tick;
				}

				return true;
			}
			else {
				if (unit_walktobl(sd, fitem_bl, 1, 1))
					return true;
			}
		}
		else
			aa_item_change(sd, 0);
	}
	return false;
}

//Auto-heal skill
bool aa_status_heal(map_session_data* sd, const status_data* status, t_tick last_tick) {
	// Check if auto-healing is enabled and the list of auto-healing skills is not empty
	if (!battle_config.feature_autoattack_autoheal || sd->aa.autoheal.empty()) {
		return false;
	}

	// Check if the global skill cooldown allows skill usage
	if (last_tick < sd->aa.skill_cd) {
		return false;
	}

	// Iterate through the list of auto-healing skills
	for (const auto& autoheal : sd->aa.autoheal) {
		// Ensure the skill can be used, and the current HP meets the trigger condition
		int hp_percentage = (status->hp * 100) / sd->status.max_hp;
		if (!aa_canuseskill(sd, autoheal.skill_id, autoheal.skill_lv) || hp_percentage >= autoheal.min_hp) {
			continue;
		}

		// Check if the skill's individual cooldown has expired
		if (last_tick < autoheal.last_use) {
			continue;
		}

		// Attempt to use the healing skill
		if (unit_skilluse_id(sd, sd->id, autoheal.skill_id, autoheal.skill_lv)) {
			// Consume skill requirements
			//skill_consume_requirement(sd, autoheal.skill_id, autoheal.skill_lv, 2);

			// Apply global cooldown if applicable
			sd->aa.skill_cd = max64(sd->aa.skill_cd, last_tick + max64(battle_config.feature_autoattack_bskill_delay, skill_delayfix(sd, autoheal.skill_id, autoheal.skill_lv)));

			return true; // Healing skill successfully used
		}
	}

	return false; // No healing skill was used
}

//Healing potions
bool aa_status_potion(map_session_data* sd, const status_data* status) {
	bool potion_used = false;

	//not if dead
	if (status_isdead(*sd))
		return false;

	// Check if the auto-potion feature is enabled
	if (!battle_config.feature_autoattack_autopotion || sd->aa.autopotion.empty())
		return false;

	// Iterate through the auto-potion configuration
	for (const auto& potion : sd->aa.autopotion) {
		struct status_data* curent_status = status_get_status_data(*sd);
		sd->canuseitem_tick = gettick();

		// Check and use a potion for HP if the threshold is met
		auto check_and_use_potion = [&](int current_stat, int max_stat, int threshold) {
			if (get_percentage(current_stat, max_stat) < threshold) {
				int index = pc_search_inventory(sd, potion.item_id);
				if (index >= 0) {
					if (pc_useitem(sd, index))
						potion_used = true;
				}
			}
			};

		check_and_use_potion(curent_status->hp, curent_status->max_hp, potion.min_hp);
		check_and_use_potion(curent_status->sp, curent_status->max_sp, potion.min_sp);
	}

	return potion_used;
}

// Automatically sit to rest or stand when conditions are met
bool aa_status_rest(map_session_data* sd, const status_data* status, t_tick last_tick) {
	if (!battle_config.feature_autoattack_sittorest || !sd->aa.autositregen.is_active)
		return false; // Return early if the feature or regen is inactive

	// Calculate the time since the last hit
	t_tick time_since_last_hit = DIFF_TICK(last_tick, sd->aa.last_hit);

	// Check overweight status based on game mode
	uint16 overweight_percent = pc_getpercentweight(*sd);
	uint8 is_overweight = (overweight_percent >= battle_config.major_overweight_rate) ? 2 : (overweight_percent >= battle_config.natural_heal_weight_rate) ? 1 : 0;

	// Calculate HP and SP percentages
	int hp_percentage = (status->hp * 100) / status->max_hp;
	int sp_percentage = (status->sp * 100) / status->max_sp;

	// Determine sit conditions
	bool needs_hp_regen = (sd->aa.autositregen.min_hp > 0 && hp_percentage < sd->aa.autositregen.min_hp);
	bool needs_sp_regen = (sd->aa.autositregen.min_sp > 0 && sp_percentage < sd->aa.autositregen.min_sp);
	bool can_sit = !pc_issit(sd) && (needs_hp_regen || needs_sp_regen) && time_since_last_hit >= 5000 && !is_overweight;

	// Determine stand conditions
	bool regen_complete = true;
	if (sd->aa.autositregen.min_hp > 0 && hp_percentage < sd->aa.autositregen.max_hp)
		regen_complete = false;
	if (sd->aa.autositregen.min_sp > 0 && sp_percentage < sd->aa.autositregen.max_sp)
		regen_complete = false;
	bool can_stand = pc_issit(sd) && (regen_complete || time_since_last_hit < 5000 || is_overweight);

	// Execute actions based on conditions
	if (can_sit) {
		pc_setsit(sd);
		skill_sit(sd, 1);
		clif_sitting(*sd);
	}
	else if (can_stand && pc_setstand(sd, false)) {
		skill_sit(sd, 0);
		clif_standing(*sd);
	}

	return true; // Indicate that the function was processed
}

// Automatically use buff skills
bool aa_status_buffs(map_session_data* sd, t_tick last_tick) {

	if (!battle_config.feature_autoattack_buffskill || sd->aa.autobuffskills.empty())
		return false; // Return early if the feature is disabled or no buffs are configured

	if (last_tick < sd->aa.skill_cd)
		return false; // Return if skill cooldown is active

	for (auto& autobuff : sd->aa.autobuffskills) {
		if (last_tick < autobuff.last_use || !autobuff.is_active) {
			continue; // Skip inactive buffs or buffs on cooldown
		}

		bool skill_exception = false;
		if (autobuff.skill_id == MO_CALLSPIRITS || autobuff.skill_id == CH_SOULCOLLECT)
			skill_exception = true;

		sc_type type = skill_get_sc(autobuff.skill_id);

		// Check if the skill can be used and the player is not already under its effect
		if (aa_canuseskill(sd, autobuff.skill_id, autobuff.skill_lv) &&
			!sd->sc.getSCE(type)) {
			if (!skill_exception) {
				std::shared_ptr<s_status_change_db> scdb = status_db.find(type);

				if (!scdb)
					continue;

				// Check failing SCs from list
				if (!scdb->fail.empty()) {
					bool scdb_result = false;
					for (const auto& it : scdb->fail) {
						// Don't let OPT1 that have RemoveOnDamaged start a new effect in the same attack.
						if (sd->sc.getSCE(it) || sd->sc.lastEffect == it) {
							scdb_result = true;
							break;
						}
					}
					if (scdb_result)
						continue;
				}
			}

			// Handle specific cases for special skills
			if ((autobuff.skill_id == MO_CALLSPIRITS || autobuff.skill_id == CH_SOULCOLLECT) && sd->spiritball == 5)
				continue; // Skip if spirit balls are already maxed

			if (autobuff.skill_id == SA_AUTOSPELL) {
				handle_autospell(sd, autobuff.skill_lv);
				continue; // Skip further processing as autospell handling is done
			}

			// Use the skill
			if (unit_skilluse_id(sd, sd->id, autobuff.skill_id, autobuff.skill_lv)) {
				//skill_consume_requirement(sd, autobuff.skill_id, autobuff.skill_lv, 2);

				sd->aa.skill_cd = max64(sd->aa.skill_cd, last_tick + max64(battle_config.feature_autoattack_bskill_delay, skill_delayfix(sd, autobuff.skill_id, autobuff.skill_lv)));

				return true; // Return true once a skill is used
			}
		}
	}

	return false; // Return false if no skills were used
}

void aa_token_respawn(map_session_data* sd, int flag) {
	if (sd->fp.is_fake_player) {
		status_revive(sd, 100, 100);
		fakeplayer_dead_tp(sd);
		return;
	}

	if (flag && sd && sd->state.autoattack) {
		//token of siefried
		if (sd->aa.token_siegfried && pc_revive_item(sd))
			return;

		// return to the save point
		if (sd->aa.return_to_savepoint) {
			struct party_data* p = (sd->status.party_id) ? party_search(sd->status.party_id) : nullptr;

			pc_respawn(sd, CLR_OUTSIGHT);
			std::string msg = "Automessage - I'm dead, returning to save point - System Off!";
			aa_message(sd, "Dead", msg.data(), 300, p);
			status_change_end(sd, SC_AUTOATTACK);
		}
	}
}

// Helper function to handle SA_AUTOSPELL logic
void handle_autospell(map_session_data* sd, int skill_lv) {
	short random_skill;

	//Already in use with right lv
	if (sd->sc.getSCE(SC_AUTOSPELL) && sd->sc.getSCE(SC_AUTOSPELL)->val1 == skill_lv)
		return;

	sd->menuskill_val = pc_checkskill(sd, SA_AUTOSPELL);

#ifdef RENEWAL
	switch (skill_lv) {
	case 1:
	case 2:
	case 3:
		random_skill = rand() % 3;
		skill_autospell(sd, random_skill == 0 ? MG_FIREBOLT : (random_skill == 1 ? MG_COLDBOLT : MG_LIGHTNINGBOLT));
		break;
	case 4:
	case 5:
	case 6:
		random_skill = rand() % 2;
		skill_autospell(sd, random_skill == 0 ? MG_FIREBALL : MG_SOULSTRIKE);
		break;
	case 7:
	case 8:
	case 9:
		random_skill = rand() % 2;
		skill_autospell(sd, random_skill == 0 ? MG_FROSTDIVER : WZ_EARTHSPIKE);
		break;
	case 10:
		random_skill = rand() % 2;
		skill_autospell(sd, random_skill == 0 ? MG_THUNDERSTORM : WZ_HEAVENDRIVE);
		break;
	}
#else
	switch (skill_lv) {
	case 1:
		skill_autospell(sd, MG_NAPALMBEAT);
		break;
	case 2:
	case 3:
	case 4:
		random_skill = rand() % 3;
		skill_autospell(sd, random_skill == 0 ? MG_FIREBOLT : (random_skill == 1 ? MG_COLDBOLT : MG_LIGHTNINGBOLT));
		break;
	case 5:
	case 6:
	case 7:
		skill_autospell(sd, MG_SOULSTRIKE);
		break;
	case 8:
	case 9:
		skill_autospell(sd, MG_FIREBALL);
		break;
	case 10:
		skill_autospell(sd, MG_FROSTDIVER);
		break;
	}
#endif

	sd->menuskill_id = 0;
	sd->menuskill_val = 0;
}

// true if have the status
bool aa_checkactivestatus(map_session_data* sd, sc_type type) {
	if (!sd)
		return 0;

	if (sd->sc.getSCE(type))
		return true;

	return false;
}

// Automatically use buff items
bool aa_status_buffitem(map_session_data* sd, t_tick last_tick) {
	// Return early if the feature is disabled or no buff items are configured
	if (!battle_config.feature_autoattack_buffitems || sd->aa.autobuffitems.empty()) {
		return false;
	}

	bool used_item = false;

	// Iterate through configured auto-buff items
	for (auto& autobuffitem : sd->aa.autobuffitems) {
		// Skip items on cooldown or inactive items
		if (!autobuffitem.is_active)
			continue;

		// Check if the player have the status
		if (aa_checkactivestatus(sd, (sc_type)autobuffitem.status))
			continue;

		// Check inventory for the item and use it if available
		int inventory_index = pc_search_inventory(sd, autobuffitem.item_id);
		if (inventory_index >= 0 && pc_useitem(sd, inventory_index))
			used_item = true;
	}

	return used_item;
}

// Handles auto-attack skills in combat
bool aa_status_attack(map_session_data* sd, struct mob_data* md_target, t_tick last_tick) {
	if (!md_target)
		return false;

	if (!battle_config.feature_autoattack_attackskill || last_tick < sd->aa.skill_cd || sd->aa.autoattackskills.empty())
		return false;

	std::vector<s_autoattackskills> combo_skills, normal_skills, ordered_combo, execution_list;

	// Separer combos / normal
	for (auto& sk : sd->aa.autoattackskills) {
		if (!sk.is_active)
			continue;
		if (skill_is_combo(sk.skill_id)) {
			switch (sk.skill_id) {
			case MO_CHAINCOMBO:
				if (sd->sc.getSCE(SC_COMBO) && sd->sc.getSCE(SC_COMBO)->val1 == MO_TRIPLEATTACK)
					combo_skills.push_back(sk);
				break;
			case MO_COMBOFINISH:
				if (sd->sc.getSCE(SC_COMBO) && sd->sc.getSCE(SC_COMBO)->val1 == MO_CHAINCOMBO)
					combo_skills.push_back(sk);
				break;
			case CH_TIGERFIST:
				if (sd->sc.getSCE(SC_COMBO) && sd->sc.getSCE(SC_COMBO)->val1 == MO_COMBOFINISH)
					combo_skills.push_back(sk);
				break;
			case CH_CHAINCRUSH:
				if (sd->sc.getSCE(SC_COMBO) && (sd->sc.getSCE(SC_COMBO)->val1 == MO_COMBOFINISH || sd->sc.getSCE(SC_COMBO)->val1 == CH_TIGERFIST))
					combo_skills.push_back(sk);
				break;
			case SR_DRAGONCOMBO:
				if (!sd->sc.getSCE(SC_COMBO))
					combo_skills.push_back(sk);
				break;
			case SR_FALLENEMPIRE:
				if (sd->sc.getSCE(SC_COMBO) && sd->sc.getSCE(SC_COMBO)->val1 == SR_DRAGONCOMBO)
					combo_skills.push_back(sk);
				break;
			case SR_TIGERCANNON:
				if (sd->sc.getSCE(SC_EXPLOSIONSPIRITS))
					combo_skills.push_back(sk);
				break;
			case SR_GATEOFHELL:
				if (sd->sc.getSCE(SC_COMBO) && sd->sc.getSCE(SC_COMBO)->val1 == SR_FALLENEMPIRE)
					combo_skills.push_back(sk);
				break;
			case TK_TURNKICK:
				if (sd->sc.getSCE(SC_COMBO) && sd->sc.getSCE(SC_COMBO)->val1 == TK_TURNKICK)
					combo_skills.push_back(sk);
				break;
			case TK_STORMKICK:
				if (sd->sc.getSCE(SC_COMBO) && sd->sc.getSCE(SC_COMBO)->val1 == TK_STORMKICK)
					combo_skills.push_back(sk);
				break;
			case TK_DOWNKICK:
				if (sd->sc.getSCE(SC_COMBO) && sd->sc.getSCE(SC_COMBO)->val1 == TK_DOWNKICK)
					combo_skills.push_back(sk);
				break;
			case TK_COUNTER:
				if (sd->sc.getSCE(SC_COMBO) && sd->sc.getSCE(SC_COMBO)->val1 == TK_COUNTER)
					combo_skills.push_back(sk);
				break;
			default:
				normal_skills.push_back(sk);
				break;
			}
		}
		else
			normal_skills.push_back(sk);
	}

	// 4. Melanger les normal_skills et concatener
	std::shuffle(normal_skills.begin(), normal_skills.end(), generator);

	execution_list.reserve(combo_skills.size() + normal_skills.size());
	execution_list.insert(execution_list.end(),
		combo_skills.begin(), combo_skills.end());
	execution_list.insert(execution_list.end(),
		normal_skills.begin(), normal_skills.end());

	t_tick attack_delay = DIFF_TICK(last_tick, sd->aa.last_attack);
	time_t current_time = time(NULL);
	bool flywing_used = false;
	bool ispathOk = false;

	if (!execution_list.empty())
		ispathOk = path_search_long(nullptr, sd->m, sd->x, sd->y, md_target->x, md_target->y, CELL_CHKWALL);

	// Process each skill
	for (const auto& skill : execution_list) {
		if (!aa_canuseskill(sd, skill.skill_id, skill.skill_lv)) {
			continue;
		}

		// Handle specific skill requirements
		switch (skill.skill_id) {
		case NC_ARMSCANNON:
		case GN_CARTCANNON:
			aa_cannonballchange(sd, md_target);
			break;
		case NJ_KUNAI:
			aa_kunaichange(sd, md_target, 1);
			break;
		case KO_HAPPOKUNAI:
			aa_kunaichange(sd, md_target, 8);
			break;
		}

		int skill_range = max(2, abs(skill_get_range2(sd, skill.skill_id, skill.skill_lv, true)));
		bool in_range = battle_check_range(sd, md_target, skill_range) ? true : false;

		// Check range and pathfinding
		if (!in_range || !ispathOk) {
			if (unit_walktobl(sd, md_target, skill_range, 1)) {
				return true;
			}
			continue;
		}

		// Execute skill
		bool skill_used = false;
		if (skill.skill_id == AL_HEAL) {
			status_data* tstatus = status_get_status_data(*md_target);

			if (battle_check_undead(tstatus->race, tstatus->def_ele))
				skill_used = unit_skilluse_id(sd, sd->aa.target_id, skill.skill_id, skill.skill_lv);
		}
		else if (skill_is_combo(skill.skill_id) || (skill_get_inf(skill.skill_id) & INF_ATTACK_SKILL)) {
			skill_used = unit_skilluse_id(sd, sd->aa.target_id, skill.skill_id, skill.skill_lv);
		}
		else if (skill_get_inf(skill.skill_id) & INF_GROUND_SKILL) {
			skill_used = unit_skilluse_pos(sd, md_target->x, md_target->y, skill.skill_id, skill.skill_lv);
		}
		else if (skill_get_inf(skill.skill_id) & INF_SELF_SKILL) {
			if (battle_check_range(sd, md_target, 2)) {
				skill_used = unit_skilluse_id(sd, sd->id, skill.skill_id, skill.skill_lv);
			}
			else {
				if (unit_walktobl(sd, md_target, 2, 1)) {
					return true;
				}
				continue;
			}
		}

		if (!skill_used)
			continue;

		// Update cooldowns and consume resources
		sd->idletime = time(NULL);
		//skill_consume_requirement(sd, skill.skill_id, skill.skill_lv, 2);

		sd->aa.skill_cd = max64(sd->aa.skill_cd, last_tick + max64(battle_config.feature_autoattack_askill_delay, skill_delayfix(sd, skill.skill_id, skill.skill_lv)));
		sd->aa.last_attack = last_tick;

		return true;
	}

	return false;
}

bool aa_status_melee(map_session_data* sd, struct mob_data* md_target, t_tick last_tick, const status_data* status) {
	bool is_attacking = false;
	if (!md_target)
		return false;

	if (sd->aa.stopmelee == 0 || (sd->aa.stopmelee == 2 && status->sp < 100)) {
		/* --- Compute weapon range and distance --- */
		int weapon_range = 1;
		if (status && status->rhw.range > 0) {
			weapon_range = status->rhw.range;
		}
		if (weapon_range > 5)
			weapon_range--;
		else if (weapon_range < 1)
			weapon_range = 1;

		int dist = distance_bl(sd, md_target);
		bool in_weapon_range = (weapon_range > 1)
			? battle_check_range(sd, (struct block_list*)md_target, weapon_range)  // arc/arme a distance
			: (dist <= weapon_range);                                             // melee pure

		/* --- Ranged branch (bow, gun, etc.) --- */
		if (weapon_range > 1) {
			if (in_weapon_range) {
				/* A portee - tirer sans avancer */
				if (!unit_attack(sd, sd->aa.target_id, 1)) {
					sd->aa.last_attack = last_tick;
					return true;
				}
				/* Si l attaque n a pas ete emise (lag/etat), ne rien faire de risque ici. */
				return false;
			}
			else {
				/* Hors de portee - se rapprocher juste assez pour etre a portee, pas plus. */
				/* On utilise unit_walktobl avec un rayon egal a weapon_range pour eviter le "hug". */
				struct unit_data* __ud_pre = unit_bl2ud(sd);
				if (__ud_pre) {
					if ((t_tick)last_tick < sd->aa.attack_lock_until)
						return false;

					if (__ud_pre->walkpath.path_len > 0 && (__ud_pre->to_x != sd->x || __ud_pre->to_y != sd->y))
						return false;
				}

				// snapshot
				int16 __prev_to_x = __ud_pre ? __ud_pre->to_x : 0;
				int16 __prev_to_y = __ud_pre ? __ud_pre->to_y : 0;
				uint32 __prev_target_to = __ud_pre ? __ud_pre->target_to : 0;
				int __prev_path_len = __ud_pre ? __ud_pre->walkpath.path_len : 0;

				int __w = unit_walktobl(sd, md_target, weapon_range, 1);
				struct unit_data* __ud_post = unit_bl2ud(sd);

				// rollback si destructeur (echec, target_to=0, ou to == pos)
				if (!__w || (__ud_post && (__ud_post->target_to == 0 ||
					(__ud_post->to_x == sd->x && __ud_post->to_y == sd->y)))) {
					if (__ud_post) {
						__ud_post->to_x = __prev_to_x;
						__ud_post->to_y = __prev_to_y;
						__ud_post->target_to = __prev_target_to;
					}
					return false;
				}

				if (__w) {
					sd->aa.last_attack = last_tick;
					return true;
				}

				return false;
			}
		}

		// unit_attack returns 0 on success in Hercules/rAthena, hence the '!' check in original code.
		if (!unit_attack(sd, sd->aa.target_id, 1)) {
			sd->aa.last_attack = last_tick;
			return true;
		}

		if (sd->state.autotrade) {
			struct unit_data* __ud_pre2 = unit_bl2ud(sd);

			if (__ud_pre2) {
				if ((t_tick)last_tick < sd->aa.attack_lock_until)
					return false;

				if (__ud_pre2->walkpath.path_len > 0 && (__ud_pre2->to_x != sd->x || __ud_pre2->to_y != sd->y))
					return false;
			}

			// snapshot
			int16 __prev_to_x2 = __ud_pre2 ? __ud_pre2->to_x : 0;
			int16 __prev_to_y2 = __ud_pre2 ? __ud_pre2->to_y : 0;
			uint32 __prev_target_to2 = __ud_pre2 ? __ud_pre2->target_to : 0;
			int __prev_path_len2 = __ud_pre2 ? __ud_pre2->walkpath.path_len : 0;

			int __w = unit_walktobl(sd, md_target, 2, 1);
			struct unit_data* __ud_post2 = unit_bl2ud(sd);

			if (!__w || (__ud_post2 && (__ud_post2->target_to == 0 ||
				(__ud_post2->to_x == sd->x && __ud_post2->to_y == sd->y)))) {
				if (__ud_post2) {
					__ud_post2->to_x = __prev_to_x2;
					__ud_post2->to_y = __prev_to_y2;
					__ud_post2->target_to = __prev_target_to2;
					__ud_post2->walkpath.path_len = __prev_path_len2;
				}
				return false;
			}

			if (__w) {
				sd->aa.last_attack = last_tick;
				return true;
			}

			return false;
		}
	}

	return false;
}

// Fonction pour calculer le coit heuristique (distance de Manhattan)
float heuristic(int x1, int y1, int x2, int y2) {
	return static_cast<float>(abs(x2 - x1) + abs(y2 - y1));
}

struct PathPoint { int x, y; }; // if your type differs, adapt

static inline int octile_h(int x, int y, int tx, int ty) {
	int dx = x > tx ? x - tx : tx - x;
	int dy = y > ty ? y - ty : ty - y;
	int m = dx < dy ? dx : dy;
	// 10 for orthogonal, 14 for diagonal (≈sqrt(2)*10)
	return (dx + dy - m) * 10 + m * 14;
}

static inline int cell_passable_fast(const struct map_data* m, int16 x, int16 y) {
	// meme logique OOB que map_getcellp pour CHKPASS : hors borne => 0
	if (x < 0 || x >= m->xs - 1 || y < 0 || y >= m->ys - 1)
		return 0;

	const size_t idx = (size_t)(uint16_t)y * (size_t)m->xs + (size_t)(uint16_t)x;
	const struct mapcell c = m->cell[idx];

#ifdef CELL_NOSTACK
	if (c.cell_bl >= battle_config.custom_cell_stack_limit)
		return 0;
#endif
	return c.walkable; // booleen stocke dans le bitfield
}

// Optional: a very small LOS check (Bresenham). Comment out if you prefer.
static inline bool los_clear(const map_data* md, int x0, int y0, int x1, int y1) {
	int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy, x = x0, y = y0;
	while (true) {
		if ((unsigned)x >= (unsigned)md->xs || (unsigned)y >= (unsigned)md->ys) return false;
		if (!cell_passable_fast(const_cast<map_data*>(md), x, y)) return false;
		if (x == x1 && y == y1) break;
		int e2 = err << 1;
		if (e2 >= dy) { err += dy; x += sx; }
		if (e2 <= dx) { err += dx; y += sy; }
	}
	return true;
}

// Calcul du chemin en algorithme A*
bool algorithm_path_finding_worker(int16_t m, int start_x, int start_y, int target_x, int target_y, std::vector<std::tuple<int, int>>& out_path, bool is_fake)
{
	// 1. Nettoyage de la sortie
	out_path.clear();

	if (start_x == target_x && start_y == target_y)
		return false;

	// Pour les fake players, on augmente aléatoirement le poids de l'heuristique.
	// 1 => chemin quasi optimal, 2..4 => plus "greedy", moins d'explorations, chemin un peu plus tordu.
	uint32_t h_mul = 1;
	uint8_t neigh_order[8];
	if (is_fake) {
		h_mul = (uint32_t)rnd_value(2, 4); // [2..4]

		for (int i = 0; i < 8; ++i)
			neigh_order[i] = (uint8_t)i;

		// Fisher-Yates shuffle simple
		for (int i = 7; i > 0; --i) {
			int j = rnd_value(0, i);
			std::swap(neigh_order[i], neigh_order[j]);
		}
	}

	// 2. Récupération des données de map (Lecture seule = Thread Safe généralement)
	map_data* md = map_getmapdata(m);
	if (!md) return false;

	const size_t xs = (size_t)md->xs, ys = (size_t)md->ys;
	if (xs <= 0 || ys <= 0) return false;
	const size_t N = xs * ys;

	// 3. Vérification des bornes
	if (start_x < 0 || start_y < 0 || (size_t)start_x >= xs || (size_t)start_y >= ys) return false;
	if (target_x < 0 || target_y < 0 || (size_t)target_x >= xs || (size_t)target_y >= ys) return false;

	// ---- Cache réutilisable par Thread (Thread Local Storage) ----
	struct Cache {
		std::vector<uint32_t> stamp;     // "vu" à epoch ?
		std::vector<uint32_t> gscore;    // meilleur g
		std::vector<int32_t>  parent;    // parent
		std::vector<uint32_t> closed;    // "clos" à epoch ?
		std::vector<int32_t>  open_pos;  // position dans le tas si open
		std::vector<uint32_t> open_tag;  // "dans open" à epoch ?
		uint32_t epoch = 1;
		size_t cap = 0;
		void ensure(size_t need) {
			if (need > cap) {
				stamp.assign(need, 0);
				gscore.assign(need, 0);
				parent.assign(need, -1);
				closed.assign(need, 0);
				open_pos.assign(need, -1);
				open_tag.assign(need, 0);
				cap = need; epoch = 1;
			}
			else if (++epoch == 0) {
				// reset lazy si overflow
				std::fill(stamp.begin(), stamp.begin() + cap, 0);
				std::fill(closed.begin(), closed.begin() + cap, 0);
				std::fill(open_pos.begin(), open_pos.begin() + cap, -1);
				std::fill(open_tag.begin(), open_tag.begin() + cap, 0);
				epoch = 1;
			}
		}
	};

	// Chaque thread aura sa propre instance de cache statique
	static thread_local Cache C;
	C.ensure(N);

	// Directions en constexpr
	static constexpr int DX[8] = { -1,-1, 0, 1, 1, 1, 0,-1 };
	static constexpr int DY[8] = { 0,-1,-1,-1, 0, 1, 1, 1 };
	static constexpr int CT[8] = { 10,14,10,14,10,14,10,14 };

	// Heuristique octile
	auto make_f = [&](uint32_t g, int nx, int ny) -> uint32_t {
		uint32_t h = (uint32_t)octile_h(nx, ny, target_x, target_y);
		// Pour les fake players, on multiplie l'heuristique : A* devient plus "greedy"
		// => moins de CPU, chemin pas forcément optimal, donc des variantes possibles.
		if (is_fake && h_mul > 1) {
			if (h > UINT32_MAX / h_mul)
				h = UINT32_MAX / h_mul;
			h *= h_mul;
		}
		if (g > UINT32_MAX / 32) g = UINT32_MAX / 32;
		return g * 32u + h * 33u;
		};

	// --- Fast-path: Ligne de vue directe (LOS) ---
	if (los_clear(md, start_x, start_y, target_x, target_y)) {
		int x0 = start_x, y0 = start_y, x1 = target_x, y1 = target_y;
		int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
		int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
		int err = dx + dy, x = x0, y = y0;

		out_path.reserve((size_t)std::max(dx, dy) + 2);

		while (true) {
			out_path.push_back(std::make_tuple(x, y)); // Modification ici
			if (x == x1 && y == y1) break;
			int e2 = err << 1;
			if (e2 >= dy) { err += dy; x += sx; }
			if (e2 <= dx) { err += dx; y += sy; }
		}

		// Pas de path_index ici, c'est géré par le main thread
		return true;
	}

	const size_t start_i = (size_t)start_y * xs + (size_t)start_x;
	const size_t target_i = (size_t)target_y * xs + (size_t)target_x;

	// ---------- Tas 8-ary indexé ----------
	struct MinHeap8 {
		struct Node { uint32_t f; int idx; };

		std::vector<Node> a;
		std::vector<int32_t>& pos;
		std::vector<uint32_t>& tag;
		uint32_t epoch;

		MinHeap8(std::vector<int32_t>& p, std::vector<uint32_t>& t, uint32_t e)
			: a(), pos(p), tag(t), epoch(e) {
		}

		inline bool empty() const { return a.empty(); }
		inline void reserve(size_t n) { a.reserve(n); }

		inline void sift_up(size_t i) {
			while (i) {
				size_t p = (i - 1) / 8;
				if (a[p].f <= a[i].f) break;
				std::swap(a[p], a[i]);
				if ((size_t)a[p].idx < pos.size()) pos[a[p].idx] = (int32_t)p;
				if ((size_t)a[i].idx < pos.size()) pos[a[i].idx] = (int32_t)i;
				i = p;
			}
		}
		inline void sift_down(size_t i) {
			size_t sz = a.size();
			while (true) {
				size_t c1 = 8 * i + 1;
				if (c1 >= sz) break;
				size_t m = c1;
				size_t c2 = c1 + 1; if (c2 < sz && a[c2].f < a[m].f) m = c2;
				size_t c3 = c1 + 2; if (c3 < sz && a[c3].f < a[m].f) m = c3;
				size_t c4 = c1 + 3; if (c4 < sz && a[c4].f < a[m].f) m = c4;
				size_t c5 = c1 + 4; if (c5 < sz && a[c5].f < a[m].f) m = c5;
				size_t c6 = c1 + 5; if (c6 < sz && a[c6].f < a[m].f) m = c6;
				size_t c7 = c1 + 6; if (c7 < sz && a[c7].f < a[m].f) m = c7;
				size_t c8 = c1 + 7; if (c8 < sz && a[c8].f < a[m].f) m = c8;

				if (a[m].f >= a[i].f) break;
				std::swap(a[i], a[m]);
				if ((size_t)a[i].idx < pos.size()) pos[a[i].idx] = (int32_t)i;
				if ((size_t)a[m].idx < pos.size()) pos[a[m].idx] = (int32_t)m;
				i = m;
			}
		}

		inline void push_new(Node n) {
			if ((size_t)n.idx >= pos.size() || (size_t)n.idx >= tag.size()) return;
			a.push_back(n);
			size_t i = a.size() - 1;
			pos[n.idx] = (int32_t)i;
			tag[n.idx] = epoch;
			sift_up(i);
		}
		inline void decrease_key(int idx, uint32_t new_f) {
			if (idx < 0 || (size_t)idx >= pos.size()) return;
			int32_t p = pos[idx];
			if (p < 0 || (size_t)p >= a.size()) return;
			Node& ref = a[(size_t)p];
			if (new_f >= ref.f) return;
			ref.f = new_f;
			sift_up((size_t)p);
		}
		inline Node pop_min8() {
			Node res = a[0];
			if ((size_t)res.idx < tag.size()) tag[res.idx] = 0;
			if ((size_t)res.idx < pos.size()) pos[res.idx] = -1;
			size_t last = a.size() - 1;
			std::swap(a[0], a[last]);
			a.pop_back();
			if (!a.empty()) {
				if ((size_t)a[0].idx < pos.size()) pos[a[0].idx] = 0;
				sift_down(0);
			}
			return res;
		}
	};

	MinHeap8 open{ C.open_pos, C.open_tag, C.epoch };

	auto get_g = [&](size_t idx) -> uint32_t {
		return (C.stamp[idx] == C.epoch) ? C.gscore[idx] : UINT32_MAX;
		};
	auto push_or_decrease = [&](size_t idx, uint32_t newg, int nx, int ny) {
		uint32_t f = make_f(newg, nx, ny);
		if (idx >= C.open_tag.size()) return;
		if (C.open_tag[idx] == C.epoch)
			open.decrease_key((int)idx, f);
		else
			open.push_new(MinHeap8::Node{ f, (int)idx });
		};

	// Init Start
	C.parent[start_i] = (int32_t)start_i;
	C.stamp[start_i] = C.epoch;
	C.gscore[start_i] = 0;
	push_or_decrease(start_i, 0u, start_x, start_y);

	// Reserve
	{
		int rough_len = octile_h(start_x, start_y, target_x, target_y) / 10 + 2;
		if (rough_len < 16) rough_len = 16;
		out_path.reserve((size_t)rough_len); // Utilisation de out_path

		size_t est_open = (size_t)rough_len * 6;
		if (est_open > N) est_open = N;
		open.reserve(est_open);
	}

	size_t max_iters = std::min<size_t>(
		N,
		(size_t)(is_fake ? 200000 : 800000)
	);
	int oct = octile_h(start_x, start_y, target_x, target_y);
	int rough_len = std::max(16, oct / 10 + 2);
	size_t base_budget = (size_t)rough_len * 40; // 40 * longueur estimée

	if (base_budget < 20000) base_budget = 20000;
	if (base_budget > 200000) base_budget = 200000;

	max_iters = std::min(max_iters, base_budget);
	size_t iters = 0;

	// --- Boucle A* ---
	while (!open.empty() && iters++ < max_iters) {
		auto cur = open.pop_min8();

		if (cur.idx < 0 || (size_t)cur.idx >= N) break;
		C.closed[(size_t)cur.idx] = C.epoch;

		if ((size_t)cur.idx == target_i) break;

		const int cx = cur.idx % (int)xs;
		const int cy = cur.idx / (int)xs;

		for (int i = 0; i < 8; ++i) {
			const int nx = cx + DX[i];
			const int ny = cy + DY[i];
			if (nx < 0 || ny < 0 || (size_t)nx >= xs || (size_t)ny >= ys) continue;

			// Note: Assure-toi que cell_passable_fast est Thread-Safe
			// (c'est-à-dire qu'elle ne lit que les GAT statiques, pas les NPC dynamiques)
			if (!cell_passable_fast(md, (int16)nx, (int16)ny)) continue;

			if (DX[i] && DY[i]) {
				if (!cell_passable_fast(md, (int16)(cx + DX[i]), (int16)cy) &&
					!cell_passable_fast(md, (int16)cx, (int16)(cy + DY[i]))) {
					continue;
				}
			}

			const size_t nidx = (size_t)ny * xs + (size_t)nx;
			if (nidx >= N) continue;
			if (C.closed[nidx] == C.epoch) continue;

			uint32_t base_g = get_g((size_t)cur.idx);
			if (base_g == UINT32_MAX) continue;
			uint32_t cand_g = base_g + (uint32_t)CT[i];

			if (cand_g < get_g(nidx)) {
				C.parent[nidx] = cur.idx;
				C.stamp[nidx] = C.epoch;
				C.gscore[nidx] = cand_g;
				push_or_decrease(nidx, cand_g, nx, ny);
			}
		}
	}

	// --- Reconstruction du chemin ---
	if (C.stamp[target_i] != C.epoch)
		return false;

	size_t idx = target_i;
	size_t guard = 0;
	while (idx != start_i && idx < N && C.parent[idx] != -1 && guard++ < N)
	{
		int x = (int)(idx % xs), y = (int)(idx / xs);
		out_path.push_back(std::make_tuple(x, y)); // Modification ici
		idx = (size_t)C.parent[idx];
	}

	if (guard >= N || idx >= N) {
		out_path.clear();
		return false;
	}

	// Ajouter le point de départ et inverser
	out_path.push_back(std::make_tuple(start_x, start_y));
	std::reverse(out_path.begin(), out_path.end());

	return true;
}

class PathFindingManager {
private:
	std::queue<std::shared_ptr<AsyncPathJob>> queue; // File d'attente
	std::vector<std::thread> workers;                // Les threads
	std::mutex mtx;                                  // Verrou pour protéger la file
	std::condition_variable cv;                      // Pour réveiller les threads
	bool stop_flag = false;                          // Pour éteindre proprement

	// La boucle infinie que chaque thread va exécuter
	void worker_loop() {
		while (true) {
			std::shared_ptr<AsyncPathJob> job;

			{
				// On verrouille pour récupérer un job
				std::unique_lock<std::mutex> lock(mtx);
				cv.wait(lock, [this] { return stop_flag || !queue.empty(); });

				if (stop_flag && queue.empty()) return; // Fin du thread

				job = queue.front();
				queue.pop();
			}

			// --- DEBUT DU CALCUL LOURD (Hors du verrou) ---

			// Appel à ta version thread-safe de l'algo (voir Étape 3)
			// Note: on passe les variables brutes, pas 'sd'
			job->success = algorithm_path_finding_worker(
				job->m,
				job->start_x, job->start_y,
				job->target_x, job->target_y,
				job->path, job->is_fake
			);

			job->is_done.store(true, std::memory_order_release);
			// --- FIN DU CALCUL ---
		}
	}

public:
	// Constructeur : lance X threads (ex: 2 ou 4 selon ton CPU)
	PathFindingManager(int thread_count = 2) {
		for (int i = 0; i < thread_count; ++i) {
			workers.emplace_back(&PathFindingManager::worker_loop, this);
		}
	}

	// Destructeur : nettoyage propre
	~PathFindingManager() {
		{
			std::lock_guard<std::mutex> lock(mtx);
			stop_flag = true;
		}
		cv.notify_all();
		for (auto& t : workers) if (t.joinable()) t.join();
	}

	// Fonction pour ajouter une requête
	void push_job(std::shared_ptr<AsyncPathJob> job) {
		{
			std::lock_guard<std::mutex> lock(mtx);
			queue.push(job);
		}
		cv.notify_one(); // Réveille un thread
	}
};

// Instance globale (à initialiser au démarrage du serveur ou statique)
static PathFindingManager g_pathfinder(2); // 2 threads dédiés

//Move
bool aa_move_short(map_session_data* sd, t_tick last_tick) {
	if (unit_is_walking(sd))
		return false;

	struct map_data* mapdata = map_getmapdata(sd->m);

	// Bornes de carte (pour eviter des map_getcell inutiles)
	const int xs = mapdata->xs;
	const int ys = mapdata->ys;

	const int max_distance = battle_config.feature_autoattack_move_max;
	const int min_distance = battle_config.feature_autoattack_move_min;
	const int grid_size = max_distance * 2 + 1;
	const int grid_area = grid_size * grid_size;

	int dx, dy, x, y;
	bool dest_checked = false, valid_move_found = false;

	// Un seul tirage aleatoire pour limiter le coût RNG
	int r = rnd();
	int direction = (r >> 3) & 3; // 0..3
	dx = r % grid_size - max_distance;
	dy = (r / grid_size) % grid_size - max_distance;

	// Respect du min_distance comme avant
	dx = (dx >= 0) ? (dx < min_distance ? min_distance : dx)
		: (dx > -min_distance ? -min_distance : dx);
	dy = (dy >= 0) ? (dy < min_distance ? min_distance : dy)
		: (dy > -min_distance ? -min_distance : dy);

	// Tentative de retour a la derniere position (logique d'origine)
	if (battle_config.feature_autoattack_movetype == 1) {
		const int target_x = sd->aa.lastposition.dx + sd->x;
		const int target_y = sd->aa.lastposition.dy + sd->y;

		const bool isLastPositionSet = (sd->aa.lastposition.dx != 0 || sd->aa.lastposition.dy != 0);
		const bool hasMovedFromLastPos = (target_x != sd->x || target_y != sd->y);

		// remplace map_getcell(..., CELL_CHKPASS)
		bool canMoveToLastPosition = cell_passable_fast(mapdata, target_x, target_y) &&
			unit_walktoxy(sd, target_x, target_y, 4);

		if (!dest_checked && isLastPositionSet && hasMovedFromLastPos && canMoveToLastPosition) {
			sd->aa.last_move = last_tick;
			return true;
		}
		dest_checked = true;
	}

	// Bitset O(1) pour eviter de re-tester la meme cellule (dx,dy)
	// Taille = ceil(grid_area/64) mots de 64 bits.
	std::vector<uint64_t> seen(static_cast<size_t>((grid_area + 63) / 64), 0ULL);

	// Directions d'origine (Right, Left, Down, Up)
	const int directions[4][2] = {
		{ 1,  0}, {-1,  0}, { 0,  1}, { 0, -1}
	};

	for (int attempt = 0; attempt < grid_area && !valid_move_found; ++attempt) {
		x = sd->x + dx;
		y = sd->y + dy;

		// Deja teste ? (bitset O(1) indexe par (dx,dy))
		const unsigned int bitidx =
			static_cast<unsigned int>(dx + max_distance) +
			static_cast<unsigned int>(dy + max_distance) * static_cast<unsigned int>(grid_size);
		const unsigned int idx_word = bitidx >> 6;
		const unsigned int idx_bit = bitidx & 63;
		if ((seen[idx_word] >> idx_bit) & 1ULL) goto advance;

		// 1) Skips rapides pour eviter map_getcell inutilement

		// Meme case : ne sert a rien de tester/cheminer
		if (x == sd->x && y == sd->y) {
			goto advance;
		}

		// Hors bornes : map_getcell ferait tôt ou tard le check, on evite l'appel
		if (!(x >= 0 && x < xs - 1 && y >= 0 && y < ys - 1)) {
			goto advance;
		}

		// 2) Test passabilite (leger), puis path (potentiellement cher)
		if (cell_passable_fast(mapdata, x, y) && unit_walktoxy(sd, x, y, 4)) {
			// Marquer cette coordonnee comme vue
			seen[idx_word] |= (1ULL << idx_bit);
			sd->aa.last_move = last_tick;
			valid_move_found = true;

			if (battle_config.feature_autoattack_movetype == 1) {
				sd->aa.lastposition.dx = dx;
				sd->aa.lastposition.dy = dy;
			}
			break;
		}

	advance:
		// Marquer la coordonnee (dx,dy) comme vue dans le bitset
		seen[idx_word] |= (1ULL << idx_bit);

		// Avancer comme dans l'algo initial
		dx += directions[direction][0] * max_distance;
		dy += directions[direction][1] * max_distance;

		// Branchless wrap (meme semantique, moins de branches)
		dx -= (dx > max_distance) * grid_size;
		dx += (dx < -max_distance) * grid_size;
		dy -= (dy > max_distance) * grid_size;
		dy += (dy < -max_distance) * grid_size;
	}

	return valid_move_found;
}

// Helpers locaux sans collision de nom
static inline int aa_sqdist(int ax, int ay, int bx, int by) {
	int dx = ax - bx, dy = ay - by;
	return dx * dx + dy * dy;
}

// LOS Bresenham tres bon marche
static inline bool aa_los_clear(const map_data* md, int x0, int y0, int x1, int y1) {
	int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy, x = x0, y = y0;
	while (true) {
		if ((unsigned)x >= (unsigned)md->xs || (unsigned)y >= (unsigned)md->ys) return false;
		if (!cell_passable_fast(const_cast<map_data*>(md), (int16)x, (int16)y)) return false;
		if (x == x1 && y == y1) break;
		int e2 = err << 1;
		if (e2 >= dy) { err += dy; x += sx; }
		if (e2 <= dx) { err += dx; y += sy; }
	}
	return true;
}

// Petite perturbation du chemin pour les fake players : on décale
// certains points latéralement pour éviter l'effet file indienne.
static void aa_perturb_fake_path(map_session_data* sd)
{
	if (!sd)
		return;

	map_data* md = map_getmapdata(sd->m);
	if (!md)
		return;

	auto& path = sd->aa.path;
	const size_t n = path.size();
	if (n < 4)
		return; // trop court, on ne touche pas

	// Limite de modifs pour éviter du taf inutile
	int max_deviations = 10;

	for (size_t i = 1; i + 1 < n && max_deviations > 0; ++i) {
		// Probabilité de dévier ce point (30%)
		if (rnd_value(0, 99) >= 80)
			continue;

		int px = (int)std::get<0>(path[i - 1]);
		int py = (int)std::get<1>(path[i - 1]);
		int cx = (int)std::get<0>(path[i]);
		int cy = (int)std::get<1>(path[i]);
		int nx = (int)std::get<0>(path[i + 1]);
		int ny = (int)std::get<1>(path[i + 1]);

		// Direction globale autour de ce point (prev -> next)
		int dx = nx - px;
		int dy = ny - py;
		if (dx < -1) dx = -1; else if (dx > 1) dx = 1;
		if (dy < -1) dy = -1; else if (dy > 1) dy = 1;

		if (dx == 0 && dy == 0)
			continue; // direction dégénérée

		// Vecteur latéral gauche/droite
		// direction (dx,dy) -> latéral (-dy, dx) ou (dy, -dx)
		bool use_left = (rnd_value(0, 1) == 0);
		int sx = cx + (use_left ? -dy : dy);
		int sy = cy + (use_left ? dx : -dx);

		// Bornes carte
		if ((unsigned)sx >= (unsigned)md->xs || (unsigned)sy >= (unsigned)md->ys)
			continue;

		// Case doit être walkable
		if (!cell_passable_fast(md, (int16)sx, (int16)sy))
			continue;

		// Facultatif : éviter de coller un NPC comme dans aa_get_random_coords
		if (!battle_config.teleport_on_portal && npc_check_areanpc(1, sd->m, sx, sy, 1))
			continue;

		// On vérifie qu'on ne casse pas la continuité (pas de gap > 1 case)
		if (aa_sqdist(px, py, sx, sy) > 2)
			continue;
		if (aa_sqdist(sx, sy, nx, ny) > 2)
			continue;

		// OK, on remplace ce point par le point décalé
		std::get<0>(path[i]) = sx;
		std::get<1>(path[i]) = sy;

		--max_deviations;
	}
}

void aa_move_path(map_session_data* sd) {
	if (unit_is_walking(sd)) return;

	t_tick last_tick = gettick();
	if (sd->aa.itempick_id || last_tick < sd->aa.pickup_lock_until) return;
	if (sd->aa.target_id || last_tick < sd->aa.attack_lock_until) return;

	// --- 1. Vérifier si une requête Async est terminée ---
	if (sd->aa.current_job != nullptr) {
		auto& job = sd->aa.current_job;
		if (job->is_done.load(std::memory_order_acquire)) {
			// Le calcul est fini !
			if (job->success &&
				sd->m == job->m &&
				sd->status.char_id == job->char_id) {
				// On récupère le chemin calculé
				sd->aa.path = std::move(sd->aa.current_job->path);
				sd->aa.path_index = 0;

				if (sd->fp.is_fake_player) {
					aa_perturb_fake_path(sd);
				}

				// On lance le mouvement immédiatement
				if (aa_move_to_path(sd->aa.path, sd) == 0) {
					sd->aa.path.clear();
				}
				else {
					clif_walkok(*sd);
				}
			}
			// Qu'il ait réussi ou échoué, on supprime le job (reset pointeur)
			sd->aa.current_job = nullptr;
			return; // On a traité le résultat, on attend le prochain tick pour la suite si besoin
		}
		else {
			return;
		}
	}

	// --- Reutiliser/ameliorer un chemin existant avant de recalculer ---
	if (!sd->aa.path.empty()) {
		map_data* md = map_getmapdata(sd->m);
		if (!md) return;

		auto& path = sd->aa.path;                  // std::vector<std::tuple<int,int>>
		int& pi = sd->aa.path_index;

		if (pi < 0) pi = 0;
		if (pi >= (int)path.size()) { path.clear(); return; }

		// 1) SNAP dans une petite fenetre autour de pi : -3..+3
		constexpr int BACK = 3, AHEAD = 3;
		constexpr int SNAP_MAX_DIST2 = 4; // <= 2 cases

		int best = pi;
		{
			int px = (int)std::get<0>(path[pi]);
			int py = (int)std::get<1>(path[pi]);
			int bestd2 = aa_sqdist(sd->x, sd->y, px, py);

			const int jmin = std::max(0, pi - BACK);
			const int jmax = std::min<int>((int)path.size() - 1, pi + AHEAD);
			for (int j = jmin; j <= jmax; ++j) {
				int jx = (int)std::get<0>(path[j]);
				int jy = (int)std::get<1>(path[j]);
				int d2 = aa_sqdist(sd->x, sd->y, jx, jy);
				// biais leger vers l'avant a distance egale
				if (d2 < bestd2 || (d2 == bestd2 && j > best)) {
					bestd2 = d2; best = j;
				}
			}
			if (best != pi) {
				int bx = (int)std::get<0>(path[best]);
				int by = (int)std::get<1>(path[best]);
				if (aa_sqdist(sd->x, sd->y, bx, by) <= SNAP_MAX_DIST2)
					pi = best;
			}
		}

		// 2) Si le prochain pas est bloque, tenter un skip LOS vers pi+2 ou pi+3
		bool next_blocked = false;
		if (pi + 1 < (int)path.size()) {
			int nx = (int)std::get<0>(path[pi + 1]);
			int ny = (int)std::get<1>(path[pi + 1]);
			if (!cell_passable_fast(md, (int16)nx, (int16)ny))
				next_blocked = true;
		}
		if (next_blocked) {
			constexpr int MAX_SKIP_AHEAD = 3; // teste pi+2 et pi+3
			int furthest = std::min<int>((int)path.size() - 1, pi + MAX_SKIP_AHEAD);
			for (int j = furthest; j >= pi + 2; --j) {
				int jx = (int)std::get<0>(path[j]);
				int jy = (int)std::get<1>(path[j]);
				if (aa_los_clear(md, sd->x, sd->y, jx, jy)) {
					// on place pi juste avant le nœud saute
					pi = j - 1;
					break;
				}
			}
			// sinon: on laisse la suite gerer (move_to_path pourra echouer → clear)
		}

		// 3) Tenter d'avancer sans recalcul
		if (aa_move_to_path(path, sd) == 0) {
			path.clear(); // bloque ou fini → la logique existante relancera A* plus tard
		}
		else {
			clif_walkok(*sd);
		}
		return;
	}

	// --- Pas de chemin : logique d'origine (cible aleatoire + A*) ---
	int max_attempt = 10;
	int tx = 0, ty = 0;
	bool found = false;

	// Trouver une cible aléatoire (toujours sur le main thread, c'est rapide)
	for (int i = 0; i < max_attempt && !found; i++) {
		found = aa_get_random_coords(sd->m, tx, ty);
		int max_dist = 200;
		if (aa_too_far(sd->x, sd->y, tx, ty, max_dist)) {
			// On abandonne ce target, on réessaiera plus tard / un autre tick
			found = false;
		}
	}

	if (found) {
		// AU LIEU DE CALCULER MAINTENANT, ON CRÉE UN JOB
		auto job = std::make_shared<AsyncPathJob>();
		job->account_id = sd->status.account_id;
		job->char_id = sd->status.char_id;
		job->m = sd->m;
		job->start_x = sd->x;
		job->start_y = sd->y;
		job->target_x = tx;
		job->target_y = ty;

		// On l'attache au joueur pour surveiller l'état
		sd->aa.current_job = job;

		// On l'envoie au worker
		g_pathfinder.push_job(job);
	}
}


void aa_status_checkmapchange(map_session_data* sd) {
	if (sd->mapindex != sd->aa.lastposition.map) {

		// Action after tp
		if (sd->state.autotrade) {
			pc_delinvincibletimer(sd);
			clif_parse_LoadEndAck(0, sd);
		}
	}
}

void aa_moblist_reset_mapchange(map_session_data* sd) {
	// Reinit mapindex for mob selection if map changed
	if (sd && sd->aa.mobs.map != sd->mapindex) {
		sd->aa.mobs.id.clear();
		sd->aa.mobs.map = sd->mapindex;
	}
}

bool aa_message(map_session_data* sd, std::string key, char* message, int delay, struct party_data* p) {
	if (!sd)
		return false;

	// Reference au vector party_msg pour une utilisation simplifiee
	auto& msg_list = sd->aa.msg_list;

	if (msg_list.empty()) {
		// Ajouter directement le message et le delai si le vecteur est vide
		msg_list.emplace_back(key, gettick() + delay * 1000);
	}
	else {
		// Rechercher si le message pour cette cle a deja ete envoye
		auto it = std::find_if(msg_list.begin(), msg_list.end(),
			[&key](const std::pair<std::string, t_tick>& msg) { return msg.first == key; });

		if (it != msg_list.end()) {
			// Verifier le delai avant d'envoyer un nouveau message
			if (gettick() < it->second) {
				return false;  // Ne pas envoyer si le delai n'est pas encore atteint
			}
			// Mettre a jour le delai du message
			it->second = gettick() + delay * 1000;
		}
		else {
			// Ajouter une nouvelle entree avec le delai du message
			msg_list.emplace_back(key, gettick() + delay * 1000);
		}
	}

	if (p)
		party_send_message(sd, message, (int)strlen(message) + 1);
	else
		clif_displaymessage(sd->fd, message);

	return true;
}

void aa_getusablepotions(map_session_data* sd, t_itemid* inventory_potion_id, int* inventory_potion_amount, char inventory_potion_name[MAX_INVENTORY][ITEM_NAME_LENGTH + 1], int* amount) {
	*amount = 0;

	if (!sd) return;

	for (int i = 0; i < MAX_INVENTORY; ++i) {
		const auto& inv_item = sd->inventory.u.items_inventory[i];

		if (auto item_data = item_db.find(inv_item.nameid)) {
			if (item_data->type == IT_HEALING) {
				inventory_potion_id[*amount] = inv_item.nameid;
				inventory_potion_amount[*amount] = inv_item.amount;
				safestrncpy(inventory_potion_name[*amount], item_data->ename.c_str(), ITEM_NAME_LENGTH);
				inventory_potion_name[*amount][ITEM_NAME_LENGTH] = '\0'; // Assurer la terminaison de la cha├«ne

				(*amount)++;
			}
		}
	}
}

uint32 aa_getrental_search_inventory(map_session_data* sd, t_itemid nameid) {
	short i;
	uint32 expire_time = 0;
	nullpo_retr(-1, sd);

	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].nameid == nameid) {
			if (sd->inventory.u.items_inventory[i].expire_time > 0) {
				expire_time = sd->inventory.u.items_inventory[i].expire_time;
			}
		}
	}

	return expire_time;
}

int aa_get_random_coords(int16 m, int& x, int& y) {
	int16 i = 0;

	struct map_data* mapdata = map_getmapdata(m);

	int32 edge = battle_config.map_edge_size;
	for (int i = 0; i < 100; i++) {
		x = rnd_value<int16>(edge, mapdata->xs - edge - 1);
		y = rnd_value<int16>(edge, mapdata->ys - edge - 1);

		if (cell_passable_fast(mapdata, x, y) &&
			(battle_config.teleport_on_portal || !npc_check_areanpc(1, m, x, y, 1))) {
			return 1;
		}
	}

	// Reduction de l'echec en elargissant la zone
	for (int i = 0; i < 50; i++) {
		x = rnd_value<int16>(0, mapdata->xs - 1);
		y = rnd_value<int16>(0, mapdata->ys - 1);
		if (cell_passable_fast(mapdata, x, y) &&
			(battle_config.teleport_on_portal || !npc_check_areanpc(1, m, x, y, 1))) {
			return 1;
		}
	}

	return 0;
}

int aa_get_random_coords_near(int16 m, int cx, int cy, int range, int& x, int& y) {
	map_data* md = map_getmapdata(m);
	if (!md) return 0;

	int32 edge = battle_config.map_edge_size;
	int sx = std::max(edge, cx - range);
	int ex = std::min((int)md->xs - edge - 1, cx + range);
	int sy = std::max(edge, cy - range);
	int ey = std::min((int)md->ys - edge - 1, cy + range);

	for (int i = 0; i < 50; ++i) {
		x = rnd_value<int16>(sx, ex);
		y = rnd_value<int16>(sy, ey);

		if (cell_passable_fast(md, x, y) &&
			(battle_config.teleport_on_portal || !npc_check_areanpc(1, m, x, y, 1))) {
			return 1;
		}
	}
	return 0;
}

// Calcul du chemin en algorithme A*
bool algorithm_path_finding(map_session_data* sd, int16_t m,
	int start_x, int start_y, int target_x, int target_y)
{
	if (!sd) return false;
	if (start_x == target_x && start_y == target_y)
		return false;

	const bool is_fake = (sd->fp.is_fake_player != 0);

	// Pour les fake players, on augmente aléatoirement le poids de l'heuristique.
	// 1 => chemin quasi optimal, 2..4 => plus "greedy", moins d'explorations, chemin un peu plus tordu.
	uint32_t h_mul = 1;
	uint8_t neigh_order[8];
	if (is_fake) {
		h_mul = (uint32_t)rnd_value(2, 4); // [2..4]

		for (int i = 0; i < 8; ++i)
			neigh_order[i] = (uint8_t)i;
		// Fisher-Yates shuffle simple
		for (int i = 7; i > 0; --i) {
			int j = rnd_value(0, i);
			std::swap(neigh_order[i], neigh_order[j]);
		}
	}

	// Nettoyage / init
	sd->aa.path.clear();
	sd->aa.path_index = 0;

	map_data* md = map_getmapdata(m);
	if (!md) return false;

	// Si la map du joueur a change entre-temps, on annule (coherence)
	if (sd->m != m) {
		return false;
	}

	const size_t xs = (size_t)md->xs, ys = (size_t)md->ys;
	if (xs <= 0 || ys <= 0) return false;
	const size_t N = xs * ys;

	// Verifier que les coordonnees d'entree sont dans les bornes
	if (start_x < 0 || start_y < 0 || (size_t)start_x >= xs || (size_t)start_y >= ys) {
		return false;
	}
	if (target_x < 0 || target_y < 0 || (size_t)target_x >= xs || (size_t)target_y >= ys) {
		return false;
	}

	// ---- Cache reutilisable par epoch ----
	struct Cache {
		std::vector<uint32_t> stamp;     // "vu" a epoch ?
		std::vector<uint32_t> gscore;    // meilleur g
		std::vector<int32_t>  parent;    // parent
		std::vector<uint32_t> closed;    // "clos" a epoch ?
		std::vector<int32_t>  open_pos;  // position dans le tas si open
		std::vector<uint32_t> open_tag;  // "dans open" a epoch ?
		uint32_t epoch = 1;
		size_t cap = 0;
		void ensure(size_t need) {
			if (need > cap) {
				stamp.assign(need, 0);
				gscore.assign(need, 0);
				parent.assign(need, -1);
				closed.assign(need, 0);
				open_pos.assign(need, -1);
				open_tag.assign(need, 0);
				cap = need; epoch = 1;
			}
			else if (++epoch == 0) {
				// reset lazy si overflow
				std::fill(stamp.begin(), stamp.begin() + cap, 0);
				std::fill(closed.begin(), closed.begin() + cap, 0);
				std::fill(open_pos.begin(), open_pos.begin() + cap, -1);
				std::fill(open_tag.begin(), open_tag.begin() + cap, 0);
				epoch = 1;
			}
		}
	};
	static thread_local Cache C;
	C.ensure(N);

	// Directions en constexpr (evite corruption de pile + plus rapide)
	static constexpr int DX[8] = { -1,-1, 0, 1, 1, 1, 0,-1 };
	static constexpr int DY[8] = { 0,-1,-1,-1, 0, 1, 1, 1 };
	static constexpr int CT[8] = { 10,14,10,14,10,14,10,14 }; // 4/8-dir : 10/14

	// Heuristique octile (deja existante) + pondération pour fake players
	auto make_f = [&](uint32_t g, int nx, int ny) -> uint32_t {
		uint32_t h = (uint32_t)octile_h(nx, ny, target_x, target_y);

		// Pour les fake players, on multiplie l'heuristique : A* devient plus "greedy"
		// => moins de CPU, chemin pas forcément optimal, donc des variantes possibles.
		if (is_fake && h_mul > 1) {
			if (h > UINT32_MAX / h_mul)
				h = UINT32_MAX / h_mul;
			h *= h_mul;
		}

		if (g > UINT32_MAX / 32)
			g = UINT32_MAX / 32; // clamp de sécurité
		return g * 32u + h * 33u;
		};

	// Fast-path: ligne de vue directe
	if (los_clear(md, start_x, start_y, target_x, target_y)) {
		int x0 = start_x, y0 = start_y, x1 = target_x, y1 = target_y;
		int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
		int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
		int err = dx + dy, x = x0, y = y0;
		sd->aa.path.reserve((size_t)std::max(dx, dy) + 2);
		while (true) {
			sd->aa.path.push_back({ x, y });
			if (x == x1 && y == y1) break;
			int e2 = err << 1;
			if (e2 >= dy) { err += dy; x += sx; }
			if (e2 <= dx) { err += dx; y += sy; }
		}
		sd->aa.path_index = 0;

		if (is_fake)
			aa_perturb_fake_path(sd);

		return true;
	}

	const size_t start_i = (size_t)start_y * xs + (size_t)start_x;
	const size_t target_i = (size_t)target_y * xs + (size_t)target_x;

	// ---------- Tas 8-ary indexe + decrease-key (sans pointeurs nus) ----------
	struct MinHeap8 {
		struct Node { uint32_t f; int idx; };

		std::vector<Node> a;
		std::vector<int32_t>& pos; // map idx -> position dans le tas
		std::vector<uint32_t>& tag; // map idx -> open_tag
		uint32_t epoch;

		MinHeap8(std::vector<int32_t>& p, std::vector<uint32_t>& t, uint32_t e)
			: a(), pos(p), tag(t), epoch(e) {}

		inline bool empty() const { return a.empty(); }
		inline void reserve(size_t n) { a.reserve(n); }

		inline void sift_up(size_t i) {
			while (i) {
				size_t p = (i - 1) / 8;
				if (a[p].f <= a[i].f) break;
				std::swap(a[p], a[i]);
				if ((size_t)a[p].idx < pos.size()) pos[a[p].idx] = (int32_t)p;
				if ((size_t)a[i].idx < pos.size()) pos[a[i].idx] = (int32_t)i;
				i = p;
			}
		}
		inline void sift_down(size_t i) {
			size_t sz = a.size();
			while (true) {
				size_t c1 = 8 * i + 1;
				if (c1 >= sz) break;

				size_t m = c1;
				size_t c2 = c1 + 1; if (c2 < sz && a[c2].f < a[m].f) m = c2;
				size_t c3 = c1 + 2; if (c3 < sz && a[c3].f < a[m].f) m = c3;
				size_t c4 = c1 + 3; if (c4 < sz && a[c4].f < a[m].f) m = c4;
				size_t c5 = c1 + 4; if (c5 < sz && a[c5].f < a[m].f) m = c5;
				size_t c6 = c1 + 5; if (c6 < sz && a[c6].f < a[m].f) m = c6;
				size_t c7 = c1 + 6; if (c7 < sz && a[c7].f < a[m].f) m = c7;
				size_t c8 = c1 + 7; if (c8 < sz && a[c8].f < a[m].f) m = c8;

				if (a[m].f >= a[i].f) break;
				std::swap(a[i], a[m]);
				if ((size_t)a[i].idx < pos.size()) pos[a[i].idx] = (int32_t)i;
				if ((size_t)a[m].idx < pos.size()) pos[a[m].idx] = (int32_t)m;
				i = m;
			}
		}

		inline void push_new(Node n) {
			if ((size_t)n.idx >= pos.size() || (size_t)n.idx >= tag.size()) {
				return;
			}
			a.push_back(n);
			size_t i = a.size() - 1;
			pos[n.idx] = (int32_t)i;
			tag[n.idx] = epoch;
			sift_up(i);
		}
		inline void decrease_key(int idx, uint32_t new_f) {
			if (idx < 0 || (size_t)idx >= pos.size()) return;
			int32_t p = pos[idx];
			if (p < 0 || (size_t)p >= a.size()) return;
			Node& ref = a[(size_t)p];
			if (new_f >= ref.f) return;
			ref.f = new_f;
			sift_up((size_t)p);
		}
		inline Node pop_min8() {
			Node res = a[0];
			if ((size_t)res.idx < tag.size()) tag[res.idx] = 0;
			if ((size_t)res.idx < pos.size()) pos[res.idx] = -1;
			size_t last = a.size() - 1;
			std::swap(a[0], a[last]);
			a.pop_back();
			if (!a.empty()) {
				if ((size_t)a[0].idx < pos.size()) pos[a[0].idx] = 0;
				sift_down(0);
			}
			return res;
		}
	};

	MinHeap8 open{ C.open_pos, C.open_tag, C.epoch };

	// Helpers g/open
	auto get_g = [&](size_t idx) -> uint32_t {
		return (C.stamp[idx] == C.epoch) ? C.gscore[idx] : UINT32_MAX;
		};
	auto push_or_decrease = [&](size_t idx, uint32_t newg, int nx, int ny) {
		uint32_t f = make_f(newg, nx, ny);
		if (idx >= C.open_tag.size()) {
			return;
		}
		if (C.open_tag[idx] == C.epoch)
			open.decrease_key((int)idx, f);
		else
			open.push_new(MinHeap8::Node{ f, (int)idx });
		};

	// Init
	C.parent[start_i] = (int32_t)start_i;
	C.stamp[start_i] = C.epoch;
	C.gscore[start_i] = 0;
	push_or_decrease(start_i, 0u, start_x, start_y);

	// Reserve : path + tas
	{
		int rough_len = octile_h(start_x, start_y, target_x, target_y) / 10 + 2;
		if (rough_len < 16) rough_len = 16;
		sd->aa.path.reserve((size_t)rough_len);

		size_t est_open = (size_t)rough_len * 6;
		if (est_open > N) est_open = N;
		open.reserve(est_open);
	}

	size_t max_iters = std::min<size_t>(
		N,
		(size_t)(is_fake ? 200000 : 800000)
	);
	int oct = octile_h(start_x, start_y, target_x, target_y);
	int rough_len = std::max(16, oct / 10 + 2);
	size_t base_budget = (size_t)rough_len * 40; // 40 * longueur estimée

	if (base_budget < 20000) base_budget = 20000;
	if (base_budget > 200000) base_budget = 200000;

	max_iters = std::min(max_iters, base_budget);
	size_t iters = 0;

	while (!open.empty() && iters++ < max_iters) {
		auto cur = open.pop_min8();

		// Securite idx
		if (cur.idx < 0 || (size_t)cur.idx >= N) {
			break;
		}

		C.closed[(size_t)cur.idx] = C.epoch;

		if ((size_t)cur.idx == target_i)
			break;

		const int cx = cur.idx % (int)xs;
		const int cy = cur.idx / (int)xs;

		// Voisins (ordre fixe pour les vrais joueurs, random pour les fakes)
		for (int vi = 0; vi < 8; ++vi) {
			int dir = is_fake ? (int)neigh_order[vi] : vi;

			const int nx = cx + DX[dir];
			const int ny = cy + DY[dir];
			if (nx < 0 || ny < 0 || (size_t)nx >= xs || (size_t)ny >= ys) continue;
			if (!cell_passable_fast(md, (int16)nx, (int16)ny)) continue;

			// Anti corner-cut permissif
			if (DX[dir] && DY[dir]) {
				if (!cell_passable_fast(md, (int16)(cx + DX[dir]), (int16)cy) &&
					!cell_passable_fast(md, (int16)cx, (int16)(cy + DY[dir]))) {
					continue;
				}
			}

			const size_t nidx = (size_t)ny * xs + (size_t)nx;
			if (nidx >= N) continue;          // securite
			if (C.closed[nidx] == C.epoch) continue;

			uint32_t base_g = get_g((size_t)cur.idx);
			if (base_g == UINT32_MAX) continue; // stale improbable, mais safe
			uint32_t cand_g = base_g + (uint32_t)CT[dir];

			if (cand_g < get_g(nidx)) {
				C.parent[nidx] = cur.idx;
				C.stamp[nidx] = C.epoch;
				C.gscore[nidx] = cand_g;
				push_or_decrease(nidx, cand_g, nx, ny);
			}
		}
	}

	// Reconstruct
	if (C.stamp[target_i] != C.epoch)
		return false;

	size_t idx = target_i;
	size_t guard = 0; // anti-cycle
	while (idx != start_i &&
		idx < N &&
		C.parent[idx] != -1 &&
		guard++ < N)
	{
		int x = (int)(idx % xs), y = (int)(idx / xs);
		sd->aa.path.push_back({ x, y });
		idx = (size_t)C.parent[idx];
	}

	if (guard >= N || idx >= N) {
		sd->aa.path.clear();
		return false;
	}

	// Ajouter la case de depart et finaliser
	sd->aa.path.push_back({ start_x, start_y });
	std::reverse(sd->aa.path.begin(), sd->aa.path.end());
	sd->aa.path_index = 0;

	if (is_fake)
		aa_perturb_fake_path(sd);

	return true;
}

// 0 - Path to recalculate - 1 - Walking to next cell - 2 moving
int aa_move_to_path(std::vector<std::tuple<int, int>>& path, map_session_data* sd) {
	int case_to_walk = AA_WALK_CELL;

	// Verifier si le pion est en deplacement
	if (unit_is_walking(sd)) {
		return 2;
	}

	// Verifier si le chemin est termine
	if (path.empty() || sd->aa.path_index >= static_cast<int>(path.size())) {
		return 0;
	}

	// Verifier si le pion est deja a la prochaine case cible
	const auto& current_target = path[sd->aa.path_index];
	if (std::make_tuple(sd->x, sd->y) == current_target) {
		if (sd->aa.path_index + 1 >= static_cast<int>(path.size())) { // plus de case suivante
			return 0;
		}

		case_to_walk = std::min(case_to_walk, static_cast<int>(path.size()) - sd->aa.path_index - 1);
		sd->aa.path_index += case_to_walk; // Avancer a la prochaine case possible

		bool tried = false;
		while (case_to_walk > 1) {
			const auto& next_target = path[sd->aa.path_index];
			int target_x = std::get<0>(next_target);
			int target_y = std::get<1>(next_target);
			tried = true;

			if (unit_walktoxy(sd, target_x, target_y, 4)) {
				return 1;
			}

			// Reduire progressivement la distance si le deplacement echoue
			case_to_walk--;
			sd->aa.path_index--;
		}
		if (!tried) {
			const auto& next_target = path[sd->aa.path_index];
			int tx = std::get<0>(next_target);
			int ty = std::get<1>(next_target);
			if (unit_walktoxy(sd, tx, ty, 4))
				return 1;
		}
	}
	else {
		const auto& next_target = path[path.size() - 1];
		int target_x = std::get<0>(next_target);
		int target_y = std::get<1>(next_target);
		return algorithm_path_finding(sd, sd->m, sd->x, sd->y, target_x, target_y);
	}

	return 0;
}

// 0 = init, 1 = start, 2 = stop
bool aa_changestate_autoattack(map_session_data* sd, int flag) {
	map_data* mapdata;
	map_session_data* pl_sd;
	struct unit_data* ud = unit_bl2ud(sd);
	struct s_mapiterator* iter;
	int ip_limitation = 0, gepard_limitation = 0;

	switch (flag) {
	case 1:
		sd->aa.duration_ = static_cast<int>(pc_readaccountreg(sd, add_str("#aa_duration")));
		mapdata = map_getmapdata(sd->m);
		if (!sd->fp.is_fake_player) {
			if (!battle_config.feature_autoattack_allow_town && mapdata->getMapFlag(MF_TOWN)) {
				std::string msg = "Automessage - Autoattack is not allowed in town!";
				aa_message(sd, "FlagOff", msg.data(), 5, nullptr);
				return false;
			}

			if (!battle_config.feature_autoattack_allow_pvp && mapdata->getMapFlag(MF_PVP)) {
				std::string msg = "Automessage - Autoattack is not allowed in pvp map!";
				aa_message(sd, "FlagOff", msg.data(), 5, nullptr);
				return false;
			}

			if (!battle_config.feature_autoattack_allow_gvg && mapdata_flag_gvg2(mapdata)) {
				std::string msg = "Automessage - Autoattack is not allowed in woe!";
				aa_message(sd, "FlagOff", msg.data(), 5, nullptr);
				return false;
			}

			if (!battle_config.feature_autoattack_allow_bg && mapdata->getMapFlag(MF_BATTLEGROUND)) {
				std::string msg = "Automessage - Autoattack is not allowed in battleground map!";
				aa_message(sd, "FlagOff", msg.data(), 5, nullptr);
				return false;
			}

			if (battle_config.feature_autoattack_iplimit) {
				iter = mapit_getallusers();
				for (pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter))
				{
					if (pl_sd->id != sd->id && session[sd->fd]->client_addr == pl_sd->aa.client_addr && pl_sd->state.autoattack)
						ip_limitation++;

					if (ip_limitation >= battle_config.feature_autoattack_iplimit) {
						std::string msg = "There is already an account using autoattack";
						aa_message(sd, "FlagOff", msg.data(), 5, nullptr);
						mapit_free(iter);
						return false;
					}
				}
				mapit_free(iter);
			}

			if (battle_config.feature_autoattack_gepardlimit) {
				iter = mapit_getallusers();
				for (pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter))
				{
					//Uncomment this if you have gepard and want to set a limit
					//if (pl_sd->id != sd->id && session[sd->fd]->gepard_info.unique_id == pl_sd->aa.unique_id && pl_sd->state.autoattack)
					//	gepard_limitation++;

					if (gepard_limitation >= battle_config.feature_autoattack_gepardlimit) {
						std::string msg = "There is already an account using autoattack";
						aa_message(sd, "FlagOff", msg.data(), 5, nullptr);
						mapit_free(iter);
						return false;
					}
				}
				mapit_free(iter);
			}

			if (battle_config.feature_autoattack_hateffect) {
				for (const auto& effectID : AA_HATEFFECTS) {
					if (effectID <= HAT_EF_MIN || effectID >= HAT_EF_MAX)
						continue;

					auto it = util::vector_get(ud->hatEffects, effectID);

					if (it != ud->hatEffects.end())
						continue;

					ud->hatEffects.push_back(effectID);

					if (!sd->state.connect_new)
						clif_hat_effect_single(*sd, effectID, true);
				}
			}

			if (battle_config.feature_autoattack_prefixname) {
				char temp_name[NAME_LENGTH];
				safestrncpy(temp_name, AA_PREFIX_NAME, sizeof(temp_name));
				strncat(temp_name, sd->status.name, sizeof(temp_name) - strlen(temp_name) - 1);
				safestrncpy(sd->fakename, temp_name, sizeof(sd->fakename));

				clif_name_area(sd);
				if (sd->disguise) // Another packet should be sent so the client updates the name for sd
					clif_name_self(sd);
			}
		}

		sd->state.autoattack = true;
		[[fallthrough]];
	case 0:
		sd->aa.lastposition.map = sd->mapindex;
		sd->aa.lastposition.x = sd->x;
		sd->aa.lastposition.y = sd->y;
		sd->aa.lastposition.dx = 0;
		sd->aa.lastposition.dy = 0;
		sd->aa.last_hit = gettick();
		sd->aa.last_teleport = gettick();
		sd->aa.last_move = gettick();
		sd->aa.last_attack = gettick();
		sd->aa.last_pick = gettick();
		sd->aa.attack_target_id = 0;
		aa_target_change(sd, 0);
		sd->aa.itempick_id = 0;
		sd->aa.path.clear();
		sd->aa.path_index = 0;
		sd->aa.client_addr = session[sd->fd]->client_addr;
		// Uncomment this if you have gepard and want to set a limit
		//sd->aa.unique_id = session[sd->fd]->gepard_info.unique_id;
		break;

	case 2:
		sd->state.autoattack = false;
		sd->aa.current_job.reset();
		if (battle_config.feature_autoattack_prefixname && sd->fakename[0]) {
			sd->fakename[0] = '\0';
			clif_name_area(sd);
			if (sd->disguise)
				clif_name_self(sd);
		}

		if (!sd->state.changemap || sd->state.rewarp || sd->state.debug_remove_map) {
			if (sd->aa.action_on_end == 1) {
				pc_setpos(sd, mapindex_name2id(sd->status.save_point.map), sd->status.save_point.x, sd->status.save_point.y, CLR_TELEPORT); // return to save point
				sd->aa.lastposition.x = sd->status.save_point.x;
				sd->aa.lastposition.y = sd->status.save_point.y;
				sd->aa.lastposition.map = sd->mapindex;

				if (sd->state.autotrade) {
					pc_delinvincibletimer(sd);
					clif_parse_LoadEndAck(0, sd);
				}
			}

			if (sd->aa.action_on_end == 2) { //logout
				if (session_isActive(sd->fd))
					clif_authfail_fd(sd->fd, 10);
				else
					map_quit(sd);
			}
		}


		if (battle_config.feature_autoattack_hateffect) {
			for (const auto& effectID : AA_HATEFFECTS) {
				if (effectID <= HAT_EF_MIN || effectID >= HAT_EF_MAX)
					continue;

				auto it = util::vector_get(ud->hatEffects, effectID);

				if (it == ud->hatEffects.end())
					continue;

				util::vector_erase_if_exists(ud->hatEffects, effectID);

				if (!sd->state.connect_new)
					clif_hat_effect_single(*sd, effectID, false);
			}
		}
		break;
	}

	return true;
}

bool aa_party_request(map_session_data* sd) {
	party_join(*sd, sd->party_invite);
	return true;
}

#ifdef AA_WALK_DEBUG

static inline bool aa_pick_random_passable(const map_data* md, int& x, int& y, std::mt19937& rng) {
	if (!md) return false;
	std::uniform_int_distribution<int> dx(0, md->xs - 1);
	std::uniform_int_distribution<int> dy(0, md->ys - 1);
	for (int tries = 0; tries < 1000; ++tries) {
		int tx = dx(rng), ty = dy(rng);
		if (cell_passable_fast(const_cast<map_data*>(md), (int16)tx, (int16)ty)) { x = tx; y = ty; return true; }
	}
	return false;
}

void aa_benchmark_paths(map_session_data* sd, int num_calls, int min_dist, uint32_t seed) {
	map_data* md = map_getmapdata(sd->m);
	if (!md || num_calls <= 0) {
		ShowInfo("aa_bench: invalid map or num_calls\n");
		return;
	}
	std::mt19937 rng(seed);

	// Prepare un petit banc de points valides pour eviter 1000 tests/iteration
	const int POOL = 512; // suffisant
	std::vector<std::tuple<int, int>> pool;
	pool.reserve(POOL);
	for (int i = 0; i < POOL; ++i) {
		int x, y;
		if (aa_pick_random_passable(md, x, y, rng)) pool.emplace_back(x, y);
	}
	if ((int)pool.size() < 32) {
		ShowInfo("aa_bench: not enough passable cells\n");
		return;
	}
	std::uniform_int_distribution<int> pick(0, (int)pool.size() - 1);

	// Warmup (stabilise caches/alloc)
	for (int i = 0; i < 128; ++i) {
		auto [sx, sy] = pool[pick(rng)];
		auto [tx, ty] = pool[pick(rng)];
		if (std::abs(tx - sx) + std::abs(ty - sy) < min_dist) { --i; continue; }
		algorithm_path_finding(sd, sd->m, sx, sy, tx, ty);
		sd->aa.path.clear();
	}

	int ok = 0, fail = 0;
	using clock = std::chrono::high_resolution_clock;
	auto t0 = clock::now();

	for (int i = 0; i < num_calls; ++i) {
		auto [sx, sy] = pool[pick(rng)];
		auto [tx, ty] = pool[pick(rng)];
		if (std::abs(tx - sx) + std::abs(ty - sy) < min_dist) { --i; continue; }

		bool res = algorithm_path_finding(sd, sd->m, sx, sy, tx, ty);
		if (res) ++ok; else ++fail;

		// evite de laisser grossir le vector
		sd->aa.path.clear();
		sd->aa.path_index = 0;
	}

	auto t1 = clock::now();
	double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
	double per_call = ms / std::max(1, ok + fail);
	double qps = 1000.0 / per_call;

	ShowInfo("aa_bench: calls=%d ok=%d fail=%d  total=%.2f ms  avg=%.3f ms  ~%.1f calls/s\n",
		ok + fail, ok, fail, ms, per_call, qps);
}

#endif
