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
#include "game/vr/VRPouch.h"
#include "game/vr/VRHands.h"
#include "game/Inventory/Inventory.h"
#include "game/Grabber.h"
#include "game/LodComponent.h"
#include "game/Objectives/MissionData.h"

idCVar vr_pouchHeight( "vr_pouchHeight", "5.5", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: pouch panel height above the left hand (units)" );
idCVar vr_pouchSize( "vr_pouchSize", "6", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: pouch panel width (units, 1 unit = 2.3 cm)" );

namespace {
	const char *POUCH_GUI = "guis/vr_pouch.gui";

	// a flat quad for the GUI: x = towards the viewer, y = viewer's right, z = up; texture (0,0) = top left
	// (seen from the viewer the corners go top left, top right, bottom right, bottom left: clockwise = front)
	idRenderModel *pouchModel = nullptr;
	idRenderModel *GetPouchModel() {
		if ( pouchModel != nullptr ) {
			return pouchModel;
		}
		const float hw = 0.5f, hh = 0.375f;		// 4:3, scaled by the render entity axis
		const idVec3 corners[4] = { idVec3( 0, -hw, hh ), idVec3( 0, hw, hh ), idVec3( 0, hw, -hh ), idVec3( 0, -hw, -hh ) };
		const float st[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		idRenderModel *model = renderModelManager->AllocModel();
		model->InitEmpty( "_vr_pouch_panel" );
		modelSurface_t surf;
		surf.id = 0;
		surf.material = declManager->FindMaterial( "vr/pouch_screen" );
		surf.geometry = model->AllocSurfaceTriangles( 4, 6 );
		srfTriangles_t *tri = surf.geometry;
		tri->numVerts = 4;
		tri->numIndexes = 6;
		tri->bounds.Clear();
		for ( int i = 0; i < 4; i++ ) {
			idDrawVert &dv = tri->verts[i];
			dv.Clear();
			dv.xyz = corners[i];
			dv.st.Set( st[i][0], st[i][1] );
			dv.normal.Set( 1, 0, 0 );
			dv.tangents[0].Set( 0, 1, 0 );
			dv.tangents[1].Set( 0, 0, -1 );
			dv.color[0] = dv.color[1] = dv.color[2] = dv.color[3] = 255;
			tri->bounds.AddPoint( corners[i] );
		}
		// clockwise as seen from the front (idTech 4 front faces)
		const glIndex_t idx[6] = { 0, 1, 2, 0, 2, 3 };
		memcpy( tri->indexes, idx, sizeof( idx ) );
		tri->tangentsCalculated = true;
		tri->facePlanesCalculated = false;
		tri->generateNormals = false;
		model->AddSurface( surf );
		pouchModel = model;
		return model;
	}

	bool ShownInPouch( const CInventoryItemPtr &item ) {
		if ( item == nullptr || item->GetType() != CInventoryItem::IT_ITEM || item->GetCount() <= 0 ) {
			return false;
		}
		idEntity *ent = item->GetItemEntity();
		if ( ent == nullptr ) {
			return false;
		}
		// lockpicks live in the wrist cuffs
		if ( ent->spawnArgs.GetString( "lockpick_type" )[0] != '\0' ) {
			return false;
		}
		return true;
	}
}

void idVRPouch::Reset() {
	open = false;
	buttonWasDown = false;
	stickState = 0;
	entries.Clear();
	grabberWaitUntil = 0;
}

void idVRPouch::Free() {
	if ( panelHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( panelHandle );
	}
	panelHandle = -1;
	open = false;
}

void idVRPouch::PlaySound( idPlayer *player, const char *shader ) const {
	const idSoundShader *snd = declManager->FindSound( shader, false );
	if ( snd != nullptr ) {
		player->StartSoundShader( snd, SND_CHANNEL_ANY, 0, false, nullptr );
	}
}

void idVRPouch::RebuildList( idPlayer *player ) {
	entries.Clear();
	const CInventoryPtr &inv = player->Inventory();
	if ( inv == nullptr ) {
		return;
	}
	for ( int c = 0; c < inv->GetNumCategories(); c++ ) {
		CInventoryCategoryPtr category = inv->GetCategory( c );
		if ( category == nullptr ) {
			continue;
		}
		for ( int i = 0; i < category->GetNumItems(); i++ ) {
			CInventoryItemPtr item = category->GetItem( i );
			if ( !ShownInPouch( item ) ) {
				continue;
			}
			entry_t e;
			e.name = item->GetName();
			e.category = category->GetName();
			entries.Append( e );
		}
	}
	// keep showing the same item if it is still there
	selected = 0;
	for ( int i = 0; i < entries.Num(); i++ ) {
		if ( entries[i].name == selectedName ) {
			selected = i;
			break;
		}
	}
}

void idVRPouch::RefreshGui( idPlayer *player ) {
	if ( gui == nullptr ) {
		return;
	}
	if ( entries.Num() == 0 ) {
		gui->SetStateInt( "pouch_has_item", 0 );
		gui->SetStateString( "pouch_icon", "" );
		gui->SetStateString( "pouch_name", "Empty" );
		gui->SetStateString( "pouch_group", "" );
		gui->SetStateString( "pouch_count", "" );
		gui->SetStateString( "pouch_position", "" );
	} else {
		selected = idMath::ClampInt( 0, entries.Num() - 1, selected );
		CInventoryItemPtr item = player->Inventory()->GetItem( entries[selected].name, entries[selected].category );
		if ( item == nullptr ) {
			RebuildList( player );
			RefreshGui( player );
			return;
		}
		selectedName = entries[selected].name;
		idStr name = common->Translate( item->GetName() );
		name.Replace( "\n", " " );
		gui->SetStateInt( "pouch_has_item", 1 );
		gui->SetStateString( "pouch_icon", item->GetIcon().c_str() );
		gui->SetStateString( "pouch_name", name.c_str() );
		gui->SetStateString( "pouch_group", common->Translate( item->Category()->GetName() ) );
		gui->SetStateString( "pouch_count", item->IsStackable() && item->GetCount() > 1 ? va( "x%d", item->GetCount() ) : "" );
		gui->SetStateString( "pouch_position", va( "%d / %d", selected + 1, entries.Num() ) );
	}
	gui->SetStateInt( "pouch_many", entries.Num() > 1 ? 1 : 0 );
	gui->StateChanged( gameLocal.time );
}

void idVRPouch::Open( idPlayer *player ) {
	if ( gui == nullptr ) {
		gui = uiManager->FindGui( POUCH_GUI, true, true );
		if ( gui == nullptr ) {
			gameLocal.Warning( "VR pouch: could not load %s", POUCH_GUI );
			return;
		}
	}
	open = true;
	stickState = 0;
	RebuildList( player );
	RefreshGui( player );
	PlaySound( player, "vr_pouch_open" );
}

void idVRPouch::Close( idPlayer *player ) {
	if ( !open ) {
		return;
	}
	open = false;
	if ( panelHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( panelHandle );
		panelHandle = -1;
	}
	PlaySound( player, "vr_pouch_close" );
}

void idVRPouch::UpdateInput( idPlayer *player, usercmd_t &cmd, idVRHand &leftHand ) {
	const bool buttonDown = ( cmd.vrButtons & VR_BUTTON_POUCH ) != 0;
	const bool pressed = buttonDown && !buttonWasDown;
	buttonWasDown = buttonDown;

	if ( pressed ) {
		if ( open ) {
			Close( player );
		} else if ( leftHand.IsTracked() && leftHand.GetInteraction() == idVRHand::IK_NONE && !player->AI_DEAD ) {
			// only a free left hand can rummage in the pouch
			Open( player );
		}
	}
	if ( !open ) {
		return;
	}
	if ( !leftHand.IsTracked() || player->AI_DEAD ) {
		Close( player );
		return;
	}

	// left stick left/right flips through the items, one item per push
	const int side = cmd.rightmove;
	int state = 0;
	if ( side > 80 ) {
		state = 1;
	} else if ( side < -80 ) {
		state = -1;
	} else if ( idMath::Abs( side ) > 40 ) {
		state = stickState;		// still pushed, no new step
	}
	if ( state != 0 && state != stickState && entries.Num() > 1 ) {
		selected = ( selected + state + entries.Num() ) % entries.Num();
		RefreshGui( player );
		PlaySound( player, "vr_pouch_scroll" );
	}
	stickState = state;

	// stand still while rummaging
	cmd.forwardmove = 0;
	cmd.rightmove = 0;
	cmd.upmove = 0;
}

void idVRPouch::UpdatePanel( idPlayer *player, const idVRHand &leftHand ) {
	if ( !open || gui == nullptr ) {
		return;
	}
	VR_UpdateHandPanel( player, leftHand, gui, panelEntity, panelHandle );
}

/*
 * A small 2D panel floating above a hand, turned towards the eyes (the pouch, the arrow selector).
 */
void VR_UpdateHandPanel( idPlayer *player, const idVRHand &hand, idUserInterface *gui, renderEntity_t &panelEntity, qhandle_t &panelHandle ) {
	if ( gameRenderWorld == nullptr || gui == nullptr || !hand.IsTracked() ) {
		return;
	}
	const renderView_t *view = player->GetRenderView();
	if ( view == nullptr ) {
		return;
	}
	// above the hand, turned towards the eyes
	const idVec3 eye = player->vrBody.LocalToWorld( view, player->usercmd.vrHeadPos );
	idVec3 pos = hand.GetVisualPos() + idVec3( 0, 0, vr_pouchHeight.GetFloat() );
	idVec3 toEye = eye - pos;
	toEye.Normalize();
	pos += toEye * 1.0f;
	idVec3 up( 0, 0, 1 );
	up -= toEye * ( up * toEye );
	if ( up.Normalize() < 0.01f ) {
		up.Set( 1, 0, 0 );
	}
	// the viewer looks along -toEye, so up x toEye points to the viewer's right (keeps the axis right-handed)
	const idVec3 right = up.Cross( toEye );
	const float size = vr_pouchSize.GetFloat();

	memset( &panelEntity, 0, sizeof( panelEntity ) );
	idRenderModel *model = GetPouchModel();
	panelEntity.hModel = model;
	panelEntity.origin = pos;
	panelEntity.axis = idMat3( toEye * size, right * size, up * size );
	panelEntity.bounds = model->Bounds();
	panelEntity.noShadow = true;
	panelEntity.noSelfShadow = true;
	panelEntity.suppressSurfaceInViewID = VID_LIGHTGEM;
	panelEntity.gui[0] = gui;
	panelEntity.shaderParms[SHADERPARM_RED] = panelEntity.shaderParms[SHADERPARM_GREEN] = panelEntity.shaderParms[SHADERPARM_BLUE] = 1.0f;
	panelEntity.shaderParms[SHADERPARM_ALPHA] = 1.0f;
	if ( panelHandle == -1 ) {
		panelHandle = gameRenderWorld->AddEntityDef( &panelEntity );
	} else {
		gameRenderWorld->UpdateEntityDef( panelHandle, &panelEntity );
	}
}

/*
 * Take the shown item out of the inventory into the left hand. Works like the game's own "drop item":
 * the item's entity comes back into the world (a stack spawns one more of its kind), the inventory count
 * goes down, and the entity goes straight into the hand instead of in front of the player.
 */
bool idVRPouch::Take( idPlayer *player, idVRHand &leftHand ) {
	if ( !open ) {
		return false;
	}
	if ( entries.Num() == 0 ) {
		return true;	// nothing to take, but the grip still belongs to the pouch
	}
	CInventoryItemPtr item = player->Inventory()->GetItem( entries[selected].name, entries[selected].category );
	if ( item == nullptr || item->GetCount() <= 0 ) {
		RebuildList( player );
		RefreshGui( player );
		return true;
	}
	idEntity *ent = item->GetItemEntity();
	if ( ent == nullptr ) {
		return true;
	}

	// items with their own drop script (flash bombs, mines, ...) spawn their world object themselves
	const function_t *dropScript = ent->scriptObject.GetFunction( TDM_INVENTORY_DROPSCRIPT );
	if ( dropScript != nullptr ) {
		idThread *thread = new idThread( dropScript );
		thread->CallFunctionArgs( dropScript, true, "ee", ent, player );
		thread->DelayedStart( 0 );
		grabberWaitUntil = gameLocal.time + 1000;
		Close( player );
		return true;
	}

	idEntity *takeEnt = ent;
	if ( ( item->IsStackable() && item->GetCount() > 1 ) || ent->spawnArgs.GetBool( "inv_inexhaustible", "0" ) ) {
		// stacks keep one entity for the whole stack: spawn one more for the hand
		const idDict *entityDef = gameLocal.FindEntityDefDict( ent->GetEntityDefName(), false );
		if ( entityDef == nullptr || !gameLocal.SpawnEntityDef( *entityDef, &takeEnt ) || takeEnt == nullptr ) {
			return true;
		}
		// locks know their keys by entity name: the copy stands for the stack's own entity
		takeEnt->spawnArgs.Set( "vr_use_as", ent->name.c_str() );
	}
	if ( gameLocal.m_MissionData->MatchLocationObjectives( takeEnt ) ) {
		takeEnt->m_bIsObjective = true;
	}

	// into the hand
	const vrHandInput_t &in = leftHand.GetInput();
	const idMat3 axis = in.axis;
	const idBounds &b = takeEnt->GetPhysics()->GetBounds();
	const idVec3 centre = b.GetCenter();
	takeEnt->Unbind();
	takeEnt->SetAxis( axis );
	takeEnt->SetOrigin( in.pos - centre * axis );
	LodComponent::EnableLOD( takeEnt, true );
	takeEnt->Show();

	player->ChangeInventoryItemCount( item->GetName(), item->Category()->GetName(), -1 );
	player->inventoryHUDNeedsUpdate = true;

	leftHand.HoldEntity( player, takeEnt, 0, centre, mat3_identity, idVec3( 0, 0, 0 ) );
	Close( player );
	return true;
}

void idVRPouch::UpdatePendingGrabberItem( idPlayer *player, idVRHand &leftHand ) {
	if ( grabberWaitUntil == 0 ) {
		return;
	}
	if ( gameLocal.time > grabberWaitUntil ) {
		grabberWaitUntil = 0;
		return;
	}
	CGrabber *grabber = gameLocal.m_Grabber;
	idEntity *ent = grabber ? grabber->GetSelected() : nullptr;
	if ( ent == nullptr || leftHand.GetInteraction() != idVRHand::IK_NONE ) {
		return;
	}
	grabberWaitUntil = 0;
	grabber->Update( player, false );		// let go in the old grabber...
	const vrHandInput_t &in = leftHand.GetInput();
	const idVec3 centre = ent->GetPhysics()->GetBounds().GetCenter();
	ent->SetAxis( in.axis );
	ent->SetOrigin( in.pos - centre * in.axis );
	leftHand.HoldEntity( player, ent, 0, centre, mat3_identity, idVec3( 0, 0, 0 ) );	// ...and into the hand
}
