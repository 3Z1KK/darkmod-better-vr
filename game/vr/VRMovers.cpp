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
#include "game/vr/VRMovers.h"
#include "game/vr/VRHands.h"
#include "game/BinaryFrobMover.h"
#include "game/FrobDoor.h"
#include "game/FrobHandle.h"
#include "game/FrobButton.h"
#include "game/FrobLever.h"

idCVar vr_moverFriction( "vr_moverFriction", "1.5", CVAR_GAME | CVAR_FLOAT, "VR: how quickly a pushed or let go door, lid or drawer slows down (fraction of its speed lost per second)" );
idCVar vr_moverStopSpeed( "vr_moverStopSpeed", "3", CVAR_GAME | CVAR_FLOAT, "VR: a coasting door stops below this speed of its far edge (units/s)" );
idCVar vr_moverBounce( "vr_moverBounce", "0.15", CVAR_GAME | CVAR_FLOAT, "VR: how much of its speed a door keeps when it swings into its fully open stop" );
idCVar vr_moverImpulseScale( "vr_moverImpulseScale", "1", CVAR_GAME | CVAR_FLOAT, "VR: how strongly thrown things push open doors" );
idCVar vr_doorSlamSpeed( "vr_doorSlamSpeed", "140", CVAR_GAME | CVAR_FLOAT, "VR: a door whose far edge closes faster than this (units/s, 1 unit = 2.3 cm) is slammed: louder, heard further by the AI" );
idCVar vr_doorLatchZone( "vr_doorLatchZone", "12", CVAR_GAME | CVAR_FLOAT, "VR: doors without a handle part can be held this close to their latch edge (units)" );
idCVar vr_doorMinHeight( "vr_doorMinHeight", "56", CVAR_GAME | CVAR_FLOAT, "VR: portal doors at least this tall without a handle part are held at the latch only; smaller things (windows, hatches) anywhere" );
idCVar vr_moversDebug( "vr_moversDebug", "0", CVAR_GAME | CVAR_BOOL, "VR: print what doors and lids are doing" );

namespace {
	void MoversDebug( const char *fmt, ... ) {
		if ( !vr_moversDebug.GetBool() ) {
			return;
		}
		va_list argptr;
		char text[1024];
		va_start( argptr, fmt );
		idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
		va_end( argptr );
		gameLocal.Printf( "VR movers: %s\n", text );
	}

	// world position of a point of the mover (given in its own space) at a point of its path
	idVec3 PointAt( CBinaryFrobMover *mover, float f, const idVec3 &local ) {
		idVec3 origin;
		idMat3 axis;
		mover->VR_GetWorldPose( f, origin, axis );
		return origin + local * axis;
	}

	enum gripZone_t {
		GRIP_HANDLE_ONLY,		// a door with a handle part: only the handle
		GRIP_LATCH_EDGE,		// a door without one: only near the latch
		GRIP_ANYWHERE			// chests, drawers, windows, ...
	};

	gripZone_t GripZone( CBinaryFrobMover *mover ) {
		if ( mover->IsType( CFrobDoor::Type ) ) {
			if ( static_cast<CFrobDoor *>( mover )->GetDoorhandle() != nullptr ) {
				return GRIP_HANDLE_ONLY;
			}
			const idBounds &b = mover->GetPhysics()->GetAbsBounds();
			if ( mover->VR_HasPortal() && b[1].z - b[0].z >= vr_doorMinHeight.GetFloat() ) {
				return GRIP_LATCH_EDGE;
			}
		}
		return GRIP_ANYWHERE;
	}
}

/*
===============================================================================
	static helpers
===============================================================================
*/

