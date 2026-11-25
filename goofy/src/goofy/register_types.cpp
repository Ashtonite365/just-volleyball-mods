#include "goofy_mods.hpp"
#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>
using namespace godot;
static void init(ModuleInitializationLevel l) { if (l == MODULE_INITIALIZATION_LEVEL_SCENE) GDREGISTER_CLASS(JVGoofyMods); }
static void deinit(ModuleInitializationLevel) {}
extern "C" GDExtensionBool GDE_EXPORT jvm_goofy_init(GDExtensionInterfaceGetProcAddress p, const GDExtensionClassLibraryPtr lib, GDExtensionInitialization *i) {
	GDExtensionBinding::InitObject o(p, lib, i);
	o.register_initializer(init); o.register_terminator(deinit);
	o.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return o.init();
}
