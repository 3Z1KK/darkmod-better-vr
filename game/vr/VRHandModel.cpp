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
#include "game/vr/VRHandModel.h"

idCVar vr_handModel( "vr_handModel", "1", CVAR_GAME | CVAR_BOOL, "VR: draw the glove hands (0 = placeholder boxes)" );
idCVar vr_handModelOffset( "vr_handModelOffset", "0 0 0", CVAR_GAME | CVAR_ARCHIVE, "VR: glove position relative to the controller grip (x = towards the thumb, y = out of the back of the left hand, z = towards the wrist), game units" );
idCVar vr_handModelAngles( "vr_handModelAngles", "0 0 0", CVAR_GAME | CVAR_ARCHIVE, "VR: glove rotation relative to the controller grip (pitch yaw roll, degrees; mirrored for the right hand)" );
idCVar vr_fingerCurlSpeed( "vr_fingerCurlSpeed", "18", CVAR_GAME | CVAR_FLOAT, "VR: how fast fingers move to their target curl (1/s)" );

namespace {
	const char *FINGER_NAMES[idVRHandModel::NUM_FINGERS] = { "thumb", "index", "middle", "ring", "pinky" };
	// distal phalanx lengths measured on the glove (game units)
	const float FINGER_TIPS[idVRHandModel::NUM_FINGERS] = { 1.16f, 1.12f, 1.20f, 1.08f, 0.68f };
	// how much each of the three joints of a finger bends for a given curl
	const float JOINT_SHARE[idVRHandModel::NUM_FINGERS][3] = {
		{ 0.35f, 1.0f, 1.0f },		// thumb: the metacarpal moves less
		{ 1.0f, 1.1f, 0.8f },
		{ 1.0f, 1.1f, 0.8f },
		{ 1.0f, 1.1f, 0.8f },
		{ 1.0f, 1.1f, 0.8f },
	};
}

idVRHandModel::~idVRHandModel() {
	Free();
}

void idVRHandModel::Free() {
	if ( jointMats ) {
		Mem_Free16( jointMats );
		jointMats = nullptr;
	}
	if ( scratchMats ) {
		Mem_Free16( scratchMats );
		scratchMats = nullptr;
	}
	model = nullptr;
	numJoints = 0;
	ringSocket = -1;
}

bool idVRHandModel::Init( int handIndex ) {
	Free();
	hand = handIndex;
	if ( !vr_handModel.GetBool() ) {
		return false;
	}
	const char *name = handIndex == 0 ? "models/vr/hands/glove_left.md5mesh" : "models/vr/hands/glove_right.md5mesh";
	idRenderModel *m = renderModelManager->FindModel( name );
	if ( m == nullptr || m->IsDefaultModel() || m->NumJoints() <= 0 ) {
		gameLocal.Warning( "VR hands: could not load %s, using placeholder hands", name );
		return false;
	}
	numJoints = m->NumJoints();
	const idMD5Joint *joints = m->GetJoints();
	parents.SetNum( numJoints );
	for ( int i = 0; i < numJoints; i++ ) {
		parents[i] = joints[i].parent ? int( joints[i].parent - joints ) : -1;
	}
	auto find = [&]( const char *jn ) -> int {
		for ( int i = 0; i < numJoints; i++ ) {
			if ( joints[i].name.Icmp( jn ) == 0 ) {
				return i;
			}
		}
		return -1;
	};
	for ( int f = 0; f < NUM_FINGERS; f++ ) {
		for ( int k = 0; k < 3; k++ ) {
			fingerJoints[f][k] = find( va( "%s%d", FINGER_NAMES[f], k + 1 ) );
			if ( fingerJoints[f][k] < 0 ) {
				gameLocal.Warning( "VR hands: %s has no joint %s%d", name, FINGER_NAMES[f], k + 1 );
				numJoints = 0;
				return false;
			}
		}
		fingerTip[f] = FINGER_TIPS[f];
		curl[f] = 10.0f;
	}
	ringSocket = find( "ring_socket" );
	jointMats = (idJointMat *)Mem_Alloc16( numJoints * sizeof( idJointMat ) );
	scratchMats = (idJointMat *)Mem_Alloc16( numJoints * sizeof( idJointMat ) );
	model = m;
	ComputeJoints( curl, jointMats );
	return true;
}

