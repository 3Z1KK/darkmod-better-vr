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
#include "game/vr/VRHands.h"
#include "game/Moveable.h"
#include "game/AFEntity.h"
#include "game/Item.h"
#include "game/ai/AI.h"
#include "game/Grabber.h"
#include "renderer/vr/OpenXRBackend.h"

idCVar vr_handsEnable( "vr_handsEnable", "1", CVAR_GAME | CVAR_BOOL, "VR: interact with the world through tracked hands" );
idCVar vr_handTouchRadius( "vr_handTouchRadius", "2.5", CVAR_GAME | CVAR_FLOAT, "VR: radius (game units) around the palm that counts as touching" );
idCVar vr_handMaxAccel( "vr_handMaxAccel", "9000", CVAR_GAME | CVAR_FLOAT, "VR: maximum acceleration (units/s^2) a hand can give a light held object" );
idCVar vr_handMaxAngAccel( "vr_handMaxAngAccel", "300", CVAR_GAME | CVAR_FLOAT, "VR: maximum angular acceleration (rad/s^2) a hand can give a light held object" );
idCVar vr_handLiftMass( "vr_handLiftMass", "10", CVAR_GAME | CVAR_FLOAT, "VR: objects heavier than this get proportionally weaker acceleration from one hand" );
idCVar vr_handFollowTime( "vr_handFollowTime", "0.05", CVAR_GAME | CVAR_FLOAT, "VR: time (s) a held object takes to close its distance to the hand" );
idCVar vr_handMaxSpeed( "vr_handMaxSpeed", "1500", CVAR_GAME | CVAR_FLOAT, "VR: maximum speed (units/s) a hand can drive a held object to" );
idCVar vr_gripBreakDistance( "vr_gripBreakDistance", "14", CVAR_GAME | CVAR_FLOAT, "VR: grip slips if a held object is this far (units) from where the hand wants it" );
idCVar vr_gripBreakTime( "vr_gripBreakTime", "250", CVAR_GAME | CVAR_INTEGER, "VR: time (ms) a held object may stay too far before the grip slips" );
idCVar vr_gripPressThreshold( "vr_gripPressThreshold", "0.6", CVAR_GAME | CVAR_FLOAT, "VR: grip/trigger value that counts as pressed" );
idCVar vr_gripReleaseThreshold( "vr_gripReleaseThreshold", "0.35", CVAR_GAME | CVAR_FLOAT, "VR: grip/trigger value that counts as released" );
idCVar vr_stomachDrop( "vr_stomachDrop", "20", CVAR_GAME | CVAR_FLOAT, "VR: stomach inventory zone, distance (units) below the head" );
idCVar vr_stomachForward( "vr_stomachForward", "4", CVAR_GAME | CVAR_FLOAT, "VR: stomach inventory zone, distance (units) in front of the head" );
idCVar vr_stomachRadius( "vr_stomachRadius", "9", CVAR_GAME | CVAR_FLOAT, "VR: stomach inventory zone radius (units)" );
idCVar vr_handsDebug( "vr_handsDebug", "0", CVAR_GAME | CVAR_BOOL, "VR: print hand interaction events" );

namespace {
	const char *HAND_MATERIAL = "vr/hand_placeholder";

	idRenderModel *handModels[2] = { nullptr, nullptr };

