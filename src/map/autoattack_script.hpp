#ifndef AUTOATTACK_SCRIPT_H
#define AUTOATTACK_SCRIPT_H

#define MIN_MOB_DB 1000
#define MAX_MOB_DB 3999
#define MIN_MOB_DB2 20020
#define MAX_MOB_DB2 31999

#include "autoattack.hpp"
#include "pc.hpp"

#include <unordered_map>
#include <functional>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>

#include <common/db.hpp>
#include <common/database.hpp>
#include <common/utilities.hpp>
#include <common/utils.hpp>

//Get string script
void handle_autoattack_heal(std::ostringstream& os_buf, int index, int extra_index, script_state* st, map_session_data* sd);
void handle_autoattack_potions(std::ostringstream& os_buf, int index, script_state* st, map_session_data* sd);
void handle_autoattack_attack(std::ostringstream& os_buf, int index, int extra_index, script_state* st, map_session_data* sd);
void handle_autoattack_buff(std::ostringstream& os_buf, int index, int extra_index, script_state* st, map_session_data* sd);
void handle_autoattack_items(std::ostringstream& os_buf, int index, script_state* st, map_session_data* sd);
void handle_autoattack_potions(std::ostringstream& os_buf, map_session_data* sd);
void handle_autoattack_token_of_siegfried(std::ostringstream& os_buf, map_session_data* sd);
void handle_autoattack_return_to_savepoint(std::ostringstream& os_buf, map_session_data* sd);
void handle_autoattack_party_request(std::ostringstream& os_buf, map_session_data* sd);
void handle_autoattack_priorize_loot_fight(std::ostringstream& os_buf, map_session_data* sd);
void handle_autoattack_sitrest(std::ostringstream& os_buf, int index, script_state* st, map_session_data* sd);
void handle_autoattack_teleport(std::ostringstream& os_buf, int index, script_state* st, map_session_data* sd);
void handle_autoattack_monsterselection(std::ostringstream& os_buf, int index, int extra_index, script_state* st, map_session_data* sd);
void handle_autoattack_itempickup(std::ostringstream& os_buf, int index, int extra_index, script_state* st, map_session_data* sd);

//Start the aa
bool handleAutoattack_fromitem(map_session_data* sd, t_itemid item_id, t_tick max_duration);

//Set script
void handleAutoAttackHeal(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackPotion(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackSkills(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackBuffSkills(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackItems(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackPotionState(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackReturnToSavepoint(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackResetConfig(map_session_data* sd);
void handleAutoAttackTokenOfSiegfried(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackPriorizeLootFight(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackAcceptPartyRequest(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackMeleeAttack(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackTeleportFlywing(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackTeleportSkill(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackSitRestHp(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackSitRestSp(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackTeleportMinHp(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackTeleportNoMobMeet(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackIgnoreAggressiveMonster(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackMonsterSelection(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackItemPickupConfiguration(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackItemPickupSelection(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackActionOnEnd(const std::vector<std::string>& result, map_session_data* sd);
void handleAutoAttackMonsterSurround(const std::vector<std::string>& result, map_session_data* sd);

int handleGetautoattackint(map_session_data* sd, const int value);

#endif // AUTOATTACK_SCRIPT_H
