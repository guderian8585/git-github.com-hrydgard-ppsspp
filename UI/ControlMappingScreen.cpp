// Copyright (c) 2013- PPSSPP Project.

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

#include "ppsspp_config.h"
#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "Common/Render/TextureAtlas.h"
#include "Common/UI/Root.h"
#include "Common/UI/UI.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Common/Log.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Input/InputState.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Core/KeyMap.h"
#include "Core/Host.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "UI/ControlMappingScreen.h"
#include "UI/GameSettingsScreen.h"

class SingleControlMapper : public UI::LinearLayout {
public:
	SingleControlMapper(int pspKey, std::string keyName, ScreenManager *scrm, UI::LinearLayoutParams *layoutParams = nullptr);

	int GetPspKey() const { return pspKey_; }

private:
	void Refresh();

	UI::EventReturn OnDelete(UI::EventParams &params);
	UI::EventReturn OnMap(UI::EventParams &params);

	UI::Choice *addButton_ = nullptr;
	UI::Choice *replaceAllButton_ = nullptr;
	std::vector<UI::View *> rows_;

	int actionIndex_;
	int pspKey_;
	std::string keyName_;
	ScreenManager *scrm_;
};

SingleControlMapper::SingleControlMapper(int pspKey, std::string keyName, ScreenManager *scrm, UI::LinearLayoutParams *layoutParams)
	: UI::LinearLayout(UI::ORIENT_VERTICAL, layoutParams), pspKey_(pspKey), keyName_(keyName), scrm_(scrm) {
	Refresh();
}

void SingleControlMapper::Refresh() {
	Clear();
	auto mc = GetI18NCategory("MappableControls");

	std::map<std::string, ImageID> keyImages;
	keyImages["Circle"] = ImageID("I_CIRCLE");
	keyImages["Cross"] = ImageID("I_CROSS");
	keyImages["Square"] = ImageID("I_SQUARE");
	keyImages["Triangle"] = ImageID("I_TRIANGLE");
	keyImages["Start"] = ImageID("I_START");
	keyImages["Select"] = ImageID("I_SELECT");
	keyImages["L"] = ImageID("I_L");
	keyImages["R"] = ImageID("I_R");

	using namespace UI;

	float itemH = 45;

	float leftColumnWidth = 200;
	float rightColumnWidth = 250;  // TODO: Should be flexible somehow. Maybe we need to implement Measure.

	LinearLayout *root = Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	root->SetSpacing(3.0f);

	auto iter = keyImages.find(keyName_);
	// First, look among images.
	if (iter != keyImages.end()) {
		replaceAllButton_ = new Choice(iter->second, new LinearLayoutParams(leftColumnWidth, itemH));
	} else {
		// No image? Let's translate.
		replaceAllButton_ = new Choice(mc->T(keyName_.c_str()), new LinearLayoutParams(leftColumnWidth, itemH));
		replaceAllButton_->SetCentered(true);
	}
	root->Add(replaceAllButton_)->OnClick.Handle(this, &SingleControlMapper::OnMap);

	addButton_ = root->Add(new Choice(" + ", new LayoutParams(WRAP_CONTENT, itemH)));
	addButton_->OnClick.Handle(this, &SingleControlMapper::OnMap);

	LinearLayout *rightColumn = root->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(rightColumnWidth, WRAP_CONTENT)));
	rightColumn->SetSpacing(2.0f);
	std::vector<KeyDef> mappings;
	KeyMap::KeyFromPspButton(pspKey_, &mappings, false);

	rows_.empty();
	for (size_t i = 0; i < mappings.size(); i++) {
		std::string deviceName = GetDeviceName(mappings[i].deviceId);
		std::string keyName = KeyMap::GetKeyOrAxisName(mappings[i].keyCode);

		LinearLayout *row = rightColumn->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		row->SetSpacing(1.0f);
		rows_.push_back(row);

		Choice *c = row->Add(new Choice(deviceName + "." + keyName, new LinearLayoutParams(FILL_PARENT, itemH, 1.0f)));
		c->OnClick.Handle(this, &SingleControlMapper::OnMap);

		Choice *d = row->Add(new Choice(" X ", new LayoutParams(WRAP_CONTENT, itemH)));
		d->SetTag(StringFromFormat("%d_Del%d", (int)i, pspKey_));
		d->OnClick.Handle(this, &SingleControlMapper::OnDelete);
	}

	if (mappings.size() == 0) {
		// look like an empty line
		Choice *c = rightColumn->Add(new Choice("", new LinearLayoutParams(FILL_PARENT, itemH)));
		c->OnClick.Handle(this, &SingleControlMapper::OnMap);
	}
}

/*void SingleControlMapper::MappedCallback(KeyDef kdf) {
	switch (action_) {
	case ADD:
		KeyMap::SetKeyMapping(pspKey_, kdf, false);
		addButton_->SetFocus();
		break;
	case REPLACEALL:
		KeyMap::SetKeyMapping(pspKey_, kdf, true);
		replaceAllButton_->SetFocus();
		break;
	case REPLACEONE:
		KeyMap::g_controllerMap[pspKey_][actionIndex_] = kdf;
		KeyMap::g_controllerMapGeneration++;
		if (actionIndex_ < rows_.size())
			rows_[actionIndex_]->SetFocus();
		else
			SetFocus();
		break;
	default:
		SetFocus();
		break;
	}
	g_Config.bMapMouse = false;
}*/

UI::EventReturn SingleControlMapper::OnMap(UI::EventParams &params) {
	auto mc = GetI18NCategory("MappableControls");
	scrm_->push(new KeyMappingDialog(pspKey_, mc));

	return UI::EVENT_DONE;
}

UI::EventReturn SingleControlMapper::OnDelete(UI::EventParams &params) {
	int index = atoi(params.v->Tag().c_str());
	KeyMap::g_controllerMap[pspKey_].erase(KeyMap::g_controllerMap[pspKey_].begin() + index);
	KeyMap::g_controllerMapGeneration++;

	if (index + 1 < rows_.size())
		rows_[index]->SetFocus();
	else
		SetFocus();
	return UI::EVENT_DONE;
}