	/*
	 * A simple mitten/fist shape made from boxes, in hand space:
	 * x = forward along the closed fist, y = left, z = up (back of the hand), origin = centre of the fist,
	 * which is where the controller grip pose sits.
	 */
	void AddBox( srfTriangles_t *tri, const idVec3 &mins, const idVec3 &maxs ) {
		static const int faceAxes[6][3] = {
			// normal axis, sign, (tangent axes follow)
			{ 0, 1, 0 }, { 0, -1, 0 }, { 1, 1, 0 }, { 1, -1, 0 }, { 2, 1, 0 }, { 2, -1, 0 },
		};
		for ( int f = 0; f < 6; f++ ) {
			const int n = faceAxes[f][0];
			const float sign = faceAxes[f][1];
			const int u = ( n + 1 ) % 3;
			const int v = ( n + 2 ) % 3;
			idVec3 normal( 0, 0, 0 );
			normal[n] = sign;
			idVec3 tu( 0, 0, 0 ), tv( 0, 0, 0 );
			tu[u] = 1;
			tv[v] = 1;

			const int base = tri->numVerts;
			for ( int c = 0; c < 4; c++ ) {
				idDrawVert &dv = tri->verts[base + c];
				dv.Clear();
				idVec3 p;
				p[n] = sign > 0 ? maxs[n] : mins[n];
				const bool cu = ( c == 1 || c == 2 );
				const bool cv = ( c >= 2 );
				p[u] = cu ? maxs[u] : mins[u];
				p[v] = cv ? maxs[v] : mins[v];
				dv.xyz = p;
				dv.st.Set( cu ? 1.0f : 0.0f, cv ? 1.0f : 0.0f );
				dv.normal = normal;
				dv.tangents[0] = tu;
				dv.tangents[1] = tv;
				dv.color[0] = dv.color[1] = dv.color[2] = dv.color[3] = 255;
			}
			glIndex_t *idx = tri->indexes + tri->numIndexes;
			// counter-clockwise when looking at the face from outside
			if ( sign > 0 ) {
				idx[0] = base + 0; idx[1] = base + 1; idx[2] = base + 2;
				idx[3] = base + 0; idx[4] = base + 2; idx[5] = base + 3;
			} else {
				idx[0] = base + 0; idx[1] = base + 2; idx[2] = base + 1;
				idx[3] = base + 0; idx[4] = base + 3; idx[5] = base + 2;
			}
			tri->numVerts += 4;
			tri->numIndexes += 6;
		}
		tri->bounds.AddPoint( mins );
		tri->bounds.AddPoint( maxs );
	}

	idRenderModel *GetHandModel( int hand ) {
		if ( handModels[hand] != nullptr ) {
			return handModels[hand];
		}
		// left hand: thumb on the right side (-y), right hand: thumb on the left side (+y)
		const float thumbSide = hand == 0 ? -1.0f : 1.0f;
		const int numBoxes = 3;

		idRenderModel *model = renderModelManager->AllocModel();
		model->InitEmpty( hand == 0 ? "_vr_hand_left" : "_vr_hand_right" );

		modelSurface_t surf;
		surf.id = 0;
		surf.material = declManager->FindMaterial( HAND_MATERIAL );
		surf.geometry = model->AllocSurfaceTriangles( numBoxes * 24, numBoxes * 36 );
		srfTriangles_t *tri = surf.geometry;
		tri->numVerts = 0;
		tri->numIndexes = 0;
		tri->bounds.Clear();

		// fist (the fingers closed around the controller handle)
		AddBox( tri, idVec3( -1.6f, -1.5f, -1.6f ), idVec3( 2.4f, 1.5f, 1.4f ) );
		// back of the hand towards the wrist
		AddBox( tri, idVec3( -4.6f, -1.4f, -1.0f ), idVec3( -1.6f, 1.4f, 1.4f ) );
		// thumb
		idVec3 thumbMins( -0.5f, thumbSide > 0 ? 1.5f : -2.4f, -0.2f );
		idVec3 thumbMaxs( 1.8f, thumbSide > 0 ? 2.4f : -1.5f, 1.2f );
		AddBox( tri, thumbMins, thumbMaxs );

		tri->tangentsCalculated = true;
		tri->facePlanesCalculated = false;
		tri->generateNormals = false;
		model->AddSurface( surf );

		handModels[hand] = model;
		return model;
	}

	bool IsInventoryItem( idEntity *ent ) {
		const char *invName = ent->spawnArgs.GetString( "inv_name", "" );
		return invName != nullptr && invName[0] != '\0';
	}

	void HandsDebug( const char *fmt, ... ) {
		if ( !vr_handsDebug.GetBool() ) {
			return;
		}
		va_list argptr;
		char text[1024];
		va_start( argptr, fmt );
		idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
		va_end( argptr );
		gameLocal.Printf( "VR hands: %s\n", text );
	}

