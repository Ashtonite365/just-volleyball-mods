#include <functional>
#include <set>
#include "safe_load.hpp"
#include "texture_loader.hpp"
#include "../common/jv_prof.hpp"
#include "../common/accessory_rules.hpp"
#include "../common/jvm_common.hpp"

#include <godot_cpp/classes/bone_attachment3d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/visual_instance3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace sk = jvm::skins;
namespace acc = jvm::acc;

static std::string S(const String &s) { return std::string(s.utf8().get_data()); }
static String G(const std::string &s) { return String::utf8(s.c_str()); }

static const char *kMeta = "jvm_accessory";

static String global_class_path(const String &name) {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (!ps) return String();
	Array list = ps->get_global_class_list();
	for (int i = 0; i < list.size(); i++) {
		if (list[i].get_type() != Variant::DICTIONARY) continue;
		Dictionary d = list[i];
		if (String(d.get("class", "")) == name) return String(d.get("path", ""));
	}
	return String();
}

static Ref<Script> load_global_script(const String &name) {
	String path = global_class_path(name);
	if (path.is_empty()) return Ref<Script>();
	return jvm_textures_io().resource("player.outfit", path);
}





static Object *locker_working_cfg(Node *from) {
	if (!from || !from->get_tree()) return nullptr;
	Node *sc = from->get_tree()->get_current_scene();
	if (!sc) return nullptr;
	String path = sc->get_scene_file_path();
	if (!path.ends_with("locker.tscn") && String(sc->get_name()).to_lower() != "locker")
		return nullptr;
	Variant v = jvm_textures_io().get("player.outfit", sc, "cfg");
	if (v.get_type() != Variant::OBJECT) return nullptr;
	Object *o = v.operator Object *();
	return (o && o->has_method("acc_id")) ? o : nullptr;
}

static Object *local_character(Node *from) {
	if (Object *cfg = locker_working_cfg(from)) return cfg;
	if (!from) return nullptr;
	Node *gc = jvm_textures_io().node("player.outfit", from, "/root/GameConfig");
	if (!gc) return nullptr;
	Variant v = jvm_textures_io().get("player.outfit", gc, "character");
	return v.get_type() == Variant::OBJECT ? v.operator Object *() : nullptr;
}

static Object *character_of(Node *player, Node *fallback_host) {
	if (Object *cfg = locker_working_cfg(fallback_host ? fallback_host : player)) return cfg;
	if (player) {
		Variant v = jvm_textures_io().get("player.outfit", player, "character");
		if (v.get_type() == Variant::OBJECT) return v.operator Object *();
	}
	return local_character(fallback_host);
}

static String acc_id_of(Object *ch, const String &slot) {
	if (!ch || !ch->has_method("acc_id")) return String();
	return String(jvm_textures_io().call("player.outfit", ch, "acc_id", slot));
}

static void set_acc_id(Object *ch, const String &slot, const String &id) {
	if (!ch || !ch->has_method("set_acc_id")) return;
	jvm_textures_io().call("player.outfit", ch, "set_acc_id", slot, id);
}

static std::map<std::string, std::string> read_slots(Object *ch) {
	std::map<std::string, std::string> out;
	if (!ch) return out;
	for (const char *cat : { acc::kHead, acc::kFace, acc::kExtra }) {
		for (int n = 0; n < acc::kSlotsPerCategory; n++) {
			String slot = G(acc::slot_name(cat, n));
			String id = acc_id_of(ch, slot);
			if (!id.is_empty()) out[S(slot)] = S(id);
			else out[S(slot)] = "";
		}
	}
	return out;
}

static std::vector<std::string> worn_from_slots(const std::map<std::string, std::string> &slots) {
	std::vector<std::string> out;
	for (const auto &kv : slots) if (!kv.second.empty()) out.push_back(kv.second);
	return out;
}

