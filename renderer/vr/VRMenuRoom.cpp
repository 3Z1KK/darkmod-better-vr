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

#include "renderer/tr_local.h"
#include "renderer/vr/OpenXRBackend.h"
#include "renderer/vr/OpenXRInput.h"
#include "renderer/vr/VRMenuRoom.h"
#include "game/Game.h"

idCVar vr_menuRoom( "vr_menuRoom", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "VR: draw the menu room around the main menu (black without it)" );
idCVar vr_menuRoomMap( "vr_menuRoomMap", "maps/vr_menu", CVAR_RENDERER, "VR: the menu room's map (compiled with dmap)" );
idCVar vr_menuRoomEyeHeight( "vr_menuRoomEyeHeight", "62", CVAR_RENDERER | CVAR_FLOAT, "VR: the menu room's eye height above its info_player_start (units)" );
idCVar vr_menuLaser( "vr_menuLaser", "1", CVAR_RENDERER | CVAR_BOOL, "VR: draw the menu laser in the menu room" );

VRMenuRoom vrMenuRoom;

namespace {
	// a unit beam along +x: two crossed thin quads (two-sided material)
	idRenderModel *BuildLaserModel() {
		idRenderModel *model = renderModelManager->AllocModel();
		model->InitEmpty( "_vr_menu_laser" );
		modelSurface_t surf;
		surf.id = 0;
		surf.material = declManager->FindMaterial( "vr/menu_laser" );
		surf.geometry = model->AllocSurfaceTriangles( 8, 12 );
		srfTriangles_t *tri = surf.geometry;
		tri->numVerts = 8;
		tri->numIndexes = 12;
		const float w = 0.5f;
		const idVec3 corners[8] = {
			idVec3( 0, -w, 0 ), idVec3( 1, -w, 0 ), idVec3( 1, w, 0 ), idVec3( 0, w, 0 ),
			idVec3( 0, 0, -w ), idVec3( 1, 0, -w ), idVec3( 1, 0, w ), idVec3( 0, 0, w ),
		};
		const float st[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		tri->bounds.Clear();
		for ( int i = 0; i < 8; i++ ) {
			idDrawVert &v = tri->verts[i];
			v.Clear();
			v.xyz = corners[i];
			v.st.Set( st[i % 4][0], st[i % 4][1] );
			v.normal.Set( 0, 0, 1 );
			v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
			tri->bounds.AddPoint( v.xyz );
		}
		const int idx[12] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
		for ( int i = 0; i < 12; i++ ) {
			tri->indexes[i] = idx[i];
		}
		tri->tangentsCalculated = true;
		tri->facePlanesCalculated = false;
		model->AddSurface( surf );
		return model;
	}
}

bool VRMenuRoom::Load() {
	const idStr mapName = vr_menuRoomMap.GetString();
	loadedMap = mapName;
	idStr procName = mapName + ".proc";
	if ( fileSystem->ReadFile( procName, nullptr, nullptr ) < 0 ) {
		common->Printf( "VR menu room: %s not found (compile the room with dmap); the menu shows on black\n", procName.c_str() );
		loadFailed = true;
		return false;
	}
	if ( gameEdit == nullptr ) {
		return false;
	}
	world = renderSystem->AllocRenderWorld();
	if ( !world->InitFromMap( mapName ) ) {
		common->Printf( "VR menu room: couldn't load %s\n", mapName.c_str() );
		renderSystem->FreeRenderWorld( world );
		world = nullptr;
		loadFailed = true;
		return false;
	}

	idMapFile mapFile;
	if ( !mapFile.Parse( mapName + ".map" ) ) {
		common->Printf( "VR menu room: couldn't read %s.map, only its walls are drawn\n", mapName.c_str() );
	}
	eyeOrigin.Zero();
	yaw = 0.0f;
	int models = 0, lights = 0;
	for ( int i = 1; i < mapFile.GetNumEntities(); i++ ) {
		const idDict &args = mapFile.GetEntity( i )->epairs;
		const char *classname = args.GetString( "classname" );
		if ( !idStr::Icmp( classname, "info_player_start" ) ) {
			eyeOrigin = args.GetVector( "origin" );
			yaw = args.GetFloat( "angle" );
			continue;
		}
		if ( !idStr::Icmp( classname, "light" ) ) {
			renderLight_t light;
			gameEdit->ParseSpawnArgsToRenderLight( &args, &light );
			world->AddLightDef( &light );
			lights++;
			// the light's own model: its flame
			const char *modelName = args.GetString( "model" );
			if ( modelName[0] ) {
				renderEntity_t ent;
				memset( &ent, 0, sizeof( ent ) );
				ent.hModel = renderModelManager->FindModel( modelName );
				if ( ent.hModel ) {
					ent.origin = args.GetVector( "origin" );
					ent.axis = mat3_identity;
					ent.bounds = ent.hModel->Bounds( &ent );
					ent.shaderParms[SHADERPARM_RED] = ent.shaderParms[SHADERPARM_GREEN] = ent.shaderParms[SHADERPARM_BLUE] = 1.0f;
					ent.shaderParms[SHADERPARM_ALPHA] = 1.0f;
					ent.noShadow = true;
					world->AddEntityDef( &ent );
					models++;
				}
			}
			continue;
		}
		if ( args.GetString( "model" )[0] ) {
			renderEntity_t ent;
			gameEdit->ParseSpawnArgsToRenderEntity( &args, &ent );
			if ( ent.hModel ) {
				world->AddEntityDef( &ent );
				models++;
			}
		}
	}
	laserModel = BuildLaserModel();
	laserHandle = -1;
	startTime = Sys_Milliseconds();
	common->Printf( "VR menu room: %s, %d models, %d lights\n", mapName.c_str(), models, lights );
	return true;
}

void VRMenuRoom::Free() {
	if ( world ) {
		renderSystem->FreeRenderWorld( world );
		world = nullptr;
	}
	laserHandle = -1;
	loadFailed = false;
}

void VRMenuRoom::UpdateLaser( const renderView_t &view ) {
	const vrMenuPointer_t &pointer = VR_GetMenuPointer();
	if ( !vr_menuLaser.GetBool() || !pointer.valid || laserModel == nullptr ) {
		if ( laserHandle != -1 ) {
			world->FreeEntityDef( laserHandle );
			laserHandle = -1;
		}
		return;
	}
	// pointer positions are in the headset's reference space, as the eyes: placed the same way (AdjustRenderView)
	const idVec3 start = view.initialVieworg + pointer.start * view.initialViewaxis;
	const idVec3 end = view.initialVieworg + pointer.end * view.initialViewaxis;
	idVec3 dir = end - start;
	const float length = dir.Normalize();
	idVec3 side, up;
	dir.NormalVectors( side, up );
	const float width = pointer.pressed ? 0.22f : 0.15f;
	renderEntity_t ent;
	memset( &ent, 0, sizeof( ent ) );
	ent.hModel = laserModel;
	ent.origin = start;
	ent.axis = idMat3( dir * Max( 0.01f, length ), side * width, up * width );
	ent.bounds = laserModel->Bounds();
	ent.noShadow = true;
	ent.shaderParms[SHADERPARM_RED] = 1.0f;
	ent.shaderParms[SHADERPARM_GREEN] = pointer.onPanel ? 0.75f : 0.45f;
	ent.shaderParms[SHADERPARM_BLUE] = pointer.onPanel ? 0.45f : 0.25f;
	ent.shaderParms[SHADERPARM_ALPHA] = 1.0f;
	if ( laserHandle == -1 ) {
		laserHandle = world->AddEntityDef( &ent );
	} else {
		world->UpdateEntityDef( laserHandle, &ent );
	}
}

bool VRMenuRoom::IsShowing() const {
	return world != nullptr && Sys_Milliseconds() - lastDrawTime < 250;
}

bool VRMenuRoom::HidesMenuBackgrounds() const {
	return vr_menuRoom.GetBool() && !loadFailed;
}

bool VRMenuRoom::Draw() {
	if ( !vr_menuRoom.GetBool() ) {
		return false;
	}
	if ( world && loadedMap != vr_menuRoomMap.GetString() ) {
		Free();
	}
	if ( !world ) {
		if ( loadFailed || !Load() ) {
			return false;
		}
	}
	renderView_t view;
	memset( &view, 0, sizeof( view ) );
	view.vieworg = eyeOrigin + idVec3( 0, 0, vr_menuRoomEyeHeight.GetFloat() );
	view.viewaxis = idAngles( 0, yaw, 0 ).ToMat3();
	view.fov_x = view.fov_y = 90;
	view.width = SCREEN_WIDTH;
	view.height = SCREEN_HEIGHT;
	view.time = Sys_Milliseconds() - startTime;
	view.viewID = VID_PLAYER_VIEW;
	vrBackend->AdjustRenderView( &view );
	if ( !view.vrAdjusted ) {
		// no head pose yet
		view.initialVieworg = view.vieworg;
		view.initialViewaxis = view.viewaxis;
	}
	UpdateLaser( view );
	world->RenderScene( view );
	lastDrawTime = Sys_Milliseconds();
	return true;
}
