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
#include "game/Misc.h"
#include "game/AFEntity.h"
#include "game/vr/VRClimb.h"
#include "game/vr/VRHands.h"

idCVar vr_climbLedges( "vr_climbLedges", "-1", CVAR_GAME | CVAR_INTEGER, "VR: climbing ledges by hand. -1 = the mission decides (worldspawn \"vr_hand_climbing\" \"1\", off if not set), 0 = off, 1 = on. Ropes and ladders are always climbed by hand in VR" );
idCVar vr_climbGraceTime( "vr_climbGraceTime", "1.5", CVAR_GAME | CVAR_FLOAT, "VR: seconds each hand can hold the body up on a ledge before gravity pulls as usual (refilled when standing on solid ground). Not for ropes and ladders" );
idCVar vr_climbSlipDistance( "vr_climbSlipDistance", "16", CVAR_GAME | CVAR_FLOAT, "VR: a climbing hand slips off when the real hand is this far (units) from where it holds on" );
idCVar vr_climbSinkSpeed( "vr_climbSinkSpeed", "20", CVAR_GAME | CVAR_FLOAT, "VR: how fast (units/s) gravity pulls the body down on a ledge once the hands' grace time is over (ropes, ladders: never)" );
idCVar vr_climbMaxSpeed( "vr_climbMaxSpeed", "250", CVAR_GAME | CVAR_FLOAT, "VR: fastest the hands can move the body while climbing (units/s)" );
idCVar vr_climbReach( "vr_climbReach", "6", CVAR_GAME | CVAR_FLOAT, "VR: how far (units) from the hand a climbable surface may be to hold on" );
idCVar vr_climbLipHeight( "vr_climbLipHeight", "4", CVAR_GAME | CVAR_FLOAT, "VR: how far above the hand (units) the fingers look for a ledge top or the bottom of a crack" );
idCVar vr_climbReleaseSpeed( "vr_climbReleaseSpeed", "120", CVAR_GAME | CVAR_FLOAT, "VR: letting go doesn't throw the body faster than this upwards (units/s)" );
idCVar vr_climbDebug( "vr_climbDebug", "0", CVAR_GAME | CVAR_BOOL, "VR: print climbing grabs, slips and mantles" );

extern idCVar cv_pm_mantle_minflatness;

namespace {
	void ClimbDebug( const char *fmt, ... ) {
		if ( !vr_climbDebug.GetBool() ) {
			return;
		}
		va_list argptr;
		char text[1024];
		va_start( argptr, fmt );
		idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
		va_end( argptr );
		gameLocal.Printf( "VR climb: %s\n", text );
	}

	idPhysics_Player *PlayerPhysics( idPlayer *player ) {
		return static_cast<idPhysics_Player *>( player->GetPhysics() );
	}

	const char *KindName( int kind ) {
		static const char *names[] = { "nothing", "ledge", "ladder", "rope" };
		return names[idMath::ClampInt( 0, 3, kind )];
	}
}

void idVRClimb::Reset() {
	for ( int h = 0; h < 2; h++ ) {
		grips[h] = gripState_t();
	}
	graceInit = false;
	vz = 0.0f;
	engaged = false;
	nextMantleTry = 0;
	LockTurning( false );
}

bool idVRClimb::IsClimbEntity( const idEntity *ent ) {
	return ent != nullptr && ( ent->m_bIsClimbableRope || ent->IsType( tdmVine::Type ) );
}

bool idVRClimb::LedgesEnabled() {
	const int forced = vr_climbLedges.GetInteger();
	if ( forced >= 0 ) {
		return forced != 0;
	}
	return gameLocal.world != nullptr && gameLocal.world->spawnArgs.GetBool( "vr_hand_climbing", "0" );
}

void idVRClimb::SetLocal( gripState_t &g, const idVec3 &point ) const {
	idEntity *ent = g.ent.GetEntity();
	if ( ent == nullptr || ent == gameLocal.world ) {
		g.local = point;
		return;
	}
	idPhysics *phys = ent->GetPhysics();
	g.local = ( point - phys->GetOrigin( g.body ) ) * phys->GetAxis( g.body ).Transpose();
}

