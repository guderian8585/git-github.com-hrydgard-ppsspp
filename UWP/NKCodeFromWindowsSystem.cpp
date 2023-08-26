#include "pch.h"
#include "NKCodeFromWindowsSystem.h"

using namespace Windows::System;

std::map<Windows::System::VirtualKey, InputKeyCode> virtualKeyCodeToNKCode{
	{ VirtualKey::A, NKCODE_A },
	{ VirtualKey::B, NKCODE_B },
	{ VirtualKey::C, NKCODE_C },
	{ VirtualKey::D, NKCODE_D },
	{ VirtualKey::E, NKCODE_E },
	{ VirtualKey::F, NKCODE_F },
	{ VirtualKey::G, NKCODE_G },
	{ VirtualKey::H, NKCODE_H },
	{ VirtualKey::I, NKCODE_I },
	{ VirtualKey::J, NKCODE_J },
	{ VirtualKey::K, NKCODE_K },
	{ VirtualKey::L, NKCODE_L },
	{ VirtualKey::M, NKCODE_M },
	{ VirtualKey::N, NKCODE_N },
	{ VirtualKey::O, NKCODE_O },
	{ VirtualKey::P, NKCODE_P },
	{ VirtualKey::Q, NKCODE_Q },
	{ VirtualKey::R, NKCODE_R },
	{ VirtualKey::S, NKCODE_S },
	{ VirtualKey::T, NKCODE_T },
	{ VirtualKey::U, NKCODE_U },
	{ VirtualKey::V, NKCODE_V },
	{ VirtualKey::W, NKCODE_W },
	{ VirtualKey::X, NKCODE_X },
	{ VirtualKey::Y, NKCODE_Y },
	{ VirtualKey::Z, NKCODE_Z },
	{ VirtualKey::Number0, NKCODE_0 },
	{ VirtualKey::Number1, NKCODE_1 },
	{ VirtualKey::Number2, NKCODE_2 },
	{ VirtualKey::Number3, NKCODE_3 },
	{ VirtualKey::Number4, NKCODE_4 },
	{ VirtualKey::Number5, NKCODE_5 },
	{ VirtualKey::Number6, NKCODE_6 },
	{ VirtualKey::Number7, NKCODE_7 },
	{ VirtualKey::Number8, NKCODE_8 },
	{ VirtualKey::Number9, NKCODE_9 },
	{ VirtualKey::Decimal, NKCODE_PERIOD },
	// { VirtualKey::Comma, NKCODE_COMMA },
	{ VirtualKey::NumberPad0, NKCODE_NUMPAD_0 },
	{ VirtualKey::NumberPad1, NKCODE_NUMPAD_1 },
	{ VirtualKey::NumberPad2, NKCODE_NUMPAD_2 },
	{ VirtualKey::NumberPad3, NKCODE_NUMPAD_3 },
	{ VirtualKey::NumberPad4, NKCODE_NUMPAD_4 },
	{ VirtualKey::NumberPad5, NKCODE_NUMPAD_5 },
	{ VirtualKey::NumberPad6, NKCODE_NUMPAD_6 },
	{ VirtualKey::NumberPad7, NKCODE_NUMPAD_7 },
	{ VirtualKey::NumberPad8, NKCODE_NUMPAD_8 },
	{ VirtualKey::NumberPad9, NKCODE_NUMPAD_9 },
	{ VirtualKey::Decimal, NKCODE_NUMPAD_DOT },
	{ VirtualKey::Divide, NKCODE_NUMPAD_DIVIDE },
	{ VirtualKey::Multiply, NKCODE_NUMPAD_MULTIPLY },
	{ VirtualKey::Subtract, NKCODE_NUMPAD_SUBTRACT },
	{ VirtualKey::Add, NKCODE_NUMPAD_ADD },
	{ VirtualKey::Separator, NKCODE_NUMPAD_COMMA },
	{ VirtualKey::LeftControl, NKCODE_CTRL_LEFT },
	{ VirtualKey::RightControl, NKCODE_CTRL_RIGHT },
	{ VirtualKey::LeftShift, NKCODE_SHIFT_LEFT },
	{ VirtualKey::RightShift, NKCODE_SHIFT_RIGHT },
	//{ VK_LMENU, NKCODE_ALT_LEFT },
	//{ VK_RMENU, NKCODE_ALT_RIGHT },
	{ VirtualKey::GoBack, NKCODE_BACK },
	{ VirtualKey::Space, NKCODE_SPACE },
	{ VirtualKey::Escape, NKCODE_ESCAPE },
	{ VirtualKey::Up, NKCODE_DPAD_UP },
	{ VirtualKey::Insert, NKCODE_INSERT },
	{ VirtualKey::Home, NKCODE_MOVE_HOME },
	{ VirtualKey::PageUp, NKCODE_PAGE_UP },
	{ VirtualKey::PageDown, NKCODE_PAGE_DOWN },
	{ VirtualKey::Delete, NKCODE_FORWARD_DEL },
	{ VirtualKey::Back, NKCODE_DEL },
	{ VirtualKey::End, NKCODE_MOVE_END },
	{ VirtualKey::Tab, NKCODE_TAB },
	{ VirtualKey::Down, NKCODE_DPAD_DOWN },
	{ VirtualKey::Left, NKCODE_DPAD_LEFT },
	{ VirtualKey::Right, NKCODE_DPAD_RIGHT },
	{ VirtualKey::CapitalLock, NKCODE_CAPS_LOCK },
	{ VirtualKey::Clear, NKCODE_CLEAR },
	// { VirtualKey::, NKCODE_SYSRQ },
	{ VirtualKey::Scroll, NKCODE_SCROLL_LOCK },
	// { , NKCODE_SEMICOLON },
	// { VK_OEM_2, NKCODE_SLASH },
	// { VK_OEM_3, NKCODE_GRAVE },
	// { VK_OEM_4, NKCODE_LEFT_BRACKET },
	// { VK_OEM_5, NKCODE_BACKSLASH },
	// { VK_OEM_6, NKCODE_RIGHT_BRACKET },
	// { VK_OEM_7, NKCODE_APOSTROPHE },
	{ VirtualKey::Enter, NKCODE_ENTER },
	// { VK_APPS, NKCODE_MENU }, // Context menu key, let's call this "menu".
	{ VirtualKey::Pause, NKCODE_BREAK },
	{ VirtualKey::F1, NKCODE_F1 },
	{ VirtualKey::F2, NKCODE_F2 },
	{ VirtualKey::F3, NKCODE_F3 },
	{ VirtualKey::F4, NKCODE_F4 },
	{ VirtualKey::F5, NKCODE_F5 },
	{ VirtualKey::F6, NKCODE_F6 },
	{ VirtualKey::F7, NKCODE_F7 },
	{ VirtualKey::F8, NKCODE_F8 },
	{ VirtualKey::F9, NKCODE_F9 },
	{ VirtualKey::F10, NKCODE_F10 },
	{ VirtualKey::F11, NKCODE_F11 },
	{ VirtualKey::F12, NKCODE_F12 },
	//{ VK_OEM_102, NKCODE_EXT_PIPE },
	//{ VK_LBUTTON, NKCODE_EXT_MOUSEBUTTON_1 },
	//{ VK_RBUTTON, NKCODE_EXT_MOUSEBUTTON_2 },;
};
