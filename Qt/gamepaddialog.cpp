#include "gamepaddialog.h"
#include "ui_gamepaddialog.h"
#include <QTimer>
#include "Core/Config.h"

// Input
struct GamePadInfo
{
	int mapping_type; // 0 : pad button, 1 : pad axis, 2 : pad Hats
	int mapping_in;
	int mapping_sign;
	QString ViewLabelName;
	QString Name;
};

// Initial values are PS3 controller
GamePadInfo GamepadPadMapping[] = {
	{0,	14, 0,		"Prev_X",		"Cross"},			//A
	{0,	13, 0,		"Prev_O",		"Circle"},			//B
	{0,	15, 0,		"Prev_S",		"Square"},			//X
	{0,	12, 0,		"Prev_T",		"Triangle"},		//Y
	{0,	10, 0,		"Prev_LT",		"Left Trigger"},	//LBUMPER
	{0,	11, 0,		"Prev_RT",		"Right Trigger"},	//RBUMPER
	{0,	3, 0,		"Prev_Start",	"Start"},			//START
	{0,	0, 0,		"Prev_Select",	"Select"},			//SELECT
	{0,	4, 0,		"Prev_Up",		"Up"},				//UP
	{0,	6, 0,		"Prev_Down",	"Down"},			//DOWN
	{0,	7, 0,		"Prev_Left",	"Left"},			//LEFT
	{0,	5, 0,		"Prev_Right",	"Right"},			//RIGHT
	{0,	0, 0,		""},								//MENU (event)
	{0,	16, 0,		"Prev_Home",	"Home"},			//BACK

	// Special case for analog stick
	{1, 0, -1,		"Prev_ALeft",	"Stick left"},
	{1, 0, 1,		"Prev_ARight",	"Stick right"},
	{1, 1, -1,		"Prev_AUp",		"Stick up"},
	{1, 1, 1,		"Prev_ADown",	"Stick bottom"}
};

// id for mapping in config start at offset 200 to not get over key mapping
const int configOffset = 200;

GamePadDialog::GamePadDialog(InputState* state, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::GamePadDialog),
	m_inputState(state),
#if QT_HAS_SDL
	m_joystick(0),
#endif
	m_isInit(false)
{
	ui->setupUi(this);
	SetViewMode();

#if QT_HAS_SDL
	SDL_Init(SDL_INIT_JOYSTICK);
#endif
	m_isInit = true;

	data_timer = new QTimer();
	data_timer->setInterval(50);
	connect(data_timer,SIGNAL(timeout()),this,SLOT(pollJoystick()));
	data_timer->start();

	for(int i=0;i<18;i++)
	{
		QLabel* labelPreview = findChild<QLabel*>(GamepadPadMapping[i].ViewLabelName);
		if(labelPreview)
		{
			labelPreview->setVisible(false);
		}

		if(g_Config.iMappingMap.find(i+configOffset) != g_Config.iMappingMap.end())
		{
			GetMappingFromInt(g_Config.iMappingMap[i+configOffset],
							  GamepadPadMapping[i].mapping_in,
							  GamepadPadMapping[i].mapping_type,
							  GamepadPadMapping[i].mapping_sign);
		}
	}

	on_refreshListBtn_clicked();
}

GamePadDialog::~GamePadDialog()
{
	data_timer->stop();
	delete data_timer;

#if QT_HAS_SDL
	if(m_joystick)
		SDL_JoystickClose(m_joystick);
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
#endif

	delete ui;
}

void GamePadDialog::on_refreshListBtn_clicked()
{
#if QT_HAS_SDL
	if(m_joystick)
	{
		SDL_JoystickClose(m_joystick);
		ui->JoyName->setText("<b>No GamePad</b>");
		m_joystick = 0;
	}
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	SDL_Init(SDL_INIT_JOYSTICK);

	int numJoy = SDL_NumJoysticks();
	ui->GamePadList->clear();
	for(int i = 0; i < numJoy; i++)
	{
		QListWidgetItem* item = new QListWidgetItem();
		QString padName = SDL_JoystickName(i);
		if(padName == "") padName = "<b>Unknown GamePad</b>";
		item->setText(padName);
		item->setData(Qt::UserRole,i);
		ui->GamePadList->addItem(item);
	}

	if(numJoy > 0)
	{
		ui->GamePadList->setCurrentRow(0);
		on_SelectPadBtn_clicked();
	}

#endif
}

