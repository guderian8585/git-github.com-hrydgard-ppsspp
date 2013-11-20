DEFINES += USING_QT_UI USE_FFMPEG
unix:!qnx:!symbian:!macx: CONFIG += linux

# Global specific
win32:CONFIG(release, debug|release): CONFIG_DIR = $$join(OUT_PWD,,,/release)
else:win32:CONFIG(debug, debug|release): CONFIG_DIR = $$join(OUT_PWD,,,/debug)
else:CONFIG_DIR=$$OUT_PWD
OBJECTS_DIR = $$CONFIG_DIR/.obj/$$TARGET
MOC_DIR = $$CONFIG_DIR/.moc/$$TARGET
UI_DIR = $$CONFIG_DIR/.ui/$$TARGET
INCLUDEPATH += ../ext/zlib ../native/ext/glew ../Common

win32-msvc* {
	QMAKE_CXXFLAGS_RELEASE += /O2 /arch:SSE2 /fp:fast
	DEFINES += _MBCS GLEW_STATIC _CRT_SECURE_NO_WARNINGS
	contains(DEFINES,UNICODE):DEFINES+=_UNICODE
	PRECOMPILED_HEADER = ../Windows/stdafx.h
	PRECOMPILED_SOURCE = ../Windows/stdafx.cpp
	INCLUDEPATH += .. ../ffmpeg/Windows/$${QMAKE_TARGET.arch}/include
} else {
	DEFINES += __STDC_CONSTANT_MACROS
	QMAKE_CXXFLAGS += -Wno-unused-function -Wno-unused-variable -Wno-multichar -Wno-uninitialized -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-unused-parameter
	QMAKE_CXXFLAGS += -ffast-math -fno-strict-aliasing
	contains(MEEGO_EDITION,harmattan): QMAKE_CXXFLAGS += -std=gnu++0x
	else: QMAKE_CXXFLAGS += -std=c++0x
	QMAKE_CFLAGS_RELEASE -= -O2
	QMAKE_CFLAGS_RELEASE += -O3
	QMAKE_CXXFLAGS_RELEASE -= -O2
	QMAKE_CXXFLAGS_RELEASE += -O3
}
# Arch specific
xarch = $$find(QT_ARCH, "86")
contains(QT_ARCH, windows)|count(xarch, 1) {
	!win32-msvc*: QMAKE_CXXFLAGS += -msse2
	CONFIG += x86
}
else { # Assume ARM
	DEFINES += ARM
	CONFIG += arm
	CONFIG += mobile_platform
}

gleslib = $$lower($$QMAKE_LIBS_OPENGL)
gleslib = $$find(gleslib, "gles")
contains(MEEGO_EDITION,harmattan)|!count(gleslib,0) {
	DEFINES += USING_GLES2
}

# Platform specific
contains(MEEGO_EDITION,harmattan) {
	# Does not yet support FFMPEG
	DEFINES -= USE_FFMPEG
	DEFINES += MEEGO_EDITION_HARMATTAN "_SYS_UCONTEXT_H=1"
}

macx:!mobile_platform {
	INCLUDEPATH += ../ffmpeg/macosx/x86_64/include
	#the qlist headers include <initializer_list> in QT5
	greaterThan(QT_MAJOR_VERSION,4):CONFIG+=c++11
}

linux:!mobile_platform {
	contains(QT_ARCH, x86_64): QMAKE_TARGET.arch = x86_64
	else: QMAKE_TARGET.arch = x86
	INCLUDEPATH += ../ffmpeg/linux/$${QMAKE_TARGET.arch}/include
}
linux:mobile_platform: INCLUDEPATH += ../ffmpeg/linux/arm/include
qnx {
	# Use mkspec: unsupported/qws/qnx-armv7-g++
	DEFINES += BLACKBERRY "_QNX_SOURCE=1" "_C99=1"
	INCLUDEPATH += ../ffmpeg/blackberry/armv7/include
}
symbian {
	# Does not seem to be a way to change to armv6 compile so just override in variants.xml (see README)
	DEFINES += "BOOST_COMPILER_CONFIG=\"$$EPOCROOT/epoc32/include/stdapis/boost/mpl/aux_/config/gcc.hpp\""
	QMAKE_CXXFLAGS += -marm -Wno-parentheses -Wno-comment
	INCLUDEPATH += $$EPOCROOT/epoc32/include/stdapis
	INCLUDEPATH += ../ffmpeg/symbian/armv6/include
}
