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

#include "OpenXRInput.h"
#include "xr_loader.h"
#include <map>
#include "framework/Session_local.h"
#include "OpenXRBackend.h"
#include "xr_math.h"

idCVar vr_useMotionControllers( "vr_useMotionControllers", "1", CVAR_GAME|CVAR_BOOL|CVAR_ARCHIVE, "If enabled, use motion controllers for game input (wip)" );
idCVar vr_inputLefthanded( "vr_inputLefthanded", "0", CVAR_BOOL|CVAR_ARCHIVE|CVAR_GAME, "If enabled, assumes the dominant hand to be the left hand" );
idCVar vr_inputWalkHeadRelative( "vr_inputWalkHeadRelative", "1", CVAR_GAME|CVAR_BOOL|CVAR_RENDERER, "If enabled, movement is relative to head orientation. Otherwise, it is relative to controller orientation." );
idCVar vr_inputSnapTurnInterval( "vr_snapTurnAngle", "0", CVAR_GAME|CVAR_INTEGER|CVAR_ARCHIVE, "Degrees per snap turn. 0 = smooth turning (default)" );
idCVar vr_smoothTurnSpeed( "vr_smoothTurnSpeed", "120", CVAR_GAME|CVAR_FLOAT|CVAR_ARCHIVE, "Smooth turning speed in degrees per second at full stick deflection" );

namespace {
	struct actionName_t {
		OpenXRInput::Action action;
		const char *name;
		const char *description;
	};

	actionName_t actionNames[] = {
		{ OpenXRInput::XR_FORWARD, "forward", "Player walk forward axis" },
		{ OpenXRInput::XR_SIDE, "side", "Player walk side axis" },
		{ OpenXRInput::XR_MOVE_DIR_HAND, "move_dir_hand", "Player walk direction hand" },
		{ OpenXRInput::XR_YAW, "yaw", "Camera look yaw" },
		{ OpenXRInput::XR_PITCH, "pitch", "Camera look pitch" },
		{ OpenXRInput::XR_SPRINT, "sprint", "Player sprint" },
		{ OpenXRInput::XR_CROUCH, "crouch", "Player crouch" },
		{ OpenXRInput::XR_JUMP, "jump", "Player jump" },
		{ OpenXRInput::XR_FROB, "frob", "Player frob" },
		{ OpenXRInput::XR_ATTACK, "attack", "Attack" },
		{ OpenXRInput::XR_PARRY, "parry", "Parry" },
		{ OpenXRInput::XR_MENU_OPEN, "menu_open", "Open main menu" },
		{ OpenXRInput::XR_INVENTORY_OPEN, "inventory_open", "Open inventory menu" },
		{ OpenXRInput::XR_USE_ITEM, "use_item", "Use currently selected item" },
		{ OpenXRInput::XR_DROP_ITEM, "drop_item", "Drop currently selected item" },
		{ OpenXRInput::XR_MENU_AIM, "menu_aim", "Menu pointer aim" },
		{ OpenXRInput::XR_MENU_CLICK, "menu_click", "Menu pointer click" },
		{ OpenXRInput::XR_MENU_ESCAPE, "menu_escape", "Menu escape" },
		{ OpenXRInput::XR_AIM, "aim", "Frob aim controller pose" },
		{ OpenXRInput::XR_HAND_POSE, "hand_pose", "Hand pose" },
		{ OpenXRInput::XR_HAND_GRIP, "hand_grip", "Hand grip" },
		{ OpenXRInput::XR_HAND_TRIGGER, "hand_trigger", "Hand trigger" },
		{ OpenXRInput::XR_POUCH, "pouch", "Open the pouch (inventory) on the left hand" },
		{ OpenXRInput::XR_ARROWS, "arrows", "Open the arrow selector on the right hand" },
		{ OpenXRInput::XR_LANTERN, "lantern", "Toggle the lantern (vanilla inventory_use Lantern)" },
		{ OpenXRInput::XR_TURN_Y, "turn_y", "Turning stick up/down (spyglass zoom)" },
	};

	OpenXRInput::Action ActionByName( const idStr &name ) {
		for ( auto actionName : actionNames ) {
			if ( idStr::Icmp( name, actionName.name ) == 0 ) {
				return actionName.action;
			}
		}
		return OpenXRInput::XR_INVALID;
	}

	actionName_t *NameByAction( OpenXRInput::Action action ) {
		for ( auto &actionName : actionNames ) {
			if ( actionName.action == action ) {
				return &actionName;
			}
		}
		return nullptr;
	}

	struct SuggestedBinding {
		OpenXRInput::Action action;
		idStr binding;
	};

	struct BindingProfile {
		idStr profilePath;		
		idList<SuggestedBinding> bindings;
	};

	std::map<idStr, BindingProfile> suggestedBindingProfiles;