	// the entity that a touch on this entity should act upon (heads etc. belong to their body)
	idEntity *InteractionTarget( idEntity *ent ) {
		if ( ent && ent->IsType( idAFAttachment::Type ) ) {
			idEntity *master = static_cast<idAFAttachment *>( ent )->GetBindMaster();
			if ( master ) {
				return master;
			}
		}
		return ent;
	}
}

/*
===============================================================================
	idVRHand
===============================================================================
*/

void idVRHand::Init( int handIndex ) {
	index = handIndex;
	pos.Zero();
	axis.Identity();
	lastPos.Zero();
	velocity.Zero();
	tracked = false;
	gripDown = false;
	triggerDown = false;
	touched = nullptr;
	touchedBodyId = 0;
	interaction = IK_NONE;
	held = nullptr;
	heldBodyId = 0;
	farFromHandTime = 0;
	stomachRelease = false;
	model = nullptr;
	renderHandle = -1;
	memset( &renderEntity, 0, sizeof( renderEntity ) );
}

void idVRHand::Shutdown() {
	if ( renderHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( renderHandle );
	}
	renderHandle = -1;
	held = nullptr;
	touched = nullptr;
	interaction = IK_NONE;
}

bool idVRHand::CanHold( idEntity *ent ) const {
	if ( !ent->spawnArgs.GetBool( "grabable", "1" ) ) {
		return false;
	}
	if ( ent->IsType( idAI::Type ) ) {
		idAI *ai = static_cast<idAI *>( ent );
		if ( ai->health > 0 && !ai->IsKnockedOut() ) {
			// no grabbing living, conscious people
			return false;
		}
	}
	return ent->IsType( idMoveable::Type ) || ent->IsType( idMoveableItem::Type ) || ent->IsType( idAFEntity_Base::Type );
}

idEntity *idVRHand::FindTouchedEntity( idPlayer *player, int &bodyId ) const {
	static idClipModel *palmClip = nullptr;
	static float palmRadius = -1;
	const float radius = vr_handTouchRadius.GetFloat();
	if ( palmClip == nullptr || palmRadius != radius ) {
		delete palmClip;
		idTraceModel trm;
		trm.SetupDodecahedron( radius * 2 );
		palmClip = new idClipModel( trm );
		palmRadius = radius;
	}

	const int mask = CONTENTS_SOLID | CONTENTS_OPAQUE | CONTENTS_BODY | CONTENTS_CORPSE | CONTENTS_RENDERMODEL | CONTENTS_FROBABLE;
	idBounds bounds( pos );
	bounds.ExpandSelf( radius );

	idClip_ClipModelList clipModels;
	gameLocal.clip.ClipModelsTouchingBounds( bounds, mask, clipModels );

	idEntity *best = nullptr;
	float bestDist = idMath::INFINITY;
	bodyId = 0;
	for ( int i = 0; i < clipModels.Num(); i++ ) {
		idClipModel *cm = clipModels[i];
		idEntity *ent = cm->GetEntity();
		if ( ent == nullptr || ent == player || ent->IsHidden() ) {
			continue;
		}
		idEntity *target = InteractionTarget( ent );
		if ( !target || !target->m_bFrobable || target->IsHidden() ) {
			continue;
		}
		// precise overlap test between the palm and the clip model
		if ( !gameLocal.clip.ContentsModel( pos, palmClip, mat3_identity, -1, cm->Handle(), cm->GetOrigin(), cm->GetAxis() ) ) {
			continue;
		}
		const float dist = ( cm->GetAbsBounds().GetCenter() - pos ).LengthSqr();
		if ( dist < bestDist ) {
			bestDist = dist;
			best = target;
			bodyId = 0;
			if ( target->IsType( idAFEntity_Base::Type ) ) {
				idAFEntity_Base *af = static_cast<idAFEntity_Base *>( target );
				if ( ent == target ) {
					bodyId = af->BodyForClipModelId( cm->GetId() );
				}
				if ( bodyId < 0 ) {
					bodyId = 0;
				}
			}
		}
	}
	return best;
}