Ref<PackedScene> JVTextureLoader::pack_accessory_scene(const AccessorySpec &spec, const String &id) {
	if (!spec.present || spec.model_path.is_empty()) return Ref<PackedScene>();
	auto it = acc_scenes_.find(S(id));
	if (it != acc_scenes_.end() && it->second.path == spec.model_path && it->second.scene.is_valid())
		return it->second.scene;
	String why;
	Node *gen = jvm::safe::load_glb(spec.model_path, why);
	if (!gen) {
		jvm::safe::note_refusal("locker accessory mesh " + spec.model_path + ": " + why);
		return Ref<PackedScene>();
	}
	Node3D *root = Object::cast_to<Node3D>(gen);
	if (!root) {
		root = memnew(Node3D);
		root->add_child(gen);
	}
	root->set_name("JvmAcc_" + id);
	root->set_meta(kMeta, true);
	root->set_meta("jvm_acc_id", id);
	if (spec.shader_mode == "toon") jvm_convert_gltf_materials_to_toon(root);



	std::function<void(Node *, Node *)> own = [&](Node *n, Node *o) {
		if (n != o) n->set_owner(o);
		for (int i = 0; i < n->get_child_count(); i++) own(n->get_child(i), o);
	};
	own(root, root);
	Ref<PackedScene> ps;
	ps.instantiate();
	Error perr = ps->pack(root);
	root->queue_free();
	if (perr != OK || ps.is_null()) {
		jvm::warn("textures", "locker accessory pack() failed for " + id);
		return Ref<PackedScene>();
	}
	acc_scenes_[S(id)] = { spec.model_path, ps };
	return ps;
}

Ref<Resource> JVTextureLoader::make_accessory_item(const String &id, const String &name,
		const String &category, const AccessorySpec &spec) {
	Ref<Resource> item;
	Ref<Script> item_script = load_global_script("AccessoryItem");
	if (item_script.is_valid() && item_script->can_instantiate()) {
		Variant inst = jvm_textures_io().call("player.outfit", item_script.ptr(), "new");
		if (inst.get_type() == Variant::OBJECT) {
			if (Resource *res = Object::cast_to<Resource>(inst.operator Object *())) item = Ref<Resource>(res);
		}
	}
	if (item.is_null()) {
		jvm::warn("textures", "AccessoryItem is not registered; cannot inject " + id + " into the locker");
		return item;
	}
	jvm_textures_io().set("player.outfit", item.ptr(), "id", id);
	jvm_textures_io().set("player.outfit", item.ptr(), "display_name", name.is_empty() ? id : name);
	jvm_textures_io().set("player.outfit", item.ptr(), "category", category);
	jvm_textures_io().set("player.outfit", item.ptr(), "mount", spec.mount.is_empty() ? String(category == "extra" ? "chest" : "head") : spec.mount);
	jvm_textures_io().set("player.outfit", item.ptr(), "anchor_offset", spec.offset);
	jvm_textures_io().set("player.outfit", item.ptr(), "anchor_rotation", spec.rotation_deg);
	jvm_textures_io().set("player.outfit", item.ptr(), "anchor_scale", spec.scale.x != 0.0f ? spec.scale.x : 1.0f);
	jvm_textures_io().set("player.outfit", item.ptr(), "price", 0);
	jvm_textures_io().set("player.outfit", item.ptr(), "supporter", false);
	if (spec.hide_vanilla_hats) jvm_textures_io().set("player.outfit", item.ptr(), "hair_mode", "top_covered");
	if (category == "extra" || spec.mount == "chest" || spec.mount == "hips") {
		jvm_textures_io().set("player.outfit", item.ptr(), "worn_on_back", true);
		jvm_textures_io().set("player.outfit", item.ptr(), "portrait", "extra");
		jvm_textures_io().set("player.outfit", item.ptr(), "turns_back", true);
	} else if (category == "head") {
		jvm_textures_io().set("player.outfit", item.ptr(), "portrait", "hat");
	} else {
		jvm_textures_io().set("player.outfit", item.ptr(), "portrait", "head");
	}
	if (spec.jiggle) {
		jvm_textures_io().set("player.outfit", item.ptr(), "jiggle_stiffness", spec.stiffness);
		jvm_textures_io().set("player.outfit", item.ptr(), "jiggle_damping", spec.drag);
		jvm_textures_io().set("player.outfit", item.ptr(), "jiggle_gravity", spec.gravity);
	}
	Ref<PackedScene> mesh = pack_accessory_scene(spec, id);
	if (mesh.is_valid()) jvm_textures_io().set("player.outfit", item.ptr(), "mesh", mesh);
	item->set_meta(kMeta, true);
	item->set_meta("jvm_acc_id", id);
	return item;
}

