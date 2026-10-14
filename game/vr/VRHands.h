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

#include "VRHandModel.h"
#include "VRTools.h"
#include "VRPouch.h"
#include "VRLocks.h"

/*
 * VR hands: the player's only way to touch the world.
 *
 * Each tracked controller drives a hand. Free hands collide with the world (they stop at walls and slide
 * along them), so only what the hand can really reach can be touched. Squeezing the grip interacts with
 * whatever the palm touches:
 *  - moveables, bodies and inventory items are held for as long as the grip is squeezed. The hand grips the
 *    object at the point it touched; from then on the drawn hand stays on that point, and the hand pulls the
 *    point towards the controller with a limited force and turns the object with a limited wrist torque.
 *    Real mass and inertia decide how the object follows: light things sit in the hand, heavy things lag,
 *    tip and sag, and two hands pull at their own grip points. Letting go keeps the object's motion (throws).
 *  - inventory items (loot, keys, readables, ...) go into the inventory only when released at the stomach
 *  - anything else that can be frobbed (doors, levers, buttons, ...) gets its normal frob action for now
 * The trigger "uses" the held object.
 *
 * The drawn hand pose is kept separate from the controller pose and every grip records where and how the
 * hand holds the object (grip point, hand orientation, contact surface), so finger posing can be added later.
 *
 * Hand state is not saved; hands simply start empty after loading a game.
 */

class idPlayer;
class idRenderModel;

struct vrHandInput_t {
	bool			tracked;
	idVec3			pos;		// controller grip pose, world space
	idMat3			axis;
	idVec3			vel;		// world space, units/s
	idVec3			angVel;		// world space, rad/s
	float			grip;
	float			trigger;
	idVec3			eye;		// where the player's head is (start of reach traces)
};

class idVRHand {
public:
	enum interactionKind_t {
		IK_NONE,
		IK_HOLD,		// physically held (moveables, bodies, moveable inventory items)
		IK_CARRY,		// kinematically carried (static inventory items)
		IK_FROB,		// generic frob action while grip is squeezed
	};

	// how the hand grips the held body, in the body's own space (for holding now, and finger posing later)
	struct grip_t {
		idVec3		anchor;			// grip point (palm centre)
		idMat3		handAxis;		// hand orientation
		idVec3		contactPoint;	// nearest surface point found when gripping
		idVec3		contactNormal;	// surface normal there (zero if unknown)
		idVec3		controllerOffset;	// grip point relative to the controller, in controller space
	};

	void			Init( int handIndex );
	void			Shutdown();

	// per tick update; hands that are not tracked release everything
	void			Update( idPlayer *player, const vrHandInput_t &input, float dt );
	void			Release( idPlayer *player, bool allowInventory );
	// after all entities moved: place the drawn hand
	void			UpdateVisual( float dt );
	const idVRHandModel &GetGlove() const { return glove; }

	idEntity *		GetTouched() const { return touched.GetEntity(); }
	idEntity *		GetHeld() const { return held.GetEntity(); }
	int				GetHeldBodyId() const { return heldBodyId; }
	interactionKind_t GetInteraction() const { return interaction; }
	const grip_t &	GetGrip() const { return grip; }
	const vrHandInput_t &GetInput() const { return input; }
	bool			IsTracked() const { return tracked; }
	const idVec3 &	GetVisualPos() const { return visualPos; }
	const idMat3 &	GetVisualAxis() const { return visualAxis; }

	// take an entity into this hand with a given grip (tools, items from the pouch)
	void			HoldEntity( idPlayer *player, idEntity *ent, int bodyId, const idVec3 &anchor, const idMat3 &handAxisInBody, const idVec3 &controllerOffset );

	// where the controller wants the grip point to be
	idVec3			GetGripTarget() const;
	// current world grip point and hand axis on the held body
	void			GetGripWorld( idVec3 &point, idMat3 &handAxis ) const;

	bool			stomachRelease;		// set by idVRHands before Release(): hand is in the stomach zone

private:
	idEntity *		FindTouchedEntity( idPlayer *player, int &bodyId ) const;
	bool			CanHold( idEntity *ent ) const;
	void			StartInteraction( idPlayer *player );
	void			MoveFreeHand( idPlayer *player );
	void			FindContact( idEntity *ent, int bodyId );
	void			BumpObject( idPlayer *player, const trace_t &tr );
	void			UpdateRenderEntity();

