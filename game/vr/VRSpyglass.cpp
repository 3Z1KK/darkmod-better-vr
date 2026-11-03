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
idCVar vr_spyglassImageFlip( "vr_spyglassImageFlip", "2", CVAR_GAME | CVAR_INTEGER, "VR spyglass: flip the picture, 1 = left/right, 2 = up/down (rendered pictures are stored bottom-up), 3 = both. A mirrored picture seems to turn when the spyglass is turned" );
idCVar vr_spyglassFisheye( "vr_spyglassFisheye", "0.35", CVAR_GAME | CVAR_FLOAT, "VR spyglass: how much the picture is squeezed towards the edge of the lens (0 = flat). Applies on map load" );
idCVar vr_spyglassNearClip( "vr_spyglassNearClip", "6", CVAR_GAME | CVAR_FLOAT, "VR spyglass: with the head this close to an eyepiece (units) the near clip plane comes closer, so the eyepiece isn't cut away" );
idCVar vr_spyglassEnterDistance( "vr_spyglassEnterDistance", "3.5", CVAR_GAME | CVAR_FLOAT, "VR spyglass: the eyepiece this close to an eye (units, 1 unit = 2.3 cm) puts that eye to the spyglass" );
idCVar vr_spyglassExitDistance( "vr_spyglassExitDistance", "9", CVAR_GAME | CVAR_FLOAT, "VR spyglass: the (invisible) eyepiece this far from the eye takes the eye off the spyglass again" );
idCVar vr_spyglassSmoothing( "vr_spyglassSmoothing", "0.12", CVAR_GAME | CVAR_FLOAT, "VR spyglass: at the eye, how long (s) the spyglass takes to follow the hand (a little weight against hand shake)" );
idCVar vr_spyglassEyeDistance( "vr_spyglassEyeDistance", "2.75", CVAR_GAME | CVAR_FLOAT, "VR spyglass: distance between the eyes (units, 2.75 = 6.3 cm)" );
idCVar vr_spyglassRimAngle( "vr_spyglassRimAngle", "45", CVAR_GAME | CVAR_FLOAT, "VR spyglass: at the eye, the dark rim around the picture reaches this far from the middle of the view (degrees)" );
idCVar vr_spyglassDebug( "vr_spyglassDebug", "0", CVAR_GAME | CVAR_BOOL, "VR spyglass: log swaps and zoom" );

namespace {
	const char *VR_SPYGLASS_DEF = "atdm:vr_spyglass";

	// models/darkmod/player_equipment/spyglass.lwo: the tube runs along x, eyepiece (thin end) at +x, objective at -x
	const float LENS_X = 9.94f;			// eyepiece glass, just in front of the eyepiece's end face (9.91), which is closed
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

	// a flat ring at x facing +x, from radius r0 to r1
	void BuildRing( idRenderModel *model, int id, const char *material, float x, float r0, float r1 ) {
		const int numVerts = 2 * SEGMENTS;
		const int numIndexes = 6 * SEGMENTS;
		modelSurface_t surf;
		surf.id = id;
		surf.material = declManager->FindMaterial( material );
		surf.geometry = model->AllocSurfaceTriangles( numVerts, numIndexes );
		srfTriangles_t *tri = surf.geometry;
		tri->numVerts = numVerts;
		tri->numIndexes = numIndexes;
		tri->bounds.Clear();
		for ( int v = 0; v < numVerts; v++ ) {
			const float r = v < SEGMENTS ? r0 : r1;
			const float angle = idMath::TWO_PI * ( v % SEGMENTS ) / SEGMENTS;
			idDrawVert &dv = tri->verts[v];
			dv.Clear();
			dv.xyz.Set( x, idMath::Cos( angle ) * r, idMath::Sin( angle ) * r );
			dv.st.Set( 0.5f, 0.5f );
			dv.normal.Set( 1, 0, 0 );
			dv.tangents[0].Set( 0, 1, 0 );
			dv.tangents[1].Set( 0, 0, -1 );
			dv.color[0] = dv.color[1] = dv.color[2] = dv.color[3] = 255;
			tri->bounds.AddPoint( dv.xyz );
		}
		int n = 0;
		for ( int s = 0; s < SEGMENTS; s++ ) {
			const int a = s, b = ( s + 1 ) % SEGMENTS, c = a + SEGMENTS, d = b + SEGMENTS;
			// clockwise seen from +x (see BuildDisc)
			tri->indexes[n++] = a; tri->indexes[n++] = d; tri->indexes[n++] = c;
			tri->indexes[n++] = a; tri->indexes[n++] = b; tri->indexes[n++] = d;
		}
		tri->tangentsCalculated = true;
		tri->facePlanesCalculated = false;
		tri->generateNormals = false;
		model->AddSurface( surf );
	}