idVec3 idVRClimb::GripWorld( const gripState_t &g ) const {
	idEntity *ent = g.ent.GetEntity();
	if ( ent == nullptr || ent == gameLocal.world ) {
		return g.local;
	}
	idPhysics *phys = ent->GetPhysics();
	return phys->GetOrigin( g.body ) + g.local * phys->GetAxis( g.body );
}

bool idVRClimb::FindRope( idPlayer *player, const idVRHand &hand, gripState_t &out, idVec3 &point ) const {
	const idVec3 handPos = hand.GetVisualPos();
	idBounds bounds( handPos );
	bounds.ExpandSelf( vr_climbReach.GetFloat() );
	idClip_ClipModelList clipModels;
	gameLocal.clip.ClipModelsTouchingBounds( bounds, CONTENTS_SOLID | CONTENTS_CORPSE | CONTENTS_BODY | CONTENTS_RENDERMODEL, clipModels );
	float best = idMath::INFINITY;
	bool found = false;
	for ( int i = 0; i < clipModels.Num(); i++ ) {
		idClipModel *cm = clipModels[i];
		idEntity *ent = cm->GetEntity();
		if ( ent == nullptr || ent == player || !IsClimbEntity( ent ) || ent->IsHidden() ) {
			continue;
		}
		const float d = ( cm->GetAbsBounds().GetCenter() - handPos ).LengthSqr();
		if ( d >= best ) {
			continue;
		}
		best = d;
		found = true;
		out.ent = ent;
		out.kind = ent->m_bIsClimbableRope ? KIND_ROPE : KIND_LADDER;
		out.body = 0;
		if ( ent->IsType( idAFEntity_Base::Type ) ) {
			out.body = Max( 0, static_cast<idAFEntity_Base *>( ent )->BodyForClipModelId( cm->GetId() ) );
		}
		point = handPos;
	}
	return found;
}

