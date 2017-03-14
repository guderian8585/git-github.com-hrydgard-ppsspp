#include "SDL/SDLJoystick.h"
#include "Core/Config.h"
#include "Common/FileUtil.h"

#include <iostream>
#include <string>
#include <algorithm>

using namespace std;

static int SDLJoystickEventHandlerWrapper(void* userdata, SDL_Event* event)
{
	static_cast<SDLJoystick *>(userdata)->ProcessInput(*event);
	return 0;
}

SDLJoystick::SDLJoystick(bool init_SDL ) : registeredAsEventHandler(false) {
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (init_SDL) {
		SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
	}
	
	char* dbEnvPath = getenv("PPSSPP_GAME_CONTROLLER_DB_PATH");
	if (dbEnvPath != NULL) {
		if (!File::Exists(dbEnvPath)) {
			cout << "WARNING! " << dbEnvPath << " does not exist!" << endl;
		} else {
			cout << "loading control pad mappings from " << dbEnvPath << ": ";
			if (SDL_GameControllerAddMappingsFromFile(dbEnvPath) == -1) {
				cout << "FAILED! Will try load from your assests directory instead..." << endl;
			} else {
				cout << "SUCCESS!" << endl;
				setUpControllers();
				return;
			}
		}
	}
		
	auto dbPath = File::GetExeDirectory() + "assets/gamecontrollerdb.txt";
	cout << "loading control pad mappings from " << dbPath << ": ";

	if (SDL_GameControllerAddMappingsFromFile(dbPath.c_str()) == -1) {
		cout << "FAILED! Please place gamecontrollerdb.txt in your assets directory." << endl;
		return;
	}
	
	cout << "SUCCESS!" << endl;
	setUpControllers();
}

void SDLJoystick::setUpControllers() {
	int numjoys = SDL_NumJoysticks();
	for (int i = 0; i < numjoys; i++) {
		setUpController(i);
	}
	if (controllers.size() > 0) {
		cout << "pad 1 has been assigned to control pad: " << SDL_GameControllerName(controllers.front()) << endl;
	}
}

void SDLJoystick::setUpController(int deviceIndex) {
	if (SDL_IsGameController(deviceIndex)) {
		SDL_GameController *controller = SDL_GameControllerOpen(deviceIndex);
		if (controller) {
			if (SDL_GameControllerGetAttached(controller)) {
				controllers.push_back(controller);
				controllerDeviceMap[SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))] = deviceIndex;
				cout << "found control pad: " << SDL_GameControllerName(controller) << ", loading mapping: ";
				auto mapping = SDL_GameControllerMapping(controller);
				if (mapping == NULL) {
					cout << "FAILED" << endl;
				} else {
					cout << "SUCCESS, mapping is:" << endl << mapping << endl;
				}
			} else {
				SDL_GameControllerClose(controller);
			}
		}
	}
}

SDLJoystick::~SDLJoystick() {
	if (registeredAsEventHandler) {
		SDL_DelEventWatch(SDLJoystickEventHandlerWrapper, this);
	}
	for (auto & controller : controllers) {
		SDL_GameControllerClose(controller);
	}
}

void SDLJoystick::registerEventHandler() {
	SDL_AddEventWatch(SDLJoystickEventHandlerWrapper, this);
	registeredAsEventHandler = true;
}

keycode_t SDLJoystick::getKeycodeForButton(SDL_GameControllerButton button) {
	switch (button) {
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		return NKCODE_DPAD_UP;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		return NKCODE_DPAD_DOWN;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		return NKCODE_DPAD_LEFT;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		return NKCODE_DPAD_RIGHT;
	case SDL_CONTROLLER_BUTTON_A:
		return NKCODE_BUTTON_2;
	case SDL_CONTROLLER_BUTTON_B:
		return NKCODE_BUTTON_3;
	case SDL_CONTROLLER_BUTTON_X:
		return NKCODE_BUTTON_4;
	case SDL_CONTROLLER_BUTTON_Y:
		return NKCODE_BUTTON_1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		return NKCODE_BUTTON_5;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		return NKCODE_BUTTON_6;
	case SDL_CONTROLLER_BUTTON_START:
		return NKCODE_BUTTON_10;
	case SDL_CONTROLLER_BUTTON_BACK:
		return NKCODE_BUTTON_9; // select button
	case SDL_CONTROLLER_BUTTON_GUIDE:
		return NKCODE_BACK; // pause menu
	case SDL_CONTROLLER_BUTTON_LEFTSTICK:
		return NKCODE_BUTTON_THUMBL;
	case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
		return NKCODE_BUTTON_THUMBR;
	}
	return NKCODE_UNKNOWN;
}

