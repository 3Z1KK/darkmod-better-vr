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
#include "game/vr/VRTools.h"
#include "game/vr/VRHands.h"
#include "game/Inventory/Inventory.h"
#include "game/Inventory/WeaponItem.h"

idCVar vr_toolsReturnToHolster( "vr_toolsReturnToHolster", "1", CVAR_GAME | CVAR_BOOL | CVAR_ARCHIVE, "VR: letting go of a tool (sword, blackjack, lockpick) returns it to its holster. 0 = it drops on the floor" );
idCVar vr_holsterHipDrop( "vr_hipHolsterDrop", "22", CVAR_GAME | CVAR_FLOAT, "VR: hip holsters, distance below the head (units, 1 unit = 2.3 cm)" );
idCVar vr_holsterHipSide( "vr_hipHolsterSide", "7.5", CVAR_GAME | CVAR_FLOAT, "VR: hip holsters, distance to the side of the body centre (units)" );
idCVar vr_holsterHipForward( "vr_hipHolsterForward", "1.5", CVAR_GAME | CVAR_FLOAT, "VR: hip holsters, distance in front of the head (units)" );
idCVar vr_holsterHipRadius( "vr_holsterHipRadius", "7", CVAR_GAME | CVAR_FLOAT, "VR: hip holster reach radius (units)" );
idCVar vr_holsterWristRadius( "vr_holsterWristRadius", "4.5", CVAR_GAME | CVAR_FLOAT, "VR: wrist (lockpick) holster reach radius (units)" );
idCVar vr_holsterShoulderDrop( "vr_shoulderHolsterDrop", "5", CVAR_GAME | CVAR_FLOAT, "VR: shoulder holsters (bow, arrows), distance below the head (units, 1 unit = 2.3 cm)" );
idCVar vr_holsterShoulderSide( "vr_shoulderHolsterSide", "7", CVAR_GAME | CVAR_FLOAT, "VR: shoulder holsters, distance to the side of the body centre (units)" );
idCVar vr_holsterShoulderForward( "vr_shoulderHolsterForward", "-2", CVAR_GAME | CVAR_FLOAT, "VR: shoulder holsters, distance in front of the head (negative = behind)" );
idCVar vr_holsterShoulderRadius( "vr_holsterShoulderRadius", "6.5", CVAR_GAME | CVAR_FLOAT, "VR: shoulder holster reach radius (units)" );
idCVar vr_holsterBodyTurn( "vr_holsterBodyTurn", "40", CVAR_GAME | CVAR_FLOAT, "VR: how far (degrees) the head can turn before the body (and the hip holsters) turns with it" );

namespace {
	struct toolSlot_t {
		const char *	name;			// "vr_tool" spawnarg value
		const char *	spawnDef;
		const char *	drawSound;
		const char *	holsterSound;
		bool			hip;			// hip holster (else wrist)
		bool			left;			// left side / left wrist
	};
	const toolSlot_t SLOTS[idVRTools::NUM_SLOTS] = {
		{ "sword",				"atdm:moveable_shortsword",		"vr_sword_draw",	"vr_sword_sheath",	true,	true },
		{ "blackjack",			"atdm:moveable_blackjack",		"vr_leather_draw",	"vr_leather_holster", true,	false },
		{ "lockpick_triangle",	"atdm:vr_lockpick_triangle",	"vr_pick_draw",		"vr_pick_holster",	false,	true },
		{ "lockpick_snake",		"atdm:vr_lockpick_snake",		"vr_pick_draw",		"vr_pick_holster",	false,	false },
	};

	const char *LOCKPICK_TYPE[idVRTools::NUM_SLOTS] = { nullptr, nullptr, "t", "s" };

	bool HasWeapon( idPlayer *player, const char *name ) {
		CInventoryWeaponItemPtr weapon = player->GetWeaponItem( name );
		return weapon != nullptr && weapon->IsEnabled();
	}
}

void idVRTools::Reset() {
	bodyYawValid = false;
}