bool idVRClimb::FindGrip( idPlayer *player, const idVRHand &hand, idEntity *touched, gripState_t &out, idVec3 &point ) const {
	out = gripState_t();

	// ropes and vines: anywhere on them
	if ( touched != nullptr && IsClimbEntity( touched ) ) {
		out.ent = touched;
		out.kind = touched->m_bIsClimbableRope ? KIND_ROPE : KIND_LADDER;
		point = hand.GetVisualPos();
		return true;
	}
	if ( FindRope( player, hand, out, point ) ) {
		return true;
	}

	// a surface next to the hand: towards the real hand (it is pressed against something), out of the palm,
	// out of the fingers, down
	const idPhysics *phys = player->GetPhysics();
	const idVec3 up = -phys->GetGravityNormal();
	const idVec3 from = hand.GetVisualPos();
	const idMat3 &axis = hand.GetInput().axis;
	const float reach = vr_climbReach.GetFloat();

	idVec3 dirs[7];
	int numDirs = 0;
	idVec3 toCtrl = hand.GetInput().pos - from;
	if ( toCtrl.Normalize() > 0.3f ) {
		dirs[numDirs++] = toCtrl;
	}
	dirs[numDirs++] = axis[1];
	dirs[numDirs++] = -axis[1];
	dirs[numDirs++] = -axis[2];
	dirs[numDirs++] = axis[0];
	dirs[numDirs++] = -axis[0];
	dirs[numDirs++] = -up;

	// a ladder: ladder surfaces are often on invisible player clip brushes in front of the visible ladder, so
	// look with everything that stops the player (also from the eyes to the hand: the hand may be inside such
	// a brush already)
	{
		const int ladderMask = MASK_SOLID | phys->GetClipMask();
		trace_t tr;
		for ( int i = 0; i < numDirs; i++ ) {
			gameLocal.clip.TracePoint( tr, from, from + dirs[i] * reach, ladderMask, player );
			if ( tr.fraction < 1.0f && tr.c.material != nullptr && tr.c.material->IsLadder() ) {
				out.ent = gameLocal.entities[tr.c.entityNum];
				out.kind = KIND_LADDER;
				point = tr.c.point;
				return out.ent.GetEntity() != nullptr;
			}
		}
		const idVec3 eye = player->GetEyePosition();
		idVec3 toHand = from - eye;
		const float handDist = toHand.Normalize();
		if ( handDist > 1.0f ) {
			gameLocal.clip.TracePoint( tr, eye, from + toHand * reach, ladderMask, player );
			if ( tr.fraction < 1.0f && tr.c.material != nullptr && tr.c.material->IsLadder()
				&& ( tr.c.point - from ).LengthSqr() < Square( reach * 2.0f ) ) {
				out.ent = gameLocal.entities[tr.c.entityNum];
				out.kind = KIND_LADDER;
				point = tr.c.point;
				return out.ent.GetEntity() != nullptr;
			}
		}
	}

	trace_t hit;
	hit.fraction = 1.0f;
	for ( int i = 0; i < numDirs; i++ ) {
		trace_t tr;
		gameLocal.clip.TracePoint( tr, from, from + dirs[i] * reach, MASK_SOLID, player );
		if ( tr.fraction < hit.fraction && tr.fraction > 0.0f ) {
			hit = tr;
		}
	}
	if ( hit.fraction >= 1.0f || hit.c.entityNum < 0 || hit.c.entityNum >= ENTITYNUM_NONE ) {
		return false;
	}
	idEntity *ent = gameLocal.entities[hit.c.entityNum];
	if ( ent == nullptr || ent->IsType( idActor::Type ) ) {
		return false;
	}

	// a ladder surface (ladders, climbable ivy) or a vine: anywhere
	if ( ( hit.c.material != nullptr && hit.c.material->IsLadder() ) || IsClimbEntity( ent ) ) {
		out.ent = ent;
		out.kind = KIND_LADDER;
		point = hit.c.point;
		return true;
	}

	// a ledge: the thing is mantleable and the surface under the fingers is flat enough
	if ( !LedgesEnabled() || !ent->IsMantleable() ) {
		return false;
	}
	const float minFlatness = cv_pm_mantle_minflatness.GetFloat();
	trace_t support;
	support.fraction = 1.0f;
	if ( hit.c.normal * up >= minFlatness ) {
		support = hit;				// the hand is on top of it
	} else {
		// a wall face: look for the top of its lip, or the bottom of a crack, just above and into it
		idVec3 into = -hit.c.normal;
		into -= ( into * up ) * up;
		if ( into.Normalize() < 0.1f ) {
			return false;			// an overhang
		}
		const float lip = vr_climbLipHeight.GetFloat();
		for ( int depth = 1; depth <= 3 && support.fraction >= 1.0f; depth++ ) {
			const idVec3 start = hit.c.point + up * lip + into * ( 1.5f * depth );
			trace_t tr;
			gameLocal.clip.TracePoint( tr, start, start - up * ( lip + 4.0f ), MASK_SOLID, player );
			if ( tr.fraction > 0.0f && tr.fraction < 1.0f && tr.c.normal * up >= minFlatness ) {
				idEntity *top = gameLocal.entities[tr.c.entityNum];
				if ( top != nullptr && top->IsMantleable() && !top->IsType( idActor::Type ) ) {
					support = tr;
				}
			}
		}
	}
	if ( support.fraction >= 1.0f ) {
		return false;
	}
	out.ent = gameLocal.entities[support.c.entityNum];
	out.kind = KIND_LEDGE;
	point = support.c.point;
	return true;
}

bool idVRClimb::TryGrab( idPlayer *player, idVRHand hands[2], int handIndex, idEntity *touched ) {
	idVRHand &hand = hands[handIndex];
	if ( !hand.IsTracked() || hand.GetInteraction() != idVRHand::IK_NONE || player->AI_DEAD ) {
		return false;
	}
	gripState_t g;
	idVec3 point;
	if ( !FindGrip( player, hand, touched, g, point ) ) {
		return false;
	}
	SetLocal( g, point );
	g.prevWorld = point;
	g.prevCtrlLocal = player->usercmd.vrHandPos[handIndex];
	g.havePrev = true;
	grips[handIndex] = g;
	if ( !graceInit ) {
		grace[0] = grace[1] = vr_climbGraceTime.GetFloat();
		graceInit = true;
	}
	hand.BeginClimb( point );
	idEntity *ent = g.ent.GetEntity();
	ClimbDebug( "hand %d holds on to a %s (%s), %.2fs left", handIndex, KindName( g.kind ), ent ? ent->name.c_str() : "?", grace[handIndex] );
	return true;
}