void ControlMappingScreen::CreateViews() {
	using namespace UI;
	mappers_.clear();

	auto km = GetI18NCategory("KeyMapping");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT, Margins(10, 0, 0, 10)));
	leftColumn->Add(new Choice(km->T("Clear All")))->OnClick.Handle(this, &ControlMappingScreen::OnClearMapping);
	leftColumn->Add(new Choice(km->T("Default All")))->OnClick.Handle(this, &ControlMappingScreen::OnDefaultMapping);

	std::string sysName = System_GetProperty(SYSPROP_NAME);
	// If there's a builtin controller, restore to default should suffice. No need to conf the controller on top.
	if (!KeyMap::HasBuiltinController(sysName) && KeyMap::GetSeenPads().size()) {
		leftColumn->Add(new Choice(km->T("Autoconfigure")))->OnClick.Handle(this, &ControlMappingScreen::OnAutoConfigure);
	}

	leftColumn->Add(new Choice(km->T("Show PSP")))->OnClick.Handle(this, &ControlMappingScreen::OnVisualizeMapping);

	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0f)));
	AddStandardBack(leftColumn);

	rightScroll_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightScroll_->SetTag("ControlMapping");
	rightScroll_->SetScrollToTop(false);
	LinearLayout *rightColumn = new LinearLayoutList(ORIENT_VERTICAL);
	rightScroll_->Add(rightColumn);

	root_->Add(leftColumn);
	root_->Add(rightScroll_);

	std::vector<KeyMap::KeyMap_IntStrPair> mappableKeys = KeyMap::GetMappableKeys();
	for (size_t i = 0; i < mappableKeys.size(); i++) {
		SingleControlMapper *mapper = rightColumn->Add(
			new SingleControlMapper(mappableKeys[i].key, mappableKeys[i].name, screenManager(),
				                    new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		mapper->SetTag(StringFromFormat("KeyMap%s", mappableKeys[i].name));
		mappers_.push_back(mapper);
	}

	keyMapGeneration_ = KeyMap::g_controllerMapGeneration;
}

void ControlMappingScreen::update() {
	if (KeyMap::HasChanged(keyMapGeneration_)) {
		RecreateViews();
	}

	UIDialogScreenWithBackground::update();
}

UI::EventReturn ControlMappingScreen::OnClearMapping(UI::EventParams &params) {
	KeyMap::g_controllerMap.clear();
	KeyMap::g_controllerMapGeneration++;
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMappingScreen::OnDefaultMapping(UI::EventParams &params) {
	KeyMap::RestoreDefault();
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMappingScreen::OnAutoConfigure(UI::EventParams &params) {
	std::vector<std::string> items;
	const auto seenPads = KeyMap::GetSeenPads();
	for (auto s = seenPads.begin(), end = seenPads.end(); s != end; ++s) {
		items.push_back(*s);
	}
	auto km = GetI18NCategory("KeyMapping");
	ListPopupScreen *autoConfList = new ListPopupScreen(km->T("Autoconfigure for device"), items, -1);
	if (params.v)
		autoConfList->SetPopupOrigin(params.v);
	screenManager()->push(autoConfList);
	return UI::EVENT_DONE;
}

UI::EventReturn ControlMappingScreen::OnVisualizeMapping(UI::EventParams &params) {
	VisualMappingScreen *visualMapping = new VisualMappingScreen();
	screenManager()->push(visualMapping);
	return UI::EVENT_DONE;
}

void ControlMappingScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DR_OK && dialog->tag() == "listpopup") {
		ListPopupScreen *popup = (ListPopupScreen *)dialog;
		KeyMap::AutoConfForPad(popup->GetChoiceString());
	}
}

void KeyMappingNewKeyDialog::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto km = GetI18NCategory("KeyMapping");
	auto mc = GetI18NCategory("MappableControls");

	std::string pspButtonName = KeyMap::GetPspButtonName(this->pspBtn_);

	parent->Add(new TextView(std::string(km->T("Map a new key for")) + " " + mc->T(pspButtonName), new LinearLayoutParams(Margins(10,0))));
}

bool KeyMappingNewKeyDialog::key(const KeyInput &key) {
	if (mapped_)
		return false;
	if (key.flags & KEY_DOWN) {
		if (key.keyCode == NKCODE_EXT_MOUSEBUTTON_1) {
			return true;
		}

		mapped_ = true;
		KeyDef kdf(key.deviceId, key.keyCode);
		TriggerFinish(DR_YES);
		if (callback_)
			callback_(kdf);
	}
	return true;
}

class BindingChoice : public UI::Choice {
private:
	std::string bindingText_;
	bool binding_ = false;

public:
	BindingChoice(const std::string &text, const std::string& bindingText, UI::LayoutParams *layoutParams = nullptr)
		: Choice(text, layoutParams), bindingText_(bindingText) {}

	void Draw(UIContext &dc) override;
	void SetBinding(bool b) { binding_ = b; }
};

void BindingChoice::Draw(UIContext &dc) {
	using namespace UI;
	
	Style style = dc.theme->itemStyle;
	if (highlighted_) {
		style = dc.theme->itemHighlightedStyle;
	}
	if (down_) {
		style = dc.theme->itemDownStyle;
	}
	if (HasFocus()) {
		style = dc.theme->itemFocusedStyle;
	}
	if (binding_) {
		style = dc.theme->itemStyle;
	}
	DrawBG(dc, style);
	
	if (binding_) {
		float opacity = sin(5.0*time_now_d())*0.5 + 0.5;
		uint32_t col = colorAlpha(dc.theme->itemFocusedStyle.background.color, opacity);
		dc.FillRect(Drawable(col), bounds_);
	}

	style = dc.theme->itemStyle;
	dc.SetFontStyle(dc.theme->uiFont);

	const int paddingX = 12;
	const float availWidth = bounds_.w - paddingX * 2 - textPadding_.horiz();
	float scale = CalculateTextScale(dc, availWidth);

	dc.SetFontScale(scale, scale);

	Bounds textBounds(bounds_.x + paddingX + textPadding_.left, bounds_.y, availWidth, bounds_.h);

	dc.DrawTextRect(binding_ ? bindingText_.c_str() : text_.c_str(), textBounds, style.fgColor, ALIGN_VCENTER | FLAG_WRAP_TEXT);
	dc.SetFontScale(1.0f, 1.0f);
}