	void XR_Profile_f( const idCmdArgs &args ) {
		int numArgs = args.Argc();
		if ( numArgs != 3 ) {
			common->Printf( "xr_profile <name> <xr_interaction_profile_path>\n" );
			return;
		}

		suggestedBindingProfiles[args.Argv(1)].profilePath = args.Argv(2);
	}

	void XR_Bind_f( const idCmdArgs &args ) {
		int numArgs = args.Argc();
		if ( numArgs != 4 ) {
			common->Printf( "xr_bind <profile> <action> <xr_binding_path>\n" );
		}

		idStr profile = args.Argv( 1 );
		if ( suggestedBindingProfiles.find( profile ) == suggestedBindingProfiles.end() ) {
			common->Printf( "No such profile: %s\n", profile.c_str() );
			return;
		}
		OpenXRInput::Action action = ActionByName( args.Argv( 2 ) );
		if ( action == OpenXRInput::XR_INVALID ) {
			common->Printf( "Invalid action name: %s\n", args.Argv(2) );
			return;
		}
		SuggestedBinding binding {
			action,
			args.Argv( 3 ),
		};
		suggestedBindingProfiles[profile].bindings.AddGrow( binding );
	}

	const char *defaultXrBindingsFile = "XrProfileBindings.default.cfg";
	const char *userXrBindingsFile = "XrProfileBindings.user.cfg";
}

void OpenXRInput::Init( XrInstance instance, XrSession session ) {
	this->instance = instance;
	this->session = session;

	xrStringToPath( instance, "/user/hand/left", &handPaths[0] );
	xrStringToPath( instance, "/user/hand/right", &handPaths[1] );

	XrReferenceSpaceCreateInfo spaceCreateInfo = {
		XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		nullptr,
		XR_REFERENCE_SPACE_TYPE_VIEW,
		{ {0, 0, 0, 1}, {0, 0, 0} },
	};
	XrResult result = xrCreateReferenceSpace( session, &spaceCreateInfo, &hmdSpace );
	XR_CheckResult( result, "creating HMD reference space", instance );

	// the stage space knows where the real floor is; used for physical crouching
	XrReferenceSpaceCreateInfo stageCreateInfo = {
		XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		nullptr,
		XR_REFERENCE_SPACE_TYPE_STAGE,
		{ {0, 0, 0, 1}, {0, 0, 0} },
	};
	stageSpace = nullptr;
	if ( XR_FAILED( xrCreateReferenceSpace( session, &stageCreateInfo, &stageSpace ) ) ) {
		common->Printf( "VR: no stage reference space, real floor height unknown\n" );
		stageSpace = nullptr;
	}

	CreateAllActions();
	LoadSuggestedBindings();
	AttachActionSets();

	activeMenuHand = !vr_inputLefthanded.GetBool();
}

void OpenXRInput::Destroy() {
	if ( stageSpace != nullptr ) {
		xrDestroySpace( stageSpace );
		stageSpace = nullptr;
	}
	suggestedBindingProfiles.clear();
	actionSpaces.Clear();
}

XrActionSet OpenXRInput::CreateActionSet( const idStr &name, uint32_t priority ) {
	XrActionSetCreateInfo createInfo { XR_TYPE_ACTION_SET_CREATE_INFO, nullptr };
	strncpy( createInfo.actionSetName, name.c_str(), XR_MAX_ACTION_SET_NAME_SIZE );
	strncpy( createInfo.localizedActionSetName, name.c_str(), XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE );
	createInfo.priority = priority;
	XrActionSet actionSet = nullptr;
	XrResult result = xrCreateActionSet( instance, &createInfo, &actionSet );
	XR_CheckResult( result, "creating action set", instance, false );
	return actionSet;
}


void OpenXRInput::CreateAction( XrActionSet actionSet, Action action, XrActionType actionType, uint32_t numSubPaths, XrPath *subPaths ) {
	const actionName_t &actionName = *NameByAction( action );
	XrActionCreateInfo createInfo { XR_TYPE_ACTION_CREATE_INFO, nullptr };
	strncpy( createInfo.actionName, actionName.name, XR_MAX_ACTION_NAME_SIZE );
	createInfo.actionType = actionType;
	createInfo.countSubactionPaths = numSubPaths;
	createInfo.subactionPaths = subPaths;
	strncpy( createInfo.localizedActionName, actionName.description, XR_MAX_LOCALIZED_ACTION_NAME_SIZE );
	XrResult result = xrCreateAction( actionSet, &createInfo, &actions[action] );
	XR_CheckResult( result, "creating action", instance, false );

	if ( actionType == XR_ACTION_TYPE_POSE_INPUT ) {
		actionSpaces.AddGrow( {
			action, XR_NULL_PATH, CreateActionSpace( action, XR_NULL_PATH )
		} );
		for ( uint32_t i = 0; i < numSubPaths; ++i ) {
			actionSpaces.AddGrow( {
				action, subPaths[i], CreateActionSpace( action, subPaths[i] )
			} );
		}
	}
}

