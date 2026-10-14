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
#include "game/vr/VRArrows.h"
#include "game/vr/VRHands.h"
#include "game/Inventory/Inventory.h"
#include "game/Inventory/WeaponItem.h"

namespace {
	const char *SELECTOR_GUI = "guis/vr_pouch.gui";		// the same leather panel as the pouch

	bool IsArrowWeapon( const CInventoryWeaponItemPtr &weapon ) {
		if ( weapon == nullptr || !weapon->IsEnabled() ) {
			return false;
		}
		const idDict *def = gameLocal.FindEntityDefDict( weapon->GetWeaponDefName().c_str(), false );
		return def != nullptr && idStr::FindText( def->GetString( "weapon_scriptobject" ), "arrow", false ) >= 0;
	}
}

void idVRArrowSelector::ListArrowTypes( idPlayer *player, idStrList &out ) {
	out.Clear();
	const CInventoryPtr &inv = player->Inventory();
	if ( inv == nullptr ) {
		return;
	}
	for ( int c = 0; c < inv->GetNumCategories(); c++ ) {
		CInventoryCategoryPtr category = inv->GetCategory( c );
		for ( int i = 0; category != nullptr && i < category->GetNumItems(); i++ ) {
			CInventoryWeaponItemPtr weapon = std::dynamic_pointer_cast<CInventoryWeaponItem>( category->GetItem( i ) );
			if ( IsArrowWeapon( weapon ) && weapon->GetAmmo() > 0 ) {
				out.AddUnique( weapon->GetWeaponName() );
			}
		}
	}
}

void idVRArrowSelector::Reset() {
	open = false;
	buttonWasDown = false;
	stickState = 0;
	entries.Clear();
	LockTurning( false );
}

void idVRArrowSelector::Free() {
	if ( panelHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( panelHandle );
	}
	panelHandle = -1;
	open = false;
	LockTurning( false );
}

void idVRArrowSelector::LockTurning( bool lock ) {
	if ( lock != turnLocked ) {
		turnLocked = lock;
		cvarSystem->SetCVarBool( "vr_turnLocked", lock );
	}
}

void idVRArrowSelector::PlaySound( idPlayer *player, const char *shader ) const {
	const idSoundShader *snd = declManager->FindSound( shader, false );
	if ( snd != nullptr ) {
		player->StartSoundShader( snd, SND_CHANNEL_ANY, 0, false, nullptr );
	}
}

idStr idVRArrowSelector::GetSelected( idPlayer *player ) {
	idStrList types;
	ListArrowTypes( player, types );
	if ( selected.IsEmpty() && types.Num() > 0 ) {
		// nothing picked yet: broadheads if there are any, otherwise the first type there is
		selected = types.FindIndex( "broadhead" ) >= 0 ? idStr( "broadhead" ) : types[0];
	}
	// the picked type until it runs out
	return types.FindIndex( selected ) >= 0 ? selected : idStr();
}

void idVRArrowSelector::RefreshGui( idPlayer *player ) {
	if ( gui == nullptr ) {
		return;
	}
	gui->SetStateString( "pouch_group", "Arrows" );
	if ( entries.Num() == 0 ) {
		gui->SetStateInt( "pouch_has_item", 0 );
		gui->SetStateString( "pouch_icon", "" );
		gui->SetStateString( "pouch_name", "No arrows" );
		gui->SetStateString( "pouch_count", "" );
		gui->SetStateString( "pouch_position", "" );
	} else {
		shown = idMath::ClampInt( 0, entries.Num() - 1, shown );
		CInventoryWeaponItemPtr weapon = player->GetWeaponItem( entries[shown] );
		if ( weapon == nullptr ) {
			ListArrowTypes( player, entries );
			if ( entries.Num() > 0 && player->GetWeaponItem( entries[0] ) != nullptr ) {
				shown = 0;
				RefreshGui( player );
			}
			return;
		}
		idStr name = common->Translate( weapon->GetName() );
		name.Replace( "\n", " " );
		gui->SetStateInt( "pouch_has_item", 1 );
		gui->SetStateString( "pouch_icon", weapon->GetIcon().c_str() );
		gui->SetStateString( "pouch_name", name.c_str() );
		gui->SetStateString( "pouch_count", va( "x%d", weapon->GetAmmo() ) );
		gui->SetStateString( "pouch_position", va( "%d / %d", shown + 1, entries.Num() ) );
	}
	gui->SetStateInt( "pouch_many", entries.Num() > 1 ? 1 : 0 );
	gui->StateChanged( gameLocal.time );
}

void idVRArrowSelector::Open( idPlayer *player ) {
	if ( gui == nullptr ) {
		gui = uiManager->FindGui( SELECTOR_GUI, true, true );
		if ( gui == nullptr ) {
			gameLocal.Warning( "VR arrows: could not load %s", SELECTOR_GUI );
			return;
		}
	}
	open = true;
	stickState = 0;
	ListArrowTypes( player, entries );
	// start at the type picked last
	const idStr current = GetSelected( player );
	shown = Max( 0, entries.FindIndex( current ) );
	RefreshGui( player );
	LockTurning( true );
	PlaySound( player, "vr_pouch_open" );
}

void idVRArrowSelector::Close( idPlayer *player ) {
	if ( !open ) {
		return;
	}
	open = false;
	LockTurning( false );
	if ( panelHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( panelHandle );
		panelHandle = -1;
	}
	PlaySound( player, "vr_pouch_close" );
}

void idVRArrowSelector::Confirm( idPlayer *player ) {
	if ( !open ) {
		return;
	}
	if ( shown >= 0 && shown < entries.Num() ) {
		selected = entries[shown];
	}
	Close( player );
}

void idVRArrowSelector::UpdateInput( idPlayer *player, usercmd_t &cmd, idVRHand &rightHand ) {
	const bool buttonDown = ( cmd.vrButtons & VR_BUTTON_ARROWS ) != 0;
	const bool pressed = buttonDown && !buttonWasDown;
	buttonWasDown = buttonDown;

	if ( pressed ) {
		if ( open ) {
			Confirm( player );			// B again picks the shown type
		} else if ( rightHand.IsTracked() && rightHand.GetInteraction() == idVRHand::IK_NONE && !player->AI_DEAD ) {
			Open( player );
		}
	}
	if ( !open ) {
		LockTurning( false );
		return;
	}
	if ( !rightHand.IsTracked() || player->AI_DEAD ) {
		Close( player );
		return;
	}

	// right stick left/right: one type per push
	const int side = cmd.vrTurnStick;
	int state = 0;
	if ( side > 80 ) {
		state = 1;
	} else if ( side < -80 ) {
		state = -1;
	} else if ( idMath::Abs( side ) > 40 ) {
		state = stickState;
	}
	if ( state != 0 && state != stickState && entries.Num() > 1 ) {
		shown = ( shown + state + entries.Num() ) % entries.Num();
		RefreshGui( player );
		PlaySound( player, "vr_pouch_scroll" );
	}
	stickState = state;

	// stand still while choosing
	cmd.forwardmove = 0;
	cmd.rightmove = 0;
	cmd.upmove = 0;
}

void idVRArrowSelector::UpdatePanel( idPlayer *player, const idVRHand &rightHand ) {
	if ( !open || gui == nullptr ) {
		return;
	}
	VR_UpdateHandPanel( player, rightHand, gui, panelEntity, panelHandle );
}
