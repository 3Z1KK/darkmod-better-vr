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

#include "precompiled.h"
#pragma hdrstop

#include "game/Game_local.h"
#include "game/MeleeWeapon.h"
#include "game/vr/VRMelee.h"
#include "game/vr/VRHands.h"
#include "game/vr/VRTools.h"

idCVar vr_meleeSwingSpeed( "vr_meleeSwingSpeed", "130", CVAR_GAME | CVAR_FLOAT | CVAR_ARCHIVE, "VR: how fast (units/s) the striking end of a held sword or blackjack has to move for a swing (130 = about 3 m/s)" );
idCVar vr_meleeSwingEnd( "vr_meleeSwingEnd", "0.5", CVAR_GAME | CVAR_FLOAT, "VR: a swing ends when the tool is slower than vr_meleeSwingSpeed times this" );
idCVar vr_meleeSwingEndTime( "vr_meleeSwingEndTime", "120", CVAR_GAME | CVAR_INTEGER, "VR: ...for this many ms" );
idCVar vr_meleeMinInto( "vr_meleeMinInto", "0.25", CVAR_GAME | CVAR_FLOAT, "VR: how directly a swing has to move into what it touches to hit it (0 = grazing counts, 1 = only straight on)" );
idCVar vr_meleeSwordDef( "vr_meleeSwordDef", "atdm:attachment_melee_shortsword", CVAR_GAME, "VR: the normal game's melee attack entity used for the sword's hits" );
idCVar vr_meleeBlackjackDef( "vr_meleeBlackjackDef", "atdm:attachment_meleetest_blackjack", CVAR_GAME, "VR: the normal game's melee attack entity used for the blackjack's hits" );
idCVar vr_meleeDebug( "vr_meleeDebug", "0", CVAR_GAME | CVAR_BOOL, "VR: print swings and hits, draw the swept tool" );

namespace {
	const char *HELPER_NAMES[2] = { "vr_melee_sword", "vr_melee_blackjack" };
}

void idVRMelee::Reset() {
	for ( int h = 0; h < 2; h++ ) {
		swings[h] = swing_t();
	}
}

void idVRMelee::Free() {
	for ( int k = 0; k < NUM_KINDS; k++ ) {
		CMeleeWeapon *helper = helpers[k].GetEntity();
		if ( helper != nullptr && helper->VR_IsSwinging() ) {
			helper->VR_EndSwing();
		}
	}
	Reset();
}

CMeleeWeapon *idVRMelee::GetHelper( idPlayer *player, int kind ) {
	CMeleeWeapon *helper = helpers[kind].GetEntity();
	if ( helper != nullptr ) {
		return helper;
	}
	// after loading a save it is still there
	idEntity *existing = gameLocal.FindEntity( HELPER_NAMES[kind] );
	if ( existing != nullptr && existing->IsType( CMeleeWeapon::Type ) ) {
		helpers[kind] = static_cast<CMeleeWeapon *>( existing );
		return helpers[kind].GetEntity();
	}

	const char *defName = kind == KIND_SWORD ? vr_meleeSwordDef.GetString() : vr_meleeBlackjackDef.GetString();
	if ( gameLocal.FindEntityDefDict( defName, false ) == nullptr ) {
		static int warned = 0;
		if ( !( warned & ( 1 << kind ) ) ) {
			warned |= 1 << kind;
			gameLocal.Warning( "VR melee: no entity def %s, swings won't hit", defName );
		}
		return nullptr;
	}
	idDict args;
	args.Set( "classname", defName );
	args.Set( "name", HELPER_NAMES[kind] );
	args.SetVector( "origin", player->GetEyePosition() );
	args.Set( "hide", "1" );
	args.Set( "solid", "0" );
	args.Set( "frobable", "0" );
	idEntity *ent = nullptr;
	if ( !gameLocal.SpawnEntityDef( args, &ent ) || ent == nullptr ) {
		return nullptr;
	}
	if ( !ent->IsType( CMeleeWeapon::Type ) ) {
		ent->PostEventMS( &EV_Remove, 0 );
		return nullptr;
	}
	ent->GetPhysics()->SetContents( 0 );
	ent->Hide();
	// bound to the player: the damage code counts hits by bound melee weapons as melee hits
	ent->Bind( player, false );
	helpers[kind] = static_cast<CMeleeWeapon *>( ent );
	return helpers[kind].GetEntity();
}

