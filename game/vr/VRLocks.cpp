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
#include "game/vr/VRLocks.h"
#include "game/vr/VRHands.h"
#include "game/Inventory/Inventory.h"
#include "game/Inventory/Category.h"
#include "game/BinaryFrobMover.h"
#include "game/FrobDoor.h"
#include "game/FrobLock.h"
#include "game/FrobHandle.h"

idCVar vr_lockReach( "vr_lockReach", "2.5", CVAR_GAME | CVAR_FLOAT, "VR: how close (units, 1 unit = 2.3 cm) a key or lockpick has to be held to the lock mechanism to use it" );
idCVar vr_locksDebug( "vr_locksDebug", "0", CVAR_GAME | CVAR_BOOL, "VR: print what keys and lockpicks are used on" );

namespace {
	const char *KEYS_CATEGORY = "#str_02392";
	const char *LOCKPICKS_CATEGORY = "#str_02389";

	// categories for stand-in items (keys taken out of the pouch are no longer in the inventory)
	idList<CInventoryCategoryPtr> standInCategories;
	CInventoryCategory *StandInCategory( const char *name ) {
		for ( const CInventoryCategoryPtr &c : standInCategories ) {
			if ( c->GetName() == name ) {
				return c.get();
			}
		}
		standInCategories.Append( std::make_shared<CInventoryCategory>( nullptr, name ) );
		return standInCategories[standInCategories.Num() - 1].get();
	}

	bool IsLockable( idEntity *ent ) {
		return ent != nullptr && ( ent->IsType( CBinaryFrobMover::Type ) || ent->IsType( CFrobLock::Type ) );
	}

	// does this lockable have a visible lock mechanism (a handle or lock part that moves when picked)?
	bool HasMechanism( idEntity *lockable ) {
		if ( lockable->IsType( CFrobDoor::Type ) && static_cast<CFrobDoor *>( lockable )->GetDoorhandle() != nullptr ) {
			return true;
		}
		for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != nullptr; ent = ent->spawnNode.Next() ) {
			if ( ent->IsType( CFrobHandle::Type ) && static_cast<CFrobHandle *>( ent )->GetFrobMaster() == lockable ) {
				return true;
			}
		}
		return false;
	}

	void LocksDebug( const char *fmt, ... ) {
		if ( !vr_locksDebug.GetBool() ) {
			return;
		}
		va_list argptr;
		char text[1024];
		va_start( argptr, fmt );
		idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
		va_end( argptr );
		gameLocal.Printf( "VR locks: %s\n", text );
	}
}

bool idVRLockUse::IsWeightless( const idEntity *ent ) {
	if ( ent == nullptr ) {
		return false;
	}
	return ent->spawnArgs.GetBool( "vr_weightless", "0" )
		|| idStr::Icmp( ent->spawnArgs.GetString( "inv_category" ), KEYS_CATEGORY ) == 0
		|| ent->spawnArgs.GetString( "lockpick_type" )[0] != '\0';
}

CInventoryItemPtr idVRLockUse::ItemFor( idPlayer *player, idEntity *held ) {
	if ( held == nullptr ) {
		return CInventoryItemPtr();
	}
	// a VR lockpick stands for the player's own lockpick of that type
	const char *pickType = held->spawnArgs.GetString( "lockpick_type_vr" );
	if ( pickType[0] != '\0' ) {
		const CInventoryPtr &inv = player->Inventory();
		for ( int c = 0; inv != nullptr && c < inv->GetNumCategories(); c++ ) {
			CInventoryCategoryPtr category = inv->GetCategory( c );
			for ( int i = 0; category != nullptr && i < category->GetNumItems(); i++ ) {
				CInventoryItemPtr it = category->GetItem( i );
				idEntity *ent = it ? it->GetItemEntity() : nullptr;
				if ( ent != nullptr && idStr::Icmp( ent->spawnArgs.GetString( "lockpick_type" ), pickType ) == 0 ) {
					return it;
				}
			}
		}
		return CInventoryItemPtr();
	}
	// a key (or other usable item) in the hand: a stand-in inventory item for it
	const char *category = held->spawnArgs.GetString( "inv_category" );
	if ( category[0] == '\0' || !held->spawnArgs.GetBool( "usable", "0" ) ) {
		return CInventoryItemPtr();
	}
	// a key that came off a stack stands for the original key (locks know keys by entity name)
	idEntity *useAs = held;
	const char *original = held->spawnArgs.GetString( "vr_use_as" );
	if ( original[0] != '\0' ) {
		idEntity *orig = gameLocal.FindEntity( original );
		if ( orig != nullptr ) {
			useAs = orig;
		}
	}
	CInventoryItemPtr standIn = std::make_shared<CInventoryItem>( useAs, player );
	standIn->SetCategory( StandInCategory( category ) );
	return standIn;
}

