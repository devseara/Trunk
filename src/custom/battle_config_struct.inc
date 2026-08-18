// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

/**
 * Custom battle config structure
 * Place the definition of custom battle configuration structure members in this file.
 *
 * Make sure that the name you want is not already taken by looking into battle.hpp
 * Do not forget to add initilization logic to battle_config_init.inc
 *
 * Format:
 *	 <datatype> name;
 **/

// Sell equipment cap and tax
int32 sell_equip_max;           // Max sell price for equipment (IT_WEAPON/IT_ARMOR/IT_SHADOWGEAR), 0 = disabled
int32 sell_tax_enable;          // Enable tax on non-equipment items (0 = disabled, 1 = enabled)
int32 sell_tax_threshold;       // Price threshold for tiered tax (items <= threshold use rate_low)
int32 sell_tax_keep_rate_low;   // Player keeps X% when price <= threshold (0-100)
int32 sell_tax_keep_rate_high;  // Player keeps X% when price > threshold (0-100)
int32 security_mode;
int32 feature_custom_item_color_packet;
int32 feature_custom_grade_color_packet;
int32 feature_custom_extended_name_packet;