void KeyMappingDialog::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto km = GetI18NCategory("KeyMapping");
	auto mc = GetI18NCategory("MappableControls");
	auto di = GetI18NCategory("Dialog");

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	LinearLayout *items = new LinearLayoutList(ORIENT_VERTICAL);
	items->SetSpacing(5.0f);

	static const std::string hint = "Hint: you may map the same key to different PSP button to be able to press multiple one with a single key!";
	parent->Add(new TextView(km->T(hint), FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT, true, new LinearLayoutParams(Margins(10,0))));

	std::vector<KeyDef> mappings;
	KeyMap::KeyFromPspButton(pspBtn_, &mappings, false);

	for (size_t i = 0; i < mappings.size(); i++) {
		std::string deviceName = GetDeviceName(mappings[i].deviceId);
		std::string keyName = KeyMap::GetKeyOrAxisName(mappings[i].keyCode);

		LinearLayout *row = items->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		row->SetSpacing(1.0f);

		BindingChoice *c = row->Add(new BindingChoice(deviceName + "." + keyName, km->T("Press a key"), new LinearLayoutParams(FILL_PARENT, 45, 1.0f)));
		c->OnClick.Add([=](EventParams &e) {
			this->selected_ = c; 
			this->selectedIndex_ = i+1;
			c->SetBinding(true);
			return UI::EVENT_CONTINUE;
		});

		Choice *d = row->Add(new Choice(km->T("Delete"), new LinearLayoutParams(WRAP_CONTENT, 45)));
		d->OnClick.Add([=](EventParams &e) {
			KeyMap::g_controllerMap[pspBtn_].erase(KeyMap::g_controllerMap[pspBtn_].begin() + i);
			KeyMap::g_controllerMapGeneration++;
			RecreateViews();
			return UI::EVENT_CONTINUE;
		});
	}

	BindingChoice *n = items->Add(new BindingChoice(km->T("Add new key binding"), km->T("Press a key"), new LinearLayoutParams(FILL_PARENT, 45)));
	n->OnClick.Add([=](EventParams &e) {
		this->selected_ = n;
		this->selectedIndex_ = 0;
		n->SetBinding(true);
		return UI::EVENT_CONTINUE;
	});

	if (g_Config.bMouseControl) {
		BindingChoice *m = items->Add(new BindingChoice(km->T("Add new mouse binding"), km->T("Press ESC to cancel"), new LinearLayoutParams(FILL_PARENT, 45)));
		m->OnClick.Add([=](EventParams &e) {
			this->selected_ = m;
			this->selectedIndex_ = 0;
			m->SetBinding(true);
			g_Config.bMapMouse = true;
			return UI::EVENT_CONTINUE;
		});
	}

	items->Add(new Spacer(10.0));
	Choice *b = items->Add(new Choice(di->T("Back"), new LinearLayoutParams(FILL_PARENT, 45)));
	b->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	scroll->Add(items);
	parent->Add(scroll);
}

bool KeyMappingDialog::key(const KeyInput &key) {
	if (selectedIndex_ < 0)
		return PopupScreen::key(key);

	if (key.flags & KEY_DOWN) {

		// Have a way out key in both mode
		if (key.keyCode != (g_Config.bMapMouse ? NKCODE_ESCAPE : NKCODE_EXT_MOUSEBUTTON_1)) {
			KeyDef kdf(key.deviceId, key.keyCode);
			if (selectedIndex_ == 0) {
				KeyMap::SetKeyMapping(pspBtn_, kdf, false);
			} else {
				KeyMap::g_controllerMap[pspBtn_][selectedIndex_-1] = kdf;
			}
			KeyMap::g_controllerMapGeneration++;
		}

		selected_->SetBinding(false);
		selectedIndex_ = -1;
		g_Config.bMapMouse = false;

		RecreateViews();
	}
	return true;
}

static bool IgnoreAxisForMapping(int axis) {
	switch (axis) {
		// Ignore the accelerometer for mapping for now.
	case JOYSTICK_AXIS_ACCELEROMETER_X:
	case JOYSTICK_AXIS_ACCELEROMETER_Y:
	case JOYSTICK_AXIS_ACCELEROMETER_Z:
		return true;

		// Also ignore some weird axis events we get on Ouya.
	case JOYSTICK_AXIS_OUYA_UNKNOWN1:
	case JOYSTICK_AXIS_OUYA_UNKNOWN2:
	case JOYSTICK_AXIS_OUYA_UNKNOWN3:
	case JOYSTICK_AXIS_OUYA_UNKNOWN4:
		return true;

	default:
		return false;
	}
}

bool KeyMappingNewKeyDialog::axis(const AxisInput &axis) {
	if (mapped_)
		return false;
	if (IgnoreAxisForMapping(axis.axisId))
		return false;

	if (axis.value > AXIS_BIND_THRESHOLD) {
		mapped_ = true;
		KeyDef kdf(axis.deviceId, KeyMap::TranslateKeyCodeFromAxis(axis.axisId, 1));
		TriggerFinish(DR_YES);
		if (callback_)
			callback_(kdf);
	}

	if (axis.value < -AXIS_BIND_THRESHOLD) {
		mapped_ = true;
		KeyDef kdf(axis.deviceId, KeyMap::TranslateKeyCodeFromAxis(axis.axisId, -1));
		TriggerFinish(DR_YES);
		if (callback_)
			callback_(kdf);
	}
	return true;
}

bool KeyMappingDialog::axis(const AxisInput &axis) {
	if (IgnoreAxisForMapping(axis.axisId) || selectedIndex_ < 0)
		return PopupScreen::axis(axis);

	if (abs(axis.value) > AXIS_BIND_THRESHOLD) {
		KeyDef kdf(axis.deviceId, KeyMap::TranslateKeyCodeFromAxis(axis.axisId, axis.value > 0 ? 1 : -1));
		if (selectedIndex_ == 0) {
			KeyMap::SetKeyMapping(pspBtn_, kdf, false);
		} else {
			KeyMap::g_controllerMap[pspBtn_][selectedIndex_-1] = kdf;
		}

		KeyMap::g_controllerMapGeneration++;
		selected_->SetBinding(false);
		selectedIndex_ = -1;
		g_Config.bMapMouse = false;

		RecreateViews();
	}
	return true;
}

enum class StickHistoryViewType {
	INPUT,
	OUTPUT
};

class JoystickHistoryView : public UI::InertView {
public:
	JoystickHistoryView(StickHistoryViewType type, std::string title, UI::LayoutParams *layoutParams = nullptr)
		: UI::InertView(layoutParams), title_(title), type_(type) {}
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return "Analog Stick View"; }
	void Update() override;
	void SetXY(float x, float y) {
		curX_ = x;
		curY_ = y;
	}

