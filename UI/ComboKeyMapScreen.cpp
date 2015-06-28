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

#include "TouchControlVisibilityScreen.h"
#include "Core/Config.h"
#include "UI/ui_atlas.h"
#include "i18n/i18n.h"
#include "ComboKeyMapScreen.h"
#include "base/colorutil.h"
#include "base/display.h"
#include "base/timeutil.h"
#include "file/path.h"
#include "gfx_es2/draw_buffer.h"
#include "math/curves.h"
#include "base/stringutil.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

void Combo_keyScreen::CreateViews() {
	using namespace UI;
	root_ = new LinearLayout(ORIENT_HORIZONTAL);
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(120, FILL_PARENT));
	I18NCategory *di = GetI18NCategory("Dialog");

	int ComboKeyImage, ComboKeyImage1, ComboKeyImage2, ComboKeyImage3, ComboKeyImage4;
		ComboKeyImage = g_Config.iComboButtonStyle ? I_SQUARE1 : I_STAR;
		ComboKeyImage1 = g_Config.iComboButtonStyle ? I_TRIANGLE1 : I_EYE;
		ComboKeyImage2 = g_Config.iComboButtonStyle ? I_CROSS1 : I_GC;
		ComboKeyImage3 = g_Config.iComboButtonStyle ? I_A : I_X;
		ComboKeyImage4 = g_Config.iComboButtonStyle ? I_B : I_Y;

	I18NCategory *co = GetI18NCategory("Controls");
	
	comboselect = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(10, 10,  NONE, NONE));
	comboselect->SetSpacing(10);
	comboselect->AddChoice(ComboKeyImage);
	comboselect->AddChoice(ComboKeyImage1);
	comboselect->AddChoice(ComboKeyImage2);
	comboselect->AddChoice(ComboKeyImage3);
	comboselect->AddChoice(ComboKeyImage4);
	comboselect->SetSelection(*mode);
	comboselect->OnChoice.Handle(this, &Combo_keyScreen::onCombo);
	leftColumn->Add(comboselect);
	root_->Add(leftColumn);
	rightScroll_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	LinearLayout *rightColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightScroll_->Add(rightColumn);
	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0f)));
	leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root_->Add(rightScroll_);
	
	const int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = rightColumn->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	switch (*mode) {
	case 0: cComboCircle = (bool)(1 & g_Config.cCombokey);
			cComboCross = (bool)(2 & g_Config.cCombokey);
			cComboSquare = (bool)(4 & g_Config.cCombokey);
			cComboTriangle = (bool)(8 & g_Config.cCombokey);
			cComboLTrigger = (bool)(16 & g_Config.cCombokey);
			cComboRTrigger = (bool)(32 & g_Config.cCombokey);
			cComboLeft = (bool)(64 & g_Config.cCombokey);
			cComboUp = (bool)(128 & g_Config.cCombokey);
			cComboRight = (bool)(256 & g_Config.cCombokey);
			cComboDown = (bool)(512 & g_Config.cCombokey);
			cComboStart = (bool)(1024 & g_Config.cCombokey);
			cComboSelect = (bool)(2048 & g_Config.cCombokey);
			break;
	case 1: cComboCircle = (bool)(1 & g_Config.cCombokey1);
			cComboCross = (bool)(2 & g_Config.cCombokey1);
			cComboSquare = (bool)(4 & g_Config.cCombokey1);
			cComboTriangle = (bool)(8 & g_Config.cCombokey1);
			cComboLTrigger = (bool)(16 & g_Config.cCombokey1);
			cComboRTrigger = (bool)(32 & g_Config.cCombokey1);
			cComboLeft = (bool)(64 & g_Config.cCombokey1);
			cComboUp = (bool)(128 & g_Config.cCombokey1);
			cComboRight = (bool)(256 & g_Config.cCombokey1);
			cComboDown = (bool)(512 & g_Config.cCombokey1);
			cComboStart = (bool)(1024 & g_Config.cCombokey1);
			cComboSelect = (bool)(2048 & g_Config.cCombokey1);
			break;
	case 2: cComboCircle = (bool)(1 & g_Config.cCombokey2);
			cComboCross = (bool)(2 & g_Config.cCombokey2);
			cComboSquare = (bool)(4 & g_Config.cCombokey2);
			cComboTriangle = (bool)(8 & g_Config.cCombokey2);
			cComboLTrigger = (bool)(16 & g_Config.cCombokey2);
			cComboRTrigger = (bool)(32 & g_Config.cCombokey2);
			cComboLeft = (bool)(64 & g_Config.cCombokey2);
			cComboUp = (bool)(128 & g_Config.cCombokey2);
			cComboRight = (bool)(256 & g_Config.cCombokey2);
			cComboDown = (bool)(512 & g_Config.cCombokey2);
			cComboStart = (bool)(1024 & g_Config.cCombokey2);
			cComboSelect = (bool)(2048 & g_Config.cCombokey2);
			break;
	case 3: cComboCircle = (bool)(1 & g_Config.cCombokey3);
			cComboCross = (bool)(2 & g_Config.cCombokey3);
			cComboSquare = (bool)(4 & g_Config.cCombokey3);
			cComboTriangle = (bool)(8 & g_Config.cCombokey3);
			cComboLTrigger = (bool)(16 & g_Config.cCombokey3);
			cComboRTrigger = (bool)(32 & g_Config.cCombokey3);
			cComboLeft = (bool)(64 & g_Config.cCombokey3);
			cComboUp = (bool)(128 & g_Config.cCombokey3);
			cComboRight = (bool)(256 & g_Config.cCombokey3);
			cComboDown = (bool)(512 & g_Config.cCombokey3);
			cComboStart = (bool)(1024 & g_Config.cCombokey3);
			cComboSelect = (bool)(2048 & g_Config.cCombokey3);
			break;
	case 4: cComboCircle = (bool)(1 & g_Config.cCombokey4);
			cComboCross = (bool)(2 & g_Config.cCombokey4);
			cComboSquare = (bool)(4 & g_Config.cCombokey4);
			cComboTriangle = (bool)(8 & g_Config.cCombokey4);
			cComboLTrigger = (bool)(16 & g_Config.cCombokey4);
			cComboRTrigger = (bool)(32 & g_Config.cCombokey4);
			cComboLeft = (bool)(64 & g_Config.cCombokey4);
			cComboUp = (bool)(128 & g_Config.cCombokey4);
			cComboRight = (bool)(256 & g_Config.cCombokey4);
			cComboDown = (bool)(512 & g_Config.cCombokey4);
			cComboStart = (bool)(1024 & g_Config.cCombokey4);
			cComboSelect = (bool)(2048 & g_Config.cCombokey4);
	}
	
	std::map<std::string, int> keyImages;
	keyImages["Circle"] = I_CIRCLE;
	keyImages["Cross"] = I_CROSS;
	keyImages["Square"] = I_SQUARE;
	keyImages["Triangle"] = I_TRIANGLE;
	keyImages["L"] = I_L;
	keyImages["R"] = I_R;
	keyImages["ULeft"] = I_ARROW;
	keyImages["VUp"] = I_ARROW1;
	keyImages["WRight"] = I_ARROW2;
	keyImages["XDown"] = I_ARROW3;
	keyImages["Start"] = I_START;
	keyImages["Select"] = I_SELECT;





	keyToggles["Circle"] = &cComboCircle;
	keyToggles["Cross"] = &cComboCross;
	keyToggles["Square"] = &cComboSquare;
	keyToggles["Triangle"] = &cComboTriangle;
	keyToggles["L"] = &cComboLTrigger;
	keyToggles["R"] = &cComboRTrigger;
	keyToggles["ULeft"] = &cComboLeft;
	keyToggles["VUp"] = &cComboUp;
	keyToggles["WRight"] = &cComboRight;
	keyToggles["XDown"] = &cComboDown;
	keyToggles["Start"] = &cComboStart;
	keyToggles["Select"] = &cComboSelect;


	std::map<std::string, int>::iterator imageFinder;

	I18NCategory *mc = GetI18NCategory("MappableControls");

	for (auto i = keyToggles.begin(); i != keyToggles.end(); ++i) {
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		row->SetSpacing(0);

		CheckBox *checkbox = new CheckBox(i->second, "", "", new LinearLayoutParams(50, WRAP_CONTENT));
		row->Add(checkbox);

		imageFinder = keyImages.find(i->first);
		Choice *choice;
		if (imageFinder != keyImages.end()) {
			choice = new Choice(keyImages[imageFinder->first], new LinearLayoutParams(1.0f));
		}
		else {
			choice = new Choice(mc->T(i->first.c_str()), new LinearLayoutParams(1.0f));
		}

		ChoiceEventHandler *choiceEventHandler = new ChoiceEventHandler(checkbox);
		choice->OnClick.Handle(choiceEventHandler, &ChoiceEventHandler::onChoiceClick);

		choice->SetCentered(true);

		row->Add(choice);
		grid->Add(row);
		
	}
}