void GamePadDialog::pollJoystick()
{
#if QT_HAS_SDL
	if(!m_joystick)
		return;
	SDL_JoystickUpdate();

	// Update buttons state
	for(int i=0;i<14;i++)
	{
		float val = 0;
		if(GamepadPadMapping[i].mapping_type == 0)
			val = SDL_JoystickGetButton(m_joystick,GamepadPadMapping[i].mapping_in);
		else if(GamepadPadMapping[i].mapping_type == 1)
		{
			val = SDL_JoystickGetAxis(m_joystick,GamepadPadMapping[i].mapping_in);
			if(val*GamepadPadMapping[i].mapping_sign > 16384) val = 1;
			else val = 0;
		}
		else if(GamepadPadMapping[i].mapping_type == 2)
			val = SDL_JoystickGetHat(m_joystick,GamepadPadMapping[i].mapping_in);
		QLabel* labelPreview = findChild<QLabel*>(GamepadPadMapping[i].ViewLabelName);
		if(labelPreview)
		{
			labelPreview->setVisible(val != 0);
		}
		if(val)
		{
			m_inputState->pad_buttons |= (1<<i);
		}
		else
		{
			m_inputState->pad_buttons &= ~(1<<i);
		}
	}
	// Update analog stick
	m_inputState->pad_lstick_x = 0;
	m_inputState->pad_lstick_y = 0;
	for(int i = 14; i < 18; i++)
	{
		float val = 0;
		if(GamepadPadMapping[i].mapping_type == 0)
			val = SDL_JoystickGetButton(m_joystick,GamepadPadMapping[i].mapping_in);
		else if(GamepadPadMapping[i].mapping_type == 1)
		{
			val = SDL_JoystickGetAxis(m_joystick,GamepadPadMapping[i].mapping_in);
			if((val <= 0 && GamepadPadMapping[i].mapping_sign < 0) || (val >= 0 && GamepadPadMapping[i].mapping_sign > 0))
				val = abs(val) * 1.0f / 32767;
			else
				val = 0;
		}
		else if(GamepadPadMapping[i].mapping_type == 2)
			val = SDL_JoystickGetHat(m_joystick,GamepadPadMapping[i].mapping_in);
		QLabel* labelPreview = findChild<QLabel*>(GamepadPadMapping[i].ViewLabelName);
		if(labelPreview)
		{
			labelPreview->setVisible(val != 0);
		}
		switch(i)
		{
		case 14:
			m_inputState->pad_lstick_x -= val;
			break;
		case 15:
			m_inputState->pad_lstick_x += val;
			break;
		case 16:
			m_inputState->pad_lstick_y -= val;
			break;
		default:
			m_inputState->pad_lstick_y += val;
			break;
		}
	}

	if(isVisible())
	{
		for(int i = 0; i < ui->padValues->topLevelItemCount(); i++)
		{
			QTreeWidgetItem* item = ui->padValues->topLevelItem(i);
			for(int j = 0; j < item->childCount(); j++)
			{
				QTreeWidgetItem* item2 = item->child(j);
				if(item2->data(0,Qt::UserRole).toInt() == 0)
				{
					item2->setText(1,QVariant(SDL_JoystickGetButton(m_joystick,item2->data(0,Qt::UserRole+1).toInt())).toString());
				}
				else if(item2->data(0,Qt::UserRole).toInt() == 1)
				{
					int val = SDL_JoystickGetAxis(m_joystick,item2->data(0,Qt::UserRole+1).toInt());
					if((val <= 0 && item2->data(0,Qt::UserRole+2).toInt() < 0) || (val >= 0 && item2->data(0,Qt::UserRole+2).toInt() > 0))
						item2->setText(1,QVariant(val).toString());
				}
				else if(item2->data(0,Qt::UserRole).toInt() == 2)
				{
					item2->setText(1,QVariant(SDL_JoystickGetHat(m_joystick,item2->data(0,Qt::UserRole+1).toInt())).toString());
				}
			}
		}
	}
#endif
}