	float ApparentHalfAngle() {
		return idMath::ATan( LENS_R, Max( 0.1f, vr_spyglassEyeRelief.GetFloat() ) );
	}

	// at the eye: the picture as a unit disc at x = 0 facing +x, and the dark rim around it (scaled by the render
	// entity so the disc covers the eyepiece's angle)
	idRenderModel *eyeModel = nullptr;
	float eyeModelRim = 0.0f;
	idRenderModel *GetEyeModel() {
		const float rimHalf = DEG2RAD( idMath::ClampFloat( 20.0f, 80.0f, vr_spyglassRimAngle.GetFloat() ) );
		const float rim = Max( 1.05f, idMath::Tan( rimHalf ) / idMath::Tan( ApparentHalfAngle() ) );
		if ( eyeModel != nullptr && eyeModelRim == rim ) {
			return eyeModel;
		}
		idRenderModel *model = renderModelManager->AllocModel();
		model->InitEmpty( "_vr_spyglass_eye" );
		BuildDisc( model, 0, "vr/spyglass_lens", 0.0f, 1.0f, true, true );
		BuildRing( model, 1, "vr/spyglass_rim", 0.0f, 0.999f, rim );
		eyeModel = model;		// (an old one is left alone: it may still be referenced this frame)
		eyeModelRim = rim;
		return model;
	}

	// the rotation taking direction a onto direction b
	idMat3 RotationBetween( const idVec3 &a, const idVec3 &b ) {
		idVec3 axis = a.Cross( b );
		const float s = axis.Normalize();
		if ( s < 1e-5f ) {
			return mat3_identity;
		}
		const float angle = RAD2DEG( idMath::ATan( s, a * b ) );
		const idMat3 r = idRotation( vec3_origin, axis, angle ).ToMat3();
		// (used as a * r; make sure of the direction)
		return ( a * r - b ).LengthSqr() < ( a * r.Transpose() - b ).LengthSqr() ? r : r.Transpose();
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
		// weightless things (the spyglass is one) are carried, heavier ones held
		if ( hand.GetInteraction() != idVRHand::IK_HOLD && hand.GetInteraction() != idVRHand::IK_CARRY ) {
			return nullptr;
		}
		idEntity *ent = hand.GetHeld();
		return idVRSpyglass::IsSpyglass( ent ) ? ent : nullptr;
	}

	float LensFov( float zoom ) {
		// magnification against the naked eye: the lens seen from the eye covers a fixed angle, the camera
		// squeezes zoom times less of the world into it
		return RAD2DEG( 2.0f * idMath::ATan( idMath::Tan( ApparentHalfAngle() ) / Max( 0.1f, zoom ) ) );
	}

	float SpyglassZoom( const idEntity *ent ) {
		const float minZoom = vr_spyglassMinZoom.GetFloat();
		return idMath::ClampFloat( minZoom, Max( minZoom, vr_spyglassMaxZoom.GetFloat() ), ent->spawnArgs.GetFloat( "vr_zoom", vr_spyglassMinZoom.GetString() ) );
	}

	void FillCameraView( renderView_t &view, const idVec3 &origin, const idMat3 &axis, float zoom ) {
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
		view.vieworg = origin;
		view.viewaxis = axis;
		view.initialVieworg = origin;
		view.initialViewaxis = axis;
		view.viewID = VID_SUBVIEW;
	}

