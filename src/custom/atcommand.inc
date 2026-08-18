// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom Atcommands
 * Place the body of custom atcommands in this file.
 * Format:
 *	ACMD_FUNC(command_name)
 *	{
 *		<code>;
 *	}
 **/

//ACMD_FUNC(newcommand)
//{
//	clif_displaymessage(fd, "It works!");
//	clif_specialeffect(&sd->bl, EF_HEARTCASTING, AREA);
//	return 0;
//}

#include <map/item_color.hpp>

/**
 * @emoteall
 **/
ACMD_FUNC(emoteall) {
	char mes[15];
	map_session_data *pl_sd;
	struct s_mapiterator *iter;
	int16 m;
	int i;

	if (battle_config.min_chat_delay) {
		if (DIFF_TICK(sd->cantalk_tick, gettick()) > 0)
			return 0;
		sd->cantalk_tick = gettick() + battle_config.min_chat_delay;
	}

	if (!*message || sscanf(message, "%14[^\n]", mes) < 1) {
		clif_displaymessage(fd, "Please enter an emotion (usage: @emoteall <emotion>");
		return -1;
	}

	if (message[0] != '/') {
		clif_displaymessage(fd, "Please enter an emotion (usage: @emoteall <emotion>");
		return -1;
	}

	const char* emo[] = {
		"/!", "/?", "/ho", "/lv", "/swt", "/ic", "/an", "/ag", "/$", "/...",
		"/scissors", "/rock", "/paper", "/korea", "/lv2", "/thx", "/wah", "/sry", "/heh", "/swt2",
		"/hmm", "/no1", "/??", "/omg", "/O", "/X", "/hlp", "/go", "/sob", "/gg",
		"/kis", "/kis2", "/pif", "/ok", "-?-", "/indonesia", "/bzz", "/rice", "/awsm", "/meh",
		"/shy", "/pat", "/mp", "/slur", "/com", "/yawn", "/grat", "/hp", "/philippines", "/malaysia",
		"/singapore", "/brazil", "/fsh", "/spin", "/sigh", "/dum", "/crwd", "/desp", "/dice", "-dice2",
		"-dice3", "-dice4", "-dice5", "-dice6", "/india", "/love", "/russia", "-?-", "/mobile", "/mail",
		"/chinese", "/antenna1", "/antenna2", "/antenna3", "/hum", "/abs", "/oops", "/spit", "/ene", "/panic",
		"/whisp"
	};

	ARR_FIND(0, ARRAYLENGTH(emo), i, stricmp(message, emo[i]) == 0);
	if (i >= ARRAYLENGTH(emo)) {
		clif_displaymessage(fd, "Invalid emotion (usage: @emoteall <emotion>).");
		return -1;
	}
	if (i == ET_DICE1) i = rnd() % 6 + ET_DICE1;
	m = sd->m;
	iter = mapit_getallusers();
	for (pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter)) {
		if (pl_sd->m != m)
			continue;
		if (pl_sd->emotionlasttime + 1 >= time(NULL)) {
			pl_sd->emotionlasttime = time(NULL);
			continue;
		}
		pl_sd->emotionlasttime = time(NULL);
		clif_emotion(*pl_sd, (emotion_type)i);
	}
	mapit_free(iter);
	return 0;
}

ACMD_FUNC(puball)
{
	map_session_data *pl_sd;
	struct s_mapiterator *iter;

	const char *pass = "";
	int limit = 10;
	int pub = 1;

	if (!message || !*message) {
		clif_displaymessage(fd, "[ System ] Usage: @puball <title> | @puball off");
		return 0;
	}

	bool disable = !strcmp(message, "off");

	iter = mapit_getallusers();

	for (pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter)) {

		// Skip yourself
		if (pl_sd == sd)
			continue;

		// Only players on the same map
		if (pl_sd->m != sd->m)
			continue;

		// Optional: Skip GM/Admin
		if (pc_get_group_level(pl_sd) > 0)
			continue;

		// Turn OFF pubs
		if (disable) {
			if (pl_sd->chatID)
				chat_leavechat(pl_sd, 0);
			continue;
		}

		// Turn ON pubs
		if (pl_sd->chatID)
			chat_changechatstatus(pl_sd, message, pass, limit, pub);
		else
			chat_createpcchat(pl_sd, message, pass, limit, pub);
	}

	mapit_free(iter);

	if (disable)
		clif_displaymessage(fd, "[ System ] All chatrooms on this map closed.");
	else
		clif_displaymessage(fd, "[ System ] All players on this map opened chatrooms.");

	return 0;
}

/*===================================
 * Gold PC / Playtime Points
 *-----------------------------------*/
ACMD_FUNC(goldpc)
{
	char output[128];
	int32 value;
	int64 current;
	int64 new_val;
	nullpo_retr(-1, sd);

	if( !battle_config.feature_goldpc_active ){
		clif_displaymessage(fd, msg_txt(sd, 149)); // Impossible to increase the number/value.
		return -1;
	}

	if( !message || !*message || (value = atoi(message)) == 0 ) {
		current = pc_readparam( sd, SP_GOLDPC_POINTS );
		if( current < 0 )
			current = 0;
		sprintf(output, "Gold PC points: %d / %d", (int32)current, battle_config.feature_goldpc_max_points);
		clif_displaymessage(fd, output);
		return -1;
	}

	current = pc_readparam( sd, SP_GOLDPC_POINTS );
	if( current < 0 )
		current = 0;

	if( value > 0 ) {
		new_val = current + value;
		new_val = cap_value( new_val, 0, battle_config.feature_goldpc_max_points );
		if( pc_setparam( sd, SP_GOLDPC_POINTS, new_val ) ){
			sprintf(output, "Gained %d Gold PC points. Total %d points.", (int32)(new_val - current), (int32)new_val);
			clif_messagecolor(sd, color_table[COLOR_LIGHT_GREEN], output, false, SELF);
		}
		else clif_displaymessage(fd, msg_txt(sd, 149)); // Impossible to increase the number/value.
	} else {
		if( -value > current )
			value = (int32)-current;
		new_val = current + value; // value is negative
		if( new_val < 0 )
			new_val = 0;
		if( pc_setparam( sd, SP_GOLDPC_POINTS, new_val ) ){
			sprintf(output, "Removed %d Gold PC points. Total %d points.", (int32)(-value), (int32)new_val);
			clif_messagecolor(sd, color_table[COLOR_LIGHT_GREEN], output, false, SELF);
		}
		else clif_displaymessage(fd, msg_txt(sd, 41)); // Unable to decrease the number/value.
	}

	return 0;
}


/* Reload item and enchant-grade colors, then refresh connected clients. */
ACMD_FUNC(itemcolorreload)
{
	nullpo_retr(-1, sd);

	do_reload_item_color();
	clif_item_color_list_all();
	clif_displaymessage(fd, "Item and enchant-grade colors reloaded and sent to all players.");
	return 0;
}

/* Set the invoking player's persistent extended display name. */
ACMD_FUNC(extname)
{
	nullpo_retr(-1, sd);

	if (message[0] == '\0') {
		clif_displaymessage(fd, "Usage: @extname <new display name>");
		return -1;
	}

	pc_set_display_name(sd, message, true);

	char output[DISPLAY_NAME_LENGTH + 40];
	snprintf(output, sizeof(output), "Extended display name set to: %s", sd->status.display_name);
	clif_displaymessage(fd, output);
	return 0;
}