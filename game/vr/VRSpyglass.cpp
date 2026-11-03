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
#include "game/Moveable.h"
#include "game/vr/VRSpyglass.h"
#include "game/vr/VRHands.h"

idCVar vr_spyglassRenderDistance( "vr_spyglassRenderDistance", "1024", CVAR_GAME | CVAR_FLOAT, "VR spyglass: the picture is live only while the player is in the spyglass's room or the next one, and no farther than this (units)" );
idCVar vr_spyglassMinZoom( "vr_spyglassMinZoom", "1.5", CVAR_GAME | CVAR_FLOAT, "VR spyglass: weakest magnification" );
idCVar vr_spyglassMaxZoom( "vr_spyglassMaxZoom", "4.5", CVAR_GAME | CVAR_FLOAT, "VR spyglass: strongest magnification (4.5 = the vanilla spyglass at its closest, fov 25 against 90)" );
idCVar vr_spyglassZoomSpeed( "vr_spyglassZoomSpeed", "1.2", CVAR_GAME | CVAR_FLOAT, "VR spyglass: zoom speed with the stick fully pushed (factor e^this per second)" );
idCVar vr_spyglassEyeRelief( "vr_spyglassEyeRelief", "2", CVAR_GAME | CVAR_FLOAT, "VR spyglass: eye to eyepiece distance (units) the magnification is worked out for" );
idCVar vr_spyglassImageFlip( "vr_spyglassImageFlip", "3", CVAR_GAME | CVAR_INTEGER, "VR spyglass: flip the picture, 1 = left/right, 2 = up/down, 3 = both (the vanilla camera screens flip both)" );
idCVar vr_spyglassFisheye( "vr_spyglassFisheye", "0.35", CVAR_GAME | CVAR_FLOAT, "VR spyglass: how much the picture is squeezed towards the edge of the lens (0 = flat). Applies on map load" );
idCVar vr_spyglassNearClip( "vr_spyglassNearClip", "6", CVAR_GAME | CVAR_FLOAT, "VR spyglass: with the head this close to an eyepiece (units) the near clip plane comes closer, so the eyepiece isn't cut away" );
idCVar vr_spyglassDebug( "vr_spyglassDebug", "0", CVAR_GAME | CVAR_BOOL, "VR spyglass: log swaps and zoom" );

namespace {
	const char *VR_SPYGLASS_DEF = "atdm:vr_spyglass";

	// models/darkmod/player_equipment/spyglass.lwo: the tube runs along x, eyepiece (thin end) at +x, objective at -x
	const float LENS_X = 9.86f;			// eyepiece glass, just inside the rim at 9.91
	const float LENS_R = 0.56f;			// inner radius of the eyepiece
	const float CAP_X = -9.12f;			// dark glass in the objective end (rim at -9.2)
	const float CAP_R = 0.97f;
	const float CAMERA_X = -9.5f;		// the camera, just in front of the objective
	const float MODEL_CENTRE_X = 0.36f;	// centre of the open model's bounds

	const int RINGS = 8;
	const int SEGMENTS = 40;

	void SpyglassDebug( const char *fmt, ... ) {
		if ( !vr_spyglassDebug.GetBool() ) {
			return;
		}
		va_list argptr;
		char text[1024];
		va_start( argptr, fmt );
		idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
		va_end( argptr );
		gameLocal.Printf( "VR spyglass: %s\n", text );
	}

