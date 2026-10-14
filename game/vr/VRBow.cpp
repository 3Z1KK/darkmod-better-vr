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
#include "game/vr/VRBow.h"
#include "game/vr/VRHands.h"
#include "game/Projectile.h"
#include "game/Moveable.h"
#include "game/Inventory/Inventory.h"
#include "game/Inventory/WeaponItem.h"

extern idCVar tdm_arrow_minPower;
extern idCVar tdm_arrow_maxPower;
extern idCVar tdm_arrow_tiredTime;
extern idCVar tdm_arrow_startTiredTime;

idCVar vr_bowMaxDraw( "vr_bowMaxDraw", "19.5", CVAR_GAME | CVAR_FLOAT, "VR: how far the bow string can be drawn at most (units, 1 unit = 2.3 cm; 19.5 = 45 cm, the arrow's tip then reaches the bow); it gets stiffer towards it" );
idCVar vr_bowStiffFrom( "vr_bowStiffFrom", "0.7", CVAR_GAME | CVAR_FLOAT, "VR: the string follows the hand freely up to this fraction of the full draw, then gets stiffer and stiffer" );
idCVar vr_bowReadyDraw( "vr_bowReadyDraw", "0.15", CVAR_GAME | CVAR_FLOAT, "VR: an arrow is only shot if the string was drawn at least this far (fraction of the full draw); less is like letting go too early in the normal game" );
idCVar vr_bowFullDraw( "vr_bowFullDraw", "0.92", CVAR_GAME | CVAR_FLOAT, "VR: drawn this far (fraction) shoots with full power" );
idCVar vr_bowGrabReach( "vr_bowGrabReach", "3", CVAR_GAME | CVAR_FLOAT, "VR: how close to the held bow the other hand has to be to take the string (units)" );
idCVar vr_bowNockReach( "vr_bowNockReach", "2", CVAR_GAME | CVAR_FLOAT, "VR: how close to the held bow an arrow has to come to be nocked (units)" );
idCVar vr_bowDebug( "vr_bowDebug", "0", CVAR_GAME | CVAR_BOOL, "VR: print what the bow does" );

namespace {
	const char *BOW_DEF = "atdm:vr_bow";
	const char *ARROW_DEF = "atdm:vr_arrow_broadhead";
	// where the string sits in the drawing hand: between the thumb and the index finger tips (hand space)
	const idVec3 PINCH( 0.6f, 0.0f, -2.6f );

	void BowDebug( const char *fmt, ... ) {
		if ( !vr_bowDebug.GetBool() ) {
			return;
		}
		va_list argptr;
		char text[1024];
		va_start( argptr, fmt );
		idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
		va_end( argptr );
		gameLocal.Printf( "VR bow: %s\n", text );
	}

	// the arrow's model points along +z with the tip at the origin: its back (feather) end
	float ArrowBackZ( idEntity *arrow ) {
		const renderEntity_t *re = arrow->GetRenderEntity();
		if ( re != nullptr && re->hModel != nullptr ) {
			const idBounds b = re->hModel->Bounds();
			if ( !b.IsCleared() && b[0].z < -5.0f ) {
				return b[0].z;
			}
		}
		const idBounds &pb = arrow->GetPhysics()->GetBounds();
		return pb[0].z < -5.0f ? pb[0].z : -30.0f;
	}

	// the arrow held like a pencil: pointing out of the fist (-z of the hand), the thumb along its x
	const idMat3 HAND_IN_ARROW( idVec3( 1, 0, 0 ), idVec3( 0, -1, 0 ), idVec3( 0, 0, -1 ) );
}

void idVRBow::Reset() {
	bowHand = -1;
	stringHand = -1;
	draw = 0.0f;
	drawnSince = 0;
	nocked = nullptr;
}

void idVRBow::Free() {
	if ( renderHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( renderHandle );
	}
	renderHandle = -1;
	if ( jointMats ) {
		Mem_Free16( jointMats );
		jointMats = nullptr;
	}
	loaded = false;
	loadFailed = false;
	model = nullptr;
	Reset();
}

