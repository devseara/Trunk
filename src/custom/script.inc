// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom Script Commands
 * Place the body of custom script commands in this file.
 * Format:
 *	BUILDIN_FUNC(command_name)
 *	{
 *		<code>;
 *	}
 **/

 #include <map/item_color.hpp>

//BUILDIN_FUNC(example)
//{
//	ShowInfo("buildin_example: It works!\n");
//	script_pushint(st,1);
//	return 0;
//}
/*==========================================
 * Item Transparency (ladder cache, community checker)
 *------------------------------------------*/
static bool itemtransparency_script_parse_group(const char* group$, e_item_transparency_group& out) {
	if (!group$) return false;
	std::string g = group$;
	for (size_t i = 0; i < g.size(); i++)
		g[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(g[i])));
	if (g == "mvp" || g == "mvp_cards" || g == "mvpcards") { out = e_item_transparency_group::MVP_CARDS; return true; }
	if (g == "rare" || g == "rare_items" || g == "rareitems") { out = e_item_transparency_group::RARE_ITEMS; return true; }
	return false;
}

BUILDIN_FUNC(get_itemtransparency_count) {
	const char* group$ = script_getstr(st, 2);
	uint32 item_id = script_getnum(st, 3);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushint(st, 0); return SCRIPT_CMD_SUCCESS; }
	int64 count = item_transparency_get_count(group, item_id);
	if (count < 0) count = 0;
	if (count > INT32_MAX) count = INT32_MAX;
	script_pushint(st, static_cast<int32>(count));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_age) {
	const char* group$ = script_getstr(st, 2);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushint(st, INT32_MAX); return SCRIPT_CMD_SUCCESS; }
	int32 age = item_transparency_get_age_sec(group);
	if (age < 0) age = 0;
	script_pushint(st, age);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_size) {
	const char* group$ = script_getstr(st, 2);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushint(st, 0); return SCRIPT_CMD_SUCCESS; }
	script_pushint(st, static_cast<int32>(item_transparency_get_size(group)));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_itemid) {
	const char* group$ = script_getstr(st, 2);
	uint32 index = script_getnum(st, 3);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushint(st, 0); return SCRIPT_CMD_SUCCESS; }
	script_pushint(st, static_cast<int32>(item_transparency_get_itemid(group, index)));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(itemtransparency_refresh) {
	item_transparency_force_refresh();
	script_pushint(st, 1);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_has_item) {
	const char* group$ = script_getstr(st, 2);
	uint32 item_id = script_getnum(st, 3);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushint(st, 0); return SCRIPT_CMD_SUCCESS; }
	script_pushint(st, item_transparency_has_item(group, item_id) ? 1 : 0);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_holder_count) {
	const char* group$ = script_getstr(st, 2);
	uint32 item_id = script_getnum(st, 3);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushint(st, 0); return SCRIPT_CMD_SUCCESS; }
	script_pushint(st, static_cast<int32>(item_transparency_get_holder_count(group, item_id)));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_holder_refine) {
	const char* group$ = script_getstr(st, 2);
	uint32 item_id = script_getnum(st, 3);
	uint32 index = script_getnum(st, 4);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushint(st, 0); return SCRIPT_CMD_SUCCESS; }
	script_pushint(st, item_transparency_get_holder_refine(group, item_id, index));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_holder_name) {
	const char* group$ = script_getstr(st, 2);
	uint32 item_id = script_getnum(st, 3);
	uint32 index = script_getnum(st, 4);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushstrcopy(st, ""); return SCRIPT_CMD_SUCCESS; }
	script_pushstrcopy(st, item_transparency_get_holder_name(group, item_id, index));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_holder_source) {
	const char* group$ = script_getstr(st, 2);
	uint32 item_id = script_getnum(st, 3);
	uint32 index = script_getnum(st, 4);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushstrcopy(st, ""); return SCRIPT_CMD_SUCCESS; }
	script_pushstrcopy(st, item_transparency_get_holder_source(group, item_id, index));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_holder_nameid) {
	const char* group$ = script_getstr(st, 2);
	uint32 item_id = script_getnum(st, 3);
	uint32 index = script_getnum(st, 4);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group)) { script_pushint(st, 0); return SCRIPT_CMD_SUCCESS; }
	script_pushint(st, static_cast<int32>(item_transparency_get_holder_nameid(group, item_id, index)));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(get_itemtransparency_holder_card) {
	const char* group$ = script_getstr(st, 2);
	uint32 item_id = script_getnum(st, 3);
	uint32 index = script_getnum(st, 4);
	uint32 slot = script_getnum(st, 5);
	e_item_transparency_group group;
	if (!itemtransparency_script_parse_group(group$, group) || slot > 3) { script_pushint(st, 0); return SCRIPT_CMD_SUCCESS; }
	script_pushint(st, static_cast<int32>(item_transparency_get_holder_card(group, item_id, index, static_cast<uint8>(slot))));
	return SCRIPT_CMD_SUCCESS;
}


