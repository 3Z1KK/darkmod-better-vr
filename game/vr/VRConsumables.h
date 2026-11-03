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
 * VR: things that are used up from the hand. All of them run their vanilla use, recognised by their definitions
 * (so mappers' own ones derived from the stock ones work too):
 *
 *   food      (an "eat_it" equip script, or AIUse AIUSE_EAT): let go of at the mouth = eaten (the vanilla eat script:
 *             crunch, healing, an apple becomes its core). Not with the trigger.
 *   potions   (inventory category Potions): let go of at the mouth, or the trigger while held = drunk (the vanilla
 *             inventory use: a health potion at full health is not used up and stays in the inventory).
 *   mines     (a projectile with an armed state: flash, explosive and gas mines): the trigger sets the vanilla mine
 *             down at the hand; it falls straight down and arms on its own timer.
 *   flashbombs (a projectile without an armed state): thrown faster than vr_flashbombThrowSpeed, it becomes the
 *             vanilla flash projectile, which goes off where it hits. Let go gently it is just dropped.
 *
 * The mouth is a small zone below the eyes (vr_mouthDrop, vr_mouthRadius); only a hand letting go there counts.
 */

class idPlayer;
struct vrHandInput_t;

class idVRConsumables {
public:
	static bool		IsFood( const idEntity *ent );
	static bool		IsPotion( const idEntity *ent );
	static bool		IsMine( const idEntity *ent );
	static bool		IsFlashbomb( const idEntity *ent );

	// the hand at the mouth?
	static bool		AtMouth( idPlayer *player, const idVec3 &handPos );

	// a hand is letting go of ent (nobody else holds it): eaten / drunk / thrown. true if ent was used up
	// (the caller only finishes the hand's own release then)
	static bool		OnRelease( idPlayer *player, idEntity *ent, const vrHandInput_t &input );

	// the trigger, after the hand let go of ent: drink a potion, set a mine down. true if it did
	static bool		UseFromHand( idPlayer *player, idEntity *ent, const idVec3 &handPos );
	// does the trigger use ent from the hand (so the hand lets go first)?
	static bool		UsedByTrigger( const idEntity *ent ) { return IsPotion( ent ) || IsMine( ent ); }

private:
	static bool		Drink( idPlayer *player, idEntity *ent );
	static bool		Eat( idPlayer *player, idEntity *ent );
	static bool		LaunchProjectile( idPlayer *player, idEntity *ent, const idVec3 &start, const idVec3 &velocity );
};