XrSpace OpenXRInput::CreateActionSpace( Action action, XrPath subPath ) {
	XrPosef origin;
	origin.position.x = origin.position.y = origin.position.z = 0;
	origin.orientation.x = origin.orientation.y = origin.orientation.z = 0;
	origin.orientation.w = 1;
	XrActionSpaceCreateInfo spaceInfo {
		XR_TYPE_ACTION_SPACE_CREATE_INFO,
		nullptr,
		actions[action],
		subPath,
		origin,
	};
	XrSpace space = nullptr;
	XrResult result = xrCreateActionSpace( session, &spaceInfo, &space );
	XR_CheckResult( result, "creating action space", instance, false );
	return space;	
}

void OpenXRInput::CreateAllActions() {
	ingameActionSet = CreateActionSet( "gameplay" );
	CreateAction( ingameActionSet, XR_FORWARD, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_SIDE, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_MOVE_DIR_HAND, XR_ACTION_TYPE_POSE_INPUT );
	CreateAction( ingameActionSet, XR_YAW, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_PITCH, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_SPRINT, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_CROUCH, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_JUMP, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_FROB, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_ATTACK, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_PARRY, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_MENU_OPEN, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_INVENTORY_OPEN, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_AIM, XR_ACTION_TYPE_POSE_INPUT );
	CreateAction( ingameActionSet, XR_USE_ITEM, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_DROP_ITEM, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_HAND_POSE, XR_ACTION_TYPE_POSE_INPUT, 2, handPaths );
	CreateAction( ingameActionSet, XR_HAND_GRIP, XR_ACTION_TYPE_FLOAT_INPUT, 2, handPaths );
	CreateAction( ingameActionSet, XR_HAND_TRIGGER, XR_ACTION_TYPE_FLOAT_INPUT, 2, handPaths );
	CreateAction( ingameActionSet, XR_POUCH, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_ARROWS, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_LANTERN, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_TURN_Y, XR_ACTION_TYPE_FLOAT_INPUT );

	menuActionSet = CreateActionSet( "menu" );
	CreateAction( menuActionSet, XR_MENU_AIM, XR_ACTION_TYPE_POSE_INPUT, 2, handPaths );
	// the trigger's value: the runtime's own boolean threshold has no hysteresis, a trigger resting near it made
	// bursts of clicks; HandleMenuInput thresholds it itself
	CreateAction( menuActionSet, XR_MENU_CLICK, XR_ACTION_TYPE_FLOAT_INPUT, 2, handPaths );
	CreateAction( menuActionSet, XR_MENU_ESCAPE, XR_ACTION_TYPE_BOOLEAN_INPUT, 2, handPaths );
}

void OpenXRInput::LoadSuggestedBindings() {
	cmdSystem->AddCommand( "xr_profile", XR_Profile_f, CMD_FL_SYSTEM, "Creates a new XR input binding profile for a specific interaction profile path" );
	cmdSystem->AddCommand( "xr_bind", XR_Bind_f, CMD_FL_SYSTEM, "Binds an input action to the specified user path for a given XR profile" );

	if ( fileSystem->FindFile( userXrBindingsFile ) != FIND_NO ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, idStr::Fmt( "exec %s\n", userXrBindingsFile ) );
	} else if ( fileSystem->FindFile( defaultXrBindingsFile ) != FIND_NO ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, idStr::Fmt( "exec %s\n", defaultXrBindingsFile ) );
	}
	cmdSystem->ExecuteCommandBuffer();
	RegisterSuggestedBindings();
}

void OpenXRInput::RegisterSuggestedBindings() {
	for ( const auto &it : suggestedBindingProfiles ) {
		const BindingProfile &profile = it.second;
		common->Printf( "Suggesting bindings for %s ...\n", profile.profilePath.c_str() );
		XrPath profilePath;
		XrResult result = xrStringToPath( instance, profile.profilePath, &profilePath );
		if ( XR_FAILED( result )) {
			common->Printf("Failed to convert to XR path: %s\n", profile.profilePath.c_str() );
			continue;
		}

		idList<XrActionSuggestedBinding> bindings;
		for ( SuggestedBinding binding : profile.bindings ) {
			XrPath bindingPath;
			result = xrStringToPath( instance, ApplyDominantHandToActionPath( profile.profilePath, binding.binding ), &bindingPath );
			if ( XR_FAILED( result ) ) {
				common->Printf( "Failed to convert to XR path: %s\n", binding.binding.c_str() );
				continue;
			}
			bindings.AddGrow( {
				actions[binding.action],
				bindingPath,
			} );
		}

		XrInteractionProfileSuggestedBinding suggestedBindings {
			XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
			nullptr,
			profilePath,
			bindings.Num(),
			bindings.Ptr(),
		};
		result = xrSuggestInteractionProfileBindings( instance, &suggestedBindings );
		XR_CheckResult( result, "suggesting input bindings", instance, false );
	}
}

