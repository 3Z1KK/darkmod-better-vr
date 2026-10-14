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
#include "game/vr/VRBody.h"
#include "renderer/vr/OpenXRBackend.h"

idCVar vr_roomscale( "vr_roomscale", "1", CVAR_GAME | CVAR_BOOL | CVAR_ARCHIVE, "VR: the player body follows the headset when walking in real life" );
idCVar vr_roomscaleRecenterDistance( "vr_roomscaleRecenterDistance", "40", CVAR_GAME | CVAR_FLOAT, "VR: a head jump larger than this (units) in one frame is a recenter, not a step" );
idCVar vr_standingEyeHeight( "vr_standingEyeHeight", "0", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: real standing head height above the floor in game units (1 unit = 2.3 cm). 0 = calibrate automatically" );
idCVar vr_crouchDrop( "vr_crouchDrop", "14", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: lower your real head by this much (units, 1 unit = 2.3 cm) to crouch" );
idCVar vr_roomStepCreepSpeed( "vr_roomStepCreepSpeed", "30", CVAR_GAME | CVAR_FLOAT, "VR: real-life walking faster than this (units/s) makes quiet (creep) footsteps, slower is silent" );
idCVar vr_roomStepWalkSpeed( "vr_roomStepWalkSpeed", "65", CVAR_GAME | CVAR_FLOAT, "VR: real-life walking faster than this (units/s) makes normal walking footsteps" );
idCVar vr_roomStepStride( "vr_roomStepStride", "28", CVAR_GAME | CVAR_FLOAT, "VR: real-life distance (units) per footstep" );
idCVar vr_bodyWidth( "vr_bodyWidth", "18", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: width of the player's collision body in VR (units; the normal game uses pm_bboxwidth 32). Applies on map load" );
idCVar vr_viewBelowBoxTop( "vr_viewBelowBoxTop", "1", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: the camera is this far (units, 1 unit = 2.3 cm) below the top of the player's collision box when standing straight" );
idCVar vr_headInWallFade( "vr_headInWallFade", "1", CVAR_GAME | CVAR_BOOL, "VR: fade to black when the real head moves into a wall" );

void idVRBody::Reset() {
	initialized = false;
	localOffset.Zero();
	headDrop = 0.0f;
	physicalCrouch = false;
	blackout = 0.0f;
	roomSpeed = 0.0f;
	stepDistance = 0.0f;
	lastTime = 0;
	// standing height stays calibrated
}

bool idVRBody::IsActive( const idPlayer *player ) const {
	return vr_useMotionControllers.GetBool() && ( player->usercmd.vrHeadFlags & VR_HEAD_TRACKED ) != 0;
}

idVec3 idVRBody::LocalToWorld( const renderView_t *view, const idVec3 &local ) const {
	// the same mapping the renderer uses for the eyes
	return view->vieworg + ( local - view->vrLocalOffset ) * view->viewaxis;
}

void idVRBody::UpdateHead( idPlayer *player ) {
	const usercmd_t &cmd = player->usercmd;
	if ( !IsActive( player ) ) {
		physicalCrouch = false;
		headDrop = 0.0f;
		localOffset.z = 0.0f;
		return;
	}

	// how far is the real head below standing?
	if ( cmd.vrHeadFlags & VR_HEAD_FLOOR ) {
		if ( vr_standingEyeHeight.GetFloat() > 0 ) {
			standingHeadHeight = vr_standingEyeHeight.GetFloat();
		} else if ( cmd.vrHeadFloorZ > standingHeadHeight ) {
			standingHeadHeight = cmd.vrHeadFloorZ;
		}
		headDrop = idMath::ClampFloat( -60.0f, 4.0f, cmd.vrHeadFloorZ - standingHeadHeight );
		// map the real head height onto the game eye height (normal view height when standing)
		localOffset.z = cmd.vrHeadPos.z - headDrop;
	} else {
		// no floor information: assume the headset's local origin is the standing head
		headDrop = idMath::ClampFloat( -60.0f, 4.0f, cmd.vrHeadPos.z );
		localOffset.z = 0.0f;
	}

	const float drop = vr_crouchDrop.GetFloat();
	if ( physicalCrouch ) {
		physicalCrouch = headDrop < -drop + 3.0f;
	} else {
		physicalCrouch = headDrop < -drop;
	}
}

void idVRBody::Update( idPlayer *player, renderView_t *view ) {
	const usercmd_t &cmd = player->usercmd;
	const int now = gameLocal.time;
	float dt = lastTime > 0 ? ( now - lastTime ) * 0.001f : 0.0f;
	lastTime = now;
	dt = idMath::ClampFloat( 0.0f, 0.1f, dt );

	if ( !IsActive( player ) ) {
		initialized = false;
		blackout = 0.0f;
		view->vrLocalOffset.Zero();
		view->vrBlackout = 0.0f;
		return;
	}

	const idVec3 &headLocal = cmd.vrHeadPos;
	if ( !initialized ) {
		// start with the body exactly under the head
		localOffset.x = headLocal.x;
		localOffset.y = headLocal.y;
		initialized = true;
	}

	idPhysics_Player *phys = static_cast<idPhysics_Player *>( player->GetPhysics() );
	idVec3 deltaLocal( headLocal.x - localOffset.x, headLocal.y - localOffset.y, 0.0f );

	if ( deltaLocal.LengthFast() > vr_roomscaleRecenterDistance.GetFloat() ) {
		// the play space was recentered: don't walk there, just re-anchor
		localOffset.x = headLocal.x;
		localOffset.y = headLocal.y;
		deltaLocal.Zero();
	}

	const bool canMove = vr_roomscale.GetBool() && !player->AI_DEAD && !player->noclip && !player->spectating
		&& !phys->IsMantling() && !phys->OnRope() && !phys->OnLadder() && player->GetBindMaster() == nullptr
		&& !gameLocal.inCinematic;

	idVec3 moved = vec3_zero;
	if ( canMove && deltaLocal.LengthSqr() > Square( 0.05f ) ) {
		idVec3 worldDelta = deltaLocal * view->viewaxis;
		worldDelta -= ( worldDelta * -phys->GetGravityNormal() ) * -phys->GetGravityNormal();
		moved = phys->VRRoomscaleMove( worldDelta );
		// the body (and the view origin) moved, the offset takes the same move so the view doesn't jump
		idVec3 horizMoved = moved;
		horizMoved -= ( horizMoved * -phys->GetGravityNormal() ) * -phys->GetGravityNormal();
		const idVec3 movedLocal = horizMoved * view->viewaxis.Transpose();
		localOffset.x += movedLocal.x;
		localOffset.y += movedLocal.y;
		view->vieworg += moved;
	}
	// Vertical: the view follows the real head 1:1 from the standing eye height. The game's own eye height
	// (used by AI, the lightgem, ...) drops to the crouch height while the body is crouched; compensate for that
	// unless the crouch button lowered the view on purpose, or the body is stuck crouched under a low ceiling
	// while the real head is up (then the view stays inside the vent).
	float eyeComp = 0.0f;
	bool standingView = false;
	if ( !player->AI_DEAD && !player->m_CrouchIntent && !phys->IsMantling() && phys->GetWaterLevel() < WATERLEVEL_WAIST ) {
		const bool stuckCrouched = phys->IsCrouching() && !physicalCrouch;
		if ( !stuckCrouched ) {
			eyeComp = pm_normalviewheight.GetFloat() - player->EyeHeight();
			standingView = true;
		}
	}
	// The camera sits at the top of the player's collision box (the game's eye height is lower than that):
	// standing it is at the top of the standing box, crouched (button, or stuck under a low ceiling) at the
	// top of the crouched box. Only the view and the hands move up; the game's eye (AI, lightgem) stays.
	float raise = 0.0f;
	if ( !player->AI_DEAD ) {
		const bool crouchedView = !standingView && phys->IsCrouching();
		const float boxTop = crouchedView ? pm_crouchheight.GetFloat() : pm_normalheight.GetFloat();
		const float eyeHeight = crouchedView ? pm_crouchviewheight.GetFloat() : pm_normalviewheight.GetFloat();
		raise = Max( 0.0f, boxTop - vr_viewBelowBoxTop.GetFloat() - eyeHeight );
	}
	view->vrLocalOffset = localOffset;
	view->vrLocalOffset.z -= eyeComp + raise;

	// footsteps from real-life walking: silent when careful, quiet when brisk, normal when fast
	if ( dt > 0 ) {
		idVec3 horiz = moved;
		horiz.z = 0;
		const float speed = horiz.Length() / dt;
		roomSpeed = roomSpeed * 0.8f + speed * 0.2f;
		if ( roomSpeed > vr_roomStepCreepSpeed.GetFloat() ) {
			stepDistance += horiz.Length();
			if ( stepDistance >= vr_roomStepStride.GetFloat() ) {
				stepDistance = 0.0f;
				const bool oldCreep = player->AI_CREEP;
				player->AI_CREEP = roomSpeed < vr_roomStepWalkSpeed.GetFloat();
				player->PlayFootStepSound();
				player->AI_CREEP = oldCreep;
			}
		} else {
			stepDistance = 0.0f;
		}
	}

	// is the real head inside a wall?
	bool headBlocked = false;
	if ( vr_headInWallFade.GetBool() && !player->noclip && !player->spectating ) {
		const idVec3 eyeWorld = LocalToWorld( view, headLocal );
		const idVec3 bodyEye = player->GetEyePosition();
		idBounds headBounds( idVec3( -2, -2, -2 ), idVec3( 2, 2, 2 ) );
		trace_t trace;
		gameLocal.clip.TraceBounds( trace, bodyEye, eyeWorld, headBounds, MASK_SOLID, player );
		headBlocked = trace.fraction < 1.0f;
	}
	const float target = headBlocked ? 1.0f : 0.0f;
	const float rate = 6.0f * dt;
	if ( blackout < target ) {
		blackout = Min( target, blackout + rate );
	} else {
		blackout = Max( target, blackout - rate * 2.0f );
	}
	view->vrBlackout = blackout;
}
