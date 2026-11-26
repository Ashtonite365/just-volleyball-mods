


#include "../models/model_loader.hpp"
#include "../models/model_tab.hpp"
#include "../textures/texture_loader.hpp"
#include "native_audio.hpp"
#include "unified_loader.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>

using namespace godot;
static void init(ModuleInitializationLevel l) {
	if (l != MODULE_INITIALIZATION_LEVEL_SCENE) return;
	GDREGISTER_CLASS(JVTextureLoader);
	GDREGISTER_CLASS(JVModelLoader);
	GDREGISTER_CLASS(JVModelTab);
	GDREGISTER_CLASS(JVLoader);
	GDREGISTER_CLASS(JVMNativeAudio);
}
static void deinit(ModuleInitializationLevel) {}
extern "C" GDExtensionBool GDE_EXPORT jvm_loader_init(GDExtensionInterfaceGetProcAddress p, const GDExtensionClassLibraryPtr lib, GDExtensionInitialization *i) {
	GDExtensionBinding::InitObject o(p, lib, i);
	o.register_initializer(init); o.register_terminator(deinit);
	o.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return o.init();
}