void idVRHandModel::SetPose( const idVec3 &gripOrigin, const idMat3 &gripAxis ) {
	idVec3 offset( 0, 0, 0 );
	idAngles angles( 0, 0, 0 );
	sscanf( vr_handModelOffset.GetString(), "%f %f %f", &offset.x, &offset.y, &offset.z );
	sscanf( vr_handModelAngles.GetString(), "%f %f %f", &angles.pitch, &angles.yaw, &angles.roll );
	if ( hand == 1 ) {
		// the right hand is the mirror image of the left one
		offset.y = -offset.y;
		angles.yaw = -angles.yaw;
		angles.roll = -angles.roll;
	}
	modelAxis = angles.ToMat3() * gripAxis;
	modelOrigin = gripOrigin + offset * gripAxis;
}

void idVRHandModel::ComputeJoints( const float curls[NUM_FINGERS], idJointMat *out ) const {
	const idJointQuat *pose = model->GetDefaultPose();
	idJointQuat local[64];
	const int n = Min( numJoints, 64 );
	for ( int i = 0; i < n; i++ ) {
		local[i] = pose[i];
	}
	for ( int f = 0; f < NUM_FINGERS; f++ ) {
		for ( int k = 0; k < 3; k++ ) {
			const int j = fingerJoints[f][k];
			const float a = DEG2RAD( curls[f] * JOINT_SHARE[f][k] );
			float s, c;
			idMath::SinCos( a, s, c );
			// bend around the joint's own y axis, x (the bone) towards -z (the palm)
			const idMat3 bend( c, 0.0f, -s, 0.0f, 1.0f, 0.0f, s, 0.0f, c );
			local[j].q = ( bend * local[j].q.ToMat3() ).ToQuat();
		}
	}
	SIMDProcessor->ConvertJointQuatsToJointMats( out, local, n );
	SIMDProcessor->TransformJoints( out, parents.Ptr(), 1, n - 1 );
}

void idVRHandModel::MoveFingers( const float targets[NUM_FINGERS], float dt ) {
	if ( !IsValid() ) {
		return;
	}
	const float k = idMath::ClampFloat( 0.0f, 1.0f, dt * vr_fingerCurlSpeed.GetFloat() );
	for ( int f = 0; f < NUM_FINGERS; f++ ) {
		curl[f] += ( targets[f] - curl[f] ) * k;
	}
	ComputeJoints( curl, jointMats );
}

void idVRHandModel::FindContactCurls( const idClipModel *clip, float maxCurl, float out[NUM_FINGERS] ) {
	for ( int f = 0; f < NUM_FINGERS; f++ ) {
		out[f] = maxCurl;
	}
	if ( !IsValid() || clip == nullptr ) {
		return;
	}
	// curl each finger step by step until one of its points ends up inside the object
	float test[NUM_FINGERS];
	bool done[NUM_FINGERS] = {};
	const float step = 5.0f;
	for ( float a = 0.0f; a <= maxCurl; a += step ) {
		for ( int f = 0; f < NUM_FINGERS; f++ ) {
			test[f] = done[f] ? out[f] : a;
		}
		ComputeJoints( test, scratchMats );
		bool all = true;
		for ( int f = 0; f < NUM_FINGERS; f++ ) {
			if ( done[f] ) {
				continue;
			}
			all = false;
			const idJointMat &j1 = scratchMats[fingerJoints[f][0]];
			const idJointMat &j2 = scratchMats[fingerJoints[f][1]];
			const idJointMat &j3 = scratchMats[fingerJoints[f][2]];
			const idVec3 tip = j3.ToVec3() + j3.ToMat3()[0] * fingerTip[f];
			const idVec3 pts[4] = { ( j1.ToVec3() + j2.ToVec3() ) * 0.5f, j2.ToVec3(), j3.ToVec3(), tip };
			for ( int p = 0; p < 4; p++ ) {
				const idVec3 world = modelOrigin + pts[p] * modelAxis;
				if ( gameLocal.clip.ContentsModel( world, nullptr, mat3_identity, -1, clip->Handle(), clip->GetOrigin(), clip->GetAxis() ) ) {
					out[f] = Max( 0.0f, a - step * 0.5f );
					done[f] = true;
					break;
				}
			}
		}
		if ( all ) {
			break;
		}
	}
}

bool idVRHandModel::GetJointWorld( int joint, idVec3 &pos, idMat3 &axis ) const {
	if ( !IsValid() || joint < 0 || joint >= numJoints ) {
		return false;
	}
	pos = modelOrigin + jointMats[joint].ToVec3() * modelAxis;
	axis = jointMats[joint].ToMat3() * modelAxis;
	return true;
}

void idVRHandModel::FillRenderEntity( renderEntity_t &re ) const {
	re.hModel = model;
	re.origin = modelOrigin;
	re.axis = modelAxis;
	re.joints = jointMats;
	re.numJoints = numJoints;
	re.bounds = idBounds( idVec3( -14, -14, -14 ), idVec3( 14, 14, 14 ) );
}
