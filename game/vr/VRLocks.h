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
 * VR: using a held key or lockpick on a lock.
 *
 * The tool has to be held close to the lock mechanism: the handle or lock part that moves while the lock is
 * picked (door handles, lock handles). Only things that have no such part (many chests, drawers, windows,
 * mapper-made lockables) accept the tool anywhere close to them. Pulling the trigger then does exactly what
 * using the item on the lock does in the normal game (press, hold and release, so picking works pin by pin).
 */

class idPlayer;

class idVRLockUse {
public:
	// trigger pressed while holding this entity: start using it on a lock nearby. false = not a lock tool or no lock
	bool			Begin( idPlayer *player, idEntity *held );
	// every tick while the trigger is held; stops by itself when the tool is taken away from the lock
	void			Continue( idPlayer *player, idEntity *held );
	// trigger released (or the tool was let go)
	void			End( idPlayer *player );
	bool			IsActive() const { return target.GetEntity() != nullptr; }

	// the inventory item a held entity counts as when used on locks (the player's own lockpick for the VR picks,
	// a stand-in for keys that came out of the pouch); null if it isn't something that can be used on locks
	static CInventoryItemPtr ItemFor( idPlayer *player, idEntity *held );
	// the lockable thing the tool is held against
	static idEntity *FindTarget( idPlayer *player, idEntity *held, const CInventoryItemPtr &item );
	// tools that are weightless in the hand (picks, keys, VR tools)
	static bool		IsWeightless( const idEntity *ent );

private:
	idEntityPtr<idEntity> target;
	CInventoryItemPtr item;
	int				startTime = 0;
};