void OpenXRInput::AttachActionSets() {
	idList<XrActionSet> actionSets = { ingameActionSet, menuActionSet };
	XrSessionActionSetsAttachInfo attachInfo {
		XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
		nullptr,
		actionSets.Num(),
		actionSets.Ptr(),
	};
	XrResult result = xrAttachSessionActionSets( session, &attachInfo );
	XR_CheckResult( result, "attaching action sets", instance, false );
}

idStr OpenXRInput::ApplyDominantHandToActionPath( const idStr &profile, const idStr &path ) {
	idStr result = path;
	if ( vr_inputLefthanded.GetBool() ) {
		if ( profile == "/interaction_profiles/hp/mixed_reality_controller" || profile == "/interaction_profiles/oculus/touch_controller" ) {
			result.Replace( "/user/hand/main/input/a/", "/user/hand/left/input/x/" );
			result.Replace( "/user/hand/main/input/b/", "/user/hand/left/input/y/" );
		}
		result.Replace( "/user/hand/main/", "/user/hand/left/" );
		result.Replace( "/user/hand/off/", "/user/hand/right/" );
	} else {
		if ( profile == "/interaction_profiles/hp/mixed_reality_controller" || profile == "/interaction_profiles/oculus/touch_controller" ) {
			result.Replace( "/user/hand/off/input/a/", "/user/hand/left/input/x/" );
			result.Replace( "/user/hand/off/input/b/", "/user/hand/left/input/y/" );
		}
		result.Replace( "/user/hand/main/", "/user/hand/right/" );
		result.Replace( "/user/hand/off/", "/user/hand/left/" );
	}
	return result;
}

void OpenXRInput::SyncActions( const idList<XrActiveActionSet> &activeSets ) {
	XrActionsSyncInfo syncInfo {
		XR_TYPE_ACTIONS_SYNC_INFO,
		nullptr,
		activeSets.Num(),
		activeSets.Ptr(),
	};
	XrResult result = xrSyncActions( session, &syncInfo );
	XR_CheckResult( result, "syncing actions", instance, false );
}

