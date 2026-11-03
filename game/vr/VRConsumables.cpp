/*****************************************************************************
The Dark Mod GPL Source Code

This file is part of the The Dark Mod Source Code, originally based
on the Doom 3 GPL Source Code as published in 2011.

The Dark Mod Source Code is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version. For details, see LICENSE.TXT.

Project: The Dark Mod (http://www.thedarkmod.com/)

******************************************************************************/

#include "precompiled.h"
#pragma hdrstop

#include "game/Game_local.h"
#include "game/Projectile.h"
#include "game/Grabber.h"
#include "game/Inventory/Inventory.h"
#include "game/vr/VRConsumables.h"
#include "game/vr/VRHands.h"

idCVar vr_mouthDrop( "vr_mouthDrop", "3.5", CVAR_GAME | CVAR_FLOAT, "VR: the mouth (food and potions let go of there are used) is this far below the eyes (units, 1 unit = 2.3 cm)" );
idCVar vr_mouthForward( "vr_mouthForward", "1.5", CVAR_GAME | CVAR_FLOAT, "VR: the mouth is this far in front of the eyes (units)" );
idCVar vr_mouthRadius( "vr_mouthRadius", "3.5", CVAR_GAME | CVAR_FLOAT, "VR: size of the mouth zone (units)" );
idCVar vr_flashbombThrowSpeed( "vr_flashbombThrowSpeed", "150", CVAR_GAME | CVAR_FLOAT, "VR: a flashbomb let go of faster than this (units/s, 150 = 3.5 m/s) is thrown and goes off where it hits; slower it is just dropped" );

namespace {
	void ConsumableDebug( const char *fmt, ... ) {
		if ( !cvarSystem->GetCVarBool( "vr_handsDebug" ) ) {
			return;
		}
		va_list argptr;
		char text[1024];
		va_start( argptr, fmt );
		idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
		va_end( argptr );
		gameLocal.Printf( "VR consumables: %s\n", text );
	}

	void RemoveUsed( idEntity *ent ) {
		ent->Hide();
		ent->GetPhysics()->UnlinkClip();
		ent->PostEventMS( &EV_Remove, 0 );
	}
}

bool idVRConsumables::IsFood( const idEntity *ent ) {
	if ( ent == nullptr ) {
		return false;
	}
	return idStr::Icmp( ent->spawnArgs.GetString( "equip_action_script" ), "eat_it" ) == 0
		|| idStr::Icmp( ent->spawnArgs.GetString( "AIUse" ), "AIUSE_EAT" ) == 0;
}

bool idVRConsumables::IsPotion( const idEntity *ent ) {
	return ent != nullptr && idStr::Cmp( ent->spawnArgs.GetString( "inv_category" ), "#str_02393" ) == 0;	// Potions
}

bool idVRConsumables::IsMine( const idEntity *ent ) {
	return ent != nullptr && ent->spawnArgs.GetString( "inv_name" )[0] != '\0'
		&& ent->spawnArgs.GetString( "def_projectile" )[0] != '\0' && ent->spawnArgs.GetString( "def_armed" )[0] != '\0';
}

bool idVRConsumables::IsFlashbomb( const idEntity *ent ) {
	return ent != nullptr && ent->spawnArgs.GetString( "inv_name" )[0] != '\0'
		&& ent->spawnArgs.GetString( "def_projectile" )[0] != '\0' && ent->spawnArgs.GetString( "def_armed" )[0] == '\0';
}

bool idVRConsumables::AtMouth( idPlayer *player, const idVec3 &handPos ) {
	const renderView_t *view = player->GetRenderView();
	if ( view == nullptr ) {
		return false;
	}
	const idVec3 head = player->vrBody.LocalToWorld( view, player->usercmd.vrHeadPos );
	const idMat3 headAxis = player->usercmd.vrHeadAxis.ToMat3() * view->viewaxis;
	const idVec3 mouth = head + headAxis[0] * vr_mouthForward.GetFloat() - headAxis[2] * vr_mouthDrop.GetFloat();
	return ( handPos - mouth ).LengthSqr() < Square( vr_mouthRadius.GetFloat() );
}

