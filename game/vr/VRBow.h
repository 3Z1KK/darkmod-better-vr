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
 * VR bow.
 *
 *  - The bow hangs at the left shoulder: either hand reaching there takes it, snapped into a fixed grip
 *    (the fist around the grip, the string towards the archer). Letting go puts it back on the shoulder.
 *    It is there if the player owns arrows (like the normal game's bow), or after grabbing a bow placed in
 *    the map: those turn into this bow.
 *  - The other hand grabbing anywhere on the bow snaps to the middle of the string and draws it: the draw is
 *    how far that hand moves back from where it grabbed. The string gets stiffer and stiffer and can't be
 *    drawn past the bow's maximum. The limbs bend with the draw.
 *  - The right shoulder gives a broadhead arrow (for testing), held at the feather end like a pencil.
 *    Touching the bow with it nocks it: the hand snaps to the string with the arrow, which lies between the
 *    string and its rest on the bow and goes back and forth with the string.
 *  - Letting go of the string shoots like the normal game: from the arrow's tip, where it points, with power
 *    from how far it was drawn. Too little draw (like letting go too early in the normal game) doesn't
 *    shoot; the arrow stays on the bow. Holding it drawn too long gets tiring and cancels, like the normal game.
 *
 * The bow is drawn by this code (it is not an entity); arrows are normal entities.
 */

class idPlayer;
class idVRHand;
class idRenderModel;

class idVRBow {
public:
	void			Reset();
	void			Free();

	// a hand squeezed its grip: take the bow (left shoulder), an arrow (right shoulder) or the string
	// (anywhere on the held bow). true if handled
	bool			TryGrab( idPlayer *player, idVRHand hands[2], int handIndex );
	// a hand grabbed a bow placed in the map: it becomes the VR bow. true if handled
	bool			TakeWorldBow( idPlayer *player, idVRHand hands[2], int handIndex, idEntity *ent );
	// a hand holding the bow or the string lets go
	void			OnRelease( idPlayer *player, idVRHand hands[2], int handIndex );
	// after the hands moved: nocking, drawing, the nocked arrow
	void			Update( idPlayer *player, idVRHand hands[2], float dt );
	// after everything moved: place the drawn bow
	void			UpdateRender( idPlayer *player, idVRHand hands[2] );

	bool			IsHeld() const { return bowHand >= 0; }
	static bool		IsWorldBow( const idEntity *ent );
	static bool		IsArrow( const idEntity *ent );

private:
	bool			Load();
	bool			HasBow( idPlayer *player ) const;
	bool			GetShoulder( idPlayer *player, bool left, idVec3 &pos ) const;
	void			GetBowPose( const idVRHand &hand, idVec3 &origin, idMat3 &axis ) const;
	bool			IsNearBow( const idVec3 &point, float margin ) const;
	void			TakeBow( idPlayer *player, idVRHand &hand, int handIndex );
	void			PutAway( idPlayer *player, idVRHand hands[2] );
	void			StartString( idPlayer *player, idVRHand hands[2], int handIndex );
	void			EndString( idPlayer *player, idVRHand hands[2], bool shoot );
	bool			SpawnArrow( idPlayer *player, idVRHand &hand );
	void			Nock( idPlayer *player, idVRHand hands[2], int handIndex, idEntity *arrow );
	void			PlaceArrow();
	bool			Shoot( idPlayer *player, idVRHand hands[2] );
	void			PoseJoints();
	void			PlaySound( idPlayer *player, const char *key ) const;
	idVec3			NockLocal() const;

	// data (from the atdm:vr_bow def)
	bool			loaded = false;
	bool			loadFailed = false;
	const idDict *	def = nullptr;
	idRenderModel *	model = nullptr;
	int				numJoints = 0;
	idList<int>		parents;
	idList<idJointQuat> restPose;
	idList<idJointQuat> fullPose;
	int				nockJoints[2] = { -1, -1 };
	idBounds		bounds;
	idVec3			restNock;
	idVec3			arrowRest;
	float			nockHeight = 0.0f;
	float			animDraw = 20.0f;
	idJointMat *	jointMats = nullptr;

	// state
	int				bowHand = -1;			// the hand holding the bow
	int				stringHand = -1;		// the hand on the string
	idVec3			bowOrigin;				// bow pose this tick (bow space -> world)
	idMat3			bowAxis;
	idVec3			stringStart;			// where the string hand grabbed, bow space
	float			draw = 0.0f;			// how far the string is drawn (units)
	int				drawnSince = 0;			// game time the draw passed the shooting threshold (0 = not)
	bool			tiredSoundPlayed = false;
	idEntityPtr<idEntity> nocked;			// the arrow on the string
	float			arrowNockZ = 0.0f;		// the arrow's back end along its model z (feather end)

	renderEntity_t	renderEntity;
	qhandle_t		renderHandle = -1;
};
