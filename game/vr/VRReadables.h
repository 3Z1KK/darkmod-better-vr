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
 * VR readables: letters, books, scrolls in the hand.
 *
 * Grabbing a readable (one lying around or taken from the pouch, or a fixed one like a book on a lectern)
 * opens it the normal game's way (its readable script, objectives and triggers included). Its page floats at
 * the holding hand, turned towards the eyes, and moves with the hand; it never goes into a wall (it is pulled
 * towards the eyes instead, looking the same size). The trigger turns the page; after the last page it goes
 * back to the first. Letting go, dropping or storing it closes it.
 * The page looks like the normal game's (no lighting of its own).
 *
 * The readable script (assets/script/tdm_readables.script) knows about VR through vr_readablesInHand:
 * a fixed readable then stays open until the hand lets go, wherever the player looks.
 */

class idPlayer;
class idVRHand;
class idUserInterface;

class idVRReadables {
public:
	void			Reset();
	void			Free();

	// after the hands moved: open what a hand took, close what was let go, turn pages
	void			Update( idPlayer *player, idVRHand hands[2] );
	// after everything moved: place the pages at the hands
	void			UpdatePanels( idPlayer *player, idVRHand hands[2] );

	// this overlay is shown at a hand (or closing), not on the screen
	bool			IsOverlayInHand( int overlayHandle ) const;
	// this GUI is a page at a hand (or closing): it is not the player's active GUI, so the controllers stay in
	// hand mode instead of the VR menu pointer mode (safe to call from the input thread)
	bool			IsGuiInHand( const idUserInterface *gui ) const;

	// a readable (scripted like the normal game's readables)? mobile = one that can be carried
	static bool		IsReadable( idEntity *ent, bool *mobile = nullptr );

private:
	struct open_t {
		idEntityPtr<idEntity> ent;
		int			hand = -1;
		bool		mobile = false;
		bool		triggerDown = false;
		int			overlay = -1;			// last seen overlay handle
		renderEntity_t panel;
		qhandle_t	panelHandle = -1;
	};
	struct closing_t {
		idEntityPtr<idEntity> ent;
		int			overlay = -1;
		int			until = 0;
	};

	static int		GetOverlayHandle( idEntity *ent );
	static float	GetScriptFloat( idEntity *ent, const char *name, float def );
	void			Open( idPlayer *player, idEntity *ent, int hand, bool mobile, idVRHand &h );
	void			Close( idPlayer *player, int index );
	void			FreePanel( open_t &o );
	void			TurnPage( idPlayer *player, open_t &o );

	void			RefreshHandGuis( idPlayer *player );

	idList<open_t>	open;
	idList<closing_t> closing;

	// the GUIs of open and closing readables, kept in a plain array for IsGuiInHand
	enum { MAX_HAND_GUIS = 8 };
	const idUserInterface * volatile handGuis[MAX_HAND_GUIS] = {};
	// a grabbed readable whose page the script hasn't created yet: the next interactive GUI is that page
	volatile bool	pendingOpen = false;
	int				pendingSince = 0;
};