CBinaryFrobMover *idVRMovers::GetMover( idEntity *ent ) {
	if ( ent == nullptr ) {
		return nullptr;
	}
	if ( ent->IsType( CFrobHandle::Type ) ) {
		// a door handle moves its door; a lock handle belongs to a lock, not to something that opens
		idEntity *master = static_cast<CFrobHandle *>( ent )->GetFrobMaster();
		return ( master != nullptr && master != ent && !master->IsType( CFrobHandle::Type ) ) ? GetMover( master ) : nullptr;
	}
	if ( !ent->IsType( CBinaryFrobMover::Type ) || ent->IsType( CFrobButton::Type ) || ent->IsType( CFrobLever::Type ) ) {
		return nullptr;
	}
	return static_cast<CBinaryFrobMover *>( ent );
}

CBinaryFrobMover *idVRMovers::GetPushMover( idEntity *ent ) {
	for ( int depth = 0; ent != nullptr && depth < 3; depth++ ) {
		CBinaryFrobMover *mover = GetMover( ent );
		if ( mover != nullptr ) {
			return mover;
		}
		ent = ent->GetBindMaster();
	}
	return nullptr;
}

bool idVRMovers::CanGripAt( idEntity *part, const idVec3 &point ) {
	CBinaryFrobMover *mover = GetMover( part );
	if ( mover == nullptr || part->IsType( CFrobHandle::Type ) ) {
		return true;
	}
	switch ( GripZone( mover ) ) {
	case GRIP_HANDLE_ONLY:
		return false;
	case GRIP_ANYWHERE:
		return true;
	default:
		break;
	}

	// near the latch: around handle height, at the edge that moves the most
	const idBounds &abs = mover->GetPhysics()->GetAbsBounds();
	const float height = Max( 1.0f, abs[1].z - abs[0].z );
	const float h = ( point.z - abs[0].z ) / height;
	if ( h < 0.2f || h > 0.75f ) {
		return false;
	}
	const idVec3 origin = mover->GetPhysics()->GetOrigin();
	const idMat3 axis = mover->GetPhysics()->GetAxis();
	const idVec3 local = ( point - origin ) * axis.Transpose();
	const float f = mover->VR_GetFraction();
	idVec3 corners[8];
	mover->GetPhysics()->GetBounds().ToPoints( corners );
	const float zone = vr_doorLatchZone.GetFloat();

	const float angle = mover->VR_PathAngle();
	if ( angle > 0.0f ) {
		// swinging: the further from the hinge, the faster a point moves along the path
		const float toRadius = 1.0f / DEG2RAD( angle );
		float maxRadius = 0.0f;
		for ( int i = 0; i < 8; i++ ) {
			maxRadius = Max( maxRadius, ( PointAt( mover, Min( 1.0f, f + 0.01f ), corners[i] ) - PointAt( mover, Max( 0.0f, f - 0.01f ), corners[i] ) ).Length() );
		}
		const float step = Min( 1.0f, f + 0.01f ) - Max( 0.0f, f - 0.01f );
		maxRadius *= toRadius / step;
		const float radius = ( PointAt( mover, Min( 1.0f, f + 0.01f ), local ) - PointAt( mover, Max( 0.0f, f - 0.01f ), local ) ).Length() * toRadius / step;
		return radius >= maxRadius - zone;
	}
	// sliding: near the front or the back edge
	idVec3 dir = PointAt( mover, 1.0f, vec3_origin ) - PointAt( mover, 0.0f, vec3_origin );
	if ( dir.Normalize() < 0.001f ) {
		return true;
	}
	const idVec3 dirLocal = dir * axis.Transpose();
	float lo = idMath::INFINITY, hi = -idMath::INFINITY;
	for ( int i = 0; i < 8; i++ ) {
		const float d = corners[i] * dirLocal;
		lo = Min( lo, d );
		hi = Max( hi, d );
	}
	const float d = local * dirLocal;
	return d <= lo + zone || d >= hi - zone;
}

bool idVRMovers::TryUnlatch( CBinaryFrobMover *mover ) {
	if ( mover->IsLocked() ) {
		mover->FrobMoverStartSound( "snd_locked" );
		MoversDebug( "%s is locked", mover->name.c_str() );
		return false;
	}
	return true;
}

bool idVRMovers::IsBusy( CBinaryFrobMover *mover ) {
	return mover->IsChangingState();
}

