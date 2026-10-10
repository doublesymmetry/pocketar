#include "arcore_api.h"
#include <dlfcn.h>

ARCoreApi &arcore_api() {
	static ARCoreApi api;
	return api;
}

bool ARCoreApi::load(std::string &r_error) {
	if (ArSession_create) return true;
	static void *library = dlopen("libarcore_sdk_c.so", RTLD_NOW | RTLD_LOCAL);
	if (!library) {
		r_error = dlerror();
		return false;
	}
#define LOAD_ARCORE(name) \
	name = reinterpret_cast<decltype(name)>(dlsym(library, #name)); \
	if (!name) { r_error = "Missing ARCore 1.56 symbol " #name; ArSession_create = nullptr; return false; }
	ARCORE_FUNCTIONS(LOAD_ARCORE)
#undef LOAD_ARCORE
	return true;
}