void OpenXRInput::UpdateInput( int axis[6], idList<padActionChange_t> &actionChanges, poseInput_t &poseInput, XrSpace referenceSpace, XrTime time ) {
	if ( !vr_useMotionControllers.GetBool() ) {
		return;
	}

	if ( sessLocal.guiActive || console->Active() || gameLocal.GetLocalPlayer()->ActiveGui() ) {
		HandleMenuInput( referenceSpace, time, actionChanges );
		return;
	}
	ReleaseMenuClick();

	idList<XrActiveActionSet> activeActionSets {
		{ ingameActionSet, XR_NULL_PATH },
	};
	SyncActions( activeActionSets );

	float forward = GetFloat( XR_FORWARD ).second;
	float side = GetFloat( XR_SIDE ).second;
	float yaw = GetFloat( XR_YAW ).second;
	float pitch = GetFloat( XR_PITCH ).second;
	axis[AXIS_SIDE] = 127.f * side;
	axis[AXIS_FORWARD] = 127.f * forward;
	axis[AXIS_YAW] = 127.f * yaw;
	axis[AXIS_PITCH] = 127.f * pitch;

	if ( vr_inputWalkHeadRelative.GetBool() ) {
		XrSpaceLocation location { XR_TYPE_SPACE_LOCATION, nullptr };
		XrResult result = xrLocateSpace( hmdSpace, referenceSpace, time, &location );
		XR_CheckResult( result, "getting HMD space", instance, false );
		poseInput.movementAxis = QuatFromXr( location.pose.orientation );
	} else {
		poseInput.movementAxis = QuatFromXr( GetPose( XR_MOVE_DIR_HAND, referenceSpace, time ).second.orientation );
	}

	auto frobHandState = GetPose( XR_AIM, referenceSpace, time );
	poseInput.frobHandAxis = QuatFromXr( frobHandState.second.orientation );
	poseInput.frobHandPos = Vec3FromXr( frobHandState.second.position );

	// head and both hands for the VR hands in game
	{
		XrSpaceLocation headLocation { XR_TYPE_SPACE_LOCATION, nullptr };
		XrResult result = xrLocateSpace( hmdSpace, referenceSpace, time, &headLocation );
		if ( XR_SUCCEEDED( result ) ) {
			poseInput.headPos = Vec3FromXr( headLocation.pose.position );
			poseInput.headAxis = QuatFromXr( headLocation.pose.orientation );
			poseInput.headTracked = ( headLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) != 0;
		}
		if ( stageSpace != nullptr ) {
			XrSpaceLocation stageLocation { XR_TYPE_SPACE_LOCATION, nullptr };
			result = xrLocateSpace( hmdSpace, stageSpace, time, &stageLocation );
			if ( XR_SUCCEEDED( result ) && ( stageLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) ) {
				poseInput.headFloorValid = true;
				poseInput.headFloorZ = Vec3FromXr( stageLocation.pose.position ).z;
			}
		}
	}
	for ( int h = 0; h < 2; ++h ) {
		XrPosef pose { {0, 0, 0, 1}, {0, 0, 0} };
		XrVector3f linVel { 0, 0, 0 }, angVel { 0, 0, 0 };
		poseInput.handTracked[h] = GetPoseAndVelocity( XR_HAND_POSE, referenceSpace, time, handPaths[h], pose, linVel, angVel );
		poseInput.handPos[h] = Vec3FromXr( pose.position );
		poseInput.handAxis[h] = QuatFromXr( pose.orientation );
		poseInput.handLinVel[h] = Vec3FromXr( linVel );
		// same axis mapping as positions (a proper rotation), but no unit scale for angular velocity
		poseInput.handAngVel[h] = idVec3( -angVel.z, -angVel.x, angVel.y );
		poseInput.handGrip[h] = GetFloat( XR_HAND_GRIP, handPaths[h] ).second;
		poseInput.handTrigger[h] = GetFloat( XR_HAND_TRIGGER, handPaths[h] ).second;
	}
	poseInput.buttons = ( GetBool( XR_POUCH ).second ? 1 : 0 ) | ( GetBool( XR_ARROWS ).second ? 2 : 0 );
	poseInput.turnStickY = GetFloat( XR_TURN_Y ).second;

	auto menuState = GetBool( XR_MENU_OPEN );
	if ( menuState.first ) {
		actionChanges.AddGrow( { UB_NONE, "escape", menuState.second } );
	}
	SendButtonLevel( XR_INVENTORY_OPEN, UB_INVENTORY_GRID, actionChanges );
	SendButtonLevel( XR_JUMP, UB_UP, actionChanges );
	SendButtonLevel( XR_FROB, UB_FROB, actionChanges );
	SendButtonLevel( XR_CROUCH, UB_CROUCH, actionChanges );
	if ( isSprinting && idMath::Fabs( side ) + idMath::Fabs( forward ) <= 0.001 ) {
		actionChanges.AddGrow( { UB_SPEED, "", false } );
		isSprinting = false;
	}
	auto sprintState = GetBool( XR_SPRINT );
	if ( sprintState.first && sprintState.second && !isSprinting ) {
		actionChanges.AddGrow( { UB_SPEED, "", true } );
		isSprinting = true;
	}

	if (fakeAttackPressed) {
		common->Printf("Releasing attack\n");
		// fixme: adjust once we have actual attack handling
		actionChanges.AddGrow( { UB_ATTACK, "", false } );
		fakeAttackPressed = false;
	}

	SendButtonLevel( XR_ATTACK, UB_ATTACK, actionChanges );

	SendButtonLevel( XR_PARRY, UB_PARRY_MANIPULATE, actionChanges );

	SendButtonLevel( XR_USE_ITEM, UB_INVENTORY_USE, actionChanges );

	SendButtonLevel( XR_DROP_ITEM, UB_INVENTORY_DROP, actionChanges );

	// the lantern, the vanilla way (the same command the keyboard binding uses; nothing happens without one)
	auto lanternState = GetBool( XR_LANTERN );
	if ( lanternState.first ) {
		actionChanges.AddGrow( { UB_NONE, lanternState.second ? "inventory_use Lantern" : "", lanternState.second } );
	}

	ClearInputState();
}

namespace {
	// game buttons kept by level (see OpenXRInput::sentDown)
	struct levelButton_t { OpenXRInput::Action action; int button; };
	const levelButton_t LEVEL_BUTTONS[] = {
		{ OpenXRInput::XR_INVENTORY_OPEN, UB_INVENTORY_GRID },
		{ OpenXRInput::XR_JUMP, UB_UP },
		{ OpenXRInput::XR_FROB, UB_FROB },
		{ OpenXRInput::XR_CROUCH, UB_CROUCH },
		{ OpenXRInput::XR_ATTACK, UB_ATTACK },
		{ OpenXRInput::XR_PARRY, UB_PARRY_MANIPULATE },
		{ OpenXRInput::XR_USE_ITEM, UB_INVENTORY_USE },
		{ OpenXRInput::XR_DROP_ITEM, UB_INVENTORY_DROP },
	};
}

void OpenXRInput::SendButtonLevel( Action action, int button, idList<padActionChange_t> &actionChanges ) {
	// The game counts presses and releases. Telling it about changes only (changedSinceLastSync) lost one now and
	// then (ClearInputState syncs again, and a change in between was swallowed), and a lost release left the button
	// held for good: jump stopped working, and a panel that zeroes the jump (pouch, arrow selector) made the
	// player jump when it closed. So the button's actual state is compared with what the game was last told.
	const bool down = GetBool( action ).second;
	if ( down != sentDown[action] ) {
		actionChanges.AddGrow( { button, "", down } );
		sentDown[action] = down;
	}
}

