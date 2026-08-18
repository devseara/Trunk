// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// @security system - centralized security check [Cydh]

#ifndef SECURITY_HPP
#define SECURITY_HPP

#include <common/cbasetypes.hpp>

class map_session_data;

/// @security: Check if player's security lock blocks the given action
/// @param sd Player session
/// @param flag e_security_check flag (from pc.hpp)
/// @param notice If true, show "You are not authorized to..." message
/// @return true if security is active (action blocked), false if allowed
bool security_check(const map_session_data* sd, int32 flag, bool notice);

#endif /* SECURITY_HPP */