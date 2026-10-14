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
idCVar vr_handCollisionSize( "vr_handCollisionSize", "1.5", CVAR_GAME | CVAR_FLOAT, "VR: half size (game units) of the hand's collision box against the world" );
idCVar vr_handCollision( "vr_handCollision", "1", CVAR_GAME | CVAR_BOOL, "VR: free hands are stopped by walls and solid objects" );
idCVar vr_handLiftMass( "vr_handLiftMass", "20", CVAR_GAME | CVAR_FLOAT, "VR: the heaviest mass one hand can just hold up against gravity (hand strength)" );
idCVar vr_handWristLever( "vr_handWristLever", "6", CVAR_GAME | CVAR_FLOAT, "VR: wrist strength: maximum torque = hand strength x this lever (game units)" );
idCVar vr_handTwoHandWrist( "vr_handTwoHandWrist", "0.35", CVAR_GAME | CVAR_FLOAT, "VR: wrist torque factor per hand when both hands hold the same object" );
idCVar vr_handFollowTime( "vr_handFollowTime", "0.03", CVAR_GAME | CVAR_FLOAT, "VR: time (s) in which a held object should close its distance to the hand" );
idCVar vr_handRotFollowTime( "vr_handRotFollowTime", "0.03", CVAR_GAME | CVAR_FLOAT, "VR: time (s) in which a held object should close its angle to the hand" );
idCVar vr_handMaxSpeed( "vr_handMaxSpeed", "1500", CVAR_GAME | CVAR_FLOAT, "VR: maximum speed (units/s) a hand can drive a held object to" );
idCVar vr_gripBreakDistance( "vr_gripBreakDistance", "16", CVAR_GAME | CVAR_FLOAT, "VR: grip slips if the grip point is this far (units) from the controller" );
idCVar vr_gripBreakTime( "vr_gripBreakTime", "300", CVAR_GAME | CVAR_INTEGER, "VR: time (ms) the grip point may stay too far before the grip slips" );
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
	input.tracked = false;
	input.pos.Zero();
	input.axis.Identity();
	input.vel.Zero();
	input.angVel.Zero();
	input.grip = 0.0f;
	input.trigger = 0.0f;
	input.eye.Zero();
	tracked = false;
	gripDown = false;
	triggerDown = false;
	physPos.Zero();
	physValid = false;
	visualPos.Zero();
	visualAxis.Identity();
	touched = nullptr;
	touchedBodyId = 0;
	interaction = IK_NONE;
	held = nullptr;
	heldBodyId = 0;
	slipTime = 0;
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
	physValid = false;
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
	idBounds bounds( physPos );
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
		if ( !gameLocal.clip.ContentsModel( physPos, palmClip, mat3_identity, -1, cm->Handle(), cm->GetOrigin(), cm->GetAxis() ) ) {
			continue;
		}
		const float dist = ( cm->GetAbsBounds().GetCenter() - physPos ).LengthSqr();
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