bool idVRMovers::HandsActive() {
	idPlayer *player = gameLocal.GetLocalPlayer();
	return player != nullptr && player->vrHands.IsActive();
}

bool idVRMovers::OnImpulse( CBinaryFrobMover *self, const idVec3 &point, const idVec3 &impulse ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player == nullptr || !player->vrHands.IsActive() ) {
		return false;
	}
	CBinaryFrobMover *mover = GetMover( self );
	if ( mover == nullptr ) {
		return false;
	}
	player->vrHands.GetMovers().Impulse( mover, point, impulse );
	return true;
}

/*
===============================================================================
	idVRMovers
===============================================================================
*/

void idVRMovers::Clear() {
	states.Clear();
}

idVRMovers::state_t *idVRMovers::Find( CBinaryFrobMover *mover, bool create ) {
	for ( int i = 0; i < states.Num(); i++ ) {
		if ( states[i].ent.GetEntity() == mover ) {
			return &states[i];
		}
	}
	if ( !create ) {
		return nullptr;
	}
	state_t s;
	s.ent = mover;
	s.f = mover->VR_GetFraction();
	s.fVel = 0.0f;
	s.cmd = s.f;
	s.cmdPending = false;
	s.holdSum = 0.0f;
	s.holdCount = 0;
	s.latchHeld = false;
	s.atOpenStop = s.f > 0.999f;
	s.lastActive = gameLocal.time;

	// how far its far edge travels, and how hard it is to set in motion (from its size, like held objects)
	idVec3 corners[8];
	mover->GetPhysics()->GetBounds().ToPoints( corners );
	float bestChord = -1.0f;
	int farCorner = 0;
	float meanSq = 0.0f;
	for ( int i = 0; i < 8; i++ ) {
		const float chord = ( PointAt( mover, 1.0f, corners[i] ) - PointAt( mover, 0.0f, corners[i] ) ).LengthSqr();
		if ( chord > bestChord ) {
			bestChord = chord;
			farCorner = i;
		}
		meanSq += PathVelocity( mover, 0.5f, corners[i] ).LengthSqr();
	}
	meanSq /= 8.0f;
	s.pathLen = 0.0f;
	idVec3 prev = PointAt( mover, 0.0f, corners[farCorner] );
	for ( int k = 1; k <= 8; k++ ) {
		const idVec3 p = PointAt( mover, k / 8.0f, corners[farCorner] );
		s.pathLen += ( p - prev ).Length();
		prev = p;
	}
	s.inertia = Max( 0.01f, VR_EffectiveMass( mover, 0 ) * meanSq );
	states.Append( s );
	MoversDebug( "%s: path %.0f units, feels like %.1f", mover->name.c_str(), s.pathLen, VR_EffectiveMass( mover, 0 ) );
	return &states[states.Num() - 1];
}

idVec3 idVRMovers::PathVelocity( CBinaryFrobMover *mover, float f, const idVec3 &local ) {
	const float f0 = Max( 0.0f, f - 0.01f );
	const float f1 = Min( 1.0f, f + 0.01f );
	return ( PointAt( mover, f1, local ) - PointAt( mover, f0, local ) ) / ( f1 - f0 );
}

float idVRMovers::FindFraction( CBinaryFrobMover *mover, const idVec3 &gripLocal, const idVec3 &target ) {
	auto distSq = [&]( float f ) {
		return ( PointAt( mover, f, gripLocal ) - target ).LengthSqr();
	};
	// search near where it is (a door can't jump to the other end of its path)
	const float cur = mover->VR_GetFraction();
	const float lo = Max( 0.0f, cur - 0.35f );
	const float hi = Min( 1.0f, cur + 0.35f );
	const int steps = 28;
	float best = cur, bestD = distSq( cur );
	for ( int i = 0; i <= steps; i++ ) {
		const float f = lo + ( hi - lo ) * i / steps;
		const float d = distSq( f );
		if ( d < bestD ) {
			bestD = d;
			best = f;
		}
	}
	// refine around the best sample
	const float step = ( hi - lo ) / steps;
	float a = Max( lo, best - step ), b = Min( hi, best + step );
	for ( int i = 0; i < 20; i++ ) {
		const float m1 = a + ( b - a ) / 3.0f;
		const float m2 = b - ( b - a ) / 3.0f;
		if ( distSq( m1 ) < distSq( m2 ) ) {
			b = m2;
		} else {
			a = m1;
		}
	}
	return idMath::ClampFloat( 0.0f, 1.0f, ( a + b ) * 0.5f );
}