void idVRMelee::SetupTool( swing_t &s, idEntity *tool, int kind ) {
	s = swing_t();
	s.tool = tool;
	s.kind = kind;

	// the striking part: three quarters of the way from the grip to the far end of the longest side
	const idBounds &b = tool->GetPhysics()->GetBounds();
	const idVec3 size = b[1] - b[0];
	int k = 0;
	for ( int i = 1; i < 3; i++ ) {
		if ( size[i] > size[k] ) {
			k = i;
		}
	}
	idVec3 anchor;
	idMat3 handAxis;
	idVRTools::GetSnapGrip( tool, anchor, handAxis );
	const float farEnd = idMath::Fabs( b[1][k] - anchor[k] ) >= idMath::Fabs( b[0][k] - anchor[k] ) ? b[1][k] : b[0][k];
	s.strikeLocal = b.GetCenter();
	s.strikeLocal[k] = anchor[k] + 0.75f * ( farEnd - anchor[k] );
	s.bladeLocal.Zero();
	s.bladeLocal[k] = farEnd >= anchor[k] ? 1.0f : -1.0f;
}

void idVRMelee::EndSwing( swing_t &s, idPlayer *player ) {
	if ( s.active && s.kind >= 0 ) {
		CMeleeWeapon *helper = helpers[s.kind].GetEntity();
		if ( helper != nullptr ) {
			helper->VR_EndSwing();
		}
		if ( vr_meleeDebug.GetBool() ) {
			gameLocal.Printf( "VR melee: swing ends%s\n", s.done ? "" : " (missed)" );
		}
	}
	s.active = false;
	s.done = false;
	s.slowTime = 0;
}

const char *idVRMelee::ChooseAttack( const swing_t &s, const idVec3 &velDir, const idMat3 &axis ) const {
	if ( s.kind == KIND_BLACKJACK ) {
		return "overhead";		// the only attack the blackjack has
	}
	return nullptr;				// decided by the caller, which knows where the player looks
}

bool idVRMelee::Sweep( idPlayer *player, idVRHand hands[2], swing_t &s, const idVec3 &origin, const idMat3 &axis, trace_t &tr ) const {
	idEntity *tool = s.tool.GetEntity();
	idClipModel *clip = tool->GetPhysics()->GetClipModel();
	if ( clip == nullptr ) {
		return false;
	}
	idMat3 delta;
	TransposeMultiply( s.prevAxis, axis, delta );
	idRotation rotation = delta.ToRotation();
	rotation.SetOrigin( s.prevOrigin );

	// don't hit the player
	idPhysics *playerPhys = player->GetPhysics();
	const int playerContents = playerPhys->GetContents();
	playerPhys->SetContents( CONTENTS_FLASHLIGHT_TRIGGER );
	const bool combat = player->SetCombatContents( false );

	gameLocal.clip.Motion( tr, s.prevOrigin, origin, rotation, clip, s.prevAxis, CMeleeWeapon::VR_ClipMask(), tool );

	player->SetCombatContents( combat );
	playerPhys->SetContents( playerContents );

	if ( vr_meleeDebug.GetBool() ) {
		collisionModelManager->DrawModel( clip->Handle(), origin, axis, player->GetEyePosition(), idMath::INFINITY );
	}
	if ( tr.fraction >= 1.0f || tr.c.entityNum < 0 || tr.c.entityNum >= ENTITYNUM_MAX_NORMAL + 1 ) {
		return false;
	}
	idEntity *other = gameLocal.entities[tr.c.entityNum];
	if ( other == nullptr ) {
		return false;
	}
	// not what the hands hold, or anything on the player
	for ( int h = 0; h < 2; h++ ) {
		if ( hands[h].GetHeld() == other ) {
			return false;
		}
	}
	for ( idEntity *m = other; m != nullptr; m = m->GetBindMaster() ) {
		if ( m == player ) {
			return false;
		}
	}
	return true;
}

