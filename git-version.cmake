set(GIT_VERSION_FILE "${OUTPUT_DIR}/git-version.cpp")
set(GIT_VERSION "1.12.3")
set(GIT_VERSION_UPDATE "1")

find_package(Git)
if(GIT_FOUND AND EXISTS "${SOURCE_DIR}/.git/")
	execute_process(COMMAND ${GIT_EXECUTABLE} describe --always
		WORKING_DIRECTORY ${SOURCE_DIR}
		RESULT_VARIABLE exit_code
		OUTPUT_VARIABLE GIT_VERSION)
	if(NOT ${exit_code} EQUAL 0)
		message(WARNING "git describe failed, using version '${GIT_VERSION}'.")
	endif()
	string(STRIP ${GIT_VERSION} GIT_VERSION)
else()
	message(WARNING "git not found, using version '${GIT_VERSION}'.")
endif()

if(EXISTS ${GIT_VERSION_FILE})
	# Don't update if marked not to update.
	file(STRINGS ${GIT_VERSION_FILE} match
		REGEX "PPSSPP_GIT_VERSION_NO_UPDATE 1")
	if(NOT ${match} EQUAL "")
		set(GIT_VERSION_UPDATE "0")
	endif()
endif()

set(code_string "// This is a generated file.\n\n"
	"const char *PPSSPP_GIT_VERSION = \"${GIT_VERSION}\"\;\n\n"
	"// If you don't want this file to update/recompile, change to 1.\n"
	"#define PPSSPP_GIT_VERSION_NO_UPDATE 0\n")

if ("${GIT_VERSION_UPDATE}" EQUAL "1")
	file(WRITE ${GIT_VERSION_FILE} ${code_string})
endif()
