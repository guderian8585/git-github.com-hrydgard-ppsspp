#include <algorithm>

#include "Common/Math/math_util.h"
#include "Common/TimeUtil.h"
#include "Common/Log.h"

#include "Core/HLE/sceCtrl.h"
#include "Core/KeyMap.h"
#include "Core/ControlMapper.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"

static float MapAxisValue(float v) {
	const float deadzone = g_Config.fAnalogDeadzone;
	const float invDeadzone = g_Config.fAnalogInverseDeadzone;
	const float sensitivity = g_Config.fAnalogSensitivity;
	const float sign = v >= 0.0f ? 1.0f : -1.0f;

	return sign * Clamp(invDeadzone + (abs(v) - deadzone) / (1.0f - deadzone) * (sensitivity - invDeadzone), 0.0f, 1.0f);
}

void ConvertAnalogStick(float &x, float &y) {
	const bool isCircular = g_Config.bAnalogIsCircular;

	float norm = std::max(fabsf(x), fabsf(y));

	if (norm == 0.0f)
		return;

	if (isCircular) {
		float newNorm = sqrtf(x * x + y * y);
		float factor = newNorm / norm;
		x *= factor;
		y *= factor;
		norm = newNorm;
	}

	float mappedNorm = MapAxisValue(norm);
	x = Clamp(x / norm * mappedNorm, -1.0f, 1.0f);
	y = Clamp(y / norm * mappedNorm, -1.0f, 1.0f);
}

void ControlMapper::SetCallbacks(
	std::function<void(int, bool)> onVKey,
	std::function<void(uint32_t, uint32_t)> setAllPSPButtonStates,
	std::function<void(int, bool)> setPSPButtonState,
	std::function<void(int, float, float)> setPSPAnalog) {
	onVKey_ = onVKey;
	setAllPSPButtonStates_ = setAllPSPButtonStates;
	setPSPButtonState_ = setPSPButtonState;
	setPSPAnalog_ = setPSPAnalog;
}

void ControlMapper::SetRawCallback(std::function<void(int, float, float)> setRawAnalog) {
	setRawAnalog_ = setRawAnalog;
}

void ControlMapper::SetPSPAxis(int device, char axis, float value, int stick) {
	int axisId = axis == 'X' ? 0 : 1;

	float position[2];
	position[0] = history_[stick][0];
	position[1] = history_[stick][1];

	position[axisId] = value;

	float x = position[0];
	float y = position[1];

	if (setRawAnalog_) {
		setRawAnalog_(stick, x, y);
	}

	// NOTE: We need to use single-axis checks, since the other axis might be from another device,
	// so we'll add a little leeway.
	bool inDeadZone = fabsf(value) < g_Config.fAnalogDeadzone * 0.7f;

	bool ignore = false;
	if (inDeadZone && lastNonDeadzoneDeviceID_[stick] != device) {
		// Ignore this event! See issue #15465
		ignore = true;
	}

	if (!inDeadZone) {
		lastNonDeadzoneDeviceID_[stick] = device;
	}

	if (!ignore) {
		history_[stick][axisId] = value;

		float x = history_[stick][0];
		float y = history_[stick][1];

		ConvertAnalogStick(x, y);
		setPSPAnalog_(stick, x, y);
	}
}

bool ControlMapper::Key(const KeyInput &key, bool *pauseTrigger) {
	std::vector<int> pspKeys;
	KeyMap::InputMappingToPspButton(InputMapping(key.deviceId, key.keyCode), &pspKeys);

	if (pspKeys.size() && (key.flags & KEY_IS_REPEAT)) {
		// Claim that we handled this. Prevents volume key repeats from popping up the volume control on Android.
		return true;
	}

	for (size_t i = 0; i < pspKeys.size(); i++) {
		SetPSPKey(key.deviceId, pspKeys[i], key.flags);
	}

	DEBUG_LOG(SYSTEM, "Key: %d DeviceId: %d", key.keyCode, key.deviceId);

	if (!pspKeys.size() || key.deviceId == DEVICE_ID_DEFAULT) {
		if ((key.flags & KEY_DOWN) && key.keyCode == NKCODE_BACK) {
			*pauseTrigger = true;
			return true;
		}
	}

	return pspKeys.size() > 0;
}

void ControlMapper::Axis(const AxisInput &axis) {
	if (axis.value > 0) {
		ProcessAxis(axis, 1);
	} else if (axis.value < 0) {
		ProcessAxis(axis, -1);
	} else if (axis.value == 0) {
		// Both directions! Prevents sticking for digital input devices that are axises (like HAT)
		ProcessAxis(axis, 1);
		ProcessAxis(axis, -1);
	}
}

