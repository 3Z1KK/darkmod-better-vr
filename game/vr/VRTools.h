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
 * VR tools: invisible holsters on the body.
 *
 *   sword       left hip
 *   blackjack   right hip
 *   lockpicks   one in each wrist cuff (triangle in the left cuff, snake in the right one),
 *               pulled out by the other hand
 *
 * Reaching to a holster with an empty hand and squeezing the grip makes the tool appear in the hand,
 * snapped into its grip (the sword is always held by the hilt). Tools are weightless. Letting go returns
 * the tool to its holster (vr_toolsReturnToHolster 1) or drops it on the floor (0), where it can be picked
 * up again and put back by letting go of it at its holster. Nothing happens if the player doesn't own the tool.
 * Drawing and holstering make a small sound that only the player hears.
 */

class idPlayer;
class idVRHand;

class idVRTools {
public:
	enum slot_t {
		SLOT_SWORD,
		SLOT_BLACKJACK,
		SLOT_PICK_TRIANGLE,
		SLOT_PICK_SNAKE,
		NUM_SLOTS
	};

	void			Reset();
	// follow the body (hip holster positions)
	void			Update( idPlayer *player, float dt );

	// an empty hand squeezed its grip without touching anything: draw a tool if the hand is at a holster
	bool			TryDraw( idPlayer *player, idVRHand &hand, int handIndex, const idVRHand &otherHand );
	// a hand lets go of a tool: true if the tool went back into its holster (the entity is gone then)
	bool			OnRelease( idPlayer *player, idEntity *ent, int handIndex, const idVRHand &hand, const idVRHand &otherHand );

	// is this entity a VR tool? (slot returned)
	static bool		IsTool( const idEntity *ent, int *slot = nullptr );
	// the fixed grip of a tool: grip point and hand orientation in the tool's own space
	static void		GetSnapGrip( idEntity *ent, idVec3 &anchor, idMat3 &handAxis );

	// for the calibration / debugging: where a holster is in the world
	bool			GetHolsterPos( idPlayer *player, int slot, const idVRHand &otherHand, idVec3 &pos ) const;

private:
	bool			HasTool( idPlayer *player, int slot ) const;
	bool			IsOut( int slot ) const;
	void			PlaySound( idPlayer *player, int slot, bool draw ) const;

	float			bodyYaw = 0.0f;			// headset space yaw the body faces
	bool			bodyYawValid = false;
};

extern idCVar vr_toolsReturnToHolster;