void idVRHand::GetHoldTarget( idVec3 &origin, idMat3 &outAxis ) const {
	origin = pos + heldRelOrigin * axis;
	outAxis = heldRelAxis * axis;
}

void idVRHand::StartInteraction( idPlayer *player ) {
	idEntity *ent = touched.GetEntity();
	if ( ent == nullptr ) {
		return;
	}

	const bool inventory = IsInventoryItem( ent );
	const bool holdable = CanHold( ent );
	if ( holdable || inventory ) {
		held = ent;
		heldBodyId = holdable ? touchedBodyId : 0;
		idPhysics *phys = ent->GetPhysics();
		const idVec3 &bodyOrigin = phys->GetOrigin( heldBodyId );
		const idMat3 &bodyAxis = phys->GetAxis( heldBodyId );
		heldRelOrigin = ( bodyOrigin - pos ) * axis.Transpose();
		heldRelAxis = bodyAxis * axis.Transpose();
		farFromHandTime = 0;
		if ( holdable ) {
			interaction = IK_HOLD;
			phys->Activate();
		} else {
			interaction = IK_CARRY;
			carryHomeOrigin = ent->GetPhysics()->GetOrigin();
			carryHomeAxis = ent->GetPhysics()->GetAxis();
		}
		player->vrHands.MakeNonSolidToPlayer( ent );
		HandsDebug( "hand %d %s %s (body %d)", index, interaction == IK_HOLD ? "holds" : "carries", ent->name.c_str(), heldBodyId );
		return;
	}

	// anything else: the entity's normal frob action
	interaction = IK_FROB;
	held = ent;
	player->m_FrobEntity = ent;
	player->PerformFrob( EPressed, ent, true );
	HandsDebug( "hand %d frobs %s", index, ent->name.c_str() );
}

void idVRHand::Release( idPlayer *player, bool allowInventory ) {
	idEntity *ent = held.GetEntity();
	const interactionKind_t kind = interaction;
	interaction = IK_NONE;
	held = nullptr;

	if ( ent == nullptr ) {
		return;
	}

	if ( kind == IK_FROB ) {
		ent->FrobReleased( true, false, 0 );
		player->m_FrobEntity = ent;
		player->PerformFrob( EReleased, ent, true );
		player->m_FrobEntity = nullptr;
		return;
	}

	// is the other hand still holding it? (this hand already let go)
	const bool otherHolds = player->vrHands.CountHolding( ent ) > 0;

	if ( allowInventory && stomachRelease && IsInventoryItem( ent ) && !otherHolds ) {
		HandsDebug( "hand %d stores %s", index, ent->name.c_str() );
		player->vrHands.ReleaseNonSolidToPlayer( ent, true );
		player->m_FrobEntity = ent;
		player->PerformFrob( EPressed, ent, false );
		player->m_FrobEntity = nullptr;
		// make sure nothing ended up in the old grabber
		if ( gameLocal.m_Grabber->GetSelected() == ent ) {
			gameLocal.m_Grabber->Update( player, false );
		}
		return;
	}

	if ( kind == IK_CARRY && !otherHolds ) {
		// static items can't fall; they go back to where they were taken from
		ent->SetOrigin( carryHomeOrigin );
		ent->SetAxis( carryHomeAxis );
	}

	if ( !otherHolds ) {
		player->vrHands.ReleaseNonSolidToPlayer( ent, false );
	}
	HandsDebug( "hand %d releases %s", index, ent->name.c_str() );
}