void idVRClimb::OnRelease( idPlayer *player, idVRHand hands[2], int handIndex ) {
	if ( grips[handIndex].kind == KIND_NONE ) {
		return;
	}
	ClimbDebug( "hand %d lets go", handIndex );
	grips[handIndex] = gripState_t();
	hands[handIndex].EndClimb();
	if ( grips[1 - handIndex].kind == KIND_NONE ) {
		StopHanging( player, true );
	}
}

void idVRClimb::StopHanging( idPlayer *player, bool keepFalling ) {
	if ( !engaged ) {
		vz = 0.0f;
		return;
	}
	engaged = false;
	const idVec3 up = -player->GetPhysics()->GetGravityNormal();
	// let go: keep falling, but never thrown upwards
	const float release = keepFalling ? Min( vz, vr_climbReleaseSpeed.GetFloat() ) : 0.0f;
	PlayerPhysics( player )->SetVRClimbing( false, up * release );
	vz = 0.0f;
}

void idVRClimb::LockTurning( bool lock ) {
	// turning the view while hanging would swing the real hands away from where they hold on
	// (the arrow selector uses the same switch: while hanging keep it on, and only clear it if this set it)
	if ( lock ) {
		if ( !cvarSystem->GetCVarBool( "vr_turnLocked" ) ) {
			cvarSystem->SetCVarBool( "vr_turnLocked", true );
		}
		turnLocked = true;
	} else if ( turnLocked ) {
		turnLocked = false;
		cvarSystem->SetCVarBool( "vr_turnLocked", false );
	}
}

