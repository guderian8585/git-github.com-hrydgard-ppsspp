// TODO: Move much of this code to vfs.cpp
#pragma once

#ifdef __ANDROID__
#include <zip.h>
#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#endif

#include <string.h>
#include <string>

#include "base/basictypes.h"
#include "file/vfs.h"
#include "file/file_util.h"

// Direct readers. deallocate using delete [].
uint8_t *ReadLocalFile(const char *filename, size_t *size);

class AssetReader {
public:
	virtual ~AssetReader() {}
	// use delete[]
	virtual uint8_t *ReadAsset(const char *path, size_t *size) = 0;
	// Filter support is optional but nice to have
	virtual bool GetFileListing(const char *path, std::vector<FileInfo> *listing, const char *filter = 0) = 0;
	virtual bool GetFileInfo(const char *path, FileInfo *info) = 0;
	virtual std::string toString() const = 0;
};

#ifdef USING_QT_UI
class AssetsAssetReader : public AssetReader {
public:
	AssetsAssetReader() {}
	~AssetsAssetReader() {}
	// use delete[]
	virtual uint8_t *ReadAsset(const char *path, size_t *size);
	virtual bool GetFileListing(const char *path, std::vector<FileInfo> *listing, const char *filter);
	virtual bool GetFileInfo(const char *path, FileInfo *info);
	virtual std::string toString() const {
		return ":assets/";
	}
};
#endif

#ifdef __ANDROID__
uint8_t *ReadFromZip(zip *archive, const char* filename, size_t *size);

// Deprecated - use AAssetReader instead.
class ZipAssetReader : public AssetReader {
public:
	ZipAssetReader(const char *zip_file, const char *in_zip_path);
	~ZipAssetReader();
	// use delete[]
	virtual uint8_t *ReadAsset(const char *path, size_t *size);
	virtual bool GetFileListing(const char *path, std::vector<FileInfo> *listing, const char *filter);
	virtual bool GetFileInfo(const char *path, FileInfo *info);
	virtual std::string toString() const {
		return in_zip_path_;
	}

private:
	zip *zip_file_;
	char in_zip_path_[256];
};

class AAssetReader : public AssetReader {
public:
	AAssetReader(JNIEnv *jni, jobject assetManager);
	~AAssetReader();
	// use delete[]
	virtual uint8_t *ReadAsset(const char *path, size_t *size);
	virtual bool GetFileListing(const char *path, std::vector<FileInfo> *listing, const char *filter);
	virtual bool GetFileInfo(const char *path, FileInfo *info);
	virtual std::string toString() const {
		return "";
	}
private:
	AAssetManager *mgr_;
};

#endif

class DirectoryAssetReader : public AssetReader {
public:
	DirectoryAssetReader(const char *path) {
		strncpy(path_, path, ARRAY_SIZE(path_));
		path_[ARRAY_SIZE(path_) - 1] = '\0';
	}
	// use delete[]
	virtual uint8_t *ReadAsset(const char *path, size_t *size);
	virtual bool GetFileListing(const char *path, std::vector<FileInfo> *listing, const char *filter);
	virtual bool GetFileInfo(const char *path, FileInfo *info);
	virtual std::string toString() const {
		return path_;
	}

private:
	char path_[512];
};