bool JVTextureLoader::inject_catalog(const Array &items) {
	if (global_class_path("AccessoryCatalog").is_empty()) {
		jvm::warn("textures", "AccessoryCatalog is not a global class; locker injection skipped");
		return false;
	}
	Ref<GDScript> scr;
	scr.instantiate();
	scr->set_source_code(
		"extends RefCounted\n"
		"func run(items):\n"
		"	AccessoryCatalog.all()\n"
		"	var kept: Array = []\n"
		"	var dropped := 0\n"
		"	for it in AccessoryCatalog._cache:\n"
		"		if it != null and it.has_meta('jvm_accessory'):\n"
		"			dropped += 1\n"
		"		else:\n"
		"			kept.append(it)\n"
		"	var added := 0\n"
		"	for it in items:\n"
		"		if it == null: continue\n"
		"		var skip := false\n"
		"		for k in kept:\n"
		"			if k != null and String(k.get('id')) == String(it.get('id')):\n"
		"				skip = true\n"
		"				break\n"
		"		if skip: continue\n"
		"		kept.append(it)\n"
		"		added += 1\n"
		"	AccessoryCatalog._cache = kept\n"
		"	return { 'total': kept.size(), 'added': added, 'dropped': dropped }\n");
	if (scr->reload() != OK) {
		jvm::warn("textures", "locker inject script failed to compile");
		return false;
	}
	Ref<RefCounted> holder;
	holder.instantiate();
	if (holder.is_null()) return false;
	holder->set_script(scr);
	Variant r = jvm_textures_io().call("player.outfit", holder.ptr(), "run", items);
	if (r.get_type() != Variant::DICTIONARY) return false;
	Dictionary d = r;
	catalog_injected_ = int(d.get("added", 0));
	catalog_total_ = int(d.get("total", 0));
	jvm::log("textures", "locker: injected " + String::num_int64(catalog_injected_) +
			" accessor(y/ies) into AccessoryCatalog (cache " + String::num_int64(catalog_total_) + ")");
	return catalog_injected_ >= 0;
}

std::set<std::string> JVTextureLoader::locker_accessory_ids() const {
	std::set<std::string> out;
	for (const auto &e : acc_entries_) out.insert(e.id);
	return out;
}

bool JVTextureLoader::is_locker_accessory(sk::SkinId id) const {
	return locker_accessory_ids().count(S(G(sk::format_id(id)))) != 0;
}

const JVTextureLoader::Skin *JVTextureLoader::skin_by_entry(const std::string &entry_id) const {

	auto exact = sk::parse_id(entry_id);
	if (exact && skins_.count(*exact)) return &skins_.at(*exact);
	auto us = entry_id.rfind('_');
	if (us != std::string::npos) {
		auto base = sk::parse_id(entry_id.substr(0, us));
		if (base && skins_.count(*base)) return &skins_.at(*base);
	}
	return nullptr;
}

const JVTextureLoader::AccessorySpec *JVTextureLoader::spec_for_entry(const AccRuntimeEntry &e) const {
	const Skin *s = skin_by_entry(e.id);
	if (!s) return nullptr;
	if (e.part == "hat") return s->hat.present ? &s->hat : nullptr;
	if (e.part == "face") return s->face.present ? &s->face : nullptr;
	if (e.part == "back") return s->back.present ? &s->back : nullptr;
	return nullptr;
}

void JVTextureLoader::rebuild_acc_entries() {
	acc_entries_.clear();
	unplaced_acc_.clear();
	for (auto id : load_order_) {
		const Skin &s = skins_[id];
		std::string pack = S(G(sk::format_id(id)));
		auto ents = acc::entries_for_pack(pack, s.hat.present, s.face.present, s.back.present,
				S(s.category), S(s.type));
		for (const auto &e : ents) acc_entries_.push_back({ e.id, e.category, e.part, pack });
	}
}

