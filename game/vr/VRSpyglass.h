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
 * VR spyglass.
 *
 * A real object (atdm:vr_spyglass, the open vanilla spyglass model) with a live picture in its eyepiece: a second
 * camera at the far end of the tube looks where the tube points, through the engine's remote render (the one that
 * shows security cameras on monitors), and the picture is drawn on a disc in the eyepiece, round, darkened and
 * slightly fish-eyed towards the edge. Only the eyepiece shows it; the far end has a dark glass cap.
 *
 * The picture is live wherever the spyglass is (held, lying on a table), as long as the player is in the same room or
 * the next one (vr_spyglassRenderDistance); the engine only renders it while the eyepiece is actually on screen.
 * The holding hand's stick up/down zooms (vr_spyglassMinZoom .. vr_spyglassMaxZoom, the vanilla maximum); that
 * stick's own job (walking on the left, turning on the right) is off while it holds the spyglass.
 *
 * Spyglasses in the map (vanilla moveable or tool spyglasses, open or closed) are swapped for the VR spyglass when
 * the map runs in VR; the vanilla inventory spyglass becomes one when taken out of the pouch.
 */

class idPlayer;
class idVRHand;

class idVRSpyglass {
public:
	void			Reset();
	void			Free();

	// before the player moves: the holding hand's stick zooms instead of walking/turning
	void			UpdateInput( idPlayer *player, usercmd_t &cmd, idVRHand hands[2] );
	// after the hands moved: swap vanilla spyglasses, zoom
	void			Update( idPlayer *player, idVRHand hands[2], float dt );
	// after everything moved this frame: place the lenses and their cameras
	void			UpdateRender( idPlayer *player );

	// the head is so close to an eyepiece that the near clip plane must come closer
	bool			WantsCloseNearPlane() const { return eyeAtLens; }

	// the VR spyglass / a vanilla spyglass (to be swapped)
	static bool		IsSpyglass( const idEntity *ent );
	static bool		IsVanillaSpyglass( const idEntity *ent );
	// a new VR spyglass at the place of ent (spawned, not yet anywhere special); nullptr if it can't be spawned
	static idEntity *SpawnReplacement( const idEntity *ent );

private:
	struct lens_t {
		idEntityPtr<idEntity> ent;
		renderEntity_t	re;
		qhandle_t		handle = -1;
		renderView_t	view;			// the far end's camera (the render entity points at it)
	};

	void			ScanEntities( idPlayer *player );
	void			UpdateLens( idPlayer *player, lens_t &lens );
	void			FreeLens( lens_t &lens );
	bool			NearPlayer( idPlayer *player, const idVec3 &pos ) const;
	void			LockTurning( bool lock );

	idList<lens_t *> lenses;
	int				nextScan = 0;
	bool			eyeAtLens = false;
	float			zoomStick[2] = { 0.0f, 0.0f };	// -1..1 per hand, while it holds a spyglass
	bool			turnLocked = false;
};
