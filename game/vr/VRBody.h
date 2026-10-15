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
 * VR body: keeps the player's game body under the real head.
 *
 * The headset reports poses in its local space. The renderer places them relative to the player's view origin,
 * minus a "local offset". When the player walks in real life, the body is moved along (with collision) and the
 * offset absorbs the move, so the view doesn't jump and turning pivots around the head.
 * Vertically, the offset maps the real head height to the game: standing tall = the normal game eye height,
 * real crouching lowers the view 1:1 and switches to the crouched body when low enough.
 * If the real head is inside a wall (the body was blocked), the view fades to black.
 */

class idPlayer;

class idVRBody {
public:
	void			Reset();
	bool			IsActive( const idPlayer *player ) const;

	// before movement: read the head, update the physical crouch state
	void			UpdateHead( idPlayer *player );
	// after the render view is calculated: move the body under the head, fade if the head is in a wall
	void			Update( idPlayer *player, renderView_t *view );

	bool			IsPhysicallyCrouched() const { return physicalCrouch; }
	const idVec3 &	GetLocalOffset() const { return localOffset; }

	// headset local space (game units) to world, using the given player render view
	idVec3			LocalToWorld( const renderView_t *view, const idVec3 &local ) const;

private:
	bool			initialized = false;
	idVec3			localOffset = vec3_zero;
	float			standingHeadHeight = 0.0f;	// head height above the real floor when standing (auto calibrated)
	float			headDrop = 0.0f;			// how far the real head is below standing height (negative = lower)
	bool			physicalCrouch = false;
	float			blackout = 0.0f;
	float			roomSpeed = 0.0f;
	float			stepDistance = 0.0f;
	int				lastTime = 0;
};

extern idCVar vr_roomscale;
extern idCVar vr_bodyWidth;
extern idCVar vr_stickCreep;
