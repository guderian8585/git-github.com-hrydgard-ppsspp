TARGET = PPSSPPQt
QT += core gui opengl

include(Settings.pri)
linux {
	CONFIG += mobility
	MOBILITY += multimedia
}
else {
	QT += multimedia
}

# Libs
symbian: LIBS += -lCore.lib -lCommon.lib -lNative.lib -lcone -leikcore -lavkon -lezlib

blackberry: LIBS += -L. -lCore -lCommon -lNative -lscreen -lsocket -lstdc++

win32: LIBS += -L. -lCore -lCommon -lNative -lwinmm -lws2_32 -lkernel32 -luser32 -lgdi32 -lshell32 -lcomctl32 -ldsound -lxinput

linux: LIBS += -L. -lCore -lCommon -lNative

# Main
SOURCES += ../native/base/QtMain.cpp
HEADERS += ../native/base/QtMain.h

# Native
SOURCES += ../android/jni/EmuScreen.cpp \
	../android/jni/MenuScreens.cpp \
	../android/jni/GamepadEmu.cpp \
	../android/jni/UIShader.cpp \
	../android/jni/ui_atlas.cpp

INCLUDEPATH += .. ../Common ../native

# Temporarily only use new UI for Linux desktop
linux:!mobile_platform {
	SOURCES += mainwindow.cpp \
		debugger_disasm.cpp \
		EmuThread.cpp\
		QtHost.cpp \
		qtemugl.cpp \
		ctrldisasmview.cpp \
		ctrlregisterlist.cpp \
		controls.cpp
	HEADERS += mainwindow.h \
		debugger_disasm.h \
		EmuThread.h \
		QtHost.h \
		qtemugl.h \
		ctrldisasmview.h \
		ctrlregisterlist.h \
		controls.h
} else {
	SOURCES += ../android/jni/NativeApp.cpp
}

# Packaging
symbian {
	vendorinfo = "%{\"Qtness\"}" ":\"Qtness\""
	packageheader = "$${LITERAL_HASH}{\"PPSSPP\"}, (0xE0095B1D), 0, 0, 5, TYPE=SA"
	my_deployment.pkg_prerules = packageheader vendorinfo
	assets.sources = ../android/assets/ui_atlas.zim ../android/assets/ppge_atlas.zim
	assets.path = E:/PPSSPP
	DEPLOYMENT += my_deployment assets
	ICON = ../assets/icon.svg
# 268MB maximum
	TARGET.EPOCHEAPSIZE = 0x40000 0x10000000
	TARGET.EPOCSTACKSIZE = 0x10000
}

linux:!mobile_platform {
	FORMS += mainwindow.ui \
	debugger_disasm.ui \
	controls.ui
}