/*==========================================
* Get security status of player
* is_security {<char_id>};
* @return sd->state.security
*------------------------------------------*/
BUILDIN_FUNC(is_security) {
	class map_session_data* sd;

	if (script_hasdata(st,2))
		sd = map_charid2sd(script_getnum(st,2));
	else if (!script_rid2sd(sd))
		sd = NULL;

	if (sd == NULL) {
		ShowError("buildin_is_security: Failed to check player's security state, no player attached!\n");
		script_pushint(st,-1);
		return SCRIPT_CMD_FAILURE;
	}
	script_pushint(st,sd->state.security);
	return SCRIPT_CMD_SUCCESS;
}

/** [Cydh]
* Set security status of player
* setsecurity <status>{,<char_id>};
* @param status 1 : enable. 0 : disable
*/
BUILDIN_FUNC(setsecurity) {
	class map_session_data* sd;

	if (script_hasdata(st,3))
		sd = map_charid2sd(script_getnum(st,3));
	else if (!script_rid2sd(sd))
		sd = NULL;

	if (sd == NULL) {
		ShowError("buildin_setsecurity: Failed to set player's security state, no player attached or target offline!\n");
		script_pushint(st,-1);
		return SCRIPT_CMD_FAILURE;
	}

	switch (script_getnum(st,2)) {
		case 1:
			sd->state.security = 1;
			clif_displaymessage(sd->fd, msg_txt(sd, 10344));
			break;
		default:
			sd->state.security = 0;
			clif_displaymessage(sd->fd, msg_txt(sd, 10345));
			break;
	}

	script_pushint(st,1);
	return SCRIPT_CMD_SUCCESS;
}

/** [Cydh] @security audit log
 * security_log "<action>";
 * Logs security events (unlock_success, unlock_fail, code_change, code_remove, lockout_triggered)
 */
BUILDIN_FUNC(security_log) {
	map_session_data* sd;
	if (!script_rid2sd(sd))
		return SCRIPT_CMD_FAILURE;
	const char* action = script_getstr(st, 2);
	if (!action || !*action)
		return SCRIPT_CMD_FAILURE;
	log_security_audit(sd, action);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(itemcolorreload)
{
	do_reload_item_color();
	clif_item_color_list_all();
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(setitemcolor)
{
	item_color_set(script_getstr(st, 2), static_cast<uint32>(script_getnum(st, 3)));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(setnpcdisplayname)
{
	const char* npc_name = script_getstr(st, 2);
	const char* display_name = script_getstr(st, 3);
	npc_data* nd = npc_name2id(npc_name);

	if (nd == nullptr) {
		ShowError("buildin_setnpcdisplayname: NPC '%s' not found.\n", npc_name);
		return SCRIPT_CMD_FAILURE;
	}

	safestrncpy(nd->display_name, display_name, DISPLAY_NAME_LENGTH);
	clif_clearunit_area(*nd, CLR_OUTSIGHT);
	clif_spawn(nd);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(setextname)
{
	map_session_data* sd = nullptr;

	if (script_hasdata(st, 3)) {
		if (!script_charid2sd(3, sd))
			return SCRIPT_CMD_FAILURE;
	} else if (!script_rid2sd(sd)) {
		return SCRIPT_CMD_FAILURE;
	}

	pc_set_display_name(sd, script_getstr(st, 2), true);
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(getextname)
{
	map_session_data* sd = nullptr;

	if (script_hasdata(st, 2)) {
		if (!script_charid2sd(2, sd)) {
			script_pushconststr(st, "");
			return SCRIPT_CMD_FAILURE;
		}
	} else if (!script_rid2sd(sd)) {
		script_pushconststr(st, "");
		return SCRIPT_CMD_FAILURE;
	}

	script_pushstrcopy(st, sd->status.display_name);
	return SCRIPT_CMD_SUCCESS;
}