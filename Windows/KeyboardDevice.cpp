#include "base/NativeApp.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "util/const_map.h"
#include "KeyMap.h"
#include "ControlMapping.h"
#include "Windows/WndMainWindow.h"
#include "KeyboardDevice.h"
#include "../Common/CommonTypes.h"
#include "../Core/HLE/sceCtrl.h"
#include "WinUser.h"

// TODO: remove. almost not used.
static unsigned int key_pad_map[] = {
	VK_ESCAPE,PAD_BUTTON_MENU,        // Open PauseScreen
	VK_BACK,  PAD_BUTTON_BACK,        // Toggle PauseScreen & Back Setting Page
	VK_F3,    PAD_BUTTON_LEFT_THUMB,  // Toggle Turbo
	VK_PAUSE, PAD_BUTTON_RIGHT_THUMB, // Open PauseScreen
};

// TODO: More keys need to be added, but this is more than
// a fair start.
std::map<int, int> windowsTransTable = InitConstMap<int, int>
	('A', KEYCODE_A)
	('B', KEYCODE_B)
	('C', KEYCODE_C)
	('D', KEYCODE_D)
	('E', KEYCODE_E)
	('F', KEYCODE_F)
	('G', KEYCODE_G)
	('H', KEYCODE_H)
	('I', KEYCODE_I)
	('J', KEYCODE_J)
	('K', KEYCODE_K)
	('L', KEYCODE_L)
	('M', KEYCODE_M)
	('N', KEYCODE_N)
	('O', KEYCODE_O)
	('P', KEYCODE_P)
	('Q', KEYCODE_Q)
	('R', KEYCODE_R)
	('S', KEYCODE_S)
	('T', KEYCODE_T)
	('U', KEYCODE_U)
	('V', KEYCODE_V)
	('W', KEYCODE_W)
	('X', KEYCODE_X)
	('Y', KEYCODE_Y)
	('Z', KEYCODE_Z)
	('0', KEYCODE_0)
	('1', KEYCODE_1)
	('2', KEYCODE_2)
	('3', KEYCODE_3)
	('4', KEYCODE_4)
	('5', KEYCODE_5)
	('6', KEYCODE_6)
	('7', KEYCODE_7)
	('8', KEYCODE_8)
	('9', KEYCODE_9)
	(VK_NUMPAD0, KEYCODE_NUMPAD_0)
	(VK_NUMPAD1, KEYCODE_NUMPAD_1)
	(VK_NUMPAD2, KEYCODE_NUMPAD_2)
	(VK_NUMPAD3, KEYCODE_NUMPAD_3)
	(VK_NUMPAD4, KEYCODE_NUMPAD_4)
	(VK_NUMPAD5, KEYCODE_NUMPAD_5)
	(VK_NUMPAD6, KEYCODE_NUMPAD_6)
	(VK_NUMPAD7, KEYCODE_NUMPAD_7)
	(VK_NUMPAD8, KEYCODE_NUMPAD_8)
	(VK_NUMPAD9, KEYCODE_NUMPAD_9)
	(VK_DIVIDE, KEYCODE_NUMPAD_DIVIDE)
	(VK_MULTIPLY, KEYCODE_NUMPAD_MULTIPLY)
	(VK_SUBTRACT, KEYCODE_NUMPAD_SUBTRACT)
	(VK_ADD, KEYCODE_NUMPAD_ADD)
	(VK_SEPARATOR, KEYCODE_NUMPAD_COMMA)
	(VK_OEM_MINUS, KEYCODE_MINUS)
	(VK_OEM_PLUS, KEYCODE_PLUS)
	(VK_LCONTROL, KEYCODE_CTRL_LEFT)
	(VK_RCONTROL, KEYCODE_CTRL_RIGHT)
	(VK_LSHIFT, KEYCODE_SHIFT_LEFT)
	(VK_RSHIFT, KEYCODE_SHIFT_RIGHT)
	(VK_LMENU, KEYCODE_ALT_LEFT)
	(VK_RMENU, KEYCODE_ALT_RIGHT)
	(VK_BACK, KEYCODE_BACK)
	(VK_SPACE, KEYCODE_SPACE)
	(VK_ESCAPE, KEYCODE_ESCAPE)
	(VK_UP, KEYCODE_DPAD_UP)
	(VK_INSERT, KEYCODE_INSERT)
	(VK_HOME, KEYCODE_HOME)
	(VK_PRIOR, KEYCODE_PAGE_UP)
	(VK_NEXT, KEYCODE_PAGE_DOWN)
	(VK_DELETE, KEYCODE_DEL)
	(VK_END, KEYCODE_MOVE_END)
	(VK_TAB, KEYCODE_TAB)
	(VK_DOWN, KEYCODE_DPAD_DOWN)
	(VK_LEFT, KEYCODE_DPAD_LEFT)
	(VK_RIGHT, KEYCODE_DPAD_RIGHT)
	(VK_CAPITAL, KEYCODE_CAPS_LOCK)
	(VK_CLEAR, KEYCODE_CLEAR)
	(VK_PRINT, KEYCODE_SYSRQ)
	(VK_SCROLL, KEYCODE_SCROLL_LOCK)
	(VK_OEM_1, KEYCODE_SEMICOLON)
	(VK_OEM_2, KEYCODE_SLASH)
	(VK_OEM_4, KEYCODE_LEFT_BRACKET)
	(VK_OEM_6, KEYCODE_RIGHT_BRACKET)
	(VK_MENU, KEYCODE_MENU);

int KeyboardDevice::UpdateState(InputState &input_state) {
	if (MainWindow::GetHWND() != GetForegroundWindow()) return -1;

	// This button isn't customizable.  Also, if alt is held, we ignore it (alt-tab is common.)
	if (GetAsyncKeyState(VK_TAB) && !GetAsyncKeyState(VK_MENU)) {
		input_state.pad_buttons |= PAD_BUTTON_UNTHROTTLE;
	}

	// TODO: remove
	for (int i = 0; i < sizeof(key_pad_map)/sizeof(key_pad_map[0]); i += 2) {
		if (!GetAsyncKeyState(key_pad_map[i])) {
			continue;
		}

		switch (key_pad_map[i + 1]) {
			case PAD_BUTTON_MENU:
			case PAD_BUTTON_BACK:
			case PAD_BUTTON_LEFT_THUMB:
			case PAD_BUTTON_RIGHT_THUMB:
				input_state.pad_buttons |= key_pad_map[i + 1];
				break;
		}
	}
	
	return 0;
}