	int				index = 0;
	vrHandInput_t	input = {};
	bool			tracked = false;
	bool			gripDown = false;
	bool			triggerDown = false;

	idVec3			physPos = vec3_zero;	// free hand position after collision with the world
	bool			physValid = false;
	idVec3			visualPos = vec3_zero;	// the drawn hand
	idMat3			visualAxis = mat3_identity;

	idEntityPtr<idEntity> touched;
	int				touchedBodyId = 0;

	interactionKind_t interaction = IK_NONE;
	idEntityPtr<idEntity> held;
	int				heldBodyId = 0;
	grip_t			grip = {};
	idVec3			carryRelOrigin;		// carried static item relative to the controller
	idMat3			carryRelAxis;
	idVec3			carryHomeOrigin;	// where a carried static item came from
	idMat3			carryHomeAxis;
	int				slipTime = 0;
	bool			carryFromPouch = false;		// a carried item that came out of the pouch goes back into it when let go
	bool			carryKinematic = false;		// a weightless physics object (tool, key) that follows the hand exactly
	idVRLockUse		lockUse;					// a key or lockpick being used on a lock (trigger held)

	void			UpdateFingers( float dt );

	idVRHandModel	glove;
	bool			gloveTried = false;
	idRenderModel *	model = nullptr;
	renderEntity_t	renderEntity;
	qhandle_t		renderHandle = -1;
};

class idVRHands {
public:
	void			Init();
	void			Shutdown();
	void			Update( idPlayer *player );
	void			PostThink( idPlayer *player );
	void			ReleaseAll( idPlayer *player );
	bool			IsActive() const;

	// before the player moves: the pouch reads the stick and holds the player still while it is open
	void			FilterInput( idPlayer *player, usercmd_t &cmd );
	// a hand squeezed its grip: the pouch or a holster may take it (true = handled)
	bool			HandleGripPress( idPlayer *player, int handIndex );
	// a hand lets go of a tool: true if the tool went back into its holster
	bool			OnToolRelease( idPlayer *player, int handIndex, idEntity *ent );

	// true if any hand currently touches or holds this entity
	bool			IsInteractingWith( const idEntity *ent ) const;
	// number of hands currently holding/carrying/frobbing this entity
	int				CountHolding( const idEntity *ent ) const;

	// held objects don't collide with the player; after release they become solid again once they stop overlapping
	void			MakeNonSolidToPlayer( idEntity *ent );
	void			ReleaseNonSolidToPlayer( idEntity *ent, bool immediately );

private:
	void			UpdateHighlights();
	void			UpdateLightCrystal( idPlayer *player );
	void			UpdateCalibration( idPlayer *player );
	void			DriveHeldObjects( float dt );
	void			UpdatePendingClipRestores( idPlayer *player );
	bool			IsInStomachZone( const idVec3 &handLocal, const idVec3 &headLocal, const idMat3 &headLocalAxis ) const;

	idVRHand		hands[2];
	idVRTools		tools;
	idVRPouch		pouch;
	int				calibrateAt = 0;
	idStr			calibrateMode;
	idList< idEntityPtr<idEntity> > highlighted;
	int				lastTime = 0;
	float			lastDt = 0.0f;
	bool			initialized = false;

	// the light crystal ring on the left hand (replaces the HUD lightgem in VR)
	renderEntity_t	crystalEntity;
	qhandle_t		crystalHandle = -1;
	renderEntity_t	ringEntity;
	qhandle_t		ringHandle = -1;
	float			crystalLevel = 0.0f;

	struct clipRestore_t {
		idEntityPtr<idEntity> ent;
		int			clipMask;
		int			contents;
		int			refCount;
	};
	idList<clipRestore_t> clipRestores;
};

extern idCVar vr_handsEnable;

// how heavy an object feels in the hands (estimated from its size, not the game's mass)
float VR_EffectiveMass( idEntity *ent, int bodyId );