void idVRClimb::Update( idPlayer *player, idVRHand hands[2], float dt ) {
	idPhysics_Player *phys = PlayerPhysics( player );
	const idVec3 up = -phys->GetGravityNormal();

	// the game's own mantle (jump while hanging, or started below) takes over: let go
	if ( phys->IsMantling() ) {
		for ( int h = 0; h < 2; h++ ) {
			if ( grips[h].kind != KIND_NONE ) {
				grips[h] = gripState_t();
				hands[h].EndClimb();
			}
		}
		if ( engaged ) {
			engaged = false;
			vz = 0.0f;
			phys->SetVRClimbing( false );
		}
	}
	LockTurning( engaged );

	// standing on solid ground refills both hands
	if ( !engaged && phys->VROnGround() ) {
		grace[0] = grace[1] = vr_climbGraceTime.GetFloat();
		graceInit = true;
	}

	if ( dt <= 0.0f ) {
		return;
	}
	const renderView_t *view = player->GetRenderView();
	if ( view == nullptr ) {
		return;
	}

	// what the holding hands do
	idVec3 pull = vec3_zero;		// how the hands move the body
	idVec3 carry = vec3_zero;		// how the held things moved (ropes swinging, lifts)
	int numHolding = 0;
	bool supported = false;			// a hand holds the body up (rope, ladder, or a ledge hand with time left)
	bool onLedge = false;
	idVec3 ledgePoint = vec3_zero;
	for ( int h = 0; h < 2; h++ ) {
		gripState_t &g = grips[h];
		if ( g.kind == KIND_NONE ) {
			continue;
		}
		if ( hands[h].GetInteraction() != idVRHand::IK_CLIMB || g.ent.GetEntity() == nullptr || g.ent.GetEntity()->IsHidden() ) {
			// the hand stopped holding on some other way, or the thing is gone
			grips[h] = gripState_t();
			if ( hands[h].GetInteraction() == idVRHand::IK_CLIMB ) {
				hands[h].EndClimb();
			}
			continue;
		}
		const idVec3 gw = GripWorld( g );
		hands[h].SetClimbAnchor( gw );
		const idVec3 ctrlLocal = player->usercmd.vrHandPos[h];
		if ( g.havePrev ) {
			carry += gw - g.prevWorld;
			// the real hand's move (tracking space, turned like the view; turning the view doesn't pull)
			pull -= ( ctrlLocal - g.prevCtrlLocal ) * view->viewaxis;
		}
		g.prevWorld = gw;
		g.prevCtrlLocal = ctrlLocal;
		g.havePrev = true;
		numHolding++;
		if ( g.kind != KIND_LEDGE || grace[h] > 0.0f ) {
			supported = true;
		}
		if ( g.kind == KIND_LEDGE ) {
			onLedge = true;
			ledgePoint = gw;
		}
	}

	if ( numHolding == 0 ) {
		StopHanging( player, true );
		LockTurning( false );
		return;
	}
	pull /= numHolding;
	carry /= numHolding;
	const float pullUp = pull * up;

	// standing on the ground: only pulling up lifts the body off it; otherwise the hands just hold on
	if ( !engaged ) {
		// (a small threshold, so a shaky hand doesn't lift the body off the floor)
		if ( phys->VROnGround() && pullUp <= 0.2f ) {
			return;
		}
		engaged = true;
		vz = 0.0f;
		phys->SetVRClimbing( true );
		ClimbDebug( "hanging" );
	} else if ( phys->VROnGround() && pullUp <= 0.0f && !supported ) {
		// sank down onto the ground
		StopHanging( player, false );
		return;
	}

	// each ledge hand uses up its own time while the body hangs on it
	for ( int h = 0; h < 2; h++ ) {
		if ( grips[h].kind == KIND_LEDGE && grace[h] > 0.0f ) {
			grace[h] = Max( 0.0f, grace[h] - dt );
			if ( grace[h] <= 0.0f ) {
				ClimbDebug( "hand %d is tired", h );
			}
		}
	}

	// the body's move: the hands' pull. Once no hand holds it up (a ledge held longer than the grace time),
	// gravity pulls it down slowly (it starts like gravity and stays at vr_climbSinkSpeed); pulling harder
	// makes up for it. The grip only fails when the real hand gets too far from it (below).
	idVec3 move = pull - up * pullUp;
	float dz;
	if ( supported ) {
		vz = 0.0f;
		dz = pullUp;
	} else {
		vz = Max( vz - phys->GetGravity().Length() * dt, -vr_climbSinkSpeed.GetFloat() );
		dz = pullUp + vz * dt;
	}
	move += up * dz + carry;
	const float maxMove = vr_climbMaxSpeed.GetFloat() * dt;
	if ( move.LengthSqr() > Square( maxMove ) ) {
		move.Normalize();
		move *= maxMove;
	}
	const idVec3 moved = phys->VRClimbMove( move );
	const float movedUp = moved * up;
	if ( ( dz < 0.0f && movedUp > dz + 0.1f ) || ( dz > 0.0f && movedUp < dz - 0.1f ) ) {
		vz = 0.0f;					// stopped by something above or below
	}

	// a hand whose real hand got too far from where it holds on slips off
	for ( int h = 0; h < 2; h++ ) {
		if ( grips[h].kind == KIND_NONE ) {
			continue;
		}
		const float apart = ( hands[h].GetVisualPos() - hands[h].GetInput().pos ).Length();
		if ( apart > vr_climbSlipDistance.GetFloat() ) {
			ClimbDebug( "hand %d slips off (%.1f units)", h, apart );
			hands[h].Release( player, false );	// calls OnRelease
		}
	}
	if ( !engaged ) {
		return;							// both hands slipped
	}

	// pulled up toward a held ledge: the game's own mantle takes over as soon as the ledge is in its reach
	if ( onLedge && pullUp > 0.0f && gameLocal.time >= nextMantleTry ) {
		nextMantleTry = gameLocal.time + 100;
		if ( phys->VRTryMantle( ledgePoint - phys->GetOrigin() ) ) {
			ClimbDebug( "mantling up" );
			engaged = false;
			vz = 0.0f;
			phys->SetVRClimbing( false );
			for ( int h = 0; h < 2; h++ ) {
				if ( grips[h].kind != KIND_NONE ) {
					grips[h] = gripState_t();
					hands[h].EndClimb();
				}
			}
		}
	}
}
