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
 * VR grips on the drawn mesh: the collision box of most small things is much bigger than what is drawn (a box
 * around a bottle), so a hand gripping by the box held air. When the palm is close to the drawn surface and
 * faces it, the grip snaps the palm onto the mesh and the fingers close until they touch the mesh.
 *
 * The triangles of an entity's drawn (static) model are read once per model and kept, in the model's space,
 * which is the entity's physics space for moveables and items.
 */

class idVRGripMesh {
public:
	// the triangles (3 points each) of the entity's drawn model in its own space; nullptr if it has no static model
	static const idList<idVec3> *	Get( idEntity *ent );
	// nearest point on the triangles to p, and the triangle's normal (unoriented); false if none within maxDist
	static bool						Nearest( const idList<idVec3> &tris, const idVec3 &p, float maxDist, idVec3 &point, idVec3 &normal );
	// the triangles within radius of p, copied (for repeated tests near the hand)
	static void						Near( const idList<idVec3> &tris, const idVec3 &p, float radius, idList<idVec3> &out );
	static idVec3					ClosestOnTriangle( const idVec3 &p, const idVec3 &a, const idVec3 &b, const idVec3 &c );
	static void						Clear();
};