void idVRMovers::Hold( CBinaryFrobMover *mover, float fraction, bool latchHeld ) {
	state_t *s = Find( mover, true );
	s->holdSum += fraction;
	s->holdCount++;
	s->latchHeld = s->latchHeld || latchHeld;
	s->lastActive = gameLocal.time;
}

void idVRMovers::Push( CBinaryFrobMover *mover, const idVec3 &point, const idVec3 &normal, const idVec3 &handVel ) {
	if ( !mover->IsOpen() || IsBusy( mover ) ) {
		return;		// latched shut (or moving on its own)
	}
	state_t *s = Find( mover, true );
	if ( s->holdCount > 0 || s->pathLen < 0.5f ) {
		return;
	}
	// the touched point has to get out of the hand's way at least as fast as the hand comes in
	const idVec3 local = ( point - mover->GetPhysics()->GetOrigin() ) * mover->GetPhysics()->GetAxis().Transpose();
	const idVec3 along = PathVelocity( mover, s->f, local );
	const idVec3 pushDir = -normal;
	const float a = along * pushDir;		// how fast the point moves into the push direction per path speed
	const float b = handVel * pushDir;		// how fast the hand comes in
	if ( b <= 0.0f || idMath::Fabs( a ) < 0.01f ) {
		return;
	}
	const float needed = b / a * 1.05f;
	if ( a > 0.0f ) {
		s->fVel = Max( s->fVel, needed );
	} else {
		s->fVel = Min( s->fVel, needed );
	}
	s->lastActive = gameLocal.time;
}

void idVRMovers::Impulse( CBinaryFrobMover *mover, const idVec3 &point, const idVec3 &impulse ) {
	if ( !mover->IsOpen() || IsBusy( mover ) ) {
		return;
	}
	state_t *s = Find( mover, true );
	if ( s->holdCount > 0 || s->pathLen < 0.5f ) {
		return;
	}
	const idVec3 local = ( point - mover->GetPhysics()->GetOrigin() ) * mover->GetPhysics()->GetAxis().Transpose();
	const float dv = ( impulse * PathVelocity( mover, s->f, local ) ) / s->inertia * vr_moverImpulseScale.GetFloat();
	s->fVel += dv;
	s->lastActive = gameLocal.time;
	MoversDebug( "%s hit, speed now %.0f units/s", mover->name.c_str(), s->fVel * s->pathLen );
}

