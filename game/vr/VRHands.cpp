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
#include "game/BinaryFrobMover.h"
#include "game/FrobHandle.h"
#include "game/FrobDoor.h"
#include "game/FrobDoorHandle.h"
#include "renderer/vr/OpenXRBackend.h"
#include <unordered_map>
#include <string>

extern idCVar vr_readablesInHand;

idCVar vr_handsEnable( "vr_handsEnable", "1", CVAR_GAME | CVAR_BOOL, "VR: interact with the world through tracked hands" );
idCVar vr_handTouchRadius( "vr_handTouchRadius", "2.5", CVAR_GAME | CVAR_FLOAT, "VR: radius (game units) around the palm that counts as touching" );
idCVar vr_handCollision( "vr_handCollision", "1", CVAR_GAME | CVAR_BOOL, "VR: free hands are stopped by walls and solid objects" );
idCVar vr_handLiftMass( "vr_handLiftMass", "6", CVAR_GAME | CVAR_FLOAT, "VR: the heaviest mass one hand can hold up against gravity" );
idCVar vr_handMoveForce( "vr_handMoveForce", "2500", CVAR_GAME | CVAR_FLOAT, "VR: how hard a hand can push/pull a held object around (mass x units/s^2): light things follow exactly, heavy things lag" );
idCVar vr_handWristAccel( "vr_handWristAccel", "1500", CVAR_GAME | CVAR_FLOAT, "VR: how fast the wrist can turn a held object (rad/s^2)" );
idCVar vr_handWristMassFactor( "vr_handWristMassFactor", "0.01", CVAR_GAME | CVAR_FLOAT, "VR: how much weight slows turning: accel = vr_handWristAccel / (1 + weight x this)" );
idCVar vr_handPushMass( "vr_handPushMass", "2", CVAR_GAME | CVAR_FLOAT, "VR: effective mass of a free hand bumping into loose objects" );
idCVar vr_handPushMinSpeed( "vr_handPushMinSpeed", "25", CVAR_GAME | CVAR_FLOAT, "VR: a free hand only knocks things when moving faster than this (units/s)" );
idCVar vr_handDensity( "vr_handDensity", "0.12", CVAR_GAME | CVAR_FLOAT, "VR: held weight from size: weight = this x (bounding volume in litres)^vr_handDensityExp" );
idCVar vr_handDensityExp( "vr_handDensityExp", "0.8", CVAR_GAME | CVAR_FLOAT, "VR: see vr_handDensity" );
idCVar vr_handBodyMass( "vr_handBodyMass", "60", CVAR_GAME | CVAR_FLOAT, "VR: weight of a whole body (ragdoll) in the hands" );
idCVar vr_handTouchMargin( "vr_handTouchMargin", "0.3", CVAR_GAME | CVAR_FLOAT, "VR: extra distance (units) around the palm and fingers that still counts as touching" );
idCVar vr_handPushBounce( "vr_handPushBounce", "0.2", CVAR_GAME | CVAR_FLOAT, "VR: bounciness of hand-object bumps" );
idCVar vr_handFollowTime( "vr_handFollowTime", "0.03", CVAR_GAME | CVAR_FLOAT, "VR: time (s) in which a held object should close its distance to the hand" );
idCVar vr_handRotFollowTime( "vr_handRotFollowTime", "0.03", CVAR_GAME | CVAR_FLOAT, "VR: time (s) in which a held object should close its angle to the hand" );
idCVar vr_handMaxSpeed( "vr_handMaxSpeed", "1500", CVAR_GAME | CVAR_FLOAT, "VR: maximum speed (units/s) a hand can drive a held object to" );
idCVar vr_gripBreakDistance( "vr_gripBreakDistance", "30", CVAR_GAME | CVAR_FLOAT, "VR: grip slips if the grip point is this far (units) from the controller" );
idCVar vr_gripBreakTime( "vr_gripBreakTime", "500", CVAR_GAME | CVAR_INTEGER, "VR: time (ms) the grip point may stay too far before the grip slips" );
idCVar vr_gripPressThreshold( "vr_gripPressThreshold", "0.6", CVAR_GAME | CVAR_FLOAT, "VR: grip/trigger value that counts as pressed" );
idCVar vr_gripReleaseThreshold( "vr_gripReleaseThreshold", "0.35", CVAR_GAME | CVAR_FLOAT, "VR: grip/trigger value that counts as released" );
idCVar vr_stomachDrop( "vr_stomachDrop", "15", CVAR_GAME | CVAR_FLOAT, "VR: stomach inventory zone, distance (units) below the head" );
idCVar vr_stomachForward( "vr_stomachForward", "4", CVAR_GAME | CVAR_FLOAT, "VR: stomach inventory zone, distance (units) in front of the head" );
idCVar vr_stomachRadius( "vr_stomachRadius", "11.5", CVAR_GAME | CVAR_FLOAT, "VR: stomach inventory zone radius (units)" );
idCVar vr_ringRadius( "vr_ringRadius", "0.8", CVAR_GAME | CVAR_FLOAT, "VR: radius of the light crystal ring on the left ring finger (game units)" );
idCVar vr_ringOffset( "vr_ringOffset", "-0.1", CVAR_GAME | CVAR_FLOAT, "VR: ring centre offset from the finger joint towards the back of the finger (game units)" );
idCVar vr_calibrateHands( "vr_calibrateHands", "", CVAR_GAME, "VR: calibrate the glove position: \"fist\" (bump your fists together), \"palms\" (palms together) or \"reset\"" );
idCVar vr_handsDebug( "vr_handsDebug", "0", CVAR_GAME | CVAR_BOOL, "VR: print hand interaction events" );
idCVar vr_handsShowTouch( "vr_handsShowTouch", "0", CVAR_GAME | CVAR_BOOL, "VR: draw where the game thinks the hands are: touch spheres (green), palm collision box (yellow), controller (red)" );

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

	// small octahedron crystal
	idRenderModel *crystalModel = nullptr;
	idRenderModel *GetCrystalModel() {
		if ( crystalModel != nullptr ) {
			return crystalModel;
		}
		const float s = 0.30f;
		const idVec3 pts[6] = { idVec3( s, 0, 0 ), idVec3( -s, 0, 0 ), idVec3( 0, s * 0.8f, 0 ), idVec3( 0, -s * 0.8f, 0 ), idVec3( 0, 0, s * 1.4f ), idVec3( 0, 0, -s * 0.6f ) };
		const int faces[8][3] = { {0,2,4}, {2,1,4}, {1,3,4}, {3,0,4}, {2,0,5}, {1,2,5}, {3,1,5}, {0,3,5} };
		idRenderModel *model = renderModelManager->AllocModel();
		model->InitEmpty( "_vr_light_crystal" );
		modelSurface_t surf;
		surf.id = 0;
		surf.material = declManager->FindMaterial( "vr/light_crystal" );
		surf.geometry = model->AllocSurfaceTriangles( 24, 24 );
		srfTriangles_t *tri = surf.geometry;
		tri->numVerts = 0;
		tri->numIndexes = 0;
		tri->bounds.Clear();
		for ( int f = 0; f < 8; f++ ) {
			const idVec3 &a = pts[faces[f][0]], &b = pts[faces[f][1]], &c = pts[faces[f][2]];
			idVec3 n = ( b - a ).Cross( c - a );
			n.Normalize();
			idVec3 t = b - a;
			t.Normalize();
			const idVec3 *p[3] = { &a, &b, &c };
			for ( int v = 0; v < 3; v++ ) {
				idDrawVert &dv = tri->verts[tri->numVerts];
				dv.Clear();
				dv.xyz = *p[v];
				dv.st.Set( v == 1 ? 1.0f : 0.0f, v == 2 ? 1.0f : 0.0f );
				dv.normal = n;
				dv.tangents[0] = t;
				dv.tangents[1] = n.Cross( t );
				dv.color[0] = dv.color[1] = dv.color[2] = dv.color[3] = 255;
				tri->indexes[tri->numIndexes++] = tri->numVerts;
				tri->bounds.AddPoint( *p[v] );
				tri->numVerts++;
			}
		}
		tri->tangentsCalculated = true;
		tri->facePlanesCalculated = false;
		tri->generateNormals = false;
		model->AddSurface( surf );
		crystalModel = model;
		return model;
	}

	// ring band around the finger: a torus around the x axis
	idRenderModel *ringModel = nullptr;
	float ringModelRadius = -1.0f;
	idRenderModel *GetRingModel( float radius ) {
		if ( ringModel != nullptr && ringModelRadius == radius ) {
			return ringModel;
		}
		const int major = 24, minor = 8;
		const float tube = 0.08f;
		idRenderModel *model = renderModelManager->AllocModel();
		model->InitEmpty( va( "_vr_light_ring_%.2f", radius ) );
		modelSurface_t surf;
		surf.id = 0;
		surf.material = declManager->FindMaterial( "vr/light_ring_band" );
		surf.geometry = model->AllocSurfaceTriangles( ( major + 1 ) * ( minor + 1 ), major * minor * 6 );
		srfTriangles_t *tri = surf.geometry;
		tri->numVerts = 0;
		tri->numIndexes = 0;
		tri->bounds.Clear();
		for ( int i = 0; i <= major; i++ ) {
			const float a = idMath::TWO_PI * i / major;
			const idVec3 dir( 0.0f, idMath::Cos( a ), idMath::Sin( a ) );
			const idVec3 tangent( 0.0f, -idMath::Sin( a ), idMath::Cos( a ) );
			for ( int j = 0; j <= minor; j++ ) {
				const float b = idMath::TWO_PI * j / minor;
				const idVec3 n = dir * idMath::Cos( b ) + idVec3( idMath::Sin( b ), 0.0f, 0.0f );
				idDrawVert &dv = tri->verts[tri->numVerts++];
				dv.Clear();
				dv.xyz = dir * radius + n * tube;
				dv.st.Set( float( i ) / major, float( j ) / minor );
				dv.normal = n;
				dv.tangents[0] = tangent;
				dv.tangents[1] = n.Cross( tangent );
				dv.color[0] = dv.color[1] = dv.color[2] = dv.color[3] = 255;
				tri->bounds.AddPoint( dv.xyz );
			}
		}
		for ( int i = 0; i < major; i++ ) {
			for ( int j = 0; j < minor; j++ ) {
				const int v0 = i * ( minor + 1 ) + j, v1 = v0 + minor + 1;
				glIndex_t *idx = tri->indexes + tri->numIndexes;
				idx[0] = v0; idx[1] = v0 + 1; idx[2] = v1;
				idx[3] = v1; idx[4] = v0 + 1; idx[5] = v1 + 1;
				tri->numIndexes += 6;
			}
		}
		tri->tangentsCalculated = true;
		tri->facePlanesCalculated = false;
		tri->generateNormals = false;
		model->AddSurface( surf );
		ringModel = model;
		ringModelRadius = radius;
		return model;
	}

	bool IsInventoryItem( idEntity *ent ) {
		const char *invName = ent->spawnArgs.GetString( "inv_name", "" );
		return invName != nullptr && invName[0] != '\0';
	}

	// an arrow lying around or stuck in something (the arrow's ammo pickup), not yet a VR arrow
	bool IsArrowPickup( idEntity *ent ) {
		if ( ent == nullptr || !ent->IsType( idMoveable::Type ) || idVRBow::IsArrow( ent ) ) {
			return false;
		}
		const idStr weaponName = ent->spawnArgs.GetString( "inv_weapon_name" );
		if ( weaponName.IsEmpty() ) {
			return false;
		}
		const idDict *weaponDef = gameLocal.FindEntityDefDict( va( "atdm:weapon_%s", weaponName.c_str() ), false );
		if ( weaponDef != nullptr ) {
			return idStr::FindText( weaponDef->GetString( "weapon_scriptobject" ), "arrow", false ) >= 0;
		}
		return idStr::FindText( ent->spawnArgs.GetString( "classname" ), "arrow", false ) >= 0;
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
	glove.Free();
	gloveTried = false;
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
	// the hand touches with its palm and every finger (the finger points follow the curled fingers)
	idList<idVRHandModel::touchPoint_t> points;
	if ( glove.IsValid() ) {
		glove.GetTouchPoints( physPos, input.axis, points );
	} else {
		idVRHandModel::touchPoint_t p;
		p.pos = physPos;
		p.radius = vr_handTouchRadius.GetFloat();
		p.palm = true;
		points.Append( p );
	}

	static idClipModel *sphereClip[2] = { nullptr, nullptr };
	static float sphereRadius[2] = { -1.0f, -1.0f };
	const int mask = CONTENTS_SOLID | CONTENTS_OPAQUE | CONTENTS_BODY | CONTENTS_CORPSE | CONTENTS_RENDERMODEL | CONTENTS_FROBABLE;
	const float margin = vr_handTouchMargin.GetFloat();

	struct candidate_t { idEntity *ent; int bodyId; float score; float dist; };
	idList<candidate_t> candidates;

	for ( int i = 0; i < points.Num(); i++ ) {
		const float r = points[i].radius + margin;
		const int slot = points[i].palm ? 0 : 1;
		if ( sphereClip[slot] == nullptr || sphereRadius[slot] != r ) {
			delete sphereClip[slot];
			idTraceModel trm;
			trm.SetupDodecahedron( r * 2 );
			sphereClip[slot] = new idClipModel( trm );
			sphereRadius[slot] = r;
		}
		idBounds bounds( points[i].pos );
		bounds.ExpandSelf( r );
		idClip_ClipModelList clipModels;
		gameLocal.clip.ClipModelsTouchingBounds( bounds, mask, clipModels );
		for ( int c = 0; c < clipModels.Num(); c++ ) {
			idClipModel *cm = clipModels[c];
			idEntity *ent = cm->GetEntity();
			if ( ent == nullptr || ent == player || ent->IsHidden() ) {
				continue;
			}
			idEntity *target = InteractionTarget( ent );
			if ( !target || !target->m_bFrobable || target->IsHidden() ) {
				continue;
			}
			if ( !gameLocal.clip.ContentsModel( points[i].pos, sphereClip[slot], mat3_identity, -1, cm->Handle(), cm->GetOrigin(), cm->GetAxis() ) ) {
				continue;
			}
			// doors are only held at their handle or latch (the rest is just solid)
			if ( !idVRMovers::CanGripAt( target, points[i].pos ) ) {
				continue;
			}
			int body = 0;
			if ( target->IsType( idAFEntity_Base::Type ) && ent == target ) {
				body = Max( 0, static_cast<idAFEntity_Base *>( target )->BodyForClipModelId( cm->GetId() ) );
			}
			const float dist = ( cm->GetAbsBounds().GetCenter() - physPos ).LengthFast();
			float score = points[i].palm ? 2.0f : 1.0f;
			if ( target->IsType( CFrobHandle::Type ) ) {
				score *= 4.0f;		// a handle wins over the door it sits on
			}
			bool found = false;
			for ( int k = 0; k < candidates.Num(); k++ ) {
				if ( candidates[k].ent == target ) {
					candidates[k].score += score;
					if ( dist < candidates[k].dist ) {
						candidates[k].dist = dist;
						candidates[k].bodyId = body;
					}
					found = true;
					break;
				}
			}
			if ( !found ) {
				candidates.Append( { target, body, score, dist } );
			}
		}
	}

	// the thing touched by most of the hand wins (the palm counts double), then the nearest
	idEntity *best = nullptr;
	float bestScore = -1.0f, bestDist = idMath::INFINITY;
	bodyId = 0;
	for ( const candidate_t &c : candidates ) {
		if ( c.score > bestScore || ( c.score == bestScore && c.dist < bestDist ) ) {
			best = c.ent;
			bestScore = c.score;
			bestDist = c.dist;
			bodyId = c.bodyId;
		}
	}
	return best;
}

