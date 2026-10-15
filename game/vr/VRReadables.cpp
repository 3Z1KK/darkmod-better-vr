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
#include "game/vr/VRReadables.h"
#include "game/vr/VRHands.h"
#include "game/vr/VRPouch.h"

idCVar vr_readablesInHand( "vr_readablesInHand", "0", CVAR_GAME | CVAR_BOOL, "VR: set by the game while VR hands are on; the readable script then keeps fixed readables open until the hand lets go" );
idCVar vr_readableSize( "vr_readableSize", "24", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: width of the readable's page area at the hand (units, 1 unit = 2.3 cm); the page itself covers part of it" );
idCVar vr_readableHeight( "vr_readableHeight", "8", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: how far above the holding hand the page's centre floats (units)" );
idCVar vr_readableForward( "vr_readableForward", "2", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: how far from the hand towards the eyes the page floats (units)" );

namespace {
	const char *STATE_CLOSE_REQUEST = "CloseRequest";
	const char *STATE_CHANGE_PAGE_REQUEST = "ChangePageRequest";
	const char *STATE_ANIMATION_IN_PROGRESS = "AnimationInProgress";
	const float TRIGGER_PRESS = 0.6f;
	const float TRIGGER_RELEASE = 0.4f;

	bool HoldsInHand( const idVRHand &hand, idEntity *ent ) {
		const idVRHand::interactionKind_t k = hand.GetInteraction();
		return ( k == idVRHand::IK_HOLD || k == idVRHand::IK_CARRY || k == idVRHand::IK_FROB ) && hand.GetHeld() == ent;
	}
}

void idVRReadables::Reset() {
	open.Clear();
	closing.Clear();
	for ( int i = 0; i < MAX_HAND_GUIS; i++ ) {
		handGuis[i] = nullptr;
	}
	pendingOpen = false;
	pendingSince = 0;
}

bool idVRReadables::IsGuiInHand( const idUserInterface *gui ) const {
	if ( gui == nullptr ) {
		return false;
	}
	if ( pendingOpen ) {
		return true;
	}
	for ( int i = 0; i < MAX_HAND_GUIS; i++ ) {
		if ( handGuis[i] == gui ) {
			return true;
		}
	}
	return false;
}

void idVRReadables::RefreshHandGuis( idPlayer *player ) {
	int n = 0;
	bool pending = false;
	for ( int i = 0; i < open.Num() && n < MAX_HAND_GUIS; i++ ) {
		const int overlay = GetOverlayHandle( open[i].ent.GetEntity() );
		idUserInterface *gui = overlay != -1 ? player->GetOverlay( overlay ) : nullptr;
		if ( gui != nullptr ) {
			handGuis[n++] = gui;
		} else {
			pending = true;
		}
	}
	// (give the script a moment to create the page; a readable that never opens doesn't block GUIs for long)
	if ( pending && !pendingOpen ) {
		pendingSince = gameLocal.time;
	}
	pendingOpen = pending && gameLocal.time < pendingSince + 2000;
	for ( int i = 0; i < closing.Num() && n < MAX_HAND_GUIS; i++ ) {
		idUserInterface *gui = player->GetOverlay( closing[i].overlay );
		if ( gui != nullptr ) {
			handGuis[n++] = gui;
		}
	}
	for ( ; n < MAX_HAND_GUIS; n++ ) {
		handGuis[n] = nullptr;
	}
}

void idVRReadables::Free() {
	for ( int i = 0; i < open.Num(); i++ ) {
		FreePanel( open[i] );
	}
	Reset();
}

bool idVRReadables::IsReadable( idEntity *ent, bool *mobile ) {
	if ( ent == nullptr || !ent->scriptObject.HasObject() || ent->scriptObject.GetVariable( "overlayHandle", ev_float ) == nullptr ) {
		return false;
	}
	const bool isMobile = ent->scriptObject.GetFunction( "inventoryUse" ) != nullptr;
	const bool isImmobile = ent->scriptObject.GetFunction( "onFrob" ) != nullptr;
	if ( !isMobile && !isImmobile ) {
		return false;
	}
	if ( mobile != nullptr ) {
		*mobile = isMobile;
	}
	return true;
}

float idVRReadables::GetScriptFloat( idEntity *ent, const char *name, float def ) {
	if ( ent == nullptr || !ent->scriptObject.HasObject() ) {
		return def;
	}
	const byte *p = ent->scriptObject.GetVariable( name, ev_float );
	return p != nullptr ? *reinterpret_cast<const float *>( p ) : def;
}

int idVRReadables::GetOverlayHandle( idEntity *ent ) {
	return idMath::FtoiRound( GetScriptFloat( ent, "overlayHandle", -1.0f ) );
}

bool idVRReadables::IsOverlayInHand( int overlayHandle ) const {
	if ( overlayHandle == -1 ) {
		return false;
	}
	for ( int i = 0; i < open.Num(); i++ ) {
		if ( open[i].overlay == overlayHandle || GetOverlayHandle( open[i].ent.GetEntity() ) == overlayHandle ) {
			return true;
		}
	}
	for ( int i = 0; i < closing.Num(); i++ ) {
		if ( closing[i].overlay == overlayHandle ) {
			return true;
		}
	}
	return false;
}

void idVRReadables::FreePanel( open_t &o ) {
	if ( o.panelHandle != -1 && gameRenderWorld ) {
		gameRenderWorld->FreeEntityDef( o.panelHandle );
	}
	o.panelHandle = -1;
}

void idVRReadables::Open( idPlayer *player, idEntity *ent, int hand, bool mobile, idVRHand &h ) {
	open_t o;
	o.ent = ent;
	o.hand = hand;
	o.mobile = mobile;
	o.triggerDown = h.GetInput().trigger > TRIGGER_PRESS;	// a trigger already squeezed doesn't turn the page
	o.overlay = GetOverlayHandle( ent );
	if ( o.overlay == -1 ) {
		if ( mobile ) {
			// like using it from the inventory in the normal game
			ent->CallScriptFunctionArgs( "inventoryUse", true, 0, "eeed", ent, player, static_cast<idEntity *>( nullptr ), 0 );
		} else if ( h.GetInteraction() != idVRHand::IK_FROB ) {
			// (a frobbed fixed readable opens through its own frob action)
			ent->CallScriptFunctionArgs( "onFrob", true, 0, "e", ent );
		}
	}
	open.Append( o );
	if ( o.overlay == -1 ) {
		pendingOpen = true;			// the page is about to appear
		pendingSince = gameLocal.time;
	}
	gameLocal.Printf( "VR readables: %s opened in hand %d\n", ent->name.c_str(), hand );
}

void idVRReadables::Close( idPlayer *player, int index ) {
	open_t &o = open[index];
	FreePanel( o );
	idEntity *ent = o.ent.GetEntity();
	const int overlay = ent != nullptr ? GetOverlayHandle( ent ) : -1;
	if ( overlay != -1 ) {
		idUserInterface *gui = player->GetOverlay( overlay );
		if ( gui != nullptr ) {
			gui->SetStateInt( STATE_CLOSE_REQUEST, 1 );
			gui->StateChanged( gameLocal.time );
		}
		// until the script has closed it: no closing animation (the page is not shown anywhere any more)
		closing_t c;
		c.ent = ent;
		c.overlay = overlay;
		c.until = gameLocal.time + 5000;
		closing.Append( c );
	}
	open.RemoveIndex( index );
}

void idVRReadables::TurnPage( idPlayer *player, open_t &o ) {
	idEntity *ent = o.ent.GetEntity();
	const int overlay = GetOverlayHandle( ent );
	idUserInterface *gui = overlay != -1 ? player->GetOverlay( overlay ) : nullptr;
	if ( gui == nullptr ) {
		return;
	}
	const int page = idMath::FtoiRound( GetScriptFloat( ent, "page", 1.0f ) );
	const int numPages = idMath::FtoiRound( GetScriptFloat( ent, "numPages", 1.0f ) );
	if ( numPages <= 1 ) {
		return;
	}
	// the script turns one page per request (queued requests add up)
	const int queued = gui->GetStateInt( STATE_CHANGE_PAGE_REQUEST );
	if ( queued < 0 ) {
		return;							// still going back to the first page
	}
	const int target = page + queued;
	int request;
	if ( target >= numPages ) {
		request = 1 - page;				// after the last page: back to the first
	} else {
		request = queued + 1;
	}
	gui->SetStateInt( STATE_CHANGE_PAGE_REQUEST, request );
	gui->StateChanged( gameLocal.time );
}

void idVRReadables::Update( idPlayer *player, idVRHand hands[2] ) {
	// what is still held, by which hand
	for ( int i = open.Num() - 1; i >= 0; i-- ) {
		open_t &o = open[i];
		idEntity *ent = o.ent.GetEntity();
		if ( ent == nullptr ) {
			FreePanel( o );
			open.RemoveIndex( i );
			continue;
		}
		if ( !HoldsInHand( hands[o.hand], ent ) ) {
			if ( HoldsInHand( hands[1 - o.hand], ent ) ) {
				// handed over to the other hand
				o.hand = 1 - o.hand;
				o.triggerDown = hands[o.hand].GetInput().trigger > TRIGGER_PRESS;
			} else {
				// let go, dropped or stored: closed
				gameLocal.Printf( "VR readables: %s closed\n", ent->name.c_str() );
				Close( player, i );
				continue;
			}
		}
		o.overlay = GetOverlayHandle( ent );

		// the trigger turns the page
		const float trigger = hands[o.hand].GetInput().trigger;
		if ( !o.triggerDown && trigger > TRIGGER_PRESS ) {
			o.triggerDown = true;
			TurnPage( player, o );
		} else if ( o.triggerDown && trigger < TRIGGER_RELEASE ) {
			o.triggerDown = false;
		}
	}

	// newly taken readables
	for ( int h = 0; h < 2; h++ ) {
		idEntity *ent = hands[h].GetHeld();
		bool mobile = false;
		if ( ent == nullptr || !HoldsInHand( hands[h], ent ) || !IsReadable( ent, &mobile ) ) {
			continue;
		}
		bool known = false;
		for ( int i = 0; i < open.Num(); i++ ) {
			if ( open[i].ent.GetEntity() == ent ) {
				known = true;
				break;
			}
		}
		if ( !known ) {
			// still closing from before: let that finish first (the script ignores opening while open)
			bool stillClosing = false;
			for ( int i = 0; i < closing.Num(); i++ ) {
				if ( closing[i].ent.GetEntity() == ent ) {
					stillClosing = true;
					break;
				}
			}
			if ( !stillClosing ) {
				Open( player, ent, h, mobile, hands[h] );
			}
		}
	}

	// closing readables: skip the fade-out animation the script waits for
	for ( int i = closing.Num() - 1; i >= 0; i-- ) {
		closing_t &c = closing[i];
		idEntity *ent = c.ent.GetEntity();
		if ( ent == nullptr || GetOverlayHandle( ent ) != c.overlay || gameLocal.time > c.until ) {
			closing.RemoveIndex( i );
			continue;
		}
		idUserInterface *gui = player->GetOverlay( c.overlay );
		if ( gui != nullptr ) {
			gui->SetStateInt( STATE_ANIMATION_IN_PROGRESS, 0 );
			gui->SetStateInt( STATE_CLOSE_REQUEST, 1 );
		}
	}

	RefreshHandGuis( player );
}

void idVRReadables::UpdatePanels( idPlayer *player, idVRHand hands[2] ) {
	const renderView_t *view = player->GetRenderView();
	for ( int i = 0; i < open.Num(); i++ ) {
		open_t &o = open[i];
		const idVRHand &hand = hands[o.hand];
		const int overlay = GetOverlayHandle( o.ent.GetEntity() );
		idUserInterface *gui = overlay != -1 ? player->GetOverlay( overlay ) : nullptr;
		if ( gui == nullptr || view == nullptr || !hand.IsTracked() ) {
			FreePanel( o );
			continue;
		}

		// at the hand: above it and a little towards the eyes, facing the eyes
		const idVec3 eye = player->vrBody.LocalToWorld( view, player->usercmd.vrHeadPos );
		idVec3 pos = hand.GetVisualPos() + idVec3( 0, 0, vr_readableHeight.GetFloat() );
		idVec3 toEye = eye - pos;
		toEye.Normalize();
		pos += toEye * vr_readableForward.GetFloat();
		toEye = eye - pos;
		toEye.Normalize();
		float size = vr_readableSize.GetFloat();

		// out of walls: if anything is between the eyes and the page (its centre or a corner), the page is moved
		// towards the eyes and made smaller by the same factor, so it looks the same but stays in front
		idVec3 up( 0, 0, 1 );
		up -= toEye * ( up * toEye );
		if ( up.Normalize() < 0.01f ) {
			up.Set( 1, 0, 0 );
		}
		const idVec3 right = up.Cross( toEye );
		const float hw = size * 0.5f, hh = size * 0.375f;
		const idVec3 points[5] = { pos, pos + right * hw + up * hh, pos - right * hw + up * hh, pos + right * hw - up * hh, pos - right * hw - up * hh };
		float fraction = 1.0f;
		for ( int p = 0; p < 5; p++ ) {
			trace_t tr;
			gameLocal.clip.TracePoint( tr, eye, points[p], MASK_OPAQUE, player );
			fraction = Min( fraction, tr.fraction );
		}
		if ( fraction < 1.0f ) {
			const float scale = idMath::ClampFloat( 0.1f, 1.0f, fraction - 0.04f );
			pos = eye + ( pos - eye ) * scale;
			size *= scale;
		}
		VR_UpdatePanel( gui, pos, toEye, size, o.panel, o.panelHandle );
	}
}
