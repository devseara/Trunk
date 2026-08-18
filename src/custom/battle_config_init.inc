// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom battle config initializations
 * Place the definition of custom battle configuration initializations in this file.
 *
 * Do not forget to add a comma at the end of the line, if you want to add another config
 *
 * Format:
 *	 { "name", &battle_config.<variable name>, <default value>, <minimum value>, <maximum value> },
 **/


// Sell equipment cap and tax
{ "sell_equip_max",                     &battle_config.sell_equip_max,                500,     0,      INT_MAX,        },
{ "sell_tax_enable",                    &battle_config.sell_tax_enable,                 0,      0,      1,              },
{ "sell_tax_threshold",                 &battle_config.sell_tax_threshold,           499,      0,      INT_MAX,        },
{ "sell_tax_keep_rate_low",             &battle_config.sell_tax_keep_rate_low,         40,      0,    100,              },
{ "sell_tax_keep_rate_high",            &battle_config.sell_tax_keep_rate_high,        60,      0,    100,              },

// @security [Cydh]
{ "security_mode",						&battle_config.security_mode,					SECU_ALL,	SECU_DROP,	SECU_ALL,	},
{ "feature.custom_item_color_packet", &battle_config.feature_custom_item_color_packet, 0, 0, 1 },
{ "feature.custom_grade_color_packet", &battle_config.feature_custom_grade_color_packet, 0, 0, 1 },
{ "feature.custom_extended_name_packet", &battle_config.feature_custom_extended_name_packet, 0, 0, 1 },