void idVRHand::MoveFreeHand( idPlayer *player ) {
	const idVec3 &target = input.pos;
	if ( !vr_handCollision.GetBool() ) {
		physPos = target;
		physValid = true;
		return;
	}

	// the hand moves from where it was towards the controller and is stopped by solid things, sliding along them.
	// If it got left far behind (or stuck), it reaches out again from the head, so it can never pass through walls.
	const float s = vr_handCollisionSize.GetFloat();
	const idBounds bounds( idVec3( -s, -s, -s ), idVec3( s, s, s ) );
	static idClipModel *handClip = nullptr;
	static float handClipSize = -1.0f;
	if ( handClip == nullptr || handClipSize != s ) {
		delete handClip;
		handClip = new idClipModel( idTraceModel( bounds ) );
		handClipSize = s;
	}
	idVec3 start = physPos;
	if ( !physValid || ( physPos - target ).LengthSqr() > Square( 24.0f ) ) {
		start = input.eye;
	}
	trace_t tr;
	for ( int attempt = 0; attempt < 2; attempt++ ) {
		idVec3 pos = start;
		idVec3 move = target - start;
		// a hand that ended up inside something (a door swung into it, ...) reaches out again from the head
		bool stuck = ( gameLocal.clip.Contents( pos, handClip, mat3_identity, MASK_SOLID, player ) & MASK_SOLID ) != 0;
		for ( int bump = 0; !stuck && bump < 3 && move.LengthSqr() > Square( 0.01f ); bump++ ) {
			gameLocal.clip.TraceBounds( tr, pos, pos + move, bounds, MASK_SOLID, player );
			pos = tr.endpos;
			if ( tr.fraction >= 1.0f ) {
				break;
			}
			move *= ( 1.0f - tr.fraction );
			move -= ( move * tr.c.normal ) * tr.c.normal;
		}
		if ( !stuck ) {
			physPos = pos;
			physValid = true;
			return;
		}
		if ( start == input.eye ) {
			break;
		}
		start = input.eye;
	}
	physPos = input.eye;
	physValid = true;
}

void idVRHand::FindContact( idEntity *ent, int bodyId ) {
	idPhysics *phys = ent->GetPhysics();
	const idVec3 bodyOrigin = phys->GetOrigin( bodyId );
	const idMat3 bodyAxisT = phys->GetAxis( bodyId ).Transpose();
	grip.contactPoint = grip.anchor;
	grip.contactNormal.Zero();

	idClipModel *cm = phys->GetClipModel( bodyId );
	if ( cm == nullptr ) {
		return;
	}
	// look from the palm towards the body for the nearest bit of its surface
	idVec3 dir = cm->GetAbsBounds().GetCenter() - physPos;
	if ( dir.Normalize() < 0.001f ) {
		return;
	}
	const idVec3 end = physPos + dir * ( vr_handTouchRadius.GetFloat() * 3.0f );
	trace_t tr;
	gameLocal.clip.TranslationModel( tr, physPos, end, nullptr, mat3_identity, -1, cm->Handle(), cm->GetOrigin(), cm->GetAxis() );
	if ( tr.fraction > 0.0f && tr.fraction < 1.0f ) {
		grip.contactPoint = ( tr.c.point - bodyOrigin ) * bodyAxisT;
		grip.contactNormal = tr.c.normal * bodyAxisT;
	}
}

idVec3 idVRHand::GetGripTarget() const {
	return input.pos + grip.controllerOffset * input.axis;
}

void idVRHand::GetGripWorld( idVec3 &point, idMat3 &handAxis ) const {
	idEntity *ent = held.GetEntity();
	if ( ent == nullptr ) {
		point = input.pos;
		handAxis = input.axis;
		return;
	}
	idPhysics *phys = ent->GetPhysics();
	const idMat3 bodyAxis = phys->GetAxis( heldBodyId );
	point = phys->GetOrigin( heldBodyId ) + grip.anchor * bodyAxis;
	handAxis = grip.handAxis * bodyAxis;
}