void idVRMelee::Update( idPlayer *player, idVRHand hands[2], float dt ) {
	if ( dt <= 0.0f ) {
		return;
	}
	for ( int h = 0; h < 2; h++ ) {
		swing_t &s = swings[h];
		idVRHand &hand = hands[h];

		// a sword or blackjack in this hand?
		idEntity *tool = hand.GetInteraction() == idVRHand::IK_CARRY ? hand.GetHeld() : nullptr;
		int slot = -1;
		int kind = -1;
		if ( tool != nullptr && idVRTools::IsTool( tool, &slot ) ) {
			if ( slot == idVRTools::SLOT_SWORD ) {
				kind = KIND_SWORD;
			} else if ( slot == idVRTools::SLOT_BLACKJACK ) {
				kind = KIND_BLACKJACK;
			}
		}
		if ( kind < 0 ) {
			EndSwing( s, player );
			s.tool = nullptr;
			s.kind = -1;
			s.havePrev = false;
			continue;
		}
		if ( s.tool.GetEntity() != tool || s.kind != kind ) {
			EndSwing( s, player );
			SetupTool( s, tool, kind );
		}

		const idVec3 origin = tool->GetPhysics()->GetOrigin();
		const idMat3 axis = tool->GetPhysics()->GetAxis();
		const idVec3 strike = origin + s.strikeLocal * axis;
		if ( !s.havePrev ) {
			s.havePrev = true;
			s.prevOrigin = origin;
			s.prevAxis = axis;
			s.prevStrike = strike;
			continue;
		}

		// how fast the striking part moves, not counting the player walking
		const idVec3 strikeVel = ( strike - s.prevStrike ) / dt - player->GetPhysics()->GetLinearVelocity();
		const float speed = strikeVel.Length();

		if ( !s.active ) {
			if ( speed > vr_meleeSwingSpeed.GetFloat() ) {
				CMeleeWeapon *helper = GetHelper( player, kind );
				if ( helper != nullptr ) {
					helper->VR_BeginSwing( player, "overhead" );
					s.active = helper->VR_IsSwinging();
					s.done = false;
					s.slowTime = 0;
					if ( vr_meleeDebug.GetBool() ) {
						gameLocal.Printf( "VR melee: hand %d swings the %s (%.0f units/s)\n", h, kind == KIND_SWORD ? "sword" : "blackjack", speed );
					}
				}
			}
		} else if ( speed < vr_meleeSwingSpeed.GetFloat() * vr_meleeSwingEnd.GetFloat() ) {
			s.slowTime += idMath::FtoiRound( dt * 1000.0f );
			if ( s.slowTime > vr_meleeSwingEndTime.GetInteger() ) {
				EndSwing( s, player );
			}
		} else {
			s.slowTime = 0;
		}

		CMeleeWeapon *helper = s.active ? helpers[kind].GetEntity() : nullptr;
		if ( helper != nullptr && !s.done ) {
			// sounds come from the tool; the blackjack swaps in the larger head models on AI near it
			helper->VR_MoveTo( strike );

			trace_t tr;
			if ( Sweep( player, hands, s, origin, axis, tr ) ) {
				// how the point that hit was moving
				const idVec3 pointLocal = ( tr.c.point - tr.endpos ) * tr.endAxis.Transpose();
				const idVec3 pointBefore = s.prevOrigin + pointLocal * s.prevAxis;
				idVec3 dir = tr.c.point - pointBefore;
				if ( dir.Normalize() < 0.01f ) {
					dir = strikeVel;
					dir.Normalize();
				}
				// moving into it, not grazing along it
				if ( dir * -tr.c.normal >= vr_meleeMinInto.GetFloat() ) {
					// which attack it was: along the blade = thrust, downwards = overhead, otherwise a slash
					const char *attack = ChooseAttack( s, dir, axis );
					if ( attack == nullptr ) {
						idVec3 fwd = tr.c.point - player->GetEyePosition();
						fwd.z = 0.0f;
						fwd.Normalize();
						const idVec3 left = idVec3( 0, 0, 1 ).Cross( fwd );
						if ( dir * ( s.bladeLocal * axis ) > 0.7f ) {
							attack = "thrust";
						} else if ( dir.z < -0.6f ) {
							attack = "overhead";
						} else {
							attack = dir * left < 0.0f ? "slash_lr" : "slash_rl";
						}
					}
					if ( !helper->VR_SetAttack( attack ) ) {
						helper->VR_SetAttack( "overhead" );
					}
					idEntity *other = gameLocal.entities[tr.c.entityNum];
					if ( vr_meleeDebug.GetBool() ) {
						gameLocal.Printf( "VR melee: %s hits %s (%.0f units/s)\n", attack, other ? other->name.c_str() : "?", speed );
						gameRenderWorld->DebugArrow( colorRed, pointBefore, tr.c.point, 2, 2000 );
					}
					helper->VR_MoveTo( tr.c.point );
					if ( !helper->VR_Hit( tr, pointBefore, dir ) ) {
						s.done = true;		// one hit per swing
					}
				}
			}
		}

		s.prevOrigin = origin;
		s.prevAxis = axis;
		s.prevStrike = strike;
	}
}