bool idVRConsumables::Eat( idPlayer *player, idEntity *ent ) {
	idStr script = ent->spawnArgs.GetString( "equip_action_script", "" );
	if ( script.IsEmpty() ) {
		script = "eat_it";
	}
	ConsumableDebug( "eats %s (%s)", ent->name.c_str(), script.c_str() );
	idThread *thread = player->CallScriptFunctionArgs( script.c_str(), true, 0, "e", ent );
	if ( thread != nullptr ) {
		thread->Execute();
	}
	return true;
}

bool idVRConsumables::Drink( idPlayer *player, idEntity *ent ) {
	// back into the inventory (the vanilla pick-up, as at the stomach) and used from there the vanilla way, so the
	// potion's own script decides (a health potion at full health stays in the inventory)
	const idStr invName = ent->spawnArgs.GetString( "inv_name" );
	const idStr category = ent->spawnArgs.GetString( "inv_category" );
	player->m_FrobEntity = ent;
	player->PerformFrob( EPressed, ent, false );
	player->m_FrobEntity = nullptr;
	if ( gameLocal.m_Grabber->GetSelected() == ent ) {
		gameLocal.m_Grabber->Update( player, false );
	}
	CInventoryItemPtr item = player->Inventory()->GetItem( invName, category );
	if ( item == nullptr ) {
		ConsumableDebug( "can't drink %s: not in the inventory after picking it up", ent->name.c_str() );
		return false;
	}
	ConsumableDebug( "drinks %s (%s)", ent->name.c_str(), invName.c_str() );
	player->UseInventoryItem( EPressed, item, 0, false );
	player->inventoryHUDNeedsUpdate = true;
	return true;
}

bool idVRConsumables::LaunchProjectile( idPlayer *player, idEntity *ent, const idVec3 &start, const idVec3 &velocity ) {
	const idDict *def = gameLocal.FindEntityDefDict( ent->spawnArgs.GetString( "def_projectile" ), false );
	if ( def == nullptr ) {
		return false;
	}
	idEntity *spawned = nullptr;
	if ( !gameLocal.SpawnEntityDef( *def, &spawned ) || spawned == nullptr ) {
		return false;
	}
	if ( !spawned->IsType( idProjectile::Type ) ) {
		spawned->PostEventMS( &EV_Remove, 0 );
		return false;
	}
	idProjectile *projectile = static_cast<idProjectile *>( spawned );
	idVec3 dir = velocity;
	if ( dir.Normalize() < 1e-3f ) {
		dir.Set( 0, 0, -1 );
	}
	// the player is its owner: it doesn't hit him on the way out
	projectile->Create( player, start, dir );
	projectile->Launch( start, dir, velocity );
	ConsumableDebug( "%s becomes %s", ent->name.c_str(), spawned->name.c_str() );
	return true;
}

bool idVRConsumables::OnRelease( idPlayer *player, idEntity *ent, const vrHandInput_t &input, bool atMouthAllowed ) {
	if ( ent == nullptr ) {
		return false;
	}
	if ( IsFlashbomb( ent ) && input.vel.LengthFast() > vr_flashbombThrowSpeed.GetFloat() ) {
		if ( LaunchProjectile( player, ent, ent->GetPhysics()->GetAbsBounds().GetCenter(), input.vel ) ) {
			RemoveUsed( ent );
			return true;
		}
		return false;
	}
	if ( !atMouthAllowed || !AtMouth( player, input.pos ) ) {
		return false;
	}
	if ( IsPotion( ent ) ) {
		return Drink( player, ent );
	}
	if ( IsFood( ent ) ) {
		// eaten as it leaves the hand (the eat script removes it or turns it into its leftover, which falls)
		ent->GetPhysics()->SetLinearVelocity( vec3_zero );
		ent->GetPhysics()->Activate();
		return Eat( player, ent );
	}
	return false;
}

bool idVRConsumables::UseFromHand( idPlayer *player, idEntity *ent, const idVec3 &handPos ) {
	if ( ent == nullptr ) {
		return false;
	}
	if ( IsMine( ent ) ) {
		// set down where the hand is: straight down, it arms on its own once it lies
		if ( LaunchProjectile( player, ent, ent->GetPhysics()->GetAbsBounds().GetCenter(), vec3_zero ) ) {
			RemoveUsed( ent );
			return true;
		}
	}
	return false;
}