void idVRMovers::Update( float dt ) {
	if ( dt <= 0.0f ) {
		return;
	}
	const int msec = Max( 1, idMath::FtoiRound( dt * 1000.0f ) );
	const float closedEps = 0.0005f;

	for ( int i = states.Num() - 1; i >= 0; i-- ) {
		state_t &s = states[i];
		CBinaryFrobMover *mover = static_cast<CBinaryFrobMover *>( s.ent.GetEntity() );
		if ( mover == nullptr || mover->IsHidden() ) {
			states.RemoveIndex( i );
			continue;
		}
		const int holdCount = s.holdCount;
		const float holdTarget = holdCount > 0 ? s.holdSum / holdCount : 0.0f;
		const bool latchHeld = s.latchHeld;
		s.holdSum = 0.0f;
		s.holdCount = 0;
		s.latchHeld = false;

		const float fNow = mover->VR_GetFraction();
		const float measuredVel = ( fNow - s.f ) / dt;
		s.f = fNow;

		if ( IsBusy( mover ) ) {
			// a normal move (AI, script) runs: leave it alone
			s.fVel = 0.0f;
			s.cmdPending = false;
			s.atOpenStop = fNow > 0.999f;
			if ( gameLocal.time - s.lastActive > 500 ) {
				states.RemoveIndex( i );
			}
			continue;
		}

		// came back to the closed position: the latch catches (silently while the handle is held down)
		if ( mover->IsOpen() && fNow <= closedEps && ( !s.cmdPending || s.cmd <= closedEps ) ) {
			if ( latchHeld ) {
				mover->VR_ReachClosed( 0, 0.0f );
				MoversDebug( "%s closed quietly", mover->name.c_str() );
			} else {
				const float speed = Max( idMath::Fabs( measuredVel ), idMath::Fabs( s.fVel ) ) * s.pathLen;
				const float slam = vr_doorSlamSpeed.GetFloat();
				if ( speed > slam ) {
					const float boost = 3.0f + 5.0f * idMath::ClampFloat( 0.0f, 1.0f, ( speed - slam ) / slam );
					mover->VR_ReachClosed( 2, boost );
					MoversDebug( "%s slammed at %.0f units/s (+%.1f dB)", mover->name.c_str(), speed, boost );
				} else {
					mover->VR_ReachClosed( 1, 0.0f );
					MoversDebug( "%s closed at %.0f units/s", mover->name.c_str(), speed );
				}
			}
			s.fVel = 0.0f;
		}

		const bool open = mover->IsOpen();
		float target;
		if ( holdCount > 0 ) {
			// held: follows the hands; the release keeps the speed it had
			target = holdTarget;
			s.fVel = s.fVel * 0.5f + measuredVel * 0.5f;
			if ( !open && !latchHeld ) {
				target = 0.0f;		// latched: the handle has to be pressed to open it
				s.fVel = 0.0f;
			}
		} else {
			if ( !open ) {
				s.fVel = 0.0f;		// latched shut: nothing moves it
			} else if ( s.cmdPending && idMath::Fabs( fNow - s.cmd ) > 0.002f ) {
				s.fVel = 0.0f;		// ran into something
				MoversDebug( "%s blocked", mover->name.c_str() );
			}
			s.fVel *= Max( 0.0f, 1.0f - vr_moverFriction.GetFloat() * dt );
			if ( idMath::Fabs( s.fVel ) * s.pathLen < vr_moverStopSpeed.GetFloat() ) {
				s.fVel = 0.0f;
			}
			target = fNow + s.fVel * dt;
		}

		// the open stop
		if ( target >= 1.0f ) {
			target = 1.0f;
			if ( holdCount == 0 && s.fVel > 0.0f ) {
				s.fVel = -s.fVel * vr_moverBounce.GetFloat();
			}
		}
		if ( fNow >= 0.999f ) {
			if ( !s.atOpenStop ) {
				s.atOpenStop = true;
				mover->VR_ReachOpen();
			}
		} else if ( fNow < 0.99f ) {
			s.atOpenStop = false;
		}

		// the closed stop: close enough and the latch catches it
		if ( target * s.pathLen < 0.5f ) {
			target = 0.0f;
		}
		if ( target <= 0.0f ) {
			target = 0.0f;
			if ( holdCount == 0 ) {
				s.fVel = Min( s.fVel, 0.0f );
			}
		}

		// leaving the closed position (only possible with the latch held open)
		if ( !mover->IsOpen() && target > closedEps ) {
			if ( !latchHeld ) {
				target = 0.0f;
			} else {
				mover->VR_LeaveClosed();
				MoversDebug( "%s opens", mover->name.c_str() );
			}
		}

		if ( idMath::Fabs( target - fNow ) > 0.00001f ) {
			mover->VR_MoveTo( target, msec );
			s.cmd = target;
			s.cmdPending = true;
			s.lastActive = gameLocal.time;
		} else {
			s.cmdPending = false;
		}

		if ( holdCount == 0 && s.fVel == 0.0f && !s.cmdPending && gameLocal.time - s.lastActive > 500 ) {
			states.RemoveIndex( i );
		}
	}
}