/*
 * The free hand is stopped by solid things: a palm-sized box (around the palm, not the whole open hand)
 * moves from where it was towards the controller and slides along whatever it hits.
 */
void idVRHand::MoveFreeHand( idPlayer *player ) {
	const idVec3 &target = input.pos;
	if ( !vr_handCollision.GetBool() ) {
		physPos = target;
		physValid = true;
		return;
	}

	// palm box in hand space: x = across the hand, y = out of the back of the left hand, z = towards the wrist
	const float side = index == 0 ? 1.0f : -1.0f;
	const idVec3 palmOffset( 0.0f, 1.3f * side, 2.2f );
	const idBounds palmBounds( idVec3( -1.8f, -0.75f, -2.4f ), idVec3( 1.8f, 0.75f, 2.4f ) );
	static idClipModel *palmClip = nullptr;
	if ( palmClip == nullptr ) {
		palmClip = new idClipModel( idTraceModel( palmBounds ) );
	}
	const idMat3 &axis = input.axis;
	const idVec3 targetPalm = target + palmOffset * axis;

	auto blocked = [&]( const idVec3 &palm ) {
		return ( gameLocal.clip.Contents( palm, palmClip, axis, MASK_SOLID, player ) & MASK_SOLID ) != 0;
	};

	idVec3 start;
	if ( physValid && ( physPos - target ).LengthSqr() < Square( 24.0f ) ) {
		start = physPos + palmOffset * axis;
	} else {
		start = input.eye;
	}
	if ( blocked( start ) ) {
		// turned into something, or a door swung into the hand: take the controller position if that is free,
		// otherwise reach out from the head again
		if ( !blocked( targetPalm ) ) {
			physPos = target;
			physValid = true;
			return;
		}
		start = input.eye;
		if ( blocked( start ) ) {
			return;		// keep the last free position
		}
	}

	trace_t tr;
	idVec3 pos = start;
	idVec3 move = targetPalm - start;
	for ( int bump = 0; bump < 3 && move.LengthSqr() > Square( 0.01f ); bump++ ) {
		gameLocal.clip.Translation( tr, pos, pos + move, palmClip, axis, MASK_SOLID, player );
		pos = tr.endpos;
		if ( tr.fraction >= 1.0f ) {
			break;
		}
		BumpObject( player, tr );
		move *= ( 1.0f - tr.fraction );
		move -= ( move * tr.c.normal ) * tr.c.normal;
	}
	physPos = pos - palmOffset * axis;
	physValid = true;
}