	// a flat disc at x facing +x (front) or -x, in the spyglass model's space
	void BuildDisc( idRenderModel *model, int id, const char *material, float x, float radius, bool facePlusX, bool lensUV ) {
		const int numVerts = 1 + RINGS * SEGMENTS;
		const int numIndexes = 3 * ( SEGMENTS + ( RINGS - 1 ) * SEGMENTS * 2 );
		modelSurface_t surf;
		surf.id = id;
		surf.material = declManager->FindMaterial( material );
		surf.geometry = model->AllocSurfaceTriangles( numVerts, numIndexes );
		srfTriangles_t *tri = surf.geometry;
		tri->numVerts = numVerts;
		tri->numIndexes = numIndexes;
		tri->bounds.Clear();

		const float k = Max( 0.0f, vr_spyglassFisheye.GetFloat() );
		const float nx = facePlusX ? 1.0f : -1.0f;
		for ( int v = 0; v < numVerts; v++ ) {
			float rho = 0.0f, angle = 0.0f;
			if ( v > 0 ) {
				const int ring = ( v - 1 ) / SEGMENTS;
				const int seg = ( v - 1 ) % SEGMENTS;
				rho = float( ring + 1 ) / RINGS;
				angle = idMath::TWO_PI * seg / SEGMENTS;
			}
			const float cy = idMath::Cos( angle ), cz = idMath::Sin( angle );
			idDrawVert &dv = tri->verts[v];
			dv.Clear();
			dv.xyz.Set( x, cy * rho * radius, cz * rho * radius );
			// seen from the front, the viewer's right is +y (looking down -x); texture u goes right, v goes down.
			// Fish-eye: the middle of the picture is spread out, the edge squeezed (barrel), still reaching the edge
			const float r = lensUV ? rho * ( 1.0f + k * rho * rho ) / ( 1.0f + k ) : rho;
			dv.st.Set( 0.5f + 0.5f * r * cy * nx, 0.5f - 0.5f * r * cz );
			dv.normal.Set( nx, 0, 0 );
			dv.tangents[0].Set( 0, nx, 0 );
			dv.tangents[1].Set( 0, 0, -1 );
			dv.color[0] = dv.color[1] = dv.color[2] = dv.color[3] = 255;
			tri->bounds.AddPoint( dv.xyz );
		}
		// front faces are clockwise as seen from the front: from +x that is the angle going down
		int n = 0;
		auto tri3 = [&]( int a, int b, int c ) {
			if ( facePlusX ) {
				tri->indexes[n++] = a; tri->indexes[n++] = c; tri->indexes[n++] = b;
			} else {
				tri->indexes[n++] = a; tri->indexes[n++] = b; tri->indexes[n++] = c;
			}
		};
		for ( int s = 0; s < SEGMENTS; s++ ) {
			tri3( 0, 1 + s, 1 + ( s + 1 ) % SEGMENTS );
		}
		for ( int ring = 0; ring < RINGS - 1; ring++ ) {
			for ( int s = 0; s < SEGMENTS; s++ ) {
				const int a = 1 + ring * SEGMENTS + s;
				const int b = 1 + ring * SEGMENTS + ( s + 1 ) % SEGMENTS;
				const int c = a + SEGMENTS;
				const int d = b + SEGMENTS;
				tri3( a, c, d );
				tri3( a, d, b );
			}
		}
		tri->tangentsCalculated = true;
		tri->facePlanesCalculated = false;
		tri->generateNormals = false;
		model->AddSurface( surf );
	}

	idRenderModel *lensModel = nullptr;
	idRenderModel *GetLensModel() {
		if ( lensModel != nullptr ) {
			return lensModel;
		}
		idRenderModel *model = renderModelManager->AllocModel();
		model->InitEmpty( "_vr_spyglass_lens" );
		BuildDisc( model, 0, "vr/spyglass_lens", LENS_X, LENS_R, true, true );
		BuildDisc( model, 1, "vr/spyglass_cap", CAP_X, CAP_R, false, false );
		lensModel = model;
		return model;
	}

	idVec3 HeadWorld( idPlayer *player ) {
		const renderView_t *view = player->GetRenderView();
		if ( view == nullptr ) {
			return player->GetEyePosition();
		}
		return player->vrBody.LocalToWorld( view, player->usercmd.vrHeadPos );
	}

