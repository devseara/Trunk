// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// @security system - centralized security check [Cydh]

#include "security.hpp"

#include <common/strlib.hpp>

#include "battle.hpp"
#include "clif.hpp"
#include "map.hpp"
#include "pc.hpp"

bool security_check(const map_session_data* sd, int32 flag, bool notice)
{
	if (!sd || !(battle_config.security_mode & flag) || !sd->state.security)
		return false;

	if (notice) {
		char output[CHAT_SIZE_MAX];
		const char* action;

		switch (flag) {
			case SECU_DROP:             action = "drop item(s)"; break;
			case SECU_VENDING:          action = "buy item from vending"; break;
			case SECU_VENDING_OPEN:     action = "open a new shop"; break;
			case SECU_BUYINGSTORE:      action = "sell item to buying store"; break;
			case SECU_BUYINGSTORE_OPEN: action = "open a buying store"; break;
			case SECU_TRADE:            action = "request transaction"; break;
			case SECU_GUILD_STORAGE:    action = "add/put item to/from guild storage"; break;
			case SECU_BREAKGUILD:       action = "break a guild"; break;
			case SECU_RESET_ITEM:       action = "clear inventory/cart/storage"; break;
			case SECU_NPCTRADE:         action = "do transaction with NPC"; break;
			case SECU_REMOVE_OPT:       action = "remove option"; break;
			case SECU_COMPOUND:         action = "compound a card"; break;
			case SECU_DELHOMUN:         action = "delete homunculus"; break;
			case SECU_MAIL:             action = "use mail"; break;
			case SECU_AUCTION:          action = "open auction"; break;
			case SECU_RESET_SKILL_STAT: action = "reset skill/stats"; break;
			case SECU_FEEDING:          action = "feed homunculus/pet"; break;
			default:                    action = "do this action"; break;
		}
		safesnprintf(output, sizeof(output), msg_txt(sd, 10346), action);
		clif_messagecolor(sd, 0xFF0000, output, true, SELF);
	}
	return true;
}