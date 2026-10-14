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
 * VR pouch: the inventory, shown on a small leather panel above the left hand.
 *
 * Y (left controller) opens it, only while the left hand is empty. While it is open the player stands still;
 * left stick left/right flips through the items (the same items the game's inventory shows, without weapons,
 * loot and lockpicks), the left grip takes the shown item out into the left hand (it leaves the inventory),
 * Y again closes it without taking anything. Things are put back by letting go of them at the stomach.
 * The right hand never uses the pouch; it has the arrow selector (VRArrows.h).
 */

class idPlayer;
class idVRHand;
class idUserInterface;

class idVRPouch {
public:
	void			Reset();
	void			Free();

	bool			IsOpen() const { return open; }

	// per tick, before the player moves: Y toggles, the stick scrolls and movement is held while open
	void			UpdateInput( idPlayer *player, usercmd_t &cmd, idVRHand &leftHand );
	// after entities moved: place the panel above the left hand
	void			UpdatePanel( idPlayer *player, const idVRHand &leftHand );

	// the left grip was squeezed while the pouch is open: take the shown item into the hand
	bool			Take( idPlayer *player, idVRHand &leftHand );
	// a drop script put the item into the old grabber: hand it over to the left hand
	void			UpdatePendingGrabberItem( idPlayer *player, idVRHand &leftHand );

private:
	struct entry_t {
		idStr		name;
		idStr		category;
	};
	void			Open( idPlayer *player );
	void			Close( idPlayer *player );
	void			RebuildList( idPlayer *player );
	void			RefreshGui( idPlayer *player );
	void			PlaySound( idPlayer *player, const char *shader ) const;

	bool			open = false;
	bool			buttonWasDown = false;
	int				stickState = 0;			// -1 / 0 / 1: the stick is held to one side (one step per push)
	idList<entry_t>	entries;
	int				selected = 0;
	idStr			selectedName;			// remembered between openings

	idUserInterface *gui = nullptr;
	renderEntity_t	panelEntity;
	qhandle_t		panelHandle = -1;
	int				grabberWaitUntil = 0;	// drop scripts: wait for the item to show up in the grabber
};

// a small 2D panel (a GUI) floating above a hand, turned towards the eyes; handle -1 = not added yet
void VR_UpdateHandPanel( idPlayer *player, const idVRHand &hand, idUserInterface *gui, renderEntity_t &panelEntity, qhandle_t &panelHandle );