	idEntity *HeldSpyglass( idVRHand &hand ) {
		if ( hand.GetInteraction() != idVRHand::IK_HOLD ) {
			return nullptr;
		}
		idEntity *ent = hand.GetHeld();
		return idVRSpyglass::IsSpyglass( ent ) ? ent : nullptr;
	}

	float LensFov( float zoom ) {
		// magnification against the naked eye: the lens seen from the eye covers a fixed angle, the camera
		// squeezes zoom times less of the world into it
		const float apparentHalf = idMath::ATan( LENS_R, Max( 0.1f, vr_spyglassEyeRelief.GetFloat() ) );
		return RAD2DEG( 2.0f * idMath::ATan( idMath::Tan( apparentHalf ) / Max( 0.1f, zoom ) ) );
	}
}

bool idVRSpyglass::IsSpyglass( const idEntity *ent ) {
	return ent != nullptr && ent->spawnArgs.GetBool( "vr_spyglass", "0" );
}

bool idVRSpyglass::IsVanillaSpyglass( const idEntity *ent ) {
	if ( ent == nullptr || IsSpyglass( ent ) || !ent->IsType( idMoveable::Type ) ) {
		return false;
	}
	return idStr::FindText( ent->spawnArgs.GetString( "model" ), "player_equipment/spyglass", false ) >= 0;
}

idEntity *idVRSpyglass::SpawnReplacement( const idEntity *ent ) {
	if ( gameLocal.FindEntityDefDict( VR_SPYGLASS_DEF, false ) == nullptr ) {
		return nullptr;
	}
	const idPhysics *phys = ent->GetPhysics();
	const idMat3 axis = phys->GetAxis();
	// the open spyglass is longer than a closed one: keep the middle where the old one's middle was
	const idVec3 oldCentre = phys->GetAbsBounds().GetCenter();
	const idVec3 origin = oldCentre - idVec3( MODEL_CENTRE_X, 0, 0 ) * axis + idVec3( 0, 0, 0.25f );
	idDict args;
	args.Set( "classname", VR_SPYGLASS_DEF );
	args.SetVector( "origin", origin );
	args.SetMatrix( "rotation", axis );
	idEntity *spawned = nullptr;
	if ( !gameLocal.SpawnEntityDef( args, &spawned ) || spawned == nullptr ) {
		return nullptr;
	}
	SpyglassDebug( "%s (%s) swapped for %s", ent->name.c_str(), ent->spawnArgs.GetString( "classname" ), spawned->name.c_str() );
	return spawned;
}

void idVRSpyglass::Reset() {
	// the render world of the last map is gone with its entity handles
	for ( lens_t *lens : lenses ) {
		delete lens;
	}
	lenses.Clear();
	nextScan = 0;
	eyeAtLens = false;
	zoomStick[0] = zoomStick[1] = 0.0f;
	LockTurning( false );
}

void idVRSpyglass::Free() {
	for ( lens_t *lens : lenses ) {
		FreeLens( *lens );
		delete lens;
	}
	lenses.Clear();
	eyeAtLens = false;
	LockTurning( false );
}

void idVRSpyglass::FreeLens( lens_t &lens ) {
	if ( lens.handle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( lens.handle );
	}
	lens.handle = -1;
}