void idVRHand::UpdateRenderEntity() {
	if ( !gameRenderWorld ) {
		return;
	}
	if ( !tracked ) {
		if ( renderHandle != -1 ) {
			gameRenderWorld->FreeEntityDef( renderHandle );
			renderHandle = -1;
		}
		return;
	}
	if ( model == nullptr ) {
		model = GetHandModel( index );
	}
	memset( &renderEntity, 0, sizeof( renderEntity ) );
	renderEntity.hModel = model;
	renderEntity.origin = pos;
	renderEntity.axis = axis;
	renderEntity.bounds = model->Bounds();
	renderEntity.noShadow = true;
	renderEntity.suppressSurfaceInViewID = VID_LIGHTGEM;
	// faint self illumination so the placeholder hands stay visible in the dark
	renderEntity.shaderParms[SHADERPARM_RED] = 0.05f;
	renderEntity.shaderParms[SHADERPARM_GREEN] = 0.045f;
	renderEntity.shaderParms[SHADERPARM_BLUE] = 0.04f;
	renderEntity.shaderParms[SHADERPARM_ALPHA] = 1.0f;
	if ( renderHandle == -1 ) {
		renderHandle = gameRenderWorld->AddEntityDef( &renderEntity );
	} else {
		gameRenderWorld->UpdateEntityDef( renderHandle, &renderEntity );
	}
}

void idVRHand::Update( idPlayer *player, const idVec3 &worldPos, const idMat3 &worldAxis, bool isTracked, float grip, float trigger, float dt ) {
	const bool wasTracked = tracked;
	tracked = isTracked;
	if ( !tracked ) {
		if ( interaction != IK_NONE ) {
			Release( player, false );
		}
		gripDown = false;
		triggerDown = false;
		touched = nullptr;
		UpdateRenderEntity();
		return;
	}

	lastPos = wasTracked ? pos : worldPos;
	pos = worldPos;
	axis = worldAxis;
	if ( dt > 0 ) {
		// smoothed hand velocity, used to throw things
		velocity = velocity * 0.5f + ( ( pos - lastPos ) / dt ) * 0.5f;
	}

	// what is the hand touching?
	int bodyId = 0;
	idEntity *nowTouching = interaction == IK_NONE ? FindTouchedEntity( player, bodyId ) : nullptr;
	touched = nowTouching;
	touchedBodyId = bodyId;

	// grip with hysteresis
	const bool wasGrip = gripDown;
	if ( gripDown ) {
		gripDown = grip > vr_gripReleaseThreshold.GetFloat();
	} else {
		gripDown = grip > vr_gripPressThreshold.GetFloat();
	}
	const bool wasTrigger = triggerDown;
	if ( triggerDown ) {
		triggerDown = trigger > vr_gripReleaseThreshold.GetFloat();
	} else {
		triggerDown = trigger > vr_gripPressThreshold.GetFloat();
	}

	if ( gripDown && !wasGrip && interaction == IK_NONE ) {
		StartInteraction( player );
	} else if ( !gripDown && wasGrip && interaction != IK_NONE ) {
		Release( player, true );
	}

	// trigger uses the held object
	if ( triggerDown && !wasTrigger && ( interaction == IK_HOLD || interaction == IK_CARRY ) ) {
		idEntity *ent = held.GetEntity();
		idStr script;
		if ( ent && ent->spawnArgs.GetString( "equip_action_script", "", script ) ) {
			HandsDebug( "hand %d uses %s (%s)", index, ent->name.c_str(), script.c_str() );
			idThread *thread = player->CallScriptFunctionArgs( script.c_str(), true, 0, "e", ent );
			if ( thread != nullptr ) {
				thread->Execute();
			}
		}
	}

	// carried static items follow the hand directly
	if ( interaction == IK_CARRY ) {
		idEntity *ent = held.GetEntity();
		if ( ent == nullptr ) {
			interaction = IK_NONE;
		} else {
			idVec3 o;
			idMat3 a;
			GetHoldTarget( o, a );
			ent->SetOrigin( o );
			ent->SetAxis( a );
		}
	}

	// a held object that gets stuck far away slips out of the hand
	if ( interaction == IK_HOLD ) {
		idEntity *ent = held.GetEntity();
		if ( ent == nullptr || ent->IsHidden() ) {
			interaction = IK_NONE;
			held = nullptr;
			if ( ent != nullptr && player->vrHands.CountHolding( ent ) == 0 ) {
				player->vrHands.ReleaseNonSolidToPlayer( ent, true );
			}
		} else {
			idVec3 o;
			idMat3 a;
			GetHoldTarget( o, a );
			const float dist = ( ent->GetPhysics()->GetOrigin( heldBodyId ) - o ).Length();
			if ( dist > vr_gripBreakDistance.GetFloat() ) {
				farFromHandTime += idMath::FtoiRound( dt * 1000 );
				if ( farFromHandTime > vr_gripBreakTime.GetInteger() ) {
					HandsDebug( "hand %d loses grip on %s", index, ent->name.c_str() );
					Release( player, false );
				}
			} else {
				farFromHandTime = 0;
			}
		}
	}

	UpdateRenderEntity();
}