idEntity *idVRLockUse::FindTarget( idPlayer *player, idEntity *held, const CInventoryItemPtr &item ) {
	if ( held == nullptr || item == nullptr ) {
		return nullptr;
	}
	idBounds bounds = held->GetPhysics()->GetAbsBounds();
	bounds.ExpandSelf( vr_lockReach.GetFloat() );
	const idVec3 centre = bounds.GetCenter();

	idClip_ClipModelList clipModels;
	gameLocal.clip.ClipModelsTouchingBounds( bounds, -1, clipModels );

	idEntity *best = nullptr;
	bool bestIsMechanism = false;
	float bestDist = idMath::INFINITY;
	for ( int i = 0; i < clipModels.Num(); i++ ) {
		idEntity *ent = clipModels[i]->GetEntity();
		if ( ent == nullptr || ent == held || ent == player || ent->IsHidden() ) {
			continue;
		}
		idEntity *lockable = nullptr;
		bool mechanism = false;
		if ( ent->IsType( CFrobHandle::Type ) ) {
			// a handle or lock part: the lock mechanism itself
			lockable = static_cast<CFrobHandle *>( ent )->GetFrobMaster();
			mechanism = true;
		} else {
			lockable = IsLockable( ent ) ? ent : ent->GetFrobMaster();
			if ( !IsLockable( lockable ) || HasMechanism( lockable ) ) {
				// things with a visible mechanism only take the tool at the mechanism
				continue;
			}
		}
		if ( !IsLockable( lockable ) || !lockable->CanBeUsedByItem( item, false ) ) {
			continue;
		}
		const float dist = ( clipModels[i]->GetAbsBounds().GetCenter() - centre ).LengthFast();
		if ( ( mechanism && !bestIsMechanism ) || ( mechanism == bestIsMechanism && dist < bestDist ) ) {
			best = lockable;
			bestIsMechanism = mechanism;
			bestDist = dist;
		}
	}
	return best;
}

bool idVRLockUse::Begin( idPlayer *player, idEntity *held ) {
	End( player );
	if ( player->GetImmobilization() & EIM_ITEM_USE ) {
		return false;
	}
	CInventoryItemPtr it = ItemFor( player, held );
	if ( it == nullptr ) {
		return false;
	}
	idEntity *lock = FindTarget( player, held, it );
	if ( lock == nullptr ) {
		LocksDebug( "%s is not held at a lock", held->name.c_str() );
		return false;
	}
	LocksDebug( "using %s on %s", held->name.c_str(), lock->name.c_str() );
	target = lock;
	item = it;
	startTime = gameLocal.time;
	lock->UseByItem( EPressed, item );
	return true;
}

void idVRLockUse::Continue( idPlayer *player, idEntity *held ) {
	idEntity *lock = target.GetEntity();
	if ( lock == nullptr ) {
		return;
	}
	// taken away from the lock: stop, like letting go of the use button
	if ( held == nullptr || FindTarget( player, held, item ) != lock ) {
		End( player );
		return;
	}
	lock->UseByItem( ERepeat, item );
}

void idVRLockUse::End( idPlayer *player ) {
	idEntity *lock = target.GetEntity();
	if ( lock != nullptr && item != nullptr ) {
		lock->UseByItem( EReleased, item );
	}
	target = nullptr;
	item.reset();
}