void Combo_keyScreen::onFinish(DialogResult result) {
	switch (*mode){
		case 0:g_Config.cCombokey = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
			break;
		case 1:g_Config.cCombokey1 = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
			break;
		case 2:g_Config.cCombokey2 = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
			break;
		case 3:g_Config.cCombokey3 = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
			break;
		case 4:g_Config.cCombokey4 = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
	}
	g_Config.Save();
}



UI::EventReturn Combo_keyScreen::ChoiceEventHandler::onChoiceClick(UI::EventParams &e){
	checkbox_->Toggle();


	return UI::EVENT_DONE;
};

UI::EventReturn Combo_keyScreen::onCombo(UI::EventParams &e) {
	switch (*mode){
	case 0:g_Config.cCombokey = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
		break;
	case 1:g_Config.cCombokey1 = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
		break;
	case 2:g_Config.cCombokey2 = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
		break;
	case 3:g_Config.cCombokey3 = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
		break;
	case 4:g_Config.cCombokey4 = cComboCircle + cComboCross * 2 + cComboSquare * 4 + cComboTriangle * 8 + cComboLTrigger * 16 + cComboRTrigger * 32 + cComboLeft * 64 + cComboUp * 128 + cComboRight * 256 + cComboDown * 512 + cComboStart * 1024 + cComboSelect * 2048;
	}
	*mode = comboselect->GetSelection();
	CreateViews();
	return UI::EVENT_DONE;
}