/*
===============================================================================
	idVRHands
===============================================================================
*/

void idVRHands::Init() {
	hands[0].Init( 0 );
	hands[1].Init( 1 );
	highlighted.Clear();
	clipRestores.Clear();
	lastTime = 0;
	initialized = true;
}

void idVRHands::Shutdown() {
	if ( !initialized ) {
		return;
	}
	hands[0].Shutdown();
	hands[1].Shutdown();
	for ( int i = 0; i < highlighted.Num(); i++ ) {
		if ( highlighted[i].GetEntity() ) {
			highlighted[i].GetEntity()->SetFrobbed( false );
		}
	}
	highlighted.Clear();
	initialized = false;
}

bool idVRHands::IsActive() const {
	return vr_handsEnable.GetBool() && vr_useMotionControllers.GetBool();
}

int idVRHands::CountHolding( const idEntity *ent ) const {
	int count = 0;
	for ( int h = 0; h < 2; h++ ) {
		if ( hands[h].GetInteraction() != idVRHand::IK_NONE && hands[h].GetHeld() == ent ) {
			count++;
		}
	}
	return count;
}

bool idVRHands::IsInteractingWith( const idEntity *ent ) const {
	for ( int h = 0; h < 2; h++ ) {
		if ( hands[h].GetTouched() == ent || hands[h].GetHeld() == ent ) {
			return true;
		}
	}
	return false;
}

void idVRHands::ReleaseAll( idPlayer *player ) {
	for ( int h = 0; h < 2; h++ ) {
		if ( hands[h].GetInteraction() != idVRHand::IK_NONE ) {
			hands[h].stomachRelease = false;
			hands[h].Release( player, false );
		}
	}
}

void idVRHands::MakeNonSolidToPlayer( idEntity *ent ) {
	for ( int i = 0; i < clipRestores.Num(); i++ ) {
		if ( clipRestores[i].ent.GetEntity() == ent ) {
			clipRestores[i].refCount++;
			return;
		}
	}
	idPhysics *phys = ent->GetPhysics();
	clipRestore_t r;
	r.ent = ent;
	r.clipMask = phys->GetClipMask();
	r.contents = phys->GetContents();
	r.refCount = 1;
	clipRestores.Append( r );

	// same approach as the grabber: held things don't collide with the player, but still with the world,
	// AI can see through them while they are carried
	if ( r.clipMask & ( CONTENTS_SOLID | CONTENTS_CORPSE ) ) {
		phys->SetClipMask( ( r.clipMask & ~MASK_PLAYERSOLID ) | CONTENTS_SOLID );
	}
	if ( r.contents & ( CONTENTS_SOLID | CONTENTS_CORPSE ) ) {
		phys->SetContents( ( r.contents & ~( CONTENTS_SOLID | CONTENTS_OPAQUE ) ) | CONTENTS_MONSTERCLIP | CONTENTS_RENDERMODEL | CONTENTS_CORPSE );
	}
}

void idVRHands::ReleaseNonSolidToPlayer( idEntity *ent, bool immediately ) {
	for ( int i = 0; i < clipRestores.Num(); i++ ) {
		if ( clipRestores[i].ent.GetEntity() == ent ) {
			clipRestores[i].refCount--;
			if ( clipRestores[i].refCount <= 0 && immediately ) {
				ent->GetPhysics()->SetClipMask( clipRestores[i].clipMask );
				ent->GetPhysics()->SetContents( clipRestores[i].contents );
				clipRestores.RemoveIndex( i );
			}
			return;
		}
	}
}