void OpenXRInput::ReleaseHeldButtons( idList<padActionChange_t> &actionChanges ) {
	for ( const levelButton_t &b : LEVEL_BUTTONS ) {
		if ( sentDown[b.action] ) {
			actionChanges.AddGrow( { b.button, "", false } );
			sentDown[b.action] = false;
		}
	}
}

namespace {
	// the aim pose's forward (-z) in the space it is given in (XR axes)
	idVec3 XrPoseForward( const XrPosef &pose ) {
		const XrQuaternionf &q = pose.orientation;
		return idVec3( -2.0f * ( q.x * q.z + q.w * q.y ), -2.0f * ( q.y * q.z - q.w * q.x ), -( 1.0f - 2.0f * ( q.x * q.x + q.y * q.y ) ) );
	}

	idVec2 UiOverlaySize() {
		return idVec2( vr_uiOverlayAspect.GetFloat(), 1 ) * vr_uiOverlayHeight.GetFloat();
	}
}

idCVar vr_menuPressThreshold( "vr_menuPressThreshold", "0.7", CVAR_GAME | CVAR_FLOAT, "VR menu: trigger value that clicks" );
idCVar vr_menuReleaseThreshold( "vr_menuReleaseThreshold", "0.3", CVAR_GAME | CVAR_FLOAT, "VR menu: trigger value that lets go again (below the click value, so a trigger resting near one value doesn't flicker)" );
idCVar vr_menuClickDebounce( "vr_menuClickDebounce", "120", CVAR_GAME | CVAR_INTEGER, "VR menu: minimum time (ms) between a change of the click and the next one" );
idCVar vr_menuPointerSmoothing( "vr_menuPointerSmoothing", "0.06", CVAR_GAME | CVAR_FLOAT, "VR menu: how long (s) the cursor takes to follow the laser (steadies the hand); 0 = off" );
idCVar vr_menuPointerHoldSmoothing( "vr_menuPointerHoldSmoothing", "0.15", CVAR_GAME | CVAR_FLOAT, "VR menu: the same while the trigger is held (dragging sliders steadily)" );

namespace {
	vrMenuPointer_t menuPointer;
}

const vrMenuPointer_t &VR_GetMenuPointer() {
	return menuPointer;
}

void OpenXRInput::ReleaseMenuClick() {
	// a mouse button held down in a menu must never stay down (sliders kept dragging)
	if ( menuClickDown ) {
		sysEvent_t clickEvent = sys->GenerateMouseButtonEvent( 1, false );
		Sys_QueEvent( 0, clickEvent.evType, clickEvent.evValue, clickEvent.evValue2, clickEvent.evPtrLength, clickEvent.evPtr );
		menuClickDown = false;
	}
	menuPointer.valid = false;
	menuPointer.pressed = false;
	smoothedIntersect.Set( -1, -1 );
}

/*
 * Menus with the controllers: a laser from the pointing hand to the menu panel moves the cursor, its trigger clicks.
 *  - the click comes from the trigger's analog value with hysteresis and a debounce time: the boolean action with
 *    the runtime's threshold flickered for a trigger resting near it, sending bursts of clicks (tutorial pages and
 *    briefings were skipped)
 *  - the button state is sent by level and let go of whenever the menu loses the controllers or the GUI goes
 *    away, so a slider can't keep dragging
 *  - the cursor follows the laser smoothed (hand shake), more while the trigger is held, and stays put in the frame
 *    the trigger goes down (pulling the trigger moves the controller a little)
 */