void JVTextureLoader::save_equipped() const {
	String path = jvm::mods_dir(texture_pack_rel()).path_join("equipped.json");
	Dictionary d;
	Variant cur = jvm::load_json(path);
	if (cur.get_type() == Variant::DICTIONARY) d = cur;
	Array a;
	for (auto id : equipped_) a.push_back(G(sk::format_id(id)));
	d["equipped"] = a;
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null()) { jvm::warn("textures", "could not write " + path); return; }
	f->store_string(JSON::stringify(d, "\t"));
}

void JVTextureLoader::migrate_and_sync_character() {
	JVM_PROF("textures.character_sync");
	Object *ch = local_character(this);
	if (!ch) return;
	std::map<std::string, std::string> cats;
	for (const auto &e : acc_entries_) cats[e.id] = e.category;
	std::vector<std::string> eq;
	for (auto id : equipped_) eq.push_back(S(G(sk::format_id(id))));
	auto ours = locker_accessory_ids();
	auto slots = read_slots(ch);
	auto collect_worn = [&]() {
		std::vector<std::string> worn_ours;
		for (const auto &id : worn_from_slots(slots)) if (ours.count(id)) worn_ours.push_back(id);
		return worn_ours;
	};
	auto worn_ours = collect_worn();



	const bool locker_changed = accessories_migrated_ && worn_ours != last_worn_acc_;
	if (!accessories_migrated_ || !locker_changed) {
		if (!accessories_migrated_) {
			auto m = acc::migrate(slots, eq, cats);
			unplaced_acc_ = m.unplaced;
			if (!m.set_slots.empty()) {
				for (const auto &kv : m.set_slots) set_acc_id(ch, G(kv.first), G(kv.second));
				if (ch->has_method("save_local")) jvm_textures_io().call("player.outfit", ch, "save_local");
				Array set;
				for (const auto &kv : m.set_slots) set.push_back(G(kv.first) + "=" + G(kv.second));
				jvm::log("textures", "locker: migrated equipped accessories into empty slots: " + String(", ").join(PackedStringArray(set)));
				slots = read_slots(ch);
				worn_ours = collect_worn();
			}
			accessories_migrated_ = true;
		}
	} else {
		unplaced_acc_.clear();


		if (Node *gc = jvm_textures_io().autoload("player.outfit", "GameConfig")) {
			Variant gv = jvm_textures_io().get("player.outfit", gc, "character");
			Object *saved = gv.get_type() == Variant::OBJECT ? gv.operator Object *() : nullptr;
			if (saved && saved != ch) {
				for (const auto &kv : slots) set_acc_id(saved, G(kv.first), G(kv.second));
				if (saved->has_method("save_local")) jvm_textures_io().call("player.outfit", saved, "save_local");
			}
		}
	}
	auto next = acc::sync_equipped(eq, worn_ours, ours);
	bool changed = next.size() != eq.size();
	if (!changed) {
		for (size_t i = 0; i < next.size(); i++) if (next[i] != eq[i]) { changed = true; break; }
	}
	if (changed) {
		equipped_.clear();
		for (const auto &id : next) {
			auto pid = sk::parse_id(id);
			if (pid && skins_.count(*pid) && equipped_.size() < sk::kMaxIds) equipped_.push_back(*pid);
		}
		save_equipped();
	}
	last_worn_acc_ = worn_ours;
}