void idVRHands::UpdatePendingClipRestores( idPlayer *player ) {
	// released objects become solid to the player again once they no longer overlap the player
	const idBounds &playerBounds = player->GetPhysics()->GetAbsBounds();
	for ( int i = clipRestores.Num() - 1; i >= 0; i-- ) {
		clipRestore_t &r = clipRestores[i];
		idEntity *ent = r.ent.GetEntity();
		if ( ent == nullptr ) {
			clipRestores.RemoveIndex( i );
			continue;
		}
		if ( r.refCount > 0 ) {
			continue;
		}
		if ( ent->GetPhysics()->GetAbsBounds().IntersectsBounds( playerBounds ) ) {
			continue;
		}
		ent->GetPhysics()->SetClipMask( r.clipMask );
		ent->GetPhysics()->SetContents( r.contents );
		clipRestores.RemoveIndex( i );
	}
}

bool idVRHands::IsInStomachZone( const idVec3 &handLocal, const idVec3 &headLocal, const idMat3 &headLocalAxis ) const {
	idVec3 forward = headLocalAxis[0];
	forward.z = 0;
	if ( forward.Normalize() < 0.001f ) {
		forward.Set( 1, 0, 0 );
	}
	const idVec3 stomach = headLocal - idVec3( 0, 0, vr_stomachDrop.GetFloat() ) + forward * vr_stomachForward.GetFloat();
	return ( handLocal - stomach ).LengthSqr() < Square( vr_stomachRadius.GetFloat() );
}

void idVRHands::UpdateHighlights() {
	idList<idEntity *> now;
	for ( int h = 0; h < 2; h++ ) {
		idEntity *e = hands[h].GetHeld() ? hands[h].GetHeld() : hands[h].GetTouched();
		if ( e ) {
			now.AddUnique( e );
		}
	}
	for ( int i = highlighted.Num() - 1; i >= 0; i-- ) {
		idEntity *e = highlighted[i].GetEntity();
		if ( e == nullptr ) {
			highlighted.RemoveIndex( i );
			continue;
		}
		if ( now.FindIndex( e ) < 0 ) {
			e->SetFrobbed( false );
			highlighted.RemoveIndex( i );
		}
	}
	for ( idEntity *e : now ) {
		bool found = false;
		for ( int i = 0; i < highlighted.Num(); i++ ) {
			if ( highlighted[i].GetEntity() == e ) {
				found = true;
				break;
			}
		}
		if ( !found ) {
			e->SetFrobbed( true );
			idEntityPtr<idEntity> p;
			p = e;
			highlighted.Append( p );
		}
	}
}

