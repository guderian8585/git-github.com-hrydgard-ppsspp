// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once


#include "../Config.h"

#define SCE_UTILITY_DIALOG_RESULT_SUCCESS				0
#define SCE_UTILITY_DIALOG_RESULT_CANCEL				1
#define SCE_UTILITY_DIALOG_RESULT_ABORT					2


class PSPDialog
{
public:
	PSPDialog();
	virtual ~PSPDialog();

	virtual void Update();
	virtual void Shutdown();

	enum DialogStatus
	{
		SCE_UTILITY_STATUS_NONE 		= 0,
		SCE_UTILITY_STATUS_INITIALIZE	= 1,
		SCE_UTILITY_STATUS_RUNNING 		= 2,
		SCE_UTILITY_STATUS_FINISHED 	= 3,
		SCE_UTILITY_STATUS_SHUTDOWN 	= 4
	};

	DialogStatus GetStatus();

	void StartDraw();
	void EndDraw();
protected:
	bool IsButtonPressed(int checkButton);

	DialogStatus status;

	unsigned int lastButtons;
	unsigned int buttons;
};
