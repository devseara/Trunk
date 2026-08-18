// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom Script Commands
 * Place the definition of custom script commands in this file.
 * Format:
 *	BUILDIN_DEF(command_name,"parameters"),
 **/

//BUILDIN_DEF(example,""),

// Item Transparency (ladder cache, community checker)
BUILDIN_DEF(get_itemtransparency_count, "si"),
BUILDIN_DEF(get_itemtransparency_age, "s"),
BUILDIN_DEF(get_itemtransparency_size, "s"),
BUILDIN_DEF(get_itemtransparency_itemid, "si"),
BUILDIN_DEF(itemtransparency_refresh, ""),
BUILDIN_DEF(get_itemtransparency_has_item, "si"),
BUILDIN_DEF(get_itemtransparency_holder_count, "si"),
BUILDIN_DEF(get_itemtransparency_holder_refine, "sii"),
BUILDIN_DEF(get_itemtransparency_holder_name, "sii"),
BUILDIN_DEF(get_itemtransparency_holder_source, "sii"),
BUILDIN_DEF(get_itemtransparency_holder_nameid, "sii"),
BUILDIN_DEF(get_itemtransparency_holder_card, "siii"),

BUILDIN_DEF(is_security,"?"), //@security [Cydh]
BUILDIN_DEF(setsecurity,"i?"), //@security [Cydh]
BUILDIN_DEF(security_log,"s"), //@security audit log
BUILDIN_DEF(itemcolorreload, ""),
BUILDIN_DEF(setitemcolor, "si"),
BUILDIN_DEF(setnpcdisplayname, "ss"),
BUILDIN_DEF(setextname, "s?"),
BUILDIN_DEF(getextname, "?"),