void idVRHands::DriveHeldObjects( float dt ) {
	if ( dt <= 0 ) {
		return;
	}
	bool done[2] = { false, false };
	for ( int h = 0; h < 2; h++ ) {
		if ( done[h] || hands[h].GetInteraction() != idVRHand::IK_HOLD ) {
			continue;
		}
		idEntity *ent = hands[h].GetHeld();
		if ( ent == nullptr ) {
			continue;
		}
		const int bodyId = hands[h].GetHeldBodyId();

		// gather all hands holding the same body
		idVec3 targetOrigin;
		idMat3 targetAxis;
		hands[h].GetHoldTarget( targetOrigin, targetAxis );
		idVec3 handVel = hands[h].GetVelocity();
		int numHands = 1;
		for ( int o = h + 1; o < 2; o++ ) {
			if ( hands[o].GetInteraction() == idVRHand::IK_HOLD && hands[o].GetHeld() == ent && hands[o].GetHeldBodyId() == bodyId ) {
				idVec3 oo;
				idMat3 oa;
				hands[o].GetHoldTarget( oo, oa );
				targetOrigin += oo;
				handVel += hands[o].GetVelocity();
				numHands++;
				done[o] = true;
			}
		}
		targetOrigin /= numHands;
		handVel /= numHands;

		idPhysics *phys = ent->GetPhysics();
		float mass = phys->GetMass( bodyId );
		if ( mass <= 0.0f ) {
			mass = 1.0f;
		}
		// each hand can only give a limited acceleration, heavier things get proportionally less
		const float strength = numHands * idMath::ClampFloat( 0.0f, 1.0f, vr_handLiftMass.GetFloat() / mass );
		const float maxAccel = vr_handMaxAccel.GetFloat() * strength;
		const float maxAngAccel = vr_handMaxAngAccel.GetFloat() * strength;

		// linear: drive towards the hand, but only as fast as the hand's strength allows
		const idVec3 origin = phys->GetOrigin( bodyId );
		const idVec3 curVel = phys->GetLinearVelocity( bodyId );
		idVec3 desiredVel = handVel + ( targetOrigin - origin ) / Max( vr_handFollowTime.GetFloat(), 0.01f );
		const float maxSpeed = vr_handMaxSpeed.GetFloat();
		if ( desiredVel.LengthSqr() > maxSpeed * maxSpeed ) {
			desiredVel.Normalize();
			desiredVel *= maxSpeed;
		}
		idVec3 deltaVel = desiredVel - curVel;
		const float maxDelta = maxAccel * dt;
		if ( deltaVel.LengthSqr() > maxDelta * maxDelta ) {
			deltaVel.Normalize();
			deltaVel *= maxDelta;
		}

		// angular: rotate the held body towards the orientation it had relative to the hand
		const idMat3 curAxis = phys->GetAxis( bodyId );
		idVec3 rotation( 0, 0, 0 );
		for ( int i = 0; i < 3; i++ ) {
			rotation += curAxis[i].Cross( targetAxis[i] );
		}
		rotation *= 0.5f;
		const idVec3 curAngVel = phys->GetAngularVelocity( bodyId );
		idVec3 deltaAngVel = rotation / Max( vr_handFollowTime.GetFloat(), 0.01f ) - curAngVel;
		const float maxAngDelta = maxAngAccel * dt;
		if ( deltaAngVel.LengthSqr() > maxAngDelta * maxAngDelta ) {
			deltaAngVel.Normalize();
			deltaAngVel *= maxAngDelta;
		}

		phys->SetLinearVelocity( curVel + deltaVel, bodyId );
		phys->SetAngularVelocity( curAngVel + deltaAngVel, bodyId );
		phys->Activate();
	}
}

void idVRHands::Update( idPlayer *player ) {
	if ( !initialized ) {
		Init();
	}

	const int now = gameLocal.time;
	float dt = lastTime > 0 ? ( now - lastTime ) * 0.001f : 0.0f;
	lastTime = now;
	dt = idMath::ClampFloat( 0.0f, 0.1f, dt );

	const usercmd_t &cmd = player->usercmd;
	const renderView_t *view = player->GetRenderView();
	const bool viewOk = view != nullptr && !player->AI_DEAD;

	for ( int h = 0; h < 2; h++ ) {
		const bool tracked = viewOk && ( cmd.vrHandFlags[h] & VR_HAND_TRACKED ) != 0;
		idVec3 worldPos( 0, 0, 0 );
		idMat3 worldAxis = mat3_identity;
		if ( tracked ) {
			// the poses are in the headset's local space, which is anchored at the player's view origin
			worldPos = view->vieworg + cmd.vrHandPos[h] * view->viewaxis;
			worldAxis = cmd.vrHandAxis[h].ToMat3() * view->viewaxis;
			hands[h].stomachRelease = IsInStomachZone( cmd.vrHandPos[h], cmd.vrHeadPos, cmd.vrHeadAxis.ToMat3() );
		}
		hands[h].Update( player, worldPos, worldAxis, tracked, cmd.vrHandGrip[h], cmd.vrHandTrigger[h], dt );
	}

	DriveHeldObjects( dt );
	UpdateHighlights();
	UpdatePendingClipRestores( player );

	// the classic view-ray frob target is not used with hands
	player->m_FrobEntity = nullptr;
}
