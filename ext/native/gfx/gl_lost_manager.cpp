#include <vector>
#include <mutex>

#include "base/basictypes.h"
#include "base/logging.h"
#include "gfx/gl_lost_manager.h"

struct Holder {
	GfxResourceHolder *holder;
	const char *desc;
	int priority;
};

static std::mutex mutex;
std::vector<Holder> *holders;

static bool inLost;
static bool inRestore;
static int g_max_priority = 0;

void register_gl_resource_holder(GfxResourceHolder *holder, const char *desc, int priority) {
	std::lock_guard<std::mutex> lock(mutex);
	if (inLost || inRestore) {
		FLOG("BAD: Should not call register_gl_resource_holder from lost/restore path");
		return;
	}
	if (holders) {
		holders->push_back({ holder, desc, priority });
		if (g_max_priority < priority)
			g_max_priority = priority;
	} else {
		WLOG("GL resource holder not initialized, cannot register resource");
	}
}

void unregister_gl_resource_holder(GfxResourceHolder *holder) {
	std::lock_guard<std::mutex> lock(mutex);
	if (inLost || inRestore) {
		FLOG("BAD: Should not call unregister_gl_resource_holder from lost/restore path");
		return;
	}
	if (holders) {
		bool erased = false;
		for (size_t i = 0; i < holders->size(); i++) {
			if ((*holders)[i].holder == holder) {
				if (erased) {
					ELOG("GL object double-registered!");
				}
				holders->erase(holders->begin() + i);
				erased = true;
			}
		}
		if (!erased) {
			WLOG("unregister_gl_resource_holder: Resource not registered");
		}
	} else {
		WLOG("GL resource holder not initialized or already shutdown, cannot unregister resource");
	}
}

void gl_restore() {
	std::lock_guard<std::mutex> lock(mutex);
	inRestore = true;
	if (!holders) {
		WLOG("GL resource holder not initialized, cannot process restore request");
		inRestore = false;
		return;
	}

	ILOG("gl_restore() restoring %d items:", (int)holders->size());
	for (int p = 0; p <= g_max_priority; p++) {
		for (size_t i = 0; i < holders->size(); i++) {
			if ((*holders)[i].priority == p) {
				ILOG("GLRestore(%d / %d, %s, prio %d)", (int)(i + 1), (int)holders->size(),
					 (*holders)[i].desc, (*holders)[i].priority);
				(*holders)[i].holder->GLRestore();
			}
		}
	}
	ILOG("gl_restore() completed on %d items:", (int)holders->size());
	inRestore = false;
}

void gl_lost() {
	std::lock_guard<std::mutex> lock(mutex);
	inLost = true;
	if (!holders) {
		WLOG("GL resource holder not initialized, cannot process restore request");
		inLost = false;
		return;
	}

	ILOG("gl_lost() clearing %i items:", (int)holders->size());
	for (int p = g_max_priority; p >= 0; p--) {
		for (size_t i = 0; i < holders->size(); i++) {
			if ((*holders)[i].priority == p) {
				ILOG("gl_lost(%d / %d, %s, prio %d)", (int) (i + 1), (int) holders->size(),
					 (*holders)[i].desc, (*holders)[i].priority);
				(*holders)[i].holder->GLLost();
			}
		}
	}
	ILOG("gl_lost() completed on %i items:", (int)holders->size());
	inLost = false;
}

void gl_lost_manager_init() {
	std::lock_guard<std::mutex> lock(mutex);
	if (holders) {
		FLOG("Double GL lost manager init");
		// Dead here (FLOG), no need to delete holders
	}
	g_max_priority = 0;
	holders = new std::vector<Holder>();
}

void gl_lost_manager_shutdown() {
	std::lock_guard<std::mutex> lock(mutex);
	if (!holders) {
		FLOG("Lost manager already shutdown");
	} else if (holders->size() > 0) {
		ELOG("Lost manager shutdown with %i objects still registered", (int)holders->size());
		for (size_t i = 0; i < holders->size(); i++) {
			ELOG("  (%d / %d, %s, prio %d)", (int)(i + 1), (int)holders->size(),
				(*holders)[i].desc, (*holders)[i].priority);
		}
	}

	delete holders;
	holders = 0;
}