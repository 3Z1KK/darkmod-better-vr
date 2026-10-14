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
 * VR: doors, chests, drawers, windows (everything that opens on a fixed path) moved by hand.
 *
 *  - Where a hand can take hold:
 *      doors with a handle: only the handle; the rest of the door is just solid
 *      doors without a handle part: only close to the latch (the edge away from the hinges, around handle height)
 *      everything else (chests, drawers, windows, cupboards...): anywhere on it
 *  - A closed mover is latched. It only opens while the trigger is held (the handle pressed down) by the hand
 *    that holds it at the latch; a locked one rattles and stays shut. It never swings open by itself.
 *  - While held, it follows the hand along its path at any speed (things in the way stop it or are pushed,
 *    like with a normal opening move). Let go and it keeps going, slowing down by friction. No gravity:
 *    a lid left half open stays half open.
 *  - Open movers can be pushed by free hands and by things that hit them (thrown objects).
 *  - Back at the closed position it latches again, with the normal close sound (louder and heard further by
 *    the AI if it was slammed). If the trigger keeps the handle pressed while closing, it closes silently and
 *    latches when the trigger is let go.
 *  - Normal moves (AI, scripts) win: hands let go of a mover that starts a normal move.
 */

class CBinaryFrobMover;
class idEntity;

class idVRMovers {
public:
	// the hand-movable thing an entity belongs to (a door handle belongs to its door); null for anything else,
	// including buttons and levers
	static CBinaryFrobMover *GetMover( idEntity *ent );
	// like GetMover, but also for things bound to a mover (decorations on a door): what a push moves
	static CBinaryFrobMover *GetPushMover( idEntity *ent );
	// can a hand that touches 'part' at 'point' take hold of it there? (true for anything that isn't a mover)
	static bool		CanGripAt( idEntity *part, const idVec3 &point );
	// trigger pressed on a mover: true if its latch may open (it isn't locked); a locked one plays its locked sound
	static bool		TryUnlatch( CBinaryFrobMover *mover );
	// a normal move (AI, scripts) is running: hands don't touch it
	static bool		IsBusy( CBinaryFrobMover *mover );
	// VR hands are in use (doors don't open by themselves on unlock then)
	static bool		HandsActive();
	// something hit a mover (CBinaryFrobMover::ApplyImpulse); true = handled here
	static bool		OnImpulse( CBinaryFrobMover *mover, const idVec3 &point, const idVec3 &impulse );

	void			Clear();
	// where along its path a held mover should be so that the grip point (in the mover's own space) is
	// closest to where the hand wants it
	float			FindFraction( CBinaryFrobMover *mover, const idVec3 &gripLocal, const idVec3 &target );
	// a hand holds the mover this tick and wants it at 'fraction'; latchHeld = trigger held at the latch
	void			Hold( CBinaryFrobMover *mover, float fraction, bool latchHeld );
	// a free hand runs into the mover (normal points from the mover towards the hand)
	void			Push( CBinaryFrobMover *mover, const idVec3 &point, const idVec3 &normal, const idVec3 &handVel );
	void			Impulse( CBinaryFrobMover *mover, const idVec3 &point, const idVec3 &impulse );
	// once per tick, after the hands
	void			Update( float dt );

private:
	struct state_t {
		idEntityPtr<idEntity> ent;
		float		f;				// position along the path, measured
		float		fVel;			// path speed (fractions per second)
		float		cmd;			// where it was told to be
		bool		cmdPending;
		float		pathLen;		// distance the far edge travels from closed to open
		float		inertia;		// size-based mass * mean squared path speed of its points
		float		holdSum;		// this tick's hand wishes
		int			holdCount;
		bool		latchHeld;
		bool		atOpenStop;
		int			lastActive;
	};

	state_t *		Find( CBinaryFrobMover *mover, bool create );
	idVec3			PathVelocity( CBinaryFrobMover *mover, float f, const idVec3 &local );

	idList<state_t>	states;
};