private:
	struct Location {
		float x;
		float y;
	};

	float curX_ = 0.0f;
	float curY_ = 0.0f;

	std::deque<Location> locations_;
	int maxCount_ = 500;
	std::string title_;
	StickHistoryViewType type_;
};

void JoystickHistoryView::Draw(UIContext &dc) {
	const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(ImageID("I_CROSS"));
	if (!image) {
		return;
	}
	float minRadius = std::min(bounds_.w, bounds_.h) * 0.5f - image->w;
	dc.Begin();
	dc.DrawTextShadow(title_.c_str(), bounds_.centerX(), bounds_.centerY() + minRadius + 5.0, 0xFFFFFFFF, ALIGN_TOP | ALIGN_HCENTER);
	dc.Flush();
	dc.BeginNoTex();
	dc.Draw()->RectOutline(bounds_.centerX() - minRadius, bounds_.centerY() - minRadius, minRadius * 2.0f, minRadius * 2.0f, 0x80FFFFFF);
	dc.Flush();
	dc.Begin();

	// First draw a grid.
	float dx = 1.0f / 10.0f;
	for (int ix = -10; ix <= 10; ix++) {
		// First draw vertical lines.
		float fx = ix * dx;
		for (int iy = -10; iy < 10; iy++) {
			float ax = fx;
			float ay = iy * dx;
			float bx = fx;
			float by = (iy + 1) * dx;

			if (type_ == StickHistoryViewType::OUTPUT) {
				ConvertAnalogStick(ax, ay);
				ConvertAnalogStick(bx, by);
			}

			ax = ax * minRadius + bounds_.centerX();
			ay = ay * minRadius + bounds_.centerY();

			bx = bx * minRadius + bounds_.centerX();
			by = by * minRadius + bounds_.centerY();

			dc.Draw()->Line(dc.theme->whiteImage, ax, ay, bx, by, 1.0, 0x70FFFFFF);
		}
	}

	for (int iy = -10; iy <= 10; iy++) {
		// Then horizontal.
		float fy = iy * dx;
		for (int ix = -10; ix < 10; ix++) {
			float ax = ix * dx;
			float ay = fy;
			float bx = (ix + 1) * dx;
			float by = fy;

			if (type_ == StickHistoryViewType::OUTPUT) {
				ConvertAnalogStick(ax, ay);
				ConvertAnalogStick(bx, by);
			}

			ax = ax * minRadius + bounds_.centerX();
			ay = ay * minRadius + bounds_.centerY();

			bx = bx * minRadius + bounds_.centerX();
			by = by * minRadius + bounds_.centerY();

			dc.Draw()->Line(dc.theme->whiteImage, ax, ay, bx, by, 1.0, 0x70FFFFFF);
		}
	}


	int a = maxCount_ - (int)locations_.size();
	for (auto iter = locations_.begin(); iter != locations_.end(); ++iter) {
		float x = bounds_.centerX() + minRadius * iter->x;
		float y = bounds_.centerY() - minRadius * iter->y;
		float alpha = (float)a / (float)(maxCount_ - 1);
		if (alpha < 0.0f) {
			alpha = 0.0f;
		}
		// Emphasize the newest (higher) ones.
		alpha = powf(alpha, 3.7f);
		// Highlight the output.
		if (alpha >= 1.0f && type_ == StickHistoryViewType::OUTPUT) {
			dc.Draw()->DrawImage(ImageID("I_CIRCLE"), x, y, 1.0f, colorAlpha(0xFFFFFF, 1.0), ALIGN_CENTER);
		} else {
			dc.Draw()->DrawImage(ImageID("I_CIRCLE"), x, y, 0.8f, colorAlpha(0xC0C0C0, alpha * 0.5f), ALIGN_CENTER);
		}
		a++;
	}
	dc.Flush();
}

void JoystickHistoryView::Update() {
	locations_.push_back(Location{ curX_, curY_ });
	if ((int)locations_.size() > maxCount_) {
		locations_.pop_front();
	}
}

AnalogSetupScreen::AnalogSetupScreen() {
	mapper_.SetCallbacks([](int vkey) {}, [](int vkey) {}, [&](int stick, float x, float y) {
		analogX_[stick] = x;
		analogY_[stick] = y;
	});
	mapper_.SetRawCallback([&](int stick, float x, float y) {
		rawX_[stick] = x;
		rawY_[stick] = y;
	});
}

void AnalogSetupScreen::update() {
	mapper_.Update();
	// We ignore the secondary stick for now and just use the two views
	// for raw and psp input.
	if (stickView_[0]) {
		stickView_[0]->SetXY(analogX_[0], analogY_[0]);
	}
	if (stickView_[1]) {
		stickView_[1]->SetXY(rawX_[0], rawY_[0]);
	}
	UIScreen::update();
}

bool AnalogSetupScreen::key(const KeyInput &key) {
	bool retval = UIScreen::key(key);

	// Allow testing auto-rotation. If it collides with UI keys, too bad.
	bool pauseTrigger = false;
	mapper_.Key(key, &pauseTrigger);

	if (UI::IsEscapeKey(key)) {
		TriggerFinish(DR_BACK);
		return retval;
	}
	return retval;
}

bool AnalogSetupScreen::axis(const AxisInput &axis) {
	// We DON'T call UIScreen::Axis here! Otherwise it'll try to move the UI focus around.
	// UIScreen::axis(axis);

	// Instead we just send the input directly to the mapper, that we'll visualize.
	return mapper_.Axis(axis);
}

void AnalogSetupScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	LinearLayout *leftColumn = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300.0f, FILL_PARENT)));
	LinearLayout *rightColumn = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));

	auto co = GetI18NCategory("Controls");
	ScrollView *scroll = leftColumn->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));

	LinearLayout *scrollContents = scroll->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300.0f, WRAP_CONTENT)));

	scrollContents->Add(new ItemHeader(co->T("Analog Settings", "Analog Settings")));

	// TODO: Would be nicer if these didn't pop up...
	scrollContents->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogDeadzone, 0.0f, 0.5f, co->T("Deadzone radius"), 0.01f, screenManager(), "/ 1.0"));
	scrollContents->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogInverseDeadzone, 0.0f, 1.0f, co->T("Low end radius"), 0.01f, screenManager(), "/ 1.0"));
	scrollContents->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogSensitivity, 0.0f, 2.0f, co->T("Sensitivity (scale)", "Sensitivity"), 0.01f, screenManager(), "x"));
	// TODO: This should probably be a slider.
	scrollContents->Add(new CheckBox(&g_Config.bAnalogIsCircular, co->T("Circular stick input")));
	scrollContents->Add(new PopupSliderChoiceFloat(&g_Config.fAnalogAutoRotSpeed, 0.0f, 20.0f, co->T("Auto-rotation speed"), 1.0f, screenManager()));
	scrollContents->Add(new Choice(co->T("Reset to defaults")))->OnClick.Handle(this, &AnalogSetupScreen::OnResetToDefaults);

	LinearLayout *theTwo = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f));

	stickView_[0] = theTwo->Add(new JoystickHistoryView(StickHistoryViewType::OUTPUT, co->T("Calibrated"), new LinearLayoutParams(1.0f)));
	stickView_[1] = theTwo->Add(new JoystickHistoryView(StickHistoryViewType::INPUT, co->T("Raw input"), new LinearLayoutParams(1.0f)));

	rightColumn->Add(theTwo);

	leftColumn->Add(new Button(di->T("Back"), new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

UI::EventReturn AnalogSetupScreen::OnResetToDefaults(UI::EventParams &e) {
	g_Config.fAnalogDeadzone = 0.15f;
	g_Config.fAnalogInverseDeadzone = 0.0f;
	g_Config.fAnalogSensitivity = 1.1f;
	g_Config.bAnalogIsCircular = false;
	g_Config.fAnalogAutoRotSpeed = 8.0f;
	return UI::EVENT_DONE;
}

bool TouchTestScreen::touch(const TouchInput &touch) {
	UIDialogScreenWithBackground::touch(touch);
	if (touch.flags & TOUCH_DOWN) {
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			if (touches_[i].id == touch.id) {
				WARN_LOG(SYSTEM, "Double touch");
				touches_[i].x = touch.x;
				touches_[i].y = touch.y;
				found = true;
			}
		}
		if (!found) {
			for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
				if (touches_[i].id == -1) {
					touches_[i].id = touch.id;
					touches_[i].x = touch.x;
					touches_[i].y = touch.y;
					break;
				}
			}
		}
	}
	if (touch.flags & TOUCH_MOVE) {
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			if (touches_[i].id == touch.id) {
				touches_[i].x = touch.x;
				touches_[i].y = touch.y;
				found = true;
			}
		}
		if (!found) {
			WARN_LOG(SYSTEM, "Move without touch down: %d", touch.id);
		}
	}
	if (touch.flags & TOUCH_UP) {
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			if (touches_[i].id == touch.id) {
				found = true;
				touches_[i].id = -1;
				break;
			}
		}
		if (!found) {
			WARN_LOG(SYSTEM, "Touch release without touch down");
		}
	}
	return true;
}

void TouchTestScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto gr = GetI18NCategory("Graphics");
	root_ = new LinearLayout(ORIENT_VERTICAL);
	LinearLayout *theTwo = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));

	lastLastKeyEvent_ = theTwo->Add(new TextView("-", new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	lastLastKeyEvent_->SetTextColor(0x80FFFFFF);   // semi-transparent
	lastKeyEvent_ = theTwo->Add(new TextView("-", new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	root_->Add(theTwo);

#if !PPSSPP_PLATFORM(UWP)
	static const char *renderingBackend[] = { "OpenGL", "Direct3D 9", "Direct3D 11", "Vulkan" };
	PopupMultiChoice *renderingBackendChoice = root_->Add(new PopupMultiChoice(&g_Config.iGPUBackend, gr->T("Backend"), renderingBackend, (int)GPUBackend::OPENGL, ARRAY_SIZE(renderingBackend), gr->GetName(), screenManager()));
	renderingBackendChoice->OnChoice.Handle(this, &TouchTestScreen::OnRenderingBackend);

	if (!g_Config.IsBackendEnabled(GPUBackend::OPENGL))
		renderingBackendChoice->HideChoice((int)GPUBackend::OPENGL);
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D9))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D9);
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D11);
	if (!g_Config.IsBackendEnabled(GPUBackend::VULKAN))
		renderingBackendChoice->HideChoice((int)GPUBackend::VULKAN);
#endif

#if PPSSPP_PLATFORM(ANDROID)
	root_->Add(new Choice(gr->T("Recreate Activity")))->OnClick.Handle(this, &TouchTestScreen::OnRecreateActivity);
#endif
	root_->Add(new CheckBox(&g_Config.bImmersiveMode, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &TouchTestScreen::OnImmersiveModeChange);
	root_->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

#if PPSSPP_PLATFORM(ANDROID)
extern int display_xres;
extern int display_yres;
#endif

bool TouchTestScreen::key(const KeyInput &key) {
	char buf[512];
	snprintf(buf, sizeof(buf), "Keycode: %d Device ID: %d [%s%s%s%s]", key.keyCode, key.deviceId,
		(key.flags & KEY_IS_REPEAT) ? "REP" : "",
		(key.flags & KEY_UP) ? "UP" : "",
		(key.flags & KEY_DOWN) ? "DOWN" : "",
		(key.flags & KEY_CHAR) ? "CHAR" : "");
	if (lastLastKeyEvent_ && lastKeyEvent_) {
		lastLastKeyEvent_->SetText(lastKeyEvent_->GetText());
		lastKeyEvent_->SetText(buf);
	}
	return true;
}

bool TouchTestScreen::axis(const AxisInput &axis) {

	// This is mainly to catch axis events that would otherwise get translated
	// into arrow keys, since seeing keyboard arrow key events appear when using
	// a controller would be confusing for the user.
	if (IgnoreAxisForMapping(axis.axisId))
		return false;

	const float AXIS_LOG_THRESHOLD = AXIS_BIND_THRESHOLD * 0.5f;
	if (axis.value > AXIS_LOG_THRESHOLD || axis.value < -AXIS_LOG_THRESHOLD) {
		char buf[512];
		snprintf(buf, sizeof(buf), "Axis: %d (value %1.3f) Device ID: %d",
			axis.axisId, axis.value, axis.deviceId);
		// Null-check just in case they weren't created yet.
		if (lastLastKeyEvent_ && lastKeyEvent_) {
			lastLastKeyEvent_->SetText(lastKeyEvent_->GetText());
			lastKeyEvent_->SetText(buf);
		}
	}
	return true;
}

void TouchTestScreen::render() {
	UIDialogScreenWithBackground::render();
	UIContext *ui_context = screenManager()->getUIContext();
	Bounds bounds = ui_context->GetLayoutBounds();

	ui_context->BeginNoTex();
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touches_[i].id != -1) {
			ui_context->Draw()->Circle(touches_[i].x, touches_[i].y, 100.0, 3.0, 80, 0.0f, 0xFFFFFFFF, 1.0);
		}
	}
	ui_context->Flush();

	ui_context->Begin();

	char buffer[1024];
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touches_[i].id != -1) {
			ui_context->Draw()->Circle(touches_[i].x, touches_[i].y, 100.0, 3.0, 80, 0.0f, 0xFFFFFFFF, 1.0);
			snprintf(buffer, sizeof(buffer), "%0.1fx%0.1f", touches_[i].x, touches_[i].y);
			ui_context->DrawText(buffer, touches_[i].x, touches_[i].y + (touches_[i].y > dp_yres - 100.0f ? -135.0f : 95.0f), 0xFFFFFFFF, ALIGN_HCENTER | FLAG_DYNAMIC_ASCII);
		}
	}

	snprintf(buffer, sizeof(buffer),
#if PPSSPP_PLATFORM(ANDROID)
		"display_res: %dx%d\n"
#endif
		"dp_res: %dx%d\n"
		"pixel_res: %dx%d\n"
		"g_dpi: %f\n"
		"g_dpi_scale: %0.3fx%0.3f\n"
		"g_dpi_scale_real: %0.3fx%0.3f\n",
#if PPSSPP_PLATFORM(ANDROID)
		display_xres, display_yres,
#endif
		dp_xres, dp_yres,
		pixel_xres, pixel_yres,
		g_dpi,
		g_dpi_scale_x, g_dpi_scale_y,
		g_dpi_scale_real_x, g_dpi_scale_real_y);

	ui_context->DrawTextShadow(buffer, bounds.centerX(), bounds.y + 20.0f, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	ui_context->Flush();
}