void GamePadDialog::on_SelectPadBtn_clicked()
{
#if QT_HAS_SDL
	int selectedJoy = -1;
	if(ui->GamePadList->currentItem() == 0)
	{
		return;
	}
	selectedJoy = ui->GamePadList->currentItem()->data(Qt::UserRole).toInt();
	m_joyId = selectedJoy;
	m_joystick = SDL_JoystickOpen(selectedJoy);

	ui->padValues->clear();
	ui->padValues->setColumnCount(3);
	ui->padValues->setColumnWidth(0,100);
	ui->padValues->setColumnWidth(1,50);
	ui->padValues->setColumnWidth(2,50);

	ui->comboPadInput->clear();
	ui->comboPSPButton->clear();

	QTreeWidgetItem* buttonItem = new QTreeWidgetItem();
	buttonItem->setText(0,"Buttons");
	ui->padValues->addTopLevelItem(buttonItem);

	for(int i = 0; i < SDL_JoystickNumButtons(m_joystick); i++)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0,QVariant(i).toString());
		item->setText(1,QVariant(0).toString());
		item->setData(0, Qt::UserRole,0);
		item->setData(0, Qt::UserRole+1,i);
		item->setData(0, Qt::UserRole+2,0);
		buttonItem->addChild(item);

		int id = i << 8;
		ui->comboPadInput->addItem("Button "+QVariant(i).toString(),GetIntFromMapping(i,0,0));
	}
	QTreeWidgetItem* axesItem = new QTreeWidgetItem();
	axesItem->setText(0,"Axes");
	ui->padValues->addTopLevelItem(axesItem);

	for(int i = 0; i < SDL_JoystickNumAxes(m_joystick); i++)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0,QVariant(i).toString()+" Neg");
		item->setText(1,QVariant(0).toString());
		item->setData(0, Qt::UserRole,1);
		item->setData(0, Qt::UserRole+1,i);
		item->setData(0, Qt::UserRole+2,-1);
		axesItem->addChild(item);

		ui->comboPadInput->addItem("Axes "+QVariant(i).toString()+" Neg",GetIntFromMapping(i,1,-1));

		item = new QTreeWidgetItem();
		item->setText(0,QVariant(i).toString()+" Pos");
		item->setText(1,QVariant(0).toString());
		item->setData(0, Qt::UserRole,1);
		item->setData(0, Qt::UserRole+1,i);
		item->setData(0, Qt::UserRole+2,1);
		axesItem->addChild(item);

		ui->comboPadInput->addItem("Axes "+QVariant(i).toString()+" Pos",GetIntFromMapping(i,1,1));
	}

	QTreeWidgetItem* hatsItem = new QTreeWidgetItem();
	hatsItem->setText(0,"Hats");
	ui->padValues->addTopLevelItem(hatsItem);

	for(int i = 0; i < SDL_JoystickNumHats(m_joystick); i++)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0,QVariant(i).toString());
		item->setText(1,QVariant(0).toString());
		item->setData(0, Qt::UserRole,2);
		item->setData(0, Qt::UserRole+1,i);
		item->setData(0, Qt::UserRole+2,0);
		hatsItem->addChild(item);

		ui->comboPadInput->addItem("Button "+QVariant(i).toString(),GetIntFromMapping(i,2,0));
	}

	for(int i = 0; i < 18; i++)
	{
		if(GamepadPadMapping[i].Name != "")
		{
			ui->comboPSPButton->addItem(GamepadPadMapping[i].Name,i);
		}
	}

	SetViewMode();
#endif
}

void GamePadDialog::SetViewMode()
{
#if QT_HAS_SDL
	ui->buttonBox->setEnabled(true);
	ui->refreshListBtn->setEnabled(true);
	ui->SelectPadBtn->setEnabled(true);
	if(!m_joystick)
		ui->JoyName->setText("<b>No GamePad</b>");
	else
		ui->JoyName->setText(QString("<b>Current gamepad : ")+SDL_JoystickName(m_joyId)+"</b>");
#endif
}

void GamePadDialog::on_AssignBtn_clicked()
{
	int idxPad = ui->comboPadInput->currentIndex();
	int idxPSP = ui->comboPSPButton->currentIndex();

	int pspButton = ui->comboPSPButton->itemData(idxPSP).toInt();
	int padInfo = ui->comboPadInput->itemData(idxPad).toInt();

	GetMappingFromInt(padInfo, GamepadPadMapping[pspButton].mapping_in,
					  GamepadPadMapping[pspButton].mapping_type,
					  GamepadPadMapping[pspButton].mapping_sign);
}

int GamePadDialog::GetIntFromMapping(int inputId, int type, int sign)
{
	sign = (sign == -1 ? 1 : (sign == 1 ? 2 : 0));
	return inputId << 8 | sign << 2 | type;
}

void GamePadDialog::GetMappingFromInt(int padInfo, int& inputId, int& type, int& sign)
{
	inputId = padInfo >> 8;
	type = padInfo & 0x3;
	sign = (padInfo >> 2) & 0x3;
	sign = (sign == 1 ? -1 : (sign == 2 ? 1 : 0));
}

void GamePadDialog::on_buttonBox_accepted()
{
	for(int i = 0; i < 18; i++)
	{
		g_Config.iMappingMap[i+configOffset] = GetIntFromMapping(GamepadPadMapping[i].mapping_in,
						  GamepadPadMapping[i].mapping_type,
						  GamepadPadMapping[i].mapping_sign);
	}
}
