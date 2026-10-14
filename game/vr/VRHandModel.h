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
#pragma once

/*
 * The rigged glove of one VR hand (models/vr/hands/glove_left|right.md5mesh, built by tools/vr_hands).
 *
 * Model space = the controller's grip pose: origin in the middle of the closed fist, x = from little finger
 * to thumb, z = towards the wrist, y = out of the back of the left hand / the palm of the right hand.
 * Every finger joint bends around its own y axis; positive angles curl towards the palm.
 * The fingers are posed by code: from the controller buttons when the hand is empty, and by curling each
 * finger until it touches the held object when holding something.
 */

class idRenderModel;
class idClipModel;

class idVRHandModel {
public:
	enum { THUMB, INDEX, MIDDLE, RING, PINKY, NUM_FINGERS };

	struct touchPoint_t {
		idVec3		pos;
		float		radius;
		bool		palm;
	};

	~idVRHandModel();

	bool			Init( int handIndex );		// false if the model is missing
	void			Free();
	bool			IsValid() const { return model != nullptr; }

	// where the controller grip pose is (the drawn hand's pose)
	void			SetPose( const idVec3 &gripOrigin, const idMat3 &gripAxis );

	// finger curls towards the given targets (degrees per joint), smoothed
	void			MoveFingers( const float targets[NUM_FINGERS], float dt );
	// how far each finger can curl before it touches the given clip model (degrees per joint)
	void			FindContactCurls( const idClipModel *clip, float maxCurl, float out[NUM_FINGERS] );

	// world position and axis of a joint in the current pose
	bool			GetJointWorld( int joint, idVec3 &pos, idMat3 &axis ) const;
	int				GetRingSocket() const { return ringSocket; }
	int				GetHandJoint() const { return handJoint; }

	// spheres covering the palm and the (currently curled) fingers, for a hand at the given grip pose
	void			GetTouchPoints( const idVec3 &gripOrigin, const idMat3 &gripAxis, idList<touchPoint_t> &out ) const;

	void			FillRenderEntity( renderEntity_t &re ) const;

private:
	void			ComputeJoints( const float curls[NUM_FINGERS], idJointMat *out ) const;
	void			ModelTransform( const idVec3 &gripOrigin, const idMat3 &gripAxis, idVec3 &origin, idMat3 &axis ) const;

	int				hand = 0;
	idRenderModel *	model = nullptr;
	int				numJoints = 0;
	idJointMat *	jointMats = nullptr;		// current pose, model space (16 byte aligned)
	idJointMat *	scratchMats = nullptr;
	idList<int>		parents;
	int				fingerJoints[NUM_FINGERS][3];
	float			fingerTip[NUM_FINGERS];		// distal phalanx length (model units)
	int				ringSocket = -1;
	int				handJoint = -1;
	float			curl[NUM_FINGERS] = {};
	idVec3			modelOrigin = vec3_zero;
	idMat3			modelAxis = mat3_identity;
};

extern idCVar vr_handModelOffset;