void idVRTools::Update( idPlayer *player, float dt ) {
	const usercmd_t &cmd = player->usercmd;
	idVec3 forward = cmd.vrHeadAxis.ToMat3()[0];
	forward.z = 0.0f;
	if ( forward.Normalize() < 0.01f ) {
		return;
	}
	const float headYaw = RAD2DEG( idMath::ATan( forward.y, forward.x ) );
	if ( !bodyYawValid ) {
		bodyYaw = headYaw;
		bodyYawValid = true;
		return;
	}
	// the body follows the head lazily: looking around doesn't swing the hips, turning around does
	float diff = idMath::AngleNormalize180( headYaw - bodyYaw );
	const float maxTurn = vr_holsterBodyTurn.GetFloat();
	if ( diff > maxTurn ) {
		bodyYaw += diff - maxTurn;
		diff = maxTurn;
	} else if ( diff < -maxTurn ) {
		bodyYaw += diff + maxTurn;
		diff = -maxTurn;
	}
	bodyYaw = idMath::AngleNormalize180( bodyYaw + diff * idMath::ClampFloat( 0.0f, 1.0f, dt * 0.7f ) );
}

bool idVRTools::GetHolsterPos( idPlayer *player, int slot, const idVRHand &otherHand, idVec3 &pos ) const {
	const toolSlot_t &s = SLOTS[slot];
	if ( s.hip ) {
		const renderView_t *view = player->GetRenderView();
		if ( view == nullptr || !bodyYawValid ) {
			return false;
		}
		float sy, cy;
		idMath::SinCos( DEG2RAD( bodyYaw ), sy, cy );
		const idVec3 forward( cy, sy, 0.0f );
		const idVec3 left( -sy, cy, 0.0f );
		idVec3 local = player->usercmd.vrHeadPos;
		local += forward * vr_holsterHipForward.GetFloat();
		local += left * ( s.left ? 1.0f : -1.0f ) * vr_holsterHipSide.GetFloat();
		local.z -= vr_holsterHipDrop.GetFloat();
		pos = player->vrBody.LocalToWorld( view, local );
		return true;
	}
	// wrist: in the cuff of the other hand, a little up the forearm from the wrist joint
	if ( !otherHand.IsTracked() ) {
		return false;
	}
	idVec3 jointPos;
	idMat3 jointAxis;
	const idVRHandModel &glove = otherHand.GetGlove();
	if ( glove.GetJointWorld( glove.GetHandJoint(), jointPos, jointAxis ) ) {
		pos = jointPos - jointAxis[0] * 3.0f;
	} else {
		pos = otherHand.GetVisualPos() + idVec3( 0.0f, 0.0f, 5.0f ) * otherHand.GetVisualAxis();
	}
	return true;
}

bool idVRTools::GetShoulderPos( idPlayer *player, bool left, idVec3 &pos ) const {
	const renderView_t *view = player->GetRenderView();
	if ( view == nullptr || !bodyYawValid ) {
		return false;
	}
	float sy, cy;
	idMath::SinCos( DEG2RAD( bodyYaw ), sy, cy );
	const idVec3 forward( cy, sy, 0.0f );
	const idVec3 leftDir( -sy, cy, 0.0f );
	idVec3 local = player->usercmd.vrHeadPos;
	local += forward * vr_holsterShoulderForward.GetFloat();
	local += leftDir * ( left ? 1.0f : -1.0f ) * vr_holsterShoulderSide.GetFloat();
	local.z -= vr_holsterShoulderDrop.GetFloat();
	pos = player->vrBody.LocalToWorld( view, local );
	return true;
}

float idVRTools::GetShoulderRadius() {
	return vr_holsterShoulderRadius.GetFloat();
}