	void SetLensParms( renderEntity_t &re, bool live ) {
		re.shaderParms[SHADERPARM_RED] = re.shaderParms[SHADERPARM_GREEN] = re.shaderParms[SHADERPARM_BLUE] = 1.0f;
		re.shaderParms[SHADERPARM_ALPHA] = 1.0f;
		// picture flips (parm4/5 scale, parm6/7 translate, as the vanilla camera screens do it) and live / dark glass (parm8)
		const int flip = vr_spyglassImageFlip.GetInteger();
		re.shaderParms[4] = ( flip & 1 ) ? -1.0f : 1.0f;
		re.shaderParms[5] = ( flip & 2 ) ? -1.0f : 1.0f;
		re.shaderParms[6] = ( flip & 1 ) ? -1.0f : 0.0f;
		re.shaderParms[7] = ( flip & 2 ) ? -1.0f : 0.0f;
		re.shaderParms[8] = live ? 1.0f : 0.0f;
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
	// full precision: SetMatrix writes 2 decimals, and the spawn code takes a matrix that isn't orthogonal enough
	// as a request to bake it into the model (proxy model, entity axis reset), which put the picture 90 degrees off
	idMat3 spawnAxis = axis;
	spawnAxis.OrthoNormalizeSelf();
	args.Set( "rotation", spawnAxis.ToString( 8 ) );
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
	eyeEnt = nullptr;
	eyeHandle = -1;
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
	ExitEyeMode();
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
	if ( ent == nullptr || ent->IsHidden() || !gameRenderWorld || ent == eyeEnt.GetEntity() ) {
		// (at the eye, the picture is drawn in front of the eye instead)
		FreeLens( lens );
		return;
	}
	idRenderModel *model = GetLensModel();
	const idVec3 origin = ent->GetPhysics()->GetOrigin();
	const idMat3 axis = ent->GetPhysics()->GetAxis();
	const idVec3 lensPos = origin + idVec3( LENS_X, 0, 0 ) * axis;
	const bool live = NearPlayer( player, lensPos );
	if ( live != ent->spawnArgs.GetBool( "vr_lens_live", "0" ) ) {
		ent->spawnArgs.SetBool( "vr_lens_live", live );
		SpyglassDebug( "%s: picture %s", ent->name.c_str(), live ? "live" : "off (too far)" );
	}

	if ( live ) {
		// the far end's camera looks out along the tube; with the spyglass, the camera turns about its length too
		// (the picture is on the glass, which turns the same way: the world stays level)
		FillCameraView( lens.view, origin + idVec3( CAMERA_X, 0, 0 ) * axis, idMat3( -axis[0], -axis[1], axis[2] ), SpyglassZoom( ent ) );
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
	SetLensParms( re, live );
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

void idVRSpyglass::EyePositions( idPlayer *player, idVec3 eyes[2], idMat3 &headAxis ) const {
	const renderView_t *view = player->GetRenderView();
	if ( view == nullptr ) {
		eyes[0] = eyes[1] = player->GetEyePosition();
		headAxis = player->viewAxis;
		return;
	}
	// the headset's origin is between the eyes
	const idVec3 head = player->vrBody.LocalToWorld( view, player->usercmd.vrHeadPos );
	headAxis = player->usercmd.vrHeadAxis.ToMat3() * view->viewaxis;
	const float half = 0.5f * vr_spyglassEyeDistance.GetFloat();
	eyes[0] = head + headAxis[1] * half;
	eyes[1] = head - headAxis[1] * half;
}

void idVRSpyglass::ExitEyeMode() {
	idEntity *ent = eyeEnt.GetEntity();
	if ( ent != nullptr ) {
		ent->GetRenderEntity()->suppressSurfaceInViewID = eyeSavedSuppress;
		ent->UpdateVisuals();
		SpyglassDebug( "%s: off the eye", ent->name.c_str() );
	}
	eyeEnt = nullptr;
	if ( eyeHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( eyeHandle );
	}
	eyeHandle = -1;
}

void idVRSpyglass::UpdateEyeMode( idPlayer *player, idVRHand hands[2] ) {
	idVec3 eyes[2];
	idMat3 headAxis;
	EyePositions( player, eyes, headAxis );

	// still at the eye? (dropped, put away, or the hand taken away from the head ends it)
	idEntity *ent = eyeEnt.GetEntity();
	if ( ent != nullptr ) {
		const idVec3 lensPos = ent->GetPhysics()->GetOrigin() + idVec3( LENS_X, 0, 0 ) * ent->GetPhysics()->GetAxis();
		if ( HeldSpyglass( hands[eyeHand] ) != ent || ent->IsHidden() || !hands[eyeHand].IsTracked()
			|| ( lensPos - eyes[eyeIndex] ).LengthFast() > vr_spyglassExitDistance.GetFloat() ) {
			ExitEyeMode();
			ent = nullptr;
		}
	} else if ( eyeHandle != -1 ) {
		ExitEyeMode();
	}

	// put to an eye?
	if ( ent == nullptr ) {
		for ( int h = 0; h < 2 && ent == nullptr; h++ ) {
			idEntity *held = HeldSpyglass( hands[h] );
			if ( held == nullptr ) {
				continue;
			}
			const idMat3 axis = held->GetPhysics()->GetAxis();
			const idVec3 lensPos = held->GetPhysics()->GetOrigin() + idVec3( LENS_X, 0, 0 ) * axis;
			for ( int e = 0; e < 2; e++ ) {
				if ( ( lensPos - eyes[e] ).LengthFast() < vr_spyglassEnterDistance.GetFloat() ) {
					ent = held;
					eyeEnt = held;
					eyeIndex = e;
					eyeHand = h;
					eyeDir = -axis[0];
					idVec3 handDir = hands[h].GetInput().pos - eyes[e];
					handDir.Normalize();
					eyeAimOffset = RotationBetween( handDir, eyeDir );
					eyeLastTime = gameLocal.time;
					eyeSavedSuppress = held->GetRenderEntity()->suppressSurfaceInViewID;
					// the spyglass itself vanishes from the player's view (it would be inside the face)
					held->GetRenderEntity()->suppressSurfaceInViewID = VID_PLAYER_VIEW;
					held->UpdateVisuals();
					SpyglassDebug( "%s: at the %s eye", held->name.c_str(), e == 0 ? "left" : "right" );
					break;
				}
			}
		}
	}
	if ( ent == nullptr || !gameRenderWorld ) {
		return;
	}

	// aim: the hand's direction from the eye (not its wobbly rotation), with a little weight
	const float dt = Max( 0, gameLocal.time - eyeLastTime ) * 0.001f;
	eyeLastTime = gameLocal.time;
	const idVec3 &eye = eyes[eyeIndex];
	idVec3 handDir = hands[eyeHand].GetInput().pos - eye;
	if ( handDir.Normalize() > 0.5f ) {
		const idVec3 target = handDir * eyeAimOffset;
		const float tau = Max( 0.001f, vr_spyglassSmoothing.GetFloat() );
		const float follow = 1.0f - idMath::Exp( -dt / tau );
		eyeDir += ( target - eyeDir ) * follow;
		eyeDir.Normalize();
	}
	// the picture keeps the head's level (turning the head turns it, as with a real spyglass at the eye)
	idVec3 up = headAxis[2] - eyeDir * ( headAxis[2] * eyeDir );
	if ( up.Normalize() < 0.1f ) {
		up = idVec3( 0, 0, 1 ) - eyeDir * eyeDir.z;
		up.Normalize();
	}
	const idVec3 left = up.Cross( eyeDir );

	// the camera at the far end of the (now invisible) tube, not inside a wall
	trace_t tr;
	const idVec3 farEnd = eye + eyeDir * ( LENS_X - CAMERA_X );
	gameLocal.clip.TracePoint( tr, eye, farEnd, MASK_SOLID, player );
	const idVec3 camPos = tr.fraction < 1.0f ? tr.endpos - eyeDir * 1.0f : farEnd;
	FillCameraView( eyeView, camPos, idMat3( eyeDir, left, up ), SpyglassZoom( ent ) );

	// the picture right in front of the eye, as big as the eyepiece looks from there
	const float dist = 1.0f;
	const float radius = dist * idMath::Tan( ApparentHalfAngle() );
	idRenderModel *model = GetEyeModel();
	memset( &eyeRe, 0, sizeof( eyeRe ) );
	eyeRe.hModel = model;
	eyeRe.origin = eye + eyeDir * dist;
	const idVec3 x = -eyeDir;
	const idVec3 y = up.Cross( x );		// the viewer's right
	eyeRe.axis = idMat3( x * radius, y * radius, up * radius );
	eyeRe.bounds = model->Bounds();
	eyeRe.noShadow = true;
	eyeRe.noSelfShadow = true;
	eyeRe.suppressSurfaceInViewID = VID_LIGHTGEM;
	eyeRe.weaponDepthHack = true;		// never behind anything (a wall right at the face)
	SetLensParms( eyeRe, true );
	eyeRe.remoteRenderView = &eyeView;
	if ( eyeHandle == -1 ) {
		eyeHandle = gameRenderWorld->AddEntityDef( &eyeRe );
	} else {
		gameRenderWorld->UpdateEntityDef( eyeHandle, &eyeRe );
	}
}

void idVRSpyglass::UpdateRender( idPlayer *player, idVRHand hands[2] ) {
	UpdateEyeMode( player, hands );
	const idVec3 head = HeadWorld( player );
	bool closeToEye = eyeEnt.GetEntity() != nullptr;
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
	if ( closeToEye != eyeAtLens ) {
		SpyglassDebug( "head %s an eyepiece (%d spyglasses)", closeToEye ? "at" : "away from", lenses.Num() );
	}
	eyeAtLens = closeToEye;
}