void OpenXRInput::HandleMenuInput( XrSpace referenceSpace, XrTime time, idList<padActionChange_t> &actionChanges ) {
	// nothing stays held in the game while a menu has the controllers
	ReleaseHeldButtons( actionChanges );

	idList<XrActiveActionSet> activeActionSets {
		{ menuActionSet, XR_NULL_PATH },
	};
	SyncActions( activeActionSets );

	const int now = Sys_Milliseconds();
	const float dt = idMath::ClampFloat( 0.0f, 0.1f, ( now - lastMenuInputTime ) * 0.001f );
	lastMenuInputTime = now;

	idPlayer *player = gameLocal.GetLocalPlayer();
	idUserInterface* gui = sessLocal.guiActive;
	if ( !gui && player ) {
		gui = player->ActiveGui();
	}

	const float trigger = GetFloat( XR_MENU_CLICK, handPaths[activeMenuHand] ).second;
	bool wantDown = menuClickDown;
	if ( menuNeedsRelease ) {
		// this hand just became the pointing hand by its pull: no click until it lets go
		if ( trigger < vr_menuReleaseThreshold.GetFloat() ) {
			menuNeedsRelease = false;
		}
	} else if ( !menuClickDown && trigger > vr_menuPressThreshold.GetFloat() ) {
		wantDown = true;
	} else if ( menuClickDown && trigger < vr_menuReleaseThreshold.GetFloat() ) {
		wantDown = false;
	}
	const bool clickChanges = gui && wantDown != menuClickDown && now - menuClickChangeTime >= vr_menuClickDebounce.GetInteger();

	auto handAimState = GetPose( XR_MENU_AIM, referenceSpace, time, handPaths[activeMenuHand] );
	menuPointer.valid = handAimState.first && gui != nullptr;
	if ( handAimState.first && gui ) {
		const idVec2 target = FindGuiOverlayIntersection( handAimState.second );
		const bool onPanel = target.x >= 0.0f && target.x <= 1.0f && target.y >= 0.0f && target.y <= 1.0f;
		if ( smoothedIntersect.x < -0.5f || !onPanel ) {
			smoothedIntersect = target;
		} else {
			const float tau = menuClickDown ? vr_menuPointerHoldSmoothing.GetFloat() : vr_menuPointerSmoothing.GetFloat();
			const float follow = tau > 0.0f ? 1.0f - idMath::Exp( -dt / tau ) : 1.0f;
			smoothedIntersect += ( target - smoothedIntersect ) * follow;
		}
		const idVec2 uiIntersect = smoothedIntersect;
		if ( ( uiIntersect - curOverlayIntersect ).LengthSqr() > 0.000004f && !( clickChanges && wantDown ) ) {
			curOverlayIntersect = uiIntersect;
			sysEvent_t moveEvent = sys->GenerateMouseMoveEvent( 0, 0 );
			gui->SetCursor( 640 * uiIntersect.x, 480 * uiIntersect.y );
			if ( gui == sessLocal.guiActive ) {
				Sys_QueEvent( 0, moveEvent.evType, moveEvent.evValue, moveEvent.evValue2, moveEvent.evPtrLength, moveEvent.evPtr );
			} else if ( player ) {
				auto command = gui->HandleEvent( &moveEvent, 0 );
				player->HandleGuiCommands( player, command );
			}
		}

		// for drawing the laser: from the controller to the cursor on the panel, or a short way if it points elsewhere
		const XrPosef &pose = handAimState.second;
		const idVec3 o( pose.position.x, pose.position.y, pose.position.z );
		idVec3 e = o + XrPoseForward( pose );
		if ( onPanel ) {
			const XrVector3f centre = VR_UiOverlayPosition();
			const idVec2 size = UiOverlaySize();
			e.Set( centre.x + ( uiIntersect.x - 0.5f ) * size.x, centre.y + ( 0.5f - uiIntersect.y ) * size.y, centre.z );
		}
		menuPointer.start = Vec3FromXr( XrVector3f{ o.x, o.y, o.z } );
		menuPointer.end = Vec3FromXr( XrVector3f{ e.x, e.y, e.z } );
		menuPointer.onPanel = onPanel;
	}

	if ( clickChanges ) {
		menuClickDown = wantDown;
		menuClickChangeTime = now;
		if ( gui == sessLocal.guiActive ) {
			sysEvent_t clickEvent = sys->GenerateMouseButtonEvent( 1, menuClickDown );
			Sys_QueEvent( 0, clickEvent.evType, clickEvent.evValue, clickEvent.evValue2, clickEvent.evPtrLength, clickEvent.evPtr );
		} else {
			// hack: needed to pass the mission start dialogue
			actionChanges.AddGrow( { UB_ATTACK, "", menuClickDown } );
			fakeAttackPressed = true;
		}
	} else if ( !gui && menuClickDown ) {
		ReleaseMenuClick();
	}
	menuPointer.pressed = menuClickDown;

	// the other hand's trigger makes it the pointing hand (not while this one holds a click; that pull doesn't click)
	const float altTrigger = GetFloat( XR_MENU_CLICK, handPaths[!activeMenuHand] ).second;
	const bool altDown = altClickDown ? altTrigger > vr_menuReleaseThreshold.GetFloat() : altTrigger > vr_menuPressThreshold.GetFloat();
	if ( altDown && !altClickDown && !menuClickDown ) {
		activeMenuHand = !activeMenuHand;
		smoothedIntersect.Set( -1, -1 );
		menuNeedsRelease = true;
		altClickDown = false;
	} else {
		altClickDown = altDown;
	}

	auto menuEscapeState = GetBool( XR_MENU_ESCAPE );
	if ( menuEscapeState.first && gui ) {
		actionChanges.AddGrow( { UB_NONE, "escape", menuEscapeState.second } );
	}

	ClearInputState();
}