bool idVRTools::HasTool( idPlayer *player, int slot ) const {
	switch ( slot ) {
	case SLOT_SWORD:
		return HasWeapon( player, "shortsword" ) || HasWeapon( player, "sword" ) || HasWeapon( player, "longsword" );
	case SLOT_BLACKJACK:
		return HasWeapon( player, "blackjack" );
	default: {
		const char *type = LOCKPICK_TYPE[slot];
		const CInventoryPtr &inv = player->Inventory();
		if ( inv == nullptr ) {
			return false;
		}
		for ( int c = 0; c < inv->GetNumCategories(); c++ ) {
			CInventoryCategoryPtr category = inv->GetCategory( c );
			if ( category == nullptr ) {
				continue;
			}
			for ( int i = 0; i < category->GetNumItems(); i++ ) {
				CInventoryItemPtr item = category->GetItem( i );
				idEntity *ent = item ? item->GetItemEntity() : nullptr;
				if ( ent != nullptr && idStr::Icmp( ent->spawnArgs.GetString( "lockpick_type" ), type ) == 0 ) {
					return true;
				}
			}
		}
		return false;
	}
	}
}

bool idVRTools::IsOut( int slot ) const {
	for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != nullptr; ent = ent->spawnNode.Next() ) {
		if ( idStr::Icmp( ent->spawnArgs.GetString( "vr_tool" ), SLOTS[slot].name ) == 0 ) {
			return true;
		}
	}
	return false;
}

bool idVRTools::IsTool( const idEntity *ent, int *slot ) {
	if ( ent == nullptr ) {
		return false;
	}
	const char *name = ent->spawnArgs.GetString( "vr_tool" );
	if ( name[0] == '\0' ) {
		return false;
	}
	for ( int s = 0; s < NUM_SLOTS; s++ ) {
		if ( idStr::Icmp( name, SLOTS[s].name ) == 0 ) {
			if ( slot ) {
				*slot = s;
			}
			return true;
		}
	}
	return false;
}

/*
 * Where a tool sits in the hand. The tool's long axis comes out of the fist on the thumb side (like a
 * sword hilt), its width lines up with the knuckles. The grip point is the end opposite the tip: weapon
 * models usually have their origin at the hilt; if not, the hand goes near the far end.
 * Overridable per entity with "vr_grip_origin" (grip point, model space) and "vr_grip_angles".
 */
void idVRTools::GetSnapGrip( idEntity *ent, idVec3 &anchor, idMat3 &handAxis ) {
	if ( ent->spawnArgs.FindKey( "vr_grip_origin" ) || ent->spawnArgs.FindKey( "vr_grip_angles" ) ) {
		anchor = ent->spawnArgs.GetVector( "vr_grip_origin", "0 0 0" );
		handAxis = ent->spawnArgs.GetAngles( "vr_grip_angles", "0 0 0" ).ToMat3();
		return;
	}
	const idBounds &b = ent->GetPhysics()->GetBounds();
	const idVec3 size = b[1] - b[0];
	int longAxis = 0;
	for ( int i = 1; i < 3; i++ ) {
		if ( size[i] > size[longAxis] ) {
			longAxis = i;
		}
	}
	const int a1 = ( longAxis + 1 ) % 3, a2 = ( longAxis + 2 ) % 3;
	const int widthAxis = size[a1] >= size[a2] ? a1 : a2;
	// the tip is the end further away from the origin
	const float sign = idMath::Fabs( b[1][longAxis] ) >= idMath::Fabs( b[0][longAxis] ) ? 1.0f : -1.0f;
	const float hiltEnd = sign > 0 ? b[0][longAxis] : b[1][longAxis];
	const float length = size[longAxis];

	anchor.Zero();
	if ( idMath::Fabs( hiltEnd ) > length * 0.3f ) {
		// the origin is not near an end: grip a little in from the end opposite the tip
		anchor[longAxis] = hiltEnd + sign * length * 0.12f;
	}

	idVec3 L( 0, 0, 0 ), W( 0, 0, 0 );
	L[longAxis] = sign;
	W[widthAxis] = 1.0f;
	const idMat3 A( L, W, L.Cross( W ) );				// rows: tool directions (tool space)
	const idMat3 C( idVec3( 1, 0, 0 ), idVec3( 0, 0, 1 ), idVec3( 0, -1, 0 ) );	// rows: where they go in the hand
	handAxis = C.Transpose() * A;
}

