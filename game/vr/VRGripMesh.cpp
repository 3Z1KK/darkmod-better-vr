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
#include "game/vr/VRGripMesh.h"

namespace {
	const int MAX_CACHED = 48;
	const int MAX_TRIS = 20000;		// bigger models grip by their collision box as before

	struct cachedMesh_t {
		const idRenderModel *	model;
		idStr					name;
		bool					usable;
		idList<idVec3>			tris;
	};
	idList<cachedMesh_t *> cache;
	int nextReplace = 0;

	cachedMesh_t *Build( const idRenderModel *model ) {
		cachedMesh_t *m = new cachedMesh_t;
		m->model = model;
		m->name = model->Name();
		m->usable = false;
		if ( model->IsDynamicModel() != DM_STATIC || model->IsDefaultModel() ) {
			return m;
		}
		int total = 0;
		for ( int s = 0; s < model->NumSurfaces(); s++ ) {
			const modelSurface_t *surf = model->Surface( s );
			if ( surf == nullptr || surf->geometry == nullptr || surf->material == nullptr || !surf->material->IsDrawn() ) {
				continue;
			}
			total += surf->geometry->numIndexes / 3;
		}
		if ( total == 0 || total > MAX_TRIS ) {
			return m;
		}
		m->tris.SetNum( 0 );
		m->tris.Resize( total * 3 );
		for ( int s = 0; s < model->NumSurfaces(); s++ ) {
			const modelSurface_t *surf = model->Surface( s );
			if ( surf == nullptr || surf->geometry == nullptr || surf->material == nullptr || !surf->material->IsDrawn() ) {
				continue;
			}
			const srfTriangles_t *tri = surf->geometry;
			if ( tri->verts == nullptr || tri->indexes == nullptr ) {
				continue;
			}
			for ( int i = 0; i + 2 < tri->numIndexes; i += 3 ) {
				m->tris.Append( tri->verts[tri->indexes[i]].xyz );
				m->tris.Append( tri->verts[tri->indexes[i + 1]].xyz );
				m->tris.Append( tri->verts[tri->indexes[i + 2]].xyz );
			}
		}
		m->usable = m->tris.Num() > 0;
		return m;
	}
}

const idList<idVec3> *idVRGripMesh::Get( idEntity *ent ) {
	if ( ent == nullptr ) {
		return nullptr;
	}
	const renderEntity_t *re = ent->GetRenderEntity();
	if ( re == nullptr || re->hModel == nullptr || re->joints != nullptr ) {
		return nullptr;
	}
	const idRenderModel *model = re->hModel;
	for ( cachedMesh_t *m : cache ) {
		if ( m->model == model && m->name == model->Name() ) {
			return m->usable ? &m->tris : nullptr;
		}
	}
	cachedMesh_t *m = Build( model );
	if ( cache.Num() < MAX_CACHED ) {
		cache.Append( m );
	} else {
		delete cache[nextReplace];
		cache[nextReplace] = m;
		nextReplace = ( nextReplace + 1 ) % MAX_CACHED;
	}
	return m->usable ? &m->tris : nullptr;
}

void idVRGripMesh::Clear() {
	cache.DeleteContents( true );
	nextReplace = 0;
}

// Ericson, Real-Time Collision Detection 5.1.5
idVec3 idVRGripMesh::ClosestOnTriangle( const idVec3 &p, const idVec3 &a, const idVec3 &b, const idVec3 &c ) {
	const idVec3 ab = b - a, ac = c - a, ap = p - a;
	const float d1 = ab * ap, d2 = ac * ap;
	if ( d1 <= 0.0f && d2 <= 0.0f ) {
		return a;
	}
	const idVec3 bp = p - b;
	const float d3 = ab * bp, d4 = ac * bp;
	if ( d3 >= 0.0f && d4 <= d3 ) {
		return b;
	}
	const float vc = d1 * d4 - d3 * d2;
	if ( vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f ) {
		return a + ab * ( d1 / ( d1 - d3 ) );
	}
	const idVec3 cp = p - c;
	const float d5 = ab * cp, d6 = ac * cp;
	if ( d6 >= 0.0f && d5 <= d6 ) {
		return c;
	}
	const float vb = d5 * d2 - d1 * d6;
	if ( vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f ) {
		return a + ac * ( d2 / ( d2 - d6 ) );
	}
	const float va = d3 * d6 - d5 * d4;
	if ( va <= 0.0f && ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f ) {
		return b + ( c - b ) * ( ( d4 - d3 ) / ( ( d4 - d3 ) + ( d5 - d6 ) ) );
	}
	const float denom = 1.0f / ( va + vb + vc );
	return a + ab * ( vb * denom ) + ac * ( vc * denom );
}

bool idVRGripMesh::Nearest( const idList<idVec3> &tris, const idVec3 &p, float maxDist, idVec3 &point, idVec3 &normal ) {
	float best = Square( maxDist );
	bool found = false;
	for ( int i = 0; i + 2 < tris.Num(); i += 3 ) {
		const idVec3 &a = tris[i], &b = tris[i + 1], &c = tris[i + 2];
		// quick reject by the triangle's bounds
		idBounds bounds( a );
		bounds.AddPoint( b );
		bounds.AddPoint( c );
		bounds.ExpandSelf( maxDist );
		if ( !bounds.ContainsPoint( p ) ) {
			continue;
		}
		const idVec3 q = ClosestOnTriangle( p, a, b, c );
		const float d = ( q - p ).LengthSqr();
		if ( d < best ) {
			idVec3 n = ( b - a ).Cross( c - a );
			if ( n.Normalize() < 1e-6f ) {
				continue;	// degenerate
			}
			best = d;
			point = q;
			normal = n;
			found = true;
		}
	}
	return found;
}

void idVRGripMesh::Near( const idList<idVec3> &tris, const idVec3 &p, float radius, idList<idVec3> &out ) {
	out.SetNum( 0, false );
	for ( int i = 0; i + 2 < tris.Num(); i += 3 ) {
		idBounds bounds( tris[i] );
		bounds.AddPoint( tris[i + 1] );
		bounds.AddPoint( tris[i + 2] );
		bounds.ExpandSelf( radius );
		if ( bounds.ContainsPoint( p ) ) {
			out.Append( tris[i] );
			out.Append( tris[i + 1] );
			out.Append( tris[i + 2] );
		}
	}
}