/*
 * A free hand ran into something loose. It gets the velocity change a collision between the hand (a body of
 * vr_handPushMass moving at the controller's velocity) and the object would give, using the object's
 * size-based mass. Only quick movements push: reaching for something slowly doesn't knock it away.
 */
void idVRHand::BumpObject( idPlayer *player, const trace_t &tr ) {
	if ( tr.c.entityNum < 0 || tr.c.entityNum >= ENTITYNUM_WORLD ) {
		return;
	}
	idEntity *ent = gameLocal.entities[tr.c.entityNum];
	if ( ent == nullptr || ent == player || ent->IsHidden() ) {
		return;
	}
	// open doors, lids and drawers give way to the hand (also to a fist, also slowly)
	CBinaryFrobMover *mover = idVRMovers::GetPushMover( ent );
	if ( mover != nullptr ) {
		player->vrHands.GetMovers().Push( mover, tr.c.point, tr.c.normal, input.vel );
		return;
	}
	if ( gripDown ) {
		return;		// closing the hand to grab, not slapping
	}
	idPhysics *phys = ent->GetPhysics();
	if ( !phys->IsType( idPhysics_RigidBody::Type ) && !phys->IsType( idPhysics_AF::Type ) ) {
		return;
	}
	if ( player->vrHands.CountHolding( ent ) > 0 ) {
		return;
	}
	int bodyId = 0;
	if ( ent->IsType( idAFEntity_Base::Type ) ) {
		bodyId = Max( 0, static_cast<idAFEntity_Base *>( ent )->BodyForClipModelId( tr.c.id ) );
	}
	impactInfo_t info;
	phys->GetImpactInfo( bodyId, tr.c.point, &info );
	if ( info.invMass <= 0.0f ) {
		return;
	}
	// the normal points from the object towards the hand
	const idVec3 &n = tr.c.normal;
	const float approach = -( ( input.vel - info.velocity ) * n );
	if ( approach < vr_handPushMinSpeed.GetFloat() ) {
		return;
	}
	const float handMass = Max( 0.1f, vr_handPushMass.GetFloat() );
	const float objMass = VR_EffectiveMass( ent, bodyId );
	const float dv = ( 1.0f + vr_handPushBounce.GetFloat() ) * approach * handMass / ( handMass + objMass );
	ent->m_SetInMotionByActor = player;
	ent->m_MovedByActor = player;
	// the engine applies impulses with the object's own mass: scale so the object gets exactly dv
	ent->ApplyImpulse( player, bodyId, tr.c.point, -n * ( dv / info.invMass ) );
	HandsDebug( "hand %d bumps %s (%.0f units/s)", index, ent->name.c_str(), dv );
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

void idVRHand::HoldEntity( idPlayer *player, idEntity *ent, int bodyId, const idVec3 &anchor, const idMat3 &handAxisInBody, const idVec3 &controllerOffset ) {
	if ( ent == nullptr ) {
		return;
	}
	touched = nullptr;
	held = ent;
	heldBodyId = bodyId;
	gripDown = true;
	slipTime = 0;
	carryFromPouch = false;
	carryKinematic = false;
	if ( CanHold( ent ) && ( idVRLockUse::IsWeightless( ent ) || VR_IsSmallObject( ent ) ) ) {
		// weightless (tools, keys, lockpicks, anything smaller than a vase): follows the hand exactly, like a coin
		carryRelAxis = handAxisInBody.Transpose();
		carryRelOrigin = controllerOffset - anchor * carryRelAxis;
		carryHomeOrigin = ent->GetPhysics()->GetOrigin();
		carryHomeAxis = ent->GetPhysics()->GetAxis();
		carryKinematic = true;
		interaction = IK_CARRY;
		ent->SetAxis( carryRelAxis * input.axis );
		ent->SetOrigin( input.pos + carryRelOrigin * input.axis );
		ent->GetPhysics()->PutToRest();
	} else if ( CanHold( ent ) ) {
		grip.anchor = anchor;
		grip.handAxis = handAxisInBody;
		grip.controllerOffset = controllerOffset;
		grip.contactPoint = anchor;
		grip.contactNormal.Zero();
		interaction = IK_HOLD;
		ent->GetPhysics()->SetLinearVelocity( input.vel, bodyId );
		ent->GetPhysics()->Activate();
	} else {
		// not a physics object: carried directly, and back into the pouch when let go
		const idMat3 ctrlAxisT = input.axis.Transpose();
		carryHomeOrigin = ent->GetPhysics()->GetOrigin();
		carryHomeAxis = ent->GetPhysics()->GetAxis();
		carryRelOrigin = ( carryHomeOrigin - input.pos ) * ctrlAxisT;
		carryRelAxis = carryHomeAxis * ctrlAxisT;
		carryFromPouch = true;
		interaction = IK_CARRY;
	}
	player->vrHands.MakeNonSolidToPlayer( ent );
	HandsDebug( "hand %d takes %s", index, ent->name.c_str() );
}

void idVRHand::StartInteraction( idPlayer *player ) {
	idEntity *ent = touched.GetEntity();
	if ( ent == nullptr ) {
		return;
	}

	// a bow placed in the map turns into the VR bow
	if ( idVRBow::IsWorldBow( ent ) && player->vrHands.BowTakeWorld( player, index, ent ) ) {
		return;
	}

	if ( idVRTools::IsTool( ent ) && CanHold( ent ) ) {
		// a dropped tool: back into its fixed grip
		idVec3 anchor;
		idMat3 handAxisInTool;
		idVRTools::GetSnapGrip( ent, anchor, handAxisInTool );
		HoldEntity( player, ent, 0, anchor, handAxisInTool, idVec3( 0, 0, 0 ) );
		return;
	}

	if ( IsArrowPickup( ent ) ) {
		const idStr weaponName = ent->spawnArgs.GetString( "inv_weapon_name" );
		if ( weaponName == "ropearrow" || weaponName == "vinearrow" ) {
			// a rope or vine hangs from it: taken back like in the normal game (the rope goes with it)
			HandsDebug( "hand %d takes back %s", index, ent->name.c_str() );
			player->m_FrobEntity = ent;
			player->PerformFrob( EPressed, ent, false );
			player->m_FrobEntity = nullptr;
			return;
		}
		// an arrow lying around or stuck in something becomes a VR arrow: pulled out of what it's stuck in,
		// weightless and passing through the world while held, can be nocked, stored at the stomach or put back
		// at the right shoulder (one arrow of its type)
		if ( ent->GetBindMaster() != nullptr ) {
			ent->Unbind();
		}
		ent->spawnArgs.Set( "vr_arrow", weaponName.c_str() );
		ent->spawnArgs.Set( "vr_weightless", "1" );
		ent->spawnArgs.Set( "removeWithMaster", "0" );
		HandsDebug( "hand %d pulls out %s (%s)", index, ent->name.c_str(), weaponName.c_str() );
	}

	if ( ( idVRLockUse::IsWeightless( ent ) || VR_IsSmallObject( ent ) ) && CanHold( ent ) ) {
		// a key, lockpick or anything smaller than a vase: held exactly where the hand touched it, weightless
		const idVec3 bodyOrigin = ent->GetPhysics()->GetOrigin();
		const idMat3 bodyAxisT = ent->GetPhysics()->GetAxis().Transpose();
		HoldEntity( player, ent, 0, ( physPos - bodyOrigin ) * bodyAxisT, input.axis * bodyAxisT, ( physPos - input.pos ) * input.axis.Transpose() );
		// weightless things are in one hand only: grabbing it with the other hand takes it over
		player->vrHands.TakeOver( player, index, ent );
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
		HandsDebug( "hand %d grips %s (body %d, feels like %.2f, game mass %.2f)", index, ent->name.c_str(), heldBodyId, VR_EffectiveMass( ent, heldBodyId ), phys->GetMass( heldBodyId ) );
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

	CBinaryFrobMover *mover = idVRMovers::GetMover( ent );
	if ( mover != nullptr ) {
		// a door (at its handle or latch), lid, drawer...: held where the hand is, moved along its path
		held = mover;
		heldBodyId = 0;
		moverPart = ent;
		idPhysics *phys = mover->GetPhysics();
		const idMat3 bodyAxisT = phys->GetAxis().Transpose();
		grip.anchor = ( physPos - phys->GetOrigin() ) * bodyAxisT;
		grip.handAxis = input.axis * bodyAxisT;
		grip.controllerOffset = ( physPos - input.pos ) * input.axis.Transpose();
		grip.contactPoint = grip.anchor;
		grip.contactNormal.Zero();
		slipTime = 0;
		latchHeld = false;
		interaction = IK_MOVER;
		HandsDebug( "hand %d holds %s at %s (%s)", index, mover->name.c_str(), ent->name.c_str(), mover->IsOpen() ? "open" : "latched" );
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
	lockUse.End( player );
	idEntity *ent = held.GetEntity();
	const interactionKind_t kind = interaction;
	// the free hand continues from where the drawn hand was
	if ( ( kind == IK_HOLD || kind == IK_MOVER ) && ent != nullptr ) {
		idMat3 a;
		GetGripWorld( physPos, a );
	}
	interaction = IK_NONE;
	held = nullptr;

	if ( kind == IK_BOW || kind == IK_STRING ) {
		// the bow goes back to the shoulder / the string is let go (shot)
		physPos = visualPos;
		physValid = true;
		player->vrHands.BowRelease( player, index );
		return;
	}

	if ( ent == nullptr ) {
		return;
	}

	if ( kind == IK_MOVER ) {
		// a let go door keeps swinging (idVRMovers keeps its speed)
		PressHandle( false );
		latchHeld = false;
		moverPart = nullptr;
		HandsDebug( "hand %d lets go of %s", index, ent->name.c_str() );
		return;
	}

	if ( kind == IK_FROB ) {
		ent->FrobReleased( true, false, 0 );
		player->m_FrobEntity = ent;
		player->PerformFrob( EReleased, ent, true );
		player->m_FrobEntity = nullptr;
		return;
	}

	// tools go back into their holster (or fall, see vr_toolsReturnToHolster)
	if ( ( kind == IK_HOLD || kind == IK_CARRY ) && player->vrHands.CountHolding( ent ) == 0 && player->vrHands.OnToolRelease( player, index, ent ) ) {
		carryKinematic = false;
		player->vrHands.ReleaseNonSolidToPlayer( ent, true );
		return;
	}

	// is the other hand still holding it? (this hand already let go)
	const bool otherHolds = player->vrHands.CountHolding( ent ) > 0;

	// an arrow let go of at the right shoulder goes back into the quiver (like at the stomach)
	bool atQuiver = false;
	if ( allowInventory && idVRBow::IsArrow( ent ) ) {
		idVec3 shoulder;
		if ( player->vrHands.GetTools().GetShoulderPos( player, false, shoulder ) ) {
			atQuiver = ( input.pos - shoulder ).LengthSqr() < Square( idVRTools::GetShoulderRadius() );
		}
	}

	if ( allowInventory && ( stomachRelease || atQuiver ) && IsInventoryItem( ent ) && !otherHolds ) {
		HandsDebug( "hand %d stores %s%s", index, ent->name.c_str(), atQuiver ? " (quiver)" : "" );
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

	if ( kind == IK_CARRY && carryKinematic ) {
		// a weightless physics object: back to normal physics, with the hand's motion (throws)
		carryKinematic = false;
		if ( !otherHolds ) {
			idPhysics *phys = ent->GetPhysics();
			phys->Activate();
			phys->SetLinearVelocity( input.vel );
			phys->SetAngularVelocity( input.angVel );
		}
		// every hand that took it gives its share back (solid again once nobody holds it)
		player->vrHands.ReleaseNonSolidToPlayer( ent, false );
		HandsDebug( "hand %d lets go of %s", index, ent->name.c_str() );
		return;
	}
	if ( kind == IK_CARRY && !otherHolds && carryFromPouch ) {
		// an item from the pouch that can't lie in the world on its own goes back into the pouch
		carryFromPouch = false;
		player->vrHands.ReleaseNonSolidToPlayer( ent, true );
		player->m_FrobEntity = ent;
		player->PerformFrob( EPressed, ent, false );
		player->m_FrobEntity = nullptr;
		return;
	}
	if ( kind == IK_CARRY && !otherHolds ) {
		// static items can't fall; they go back to where they were taken from
		ent->SetOrigin( carryHomeOrigin );
		ent->SetAxis( carryHomeAxis );
	}

	// every hand that took it gives its share back (solid again once nobody holds it)
	player->vrHands.ReleaseNonSolidToPlayer( ent, false );
	if ( kind == IK_HOLD ) {
		// the object keeps the motion the hand gave it last frame (it was moving with the controller)
		HandsDebug( "hand %d lets go of %s at %.0f units/s", index, ent->name.c_str(), ent->GetPhysics()->GetLinearVelocity( heldBodyId ).Length() );
	} else {
		HandsDebug( "hand %d releases %s", index, ent->name.c_str() );
	}
}

/*
 * Bow states (idVRBow decides when): the hand holds the bow, or is on its string. Nothing else is held then;
 * an arrow that is nocked belongs to the bow (it is placed by the bow every tick).
 */
void idVRHand::BeginBow() {
	interaction = IK_BOW;
	held = nullptr;
	carryKinematic = false;
	carryFromPouch = false;
	touched = nullptr;
}

void idVRHand::BeginString( idEntity *arrow ) {
	interaction = IK_STRING;
	held = arrow;
	carryKinematic = false;
	carryFromPouch = false;
	touched = nullptr;
	overridePos = input.pos;
	overrideAxis = input.axis;
}

void idVRHand::EndBowState() {
	if ( interaction == IK_BOW || interaction == IK_STRING ) {
		physPos = visualPos;
		physValid = true;
		interaction = IK_NONE;
		held = nullptr;
	}
}

/*
 * The visible handle turns down while the trigger holds it (like a hand pressing a door handle) and comes
 * back up when it is let go. Only door handles move; lids and drawers have no handle part.
 */
void idVRHand::PressHandle( bool down ) {
	idEntity *part = moverPart.GetEntity();
	if ( part == nullptr || !part->IsType( CFrobHandle::Type ) ) {
		return;
	}
	CFrobHandle *handle = static_cast<CFrobHandle *>( part );
	if ( handle->IsChangingState() ) {
		return;		// the handle is busy with its own animation
	}
	handle->SetFractionalPosition( down ? 1.0f : 0.0f, false );
	if ( down && handle->IsType( CFrobDoorHandle::Type ) && handle->IsMasterHandle() ) {
		// the handle's own sound, like turning it in the normal game
		CFrobDoor *door = static_cast<CFrobDoorHandle *>( handle )->GetDoor();
		if ( door != nullptr ) {
			handle->FrobMoverStartSound( door->IsLocked() ? "snd_tap_locked" : "snd_tap_default" );
		}
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
	memset( &renderEntity, 0, sizeof( renderEntity ) );
	if ( glove.IsValid() ) {
		glove.FillRenderEntity( renderEntity );
	} else {
		if ( model == nullptr ) {
			model = GetHandModel( index );
		}
		renderEntity.hModel = model;
		renderEntity.origin = visualPos;
		renderEntity.axis = visualAxis;
		renderEntity.bounds = model->Bounds();
	}
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

/*
 * Fingers: an empty hand follows the controller buttons (grip curls the lower three fingers around the handle,
 * the trigger curls the index finger). A hand holding something curls each finger until it touches the object.
 */
void idVRHand::UpdateFingers( float dt ) {
	if ( !glove.IsValid() ) {
		return;
	}
	float targets[idVRHandModel::NUM_FINGERS];
	idEntity *ent = held.GetEntity();
	if ( ( interaction == IK_HOLD || interaction == IK_CARRY ) && ent != nullptr ) {
		glove.FindContactCurls( ent->GetPhysics()->GetClipModel( interaction == IK_HOLD ? heldBodyId : 0 ), 85.0f, targets );
	} else if ( interaction == IK_BOW ) {
		// a fist around the bow's grip
		targets[idVRHandModel::THUMB] = 40.0f;
		targets[idVRHandModel::INDEX] = 72.0f;
		targets[idVRHandModel::MIDDLE] = 75.0f;
		targets[idVRHandModel::RING] = 78.0f;
		targets[idVRHandModel::PINKY] = 80.0f;
	} else if ( interaction == IK_STRING ) {
		// the string pinched between thumb and index finger, the other fingers tucked away
		targets[idVRHandModel::THUMB] = 45.0f;
		targets[idVRHandModel::INDEX] = 55.0f;
		targets[idVRHandModel::MIDDLE] = 60.0f;
		targets[idVRHandModel::RING] = 85.0f;
		targets[idVRHandModel::PINKY] = 88.0f;
	} else if ( interaction == IK_MOVER && moverPart.GetEntity() != nullptr ) {
		glove.FindContactCurls( moverPart.GetEntity()->GetPhysics()->GetClipModel(), 85.0f, targets );
	} else if ( interaction == IK_FROB ) {
		for ( int f = 0; f < idVRHandModel::NUM_FINGERS; f++ ) {
			targets[f] = 35.0f + input.grip * 30.0f;
		}
	} else {
		const float relaxed = 10.0f;
		targets[idVRHandModel::THUMB] = relaxed + input.grip * 25.0f;
		targets[idVRHandModel::INDEX] = relaxed + input.trigger * 65.0f;
		targets[idVRHandModel::MIDDLE] = relaxed + input.grip * 55.0f;
		targets[idVRHandModel::RING] = relaxed + input.grip * 58.0f;
		targets[idVRHandModel::PINKY] = relaxed + input.grip * 62.0f;
	}
	glove.MoveFingers( targets, dt );
}

void idVRHand::UpdateVisual( float dt ) {
	if ( !gloveTried ) {
		gloveTried = true;
		glove.Init( index );
	}
	if ( tracked ) {
		switch ( interaction ) {
		case IK_HOLD:
		case IK_MOVER:
			// the drawn hand stays on the grip point, even if the controller moved on
			GetGripWorld( visualPos, visualAxis );
			break;
		case IK_CARRY:
		case IK_BOW:
			visualPos = input.pos;
			visualAxis = input.axis;
			break;
		case IK_STRING:
			// on the bow's string
			visualPos = overridePos;
			visualAxis = overrideAxis;
			break;
		default:
			visualPos = physValid ? physPos : input.pos;
			visualAxis = input.axis;
			break;
		}
		if ( glove.IsValid() ) {
			glove.SetPose( visualPos, visualAxis );
			UpdateFingers( dt );
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

	const interactionKind_t interactionBefore = interaction;

	// free hands are stopped by the world
	if ( interaction != IK_HOLD && interaction != IK_MOVER && interaction != IK_BOW && interaction != IK_STRING ) {
		MoveFreeHand( player );
	}

	// what is the hand touching?
	int bodyId = 0;
	idEntity *nowTouching = interaction == IK_NONE ? FindTouchedEntity( player, bodyId ) : nullptr;

	if ( vr_handsShowTouch.GetBool() && gameRenderWorld ) {
		idList<idVRHandModel::touchPoint_t> points;
		if ( glove.IsValid() ) {
			glove.GetTouchPoints( physPos, input.axis, points );
		}
		for ( const idVRHandModel::touchPoint_t &p : points ) {
			gameRenderWorld->DebugSphere( nowTouching ? colorGreen : colorCyan, idSphere( p.pos, p.radius + vr_handTouchMargin.GetFloat() ), 0, false );
		}
		const float side = index == 0 ? 1.0f : -1.0f;
		const idVec3 palmOffset( 0.0f, 1.3f * side, 2.2f );
		gameRenderWorld->DebugBox( colorYellow, idBox( idBounds( idVec3( -1.8f, -0.75f, -2.4f ), idVec3( 1.8f, 0.75f, 2.4f ) ), physPos + palmOffset * input.axis, input.axis ), 0 );
		gameRenderWorld->DebugSphere( colorRed, idSphere( input.pos, 0.4f ), 0, false );
	}
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
		// the pouch (left hand) and the holsters get the first chance, then whatever the hand touches
		if ( !player->vrHands.HandleGripPress( player, index ) ) {
			StartInteraction( player );
		}
	} else if ( !gripDown && wasGrip && interaction != IK_NONE ) {
		Release( player, true );
	}

	// trigger uses the held object
	if ( triggerDown && !wasTrigger && ( interaction == IK_HOLD || interaction == IK_CARRY ) ) {
		idEntity *ent = held.GetEntity();
		idStr script;
		if ( ent && lockUse.Begin( player, ent ) ) {
			// a key or lockpick held against a lock: used on it like in the normal game
			HandsDebug( "hand %d uses %s on a lock", index, ent->name.c_str() );
		} else if ( ent && ent->spawnArgs.GetString( "equip_action_script", "", script ) ) {
			HandsDebug( "hand %d uses %s (%s)", index, ent->name.c_str(), script.c_str() );
			idThread *thread = player->CallScriptFunctionArgs( script.c_str(), true, 0, "e", ent );
			if ( thread != nullptr ) {
				thread->Execute();
			}
		}
	}

	// a held door handle (or lid, drawer): the trigger presses it down, which opens the latch while held
	if ( interaction == IK_MOVER ) {
		CBinaryFrobMover *mover = static_cast<CBinaryFrobMover *>( held.GetEntity() );
		// (a trigger already squeezed when the hand closes on the handle presses it right away)
		if ( triggerDown && ( !wasTrigger || interactionBefore != IK_MOVER ) && mover != nullptr ) {
			latchHeld = mover->IsOpen() || idVRMovers::TryUnlatch( mover );
			PressHandle( true );
		} else if ( !triggerDown && wasTrigger ) {
			latchHeld = false;
			PressHandle( false );
		}
	}

	// lockpicking goes on while the trigger is held at the lock
	if ( lockUse.IsActive() ) {
		if ( triggerDown && ( interaction == IK_HOLD || interaction == IK_CARRY ) ) {
			lockUse.Continue( player, held.GetEntity() );
		} else {
			lockUse.End( player );
		}
	}

	// carried items (static ones, weightless tools and keys) follow the controller directly
	if ( interaction == IK_CARRY ) {
		idEntity *ent = held.GetEntity();
		if ( ent == nullptr ) {
			interaction = IK_NONE;
		} else {
			ent->SetAxis( carryRelAxis * in.axis );
			ent->SetOrigin( in.pos + carryRelOrigin * in.axis );
			if ( carryKinematic ) {
				ent->GetPhysics()->PutToRest();
			}
		}
	}

	if ( interaction == IK_MOVER ) {
		CBinaryFrobMover *mover = static_cast<CBinaryFrobMover *>( held.GetEntity() );
		if ( mover == nullptr || mover->IsHidden() || idVRMovers::IsBusy( mover ) ) {
			// gone, or an AI / script moves it now
			HandsDebug( "hand %d: %s taken away", index, mover ? mover->name.c_str() : "mover" );
			Release( player, false );
		} else {
			idVRMovers &movers = player->vrHands.GetMovers();
			movers.Hold( mover, movers.FindFraction( mover, grip.anchor, GetGripTarget() ), latchHeld );
			// pulled too far from it (blocked, or latched shut): the hand slips off
			idVec3 point;
			idMat3 a;
			GetGripWorld( point, a );
			if ( ( point - GetGripTarget() ).LengthSqr() > Square( vr_gripBreakDistance.GetFloat() ) ) {
				slipTime += idMath::FtoiRound( dt * 1000 );
				if ( slipTime > vr_gripBreakTime.GetInteger() ) {
					HandsDebug( "hand %d slips off %s", index, mover->name.c_str() );
					Release( player, false );
				}
			} else {
				slipTime = 0;
			}
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
	movers.Clear();
	bow.Reset();
	arrowSelector.Reset();
	melee.Reset();
	readables.Reset();
	lastTime = 0;
	lastDt = 0.0f;
	initialized = true;
}

void idVRHands::Shutdown() {
	if ( !initialized ) {
		return;
	}
	pouch.Free();
	arrowSelector.Free();
	bow.Free();
	melee.Free();
	readables.Free();
	if ( crystalHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( crystalHandle );
	}
	crystalHandle = -1;
	if ( ringHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( ringHandle );
	}
	ringHandle = -1;
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
 * Small things are weightless in the hand. "Small" is measured against a reference object, a vase
 * (vr_weightlessReference, an entity def): anything with less volume than its model, and not much longer
 * than it, follows the hand exactly like a coin (spoons, candlesticks, bottles, books...). As big as the vase
 * or bigger keeps the weighted handling. Bodies always keep it.
 */
idCVar vr_weightlessReference( "vr_weightlessReference", "atdm:moveable_vase1", CVAR_GAME, "VR: entity def whose size decides what is weightless in the hand: smaller things are, as big or bigger keep their weight" );
idCVar vr_weightlessLengthFactor( "vr_weightlessLengthFactor", "1.5", CVAR_GAME | CVAR_FLOAT, "VR: weightless things also have to be shorter than the reference object's longest side times this" );

bool VR_IsSmallObject( idEntity *ent ) {
	if ( ent == nullptr || ( !ent->IsType( idMoveable::Type ) && !ent->IsType( idMoveableItem::Type ) ) ) {
		return false;
	}
	static idStr refName;
	static float refVolume = 0.0f;
	static float refLongest = 0.0f;
	if ( refName != vr_weightlessReference.GetString() || refVolume <= 0.0f ) {
		refName = vr_weightlessReference.GetString();
		// fallback: a vase about 37 cm tall and 18 cm wide
		idVec3 size( 8.0f, 8.0f, 16.0f );
		const idDict *def = gameLocal.FindEntityDefDict( refName.c_str(), false );
		if ( def != nullptr ) {
			const char *modelName = def->GetString( "clipmodel", def->GetString( "model" ) );
			idRenderModel *model = modelName[0] ? renderModelManager->CheckModel( modelName ) : nullptr;
			if ( model == nullptr && modelName[0] ) {
				model = renderModelManager->FindModel( modelName );
			}
			if ( model != nullptr && !model->IsDefaultModel() ) {
				const idBounds b = model->Bounds();
				if ( !b.IsCleared() && ( b[1] - b[0] ).x > 0.0f ) {
					size = b[1] - b[0];
				}
			}
		}
		refVolume = size.x * size.y * size.z;
		refLongest = Max( size.x, Max( size.y, size.z ) );
		gameLocal.Printf( "VR hands: weightless below %s (%.1f x %.1f x %.1f units)\n", refName.c_str(), size.x, size.y, size.z );
	}
	const idBounds &b = ent->GetPhysics()->GetBounds();
	const idVec3 size = b[1] - b[0];
	const float volume = size.x * size.y * size.z;
	const float longest = Max( size.x, Max( size.y, size.z ) );
	return volume < refVolume && longest < refLongest * vr_weightlessLengthFactor.GetFloat();
}

void idVRHands::TakeOver( idPlayer *player, int handIndex, idEntity *ent ) {
	idVRHand &other = hands[1 - handIndex];
	if ( other.GetHeld() == ent && other.GetInteraction() == idVRHand::IK_CARRY ) {
		other.stomachRelease = false;
		other.Release( player, false );
	}
}

/*
 * How heavy something feels in the hand. The game's own "mass" values are not reliable for this
 * (a gold bracelet can weigh more than a chair), so the weight is estimated from the object's size:
 * coins and bottles are light, chairs are a handful, big crates are heavy. Bodies are always heavy.
 */
float VR_EffectiveMass( idEntity *ent, int bodyId ) {
	if ( ent->spawnArgs.GetBool( "vr_weightless", "0" ) ) {
		return 0.05f;
	}
	idPhysics *phys = ent->GetPhysics();
	if ( phys->IsType( idPhysics_AF::Type ) ) {
		const float total = Max( 0.01f, phys->GetMass( -1 ) );
		return vr_handBodyMass.GetFloat() * phys->GetMass( bodyId ) / total;
	}
	const idBounds &b = phys->GetBounds( bodyId );
	const idVec3 size = b[1] - b[0];
	const float boxVolume = Max( 0.0f, size.x * size.y * size.z );
	// the material, not the air: an empty bucket weighs what its walls weigh
	const float volume = VR_MaterialVolume( ent, boxVolume );
	const float litres = volume * 0.01231f;	// 1 unit = 2.309 cm
	return idMath::ClampFloat( 0.05f, 80.0f, vr_handDensity.GetFloat() * idMath::Pow( litres, vr_handDensityExp.GetFloat() ) );
}

/*
 * How much material an object is made of (cubic units), from its visible model. Measured in the game the first
 * time a hand deals with an object of that model (so mappers' own models are measured too), then remembered;
 * models over vr_handWeightMaxTris triangles keep the box (no stutter on a very detailed model):
 *  - a closed mesh (a rock, a sack, a bucket modelled with thick walls): the volume it encloses
 *  - an open, single-skin mesh (most buckets, pots, bowls, chairs made of planks): its surface area times
 *    a wall thickness (vr_handWallThickness)
 * Never more than the object's box. Animated and missing models use the box.
 */
idCVar vr_handWallThickness( "vr_handWallThickness", "0.5", CVAR_GAME | CVAR_FLOAT, "VR: wall thickness (units, 1 unit = 2.3 cm) assumed for open, single-skin models (buckets, bowls) when estimating their weight" );
idCVar vr_handWeightFromModel( "vr_handWeightFromModel", "1", CVAR_GAME | CVAR_BOOL, "VR: weight from the model's material (ignores the air in buckets, chairs...). 0 = from the bounding box" );
idCVar vr_handWeightMaxTris( "vr_handWeightMaxTris", "40000", CVAR_GAME | CVAR_INTEGER, "VR: models with more triangles than this are weighed by their box (measuring them would take too long)" );

float VR_MaterialVolume( idEntity *ent, float boxVolume ) {
	if ( !vr_handWeightFromModel.GetBool() || boxVolume <= 0.0f ) {
		return boxVolume;
	}
	const renderEntity_t *re = ent->GetRenderEntity();
	idRenderModel *model = re != nullptr ? re->hModel : nullptr;
	if ( model == nullptr || model->IsDefaultModel() || model->IsDynamicModel() != DM_STATIC ) {
		return boxVolume;
	}

	struct cached_t { float enclosed; float area; bool closed; };
	static std::unordered_map<std::string, cached_t> cache;
	const std::string key = model->Name();
	auto it = cache.find( key );
	if ( it == cache.end() ) {
		cached_t c = { 0.0f, 0.0f, false };
		int numTris = 0;
		for ( int s = 0; s < model->NumSurfaces(); s++ ) {
			const modelSurface_t *surf = model->Surface( s );
			if ( surf != nullptr && surf->geometry != nullptr ) {
				numTris += surf->geometry->numIndexes / 3;
			}
		}
		if ( numTris > vr_handWeightMaxTris.GetInteger() ) {
			// too detailed to measure quickly: the box it is
			cache.emplace( key, c );
			return boxVolume;
		}
		// edges between welded positions: a closed mesh has every edge shared by exactly two triangles
		std::unordered_map<uint64_t, int> edges;
		std::unordered_map<uint64_t, int> weld;
		edges.reserve( numTris * 2 );
		weld.reserve( numTris );
		auto posId = [&]( const idVec3 &v ) -> int {
			const int64_t x = (int64_t)idMath::Floor( v.x * 50.0f + 0.5f );
			const int64_t y = (int64_t)idMath::Floor( v.y * 50.0f + 0.5f );
			const int64_t z = (int64_t)idMath::Floor( v.z * 50.0f + 0.5f );
			const uint64_t h = ( (uint64_t)( x & 0x1fffff ) << 42 ) | ( (uint64_t)( y & 0x1fffff ) << 21 ) | (uint64_t)( z & 0x1fffff );
			auto w = weld.find( h );
			if ( w != weld.end() ) {
				return w->second;
			}
			const int id = (int)weld.size();
			weld[h] = id;
			return id;
		};
		double enclosed = 0.0, area = 0.0;
		for ( int s = 0; s < model->NumSurfaces(); s++ ) {
			const modelSurface_t *surf = model->Surface( s );
			if ( surf == nullptr || surf->geometry == nullptr || surf->geometry->verts == nullptr || surf->geometry->indexes == nullptr ) {
				continue;
			}
			if ( surf->material != nullptr && !surf->material->IsDrawn() ) {
				continue;	// collision and other invisible surfaces
			}
			const srfTriangles_t *tri = surf->geometry;
			for ( int i = 0; i + 2 < tri->numIndexes; i += 3 ) {
				const idVec3 &a = tri->verts[tri->indexes[i + 0]].xyz;
				const idVec3 &b = tri->verts[tri->indexes[i + 1]].xyz;
				const idVec3 &c3 = tri->verts[tri->indexes[i + 2]].xyz;
				enclosed += a * b.Cross( c3 ) / 6.0;
				area += ( b - a ).Cross( c3 - a ).Length() * 0.5;
				const int ids[3] = { posId( a ), posId( b ), posId( c3 ) };
				for ( int e = 0; e < 3; e++ ) {
					const int p = Min( ids[e], ids[( e + 1 ) % 3] ), q = Max( ids[e], ids[( e + 1 ) % 3] );
					if ( p != q ) {
						edges[( (uint64_t)p << 32 ) | (uint64_t)q ]++;
					}
				}
			}
		}
		int twoSided = 0;
		for ( const auto &e : edges ) {
			if ( e.second == 2 ) {
				twoSided++;
			}
		}
		c.enclosed = (float)idMath::Fabs( (float)enclosed );
		c.area = (float)area;
		c.closed = !edges.empty() && twoSided >= (int)( edges.size() * 0.95f );
		it = cache.emplace( key, c ).first;
		if ( vr_handsDebug.GetBool() ) {
			gameLocal.Printf( "VR hands: %s is %s, %.0f cubic units enclosed, %.0f square units of surface\n", key.c_str(), c.closed ? "closed" : "open", c.enclosed, c.area );
		}
	}
	const cached_t &c = it->second;
	const float material = c.closed ? c.enclosed : c.area * vr_handWallThickness.GetFloat();
	if ( material <= 0.0f ) {
		return boxVolume;
	}
	return idMath::ClampFloat( boxVolume * 0.01f, boxVolume, material );
}

/*
 * Holding: every hand that holds a body says where the whole body should be (its grip point on the
 * controller, its orientation following the hand), two hands average their wishes. The body is then pushed
 * towards that pose with velocity changes that are limited like a real hand:
 *  - turning: fast, barely affected by weight
 *  - moving: limited by the hand's force against the (size-based) weight; light things follow exactly,
 *    heavy things lag and drag, two hands are twice as strong
 *  - holding up: each hand carries up to vr_handLiftMass against gravity
 */
void idVRHands::DriveHeldObjects( float dt ) {
	if ( dt <= 0 ) {
		return;
	}
	const idVec3 gravity = gameLocal.GetGravity();
	bool done[2] = { false, false };

	for ( int h = 0; h < 2; h++ ) {
		if ( done[h] || hands[h].GetInteraction() != idVRHand::IK_HOLD || hands[h].GetHeld() == nullptr ) {
			continue;
		}
		idEntity *ent = hands[h].GetHeld();
		const int bodyId = hands[h].GetHeldBodyId();
		int group[2];
		int n = 0;
		for ( int o = h; o < 2; o++ ) {
			if ( hands[o].GetInteraction() == idVRHand::IK_HOLD && hands[o].GetHeld() == ent && hands[o].GetHeldBodyId() == bodyId ) {
				group[n++] = o;
				done[o] = true;
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
		const idVec3 com = bodyOrigin - info.position;
		const idVec3 comLocal = ( com - bodyOrigin ) * bodyAxis.Transpose();
		const float mass = VR_EffectiveMass( ent, bodyId );

		// where the hands want the body
		idVec3 targetCom( 0, 0, 0 ), ffVel( 0, 0, 0 ), ffAngVel( 0, 0, 0 );
		idQuat targetQuat;
		for ( int i = 0; i < n; i++ ) {
			const idVRHand &hand = hands[group[i]];
			const vrHandInput_t &in = hand.GetInput();
			const idMat3 axis = hand.GetGrip().handAxis.Transpose() * in.axis;
			const idVec3 origin = hand.GetGripTarget() - hand.GetGrip().anchor * axis;
			const idVec3 c = origin + comLocal * axis;
			targetCom += c;
			ffVel += in.vel + in.angVel.Cross( c - in.pos );
			ffAngVel += in.angVel;
			idQuat q = axis.ToQuat();
			if ( i == 0 ) {
				targetQuat = q;
			} else {
				if ( q.x * targetQuat.x + q.y * targetQuat.y + q.z * targetQuat.z + q.w * targetQuat.w < 0.0f ) {
					q = -q;
				}
				targetQuat = targetQuat + q;
			}
		}
		targetCom /= float( n );
		ffVel /= float( n );
		ffAngVel /= float( n );
		targetQuat.Normalize();
		const idMat3 targetAxis = targetQuat.ToMat3();

		idVec3 linVel = phys->GetLinearVelocity( bodyId );
		idVec3 angVel = phys->GetAngularVelocity( bodyId );

		// turning
		{
			idVec3 axisSum( 0, 0, 0 );
			float dots = 0.0f;
			for ( int i = 0; i < 3; i++ ) {
				axisSum += bodyAxis[i].Cross( targetAxis[i] );
				dots += bodyAxis[i] * targetAxis[i];
			}
			const float angle = idMath::ACos( idMath::ClampFloat( -1.0f, 1.0f, ( dots - 1.0f ) * 0.5f ) );
			idVec3 rotError( 0, 0, 0 );
			if ( axisSum.Normalize() > 1e-5f ) {
				rotError = axisSum * angle;
			}
			idVec3 dAng = ffAngVel + rotError / Max( vr_handRotFollowTime.GetFloat(), dt ) - angVel;
			const float maxDAng = vr_handWristAccel.GetFloat() / ( 1.0f + mass * vr_handWristMassFactor.GetFloat() ) * dt;
			if ( dAng.LengthSqr() > maxDAng * maxDAng ) {
				dAng.Normalize();
				dAng *= maxDAng;
			}
			angVel += dAng;
		}

		// holding up
		const float support = idMath::ClampFloat( 0.0f, 1.0f, vr_handLiftMass.GetFloat() * n / mass );
		linVel -= gravity * ( dt * support );

		// moving
		{
			idVec3 dVel = ffVel + ( targetCom - com ) / Max( vr_handFollowTime.GetFloat(), dt ) - linVel;
			const float maxDVel = vr_handMoveForce.GetFloat() * n / mass * dt;
			if ( dVel.LengthSqr() > maxDVel * maxDVel ) {
				dVel.Normalize();
				dVel *= maxDVel;
			}
			linVel += dVel;
			const float maxSpeed = vr_handMaxSpeed.GetFloat();
			if ( linVel.LengthSqr() > maxSpeed * maxSpeed ) {
				linVel.Normalize();
				linVel *= maxSpeed;
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

	// the readable script keeps fixed readables open while a hand holds them
	if ( vr_readablesInHand.GetBool() != IsActive() ) {
		vr_readablesInHand.SetBool( IsActive() );
	}

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
	// doors, lids and drawers: after the hands said where they want them
	movers.Update( dt );
	// the bow: nocking, drawing, the arrow on the string
	bow.Update( player, hands, dt );
	// swinging the sword or blackjack
	melee.Update( player, hands, dt );
	// letters and books in the hand
	readables.Update( player, hands );

	tools.Update( player, dt );
	pouch.UpdatePendingGrabberItem( player, hands[0] );
	UpdateCalibration( player );
	DriveHeldObjects( dt );
	UpdateHighlights();
	UpdatePendingClipRestores( player );

	// the classic view-ray frob target is not used with hands
	player->m_FrobEntity = nullptr;
}

void idVRHands::FilterInput( idPlayer *player, usercmd_t &cmd ) {
	if ( !initialized || !IsActive() ) {
		return;
	}
	pouch.UpdateInput( player, cmd, hands[0] );
	arrowSelector.UpdateInput( player, cmd, hands[1] );
}

bool idVRHands::HandleGripPress( idPlayer *player, int handIndex ) {
	// the pouch belongs to the left hand: while it is open, the left grip takes the shown item
	if ( handIndex == 0 && pouch.IsOpen() ) {
		return pouch.Take( player, hands[0] );
	}
	// the arrow selector belongs to the right hand: the right grip picks the shown arrow type
	if ( handIndex == 1 && arrowSelector.IsOpen() ) {
		arrowSelector.Confirm( player );
		return true;
	}
	// the bow: the string (anywhere on the held bow), the bow (left shoulder), an arrow (right shoulder)
	if ( bow.TryGrab( player, hands, handIndex ) ) {
		return true;
	}
	// an empty hand that touches nothing may be at a holster
	if ( hands[handIndex].GetTouched() == nullptr ) {
		return tools.TryDraw( player, hands[handIndex], handIndex, hands[1 - handIndex] );
	}
	return false;
}

bool idVRHands::OnToolRelease( idPlayer *player, int handIndex, idEntity *ent ) {
	return tools.OnRelease( player, ent, handIndex, hands[handIndex], hands[1 - handIndex] );
}

/*
 * Hand calibration: vr_calibrateHands fist|palms|reset
 * After a short countdown the gap between the two gloves is measured while the real hands touch
 * (knuckles together for "fist", flat palms together for "palms") and the glove offset
 * (vr_handModelOffset, same for both hands, mirrored) is corrected so the gloves touch too.
 */
void idVRHands::UpdateCalibration( idPlayer *player ) {
	if ( vr_calibrateHands.IsModified() ) {
		vr_calibrateHands.ClearModified();
		calibrateMode = vr_calibrateHands.GetString();
		calibrateMode.ToLower();
		if ( calibrateMode == "reset" ) {
			vr_handModelOffset.SetString( "0 0 0" );
			player->SendHUDMessage( "Hand calibration reset" );
			calibrateMode.Clear();
		} else if ( calibrateMode == "fist" || calibrateMode == "palms" ) {
			calibrateAt = gameLocal.time + 4000;
			player->SendHUDMessage( calibrateMode == "fist" ? "Calibration: bump your fists together and hold still" : "Calibration: press your palms flat together and hold still" );
		} else {
			calibrateMode.Clear();
		}
		if ( vr_calibrateHands.GetString()[0] != '\0' ) {
			vr_calibrateHands.SetString( "" );
			vr_calibrateHands.ClearModified();
		}
	}
	if ( calibrateMode.IsEmpty() || gameLocal.time < calibrateAt ) {
		return;
	}
	const idStr mode = calibrateMode;
	calibrateMode.Clear();
	if ( !hands[0].IsTracked() || !hands[1].IsTracked() || !hands[0].GetGlove().IsValid() || !hands[1].GetGlove().IsValid() ) {
		player->SendHUDMessage( "Calibration failed: both hands must be visible" );
		return;
	}
	idList<idVRHandModel::touchPoint_t> pts[2];
	for ( int h = 0; h < 2; h++ ) {
		hands[h].GetGlove().GetTouchPoints( hands[h].GetVisualPos(), hands[h].GetVisualAxis(), pts[h] );
	}
	// the closest pair of finger points (fist) or the two palm points (palms)
	const bool palms = mode == "palms";
	idVec3 a, b;
	float best = idMath::INFINITY;
	for ( const idVRHandModel::touchPoint_t &p : pts[0] ) {
		for ( const idVRHandModel::touchPoint_t &q : pts[1] ) {
			if ( p.palm != palms || q.palm != palms ) {
				continue;
			}
			const float d = ( p.pos - q.pos ).LengthFast();
			if ( d < best ) {
				best = d;
				a = p.pos;
				b = q.pos;
			}
		}
	}
	if ( best == idMath::INFINITY || best < 0.01f ) {
		player->SendHUDMessage( "Calibration failed" );
		return;
	}
	// surface distance from the measured points: finger half thickness / palm point to palm surface
	const float surface = palms ? 0.26f : 0.72f;
	const float gap = best - 2.0f * surface;
	if ( gap < -4.0f || gap > 12.0f ) {
		player->SendHUDMessage( va( "Calibration failed: hands %.0f cm apart, are they touching?", gap * 2.309f ) );
		return;
	}
	const idVec3 dir = ( b - a ) / best;
	// left glove moves towards the right one by half the gap, and the other way round; in grip space
	idVec3 leftLocal = ( dir * ( gap * 0.5f ) ) * hands[0].GetInput().axis.Transpose();
	idVec3 rightLocal = ( dir * ( -gap * 0.5f ) ) * hands[1].GetInput().axis.Transpose();
	rightLocal.y = -rightLocal.y;		// the right glove is the mirror image
	const idVec3 delta = ( leftLocal + rightLocal ) * 0.5f;
	idVec3 offset( 0, 0, 0 );
	sscanf( vr_handModelOffset.GetString(), "%f %f %f", &offset.x, &offset.y, &offset.z );
	offset += delta;
	vr_handModelOffset.SetString( va( "%.2f %.2f %.2f", offset.x, offset.y, offset.z ) );
	player->SendHUDMessage( va( "Calibration done: gap was %.1f cm, glove offset now %s", gap * 2.309f, vr_handModelOffset.GetString() ) );
	gameLocal.Printf( "VR hands calibration (%s): gap %.2f units, vr_handModelOffset \"%s\"\n", mode.c_str(), gap, vr_handModelOffset.GetString() );
}

void idVRHands::PostThink( idPlayer *player ) {
	if ( !initialized ) {
		return;
	}
	pouch.UpdatePanel( player, hands[0] );
	arrowSelector.UpdatePanel( player, hands[1] );
	hands[0].UpdateVisual( lastDt );
	hands[1].UpdateVisual( lastDt );
	bow.UpdateRender( player, hands );
	readables.UpdatePanels( player, hands );
	UpdateLightCrystal( player );
}

/*
 * The light crystal sits in a ring on the left ring finger and glows as brightly as the player is lit
 * (the lightgem). Without the glove model it sits on the back of the placeholder hand.
 */
void idVRHands::UpdateLightCrystal( idPlayer *player ) {
	const idVRHand &hand = hands[0];
	if ( !gameRenderWorld ) {
		return;
	}
	auto freeEntities = [&]() {
		if ( crystalHandle != -1 ) {
			gameRenderWorld->FreeEntityDef( crystalHandle );
			crystalHandle = -1;
		}
		if ( ringHandle != -1 ) {
			gameRenderWorld->FreeEntityDef( ringHandle );
			ringHandle = -1;
		}
	};
	if ( !hand.IsTracked() || !IsActive() ) {
		freeEntities();
		return;
	}
	const float target = idMath::ClampFloat( 0.0f, 1.0f, player->GetCurrentLightgemValue() / float( DARKMOD_LG_MAX ) );
	crystalLevel += ( target - crystalLevel ) * idMath::ClampFloat( 0.0f, 1.0f, lastDt * 8.0f );
	const float glow = 0.02f + 0.98f * crystalLevel * crystalLevel;

	// where: the ring socket joint (x along the finger, z out of the back of the finger)
	idVec3 socketPos;
	idMat3 socketAxis;
	const bool onFinger = hand.GetGlove().GetJointWorld( hand.GetGlove().GetRingSocket(), socketPos, socketAxis );
	const float ringRadius = vr_ringRadius.GetFloat();
	idVec3 crystalPos;
	idMat3 crystalAxis;
	if ( onFinger ) {
		const idVec3 ringCenter = socketPos + socketAxis[2] * vr_ringOffset.GetFloat();
		crystalPos = ringCenter + socketAxis[2] * ( ringRadius + 0.12f );
		crystalAxis = socketAxis;

		idRenderModel *ring = GetRingModel( ringRadius );
		memset( &ringEntity, 0, sizeof( ringEntity ) );
		ringEntity.hModel = ring;
		ringEntity.origin = ringCenter;
		ringEntity.axis = socketAxis;
		ringEntity.bounds = ring->Bounds();
		ringEntity.noShadow = true;
		ringEntity.suppressSurfaceInViewID = VID_LIGHTGEM;
		ringEntity.shaderParms[SHADERPARM_RED] = ringEntity.shaderParms[SHADERPARM_GREEN] = ringEntity.shaderParms[SHADERPARM_BLUE] = 1.0f;
		ringEntity.shaderParms[SHADERPARM_ALPHA] = 1.0f;
		if ( ringHandle == -1 ) {
			ringHandle = gameRenderWorld->AddEntityDef( &ringEntity );
		} else {
			gameRenderWorld->UpdateEntityDef( ringHandle, &ringEntity );
		}
	} else {
		if ( ringHandle != -1 ) {
			gameRenderWorld->FreeEntityDef( ringHandle );
			ringHandle = -1;
		}
		crystalPos = hand.GetVisualPos() + idVec3( -3.0f, 0.0f, 1.8f ) * hand.GetVisualAxis();
		crystalAxis = hand.GetVisualAxis();
	}

	idRenderModel *model = GetCrystalModel();
	memset( &crystalEntity, 0, sizeof( crystalEntity ) );
	crystalEntity.hModel = model;
	crystalEntity.origin = crystalPos;
	crystalEntity.axis = crystalAxis;
	crystalEntity.bounds = model->Bounds();
	crystalEntity.noShadow = true;
	crystalEntity.suppressSurfaceInViewID = VID_LIGHTGEM;
	crystalEntity.shaderParms[SHADERPARM_RED] = glow * 1.0f;
	crystalEntity.shaderParms[SHADERPARM_GREEN] = glow * 0.85f;
	crystalEntity.shaderParms[SHADERPARM_BLUE] = glow * 0.55f;
	crystalEntity.shaderParms[SHADERPARM_ALPHA] = 1.0f;
	if ( crystalHandle == -1 ) {
		crystalHandle = gameRenderWorld->AddEntityDef( &crystalEntity );
	} else {
		gameRenderWorld->UpdateEntityDef( crystalHandle, &crystalEntity );
	}
}

