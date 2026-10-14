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
 * VR hands: the player's only way to touch the world.
 *
 * Each tracked controller drives a hand. A hand touches whatever its palm overlaps (no aiming, no reach),
 * squeezing the grip interacts with the touched entity:
 *  - moveables, bodies and inventory items are held for as long as the grip is squeezed. Held objects are
 *    pulled towards the hand by a force limited by their mass, so heavy objects lag, sag and can slip away.
 *  - inventory items (loot, keys, readables, ...) go into the inventory only when released at the stomach
 *  - anything else that can be frobbed (doors, levers, buttons, ...) gets its normal frob action for now
 * The trigger "uses" the held object.
 *
 * Hand state is not saved; hands simply start empty after loading a game.
 */

class idPlayer;
class idRenderModel;

class idVRHand {
public:
	enum interactionKind_t {
		IK_NONE,
		IK_HOLD,		// physically held (moveables, bodies, moveable inventory items)
		IK_CARRY,		// kinematically carried (static inventory items)
		IK_FROB,		// generic frob action while grip is squeezed
	};

	void			Init( int handIndex );
	void			Shutdown();

	// per frame update; hands that are not tracked release everything
	void			Update( idPlayer *player, const idVec3 &worldPos, const idMat3 &worldAxis, bool tracked, float grip, float trigger, float dt );
	void			Release( idPlayer *player, bool allowInventory );

	idEntity *		GetTouched() const { return touched.GetEntity(); }
	idEntity *		GetHeld() const { return held.GetEntity(); }
	int				GetHeldBodyId() const { return heldBodyId; }
	interactionKind_t GetInteraction() const { return interaction; }

	// hold target for the held body (where the hand wants it to be)
	void			GetHoldTarget( idVec3 &origin, idMat3 &axis ) const;
	const idVec3 &	GetVelocity() const { return velocity; }

	bool			stomachRelease;		// set by idVRHands before Release(): hand is in the stomach zone

private:
	idEntity *		FindTouchedEntity( idPlayer *player, int &bodyId ) const;
	bool			CanHold( idEntity *ent ) const;
	void			StartInteraction( idPlayer *player );
	void			UpdateRenderEntity();

	int				index = 0;
	idVec3			pos;
	idMat3			axis;
	idVec3			lastPos;
	idVec3			velocity;
	bool			tracked = false;
	bool			gripDown = false;
	bool			triggerDown = false;

	idEntityPtr<idEntity> touched;
	int				touchedBodyId = 0;

	interactionKind_t interaction = IK_NONE;
	idEntityPtr<idEntity> held;
	int				heldBodyId = 0;
	idVec3			heldRelOrigin;		// held body origin relative to the hand
	idMat3			heldRelAxis;
	idVec3			carryHomeOrigin;	// where a carried static item came from
	idMat3			carryHomeAxis;
	int				farFromHandTime = 0;

	idRenderModel *	model = nullptr;
	renderEntity_t	renderEntity;
	qhandle_t		renderHandle = -1;
};

class idVRHands {
public:
	void			Init();
	void			Shutdown();
	void			Update( idPlayer *player );
	void			ReleaseAll( idPlayer *player );
	bool			IsActive() const;

	// true if any hand currently touches or holds this entity
	bool			IsInteractingWith( const idEntity *ent ) const;
	// number of hands currently holding/carrying/frobbing this entity
	int				CountHolding( const idEntity *ent ) const;

private:
	void			UpdateHighlights();
	void			DriveHeldObjects( float dt );
	void			UpdatePendingClipRestores( idPlayer *player );
	bool			IsInStomachZone( const idVec3 &handLocal, const idVec3 &headLocal, const idMat3 &headLocalAxis ) const;

	idVRHand		hands[2];
	idList< idEntityPtr<idEntity> > highlighted;
	int				lastTime = 0;
	bool			initialized = false;

	struct clipRestore_t {
		idEntityPtr<idEntity> ent;
		int			clipMask;
		int			contents;
		int			refCount;
	};
	idList<clipRestore_t> clipRestores;
public:
	// held objects don't collide with the player; after release they become solid again once they stop overlapping
	void			MakeNonSolidToPlayer( idEntity *ent );
	void			ReleaseNonSolidToPlayer( idEntity *ent, bool immediately );
};

extern idCVar vr_handsEnable;
