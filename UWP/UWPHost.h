#pragma once

#include "Core/Host.h"

#include <list>
#include <memory>

#include "Windows/InputDevice.h"

class UWPHost : public Host {
public:
	UWPHost();
	~UWPHost();

	void SetDebugMode(bool mode) override;

	// If returns false, will return a null context
	bool InitGraphics(std::string *error_message, GraphicsContext **ctx) override;
	void PollControllers() override;
	void ShutdownGraphics() override;

	void InitSound() override;
	void UpdateSound() override;
	void ShutdownSound() override;

	bool AttemptLoadSymbolMap() override;
	void SaveSymbolMap() override;
	void SetWindowTitle(const char *message) override;

	void ToggleDebugConsoleVisibility() override;

	void NotifyUserMessage(const std::string &message, float duration = 1.0f, u32 color = 0x00FFFFFF, const char *id = nullptr) override;

	GraphicsContext *GetGraphicsContext() { return nullptr; }

private:
	void SetConsolePosition();
	void UpdateConsolePosition();

	std::list<std::unique_ptr<InputDevice>> input;
};