void ControlMapper::Update() {
	if (autoRotatingAnalogCW_) {
		const double now = time_now_d();
		// Clamp to a square
		float x = std::min(1.0f, std::max(-1.0f, 1.42f * (float)cos(now * -g_Config.fAnalogAutoRotSpeed)));
		float y = std::min(1.0f, std::max(-1.0f, 1.42f * (float)sin(now * -g_Config.fAnalogAutoRotSpeed)));

		setPSPAnalog_(0, x, y);
	} else if (autoRotatingAnalogCCW_) {
		const double now = time_now_d();
		float x = std::min(1.0f, std::max(-1.0f, 1.42f * (float)cos(now * g_Config.fAnalogAutoRotSpeed)));
		float y = std::min(1.0f, std::max(-1.0f, 1.42f * (float)sin(now * g_Config.fAnalogAutoRotSpeed)));

		setPSPAnalog_(0, x, y);
	}
}

inline bool IsAnalogStickKey(int key) {
	switch (key) {
	case VIRTKEY_AXIS_X_MIN:
	case VIRTKEY_AXIS_X_MAX:
	case VIRTKEY_AXIS_Y_MIN:
	case VIRTKEY_AXIS_Y_MAX:
	case VIRTKEY_AXIS_RIGHT_X_MIN:
	case VIRTKEY_AXIS_RIGHT_X_MAX:
	case VIRTKEY_AXIS_RIGHT_Y_MIN:
	case VIRTKEY_AXIS_RIGHT_Y_MAX:
		return true;
	default:
		return false;
	}
}

static int RotatePSPKeyCode(int x) {
	switch (x) {
	case CTRL_UP: return CTRL_RIGHT;
	case CTRL_RIGHT: return CTRL_DOWN;
	case CTRL_DOWN: return CTRL_LEFT;
	case CTRL_LEFT: return CTRL_UP;
	default:
		return x;
	}
}

void ControlMapper::SetVKeyAnalog(int deviceId, char axis, int stick, int virtualKeyMin, int virtualKeyMax, bool setZero) {
	// The down events can repeat, so just trust the virtKeys_ array.
	bool minDown = virtKeys_[virtualKeyMin - VIRTKEY_FIRST];
	bool maxDown = virtKeys_[virtualKeyMax - VIRTKEY_FIRST];

	const float scale = virtKeys_[VIRTKEY_ANALOG_LIGHTLY - VIRTKEY_FIRST] ? g_Config.fAnalogLimiterDeadzone : 1.0f;
	float value = 0.0f;
	if (minDown)
		value -= scale;
	if (maxDown)
		value += scale;
	if (setZero || minDown || maxDown) {
		SetPSPAxis(deviceId, axis, value, stick);
	}
}

void ControlMapper::PSPKey(int deviceId, int pspKeyCode, int flags) {
	SetPSPKey(deviceId, pspKeyCode, flags);
}

void ControlMapper::SetPSPKey(int deviceId, int pspKeyCode, int flags) {
	if (pspKeyCode >= VIRTKEY_FIRST) {
		int vk = pspKeyCode - VIRTKEY_FIRST;
		if (flags & KEY_DOWN) {
			virtKeys_[vk] = true;
			onVKey(deviceId, pspKeyCode, true);
		}
		if (flags & KEY_UP) {
			virtKeys_[vk] = false;
			onVKey(deviceId, pspKeyCode, false);
		}
	} else {
		int rotations = 0;
		switch (g_Config.iInternalScreenRotation) {
		case ROTATION_LOCKED_HORIZONTAL180:
			rotations = 2;
			break;
		case ROTATION_LOCKED_VERTICAL:
			rotations = 1;
			break;
		case ROTATION_LOCKED_VERTICAL180:
			rotations = 3;
			break;
		}

		for (int i = 0; i < rotations; i++) {
			pspKeyCode = RotatePSPKeyCode(pspKeyCode);
		}

		// INFO_LOG(SYSTEM, "pspKey %d %d", pspKeyCode, flags);
		if (flags & KEY_DOWN)
			setPSPButtonState_(pspKeyCode, true);
		if (flags & KEY_UP)
			setPSPButtonState_(pspKeyCode, false);
	}
}