void idVRSpyglass::LockTurning( bool lock ) {
	// the turning stick zooms while the right hand holds a spyglass (see idVRClimb::LockTurning for the sharing)
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

void idVRSpyglass::UpdateInput( idPlayer *player, usercmd_t &cmd, idVRHand hands[2] ) {
	// left hand: the walking stick, right hand: the turning stick (its up/down is otherwise unused)
	if ( HeldSpyglass( hands[0] ) != nullptr ) {
		zoomStick[0] = cmd.forwardmove / 127.0f;
		cmd.forwardmove = 0;
		cmd.rightmove = 0;
	} else {
		zoomStick[0] = 0.0f;
	}
	if ( HeldSpyglass( hands[1] ) != nullptr ) {
		zoomStick[1] = cmd.vrTurnStickY / 127.0f;
		LockTurning( true );
	} else {
		zoomStick[1] = 0.0f;
		LockTurning( false );
	}
}

void idVRSpyglass::ScanEntities( idPlayer *player ) {
	idList<idEntity *> vanilla;
	for ( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != nullptr; ent = ent->spawnNode.Next() ) {
		if ( IsSpyglass( ent ) ) {
			bool known = false;
			for ( const lens_t *lens : lenses ) {
				if ( lens->ent.GetEntity() == ent ) {
					known = true;
					break;
				}
			}
			if ( !known ) {
				lens_t *lens = new lens_t;
				memset( &lens->re, 0, sizeof( lens->re ) );
				memset( &lens->view, 0, sizeof( lens->view ) );
				lens->ent = ent;
				lenses.Append( lens );
			}
		} else if ( IsVanillaSpyglass( ent ) && !ent->IsHidden() && ent->GetBindMaster() == nullptr ) {
			// lying in the map (not in the inventory, which hides its items)
			vanilla.Append( ent );
		}
	}
	for ( idEntity *ent : vanilla ) {
		if ( SpawnReplacement( ent ) != nullptr ) {
			ent->Hide();
			ent->GetPhysics()->UnlinkClip();
			ent->PostEventMS( &EV_Remove, 0 );
		}
	}
}

bool idVRSpyglass::NearPlayer( idPlayer *player, const idVec3 &pos ) const {
	const idVec3 eye = player->GetEyePosition();
	if ( ( pos - eye ).LengthFast() > vr_spyglassRenderDistance.GetFloat() ) {
		return false;
	}
	if ( !gameRenderWorld ) {
		return false;
	}
	// the same room or the next one (through one portal)
	const int playerArea = gameRenderWorld->GetAreaAtPoint( eye );
	const int lensArea = gameRenderWorld->GetAreaAtPoint( pos );
	if ( playerArea < 0 || lensArea < 0 || playerArea == lensArea ) {
		return true;
	}
	const int num = gameRenderWorld->NumPortalsInArea( playerArea );
	for ( int i = 0; i < num; i++ ) {
		const exitPortal_t portal = gameRenderWorld->GetPortal( playerArea, i );
		if ( portal.areas[0] == lensArea || portal.areas[1] == lensArea ) {
			return true;
		}
	}
	return false;
}

void idVRSpyglass::UpdateLens( idPlayer *player, lens_t &lens ) {
	idEntity *ent = lens.ent.GetEntity();
	if ( ent == nullptr || ent->IsHidden() || !gameRenderWorld ) {
		FreeLens( lens );
		return;
	}
	idRenderModel *model = GetLensModel();
	const idVec3 origin = ent->GetPhysics()->GetOrigin();
	const idMat3 axis = ent->GetPhysics()->GetAxis();
	const idVec3 lensPos = origin + idVec3( LENS_X, 0, 0 ) * axis;
	const bool live = NearPlayer( player, lensPos );

	if ( live ) {
		// the far end's camera looks out along the tube; turning the spyglass about its length turns the picture
		const float zoom = idMath::ClampFloat( vr_spyglassMinZoom.GetFloat(), Max( vr_spyglassMinZoom.GetFloat(), vr_spyglassMaxZoom.GetFloat() ),
			ent->spawnArgs.GetFloat( "vr_zoom", vr_spyglassMinZoom.GetString() ) );
		renderView_t &view = lens.view;
		memset( &view, 0, sizeof( view ) );
		for ( int i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
			view.shaderParms[i] = gameLocal.globalShaderParms[i];
		}
		view.globalMaterial = gameLocal.GetGlobalMaterial();
		view.time = gameLocal.time;
		view.x = 0;
		view.y = 0;
		view.width = SCREEN_WIDTH;
		view.height = SCREEN_HEIGHT;
		view.fov_x = view.fov_y = LensFov( zoom );
		view.vieworg = origin + idVec3( CAMERA_X, 0, 0 ) * axis;
		view.viewaxis = idMat3( -axis[0], -axis[1], axis[2] );
		view.initialVieworg = view.vieworg;
		view.initialViewaxis = view.viewaxis;
		view.viewID = VID_SUBVIEW;
	}

	renderEntity_t &re = lens.re;
	memset( &re, 0, sizeof( re ) );
	re.hModel = model;
	re.origin = origin;
	re.axis = axis;
	re.bounds = model->Bounds();
	re.noShadow = true;
	re.noSelfShadow = true;
	re.suppressSurfaceInViewID = VID_LIGHTGEM;
	re.shaderParms[SHADERPARM_RED] = re.shaderParms[SHADERPARM_GREEN] = re.shaderParms[SHADERPARM_BLUE] = 1.0f;
	re.shaderParms[SHADERPARM_ALPHA] = 1.0f;
	// picture flips (parm4/5 scale, parm6/7 translate, as the vanilla camera screens do it) and live / dark glass (parm8)
	const int flip = vr_spyglassImageFlip.GetInteger();
	re.shaderParms[4] = ( flip & 1 ) ? -1.0f : 1.0f;
	re.shaderParms[5] = ( flip & 2 ) ? -1.0f : 1.0f;
	re.shaderParms[6] = ( flip & 1 ) ? -1.0f : 0.0f;
	re.shaderParms[7] = ( flip & 2 ) ? -1.0f : 0.0f;
	re.shaderParms[8] = live ? 1.0f : 0.0f;
	re.remoteRenderView = live ? &lens.view : nullptr;
	if ( lens.handle == -1 ) {
		lens.handle = gameRenderWorld->AddEntityDef( &re );
	} else {
		gameRenderWorld->UpdateEntityDef( lens.handle, &re );
	}
}

void idVRSpyglass::Update( idPlayer *player, idVRHand hands[2], float dt ) {
	if ( gameLocal.time >= nextScan ) {
		nextScan = gameLocal.time + 1000;
		ScanEntities( player );
	}

	// zoom with the holding hand's stick
	for ( int h = 0; h < 2; h++ ) {
		idEntity *ent = HeldSpyglass( hands[h] );
		const float stick = zoomStick[h];
		if ( ent == nullptr || idMath::Fabs( stick ) < 0.2f || dt <= 0.0f ) {
			continue;
		}
		const float minZoom = vr_spyglassMinZoom.GetFloat();
		const float maxZoom = Max( minZoom, vr_spyglassMaxZoom.GetFloat() );
		float zoom = ent->spawnArgs.GetFloat( "vr_zoom", vr_spyglassMinZoom.GetString() );
		zoom *= idMath::Exp( vr_spyglassZoomSpeed.GetFloat() * stick * dt );
		zoom = idMath::ClampFloat( minZoom, maxZoom, zoom );
		ent->spawnArgs.SetFloat( "vr_zoom", zoom );
		SpyglassDebug( "zoom %.2fx (fov %.1f)", zoom, LensFov( zoom ) );
	}
}

void idVRSpyglass::UpdateRender( idPlayer *player ) {
	const idVec3 head = HeadWorld( player );
	bool closeToEye = false;
	for ( int i = lenses.Num() - 1; i >= 0; i-- ) {
		lens_t *lens = lenses[i];
		idEntity *ent = lens->ent.GetEntity();
		if ( ent == nullptr ) {
			FreeLens( *lens );
			delete lens;
			lenses.RemoveIndex( i );
			continue;
		}
		UpdateLens( player, *lens );
		if ( !ent->IsHidden() ) {
			const idVec3 lensPos = ent->GetPhysics()->GetOrigin() + idVec3( LENS_X, 0, 0 ) * ent->GetPhysics()->GetAxis();
			if ( ( lensPos - head ).LengthFast() < vr_spyglassNearClip.GetFloat() ) {
				closeToEye = true;
			}
		}
	}
	eyeAtLens = closeToEye;
}
