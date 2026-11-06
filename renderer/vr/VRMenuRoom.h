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
 * VR main menu room: while the main menu is up in VR, a small room (maps/vr_menu, vr_menuRoomMap) is drawn around
 * the headset instead of black, and the menu hangs in front of it on its panel. Nothing of the game runs in it: its
 * compiled geometry is drawn, and from the .map file its models (func_static and the like), its lights and the
 * lights' models (flame particles). The head sits at the info_player_start (vr_menuRoomEyeHeight above it),
 * looking the way it faces, and can look around but not walk. The menu laser is drawn from the pointing hand to
 * the panel.
 *
 * Freed when a mission starts loading (its models must not be purged under it) and loaded again when the menu
 * comes back. Without a compiled room (no .proc) the menu shows on black as before.
 */

class idRenderWorld;

class VRMenuRoom {
public:
	// draws the room for this frame (call where a 3D view goes, before the tonemap). false if there is no room
	bool	Draw();
	void	Free();
	// the room was drawn just now (the menu panel moves for it)
	bool	IsShowing() const;
	// the main menu leaves out its full-screen background layers (the paper menus hang in the room on their own)
	bool	HidesMenuBackgrounds() const;

private:
	bool	Load();
	void	UpdateLaser( const renderView_t &view );

	idRenderWorld *	world = nullptr;
	bool			loadFailed = false;
	idStr			loadedMap;
	idVec3			eyeOrigin;
	float			yaw = 0.0f;
	int				startTime = 0;
	int				lastDrawTime = -100000;
	idRenderModel *	laserModel = nullptr;
	qhandle_t		laserHandle = -1;
};

extern VRMenuRoom vrMenuRoom;
extern idCVar vr_menuRoom;