bool idVRTools::TryDraw( idPlayer *player, idVRHand &hand, int handIndex, const idVRHand &otherHand ) {
	if ( !hand.IsTracked() ) {
		return false;
	}
	const idVec3 handPos = hand.GetInput().pos;
	int best = -1;
	float bestDist = idMath::INFINITY;
	for ( int s = 0; s < NUM_SLOTS; s++ ) {
		const toolSlot_t &slot = SLOTS[s];
		if ( !slot.hip ) {
			// a lockpick is pulled from the other hand's wrist
			const bool pickIsInLeftWrist = slot.left;
			if ( ( handIndex == 0 ) == pickIsInLeftWrist ) {
				continue;
			}
		}
		idVec3 pos;
		if ( !GetHolsterPos( player, s, otherHand, pos ) ) {
			continue;
		}
		const float radius = slot.hip ? vr_holsterHipRadius.GetFloat() : vr_holsterWristRadius.GetFloat();
		const float dist = ( pos - handPos ).LengthFast();
		if ( dist < radius && dist < bestDist ) {
			best = s;
			bestDist = dist;
		}
	}
	if ( best < 0 ) {
		return false;
	}
	// nothing there if the player doesn't own it, or it is lying around somewhere
	if ( !HasTool( player, best ) || IsOut( best ) ) {
		return false;
	}

	const toolSlot_t &slot = SLOTS[best];
	idDict args;
	args.Set( "classname", slot.spawnDef );
	args.Set( "vr_tool", slot.name );
	args.Set( "vr_weightless", "1" );
	args.Set( "frobable", "1" );
	args.Set( "grabable", "1" );
	args.SetVector( "origin", handPos );
	idEntity *ent = nullptr;
	if ( !gameLocal.SpawnEntityDef( args, &ent ) || ent == nullptr ) {
		gameLocal.Warning( "VR tools: could not spawn %s", slot.spawnDef );
		return false;
	}

	// into the hand, already in its grip
	idVec3 anchor;
	idMat3 handAxisInTool;
	GetSnapGrip( ent, anchor, handAxisInTool );
	const idMat3 toolAxis = handAxisInTool.Transpose() * hand.GetInput().axis;
	ent->SetAxis( toolAxis );
	ent->SetOrigin( handPos - anchor * toolAxis );
	hand.HoldEntity( player, ent, 0, anchor, handAxisInTool, idVec3( 0, 0, 0 ) );
	PlaySound( player, best, true );
	return true;
}

bool idVRTools::OnRelease( idPlayer *player, idEntity *ent, int handIndex, const idVRHand &hand, const idVRHand &otherHand ) {
	int slot;
	if ( !IsTool( ent, &slot ) ) {
		return false;
	}
	bool atHolster = false;
	idVec3 pos;
	if ( GetHolsterPos( player, slot, otherHand, pos ) ) {
		const float radius = SLOTS[slot].hip ? vr_holsterHipRadius.GetFloat() : vr_holsterWristRadius.GetFloat();
		atHolster = ( pos - hand.GetInput().pos ).LengthFast() < radius;
	}
	if ( !vr_toolsReturnToHolster.GetBool() && !atHolster ) {
		return false;	// falls to the floor
	}
	ent->Hide();
	ent->GetPhysics()->UnlinkClip();
	ent->PostEventMS( &EV_Remove, 0 );
	PlaySound( player, slot, false );
	return true;
}

void idVRTools::PlaySound( idPlayer *player, int slot, bool draw ) const {
	// a local effect only: StartSoundShader doesn't propagate the sound to the AI
	const idSoundShader *shader = declManager->FindSound( draw ? SLOTS[slot].drawSound : SLOTS[slot].holsterSound, false );
	if ( shader != nullptr ) {
		player->StartSoundShader( shader, SND_CHANNEL_ANY, 0, false, nullptr );
	}
}