void idVRHand::StartInteraction( idPlayer *player ) {
	idEntity *ent = touched.GetEntity();
	if ( ent == nullptr ) {
		return;
	}

	if ( CanHold( ent ) ) {
		// grip the object where the hand touches it
		held = ent;
		heldBodyId = touchedBodyId;
		idPhysics *phys = ent->GetPhysics();
		const idVec3 bodyOrigin = phys->GetOrigin( heldBodyId );
		const idMat3 bodyAxisT = phys->GetAxis( heldBodyId ).Transpose();
		grip.anchor = ( physPos - bodyOrigin ) * bodyAxisT;
		grip.handAxis = input.axis * bodyAxisT;
		grip.controllerOffset = ( physPos - input.pos ) * input.axis.Transpose();
		FindContact( ent, heldBodyId );
		slipTime = 0;
		interaction = IK_HOLD;
		phys->Activate();
		player->vrHands.MakeNonSolidToPlayer( ent );
		HandsDebug( "hand %d grips %s (body %d, mass %.2f)", index, ent->name.c_str(), heldBodyId, phys->GetMass( heldBodyId ) );
		return;
	}

	if ( IsInventoryItem( ent ) ) {
		// static inventory item (can't move physically): carried directly
		held = ent;
		heldBodyId = 0;
		interaction = IK_CARRY;
		const idMat3 ctrlAxisT = input.axis.Transpose();
		carryHomeOrigin = ent->GetPhysics()->GetOrigin();
		carryHomeAxis = ent->GetPhysics()->GetAxis();
		carryRelOrigin = ( carryHomeOrigin - input.pos ) * ctrlAxisT;
		carryRelAxis = carryHomeAxis * ctrlAxisT;
		player->vrHands.MakeNonSolidToPlayer( ent );
		HandsDebug( "hand %d carries %s", index, ent->name.c_str() );
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
	// the free hand continues from where the drawn hand was
	if ( kind == IK_HOLD && ent != nullptr ) {
		idMat3 a;
		GetGripWorld( physPos, a );
	}
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
	if ( kind == IK_HOLD ) {
		// the object keeps the motion the hand gave it last frame (it was moving with the controller)
		HandsDebug( "hand %d lets go of %s at %.0f units/s", index, ent->name.c_str(), ent->GetPhysics()->GetLinearVelocity( heldBodyId ).Length() );
	} else {
		HandsDebug( "hand %d releases %s", index, ent->name.c_str() );
	}
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
	renderEntity.origin = visualPos;
	renderEntity.axis = visualAxis;
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

void idVRHand::UpdateVisual() {
	if ( tracked ) {
		switch ( interaction ) {
		case IK_HOLD:
			// the drawn hand stays on the grip point, even if the controller moved on
			GetGripWorld( visualPos, visualAxis );
			break;
		case IK_CARRY:
			visualPos = input.pos;
			visualAxis = input.axis;
			break;
		default:
			visualPos = physValid ? physPos : input.pos;
			visualAxis = input.axis;
			break;
		}
	}
	UpdateRenderEntity();
}

void idVRHand::Update( idPlayer *player, const vrHandInput_t &in, float dt ) {
	input = in;
	tracked = in.tracked;
	if ( !tracked ) {
		if ( interaction != IK_NONE ) {
			Release( player, false );
		}
		gripDown = false;
		triggerDown = false;
		touched = nullptr;
		physValid = false;
		return;
	}

	// free hands are stopped by the world
	if ( interaction != IK_HOLD ) {
		MoveFreeHand( player );
	}

	// what is the hand touching?
	int bodyId = 0;
	idEntity *nowTouching = interaction == IK_NONE ? FindTouchedEntity( player, bodyId ) : nullptr;
	touched = nowTouching;
	touchedBodyId = bodyId;

	// grip and trigger with hysteresis
	const bool wasGrip = gripDown;
	if ( gripDown ) {
		gripDown = in.grip > vr_gripReleaseThreshold.GetFloat();
	} else {
		gripDown = in.grip > vr_gripPressThreshold.GetFloat();
	}
	const bool wasTrigger = triggerDown;
	if ( triggerDown ) {
		triggerDown = in.trigger > vr_gripReleaseThreshold.GetFloat();
	} else {
		triggerDown = in.trigger > vr_gripPressThreshold.GetFloat();
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

	// carried static items follow the controller directly
	if ( interaction == IK_CARRY ) {
		idEntity *ent = held.GetEntity();
		if ( ent == nullptr ) {
			interaction = IK_NONE;
		} else {
			ent->SetOrigin( in.pos + carryRelOrigin * in.axis );
			ent->SetAxis( carryRelAxis * in.axis );
		}
	}

	if ( interaction == IK_HOLD ) {
		idEntity *ent = held.GetEntity();
		if ( ent == nullptr || ent->IsHidden() ) {
			interaction = IK_NONE;
			held = nullptr;
			if ( ent != nullptr && player->vrHands.CountHolding( ent ) == 0 ) {
				player->vrHands.ReleaseNonSolidToPlayer( ent, true );
			}
		} else {
			// the grip slips if the object stays too far from where the hand wants it
			idVec3 point;
			idMat3 a;
			GetGripWorld( point, a );
			if ( ( point - GetGripTarget() ).LengthSqr() > Square( vr_gripBreakDistance.GetFloat() ) ) {
				slipTime += idMath::FtoiRound( dt * 1000 );
				if ( slipTime > vr_gripBreakTime.GetInteger() ) {
					HandsDebug( "hand %d loses its grip on %s", index, ent->name.c_str() );
					Release( player, false );
				}
			} else {
				slipTime = 0;
			}
		}
	}
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
	lastDt = 0.0f;
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

/*
 * Every hand that holds a body pulls its grip point towards the controller and turns the body with its wrist.
 * Both are velocity changes computed from the body's real mass and inertia, capped by what a hand can do in
 * one frame, so the result follows physics: a bottle sits in the hand, a chair held by a leg is heavy at the
 * far end and tips, a crate barely moves with one hand and lifts slowly with two.
 */
void idVRHands::DriveHeldObjects( float dt ) {
	if ( dt <= 0 ) {
		return;
	}
	const idVec3 gravity = gameLocal.GetGravity();
	const float handForce = vr_handLiftMass.GetFloat() * gravity.Length();
	const float wristTorque = handForce * vr_handWristLever.GetFloat();

	for ( int h = 0; h < 2; h++ ) {
		idVRHand &hand = hands[h];
		if ( hand.GetInteraction() != idVRHand::IK_HOLD ) {
			continue;
		}
		idEntity *ent = hand.GetHeld();
		if ( ent == nullptr ) {
			continue;
		}
		const int bodyId = hand.GetHeldBodyId();
		int numHands = 0;
		for ( int o = 0; o < 2; o++ ) {
			if ( hands[o].GetInteraction() == idVRHand::IK_HOLD && hands[o].GetHeld() == ent && hands[o].GetHeldBodyId() == bodyId ) {
				numHands++;
			}
		}

		idPhysics *phys = ent->GetPhysics();
		const idVec3 bodyOrigin = phys->GetOrigin( bodyId );
		const idMat3 bodyAxis = phys->GetAxis( bodyId );
		impactInfo_t info;
		phys->GetImpactInfo( bodyId, bodyOrigin, &info );
		if ( info.invMass <= 0.0f ) {
			continue;
		}
		const idMat3 invInertia = info.invInertiaTensor;
		idMat3 inertia = invInertia;
		if ( !inertia.InverseSelf() ) {
			continue;
		}
		idVec3 linVel = phys->GetLinearVelocity( bodyId );
		idVec3 angVel = phys->GetAngularVelocity( bodyId );
		const vrHandInput_t &in = hand.GetInput();

		// wrist: turn the body so the hand keeps its grip orientation
		{
			const idMat3 desired = hand.GetGrip().handAxis.Transpose() * in.axis;
			idVec3 axisSum( 0, 0, 0 );
			float dots = 0.0f;
			for ( int i = 0; i < 3; i++ ) {
				axisSum += bodyAxis[i].Cross( desired[i] );
				dots += bodyAxis[i] * desired[i];
			}
			const float angle = idMath::ACos( idMath::ClampFloat( -1.0f, 1.0f, ( dots - 1.0f ) * 0.5f ) );
			idVec3 rotError( 0, 0, 0 );
			if ( axisSum.Normalize() > 1e-5f ) {
				rotError = axisSum * angle;
			}
			const idVec3 wantAngVel = in.angVel + rotError / Max( vr_handRotFollowTime.GetFloat(), dt );
			idVec3 dL = inertia * ( wantAngVel - angVel );
			const float maxL = wristTorque * ( numHands > 1 ? vr_handTwoHandWrist.GetFloat() : 1.0f ) * dt;
			if ( dL.LengthSqr() > maxL * maxL ) {
				dL.Normalize();
				dL *= maxL;
			}
			angVel += invInertia * dL;
		}

		// arm: pull the grip point towards the controller
		{
			idVec3 gripPoint;
			idMat3 unused;
			hand.GetGripWorld( gripPoint, unused );
			impactInfo_t pointInfo;
			phys->GetImpactInfo( bodyId, gripPoint, &pointInfo );
			const idVec3 r = pointInfo.position;
			const idVec3 pointVel = linVel + angVel.Cross( r );

			// hold against gravity too (shared between the hands), as far as the hand's strength allows
			idVec3 wantVel = in.vel + ( hand.GetGripTarget() - gripPoint ) / Max( vr_handFollowTime.GetFloat(), dt ) - gravity * ( dt / numHands );
			const float maxSpeed = vr_handMaxSpeed.GetFloat();
			if ( wantVel.LengthSqr() > maxSpeed * maxSpeed ) {
				wantVel.Normalize();
				wantVel *= maxSpeed;
			}

			// impulse at the grip point that gives the point the wanted velocity change (K = point mass matrix)
			const idMat3 skew( 0.0f, -r.z, r.y, r.z, 0.0f, -r.x, -r.y, r.x, 0.0f );
			idMat3 K = mat3_identity * info.invMass - skew * invInertia * skew;
			if ( K.InverseSelf() ) {
				idVec3 impulse = K * ( wantVel - pointVel );
				const float maxImpulse = handForce * dt;
				if ( impulse.LengthSqr() > maxImpulse * maxImpulse ) {
					impulse.Normalize();
					impulse *= maxImpulse;
				}
				linVel += impulse * info.invMass;
				angVel += invInertia * r.Cross( impulse );
			}
		}

		phys->SetLinearVelocity( linVel, bodyId );
		phys->SetAngularVelocity( angVel, bodyId );
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
	lastDt = dt;

	const usercmd_t &cmd = player->usercmd;
	const renderView_t *view = player->GetRenderView();
	const bool viewOk = view != nullptr && !player->AI_DEAD;
	const idVec3 bodyVel = player->GetPhysics()->GetLinearVelocity();

	for ( int h = 0; h < 2; h++ ) {
		vrHandInput_t in;
		in.tracked = viewOk && ( cmd.vrHandFlags[h] & VR_HAND_TRACKED ) != 0;
		in.pos.Zero();
		in.axis.Identity();
		in.vel.Zero();
		in.angVel.Zero();
		in.grip = cmd.vrHandGrip[h];
		in.trigger = cmd.vrHandTrigger[h];
		in.eye = player->GetEyePosition();
		if ( in.tracked ) {
			in.pos = player->vrBody.LocalToWorld( view, cmd.vrHandPos[h] );
			in.axis = cmd.vrHandAxis[h].ToMat3() * view->viewaxis;
			in.vel = cmd.vrHandLinVel[h] * view->viewaxis + bodyVel;
			in.angVel = cmd.vrHandAngVel[h] * view->viewaxis;
			hands[h].stomachRelease = IsInStomachZone( cmd.vrHandPos[h], cmd.vrHeadPos, cmd.vrHeadAxis.ToMat3() );
		}
		hands[h].Update( player, in, dt );
	}

	DriveHeldObjects( dt );
	UpdateHighlights();
	UpdatePendingClipRestores( player );

	// the classic view-ray frob target is not used with hands
	player->m_FrobEntity = nullptr;
}

void idVRHands::PostThink( idPlayer *player ) {
	if ( !initialized ) {
		return;
	}
	hands[0].UpdateVisual();
	hands[1].UpdateVisual();
}