void RecreateActivity();

UI::EventReturn TouchTestScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_SendMessage("immersive", "");
	if (g_Config.iAndroidHwScale != 0) {
		RecreateActivity();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn TouchTestScreen::OnRenderingBackend(UI::EventParams &e) {
	g_Config.Save("GameSettingsScreen::RenderingBackend");
	System_SendMessage("graphics_restart", "--touchscreentest");
	return UI::EVENT_DONE;
}

UI::EventReturn TouchTestScreen::OnRecreateActivity(UI::EventParams &e) {
	RecreateActivity();
	return UI::EVENT_DONE;
}

class Backplate : public UI::InertView {
public:
	Backplate(float scale, UI::LayoutParams *layoutParams = nullptr) : InertView(layoutParams), scale_(scale) {
	}

	void Draw(UIContext &dc) override {
		using namespace UI;

		const AtlasImage *whiteImage = dc.Draw()->GetAtlas()->getImage(dc.theme->whiteImage);
		float centerU = (whiteImage->u1 + whiteImage->u2) * 0.5f;
		float centerV = (whiteImage->v1 + whiteImage->v2) * 0.5f;
		const uint32_t color = 0xB01C1818;

		auto V = [&](float x, float y) {
			dc.Draw()->V(bounds_.x + x * scale_, bounds_.y + y * scale_, color, centerU, centerV);
		};
		auto R = [&](float x1, float y1, float x2, float y2) {
			V(x1, y1); V(x2, y1); V(x2, y2);
			V(x1, y1); V(x2, y2); V(x1, y2);
		};

		// Curved left side.
		V(12.0f, 44.0f); V(30.0f, 16.0f); V(30.0f, 44.0f);
		V(0.0f, 80.0f); V(12.0f, 44.0f); V(12.0f, 80.0f);
		R(12.0f, 44.0f, 30.0f, 80.0f);
		R(0.0f, 80.0f, 30.0f, 114.0f);
		V(0.0f, 114.0f); V(12.0f, 114.0f); V(12.0f, 154.0f);
		R(12.0f, 114.0f, 30.0f, 154.0f);
		V(12.0f, 154.0f); V(30.0f, 154.0f); V(30.0f, 180.0f);
		// Left side.
		V(30.0f, 16.0f); V(64.0f, 13.0f); V(64.0f, 184.0f);
		V(30.0f, 16.0f); V(64.0f, 184.0f); V(30.0f, 180.0f);
		V(64.0f, 13.0f); V(76.0f, 0.0f); V(76.0f, 13.0f);
		V(64.0f, 184.0f); V(76.0f, 200.0f); V(76.0f, 184.0f);
		R(64.0f, 13.0f, 76.0f, 184.0f);
		// Center.
		V(76.0f, 0.0f); V(400.0f, 0.0f); V(400.0f, 200.0f);
		V(76.0f, 0.0f); V(400.0f, 200.0f); V(76.0f, 200.0f);
		// Right side.
		V(400.0f, 0.0f); V(412.0f, 13.0f); V(400.0f, 13.0f);
		V(400.0f, 184.0f); V(412.0f, 184.0f); V(400.0f, 200.0f);
		R(400.0f, 13.0f, 412.0f, 184.0f);
		V(412.0f, 13.0f); V(446.0f, 16.0f); V(446.0f, 180.0f);
		V(412.0f, 13.0f); V(446.0f, 180.0f); V(412.0f, 184.0f);
		// Curved right side.
		V(446.0f, 16.0f); V(462.0f, 44.0f); V(446.0f, 44.0f);
		V(462.0f, 44.0f); V(474.0f, 80.0f); V(462.0f, 80.0f);
		R(446.0f, 44.0f, 462.0f, 80.0f);
		R(446.0f, 80.0f, 474.0f, 114.0f);
		V(462.0f, 114.0f); V(474.0f, 114.0f); V(462.0f, 154.0f);
		R(446.0f, 114.0f, 462.0f, 154.0f);
		V(446.0f, 154.0f); V(462.0f, 154.0f); V(446.0f, 180.0f);
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 474.0f * scale_;
		h = 200.0f * scale_;
	}

protected:
	float scale_ = 1.0f;
};

class MockScreen : public UI::InertView {
public:
	MockScreen(UI::LayoutParams *layoutParams = nullptr) : InertView(layoutParams) {
	}

	void Draw(UIContext &dc) override {
		ImageID bg = ImageID("I_BG");
		dc.Draw()->DrawImageStretch(bg, bounds_, 0xFFFFFFFF);

		if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
			dc.Draw()->DrawImage(ImageID("I_ICONGOLD"), bounds_.centerX() - 120, bounds_.centerY() - 30, 1.2f, 0xFFFFFFFF, ALIGN_CENTER);
		} else {
			dc.Draw()->DrawImage(ImageID("I_ICON"), bounds_.centerX() - 120, bounds_.centerY() - 30, 1.2f, 0xFFFFFFFF, ALIGN_CENTER);
		}
		dc.Draw()->DrawImage(ImageID("I_LOGO"), bounds_.centerX() + 40, bounds_.centerY() - 30, 1.5f, 0xFFFFFFFF, ALIGN_CENTER);
	}
};

