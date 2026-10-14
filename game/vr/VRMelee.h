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
 * VR melee: swinging the sword or the blackjack held in a hand.
 *
 * A swing starts when the striking end of the tool moves fast enough (vr_meleeSwingSpeed) and ends when it
 * slows down again. During a swing the tool is swept from where it was last tick to where it is now; the
 * first thing it really hits (moving into it, not grazing along it) gets the normal game's melee hit:
 * the same hidden attack entity the normal game binds to the player's weapon (atdm:attachment_melee_shortsword,
 * atdm:attachment_meleetest_blackjack) does the damage def, armor and surface multipliers, head-only
 * knockouts (with the larger head models the blackjack uses), AI parries, sounds, sparks and alerts.
 * One hit per swing; light moveables don't stop it.
 *
 * Which attack it was (for AI parries) comes from the motion: along the blade = thrust, downwards = overhead,
 * otherwise a slash left to right or right to left. The blackjack only has the overhead attack.
 */

class idPlayer;
class idVRHand;
class CMeleeWeapon;

class idVRMelee {
public:
	void			Reset();
	void			Free();
	// after the hands moved
	void			Update( idPlayer *player, idVRHand hands[2], float dt );

private:
	enum { KIND_SWORD, KIND_BLACKJACK, NUM_KINDS };

	struct swing_t {
		idEntityPtr<idEntity> tool;
		int			kind = -1;
		idVec3		strikeLocal;			// the striking part, tool space
		idVec3		bladeLocal;				// grip -> tip direction, tool space
		bool		havePrev = false;
		idVec3		prevOrigin;
		idMat3		prevAxis;
		idVec3		prevStrike;
		bool		active = false;			// swinging
		bool		done = false;			// this swing already hit
		int			slowTime = 0;			// ms the tool has been slow during a swing
	};

	void			SetupTool( swing_t &s, idEntity *tool, int kind );
	void			EndSwing( swing_t &s, idPlayer *player );
	CMeleeWeapon *	GetHelper( idPlayer *player, int kind );
	const char *	ChooseAttack( const swing_t &s, const idVec3 &velDir, const idMat3 &axis ) const;
	bool			Sweep( idPlayer *player, idVRHand hands[2], swing_t &s, const idVec3 &origin, const idMat3 &axis, trace_t &tr ) const;

	swing_t			swings[2];
	idEntityPtr<CMeleeWeapon> helpers[NUM_KINDS];
};
