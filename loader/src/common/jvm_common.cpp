#include "jvm_common.hpp"
#include "jv_prof.hpp"

#include <cstdio>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace jvm {



String install_dir() {
	return OS::get_singleton()->get_executable_path().get_base_dir().path_join("mods");
}

String mods_root() {
	OS *os = OS::get_singleton();

	String env = os->is_debug_build() ? os->get_environment("JV_MODS_DIR") : String();
	if (!env.is_empty()) return env.simplify_path();
	return install_dir();
}

String mods_dir(const String &category) { return mods_root().path_join(category); }






static FILE *log_fp = nullptr;
static void write_log_line(const String &line) {
	JVM_PROF("log.write");
	if (!log_fp) {
		DirAccess::make_dir_recursive_absolute("user://jv_mods");
		String path = "user://jv_mods/jv_mods.log";
		if (ProjectSettings *ps = ProjectSettings::get_singleton())
			path = ps->globalize_path(path);
		log_fp = fopen(path.utf8().get_data(), "a");
		if (!log_fp) return;
	}
	const CharString utf = line.utf8();
	fwrite(utf.get_data(), 1, utf.length(), log_fp);
	fputc('\n', log_fp);
	fflush(log_fp);
}

static String stamp() { return Time::get_singleton()->get_datetime_string_from_system(false, true); }




int log_level(const String &tag) {
	Engine *e = Engine::get_singleton();
	if (!e || !e->has_meta("jv_log_levels")) return 1;
	Variant v = e->get_meta("jv_log_levels");
	if (v.get_type() != Variant::DICTIONARY) return 1;
	Dictionary d = v;
	if (d.has(tag)) return int(d[tag]);
	if (d.has("*")) return int(d["*"]);
	return 1;
}

void debug(const String &tag, const String &msg) {
	if (log_level(tag) > 0) return;
	String line = "[" + stamp() + "] [jv-mods:" + tag + "] DEBUG " + msg;
	UtilityFunctions::print(line);
	write_log_line(line);
}

void log(const String &tag, const String &msg) {
	if (log_level(tag) > 1) return;
	String line = "[" + stamp() + "] [jv-mods:" + tag + "] " + msg;
	UtilityFunctions::print(line);
	write_log_line(line);
}

void warn(const String &tag, const String &msg) {
	if (log_level(tag) > 2) return;
	String line = "[" + stamp() + "] [jv-mods:" + tag + "] WARNING " + msg;
	UtilityFunctions::push_warning(line);
	write_log_line(line);
}

Variant load_json(const String &path) {
	if (!FileAccess::file_exists(path)) return Variant();
	{
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
		if (f.is_null()) return Variant();
		if (f->get_length() > 4u * 1024 * 1024) { warn("common", "JSON file too large (over 4 MB), ignored: " + path); return Variant(); }
	}
	String text = FileAccess::get_file_as_string(path);
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(text) != OK) {
		warn("common", "JSON error in " + path + " line " + String::num_int64(json->get_error_line()) + ": " + json->get_error_message());
		return Variant();
	}
	return json->get_data();
}

PackedStringArray list_json(const String &dir) {
	PackedStringArray out;
	if (!DirAccess::dir_exists_absolute(dir)) return out;
	PackedStringArray files = DirAccess::get_files_at(dir);
	for (int i = 0; i < files.size(); i++)
		if (files[i].to_lower().ends_with(".json")) out.push_back(dir.path_join(files[i]));
	out.sort();
	return out;
}

Dictionary merge(const Dictionary &a, const Dictionary &b) {
	Dictionary out = a.duplicate(true);
	Array keys = b.keys();
	for (int i = 0; i < keys.size(); i++) {
		Variant k = keys[i];
		Variant bv = b[k];
		if (out.has(k) && out[k].get_type() == Variant::DICTIONARY && bv.get_type() == Variant::DICTIONARY)
			out[k] = merge(out[k], bv);
		else
			out[k] = bv;
	}
	return out;
}

Dictionary load_category_config(const String &category) {
	Dictionary cfg;
	PackedStringArray files = list_json(mods_dir(category));
	for (int i = 0; i < files.size(); i++) {
		Variant v = load_json(files[i]);
		if (v.get_type() == Variant::DICTIONARY) {
			cfg = merge(cfg, v);
			log(category, "loaded " + files[i]);
		}
	}
	return cfg;
}

bool script_is(Object *o, const String &cls) {
	if (!o) return false;
	Ref<Script> s = o->get_script();
	for (int depth = 0; s.is_valid() && depth < 16; depth++) {
		if (String(s->get_global_name()) == cls) return true;
		s = s->get_base_script();
	}
	return false;
}

Object *owner_property(Node *from, const String &prop, const String &cls) {
	const StringName pn = prop;
	for (Node *n = from ? from->get_parent() : nullptr; n; n = n->get_parent()) {
		Variant v = n->get(pn);
		if (v.get_type() != Variant::OBJECT) continue;
		Object *o = v;
		if (o && script_is(o, cls)) return o;
	}
	return nullptr;
}

static void collect_script_class(Node *n, const String &cls, TypedArray<Node> &out, int depth) {
	if (!n || depth > 64) return;
	if (script_is(n, cls)) out.push_back(n);
	for (int i = 0; i < n->get_child_count(); i++) collect_script_class(n->get_child(i), cls, out, depth + 1);
}

Node *current_scene(Node *from) {
	if (!from || !from->is_inside_tree()) return nullptr;
	return from->get_tree()->get_current_scene();
}

TypedArray<Node> find_targets(Node *from, const Variant &spec) {
	TypedArray<Node> out;
	if (!from || !from->is_inside_tree() || spec.get_type() != Variant::DICTIONARY) return out;
	Dictionary d = spec;
	if (d.has("path")) {
		Node *n = from->get_tree()->get_root()->get_node_or_null(NodePath(String(d["path"])));
		if (n) out.push_back(n);
	}
	if (d.has("group")) {
		TypedArray<Node> g = from->get_tree()->get_nodes_in_group(StringName(String(d["group"])));
		for (int i = 0; i < g.size(); i++) out.push_back(g[i]);
	}


	if (d.has("script_class")) collect_script_class(current_scene(from), String(d["script_class"]), out, 0);


	if (d.has("scene_property")) {
		Node *sc = current_scene(from);
		if (sc) {
			Variant v = sc->get(StringName(String(d["scene_property"])));
			if (v.get_type() == Variant::OBJECT) {
				Node *n = Object::cast_to<Node>(v.operator Object *());
				if (n && n->is_inside_tree()) out.push_back(n);
			}
		}
	}
	return out;
}

Node *find_gate(Node *from) {
	if (!from || !from->is_inside_tree()) return nullptr;
	Node *g = from->get_tree()->get_root()->get_node_or_null(NodePath("JVModGate"));
	if (!g || !g->has_method("is_active")) return nullptr;
	return g;
}

bool gate_active(Node *from) {
	Node *g = find_gate(from);
	if (!g) return false;
	return bool(g->call("is_active"));
}




bool sync_allowed(Node *from) {
	Node *g = find_gate(from);
	if (!g || !bool(g->call("is_active"))) return false;
	const String m = g->call("get_mode");
	return m == "LAN" || m == "private" || m == "friends-only";
}

String gate_reason(Node *from) {
	Node *g = find_gate(from);
	if (!g) return "no core gate";
	return String(g->call("get_reason"));
}

bool gate_cosmetic_active(Node *from) {
	Node *g = find_gate(from);
	if (!g) return false;
	return bool(g->call("is_cosmetic_active"));
}

}