void JVTextureLoader::apply_worn_locker_accessories(Node *player, int32_t peer) {






	if (!player) return;
	Object *ch = character_of(player, this);
	if (!ch) ch = local_character(this);
	if (!ch) return;
	auto slots = read_slots(ch);
	auto ours = locker_accessory_ids();
	String mid = jvm_model_id_worn_by(player);
	Dictionary game_roots, game_ids;
	if (Node *rig = jvm_find_model_rig(player)) {
		Variant r = jvm_textures_io().get("player.outfit", rig, "_acc_roots");
		Variant i = jvm_textures_io().get("player.outfit", rig, "_acc_ids");
		if (r.get_type() == Variant::DICTIONARY) game_roots = r;
		if (i.get_type() == Variant::DICTIONARY) game_ids = i;
	}
	auto game_owns = [&](const std::string &slot, const std::string &id) {
		String gs = G(slot);
		if (!game_roots.has(gs) || String(game_ids.get(gs, "")) != G(id)) return false;
		Node3D *root = Object::cast_to<Node3D>(game_roots[gs]);
		return root && root->is_inside_tree() && !root->is_queued_for_deletion();
	};

	std::vector<std::string> need;
	std::vector<std::string> worn;
	for (const auto &kv : slots) {
		if (!ours.count(kv.second)) continue;
		worn.push_back(kv.first + "=" + kv.second);
		if (!game_owns(kv.first, kv.second)) need.push_back(kv.first + "=" + kv.second);
	}
	uint64_t pid = player->get_instance_id();
	auto &st = applied_locker_[peer];
	auto tag_of = [](const std::string &pair) {
		size_t eq = pair.find('=');
		return "Worn_" + G(pair.substr(eq + 1)) + "_" + G(pair.substr(0, eq));
	};
	bool worn_visible = true;
	for (const auto &pr : need) {
		bool found = false;
		for (const auto &sn : spawned_nodes_) {
			if (sn.owner_peer != peer) continue;
			Node3D *n = Object::cast_to<Node3D>(ObjectDB::get_instance(sn.node_id));
			if (n && n->is_inside_tree() && String(n->get_name()) == "JVM_" + tag_of(pr) && n->is_visible_in_tree()) { found = true; break; }
		}
		if (!found) { worn_visible = false; break; }
	}
	pose_game_accessory_roots(player);
	if (st.player_id == pid && st.model_id == S(mid) && st.ids == need && player_overrides_intact(peer) && worn_visible)
		return;

	for (auto it = spawned_nodes_.begin(); it != spawned_nodes_.end(); ) {
		if (it->owner_peer != peer) { ++it; continue; }
		Node *n = Object::cast_to<Node>(ObjectDB::get_instance(it->node_id));
		if (n && (String(n->get_name()).begins_with("JVM_Worn_") || String(n->get_name()).begins_with("JVM_Mount_Worn_"))) {
			n->queue_free();
			it = spawned_nodes_.erase(it);
			continue;
		}
		++it;
	}
	st.player_id = pid;
	st.model_id = S(mid);
	st.ids = need;
	for (const auto &pr : need) {
		std::string id = pr.substr(pr.find('=') + 1);
		const AccRuntimeEntry *ent = nullptr;
		for (const auto &e : acc_entries_) if (e.id == id) { ent = &e; break; }
		const AccessorySpec *spec = ent ? spec_for_entry(*ent) : nullptr;
		if (spec) apply_accessory_to(player, peer, *spec, tag_of(pr), mid);
	}
	(void)worn;
}

void JVTextureLoader::sync_locker_accessories() {
	rebuild_acc_entries();
	Array items;
	for (const auto &e : acc_entries_) {
		const Skin *s = skin_by_entry(e.id);
		const AccessorySpec *spec = spec_for_entry(e);
		if (!s || !spec) continue;
		Ref<Resource> item = make_accessory_item(G(e.id), s->name, G(e.category), *spec);
		if (item.is_valid()) items.append(item);
	}
	accessories_injected_ = inject_catalog(items);
	accessories_migrated_ = false;
	if (!accessories_injected_ && !acc_entries_.empty()) {

		unplaced_acc_.clear();
		for (const auto &e : acc_entries_) unplaced_acc_.push_back(e.id);
	} else {
		migrate_and_sync_character();
	}
	invalidate_status_cache();
}

Dictionary JVTextureLoader::get_accessory_status() const {
	Dictionary d;
	d["injected"] = accessories_injected_;
	d["catalog_added"] = int64_t(catalog_injected_);
	d["catalog_total"] = int64_t(catalog_total_);
	Array ents;
	for (const auto &e : acc_entries_) {
		Dictionary one;
		one["id"] = G(e.id);
		one["category"] = G(e.category);
		one["part"] = G(e.part);
		one["pack"] = G(e.pack);
		ents.append(one);
	}
	d["entries"] = ents;
	Array worn;
	for (const auto &id : last_worn_acc_) worn.append(G(id));
	d["worn"] = worn;
	Array unp;
	for (const auto &id : unplaced_acc_) unp.append(G(id));
	d["unplaced"] = unp;
	return d;
}