void SDLJoystick::ProcessInput(SDL_Event &event){
	switch (event.type) {
	case SDL_CONTROLLERBUTTONDOWN:
		if (event.cbutton.state == SDL_PRESSED) {
			auto code = getKeycodeForButton((SDL_GameControllerButton)event.cbutton.button);
			if (code != NKCODE_UNKNOWN) {
				KeyInput key;
				key.flags = KEY_DOWN;
				key.keyCode = code;
				key.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.cbutton.which);
				keysPressedCombo.push_back(code);
				if ( checkGuideCombo() ) {
					key.keyCode = NKCODE_BACK;
				}
				NativeKey(key);
			}
		}
		break;
	case SDL_CONTROLLERBUTTONUP:
		if (event.cbutton.state == SDL_RELEASED) {
			auto code = getKeycodeForButton((SDL_GameControllerButton)event.cbutton.button);
			if (code != NKCODE_UNKNOWN) {
				KeyInput key;
				key.flags = KEY_UP;
				key.keyCode = code;
				key.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.cbutton.which);
				keysPressedCombo.erase(std::remove(keysPressedCombo.begin(), keysPressedCombo.end(), code), keysPressedCombo.end());
				NativeKey(key);
			}
		}
		break;
	case SDL_CONTROLLERAXISMOTION:
		AxisInput axis;
		axis.axisId = event.caxis.axis;
		// 1.2 to try to approximate the PSP's clamped rectangular range.
		axis.value = 1.2 * event.caxis.value / 32767.0f;
		if (axis.value > 1.0f) axis.value = 1.0f;
		if (axis.value < -1.0f) axis.value = -1.0f;
		axis.deviceId = DEVICE_ID_PAD_0 + getDeviceIndex(event.caxis.which);
		axis.flags = 0;
		NativeAxis(axis);
		break;
	case SDL_CONTROLLERDEVICEREMOVED:
		// for removal events, "which" is the instance ID for SDL_CONTROLLERDEVICEREMOVED		
		for (auto it = controllers.begin(); it != controllers.end(); ++it) {
			if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*it)) == event.cdevice.which) {
				SDL_GameControllerClose(*it);
				controllers.erase(it);
				break;
			}
		}
		break;
	case SDL_CONTROLLERDEVICEADDED:
		// for add events, "which" is the device index!
		int prevNumControllers = controllers.size();
		setUpController(event.cdevice.which);
		if (prevNumControllers == 0 && controllers.size() > 0) {
			cout << "pad 1 has been assigned to control pad: " << SDL_GameControllerName(controllers.front()) << endl;
		}
		break;
	}
}

int SDLJoystick::getDeviceIndex(int instanceId) {
	auto it = controllerDeviceMap.find(instanceId);
	if (it == controllerDeviceMap.end()) {
			// could not find device
			return -1;
	}
	return it->second;
}

bool SDLJoystick::checkGuideCombo() {
	if (  std::find(keysPressedCombo.begin(), keysPressedCombo.end(), NKCODE_BUTTON_5) != keysPressedCombo.end() &&
	      std::find(keysPressedCombo.begin(), keysPressedCombo.end(), NKCODE_BUTTON_6) != keysPressedCombo.end() &&
	      std::find(keysPressedCombo.begin(), keysPressedCombo.end(), NKCODE_BUTTON_9) != keysPressedCombo.end() &&
	      std::find(keysPressedCombo.begin(), keysPressedCombo.end(), NKCODE_BUTTON_10) != keysPressedCombo.end() ) {
		// Found the magic combo L + R + Start + Select
		return true;
	}
	return false;
}
