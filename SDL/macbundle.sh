#!/bin/bash

echo "Hello from macbundle.sh"

PPSSPP="${1}"
PPSSPP_SHORTNAME="${2}"
CMAKE_BINARY_DIR="${3}"
PPSSPPSDL="${PPSSPP}/Contents/MacOS/${PPSSPP_SHORTNAME}"
MOLTENVK="${PPSSPP}/Contents/Frameworks/libMoltenVK.dylib"

ls ${PPSSPPSDL}

if [ ! -f "${PPSSPPSDL}" ]; then
  echo "No such file: ${PPSSPPSDL}!"
  exit 0
fi

echo pwd=`pwd`
echo PPSSPP=$PPSSPP
echo PPSSPPSDL=$PPSSPPSDL

cd "$(dirname "$0")"
RPATH="$(pwd)/macOS"
cd -
echo RPATH=$RPATH
SDL="${RPATH}/SDL2.framework"
if [ ! -d "${SDL}" ]; then
  echo "Cannot locate SDL.framework: ${SDL}!"
  exit 0
fi

rm -rf "${PPSSPP}/Contents/Frameworks/SDL2.framework" || exit 0
mkdir -p "${PPSSPP}/Contents/Frameworks" || exit 0
cp -a "$SDL" "${PPSSPP}/Contents/Frameworks" || exit 0
echo install_name_tool -rpath "${RPATH}" "@executable_path/../Frameworks" "${PPSSPPSDL}" || echo "Already patched."
install_name_tool -rpath "${RPATH}" "@executable_path/../Frameworks" "${PPSSPPSDL}" || echo "Already patched."

echo "Done."

GIT_VERSION_LINE=$(grep "PPSSPP_GIT_VERSION = " "${CMAKE_BINARY_DIR}/git-version.cpp")

echo "Setting version to '${GIT_VERSION_LINE}'..."
SHORT_VERSION_MATCH='.*"v([0-9]+(\.[0-9]+)*).*";'
LONG_VERSION_MATCH='.*"v(.*)";'
if [[ "${GIT_VERSION_LINE}" =~ ^${SHORT_VERSION_MATCH}$ ]]; then
	plutil -replace CFBundleShortVersionString -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/-/./g; s/${SHORT_VERSION_MATCH}/\$1/g") ${PPSSPP}/Contents/Info.plist
	plutil -replace CFBundleVersion            -string $(echo ${GIT_VERSION_LINE} | perl -pe "s/${LONG_VERSION_MATCH}/\$1/g")  ${PPSSPP}/Contents/Info.plist
else
	plutil -replace CFBundleShortVersionString -string "" ${PPSSPP}/Contents/Info.plist
	plutil -replace CFBundleVersion            -string "" ${PPSSPP}/Contents/Info.plist
fi

# AdHoc codesign is required for Apple Silicon.
echo "Signing..."

codesign -fs - --entitlements ../macOS/Entitlements.plist --timestamp "${MOLTENVK}" || echo "Failed signing MoltenVK"
codesign -fs - --entitlements ../macOS/Entitlements.plist --timestamp "${PPSSPPSDL}" || echo "Failed signing PPSSPPSDL"
codesign -fs - --entitlements ../macOS/Entitlements.plist --timestamp "${PPSSPP}" || echo "Failed signing PPSSPP"