class MockButton : public UI::Clickable {
public:
	MockButton(int button, ImageID img, ImageID bg, float angle, UI::LayoutParams *layoutParams = nullptr)
		: Clickable(layoutParams), button_(button), img_(img), bgImg_(bg), angle_(angle) {
	}

	void Draw(UIContext &dc) override {
		uint32_t c = 0xFFFFFFFF;
		if (HasFocus() || Selected())
			c = dc.theme->buttonFocusedStyle.background.color;

		float scales[2]{};
		if (bgImg_.isValid())
			dc.Draw()->DrawImageRotatedStretch(bgImg_, bounds_, scales, angle_, c, flipHBG_);
		if (img_.isValid()) {
			scales[0] *= scale_;
			scales[1] *= scale_;
			dc.Draw()->DrawImageRotatedStretch(img_, bounds_.Offset(offsetX_, offsetY_), scales, angle_, c);
		}
	}

	MockButton *SetScale(float s) {
		scale_ = s;
		return this;
	}

	MockButton *SetFlipHBG(float f) {
		flipHBG_ = f;
		return this;
	}

	MockButton *SetOffset(float x, float y) {
		offsetX_ = x;
		offsetY_ = y;
		return this;
	}

	MockButton *SetSelectedButton(int *s) {
		selectedButton_ = s;
		return this;
	}

	bool Selected() {
		return selectedButton_ && *selectedButton_ == button_;
	}

	int Button() {
		return button_;
	}

private:
	int button_;
	ImageID img_;
	ImageID bgImg_;
	float angle_;
	float scale_ = 1.0f;
	float offsetX_ = 0.0f;
	float offsetY_ = 0.0f;
	bool flipHBG_ = false;
	int *selectedButton_ = nullptr;
};

class MockPSP : public UI::AnchorLayout {
public:
	static constexpr float SCALE = 1.4f;

	MockPSP(UI::LayoutParams *layoutParams = nullptr);
	void SelectButton(int btn);
	void FocusButton(int btn);
	float GetPopupOffset();

	UI::Event ButtonClick;

private:
	UI::AnchorLayoutParams *LayoutAt(float l, float t, float r, float b);
	UI::AnchorLayoutParams *LayoutSize(float w, float h, float l, float t);
	MockButton *AddButton(int button, ImageID img, ImageID bg, float angle, UI::LayoutParams *lp);

	UI::EventReturn OnSelectButton(UI::EventParams &e);

	std::unordered_map<int, MockButton *> buttons_;
	int selectedButton_ = 0;
};

MockPSP::MockPSP(UI::LayoutParams *layoutParams) : AnchorLayout(layoutParams) {
	Add(new Backplate(SCALE));
	Add(new MockScreen(LayoutAt(99.0f, 13.0f, 97.0f, 33.0f)));

	// Left side.
	AddButton(VIRTKEY_AXIS_Y_MAX, ImageID("I_STICK_LINE"), ImageID("I_STICK_BG_LINE"), 0.0f, LayoutSize(34.0f, 34.0f, 35.0f, 133.0f));
	AddButton(CTRL_LEFT, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 0.0f, LayoutSize(28.0f, 20.0f, 14.0f, 75.0f))->SetOffset(-4.0f * SCALE, 0.0f);
	AddButton(CTRL_UP, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 0.5f, LayoutSize(20.0f, 28.0f, 40.0f, 50.0f))->SetOffset(0.0f, -4.0f * SCALE);
	AddButton(CTRL_RIGHT, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 1.0f, LayoutSize(28.0f, 20.0f, 58.0f, 75.0f))->SetOffset(4.0f * SCALE, 0.0f);
	AddButton(CTRL_DOWN, ImageID("I_ARROW"), ImageID("I_DIR_LINE"), M_PI * 1.5f, LayoutSize(20.0f, 28.0f, 40.0f, 92.0f))->SetOffset(0.0f, 4.0f * SCALE);

	// Top.
	AddButton(CTRL_LTRIGGER, ImageID("I_L"), ImageID("I_SHOULDER_LINE"), 0.0f, LayoutSize(50.0f, 16.0f, 29.0f, 0.0f));
	AddButton(CTRL_RTRIGGER, ImageID("I_R"), ImageID("I_SHOULDER_LINE"), 0.0f, LayoutSize(50.0f, 16.0f, 397.0f, 0.0f))->SetFlipHBG(true);

	// Bottom.
	AddButton(CTRL_HOME, ImageID("I_ICON"), ImageID("I_RECT_LINE"), 0.0f, LayoutSize(28.0f, 14.0f, 88.0f, 181.0f))->SetScale(0.4f);
	AddButton(CTRL_SELECT, ImageID("I_SELECT"), ImageID("I_RECT_LINE"), 0.0f, LayoutSize(28.0f, 14.0f, 330.0f, 181.0f));
	AddButton(CTRL_START, ImageID("I_START"), ImageID("I_RECT_LINE"), 0.0f, LayoutSize(28.0f, 14.0f, 361.0f, 181.0f));

	// Right side.
	AddButton(CTRL_TRIANGLE, ImageID("I_TRIANGLE"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 419.0f, 46.0f))->SetScale(0.7f)->SetOffset(0.0f, -1.0f * SCALE);
	AddButton(CTRL_CIRCLE, ImageID("I_CIRCLE"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 446.0f, 74.0f))->SetScale(0.7f);
	AddButton(CTRL_CROSS, ImageID("I_CROSS"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 419.0f, 102.0f))->SetScale(0.7f);
	AddButton(CTRL_SQUARE, ImageID("I_SQUARE"), ImageID("I_ROUND_LINE"), 0.0f, LayoutSize(23.0f, 23.0f, 392.0f, 74.0f))->SetScale(0.7f);
}

