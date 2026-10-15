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
 * VR hand climbing.
 *
 * A free hand squeezing its grip on something climbable holds on there; the drawn hand stays on that spot.
 * While a hand holds on, moving it moves the body the other way (pull down = go up, push sideways = go
 * sideways), through the normal collision (no going into walls). Two hands average. Letting go with one
 * hand and grabbing higher climbs hand over hand.
 *
 *   ledges      anything the hand can rest on that the game would let the player mantle onto: the thing is
 *               mantleable and the surface under the fingers is flat enough (the game's own pm_mantle_minflatness).
 *               Cracks and lips in walls count too. Only if the mission allows it (worldspawn "vr_hand_climbing" 1;
 *               vr_climbLedges overrides that for testing). Each hand has vr_climbGraceTime seconds (1.5) of
 *               holding the body up; after that gravity slowly pulls the body down (vr_climbSinkSpeed) and the
 *               player has to pull harder to make up for it. The grip only fails when the real hand ends up too
 *               far from where it holds on (vr_climbSlipDistance). Standing on solid ground refills both hands.
 *               Pulling up toward a held ledge starts the game's own mantle (as if jump was pressed) as soon
 *               as the ledge is in its reach.
 *   ladders     ladder surfaces (material) and vines: hands only in VR (walking or jumping into them doesn't
 *   ropes       attach any more), held as long as wanted, no gravity while held.
 *
 * Holding something in the other hand is fine; the climbing hand has to be free.
 */

class idPlayer;
class idVRHand;

class idVRClimb {
public:
	void			Reset();

	// a free hand squeezed its grip (touched = what it touches, if anything): hold on to a ledge, ladder,
	// vine or rope there. true if it did
	bool			TryGrab( idPlayer *player, idVRHand hands[2], int handIndex, idEntity *touched );
	// a hand holding on lets go (or slipped off)
	void			OnRelease( idPlayer *player, idVRHand hands[2], int handIndex );
	// after the hands moved: move the body
	void			Update( idPlayer *player, idVRHand hands[2], float dt );

	// ropes and vines are climbed, not picked up or frobbed
	static bool		IsClimbEntity( const idEntity *ent );
	// may ledges be climbed in this mission?
	static bool		LedgesEnabled();

	bool			IsHanging() const { return engaged; }

private:
	enum kind_t { KIND_NONE, KIND_LEDGE, KIND_LADDER, KIND_ROPE };

	struct gripState_t {
		kind_t		kind = KIND_NONE;
		idEntityPtr<idEntity> ent;			// what it holds on to (the world for the level geometry)
		int			body = 0;				// body of a rope
		idVec3		local;					// grip point in that body's space (world space for the world)
		idVec3		prevWorld;				// grip point last tick (follows ropes and moving things)
		idVec3		prevCtrlLocal;			// the controller in tracking space last tick
		bool		havePrev = false;
	};

	bool			FindGrip( idPlayer *player, const idVRHand &hand, idEntity *touched, gripState_t &out, idVec3 &point ) const;
	bool			FindRope( idPlayer *player, const idVRHand &hand, gripState_t &out, idVec3 &point ) const;
	idVec3			GripWorld( const gripState_t &g ) const;
	void			SetLocal( gripState_t &g, const idVec3 &point ) const;
	void			StopHanging( idPlayer *player, bool keepFalling );
	void			LockTurning( bool lock );

	gripState_t		grips[2];
	float			grace[2] = { 0.0f, 0.0f };	// seconds each hand can still hold the body up on a ledge
	bool			graceInit = false;
	float			vz = 0.0f;					// vertical speed while hanging (gravity), up is positive
	bool			engaged = false;			// the body hangs from the hands (the physics leave it alone)
	int				nextMantleTry = 0;
	bool			turnLocked = false;
};

extern idCVar vr_climbGraceTime;