void ControlMapper::onVKey(int deviceId, int vkey, bool down) {
	switch (vkey) {
	case VIRTKEY_AXIS_X_MIN:
	case VIRTKEY_AXIS_X_MAX:
		SetVKeyAnalog(deviceId, 'X', CTRL_STICK_LEFT, VIRTKEY_AXIS_X_MIN, VIRTKEY_AXIS_X_MAX);
		break;
	case VIRTKEY_AXIS_Y_MIN:
	case VIRTKEY_AXIS_Y_MAX:
		SetVKeyAnalog(deviceId, 'Y', CTRL_STICK_LEFT, VIRTKEY_AXIS_Y_MIN, VIRTKEY_AXIS_Y_MAX);
		break;

	case VIRTKEY_AXIS_RIGHT_X_MIN:
	case VIRTKEY_AXIS_RIGHT_X_MAX:
		SetVKeyAnalog(deviceId, 'X', CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_X_MIN, VIRTKEY_AXIS_RIGHT_X_MAX);
		break;
	case VIRTKEY_AXIS_RIGHT_Y_MIN:
	case VIRTKEY_AXIS_RIGHT_Y_MAX:
		SetVKeyAnalog(deviceId, 'Y', CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_Y_MIN, VIRTKEY_AXIS_RIGHT_Y_MAX);
		break;

	case VIRTKEY_ANALOG_LIGHTLY:
		SetVKeyAnalog(deviceId, 'X', CTRL_STICK_LEFT, VIRTKEY_AXIS_X_MIN, VIRTKEY_AXIS_X_MAX, false);
		SetVKeyAnalog(deviceId, 'Y', CTRL_STICK_LEFT, VIRTKEY_AXIS_Y_MIN, VIRTKEY_AXIS_Y_MAX, false);
		SetVKeyAnalog(deviceId, 'X', CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_X_MIN, VIRTKEY_AXIS_RIGHT_X_MAX, false);
		SetVKeyAnalog(deviceId, 'Y', CTRL_STICK_RIGHT, VIRTKEY_AXIS_RIGHT_Y_MIN, VIRTKEY_AXIS_RIGHT_Y_MAX, false);
		break;

	case VIRTKEY_ANALOG_ROTATE_CW:
		if (down) {
			autoRotatingAnalogCW_ = true;
			autoRotatingAnalogCCW_ = false;
		} else {
			autoRotatingAnalogCW_ = false;
			setPSPAnalog_(0, 0.0f, 0.0f);
		}
		break;
	case VIRTKEY_ANALOG_ROTATE_CCW:
		if (down) {
			autoRotatingAnalogCW_ = false;
			autoRotatingAnalogCCW_ = true;
		} else {
			autoRotatingAnalogCCW_ = false;
			setPSPAnalog_(0, 0.0f, 0.0f);
		}
		break;

	default:
		if (onVKey_)
			onVKey_(vkey, down);
		break;
	}
}

void ControlMapper::ProcessAxis(const AxisInput &axis, int direction) {
	// Sanity check
	if (axis.axisId < 0 || axis.axisId >= JOYSTICK_AXIS_MAX) {
		return;
	}

	const float scale = virtKeys_[VIRTKEY_ANALOG_LIGHTLY - VIRTKEY_FIRST] ? g_Config.fAnalogLimiterDeadzone : 1.0f;

	std::vector<int> results;
	KeyMap::InputMappingToPspButton(InputMapping(axis.deviceId, axis.axisId, direction), &results);

	for (int result : results) {
		float value = fabs(axis.value) * scale;
		switch (result) {
		case VIRTKEY_AXIS_X_MIN:
			SetPSPAxis(axis.deviceId, 'X', -value, CTRL_STICK_LEFT);
			break;
		case VIRTKEY_AXIS_X_MAX:
			SetPSPAxis(axis.deviceId, 'X', value, CTRL_STICK_LEFT);
			break;
		case VIRTKEY_AXIS_Y_MIN:
			SetPSPAxis(axis.deviceId, 'Y', -value, CTRL_STICK_LEFT);
			break;
		case VIRTKEY_AXIS_Y_MAX:
			SetPSPAxis(axis.deviceId, 'Y', value, CTRL_STICK_LEFT);
			break;

		case VIRTKEY_AXIS_RIGHT_X_MIN:
			SetPSPAxis(axis.deviceId, 'X', -value, CTRL_STICK_RIGHT);
			break;
		case VIRTKEY_AXIS_RIGHT_X_MAX:
			SetPSPAxis(axis.deviceId, 'X', value, CTRL_STICK_RIGHT);
			break;
		case VIRTKEY_AXIS_RIGHT_Y_MIN:
			SetPSPAxis(axis.deviceId, 'Y', -value, CTRL_STICK_RIGHT);
			break;
		case VIRTKEY_AXIS_RIGHT_Y_MAX:
			SetPSPAxis(axis.deviceId, 'Y', value, CTRL_STICK_RIGHT);
			break;

		case VIRTKEY_SPEED_ANALOG:
			ProcessAnalogSpeed(axis, false);
			break;
		}
	}

	std::vector<int> resultsOpposite;
	KeyMap::InputMappingToPspButton(InputMapping(axis.deviceId, axis.axisId, -direction), &resultsOpposite);

	for (int result : resultsOpposite) {
		if (result == VIRTKEY_SPEED_ANALOG)
			ProcessAnalogSpeed(axis, true);
	}

	int axisState = 0;
	float threshold = axis.deviceId == DEVICE_ID_MOUSE ? AXIS_BIND_THRESHOLD_MOUSE : AXIS_BIND_THRESHOLD;
	if (direction == 1 && axis.value >= threshold) {
		axisState = 1;
	} else if (direction == -1 && axis.value <= -threshold) {
		axisState = -1;
	} else {
		axisState = 0;
	}

	if (axisState != axisState_[axis.axisId]) {
		axisState_[axis.axisId] = axisState;
		if (axisState != 0) {
			for (size_t i = 0; i < results.size(); i++) {
				if (!IsAnalogStickKey(results[i]))
					SetPSPKey(axis.deviceId, results[i], KEY_DOWN);
			}
			// Also unpress the other direction (unless both directions press the same key.)
			for (size_t i = 0; i < resultsOpposite.size(); i++) {
				if (!IsAnalogStickKey(resultsOpposite[i]) && std::find(results.begin(), results.end(), resultsOpposite[i]) == results.end())
					SetPSPKey(axis.deviceId, resultsOpposite[i], KEY_UP);
			}
		} else if (axisState == 0) {
			// Release both directions, trying to deal with some erratic controllers that can cause it to stick.
			for (size_t i = 0; i < results.size(); i++) {
				if (!IsAnalogStickKey(results[i]))
					SetPSPKey(axis.deviceId, results[i], KEY_UP);
			}
			for (size_t i = 0; i < resultsOpposite.size(); i++) {
				if (!IsAnalogStickKey(resultsOpposite[i]))
					SetPSPKey(axis.deviceId, resultsOpposite[i], KEY_UP);
			}
		}
	}
}

