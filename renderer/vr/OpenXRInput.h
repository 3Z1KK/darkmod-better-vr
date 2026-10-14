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
#include <openxr/openxr.h>
#include "framework/GamepadInput.h"

extern idCVar vr_useMotionControllers;
extern idCVar vr_inputSnapTurnInterval;
extern idCVar vr_smoothTurnSpeed;

struct poseInput_t {
	idQuat movementAxis;
	idVec3 frobHandPos;
	idQuat frobHandAxis;

	// VR hands (0 = left, 1 = right) and head, in game units/axes in the headset's local space
	idVec3 headPos;
	idQuat headAxis;
	idVec3 handPos[2];
	idQuat handAxis[2];
	float handGrip[2];
	float handTrigger[2];
	bool handTracked[2];
};

class OpenXRInput {
public:
	void Init(XrInstance instance, XrSession session);
	void Destroy();

	void UpdateInput( int axis[6], idList<padActionChange_t> &actionChanges, poseInput_t &poseInput, XrSpace referenceSpace, XrTime time );

	enum Action {
		XR_FORWARD = 0,
		XR_SIDE,
		XR_MOVE_DIR_HAND,
		XR_YAW,
		XR_PITCH,
		XR_SPRINT,
		XR_CROUCH,
		XR_JUMP,
		XR_FROB,
		XR_ATTACK,
		XR_PARRY,
		XR_MENU_OPEN,
		XR_INVENTORY_OPEN,
		XR_AIM,
		XR_USE_ITEM,
		XR_DROP_ITEM,

		XR_MENU_AIM,
		XR_MENU_CLICK,
		XR_MENU_ESCAPE,
		XR_HAND_POSE,
		XR_HAND_GRIP,
		XR_HAND_TRIGGER,

		XR_NUM_ACTIONS,
		XR_INVALID = -1,
	};
private:
	XrInstance instance = nullptr;
	XrSession session = nullptr;
	XrSpace hmdSpace = nullptr;
	XrTime refActionTime = 0;

	struct actionSpace_t {
		Action action;
		XrPath subPath;
		XrSpace space;
	};
	XrPath handPaths[2] = { XR_NULL_PATH };
	XrAction actions[XR_NUM_ACTIONS] = { nullptr };
	idList<actionSpace_t> actionSpaces;
	XrActionSet ingameActionSet = nullptr;
	XrActionSet menuActionSet = nullptr;

	bool isSprinting = false;

	XrPosef guiOverlayPose;
	idVec2 curOverlayIntersect;
	int activeMenuHand = 0;
	bool fakeAttackPressed = false;

	XrActionSet CreateActionSet( const idStr &name, uint32_t priority = 0 );
	void CreateAction( XrActionSet actionSet, Action action, XrActionType actionType, uint32_t numSubPaths = 0, XrPath *subPaths = nullptr );
	XrSpace CreateActionSpace( Action action, XrPath subPath );
	void CreateAllActions();
	void LoadSuggestedBindings();
	void RegisterSuggestedBindings();
	void AttachActionSets();
	idStr ApplyDominantHandToActionPath( const idStr &profile, const idStr &path );
	void SyncActions( const idList<XrActiveActionSet> &activeSets );

	void HandleMenuInput( XrSpace referenceSpace, XrTime time, idList<padActionChange_t> &actionChanges );
	idVec2 FindGuiOverlayIntersection( XrPosef pointerPose );

	void ClearInputState();

	XrSpace FindActionSpace( Action action, XrPath subPath = XR_NULL_PATH );
	std::pair<bool, bool> GetBool( Action action, XrPath subPath = XR_NULL_PATH );
	std::pair<bool, float> GetFloat( Action action, XrPath subPath = XR_NULL_PATH );
	std::pair<bool, idVec2> GetVec2( Action action, XrPath subPath = XR_NULL_PATH );
	std::pair<bool, XrPosef> GetPose( Action action, XrSpace referenceSpace, XrTime time, XrPath subPath = XR_NULL_PATH );
};