idVec2 OpenXRInput::FindGuiOverlayIntersection( XrPosef pointerPose ) {
	// the panel faces the player (no rotation), centred at VR_UiOverlayPosition
	const XrVector3f centre = VR_UiOverlayPosition();
	guiOverlayPose = { {0, 0, 0, 1}, centre };
	const idVec3 o( pointerPose.position.x, pointerPose.position.y, pointerPose.position.z );
	const idVec3 dir = XrPoseForward( pointerPose );
	if ( dir.z > -1e-4f ) {
		return idVec2( -1, -1 );
	}
	const float t = ( centre.z - o.z ) / dir.z;
	if ( t < 0 ) {
		return idVec2( -1, -1 );
	}
	const idVec2 size = UiOverlaySize();
	const idVec3 hit = o + dir * t;
	return idVec2( ( hit.x - centre.x ) / size.x + 0.5f, 0.5f - ( hit.y - centre.y ) / size.y );
}

void OpenXRInput::ClearInputState() {
	// hack: retrieve all boolean input action states so that the "change since last sync" is reset
	// without this, buttons that are mapped to different action sets can immediately activate
	// when switching between them, which is generally not what we want

	idList<XrActiveActionSet> activeActionSets {
		{ ingameActionSet, XR_NULL_PATH },
		{ menuActionSet, XR_NULL_PATH },
	};
	SyncActions( activeActionSets );

	GetBool( XR_SPRINT );
	GetBool( XR_CROUCH );
	GetBool( XR_JUMP );
	GetBool( XR_FROB );
	GetBool( XR_ATTACK );
	GetBool( XR_PARRY );
	GetBool( XR_MENU_OPEN );
	GetBool( XR_INVENTORY_OPEN );
	GetBool( XR_USE_ITEM );
	GetBool( XR_DROP_ITEM );
	GetBool( XR_LANTERN );
	GetFloat( XR_MENU_CLICK );
	GetBool( XR_MENU_ESCAPE );
}

XrSpace OpenXRInput::FindActionSpace( Action action, XrPath subPath ) {
	for ( const auto &actionSpace : actionSpaces ) {
		if ( actionSpace.action == action && actionSpace.subPath == subPath ) {
			return actionSpace.space;
		}
	}
	return nullptr;
}

std::pair<bool, bool> OpenXRInput::GetBool( Action action, XrPath subPath ) {
	XrActionStateBoolean state { XR_TYPE_ACTION_STATE_BOOLEAN, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], subPath };
	XrResult result = xrGetActionStateBoolean( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	return { state.changedSinceLastSync, state.currentState };
}

std::pair<bool, float> OpenXRInput::GetFloat( Action action, XrPath subPath ) {
	XrActionStateFloat state { XR_TYPE_ACTION_STATE_FLOAT, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], subPath };
	XrResult result = xrGetActionStateFloat( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	return { state.changedSinceLastSync, state.currentState };
}

std::pair<bool, idVec2> OpenXRInput::GetVec2( Action action, XrPath subPath ) {
	XrActionStateVector2f state { XR_TYPE_ACTION_STATE_VECTOR2F, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], subPath };
	XrResult result = xrGetActionStateVector2f( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	return { state.changedSinceLastSync, idVec2(state.currentState.x, state.currentState.y) };
}

std::pair<bool, XrPosef> OpenXRInput::GetPose( Action action, XrSpace referenceSpace, XrTime time, XrPath subPath ) {
	XrActionStatePose state { XR_TYPE_ACTION_STATE_POSE, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], subPath };
	XrResult result = xrGetActionStatePose( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );

	XrSpaceLocation location { XR_TYPE_SPACE_LOCATION, nullptr };
	result = xrLocateSpace( FindActionSpace( action, subPath ), referenceSpace, time, &location );
	XR_CheckResult( result, "getting action space", instance, false );
	return { state.isActive, location.pose };
}

bool OpenXRInput::GetPoseAndVelocity( Action action, XrSpace referenceSpace, XrTime time, XrPath subPath, XrPosef &pose, XrVector3f &linVel, XrVector3f &angVel ) {
	XrActionStatePose state { XR_TYPE_ACTION_STATE_POSE, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], subPath };
	XrResult result = xrGetActionStatePose( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	if ( XR_FAILED( result ) || !state.isActive ) {
		return false;
	}

	XrSpaceVelocity velocity { XR_TYPE_SPACE_VELOCITY, nullptr };
	XrSpaceLocation location { XR_TYPE_SPACE_LOCATION, &velocity };
	result = xrLocateSpace( FindActionSpace( action, subPath ), referenceSpace, time, &location );
	XR_CheckResult( result, "getting action space", instance, false );
	if ( XR_FAILED( result ) ) {
		return false;
	}
	if ( !( location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) ) {
		return false;
	}
	pose = location.pose;
	if ( velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT ) {
		linVel = velocity.linearVelocity;
	}
	if ( velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT ) {
		angVel = velocity.angularVelocity;
	}
	return true;
}