void ControlMapper::ProcessAnalogSpeed(const AxisInput &axis, bool opposite) {
	static constexpr float DEADZONE_THRESHOLD = 0.15f;
	static constexpr float DEADZONE_SCALE = 1.0f / (1.0f - DEADZONE_THRESHOLD);

	FPSLimit &limitMode = PSP_CoreParameter().fpsLimit;
	// If we're using an alternate speed already, let that win.
	if (limitMode != FPSLimit::NORMAL && limitMode != FPSLimit::ANALOG)
		return;
	// Don't even try if the limit is invalid.
	if (g_Config.iAnalogFpsLimit <= 0)
		return;

	AnalogFpsMode mode = (AnalogFpsMode)g_Config.iAnalogFpsMode;
	float value = axis.value;
	if (mode == AnalogFpsMode::AUTO) {
		// TODO: Consider the pad name for better auto?  KeyMap::PadName(axis.deviceId);
		switch (axis.axisId) {
		case JOYSTICK_AXIS_X:
		case JOYSTICK_AXIS_Y:
		case JOYSTICK_AXIS_Z:
		case JOYSTICK_AXIS_RX:
		case JOYSTICK_AXIS_RY:
		case JOYSTICK_AXIS_RZ:
			// These, at least on directinput, can be used for triggers that go from mapped to opposite.
			mode = AnalogFpsMode::MAPPED_DIR_TO_OPPOSITE_DIR;
			break;

		default:
			// Other axises probably don't go from negative to positive.
			mode = AnalogFpsMode::MAPPED_DIRECTION;
			break;
		}
	}

	// Okay, now let's map it as appropriate.
	if (mode == AnalogFpsMode::MAPPED_DIRECTION) {
		value = fabsf(value);
		// Clamp to 0 in this case if we're processing the opposite direction.
		if (opposite)
			value = 0.0f;
	} else if (mode == AnalogFpsMode::MAPPED_DIR_TO_OPPOSITE_DIR) {
		value = fabsf(value);
		if (opposite)
			value = -value;
		value = 0.5f - value * 0.5f;
	}

	// Apply a small deadzone (against the resting position.)
	value = std::max(0.0f, (value - DEADZONE_THRESHOLD) * DEADZONE_SCALE);

	// If target is above 60, value is how much to speed up over 60.  Otherwise, it's how much slower.
	// So normalize the target.
	int target = g_Config.iAnalogFpsLimit - 60;
	PSP_CoreParameter().analogFpsLimit = 60 + (int)(target * value);

	// If we've reset back to normal, turn it off.
	limitMode = PSP_CoreParameter().analogFpsLimit == 60 ? FPSLimit::NORMAL : FPSLimit::ANALOG;
}