void MockPSP::SelectButton(int btn) {
	selectedButton_ = btn;
}

void MockPSP::FocusButton(int btn) {
	MockButton *view = buttons_[selectedButton_];
	if (view)
		view->SetFocus();
}

float MockPSP::GetPopupOffset() {
	MockButton *view = buttons_[selectedButton_];
	if (!view)
		return 0.0f;

	float ypos = view->GetBounds().centerY();
	if (ypos > bounds_.centerY()) {
		return -0.25f;
	}
	return 0.25f;
}

UI::AnchorLayoutParams *MockPSP::LayoutAt(float l, float t, float r, float b) {
	return new UI::AnchorLayoutParams(l * SCALE, t * SCALE, r * SCALE, b * SCALE);
}
UI::AnchorLayoutParams *MockPSP::LayoutSize(float w, float h, float l, float t) {
	return new UI::AnchorLayoutParams(w * SCALE, h * SCALE, l * SCALE, t * SCALE, UI::NONE, UI::NONE);
}

MockButton *MockPSP::AddButton(int button, ImageID img, ImageID bg, float angle, UI::LayoutParams *lp) {
	MockButton *view = Add(new MockButton(button, img, bg, angle, lp));
	view->OnClick.Handle(this, &MockPSP::OnSelectButton);
	view->SetSelectedButton(&selectedButton_);
	buttons_[button] = view;
	return view;
}

UI::EventReturn MockPSP::OnSelectButton(UI::EventParams &e) {
	auto view = (MockButton *)e.v;
	e.a = view->Button();
	return ButtonClick.Dispatch(e);
}

static std::vector<int> bindAllOrder{
	CTRL_LTRIGGER,
	CTRL_RTRIGGER,
	CTRL_UP,
	CTRL_DOWN,
	CTRL_LEFT,
	CTRL_RIGHT,
	VIRTKEY_AXIS_Y_MAX,
	VIRTKEY_AXIS_Y_MIN,
	VIRTKEY_AXIS_X_MIN,
	VIRTKEY_AXIS_X_MAX,
	CTRL_HOME,
	CTRL_SELECT,
	CTRL_START,
	CTRL_CROSS,
	CTRL_CIRCLE,
	CTRL_TRIANGLE,
	CTRL_SQUARE,
};

void VisualMappingScreen::CreateViews() {
	using namespace UI;

	auto km = GetI18NCategory("KeyMapping");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	constexpr float leftColumnWidth = 200.0f;
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(leftColumnWidth, FILL_PARENT, Margins(10, 0, 0, 10)));
	leftColumn->Add(new Choice(km->T("Bind All")))->OnClick.Handle(this, &VisualMappingScreen::OnBindAll);
	leftColumn->Add(new CheckBox(&replace_, km->T("Replace"), ""));

	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0f)));
	AddStandardBack(leftColumn);

	Bounds bounds = screenManager()->getUIContext()->GetLayoutBounds();
	// Account for left side.
	bounds.w -= leftColumnWidth + 10.0f;

	AnchorLayout *rightColumn = new AnchorLayout(new LinearLayoutParams(bounds.w, FILL_PARENT, 1.0f));
	psp_ = rightColumn->Add(new MockPSP(new AnchorLayoutParams(bounds.centerX(), bounds.centerY(), NONE, NONE, true)));
	psp_->ButtonClick.Handle(this, &VisualMappingScreen::OnMapButton);

	root_->Add(leftColumn);
	root_->Add(rightColumn);
}

void VisualMappingScreen::resized() {
	UIDialogScreenWithBackground::resized();
	RecreateViews();
}

UI::EventReturn VisualMappingScreen::OnMapButton(UI::EventParams &e) {
	nextKey_ = e.a;
	MapNext();
	return UI::EVENT_DONE;
}

UI::EventReturn VisualMappingScreen::OnBindAll(UI::EventParams &e) {
	bindAll_ = 0;
	nextKey_ = bindAllOrder[bindAll_];
	MapNext();
	return UI::EVENT_DONE;
}

void VisualMappingScreen::HandleKeyMapping(KeyDef key) {
	KeyMap::SetKeyMapping(nextKey_, key, replace_);

	if (bindAll_ < 0) {
		// For analog, we do each direction in a row.
		if (nextKey_ == VIRTKEY_AXIS_Y_MAX)
			nextKey_ = VIRTKEY_AXIS_Y_MIN;
		else if (nextKey_ == VIRTKEY_AXIS_Y_MIN)
			nextKey_ = VIRTKEY_AXIS_X_MIN;
		else if (nextKey_ == VIRTKEY_AXIS_X_MIN)
			nextKey_ = VIRTKEY_AXIS_X_MAX;
		else {
			if (nextKey_ == VIRTKEY_AXIS_X_MAX)
				psp_->FocusButton(VIRTKEY_AXIS_Y_MAX);
			else
				psp_->FocusButton(nextKey_);
			nextKey_ = 0;
		}
	} else if ((size_t)bindAll_ + 1 < bindAllOrder.size()) {
		bindAll_++;
		nextKey_ = bindAllOrder[bindAll_];
	} else {
		bindAll_ = -1;
		nextKey_ = 0;
	}
}

void VisualMappingScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DR_YES && nextKey_ != 0) {
		MapNext();
	} else {
		// This means they canceled.
		if (nextKey_ != 0)
			psp_->FocusButton(nextKey_);
		nextKey_ = 0;
		bindAll_ = -1;
		psp_->SelectButton(0);
	}
}

void VisualMappingScreen::MapNext() {
	auto km = GetI18NCategory("KeyMapping");

	if (nextKey_ == VIRTKEY_AXIS_Y_MIN || nextKey_ == VIRTKEY_AXIS_X_MIN || nextKey_ == VIRTKEY_AXIS_X_MAX) {
		psp_->SelectButton(VIRTKEY_AXIS_Y_MAX);
	} else {
		psp_->SelectButton(nextKey_);
	}
	auto dialog = new KeyMappingNewKeyDialog(nextKey_, true, std::bind(&VisualMappingScreen::HandleKeyMapping, this, std::placeholders::_1), km);

	Bounds bounds = screenManager()->getUIContext()->GetLayoutBounds();
	dialog->SetPopupOffset(psp_->GetPopupOffset() * bounds.h);
	screenManager()->push(dialog);
}
