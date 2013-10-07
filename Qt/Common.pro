QT += opengl
QT -= gui
TARGET = Common
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

arm {
	SOURCES += ../Common/ArmCPUDetect.cpp \
		../Common/ArmEmitter.cpp \
		../Common/ArmThunk.cpp
	HEADERS += ../Common/ArmEmitter.h
}
x86 {
	SOURCES += ../Common/ABI.cpp \
		../Common/CPUDetect.cpp \
		../Common/Thunk.cpp \
		../Common/x64Analyzer.cpp \
		../Common/x64Emitter.cpp
	HEADERS +=  ../Common/ABI.h \
		../Common/CPUDetect.h \
		../Common/Thunk.h \
		../Common/x64Analyzer.h \
		../Common/x64Emitter.h
}
win32 {
	SOURCES += ../Common/stdafx.cpp
	HEADERS += ../Common/stdafx.h
}

SOURCES += ../Common/ChunkFile.cpp \
	../Common/ConsoleListener.cpp \
	../Common/FileUtil.cpp \
	../Common/LogManager.cpp \
	../Common/KeyMap.cpp \
	../Common/MathUtil.cpp \
	../Common/MemArena.cpp \
	../Common/MemoryUtil.cpp \
	../Common/Misc.cpp \
	../Common/MsgHandler.cpp \
	../Common/StringUtils.cpp \
	../Common/Thread.cpp \
	../Common/ThreadPools.cpp \
	../Common/Timer.cpp \
	../Common/Crypto/*.cpp
HEADERS += ../Common/ChunkFile.h \
	../Common/ConsoleListener.h \
	../Common/FileUtil.h \
	../Common/LogManager.h \
	../Common/KeyMap.h \
	../Common/MathUtil.h \
	../Common/MemArena.h \
	../Common/MemoryUtil.h \
	../Common/MsgHandler.h \
	../Common/StringUtils.h \
	../Common/Thread.h \
	../Common/ThreadPools.h \
	../Common/Timer.h \
	../Common/Crypto/*.h

INCLUDEPATH += ../native

