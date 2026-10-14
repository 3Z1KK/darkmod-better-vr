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
 * VR arrow selector: the arrow types the player has, on a small panel above the right hand (like the pouch on
 * the left hand).
 *
 * B (right controller) opens it, only while the right hand is empty. While it is open the player stands still
 * and doesn't turn: the right stick flips through the arrow types (only types with arrows left). The right grip
 * or B again picks the shown type and closes it. Nothing appears in the hand: from then on the right shoulder
 * gives arrows of that type, one less in the inventory each time, until there are none left.
 */

class idPlayer;
class idVRHand;
class idUserInterface;

class idVRArrowSelector {
public:
	void			Reset();
	void			Free();

	bool			IsOpen() const { return open; }

	// per tick, before the player moves: B toggles, the right stick scrolls, moving and turning are held
	void			UpdateInput( idPlayer *player, usercmd_t &cmd, idVRHand &rightHand );
	// after entities moved: place the panel above the right hand
	void			UpdatePanel( idPlayer *player, const idVRHand &rightHand );
	// the right grip was squeezed while it is open: pick the shown type
	void			Confirm( idPlayer *player );

	// the arrow type (weapon name, e.g. "broadhead") the right shoulder gives; empty if none are left
	idStr			GetSelected( idPlayer *player );

	// the player's arrow types with arrows left (weapon names), in inventory order
	static void		ListArrowTypes( idPlayer *player, idStrList &out );

private:
	void			Open( idPlayer *player );
	void			Close( idPlayer *player );
	void			RefreshGui( idPlayer *player );
	void			PlaySound( idPlayer *player, const char *shader ) const;
	void			LockTurning( bool lock );

	bool			open = false;
	bool			buttonWasDown = false;
	int				stickState = 0;
	bool			turnLocked = false;
	idStrList		entries;
	int				shown = 0;
	idStr			selected;				// the picked type (remembered)

	idUserInterface *gui = nullptr;
	renderEntity_t	panelEntity;
	qhandle_t		panelHandle = -1;
};