bool idVRBow::Load() {
	if ( loaded ) {
		return true;
	}
	if ( loadFailed ) {
		return false;
	}
	loadFailed = true;
	def = gameLocal.FindEntityDefDict( BOW_DEF, false );
	if ( def == nullptr ) {
		gameLocal.Warning( "VR bow: no %s def", BOW_DEF );
		return false;
	}
	model = renderModelManager->FindModel( def->GetString( "model" ) );
	if ( model == nullptr || model->IsDefaultModel() || model->NumJoints() <= 1 ) {
		gameLocal.Warning( "VR bow: could not load %s", def->GetString( "model" ) );
		model = nullptr;
		return false;
	}
	numJoints = model->NumJoints();
	const idMD5Joint *joints = model->GetJoints();
	const idJointQuat *pose = model->GetDefaultPose();
	parents.SetNum( numJoints );
	restPose.SetNum( numJoints );
	fullPose.SetNum( numJoints );
	for ( int i = 0; i < numJoints; i++ ) {
		parents[i] = joints[i].parent ? int( joints[i].parent - joints ) : -1;
		restPose[i] = pose[i];
		fullPose[i] = pose[i];
		float v[7];
		const char *s = def->GetString( va( "vr_full_%s", joints[i].name.c_str() ), "" );
		if ( s[0] && sscanf( s, "%f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6] ) == 7 ) {
			fullPose[i].t.Set( v[0], v[1], v[2] );
			fullPose[i].q.Set( v[3], v[4], v[5], v[6] );
		}
		if ( joints[i].name.Icmp( "nock_up" ) == 0 ) {
			nockJoints[0] = i;
		} else if ( joints[i].name.Icmp( "nock_lo" ) == 0 ) {
			nockJoints[1] = i;
		}
	}
	bounds[0] = def->GetVector( "vr_bounds_min", "-29 -1 -1" );
	bounds[1] = def->GetVector( "vr_bounds_max", "29 1 10.5" );
	restNock = def->GetVector( "vr_rest_nock", "0 0 9.4" );
	arrowRest = def->GetVector( "vr_arrow_rest", "2.4 0.6 -0.3" );
	nockHeight = def->GetFloat( "vr_nock_height", "2.4" );
	animDraw = def->GetFloat( "vr_anim_draw", "20" );
	jointMats = (idJointMat *)Mem_Alloc16( numJoints * sizeof( idJointMat ) );
	loaded = true;
	loadFailed = false;
	return true;
}

/*
 * The player has a bow when he owns arrows (in the normal game owning arrows means having the bow), or
 * after picking up a bow placed in the map.
 */
bool idVRBow::HasBow( idPlayer *player ) const {
	if ( player->spawnArgs.GetBool( "vr_has_bow", "0" ) ) {
		return true;
	}
	const CInventoryPtr &inv = player->Inventory();
	if ( inv == nullptr ) {
		return false;
	}
	for ( int c = 0; c < inv->GetNumCategories(); c++ ) {
		CInventoryCategoryPtr category = inv->GetCategory( c );
		for ( int i = 0; category != nullptr && i < category->GetNumItems(); i++ ) {
			CInventoryWeaponItemPtr weapon = std::dynamic_pointer_cast<CInventoryWeaponItem>( category->GetItem( i ) );
			if ( weapon == nullptr || !weapon->IsEnabled() ) {
				continue;
			}
			const idDict *wdef = gameLocal.FindEntityDefDict( weapon->GetWeaponDefName().c_str(), false );
			if ( wdef != nullptr && idStr::FindText( wdef->GetString( "weapon_scriptobject" ), "arrow", false ) >= 0 ) {
				return true;
			}
		}
	}
	return false;
}

bool idVRBow::IsWorldBow( const idEntity *ent ) {
	if ( ent == nullptr || !ent->IsType( idMoveable::Type ) ) {
		return false;
	}
	if ( ent->spawnArgs.GetBool( "vr_bow", "0" ) ) {
		return true;
	}
	// the game's bow props: atdm:moveable_shortbow, atdm:moveable_longbow, atdm:moveable_weapon_bow_*
	const idStr cls = ent->spawnArgs.GetString( "classname" );
	return cls.Find( "shortbow", false ) >= 0 || cls.Find( "longbow", false ) >= 0 || cls.Find( "_bow_", false ) >= 0
		|| ( cls.Length() > 4 && cls.Right( 4 ).Icmp( "_bow" ) == 0 );
}

bool idVRBow::IsArrow( const idEntity *ent ) {
	return ent != nullptr && ent->spawnArgs.GetString( "vr_arrow" )[0] != '\0';
}

bool idVRBow::GetShoulder( idPlayer *player, bool left, idVec3 &pos ) const {
	return player->vrHands.GetTools().GetShoulderPos( player, left, pos );
}

void idVRBow::GetBowPose( const idVRHand &hand, idVec3 &origin, idMat3 &axis ) const {
	// bow space is hand space: the bow sits in the drawn glove's fist
	const vrHandInput_t &in = hand.GetInput();
	if ( hand.GetGlove().IsValid() ) {
		hand.GetGlove().ModelTransform( in.pos, in.axis, origin, axis );
	} else {
		origin = in.pos;
		axis = in.axis;
	}
}

bool idVRBow::IsNearBow( const idVec3 &point, float margin ) const {
	const idVec3 local = ( point - bowOrigin ) * bowAxis.Transpose();
	idBounds b = bounds;
	b.ExpandSelf( margin );
	return b.ContainsPoint( local );
}

idVec3 idVRBow::NockLocal() const {
	// the string's middle; held or with an arrow on it, at the nocking point (level with the arrow rest)
	const float height = ( stringHand >= 0 || nocked.GetEntity() != nullptr ) ? nockHeight : restNock.x;
	return idVec3( height, 0.0f, restNock.z + draw );
}

void idVRBow::PlaySound( idPlayer *player, const char *key ) const {
	if ( def == nullptr ) {
		return;
	}
	const idSoundShader *shader = declManager->FindSound( def->GetString( key ), false );
	if ( shader != nullptr ) {
		// heard by the player only, like the normal game's bow handling sounds
		player->StartSoundShader( shader, SND_CHANNEL_ANY, 0, false, nullptr );
	}
}

/*
===============================================================================
	taking and letting go
===============================================================================
*/

bool idVRBow::TryGrab( idPlayer *player, idVRHand hands[2], int handIndex ) {
	idVRHand &hand = hands[handIndex];
	if ( !hand.IsTracked() || hand.GetInteraction() != idVRHand::IK_NONE || !Load() ) {
		return false;
	}
	const idVec3 handPos = hand.GetInput().pos;

	// the other hand holds the bow: grabbing it anywhere takes the string
	if ( bowHand == 1 - handIndex && stringHand < 0 ) {
		GetBowPose( hands[bowHand], bowOrigin, bowAxis );
		if ( IsNearBow( handPos, vr_bowGrabReach.GetFloat() ) ) {
			StartString( player, hands, handIndex );
			return true;
		}
	}
	if ( !HasBow( player ) ) {
		return false;
	}
	const float radius = idVRTools::GetShoulderRadius();
	idVec3 shoulder;
	// the bow on the left shoulder
	if ( bowHand < 0 && GetShoulder( player, true, shoulder ) && ( shoulder - handPos ).LengthFast() < radius ) {
		TakeBow( player, hand, handIndex );
		return true;
	}
	// arrows on the right shoulder (endless for now; the quiver comes later)
	if ( GetShoulder( player, false, shoulder ) && ( shoulder - handPos ).LengthFast() < radius ) {
		return SpawnArrow( player, hand );
	}
	return false;
}

bool idVRBow::TakeWorldBow( idPlayer *player, idVRHand hands[2], int handIndex, idEntity *ent ) {
	if ( !IsWorldBow( ent ) || bowHand >= 0 || !Load() ) {
		return false;
	}
	// the bow placed in the map becomes the VR bow, and the player owns a bow from now on
	player->spawnArgs.SetBool( "vr_has_bow", true );
	ent->Hide();
	ent->GetPhysics()->UnlinkClip();
	ent->GetPhysics()->PutToRest();
	BowDebug( "%s becomes the VR bow", ent->name.c_str() );
	TakeBow( player, hands[handIndex], handIndex );
	return true;
}

void idVRBow::TakeBow( idPlayer *player, idVRHand &hand, int handIndex ) {
	bowHand = handIndex;
	stringHand = -1;
	draw = 0.0f;
	drawnSince = 0;
	hand.BeginBow();
	GetBowPose( hand, bowOrigin, bowAxis );
	PlaySound( player, "snd_take" );
	BowDebug( "hand %d takes the bow", handIndex );
}

void idVRBow::PutAway( idPlayer *player, idVRHand hands[2] ) {
	if ( stringHand >= 0 ) {
		hands[stringHand].EndBowState();
		stringHand = -1;
	}
	// an arrow on the string goes away with the bow
	idEntity *arrow = nocked.GetEntity();
	if ( arrow != nullptr ) {
		arrow->Hide();
		arrow->GetPhysics()->UnlinkClip();
		arrow->PostEventMS( &EV_Remove, 0 );
	}
	nocked = nullptr;
	if ( bowHand >= 0 ) {
		hands[bowHand].EndBowState();
		bowHand = -1;
		PlaySound( player, "snd_put" );
	}
	draw = 0.0f;
	drawnSince = 0;
	if ( renderHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( renderHandle );
		renderHandle = -1;
	}
	BowDebug( "bow put away" );
}

void idVRBow::OnRelease( idPlayer *player, idVRHand hands[2], int handIndex ) {
	if ( handIndex == stringHand ) {
		EndString( player, hands, true );
	} else if ( handIndex == bowHand ) {
		PutAway( player, hands );
	}
}

bool idVRBow::SpawnArrow( idPlayer *player, idVRHand &hand ) {
	idDict args;
	args.Set( "classname", ARROW_DEF );
	args.SetVector( "origin", hand.GetInput().pos );
	idEntity *ent = nullptr;
	if ( !gameLocal.SpawnEntityDef( args, &ent ) || ent == nullptr ) {
		gameLocal.Warning( "VR bow: could not spawn %s", ARROW_DEF );
		return false;
	}
	// held near the feather end, pointing out of the fist
	const idVec3 anchor( 0.0f, 0.0f, ArrowBackZ( ent ) + 3.0f );
	const idMat3 axis = HAND_IN_ARROW.Transpose() * hand.GetInput().axis;
	ent->SetAxis( axis );
	ent->SetOrigin( hand.GetInput().pos - anchor * axis );
	hand.HoldEntity( player, ent, 0, anchor, HAND_IN_ARROW, idVec3( 0, 0, 0 ) );
	const idSoundShader *shader = declManager->FindSound( "vr_leather_draw", false );
	if ( shader != nullptr ) {
		player->StartSoundShader( shader, SND_CHANNEL_ANY, 0, false, nullptr );
	}
	BowDebug( "arrow %s from the shoulder", ent->name.c_str() );
	return true;
}

/*
===============================================================================
	string and arrow
===============================================================================
*/

void idVRBow::StartString( idPlayer *player, idVRHand hands[2], int handIndex ) {
	stringHand = handIndex;
	hands[handIndex].BeginString( nocked.GetEntity() );
	stringStart = ( hands[handIndex].GetInput().pos - bowOrigin ) * bowAxis.Transpose();
	draw = 0.0f;
	drawnSince = 0;
	tiredSoundPlayed = false;
	BowDebug( "hand %d takes the string%s", handIndex, nocked.GetEntity() ? " with the arrow" : "" );
}

void idVRBow::Nock( idPlayer *player, idVRHand hands[2], int handIndex, idEntity *arrow ) {
	nocked = arrow;
	arrowNockZ = ArrowBackZ( arrow );
	PlaySound( player, "snd_nock" );
	BowDebug( "%s nocked", arrow->name.c_str() );
	// the hand that brought the arrow is on the string now
	StartString( player, hands, handIndex );
}

void idVRBow::EndString( idPlayer *player, idVRHand hands[2], bool shoot ) {
	if ( stringHand < 0 ) {
		return;
	}
	const float wasDrawn = draw;
	if ( shoot && nocked.GetEntity() != nullptr ) {
		Shoot( player, hands );
	} else if ( shoot && wasDrawn > vr_bowMaxDraw.GetFloat() * vr_bowReadyDraw.GetFloat() ) {
		PlaySound( player, "snd_release" );		// an empty string snapping back
	}
	hands[stringHand].EndBowState();
	stringHand = -1;
	draw = 0.0f;
	drawnSince = 0;
}

/*
 * Letting go of the string with an arrow on it: the normal game's shot. Start = the arrow's tip, direction =
 * where the arrow points, power from how far the string was drawn (like the normal game's power from how long
 * the button was held). Not drawn far enough = no shot, like letting go too early in the normal game.
 */
bool idVRBow::Shoot( idPlayer *player, idVRHand hands[2] ) {
	idEntity *arrow = nocked.GetEntity();
	if ( arrow == nullptr ) {
		return false;
	}
	const float maxDraw = Max( 1.0f, vr_bowMaxDraw.GetFloat() );
	const float drawn = draw / maxDraw;
	const float ready = vr_bowReadyDraw.GetFloat();
	if ( drawn < ready ) {
		BowDebug( "not drawn enough (%.0f%%), no shot", drawn * 100.0f );
		return false;
	}
	const char *type = arrow->spawnArgs.GetString( "vr_arrow", "broadhead" );
	const idDict *weaponDef = gameLocal.FindEntityDefDict( va( "atdm:weapon_%s", type ), false );
	const char *projName = weaponDef != nullptr ? weaponDef->GetString( "def_projectile", "atdm:projectile_broadhead" ) : "atdm:projectile_broadhead";
	const idDict *projDef = gameLocal.FindEntityDefDict( projName, false );
	if ( projDef == nullptr ) {
		gameLocal.Warning( "VR bow: no projectile %s", projName );
		return false;
	}

	const float minPower = tdm_arrow_minPower.GetFloat();
	const float maxPower = tdm_arrow_maxPower.GetFloat();
	const float full = Max( ready + 0.01f, vr_bowFullDraw.GetFloat() );
	const float power = minPower + ( maxPower - minPower ) * idMath::ClampFloat( 0.0f, 1.0f, ( drawn - ready ) / ( full - ready ) );
	float dmgScale = 1.0f;
	if ( weaponDef != nullptr && weaponDef->GetFloat( "vel_scales_dmg", "0" ) != 0.0f && maxPower > 0.0f ) {
		// like the normal game: damage follows the kinetic energy, the square of the power
		dmgScale = ( power / maxPower ) * ( power / maxPower );
	}

	// the arrow's model has its tip at the origin and points along +z
	const idMat3 arrowAxis = arrow->GetPhysics()->GetAxis();
	const idVec3 dir = arrowAxis[2];
	idVec3 start = arrow->GetPhysics()->GetOrigin();

	// the arrow in the hand goes, the shot arrow comes
	nocked = nullptr;
	arrow->Hide();
	arrow->GetPhysics()->UnlinkClip();
	arrow->PostEventMS( &EV_Remove, 0 );

	// don't start the shot inside a wall the bow is poking into
	trace_t tr;
	gameLocal.clip.TracePoint( tr, bowOrigin, start, MASK_SHOT_RENDERMODEL, player );
	if ( tr.fraction < 1.0f ) {
		start = tr.endpos - dir * 1.0f;
	}

	idEntity *ent = nullptr;
	gameLocal.SpawnEntityDef( *projDef, &ent, false );
	if ( ent == nullptr || !ent->IsType( idProjectile::Type ) ) {
		if ( ent != nullptr ) {
			ent->PostEventMS( &EV_Remove, 0 );
		}
		gameLocal.Warning( "VR bow: %s is not a projectile", projName );
		return false;
	}
	idProjectile *proj = static_cast<idProjectile *>( ent );
	proj->Create( player, start, dir );
	proj->Launch( start, dir, vec3_origin, 0.0f, power, dmgScale );
	proj->m_SetInMotionByActor = player;
	player->AddProjectilesFired( 1 );
	PlaySound( player, "snd_release" );
	BowDebug( "shot %s at %.0f%% draw: power %.2f, damage x%.2f", projName, drawn * 100.0f, power, dmgScale );
	return true;
}

void idVRBow::PlaceArrow() {
	idEntity *arrow = nocked.GetEntity();
	if ( arrow == nullptr ) {
		return;
	}
	// the arrow lies between the string and its rest on the bow, on the side of the bow hand's thumb knuckle
	idVec3 restLocal = arrowRest;
	if ( bowHand == 1 ) {
		restLocal.y = -restLocal.y;
	}
	const idVec3 nockWorld = bowOrigin + NockLocal() * bowAxis;
	const idVec3 restWorld = bowOrigin + restLocal * bowAxis;
	idVec3 dir = restWorld - nockWorld;
	if ( dir.Normalize() < 0.001f ) {
		dir = -bowAxis[2];
	}
	idVec3 up = bowAxis[0] - dir * ( bowAxis[0] * dir );
	if ( up.Normalize() < 0.001f ) {
		up = bowAxis[1];
	}
	const idMat3 axis( up, dir.Cross( up ), dir );
	arrow->SetAxis( axis );
	arrow->SetOrigin( nockWorld - dir * arrowNockZ );
	arrow->GetPhysics()->PutToRest();
}

void idVRBow::Update( idPlayer *player, idVRHand hands[2], float dt ) {
	if ( bowHand < 0 ) {
		return;
	}
	idVRHand &bh = hands[bowHand];
	if ( !bh.IsTracked() || bh.GetInteraction() != idVRHand::IK_BOW ) {
		PutAway( player, hands );
		return;
	}
	GetBowPose( bh, bowOrigin, bowAxis );

	// an arrow touching the bow is nocked, and its hand goes onto the string
	const int other = 1 - bowHand;
	idVRHand &oh = hands[other];
	if ( stringHand < 0 && nocked.GetEntity() == nullptr && oh.GetInteraction() == idVRHand::IK_CARRY && IsArrow( oh.GetHeld() ) ) {
		idEntity *arrow = oh.GetHeld();
		const idMat3 a = arrow->GetPhysics()->GetAxis();
		const idVec3 o = arrow->GetPhysics()->GetOrigin();
		const float backZ = ArrowBackZ( arrow );
		const float reach = vr_bowNockReach.GetFloat();
		bool touches = IsNearBow( oh.GetInput().pos, reach );
		for ( int k = 0; k <= 4 && !touches; k++ ) {
			touches = IsNearBow( o + a[2] * ( backZ * k / 4.0f ), reach );
		}
		if ( touches ) {
			Nock( player, hands, other, arrow );
		}
	}

	if ( stringHand >= 0 ) {
		idVRHand &sh = hands[stringHand];
		const float maxDraw = Max( 1.0f, vr_bowMaxDraw.GetFloat() );
		// how far the hand went back from where it took the string: followed freely at first, then stiffer and
		// stiffer, never past the maximum (the drawn hand stays on the string, the real hand goes on)
		const idVec3 cur = ( sh.GetInput().pos - bowOrigin ) * bowAxis.Transpose();
		const float raw = Max( 0.0f, cur.z - stringStart.z );
		const float before = draw;
		const float freeDraw = maxDraw * idMath::ClampFloat( 0.0f, 0.99f, vr_bowStiffFrom.GetFloat() );
		const float soft = maxDraw - freeDraw;
		draw = raw <= freeDraw ? raw : freeDraw + soft * tanhf( ( raw - freeDraw ) / soft );
		if ( before < maxDraw * 0.3f && draw >= maxDraw * 0.3f ) {
			PlaySound( player, "snd_draw" );
		}

		// held drawn too long: the arms get tired and give up, like the normal game
		if ( draw >= maxDraw * vr_bowReadyDraw.GetFloat() ) {
			if ( drawnSince == 0 ) {
				drawnSince = gameLocal.time;
				tiredSoundPlayed = false;
			}
			const int held = gameLocal.time - drawnSince;
			if ( !tiredSoundPlayed && held > SEC2MS( tdm_arrow_startTiredTime.GetFloat() ) ) {
				player->StartSound( "snd_player_tired", SND_CHANNEL_VOICE, 0, false, nullptr );
				tiredSoundPlayed = true;
			}
			if ( held > SEC2MS( tdm_arrow_tiredTime.GetFloat() ) ) {
				BowDebug( "too tired, the string slips" );
				player->StopSound( SND_CHANNEL_VOICE, false );
				EndString( player, hands, false );
			}
		} else {
			drawnSince = 0;
		}
	}
	if ( stringHand >= 0 ) {
		// the drawing hand is drawn on the string
		idVRHand &sh = hands[stringHand];
		const idVec3 nockWorld = bowOrigin + NockLocal() * bowAxis;
		const idMat3 &handAxis = sh.GetInput().axis;
		sh.SetVisualOverride( nockWorld - PINCH * handAxis, handAxis );
	}
	PlaceArrow();
}

/*
===============================================================================
	drawing the bow
===============================================================================
*/

void idVRBow::PoseJoints() {
	const float bend = idMath::ClampFloat( 0.0f, 1.0f, draw / Max( 1.0f, vr_bowMaxDraw.GetFloat() ) );
	idJointQuat local[64];
	const int n = Min( numJoints, 64 );
	for ( int i = 0; i < n; i++ ) {
		local[i].q.Slerp( restPose[i].q, fullPose[i].q, bend );
		local[i].t = restPose[i].t + ( fullPose[i].t - restPose[i].t ) * bend;
	}
	SIMDProcessor->ConvertJointQuatsToJointMats( jointMats, local, n );
	SIMDProcessor->TransformJoints( jointMats, parents.Ptr(), 1, n - 1 );
	// the string's middle: where the drawing hand is (or at rest)
	const idVec3 nock = NockLocal();
	for ( int k = 0; k < 2; k++ ) {
		if ( nockJoints[k] >= 0 && nockJoints[k] < n ) {
			jointMats[nockJoints[k]].SetTranslation( nock );
		}
	}
}

void idVRBow::UpdateRender( idPlayer *player, idVRHand hands[2] ) {
	if ( bowHand < 0 || !loaded || gameRenderWorld == nullptr ) {
		return;
	}
	PoseJoints();
	memset( &renderEntity, 0, sizeof( renderEntity ) );
	renderEntity.hModel = model;
	renderEntity.origin = bowOrigin;
	renderEntity.axis = bowAxis;
	renderEntity.joints = jointMats;
	renderEntity.numJoints = numJoints;
	idBounds b = bounds;
	b.AddPoint( NockLocal() );
	b.ExpandSelf( 4.0f );
	renderEntity.bounds = b;
	renderEntity.noShadow = true;
	renderEntity.suppressSurfaceInViewID = VID_LIGHTGEM;
	renderEntity.shaderParms[SHADERPARM_RED] = 1.0f;
	renderEntity.shaderParms[SHADERPARM_GREEN] = 1.0f;
	renderEntity.shaderParms[SHADERPARM_BLUE] = 1.0f;
	renderEntity.shaderParms[SHADERPARM_ALPHA] = 1.0f;
	if ( renderHandle == -1 ) {
		renderHandle = gameRenderWorld->AddEntityDef( &renderEntity );
	} else {
		gameRenderWorld->UpdateEntityDef( renderHandle, &renderEntity );
	}
}
