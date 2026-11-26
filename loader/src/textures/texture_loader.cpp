#include <godot_cpp/classes/time.hpp>
#include "texture_loader.hpp"
#include "../common/display_avatar.hpp"
#include "safe_load.hpp"
#include <godot_cpp/classes/script.hpp>
#include "gate/safe_content.hpp"
#include "accessory_anchor.hpp"
#include "../common/jv_prof.hpp"
#include "models/model_loader.hpp"
#include "models/content_paths.hpp"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/bone_attachment3d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include "custom_ball.hpp"
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/primitive_mesh.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_multiplayer.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/spring_bone_collision_capsule3d.hpp>
#include <godot_cpp/classes/spring_bone_simulator3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/window.hpp>

#include <cmath>
#include <functional>
#include <cstring>

using namespace godot;
namespace sk = jvm::skins;

static std::string S(const String &s) { return std::string(s.utf8().get_data()); }
static String G(const std::string &s) { return String::utf8(s.c_str()); }








static int s3tc_state = -1;
static int s3tc_warnings = 0;

static bool ensure_pack_mips(const Ref<Image> &img) {
	JVM_PROF("textures.image_mips_compress");
	if (img.is_null() || img->is_empty()) return false;
	if (!img->has_mipmaps()) {
		if (img->is_compressed() && img->decompress() != OK) {
			jvm::warn("textures", "cannot decompress image to build mipmaps");
			return false;
		}
		if (img->generate_mipmaps() != OK) {
			jvm::warn("textures", "generate_mipmaps failed");
			return img->has_mipmaps();
		}
	}




	String driver;
	if (RenderingServer::get_singleton() && RenderingServer::get_singleton()->has_method("get_current_rendering_driver_name"))
		driver = RenderingServer::get_singleton()->get_current_rendering_driver_name();
	if (driver == "d3d12") s3tc_state = 0;
	if (!img->is_compressed() && s3tc_state != 0) {
		if (img->compress(Image::COMPRESS_S3TC, Image::COMPRESS_SOURCE_SRGB) == OK)
			s3tc_state = 1;
		else {
			s3tc_state = 0;
			if (s3tc_warnings == 0) {
				s3tc_warnings = 1;
				jvm::warn("textures", "S3TC compress failed; leaving uncompressed with mipmaps (further attempts skipped)");
			}
		}
	}
	return img->has_mipmaps();
}

static void upgrade_material_filter(const Ref<BaseMaterial3D> &bm) {
	if (bm.is_null()) return;
	switch (bm->get_texture_filter()) {
		case BaseMaterial3D::TEXTURE_FILTER_NEAREST:
		case BaseMaterial3D::TEXTURE_FILTER_LINEAR:
		case BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS:
			bm->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
			break;
		case BaseMaterial3D::TEXTURE_FILTER_NEAREST_WITH_MIPMAPS:
			bm->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC);
			break;
		default:
			break;
	}
}

static void set_albedo_tex(const Ref<BaseMaterial3D> &bm, const Ref<Texture2D> &tex) {
	if (bm.is_null() || tex.is_null()) return;
	bm->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
	upgrade_material_filter(bm);
}





static const char *const kTexturesCaps[] = { "gate.mode", "config.files", "scene.current", "scene.balls", "scene.players",
	"scene.stage", "net.state", "net.member_data", "net.packets", "net.content_offer", "net.content_download", "ball.model", "ball.physics", "ball.skin", "player.model",
	"player.look", "player.outfit", "stage.court_look", "stage.net_look", "court.env", "models.query", "ui.locker", "ui.menu" };

static bool core_sync_open() { godot::Object *a = jvm_textures_io().api(); return a && bool(a->call("is_open", "sync")); }
static godot::String core_mode(const char *dflt) { godot::Object *a = jvm_textures_io().api(); return a ? godot::String(a->call("get_mode")) : godot::String(dflt); }
jvm::GameIO &jvm_textures_io() {
	static jvm::GameIO io = [] {
		jvm::GameIO g;
		g.mod = "textures";
		g.writes_need_open = false;
		for (const char *c : kTexturesCaps) g.caps.push_back(c);
		return g;
	}();
	return io;
}
JVTextureLoader::JVTextureLoader() {
	follow_.mod = "textures";
	follow_.access = "cosmetic";
	follow_.api_version = "1.1";
	for (const char *c : kTexturesCaps) follow_.caps.push_back(c);
	set_process_mode(PROCESS_MODE_ALWAYS);
}

void JVTextureLoader::_bind_methods() {
	JVM_PROF_TAP_BIND(JVTextureLoader);
	ClassDB::bind_method(D_METHOD("reload_config"), &JVTextureLoader::reload_config);
	ClassDB::bind_method(D_METHOD("_on_gate_changed", "active", "reason"), &JVTextureLoader::_on_gate_changed);
	ClassDB::bind_method(D_METHOD("_on_access_changed", "access", "open", "reason"), &JVTextureLoader::_on_access_changed);
	ClassDB::bind_method(D_METHOD("_on_peer_packet", "id", "packet"), &JVTextureLoader::_on_peer_packet);
	ClassDB::bind_method(D_METHOD("_on_peer_connected", "id"), &JVTextureLoader::_on_peer_connected);
	ClassDB::bind_method(D_METHOD("_on_peer_disconnected", "id"), &JVTextureLoader::_on_peer_disconnected);
	ClassDB::bind_method(D_METHOD("get_status"), &JVTextureLoader::get_status);
	ClassDB::bind_method(D_METHOD("get_sync_status"), &JVTextureLoader::get_sync_status);
	ClassDB::bind_method(D_METHOD("get_content_status"), &JVTextureLoader::get_content_status);
	ClassDB::bind_method(D_METHOD("content_fetch", "id"), &JVTextureLoader::content_fetch);
	ClassDB::bind_method(D_METHOD("get_swapped_count"), &JVTextureLoader::get_swapped_count);
	ClassDB::bind_method(D_METHOD("get_missing_lines"), &JVTextureLoader::get_missing_lines);
	ClassDB::bind_method(D_METHOD("get_override_count"), &JVTextureLoader::get_override_count);
	ClassDB::bind_method(D_METHOD("get_spawned_count"), &JVTextureLoader::get_spawned_count);
	ClassDB::bind_method(D_METHOD("list_picker_packs", "type"), &JVTextureLoader::list_picker_packs);
	ClassDB::bind_method(D_METHOD("pack_info", "id"), &JVTextureLoader::pack_info);
	ClassDB::bind_method(D_METHOD("get_picked_stage"), &JVTextureLoader::get_picked_stage);
	ClassDB::bind_method(D_METHOD("get_picked_ball"), &JVTextureLoader::get_picked_ball);
	ClassDB::bind_method(D_METHOD("set_picked_stage", "id"), &JVTextureLoader::set_picked_stage);
	ClassDB::bind_method(D_METHOD("set_picked_ball", "id"), &JVTextureLoader::set_picked_ball);
	ClassDB::bind_method(D_METHOD("rescan_picker_packs"), &JVTextureLoader::rescan_picker_packs);
	ClassDB::bind_method(D_METHOD("apply_picked_cosmetics"), &JVTextureLoader::apply_picked_cosmetics);
	ClassDB::bind_method(D_METHOD("_on_scene_node_added", "node"), &JVTextureLoader::_on_scene_node_added);
	ClassDB::bind_method(D_METHOD("_apply_pending_env"), &JVTextureLoader::_apply_pending_env);
	ClassDB::bind_method(D_METHOD("get_sky_status"), &JVTextureLoader::get_sky_status);
	ClassDB::bind_method(D_METHOD("get_env_status"), &JVTextureLoader::get_env_status);
	ClassDB::bind_method(D_METHOD("get_audio_status"), &JVTextureLoader::get_audio_status);
	ClassDB::bind_method(D_METHOD("audio_list"), &JVTextureLoader::audio_list);
	ClassDB::bind_method(D_METHOD("audio_buses"), &JVTextureLoader::audio_buses);
	ClassDB::bind_method(D_METHOD("audio_info", "id"), &JVTextureLoader::audio_info);
	ClassDB::bind_method(D_METHOD("audio_play", "id", "opts"), &JVTextureLoader::audio_play, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("audio_play_loop", "id", "opts"), &JVTextureLoader::audio_play_loop, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("audio_stop", "handle_or_id"), &JVTextureLoader::audio_stop);
	ClassDB::bind_method(D_METHOD("audio_stop_all"), &JVTextureLoader::audio_stop_all);
	ClassDB::bind_method(D_METHOD("audio_set_volume", "handle", "volume"), &JVTextureLoader::audio_set_volume);
	ClassDB::bind_method(D_METHOD("audio_set_bus", "handle", "bus"), &JVTextureLoader::audio_set_bus);
	ClassDB::bind_method(D_METHOD("audio_apply_pack", "stage_id"), &JVTextureLoader::audio_apply_pack);
	ClassDB::bind_method(D_METHOD("get_cache_count"), &JVTextureLoader::get_cache_count);
	ClassDB::bind_method(D_METHOD("get_disk_load_count"), &JVTextureLoader::get_disk_load_count);
	ClassDB::bind_method(D_METHOD("get_apply_env_count"), &JVTextureLoader::get_apply_env_count);
	ClassDB::bind_method(D_METHOD("get_cosmetics_force_count"), &JVTextureLoader::get_cosmetics_force_count);
	ClassDB::bind_method(D_METHOD("get_cosmetics_periodic_count"), &JVTextureLoader::get_cosmetics_periodic_count);
	ClassDB::bind_method(D_METHOD("apply_worn_locker_accessories_for_test", "player"), &JVTextureLoader::apply_worn_locker_accessories_for_test);
	ClassDB::bind_method(D_METHOD("apply_scene_cosmetics_for_test", "force"), &JVTextureLoader::apply_scene_cosmetics_for_test);
	ClassDB::bind_method(D_METHOD("get_cache_ids"), &JVTextureLoader::get_cache_ids);
	ClassDB::bind_method(D_METHOD("get_cached_texture", "path"), &JVTextureLoader::get_cached_texture);
	ClassDB::bind_method(D_METHOD("get_mip_stats"), &JVTextureLoader::get_mip_stats);
	ClassDB::bind_method(D_METHOD("get_accessory_status"), &JVTextureLoader::get_accessory_status);
	ClassDB::bind_method(D_METHOD("get_status_brief"), &JVTextureLoader::get_status_brief);
	ClassDB::bind_method(D_METHOD("get_status_rebuild_count"), &JVTextureLoader::get_status_rebuild_count);
	ClassDB::bind_method(D_METHOD("pose_game_accessory_roots", "player"), &JVTextureLoader::pose_game_accessory_roots);
	ClassDB::bind_method(D_METHOD("describe_accessory_anchors", "player"), &JVTextureLoader::describe_accessory_anchors);
	ClassDB::bind_method(D_METHOD("_apply_pending_anchors"), &JVTextureLoader::_apply_pending_anchors);
}


bool JVTextureLoader::reload_config() {
	if (!follow_.applied) return false;
	revert();
	load_packs(true);
	apply();
	return true;
}

Ref<Texture2D> JVTextureLoader::cache_or_load_texture(const String &full_path, bool force) {
	if (full_path.is_empty()) return Ref<Texture2D>();
	if (!force) {
		auto it = tex_cache_.find(full_path);
		if (it != tex_cache_.end() && it->second.is_valid()) return it->second;
	}
	Ref<Texture2D> tex;
	{


		String why;
		Ref<Image> img = jvm::safe::load_image(full_path, why);
		if (img.is_null()) {
			jvm::safe::note_refusal("texture " + full_path + ": " + why);
			return Ref<Texture2D>();
		}
		ensure_pack_mips(img);
		tex = ImageTexture::create_from_image(img);
	}
	if (tex.is_valid()) {
		disk_loads_++;
		tex_cache_[full_path] = tex;
		invalidate_mip_stats();
		invalidate_status_cache();
	}
	return tex;
}

void JVTextureLoader::prune_texture_cache(const std::set<String> &keep) {
	bool lost = false;
	for (auto it = tex_cache_.begin(); it != tex_cache_.end();) {
		if (keep.count(it->first)) ++it;
		else { it = tex_cache_.erase(it); lost = true; }
	}
	if (lost) { invalidate_mip_stats(); invalidate_status_cache(); }
}

Dictionary JVTextureLoader::get_cache_ids() const {
	Dictionary d;
	for (auto &[path, tex] : tex_cache_)
		if (tex.is_valid()) d[path] = int64_t(tex->get_instance_id());
	return d;
}

Ref<Texture2D> JVTextureLoader::get_cached_texture(const String &path) const {
	auto it = tex_cache_.find(path);
	if (it != tex_cache_.end()) return it->second;
	return Ref<Texture2D>();
}

String godot::texture_pack_rel() {
	if (DirAccess::dir_exists_absolute(jvm::mods_dir(kPackDir))) return kPackDir;
	if (DirAccess::dir_exists_absolute(jvm::mods_dir(kOldPackDir))) return kOldPackDir;
	return kPackDir;
}

static void walk(const String &base, const String &rel, PackedStringArray &out) {
	String dir = base.path_join(rel);
	PackedStringArray files = DirAccess::get_files_at(dir);
	for (int i = 0; i < files.size(); i++) out.push_back(rel.is_empty() ? files[i] : rel.path_join(files[i]));
	PackedStringArray dirs = DirAccess::get_directories_at(dir);
	for (int i = 0; i < dirs.size(); i++) walk(base, rel.is_empty() ? dirs[i] : rel.path_join(dirs[i]), out);
}

static bool is_image(const String &p) {
	return jvm::safe::image_ext(std::string(p.get_extension().utf8().get_data()));
}

static bool parse_color(const Variant &v, Color &out) {
	if (v.get_type() == Variant::COLOR) { out = Color(v); return true; }
	if (v.get_type() == Variant::STRING) {
		String s = String(v).strip_edges();
		if (s.is_empty()) return false;
		Color c = Color::from_string(s, Color(-1, -1, -1, -1));
		if (c.r >= 0.0f) { out = c; return true; }
		if (Color::html_is_valid(s)) { out = Color::html(s); return true; }
		return false;
	}
	if (v.get_type() == Variant::ARRAY) {
		Array a = v;
		if (a.size() < 3) return false;
		double r = double(a[0]), g = double(a[1]), b = double(a[2]);
		double al = a.size() >= 4 ? double(a[3]) : 1.0;
		if (r > 1.0 || g > 1.0 || b > 1.0 || (a.size() >= 4 && al > 1.0)) {
			r /= 255.0; g /= 255.0; b /= 255.0;
			if (a.size() >= 4) al /= 255.0;
		}
		out = Color(float(r), float(g), float(b), float(al));
		return true;
	}
	return false;
}

static bool parse_vec3(const Variant &v, Vector3 &out) {
	if (v.get_type() == Variant::VECTOR3) { out = Vector3(v); return true; }
	if (v.get_type() == Variant::ARRAY) {
		Array a = v;
		if (a.size() >= 3) {
			out = Vector3(float(double(a[0])), float(double(a[1])), float(double(a[2])));
			return true;
		}
	}
	return false;
}

static bool parse_vec2(const Variant &v, Vector2 &out) {
	if (v.get_type() == Variant::VECTOR2) { out = Vector2(v); return true; }
	if (v.get_type() == Variant::ARRAY) {
		Array a = v;
		if (a.size() >= 2) {
			out = Vector2(float(double(a[0])), float(double(a[1])));
			return true;
		}
	}
	return false;
}

static bool parse_float(const Variant &v, float &out) {
	if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
		out = float(double(v));
		return true;
	}
	return false;
}



static String resolve_pack_path(const String &pack_dir, const String &rel, bool images_only = false) {
	if (rel.is_empty()) return "";
	String full = jvm::safe::pack_file(pack_dir, rel, images_only);
	if (full.is_empty()) return "";
	if (!jvm::safe::no_links(pack_dir, full)) { jvm::safe::note_refusal("'" + rel.left(120) + "' in " + pack_dir.get_file() + " goes through a symlink"); return ""; }
	return full;
}

static String resolve_pack_glb(const String &pack_dir, const String &rel) {
	String full = resolve_pack_path(pack_dir, rel);
	if (full.is_empty()) return "";
	if (full.get_extension().to_lower() != "glb") { jvm::safe::note_refusal("'" + rel.left(120) + "' in " + pack_dir.get_file() + ": meshes must be .glb"); return ""; }
	return full;
}

Ref<Texture2D> JVTextureLoader::load_pack_texture(const String &pack_dir, const String &rel_path) {
	if (rel_path.is_empty()) return Ref<Texture2D>();
	String full = resolve_pack_path(pack_dir, rel_path, true);
	if (full.is_empty()) return Ref<Texture2D>();
	return cache_or_load_texture(full, cache_force_);
}

void JVTextureLoader::collect(const String &pack_dir, Dictionary &out) {
	String res_dir = pack_dir.path_join("res");
	if (DirAccess::dir_exists_absolute(res_dir)) {
		PackedStringArray files;
		walk(res_dir, "", files);
		for (int i = 0; i < files.size(); i++) {
			if (!is_image(files[i])) continue;
			String target = "res://" + files[i];
			if (!jvm::safe::game_texture_target(std::string(target.utf8().get_data()))) { jvm::safe::note_refusal("replacement target " + target + " in " + pack_dir.get_file() + " is not a game texture"); continue; }
			String full = resolve_pack_path(pack_dir, String("res").path_join(files[i]), true);
			if (!full.is_empty()) out[target] = full;
		}
	}
	Variant m = jvm::load_json(pack_dir.path_join("texture_map.json"));
	if (m.get_type() == Variant::DICTIONARY) {
		Dictionary d = m;
		Array k = d.keys();
		for (int i = 0; i < k.size(); i++) {
			String target = String(k[i]);
			if (!jvm::safe::game_texture_target(std::string(target.utf8().get_data()))) { jvm::safe::note_refusal("texture_map.json target '" + target.left(120) + "' in " + pack_dir.get_file() + " is not a game texture"); continue; }
			if (d[k[i]].get_type() != Variant::STRING) continue;
			String full = resolve_pack_path(pack_dir, String(d[k[i]]), true);
			if (!full.is_empty()) out[target] = full;
		}
	}
}

void JVTextureLoader::parse_skin_extensions(const String &pack_dir, const Dictionary &man, Skin &s) {
	if (man.has("outfit") && man["outfit"].get_type() == Variant::DICTIONARY) {
		Dictionary od = man["outfit"];
		s.outfit.present = true;
		s.outfit.default_tint_mode = String(od.get("tint_mode", "replace")).to_lower();
		Dictionary raw_slots = od.get("slots", Dictionary()).get_type() == Variant::DICTIONARY
			? Dictionary(od.get("slots", Dictionary()))
			: od;
		Array skeys = raw_slots.keys();
		for (int i = 0; i < skeys.size(); i++) {
			String slot = String(skeys[i]).to_lower();
			if (slot == "tint_mode" || slot == "slots" || slot == "colors") continue;
			Variant val = raw_slots[skeys[i]];
			OutfitSlotRule rule;
			if (val.get_type() == Variant::STRING) {
				rule.tex = load_pack_texture(pack_dir, String(val));
			} else if (val.get_type() == Variant::DICTIONARY) {
				Dictionary rd = val;
				String tex_rel = String(rd.get("tex", rd.get("texture", "")));
				if (!tex_rel.is_empty()) rule.tex = load_pack_texture(pack_dir, tex_rel);
				if (rd.has("tint_mode")) rule.tint_mode = String(rd["tint_mode"]).to_lower();
				if (rd.has("color")) rule.color.set = parse_color(rd["color"], rule.color.color);
			}
			if (rule.tex.is_valid() || rule.color.set) s.outfit.slots[slot] = rule;
		}
		if (od.has("colors") && od["colors"].get_type() == Variant::DICTIONARY) {
			Dictionary cd = od["colors"];
			Array ck = cd.keys();
			for (int i = 0; i < ck.size(); i++) {
				String slot = String(ck[i]).to_lower();
				Color c;
				if (parse_color(cd[ck[i]], c)) {
					s.outfit.slots[slot].color = { true, c };
					if (s.outfit.slots[slot].tint_mode.is_empty() && s.outfit.slots[slot].tex.is_null())
						s.outfit.slots[slot].tint_mode = "keep";
				}
			}
		}
	}

	auto parse_acc = [&](const char *key, const char *def_mount, bool def_jiggle, AccessorySpec &out) {
		if (!man.has(key) || man[key].get_type() != Variant::DICTIONARY) return;
		Dictionary ad = man[key];
		String rel = String(ad.get("model", ""));
		if (rel.is_empty()) return;
		String path = resolve_pack_glb(pack_dir, rel);
		if (path.is_empty()) return;
		if (!FileAccess::file_exists(path)) {
			jvm::warn("textures", String(key) + " model missing: " + path);
			return;
		}
		out.present = true;
		out.model_path = path;
		out.mount = String(ad.get("mount", ad.get("bone", def_mount)));
		parse_vec3(ad.get("offset", Variant()), out.offset);
		parse_vec3(ad.get("rotation_deg", ad.get("rotation", Variant())), out.rotation_deg);
		if (ad.has("scale")) {
			float uni = 1.0f;
			if (parse_float(ad["scale"], uni)) out.scale = Vector3(uni, uni, uni);
			else parse_vec3(ad["scale"], out.scale);
		}
		out.space = String(ad.get("space", "local")).to_lower();
		out.shader_mode = String(ad.get("shader", "gltf")).to_lower();
		out.hide_vanilla_hats = bool(ad.get("hide_vanilla_hats", false));
		if (ad.has("jiggle")) {
			out.jiggle = bool(ad["jiggle"]);
			out.jiggle_explicit = true;
		} else {
			out.jiggle = def_jiggle;
		}
		parse_float(ad.get("stiffness", Variant()), out.stiffness);
		parse_float(ad.get("drag", Variant()), out.drag);
		parse_float(ad.get("gravity", Variant()), out.gravity);
		parse_float(ad.get("radius", Variant()), out.radius);
		if (ad.has("tail_prefix")) out.tail_prefix = String(ad["tail_prefix"]).to_lower();
		if (ad.has("fit") && ad["fit"].get_type() == Variant::DICTIONARY) {
			Dictionary fd = ad["fit"];
			Array fkeys = fd.keys();
			for (int i = 0; i < fkeys.size(); i++) {
				if (fd[fkeys[i]].get_type() != Variant::DICTIONARY) continue;
				Dictionary one = fd[fkeys[i]];
				AccessoryFit fit;
				fit.present = true;
				if (one.has("offset") && parse_vec3(one["offset"], fit.offset)) fit.has_offset = true;
				Variant rv = one.get("rotation_deg", one.get("rotation", Variant()));
				if ((one.has("rotation_deg") || one.has("rotation")) && parse_vec3(rv, fit.rotation_deg)) fit.has_rotation = true;
				if (one.has("scale")) {
					float uni = 1.0f;
					if (parse_float(one["scale"], uni)) { fit.scale = Vector3(uni, uni, uni); fit.has_scale = true; }
					else if (parse_vec3(one["scale"], fit.scale)) fit.has_scale = true;
				}
				if (one.has("mount")) { fit.mount = String(one["mount"]); fit.has_mount = !fit.mount.is_empty(); }
				if (one.has("parts") && one["parts"].get_type() == Variant::DICTIONARY) {
					Dictionary pd = one["parts"];
					Array pkeys = pd.keys();
					for (int j = 0; j < pkeys.size(); j++) {
						if (pd[pkeys[j]].get_type() != Variant::DICTIONARY) continue;
						Dictionary part = pd[pkeys[j]];
						AccessoryPartFit pf;
						parse_vec3(part.get("offset", Variant()), pf.offset);
						parse_vec3(part.get("rotation_deg", part.get("rotation", Variant())), pf.rotation_deg);
						if (part.has("scale")) {
							float uni = 1.0f;
							if (parse_float(part["scale"], uni)) pf.scale = Vector3(uni, uni, uni);
							else parse_vec3(part["scale"], pf.scale);
						}
						fit.parts[String(pkeys[j]).to_lower()] = pf;
					}
				}
				out.fit[String(fkeys[i]).to_upper()] = fit;
			}
		}
	};
	parse_acc("hat", "head", false, s.hat);
	parse_acc("face", "head", false, s.face);
	parse_acc("back", "chest", true, s.back);
	s.category = String(man.get("category", man.get("locker_category", ""))).strip_edges().to_lower();

	if (man.has("net") && man["net"].get_type() == Variant::DICTIONARY) {
		Dictionary nd = man["net"];
		s.net.present = true;
		s.net.tape_color.set = parse_color(nd.get("tape_color", Variant()), s.net.tape_color.color);
		s.net.cord_color.set = parse_color(nd.get("cord_color", Variant()), s.net.cord_color.color);
		s.net.ink_color.set = parse_color(nd.get("ink_color", Variant()), s.net.ink_color.color);
		s.net.post_color.set = parse_color(nd.get("post_color", Variant()), s.net.post_color.color);
		s.net.pad_color.set = parse_color(nd.get("pad_color", Variant()), s.net.pad_color.color);
		s.net.band_color.set = parse_color(nd.get("band_color", Variant()), s.net.band_color.color);
		s.net.grid.set = parse_vec2(nd.get("grid", Variant()), s.net.grid.value);
		s.net.tape_top.set = parse_float(nd.get("tape_top", Variant()), s.net.tape_top.value);
		s.net.tape_bottom.set = parse_float(nd.get("tape_bottom", Variant()), s.net.tape_bottom.value);
		s.net.tape_side.set = parse_float(nd.get("tape_side", Variant()), s.net.tape_side.value);
		if (nd.has("tape_tex")) s.net.tape_tex = load_pack_texture(pack_dir, String(nd["tape_tex"]));
		if (nd.has("net_tex")) s.net.net_tex = load_pack_texture(pack_dir, String(nd["net_tex"]));
		if (nd.has("post_tex")) s.net.post_tex = load_pack_texture(pack_dir, String(nd["post_tex"]));
		if (nd.has("pad_tex")) s.net.pad_tex = load_pack_texture(pack_dir, String(nd["pad_tex"]));
	}

	if (man.has("court") && man["court"].get_type() == Variant::DICTIONARY) {
		Dictionary cd = man["court"];
		s.court.present = true;
		if (cd.has("floor_tex")) s.court.floor_tex = load_pack_texture(pack_dir, String(cd["floor_tex"]));
		if (cd.has("grass_tex")) s.court.grass_tex = load_pack_texture(pack_dir, String(cd["grass_tex"]));
		if (cd.has("hill_tex")) s.court.hill_tex = load_pack_texture(pack_dir, String(cd["hill_tex"]));
		s.court.floor_color.set = parse_color(cd.get("floor_color", Variant()), s.court.floor_color.color);
		s.court.checker_color.set = parse_color(cd.get("checker_color", Variant()), s.court.checker_color.color);
		s.court.line_color.set = parse_color(cd.get("line_color", Variant()), s.court.line_color.color);
		s.court.checker_size.set = parse_float(cd.get("checker_size", Variant()), s.court.checker_size.value);
		s.court.grass_tint.set = parse_color(cd.get("grass_tint", Variant()), s.court.grass_tint.color);
		s.court.grass_tip_color.set = parse_color(cd.get("grass_tip_color", Variant()), s.court.grass_tip_color.color);
		s.court.grass_patch_color.set = parse_color(cd.get("grass_patch_color", Variant()), s.court.grass_patch_color.color);
		s.court.grass_grad_top.set = parse_color(cd.get("grass_grad_top", Variant()), s.court.grass_grad_top.color);
		s.court.grass_grad_bottom.set = parse_color(cd.get("grass_grad_bottom", Variant()), s.court.grass_grad_bottom.color);
		s.court.hill_tint.set = parse_color(cd.get("hill_tint", Variant()), s.court.hill_tint.color);
		s.court.grass_brightness.set = parse_float(cd.get("grass_brightness", Variant()), s.court.grass_brightness.value);
	}

	if (man.has("sky") && man["sky"].get_type() == Variant::DICTIONARY) {
		Dictionary sd = man["sky"];
		s.sky.present = true;
		String pan = String(sd.get("panorama", sd.get("tex", "")));
		if (!pan.is_empty()) s.sky.panorama = load_pack_texture(pack_dir, pan);
		s.sky.energy_set = parse_float(sd.get("energy", Variant()), s.sky.energy);
		s.sky.ambient.set = parse_color(sd.get("ambient", Variant()), s.sky.ambient.color);
		s.sky.fog.set = parse_color(sd.get("fog_color", sd.get("fog", Variant())), s.sky.fog.color);
		s.sky.fog_density_set = parse_float(sd.get("fog_density", Variant()), s.sky.fog_density);
		s.sky.hide_env_props = bool(sd.get("hide_env_props", false));
		s.sky.hide_ground = bool(sd.get("hide_ground", false));
		Variant hv = sd.get("hide", sd.get("hide_names", Variant()));
		if (hv.get_type() == Variant::ARRAY) {
			Array ha = hv;
			for (int i = 0; i < ha.size(); i++)
				s.sky.hide_names.push_back(String(ha[i]));
		} else if (hv.get_type() == Variant::STRING && !String(hv).is_empty()) {
			s.sky.hide_names.push_back(String(hv));
		}
	}

	if (man.has("environment") && man["environment"].get_type() == Variant::DICTIONARY) {
		Dictionary ed = man["environment"];
		s.env.present = true;
		String rel = String(ed.get("mesh", ed.get("glb", ed.get("path", ""))));
		if (!rel.is_empty()) {
			String full = resolve_pack_glb(pack_dir, rel);
			if (!full.is_empty()) s.env.mesh_path = full;
		}
		s.env.name = String(ed.get("name", "env"));
		parse_vec3(ed.get("position", ed.get("translation", Variant())), s.env.position);
		parse_vec3(ed.get("rotation_deg", ed.get("rotation", Variant())), s.env.rotation_deg);
		if (ed.has("scale")) {
			float uni = 1.0f;
			if (parse_float(ed["scale"], uni)) s.env.scale = Vector3(uni, uni, uni);
			else parse_vec3(ed["scale"], s.env.scale);
		}
		s.env.hide_env_props = bool(ed.get("hide_env_props", s.sky.hide_env_props));
		s.env.hide_ground = bool(ed.get("hide_ground", false));
		s.env.baked_lighting = bool(ed.get("baked_lighting", ed.get("baked", false)));
		s.env.sway = bool(ed.get("sway", false));
		Variant hv = ed.get("hide", ed.get("hide_names", Variant()));
		if (hv.get_type() == Variant::ARRAY) {
			Array ha = hv;
			for (int i = 0; i < ha.size(); i++)
				s.env.hide_names.push_back(String(ha[i]));
		} else if (hv.get_type() == Variant::STRING && !String(hv).is_empty()) {
			s.env.hide_names.push_back(String(hv));
		}
		Variant lv = ed.get("lod", Variant());
		if (lv.get_type() == Variant::ARRAY) {
			Array la = lv;
			for (int i = 0; i < la.size(); i++) {
				if (la[i].get_type() != Variant::DICTIONARY) continue;
				Dictionary ld = la[i];
				jvm::court_env::LodSpec lod;
				String lrel = String(ld.get("mesh", ld.get("glb", "")));
				if (lrel.is_empty()) continue;
				String full = resolve_pack_glb(pack_dir, lrel);
				if (full.is_empty()) continue;
				lod.mesh_path = full;
				parse_float(ld.get("visible_begin", ld.get("range_begin", Variant())), lod.visible_begin);
				parse_float(ld.get("visible_end", ld.get("range", ld.get("range_end", Variant()))), lod.visible_end);
				s.env.lods.push_back(lod);
			}
		}
		Variant liv = ed.get("lights", Variant());
		if (liv.get_type() == Variant::ARRAY) {
			Array la = liv;

			const int kMaxPackLights = 8;
			if (la.size() > kMaxPackLights) jvm::safe::note_refusal("environment.lights in " + pack_dir.get_file() + ": " + String::num_int64(la.size()) + " lights, only the first 8 are used");
			for (int i = 0; i < la.size() && int(s.env.lights.size()) < kMaxPackLights; i++) {
				if (la[i].get_type() != Variant::DICTIONARY) continue;
				Dictionary ld = la[i];
				jvm::court_env::LightSpec L;
				L.type = String(ld.get("type", "omni")).strip_edges().to_lower().left(16);
				parse_vec3(ld.get("position", ld.get("pos", Variant())), L.position);
				parse_color(ld.get("color", Variant()), L.color);
				parse_float(ld.get("energy", Variant()), L.energy);
				parse_float(ld.get("range", ld.get("omni_range", Variant())), L.range);
				if (!L.position.is_finite() || !std::isfinite(L.energy) || !std::isfinite(L.range)
						|| !std::isfinite(L.color.r) || !std::isfinite(L.color.g) || !std::isfinite(L.color.b)) {
					jvm::safe::note_refusal("environment.lights[" + String::num_int64(i) + "] in " + pack_dir.get_file() + ": non-finite value");
					continue;
				}
				L.energy = CLAMP(L.energy, 0.0f, 16.0f);
				L.range = CLAMP(L.range, 0.0f, 100.0f);
				L.position = L.position.clamp(Vector3(-500, -500, -500), Vector3(500, 500, 500));
				L.color = Color(CLAMP(L.color.r, 0.0f, 4.0f), CLAMP(L.color.g, 0.0f, 4.0f), CLAMP(L.color.b, 0.0f, 4.0f), 1.0f);
				s.env.lights.push_back(L);
			}
		}
		if (s.env.mesh_path.is_empty() && s.env.lods.empty())
			s.env.present = s.env.hide_env_props || s.env.hide_ground || s.env.hide_names.size() > 0
				|| s.env.lights.size() > 0;
	}

	if (man.has("audio") && man["audio"].get_type() == Variant::DICTIONARY) {

		s.audio = jvm::native_audio::pack_from_dict(man["audio"]);
	}

	if (man.has("ball") && man["ball"].get_type() == Variant::DICTIONARY) {
		Dictionary bd = man["ball"];
		s.ball.present = true;
		if (bd.has("albedo_tex")) s.ball.albedo_tex = load_pack_texture(pack_dir, String(bd["albedo_tex"]));
		else if (bd.has("tex")) s.ball.albedo_tex = load_pack_texture(pack_dir, String(bd["tex"]));
		s.ball.albedo_color.set = parse_color(bd.get("albedo_color", Variant()), s.ball.albedo_color.color);
		s.ball.color_a.set = parse_color(bd.get("color_a", Variant()), s.ball.color_a.color);
		s.ball.color_b.set = parse_color(bd.get("color_b", Variant()), s.ball.color_b.color);
		s.ball.shade.set = parse_float(bd.get("shade", Variant()), s.ball.shade.value);
		s.ball.wrap = String(bd.get("wrap", "")).strip_edges().to_lower();
		s.ball.extras = bd;
	}
}

void JVTextureLoader::load_packs(bool force_reload) {
	cache_force_ = force_reload;
	skins_.clear();
	load_order_.clear();
	registry_warnings_.clear();
	equipped_.clear();
	added_.clear();
	std::set<String> used;
	String root = jvm::mods_dir(texture_pack_rel());
	if (!DirAccess::dir_exists_absolute(root)) {
		jvm::log("textures", "no folder " + root);
		prune_texture_cache(used);
		packs_loaded_ = true;
		cache_force_ = false;
		return;
	}
	PackedStringArray packs = DirAccess::get_directories_at(root);
	packs.sort();
	std::vector<String> pack_dirs;
	for (int p = 0; p < packs.size(); p++) pack_dirs.push_back(root.path_join(packs[p]));
	std::vector<sk::PackInfo> infos;
	std::vector<Dictionary> manifests;
	std::set<std::string> installed_ids;
	for (int p = 0; p < packs.size(); p++) {
		Variant man = jvm::load_json(pack_dirs[p].path_join("skin.json"));
		Dictionary d = man.get_type() == Variant::DICTIONARY ? Dictionary(man) : Dictionary();
		infos.push_back({ S(packs[p]), S(String(d.get("id", ""))) });
		installed_ids.insert(S(String(d.get("id", "")).to_upper()));
		manifests.push_back(d);
	}


	for (int p = int(packs.size()) - 1; p >= 0; p--) {
		if (String(manifests[p].get("kind", "")).strip_edges().to_lower() != "model") continue;
		installed_ids.erase(S(String(manifests[p].get("id", "")).to_upper()));
		packs.remove_at(p);
		pack_dirs.erase(pack_dirs.begin() + p);
		infos.erase(infos.begin() + p);
		manifests.erase(manifests.begin() + p);
	}



	{
		String croot = content_cache_dir();
		PackedStringArray cached = DirAccess::dir_exists_absolute(croot) ? DirAccess::get_directories_at(croot) : PackedStringArray();
		cached.sort();
		for (int c = 0; c < cached.size(); c++) {
			if (cached[c].begins_with(".")) continue;
			String dir = croot.path_join(cached[c]);
			Variant man = jvm::load_json(dir.path_join("skin.json"));
			Dictionary d = man.get_type() == Variant::DICTIONARY ? Dictionary(man) : Dictionary();
			String kind = String(d.get("kind", "")).strip_edges().to_lower();
			if (kind == "model" || installed_ids.count(S(String(d.get("id", "")).to_upper()))) continue;
			packs.push_back("cache/" + cached[c]);
			pack_dirs.push_back(dir);
			infos.push_back({ S(packs[packs.size() - 1]), S(String(d.get("id", ""))) });
			manifests.push_back(d);
		}
	}
	pack_refs_.clear();
	sk::Registry reg = sk::build_registry(infos);
	registry_warnings_ = reg.warnings;
	for (auto &w : reg.warnings) jvm::warn("textures", G(w));
	for (int p = 0; p < packs.size(); p++) {
		std::string pname = S(packs[p]);
		for (auto &[id, pack] : reg.by_id) {
			if (pack != pname) continue;
			Dictionary man = manifests[p];
			String pack_dir = pack_dirs[p];
			pack_refs_[id] = { pack_dir, packs[p].begins_with("cache/") };
			Skin s;
			s.pack = packs[p];
			s.name = man.get("name", packs[p]);
			s.global = String(man.get("kind", "skin")) == "global";
			Dictionary files;
			collect(pack_dir, files);
			Array keys = files.keys();
			for (int i = 0; i < keys.size(); i++) {
				String fpath = files[keys[i]];
				Ref<Texture2D> tex = cache_or_load_texture(fpath, force_reload);
				if (tex.is_null()) { jvm::warn("textures", "cannot read " + fpath); continue; }
				used.insert(fpath);
				s.map[keys[i]] = tex;
			}
			String add_dir = pack_dir.path_join("add");
			if (DirAccess::dir_exists_absolute(add_dir)) {
				PackedStringArray af;
				walk(add_dir, "", af);
				for (int i = 0; i < af.size(); i++) {
					if (!is_image(af[i])) continue;
					String fpath = add_dir.path_join(af[i]);
					Ref<Texture2D> t = cache_or_load_texture(fpath, force_reload);
					if (t.is_null()) continue;
					used.insert(fpath);
					t->take_over_path(String("res://mods/") + texture_pack_rel() + "/" + s.pack + "/add/" + af[i]);
					added_.push_back(t);
				}
			}
			parse_skin_extensions(pack_dir, man, s);
			s.type = infer_pack_type(man);
			if (s.type.is_empty()) s.type = String(man.get("kind", "skin")).strip_edges().to_lower();
			s.picker = s.type == "stage" || s.type == "ball";
			if (s.picker) s.global = false;
			s.label = man.get("label", s.name);
			s.base_stage = man.get("base_stage", man.get("base", "grass"));
			s.preview_path = man.get("preview", "preview.png");
			if (man.has("variants") && man["variants"].get_type() == Variant::ARRAY) {
				Array va = man["variants"];
				for (int i = 0; i < va.size(); i++) s.variants.append(String(va[i]));
			} else if (man.has("tod") && man["tod"].get_type() == Variant::ARRAY) {
				Array va = man["tod"];
				for (int i = 0; i < va.size(); i++) s.variants.append(String(va[i]));
			}
			{
				String pp = jvm::safe::pack_file(pack_dir, s.preview_path, true);
				if (!pp.is_empty() && FileAccess::file_exists(pp)) s.preview_tex = load_pack_texture(pack_dir, s.preview_path);
			}
			skins_[id] = s;
			load_order_.push_back(id);
			break;
		}
	}
	Variant eq = jvm::load_json(root.path_join("equipped.json"));
	if (eq.get_type() == Variant::DICTIONARY) {
		Array a = Dictionary(eq).get("equipped", Array());
		for (int i = 0; i < a.size(); i++) {
			auto id = sk::parse_id(S(String(a[i])));
			if (!id) { jvm::warn("textures", "equipped.json: bad id " + String(a[i])); continue; }
			if (!skins_.count(*id)) { jvm::warn("textures", "equipped.json: " + String(a[i]) + " is not installed"); continue; }
			if (skins_[*id].global) continue;
			if (equipped_.size() < sk::kMaxIds) equipped_.push_back(*id);
		}
	}
	Variant sc = jvm::load_json(jvm::install_dir().path_join("texture_sync.json"));
	sync_cfg_ = sc.get_type() == Variant::DICTIONARY ? Dictionary(sc) : Dictionary();
	if (OS::get_singleton()->is_debug_build() && !OS::get_singleton()->get_environment("JV_TEXTURE_SYNC").is_empty()) {
		Variant t = jvm::load_json(OS::get_singleton()->get_environment("JV_TEXTURE_SYNC"));
		if (t.get_type() == Variant::DICTIONARY) sync_cfg_ = t;
	}
	load_picker_state();

	for (auto &[id, s] : skins_) {
		auto keep_tex = [&](const Ref<Texture2D> &tex) {
			if (tex.is_null()) return;
			for (auto &[path, cached] : tex_cache_)
				if (cached == tex) used.insert(path);
		};
		Array mk = s.map.keys();
		for (int i = 0; i < mk.size(); i++) keep_tex(s.map[mk[i]]);
		keep_tex(s.preview_tex);
		keep_tex(s.net.tape_tex); keep_tex(s.net.net_tex); keep_tex(s.net.post_tex); keep_tex(s.net.pad_tex);
		keep_tex(s.court.floor_tex); keep_tex(s.court.grass_tex); keep_tex(s.court.hill_tex);
		keep_tex(s.ball.albedo_tex);
		for (auto &[slot, rule] : s.outfit.slots) keep_tex(rule.tex);
	}
	for (int i = 0; i < added_.size(); i++) {
		Ref<Texture2D> t = added_[i];
		if (t.is_null()) continue;
		for (auto &[path, cached] : tex_cache_)
			if (cached == t) used.insert(path);
	}
	prune_texture_cache(used);
	packs_loaded_ = true;
	cache_force_ = false;
	invalidate_status_cache();
}

void JVTextureLoader::apply_global(const Skin &s) {
	RenderingServer *rs = RenderingServer::get_singleton();
	ResourceLoader *rl = ResourceLoader::get_singleton();
	Array keys = s.map.keys();
	for (int i = 0; i < keys.size(); i++) {
		String res_path = keys[i];
		if (!jvm::safe::game_texture_target(std::string(res_path.utf8().get_data()))) { jvm::safe::note_refusal("global replacement target " + res_path + " is not a game texture"); continue; }
		if (!rl->exists(res_path, "Texture2D")) { jvm::warn("textures", "game has no " + res_path + " (skipped)"); continue; }
		Ref<Texture2D> orig = rl->load(res_path, "Texture2D");
		if (orig.is_null()) continue;
		Ref<ImageTexture> repl = s.map[res_path];
		Ref<Image> backup = orig->get_image();
		if (backup.is_null() || repl.is_null()) continue;
		if (repl->get_width() != orig->get_width() || repl->get_height() != orig->get_height())
			jvm::warn("textures", res_path + ": size differs from original; UVs/regions use the original size");
		Ref<Image> repl_img = repl->get_image();
		if (repl_img.is_valid()) {
			repl_img = repl_img->duplicate();
			ensure_pack_mips(repl_img);
		}
		if (backup.is_valid() && !backup->has_mipmaps()) {
			backup = backup->duplicate();
			ensure_pack_mips(backup);
		}
		rs->texture_replace(orig->get_rid(), rs->texture_2d_create(repl_img));
		swaps_.push_back({ orig, backup, res_path });
	}
}

void JVTextureLoader::apply() {
	ensure_court_watch();
	if (!packs_loaded_) load_packs(false);
	for (auto id : load_order_) {
		const Skin &s = skins_[id];
		if (s.global && !s.picker) apply_global(s);
	}
	jvm::log("textures", "applied " + String::num_int64(swaps_.size()) + " global replacement(s), " + String::num_int64(skins_.size()) + " pack(s), " +
			String::num_int64(equipped_.size()) + " equipped skin(s)");
	apply_scene_cosmetics(true);
	sync_locker_accessories();
	refresh_players();
	refresh_display_avatars();
	request_menu_portrait_refresh();
	invalidate_status_cache();
}

void JVTextureLoader::revert() {
	net_stop();
	RenderingServer *rs = RenderingServer::get_singleton();
	for (auto &s : swaps_)
		if (s.original.is_valid() && s.backup.is_valid()) {
		Ref<Image> back = s.backup;
		if (!back->has_mipmaps()) {
			back = back->duplicate();
			ensure_pack_mips(back);
		}
		rs->texture_replace(s.original->get_rid(), rs->texture_2d_create(back));
	}
	clear_scene_cosmetics();
	std::vector<int32_t> peers;
	for (auto &[p, ids] : applied_) peers.push_back(p);
	for (int32_t p : peers) clear_overrides(p);
	overrides_.clear();
	mat_overrides_.clear();
	visibility_backups_.clear();
	spawned_nodes_.clear();
	outfit_bindings_.clear();
	applied_.clear();
	applied_player_id_.clear();
	applied_model_id_.clear();


	jvm::log("textures", "reverted " + String::num_int64(swaps_.size()) + " replacement(s) and all per-player skins");
	swaps_.clear();
	remote_.clear();
	update_missing_ui();
	invalidate_status_cache();
}

Ref<Shader> JVTextureLoader::get_textured_toon_shader(const Ref<Shader> &orig) {
	if (orig.is_null()) return Ref<Shader>();
	String code = orig->get_code();
	uint64_t key = uint64_t(code.hash()) ^ 0x544F4F4EULL;
	auto it = shader_cache_.find(key);
	if (it != shader_cache_.end() && it->second.is_valid()) return it->second;

	String patched = code;
	String uniforms =
		"\nuniform sampler2D jv_skin_tex : source_color, hint_default_white, filter_linear_mipmap_anisotropic;\n"
		"uniform float jv_skin_tint_mix : hint_range(0.0, 1.0) = 0.0;\n";

	int alb_pos = patched.find("uniform vec4 albedo");
	if (alb_pos >= 0) {
		int eol = patched.find("\n", alb_pos);
		if (eol >= 0) patched = patched.substr(0, eol + 1) + uniforms + patched.substr(eol + 1);
	} else {
		int fn_pos = patched.find("void fragment()");
		if (fn_pos >= 0) patched = patched.substr(0, fn_pos) + uniforms + patched.substr(fn_pos);
	}

	const String toon_old = "vec3 base = jv_in(seam > 0.0 ? sleeve_color.rgb : albedo.rgb);";
	const String toon_new =
		"vec3 jv_tex_rgb = jv_in(texture(jv_skin_tex, UV).rgb);\n"
		"\tvec3 base = (seam > 0.0) ? jv_in(sleeve_color.rgb) : mix(jv_tex_rgb, jv_tex_rgb * jv_in(albedo.rgb), jv_skin_tint_mix);";
	const String wii_old = "vec3 col = jv_in(albedo.rgb) * level * light;";
	const String wii_new =
		"vec3 jv_tex_rgb = jv_in(texture(jv_skin_tex, UV).rgb);\n"
		"\tvec3 col = mix(jv_tex_rgb, jv_tex_rgb * jv_in(albedo.rgb), jv_skin_tint_mix) * level * light;";

	if (patched.contains(toon_old)) patched = patched.replace(toon_old, toon_new);
	else if (patched.contains(wii_old)) patched = patched.replace(wii_old, wii_new);
	else return Ref<Shader>();

	Ref<Shader> sh;
	sh.instantiate();
	sh->set_code(patched);
	shader_cache_[key] = sh;
	return sh;
}

Ref<Shader> JVTextureLoader::get_textured_net_shader(const Ref<Shader> &orig) {
	if (orig.is_null()) return Ref<Shader>();
	String code = orig->get_code();
	uint64_t key = uint64_t(code.hash()) ^ 0x4E4554ULL;
	auto it = shader_cache_.find(key);
	if (it != shader_cache_.end() && it->second.is_valid()) return it->second;

	String patched = code;
	String uniforms =
		"\nuniform sampler2D jv_tape_tex : source_color, hint_default_white, filter_linear_mipmap_anisotropic;\n"
		"uniform bool jv_use_tape_tex = false;\n"
		"uniform sampler2D jv_net_tex : source_color, hint_default_white, filter_linear_mipmap_anisotropic;\n"
		"uniform bool jv_use_net_tex = false;\n";
	int asp_pos = patched.find("uniform float aspect");
	if (asp_pos >= 0) {
		int eol = patched.find("\n", asp_pos);
		if (eol >= 0) patched = patched.substr(0, eol + 1) + uniforms + patched.substr(eol + 1);
	}
	const String tape_old = "ALBEDO = jv_out(jv_in((inside < tape_ink || outer) ? ink_color : tape_color) * mix(vec3(1.0), jv_body_tint, world_lit));";
	const String tape_new =
		"vec3 jv_tc = tape_color;\n"
		"\t\tif (jv_use_tape_tex) { jv_tc *= texture(jv_tape_tex, vec2(UV.x * aspect, clamp(UV.y / max(tape_top, 0.001), 0.0, 1.0))).rgb; }\n"
		"\t\tALBEDO = jv_out(jv_in((inside < tape_ink || outer) ? ink_color : jv_tc) * mix(vec3(1.0), jv_body_tint, world_lit));";
	const String cord_old = "ALBEDO = jv_out(jv_in(cord_color) * mix(vec3(1.0), jv_body_tint, world_lit));";
	const String cord_new =
		"vec3 jv_cc = cord_color;\n"
		"\t\tif (jv_use_net_tex) { jv_cc *= texture(jv_net_tex, UV * grid).rgb; }\n"
		"\t\tALBEDO = jv_out(jv_in(jv_cc) * mix(vec3(1.0), jv_body_tint, world_lit));";
	if (!patched.contains(tape_old) || !patched.contains(cord_old)) return Ref<Shader>();
	patched = patched.replace(tape_old, tape_new).replace(cord_old, cord_new);
	Ref<Shader> sh;
	sh.instantiate();
	sh->set_code(patched);
	shader_cache_[key] = sh;
	return sh;
}

Ref<Shader> JVTextureLoader::get_textured_post_shader(const Ref<Shader> &orig) {
	if (orig.is_null()) return Ref<Shader>();
	String code = orig->get_code();
	uint64_t key = uint64_t(code.hash()) ^ 0x504F5354ULL;
	auto it = shader_cache_.find(key);
	if (it != shader_cache_.end() && it->second.is_valid()) return it->second;

	String patched = code;
	String uniforms =
		"\nuniform sampler2D jv_post_tex : source_color, hint_default_white, filter_linear_mipmap_anisotropic;\n"
		"uniform bool jv_use_post_tex = false;\n";
	int bp = patched.find("uniform vec3 base_color");
	if (bp >= 0) {
		int eol = patched.find("\n", bp);
		if (eol >= 0) patched = patched.substr(0, eol + 1) + uniforms + patched.substr(eol + 1);
	}
	const String old_col = "vec3 col = base_color * mix(dark, light, t);";
	const String new_col = "vec3 col = (jv_use_post_tex ? texture(jv_post_tex, UV).rgb : vec3(1.0)) * base_color * mix(dark, light, t);";
	if (!patched.contains(old_col)) return Ref<Shader>();
	patched = patched.replace(old_col, new_col);
	Ref<Shader> sh;
	sh.instantiate();
	sh->set_code(patched);
	shader_cache_[key] = sh;
	return sh;
}

static void meshes_under(Node *n, std::vector<MeshInstance3D *> &out) {
	if (!n) return;
	if (MeshInstance3D *m = Object::cast_to<MeshInstance3D>(n)) out.push_back(m);
	for (int i = 0; i < n->get_child_count(); i++) meshes_under(n->get_child(i), out);
}

static Node *find_node_named(Node *root, const String &name) {
	if (!root) return nullptr;
	if (root->get_name() == StringName(name)) return root;
	for (int i = 0; i < root->get_child_count(); i++) {
		if (Node *hit = find_node_named(root->get_child(i), name)) return hit;
	}
	return nullptr;
}

static Node *find_court_node(Node *scene) {
	if (!scene) return nullptr;
	if (Node *c = jvm_textures_io().node("stage.court_look", scene, "Court")) return c;
	return find_node_named(scene, "Court");
}

static Node *find_grass_node(Node *scene) {
	if (!scene) return nullptr;
	if (Node *g = jvm_textures_io().node("stage.court_look", scene, "Stage/Grass")) return g;
	if (Node *g = jvm_textures_io().node("stage.court_look", scene, "Grass")) return g;
	return find_node_named(scene, "Grass");
}

static Node *find_ball_node(Node *scene) {
	if (!scene) return nullptr;
	Variant bv = jvm_textures_io().get("scene.balls", scene, "ball");
	if (Node *b = Object::cast_to<Node>(bv)) return b;
	if (Node *b = jvm_textures_io().node("scene.balls", scene, "Ball")) return b;
	return find_node_named(scene, "Ball");
}

static void collect_named(Node *n, const char *name, std::vector<Node *> &out, int depth = 0) {
	if (!n || depth > 64) return;
	if (String(n->get_name()) == name) out.push_back(n);
	for (int i = 0; i < n->get_child_count(); i++) collect_named(n->get_child(i), name, out, depth + 1);
}

static void collect_balls(Node *n, std::vector<Node *> &out, int depth) {
	if (!n || depth > 64) return;
	if (jvm::script_is(n, "Ball")) out.push_back(n);
	int cc = n->get_child_count();
	for (int i = 0; i < cc; i++) collect_balls(n->get_child(i), out, depth + 1);
}

static std::vector<Node *> all_balls(Node *from) {
	std::vector<Node *> out;
	if (!from || !from->is_inside_tree()) return out;
	collect_balls(from->get_tree()->get_root(), out, 0);
	return out;
}

static Node *custom_visual_on(Node *ball) {
	if (!ball) return nullptr;
	TypedArray<Node> ms = ball->find_children(jvm::custom_ball::kNodeName, "MeshInstance3D", true, false);
	for (int i = 0; i < ms.size(); i++) {
		Node *n = Object::cast_to<Node>(ms[i]);
		if (n && n->is_inside_tree()) return n;
	}
	return nullptr;
}

void JVTextureLoader::clear_scene_cosmetics() {
	jvm::court_env::revert_env();
	jvm::court_sky::revert_sky();
	jvm::native_audio::clear_pack_audio();
	for (auto it = overrides_.begin(); it != overrides_.end();) {
		if (it->owner_peer != 0) { ++it; continue; }
		if (MeshInstance3D *m = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(it->mesh_id)))
			m->set_surface_override_material(it->surface, it->previous);
		it = overrides_.erase(it);
	}
	for (auto it = mat_overrides_.begin(); it != mat_overrides_.end();) {
		if (it->owner_peer != 0) { ++it; continue; }
		if (MeshInstance3D *m = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(it->mesh_id)))
			m->set_material_override(it->previous);
		it = mat_overrides_.erase(it);
	}
	for (auto it = prop_backups_.rbegin(); it != prop_backups_.rend(); ++it) {
		if (Object *o = ObjectDB::get_instance(it->obj_id))
			jvm_textures_io().set_undo("stage.court_look", o, it->prop, it->previous);
	}
	prop_backups_.clear();
	for (auto it = shader_param_backups_.rbegin(); it != shader_param_backups_.rend(); ++it) {
		if (ShaderMaterial *sm = Object::cast_to<ShaderMaterial>(ObjectDB::get_instance(it->mat_id)))
			sm->set_shader_parameter(it->param, it->previous);
	}
	shader_param_backups_.clear();
	clear_custom_ball_visual();
	styled_scene_id_ = 0;
	styled_court_id_ = 0;
	styled_grass_id_ = 0;
	styled_ball_id_ = 0;
	styled_grass_mat_id_ = 0;
	styled_node_count_ = 0;
	styled_applied_ids_.clear();
}

void JVTextureLoader::apply_net_spec(Node *court, const NetSpec &net) {
	if (!court || !net.present) return;

	auto override_mesh_mat = [&](MeshInstance3D *mi, const ColorParam &cp, const Ref<Texture2D> &tex, bool post_grad) {
		if (!mi) return;
		Ref<Material> orig_ov = mi->get_material_override();
		Ref<Material> base_mat = orig_ov.is_valid() ? orig_ov : mi->get_active_material(0);
		if (base_mat.is_null()) return;
		Ref<Material> dup = base_mat->duplicate();
		Ref<ShaderMaterial> sm = dup;
		if (sm.is_valid()) {
			if (post_grad && tex.is_valid()) {
				Ref<Shader> sh = get_textured_post_shader(sm->get_shader());
				if (sh.is_valid()) {
					sm->set_shader(sh);
					sm->set_shader_parameter("jv_post_tex", tex);
					sm->set_shader_parameter("jv_use_post_tex", true);
				}
			}
			if (cp.set) sm->set_shader_parameter("base_color", Color(cp.color.r, cp.color.g, cp.color.b, 1.0f));
		} else {
			Ref<BaseMaterial3D> bm = dup;
			if (bm.is_valid()) {
			if (cp.set) bm->set_albedo(cp.color);
			if (tex.is_valid()) set_albedo_tex(bm, tex);
		}
		}
		mat_overrides_.push_back({ mi->get_instance_id(), orig_ov, 0 });
		mi->set_material_override(dup);
	};

	if (MeshInstance3D *net_mi = Object::cast_to<MeshInstance3D>(jvm_textures_io().node("stage.net_look", court, "Net"))) {
		Ref<Material> orig_ov = net_mi->get_material_override();
		Ref<Material> base_mat = orig_ov.is_valid() ? orig_ov : net_mi->get_active_material(0);
		Ref<ShaderMaterial> sm_orig = base_mat;
		if (sm_orig.is_valid()) {
			Ref<ShaderMaterial> dup = sm_orig->duplicate();
			if (net.tape_tex.is_valid() || net.net_tex.is_valid()) {
				Ref<Shader> sh = get_textured_net_shader(dup->get_shader());
				if (sh.is_valid()) {
					dup->set_shader(sh);
					if (net.tape_tex.is_valid()) {
						dup->set_shader_parameter("jv_tape_tex", net.tape_tex);
						dup->set_shader_parameter("jv_use_tape_tex", true);
					}
					if (net.net_tex.is_valid()) {
						dup->set_shader_parameter("jv_net_tex", net.net_tex);
						dup->set_shader_parameter("jv_use_net_tex", true);
					}
				}
			}
			if (net.tape_color.set) dup->set_shader_parameter("tape_color", Vector3(net.tape_color.color.r, net.tape_color.color.g, net.tape_color.color.b));
			if (net.cord_color.set) dup->set_shader_parameter("cord_color", Vector3(net.cord_color.color.r, net.cord_color.color.g, net.cord_color.color.b));
			if (net.ink_color.set) dup->set_shader_parameter("ink_color", Vector3(net.ink_color.color.r, net.ink_color.color.g, net.ink_color.color.b));
			if (net.grid.set) dup->set_shader_parameter("grid", net.grid.value);
			if (net.tape_top.set) dup->set_shader_parameter("tape_top", net.tape_top.value);
			if (net.tape_bottom.set) dup->set_shader_parameter("tape_bottom", net.tape_bottom.value);
			if (net.tape_side.set) dup->set_shader_parameter("tape_side", net.tape_side.value);
			mat_overrides_.push_back({ net_mi->get_instance_id(), orig_ov, 0 });
			net_mi->set_material_override(dup);
		}
	}

	const ColorParam band_col = net.band_color.set ? net.band_color : net.tape_color;
	for (const char *n : { "NetBand", "NetBandBottom" }) {
		if (band_col.set || net.tape_tex.is_valid())
			override_mesh_mat(Object::cast_to<MeshInstance3D>(jvm_textures_io().node("stage.net_look", court, String(n))), band_col, net.tape_tex, false);
	}
	if (net.cord_color.set) {
		for (const char *n : { "CordTopWest", "CordBottomWest", "CordTopEast", "CordBottomEast" })
			override_mesh_mat(Object::cast_to<MeshInstance3D>(jvm_textures_io().node("stage.net_look", court, String(n))), net.cord_color, Ref<Texture2D>(), false);
	}
	if (net.post_color.set || net.post_tex.is_valid()) {
		for (const char *n : { "PostWest", "PostEast" })
			override_mesh_mat(Object::cast_to<MeshInstance3D>(jvm_textures_io().node("stage.net_look", court, String(n))), net.post_color, net.post_tex, true);
	}
	if (net.pad_color.set || net.pad_tex.is_valid()) {
		for (const char *n : { "PostWestPad", "PostEastPad" })
			override_mesh_mat(Object::cast_to<MeshInstance3D>(jvm_textures_io().node("stage.net_look", court, String(n))), net.pad_color, net.pad_tex, true);
	}
}

void JVTextureLoader::apply_court_spec(Node *scene, Node *court, Node *grass, const CourtSpec &cs) {
	if (!cs.present) return;

	if (court) {
		if (MeshInstance3D *cf = Object::cast_to<MeshInstance3D>(jvm_textures_io().node("stage.court_look", court, "CourtFloor"))) {
			if (cs.floor_tex.is_valid() || cs.floor_color.set || cs.checker_color.set || cs.checker_size.set) {
				Ref<Material> orig_ov = cf->get_material_override();
				Ref<Material> base_mat = orig_ov.is_valid() ? orig_ov : cf->get_active_material(0);
				Ref<ShaderMaterial> sm_orig = base_mat;
				if (sm_orig.is_valid()) {
					Ref<ShaderMaterial> dup = sm_orig->duplicate();
					if (cs.floor_tex.is_valid()) {
						dup->set_shader_parameter("albedo_tex", cs.floor_tex);
						dup->set_shader_parameter("use_albedo_tex", true);
					}
					if (cs.floor_color.set) dup->set_shader_parameter("base_color", cs.floor_color.color);
					if (cs.checker_color.set) dup->set_shader_parameter("checker_color", cs.checker_color.color);
					if (cs.checker_size.set) dup->set_shader_parameter("checker_size", cs.checker_size.value);
					mat_overrides_.push_back({ cf->get_instance_id(), orig_ov, 0 });
					cf->set_material_override(dup);
				} else {
					Ref<BaseMaterial3D> bm_orig = base_mat;
					if (bm_orig.is_valid()) {
						Ref<BaseMaterial3D> dup = bm_orig->duplicate();
						if (cs.floor_tex.is_valid()) set_albedo_tex(dup, cs.floor_tex);
						if (cs.floor_color.set) dup->set_albedo(cs.floor_color.color);
						mat_overrides_.push_back({ cf->get_instance_id(), orig_ov, 0 });
						cf->set_material_override(dup);
					}
				}
			}
		}
		if (cs.line_color.set) {
			for (const char *ln : { "SideLineWest", "SideLineEast", "EndLineNorth", "EndLineSouth", "CenterLine" }) {
				if (MeshInstance3D *lmi = Object::cast_to<MeshInstance3D>(jvm_textures_io().node("stage.court_look", court, String(ln)))) {
					Ref<Material> orig_ov = lmi->get_material_override();
					Ref<Material> base_mat = orig_ov.is_valid() ? orig_ov : lmi->get_active_material(0);
					Ref<BaseMaterial3D> bm = base_mat;
					if (bm.is_valid()) {
						Ref<BaseMaterial3D> dup = bm->duplicate();
						dup->set_albedo(cs.line_color.color);
						mat_overrides_.push_back({ lmi->get_instance_id(), orig_ov, 0 });
						lmi->set_material_override(dup);
					}
				}
			}
		}
	}

	if (grass) {
		auto override_grass_floor_mesh = [&](MeshInstance3D *mi, const Ref<Texture2D> &tex, const ColorParam &tint) {
			if (!mi) return;
			Ref<Material> orig_ov = mi->get_material_override();
			Ref<Material> base_mat = orig_ov.is_valid() ? orig_ov : mi->get_active_material(0);
			Ref<ShaderMaterial> sm_orig = base_mat;
			if (sm_orig.is_valid()) {
				Ref<ShaderMaterial> dup = sm_orig->duplicate();
				if (tex.is_valid()) dup->set_shader_parameter("tex", tex);
				if (tint.set) dup->set_shader_parameter("tint", Vector3(tint.color.r, tint.color.g, tint.color.b));
				if (cs.grass_patch_color.set) dup->set_shader_parameter("patch_color", Vector3(cs.grass_patch_color.color.r, cs.grass_patch_color.color.g, cs.grass_patch_color.color.b));
				if (cs.grass_brightness.set) dup->set_shader_parameter("brightness", cs.grass_brightness.value);
				mat_overrides_.push_back({ mi->get_instance_id(), orig_ov, 0 });
				mi->set_material_override(dup);
			}
		};

		if (MeshInstance3D *env_floor = Object::cast_to<MeshInstance3D>(jvm_textures_io().node("stage.court_look", grass, "Backdrop/EnvFloor/EnvFloor"))) {
			override_grass_floor_mesh(env_floor, cs.grass_tex, cs.grass_tint);
		}
		if (Node *hills = jvm_textures_io().node("stage.court_look", grass, "Hills")) {
			std::vector<MeshInstance3D *> hmeshes;
			meshes_under(hills, hmeshes);
			Ref<Texture2D> htex = cs.hill_tex.is_valid() ? cs.hill_tex : cs.grass_tex;
			ColorParam htint = cs.hill_tint.set ? cs.hill_tint : cs.grass_tint;
			for (auto *hm : hmeshes) override_grass_floor_mesh(hm, htex, htint);
		}
		if (Node *gf = jvm_textures_io().node("stage.court_look", grass, "GrassField")) {
			auto set_gf_prop = [&](const StringName &prop, const Variant &val) {
				Variant prev = jvm_textures_io().get("stage.court_look", gf, prop);
				if (prev.get_type() != Variant::NIL) {
					prop_backups_.push_back({ gf->get_instance_id(), prop, prev });
					jvm_textures_io().set("stage.court_look", gf, prop, val);
				}
			};
			if (cs.grass_tex.is_valid()) set_gf_prop("ground_texture", cs.grass_tex);
			if (cs.grass_tint.set) set_gf_prop("base_tint", cs.grass_tint.color);
			if (cs.grass_tip_color.set) set_gf_prop("tip_color", cs.grass_tip_color.color);
			if (cs.grass_patch_color.set) set_gf_prop("patch_color", cs.grass_patch_color.color);
			if (cs.grass_grad_top.set) set_gf_prop("grad_top", cs.grass_grad_top.color);
			if (cs.grass_grad_bottom.set) set_gf_prop("grad_bottom", cs.grass_grad_bottom.color);
			if (cs.grass_brightness.set) set_gf_prop("brightness", cs.grass_brightness.value);

			Variant mv = jvm_textures_io().get("stage.court_look", gf, "_mat");
			if (ShaderMaterial *sm = Object::cast_to<ShaderMaterial>(mv)) {
				auto set_sp = [&](const StringName &param, const Variant &val) {
					shader_param_backups_.push_back({ sm->get_instance_id(), param, sm->get_shader_parameter(param) });
					sm->set_shader_parameter(param, val);
				};
				if (cs.grass_tex.is_valid()) {
					set_sp("grass_tex", cs.grass_tex);
					set_sp("use_tex", true);
				}
				if (cs.grass_tint.set) set_sp("base_tint", Vector3(cs.grass_tint.color.r, cs.grass_tint.color.g, cs.grass_tint.color.b));
				if (cs.grass_tip_color.set) set_sp("tip_color", Vector3(cs.grass_tip_color.color.r, cs.grass_tip_color.color.g, cs.grass_tip_color.color.b));
				if (cs.grass_patch_color.set) set_sp("patch_color", Vector3(cs.grass_patch_color.color.r, cs.grass_patch_color.color.g, cs.grass_patch_color.color.b));
				if (cs.grass_grad_top.set) set_sp("grad_top", Vector3(cs.grass_grad_top.color.r, cs.grass_grad_top.color.g, cs.grass_grad_top.color.b));
				if (cs.grass_grad_bottom.set) set_sp("grad_bottom", Vector3(cs.grass_grad_bottom.color.r, cs.grass_grad_bottom.color.g, cs.grass_grad_bottom.color.b));
				if (cs.grass_brightness.set) set_sp("brightness", cs.grass_brightness.value);
			}
			Variant gfm = jvm_textures_io().get("stage.court_look", gf, "_gfloor_mat");
			if (ShaderMaterial *gfm_sm = Object::cast_to<ShaderMaterial>(gfm)) {
				auto set_gp = [&](const StringName &param, const Variant &val) {
					shader_param_backups_.push_back({ gfm_sm->get_instance_id(), param, gfm_sm->get_shader_parameter(param) });
					gfm_sm->set_shader_parameter(param, val);
				};
				if (cs.grass_tex.is_valid()) set_gp("tex", cs.grass_tex);
				if (cs.grass_tint.set) set_gp("tint", Vector3(cs.grass_tint.color.r, cs.grass_tint.color.g, cs.grass_tint.color.b));
				if (cs.grass_patch_color.set) set_gp("patch_color", Vector3(cs.grass_patch_color.color.r, cs.grass_patch_color.color.g, cs.grass_patch_color.color.b));
				if (cs.grass_brightness.set) set_gp("brightness", cs.grass_brightness.value);
			}
		}
	}
	(void)scene;
}

void JVTextureLoader::apply_ball_spec(Node *ball, const BallSpec &bs) {
	if (!ball || !bs.present) return;
	std::vector<MeshInstance3D *> meshes;
	meshes_under(ball, meshes);
	auto is_non_ball_fx = [](MeshInstance3D *mi) -> bool {
		String n = String(mi->get_name()).to_lower();
		if (n == "shadow" || n == "landingring" || n == "outline" || n == "inneroutline") return true;
		if (n.contains("shadow") || n.contains("outline") || n.contains("ring") || n.contains("column")) return true;
		Ref<Material> m = mi->get_material_override();
		if (m.is_null() && mi->get_mesh().is_valid() && mi->get_mesh()->get_surface_count() > 0)
			m = mi->get_active_material(0);
		Ref<ShaderMaterial> sm = m;
		if (sm.is_valid() && sm->get_shader().is_valid()) {
			String sp = sm->get_shader()->get_path().to_lower();
			if (sp.contains("land_column") || sp.contains("outline") || sp.contains("shadow")) return true;
		}
		return false;
	};
	for (auto *mi : meshes) {
		if (is_non_ball_fx(mi)) continue;
		Ref<Mesh> mesh = mi->get_mesh();
		if (mesh.is_null()) continue;
		Ref<Material> mat_ov = mi->get_material_override();
		Ref<ShaderMaterial> sm_ov = mat_ov;
		if (sm_ov.is_valid()) {
			Ref<ShaderMaterial> dup = sm_ov->duplicate();
			if (bs.albedo_tex.is_valid()) dup->set_shader_parameter("albedo_tex", bs.albedo_tex);
			if (bs.albedo_color.set) dup->set_shader_parameter("albedo_color", bs.albedo_color.color);
			if (bs.color_a.set) dup->set_shader_parameter("color_a", bs.color_a.color);
			if (bs.color_b.set) dup->set_shader_parameter("color_b", bs.color_b.color);
			if (bs.shade.set) dup->set_shader_parameter("shade", bs.shade.value);
			mat_overrides_.push_back({ mi->get_instance_id(), mat_ov, 0 });
			mi->set_material_override(dup);
			continue;
		}
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			Ref<Material> act = mi->get_active_material(s);
			Ref<ShaderMaterial> sm = act;
			if (sm.is_valid()) {
				Ref<ShaderMaterial> dup = sm->duplicate();
				if (bs.albedo_tex.is_valid()) dup->set_shader_parameter("albedo_tex", bs.albedo_tex);
				if (bs.albedo_color.set) dup->set_shader_parameter("albedo_color", bs.albedo_color.color);
				if (bs.color_a.set) dup->set_shader_parameter("color_a", bs.color_a.color);
				if (bs.color_b.set) dup->set_shader_parameter("color_b", bs.color_b.color);
				if (bs.shade.set) dup->set_shader_parameter("shade", bs.shade.value);
				overrides_.push_back({ mi->get_instance_id(), s, mi->get_surface_override_material(s), 0 });
				mi->set_surface_override_material(s, dup);
			} else {
				Ref<BaseMaterial3D> bm = act;
				if (bm.is_valid()) {
					Ref<BaseMaterial3D> dup = bm->duplicate();
					if (bs.albedo_tex.is_valid()) set_albedo_tex(dup, bs.albedo_tex);
					if (bs.albedo_color.set) dup->set_albedo(bs.albedo_color.color);
					overrides_.push_back({ mi->get_instance_id(), s, mi->get_surface_override_material(s), 0 });
					mi->set_surface_override_material(s, dup);
				}
			}
		}
	}
}

void JVTextureLoader::apply_scene_cosmetics(bool force) {
	if (!get_tree()) return;
	Node *scene = get_tree()->get_current_scene();
	if (!scene) return;
	uint64_t sid = scene->get_instance_id();
	JVM_PROF("textures.cosmetics");








	if (!force) {
		cosmetics_periodic_count_++;
		return;
	}
	cosmetics_force_count_++;

	std::vector<Node *> courts, grasses;
	JVM_PROF("textures.cosmetics.full");
	collect_named(scene, "Court", courts);
	if (Node *c = find_court_node(scene)) courts.insert(courts.begin(), c);
	collect_named(scene, "Grass", grasses);
	if (Node *g = find_grass_node(scene)) grasses.insert(grasses.begin(), g);
	if (Node *root = get_tree()->get_root()) {
		collect_named(root, "Court", courts);
		collect_named(root, "Grass", grasses);
	}
	std::vector<Node *> balls = all_balls(this);

	auto uniq = [](std::vector<Node *> &v) {
		std::vector<Node *> o;
		std::set<uint64_t> s;
		for (Node *n : v) {
			if (!n || s.count(n->get_instance_id())) continue;
			s.insert(n->get_instance_id());
			o.push_back(n);
		}
		v.swap(o);
	};
	uniq(courts); uniq(grasses);

	uint64_t cid = 0, gid = 0, bid = 0, gmid = 0;
	for (Node *n : courts) cid ^= n->get_instance_id();
	for (Node *n : grasses) gid ^= n->get_instance_id();
	for (Node *n : balls) bid ^= n->get_instance_id();
	for (Node *grass : grasses) {
		if (Node *gf = jvm_textures_io().node("stage.court_look", grass, "GrassField")) {
			if (Object *mo = Object::cast_to<Object>(jvm_textures_io().get("stage.court_look", gf, "_mat"))) gmid ^= mo->get_instance_id();
		}
	}

	bool visuals_ok = true;
	const String shown_ball = shown_pick("ball");
	if (shown_ball.is_empty()) {
		for (Node *b : balls) if (custom_visual_on(b)) { visuals_ok = false; break; }
	} else {
		if (balls.empty()) visuals_ok = custom_visual_alive();
		else for (Node *b : balls) if (!custom_visual_on(b)) { visuals_ok = false; break; }
	}

	if (!force && sid == styled_scene_id_ && cid == styled_court_id_ && gid == styled_grass_id_ && bid == styled_ball_id_ && gmid == styled_grass_mat_id_
			&& visuals_ok) {
		return;
	}






	bool keep = !force && sid == styled_scene_id_ && sid != 0;
	if (!keep) {
		JVM_PROF("textures.cosmetics.clear");
		clear_scene_cosmetics();
	}
	styled_scene_id_ = sid;
	styled_court_id_ = cid;
	styled_grass_id_ = gid;
	styled_ball_id_ = bid;
	styled_grass_mat_id_ = gmid;
	styled_picked_stage_ = picked_stage_;
	styled_picked_ball_ = picked_ball_;

	auto already = [&](Node *n) -> bool {
		return keep && n && styled_applied_ids_.count(n->get_instance_id());
	};
	auto mark = [&](Node *n) {
		if (n) styled_applied_ids_.insert(n->get_instance_id());
	};

	auto is_equipped_local = [&](sk::SkinId id) {
		for (auto eq : equipped_) if (eq == id) return true;
		return false;
	};
	String stage_id = shown_pick("stage").to_upper();
	String ball_id = shown_ball.to_upper();
	for (auto id : load_order_) {
		const Skin &sk = skins_[id];
		String hid = G(sk::format_id(id));
		bool picked_stage = sk.picker && sk.type == "stage" && hid == stage_id;
		bool picked_ball = sk.picker && sk.type == "ball" && hid == ball_id;
		bool always = sk.global && !sk.picker;
		if (!always && !picked_stage && !picked_ball && !is_equipped_local(id)) continue;
		if (sk.net.present && always) {
			for (Node *court : courts) {
				if (already(court)) continue;
				apply_net_spec(court, sk.net);
				mark(court);
			}
		}
		if ((sk.court.present || sk.sky.present || sk.env.present || sk.audio.present) && picked_stage) {
			if (courts.empty()) {
				Node *grass = grasses.empty() ? nullptr : grasses[0];
				apply_picked_stage(scene, nullptr, grass, sk);
			} else {
				for (Node *court : courts) {
					if (already(court)) continue;
					Node *grass = nullptr;
					if (Node *p = court->get_parent()) {
						grass = jvm_textures_io().node("stage.court_look", p, "Grass");
						if (!grass && p->get_parent()) grass = jvm_textures_io().node("stage.court_look", p->get_parent(), "Grass");
					}
					if (!grass && !grasses.empty()) grass = grasses[0];
					apply_picked_stage(scene, court, grass, sk);
					mark(court);
				}
			}
		}
		if (sk.ball.present && picked_ball) {
			for (Node *b : balls) {
				if (already(b)) continue;
				apply_picked_ball(b, sk);
				mark(b);
			}
		}
	}
}


int32_t JVTextureLoader::my_peer() {
	Ref<MultiplayerPeer> peer = get_tree()->get_multiplayer()->get_multiplayer_peer();
	if (peer.is_null() || peer->get_class() == "OfflineMultiplayerPeer") return 1;
	return get_tree()->get_multiplayer()->get_unique_id();
}

void JVTextureLoader::clear_overrides(int32_t peer) {
	for (auto it = overrides_.begin(); it != overrides_.end();) {
		if (it->owner_peer != peer) { ++it; continue; }
		if (MeshInstance3D *m = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(it->mesh_id)))
			m->set_surface_override_material(it->surface, it->previous);
		it = overrides_.erase(it);
	}
	for (auto it = mat_overrides_.begin(); it != mat_overrides_.end();) {
		if (it->owner_peer != peer) { ++it; continue; }
		if (MeshInstance3D *m = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(it->mesh_id)))
			m->set_material_override(it->previous);
		it = mat_overrides_.erase(it);
	}
	for (auto it = visibility_backups_.begin(); it != visibility_backups_.end();) {
		if (it->owner_peer != peer) { ++it; continue; }
		if (Node3D *n = Object::cast_to<Node3D>(ObjectDB::get_instance(it->node_id)))
			n->set_visible(it->was_visible);
		it = visibility_backups_.erase(it);
	}
	for (auto it = spawned_nodes_.begin(); it != spawned_nodes_.end();) {
		if (it->owner_peer != peer) { ++it; continue; }
		if (Node *n = Object::cast_to<Node>(ObjectDB::get_instance(it->node_id))) {
			if (n->get_parent()) n->get_parent()->remove_child(n);
			n->queue_free();
		}
		it = spawned_nodes_.erase(it);
	}
	for (auto it = outfit_bindings_.begin(); it != outfit_bindings_.end();) {
		if (it->owner_peer != peer) { ++it; continue; }
		it = outfit_bindings_.erase(it);
	}
	applied_.erase(peer);
	applied_player_id_.erase(peer);
	applied_model_id_.erase(peer);
}

Node *godot::jvm_find_model_rig(Node *player) {
	if (!player) return nullptr;
	if (jvm_textures_io().get("player.look", player, "_surface_slots").get_type() == Variant::DICTIONARY) return player;
	for (int i = 0; i < player->get_child_count(); i++) {
		if (Node *hit = jvm_find_model_rig(player->get_child(i))) return hit;
	}
	return nullptr;
}

static Skeleton3D *find_skeleton_under(Node *root) {
	if (!root) return nullptr;
	if (Skeleton3D *sk = Object::cast_to<Skeleton3D>(root)) return sk;
	for (int i = 0; i < root->get_child_count(); i++) {
		if (Skeleton3D *hit = find_skeleton_under(root->get_child(i))) return hit;
	}
	return nullptr;
}

static float tint_mix_for_mode(const String &mode) {
	if (mode == "multiply") return 1.0f;
	if (mode == "mix") return 0.35f;
	return 0.0f;
}

void JVTextureLoader::apply_outfit_to(Node *player, int32_t peer, const OutfitSpec &outfit) {
	if (!player || !outfit.present) return;

	auto lookup_rule = [&](const String &slot_name, const String &mesh_name, OutfitSlotRule &out) -> bool {
		String s = slot_name.to_lower();
		String mn = mesh_name.to_lower();
		if (mn.begins_with("numbermesh")) {
			if (outfit.slots.count("number")) { out = outfit.slots.at("number"); return true; }
			if (outfit.slots.count("numbermesh")) { out = outfit.slots.at("numbermesh"); return true; }
			return false;
		}
		if (outfit.slots.count(s)) { out = outfit.slots.at(s); return true; }
		if (s == "jerseyfront" || s == "jerseyback" || s == "jerseycollar" || s == "accent") {
			if (outfit.slots.count("jersey")) { out = outfit.slots.at("jersey"); return true; }
		}
		if (s == "practice_jersey_accent") {
			if (outfit.slots.count("practice_jersey")) { out = outfit.slots.at("practice_jersey"); return true; }
			if (outfit.slots.count("jersey")) { out = outfit.slots.at("jersey"); return true; }
		}
		if (s == "pants_accent" || s == "pantshem" || s == "pantswaist") {
			if (outfit.slots.count("shorts")) { out = outfit.slots.at("shorts"); return true; }
		}
		if (s == "shoes" && outfit.slots.count("shoe")) { out = outfit.slots.at("shoe"); return true; }
		if (s == "socks" && outfit.slots.count("sock")) { out = outfit.slots.at("sock"); return true; }
		if (s == "kneepad" && outfit.slots.count("kneepads")) { out = outfit.slots.at("kneepads"); return true; }
		return false;
	};

	Node *rig = jvm_find_model_rig(player);
	if (rig) {
		Variant ssv = jvm_textures_io().get("player.look", rig, "_surface_slots");
		if (ssv.get_type() == Variant::DICTIONARY) {
			Dictionary ss = ssv;
			Array mesh_objs = ss.keys();
			for (int i = 0; i < mesh_objs.size(); i++) {
				MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(mesh_objs[i]);
				if (!mi) continue;
				Variant surf_v = ss[mesh_objs[i]];
				if (surf_v.get_type() != Variant::DICTIONARY) continue;
				Dictionary surfs = surf_v;
				Array sidxs = surfs.keys();
				String mname = mi->get_name();
				for (int j = 0; j < sidxs.size(); j++) {
					int sidx = int(int64_t(sidxs[j]));
					String slot_name = String(surfs[sidxs[j]]);
					OutfitSlotRule rule;
					if (!lookup_rule(slot_name, mname, rule)) continue;

					Ref<Material> prev = mi->get_surface_override_material(sidx);
					Ref<Material> act = mi->get_active_material(sidx);
					if (act.is_null()) continue;
					Ref<ShaderMaterial> sm_act = act;
					if (sm_act.is_valid()) {
						Ref<ShaderMaterial> dup = sm_act->duplicate();
						if (rule.tex.is_valid()) {
							Ref<Shader> sh = get_textured_toon_shader(sm_act->get_shader());
							if (sh.is_valid()) {
								dup->set_shader(sh);
								dup->set_shader_parameter("jv_skin_tex", rule.tex);
								String mode = rule.tint_mode.is_empty() ? outfit.default_tint_mode : rule.tint_mode;
								dup->set_shader_parameter("jv_skin_tint_mix", tint_mix_for_mode(mode));
							}
						}
						if (rule.color.set) dup->set_shader_parameter("albedo", rule.color.color);
						overrides_.push_back({ mi->get_instance_id(), sidx, prev, peer });
						mi->set_surface_override_material(sidx, dup);
						outfit_bindings_.push_back({ mi->get_instance_id(), sidx, sm_act->get_instance_id(), dup->get_instance_id(), peer });
					} else {
						Ref<BaseMaterial3D> bm_act = act;
						if (bm_act.is_valid()) {
							Ref<BaseMaterial3D> dup = bm_act->duplicate();
							if (rule.tex.is_valid()) set_albedo_tex(dup, rule.tex);
							if (rule.color.set) dup->set_albedo(rule.color.color);
							overrides_.push_back({ mi->get_instance_id(), sidx, prev, peer });
							mi->set_surface_override_material(sidx, dup);
						}
					}
				}
			}
			return;
		}
	}

	auto apply_rule_to_mat = [&](const Ref<Material> &act, const OutfitSlotRule &rule) -> Ref<Material> {
		if (act.is_null()) return Ref<Material>();
		Ref<ShaderMaterial> sm = act;
		if (sm.is_valid()) {
			Ref<ShaderMaterial> dup = sm->duplicate();
			if (rule.color.set) {
				dup->set_shader_parameter("albedo", rule.color.color);
				Color sss = rule.color.color.lerp(Color(1, 1, 1), 0.42f);
				sss.a = 1.0f;
				dup->set_shader_parameter("sss_tint", sss);
			}
			if (rule.tex.is_valid()) {
				dup->set_shader_parameter("albedo_tex", rule.tex);
				dup->set_shader_parameter("jv_skin_tex", rule.tex);
			}
			return dup;
		}
		Ref<BaseMaterial3D> bm = act;
		if (bm.is_valid()) {
			Ref<BaseMaterial3D> dup = bm->duplicate();
			if (rule.tex.is_valid()) set_albedo_tex(dup, rule.tex);
			if (rule.color.set) dup->set_albedo(rule.color.color);
			return dup;
		}
		return Ref<Material>();
	};

	std::vector<MeshInstance3D *> meshes;
	meshes_under(player, meshes);
	for (auto *mi : meshes) {
		Ref<Mesh> mesh = mi->get_mesh();
		if (mesh.is_null()) continue;
		String mn = String(mi->get_name()).to_lower();
		String guessed_slot;
		if (mn.contains("jersey")) guessed_slot = "jersey";
		else if (mn.contains("short")) guessed_slot = "shorts";
		else if (mn.contains("sock")) guessed_slot = "socks";
		else if (mn.contains("shoe") || mn.contains("cleet")) guessed_slot = "shoes";
		else if (mn.contains("knee")) guessed_slot = "kneepad";
		else if (mn.contains("hood")) guessed_slot = "hoodie";
		else if (mn.contains("aero") || mn.contains("bean") || mn == "aeromesh") guessed_slot = "body";
		else if (mn.contains("body") || mn.contains("head") || mn.contains("skin")) guessed_slot = "skin";

		OutfitSlotRule rule;
		bool found = false;
		if (!guessed_slot.is_empty() && lookup_rule(guessed_slot, mi->get_name(), rule)) found = true;
		if (!found) {


			for (const char *k : { "body", "all", "bean", "aero", "skin" }) {
				if (outfit.slots.count(k)) {
					rule = outfit.slots.at(k);
					found = true;
					break;
				}
			}
		}
		if (!found) continue;


		Ref<Material> mov = mi->get_material_override();
		if (mov.is_valid()) {
			Ref<Material> dup = apply_rule_to_mat(mov, rule);
			if (dup.is_valid()) {
				mat_overrides_.push_back({ mi->get_instance_id(), mov, peer });
				mi->set_material_override(dup);
			}
			continue;
		}
		for (int sidx = 0; sidx < mesh->get_surface_count(); sidx++) {
			Ref<Material> act = mi->get_active_material(sidx);
			Ref<Material> dup = apply_rule_to_mat(act, rule);
			if (dup.is_null()) continue;
			overrides_.push_back({ mi->get_instance_id(), sidx, mi->get_surface_override_material(sidx), peer });
			mi->set_surface_override_material(sidx, dup);
		}
	}
}

static int32_t resolve_bone_index(Skeleton3D *skel, const String &mount) {
	if (!skel) return -1;
	String m = mount.to_lower();
	if (m == "head") {
		for (const char *c : { "head", "Head", "head_bone" }) {
			int32_t idx = skel->find_bone(c);
			if (idx >= 0) return idx;
		}
	} else if (m == "chest" || m == "back" || m == "torso") {
		for (const char *c : { "spine_1", "spine.001", "spine_001", "spine1", "Spine_1", "spine" }) {
			int32_t idx = skel->find_bone(c);
			if (idx >= 0) return idx;
		}
	} else if (m == "hips") {
		for (const char *c : { "spine", "hips", "root" }) {
			int32_t idx = skel->find_bone(c);
			if (idx >= 0) return idx;
		}
	}
	return skel->find_bone(mount);
}

static void walk_bone_chain(Skeleton3D *sk, int32_t b, const String &prefix, bool use_prefix, std::vector<int32_t> &chain, std::set<int32_t> &visited, std::vector<std::vector<int32_t>> &out) {
	if (visited.count(b)) return;
	visited.insert(b);
	chain.push_back(b);
	PackedInt32Array kids = sk->get_bone_children(b);
	std::vector<int32_t> valid_kids;
	for (int i = 0; i < kids.size(); i++) {
		int32_t c = kids[i];
		if (!use_prefix || sk->get_bone_name(c).to_lower().begins_with(prefix))
			valid_kids.push_back(c);
	}
	if (valid_kids.empty()) {
		if (!chain.empty()) out.push_back(chain);
		return;
	}
	int32_t first = valid_kids[0];
	walk_bone_chain(sk, first, prefix, use_prefix, chain, visited, out);
	for (size_t i = 1; i < valid_kids.size(); i++) {
		std::vector<int32_t> branch;
		walk_bone_chain(sk, valid_kids[i], prefix, use_prefix, branch, visited, out);
	}
}

void JVTextureLoader::setup_spring_physics(Node *acc_root, const AccessorySpec &acc) {
	if (!acc.jiggle) return;
	Skeleton3D *sk = find_skeleton_under(acc_root);
	if (!sk) return;

	std::vector<std::vector<int32_t>> chains;
	std::set<int32_t> visited;
	const int32_t n = sk->get_bone_count();

	bool any_prefix = false;
	if (!acc.tail_prefix.is_empty()) {
		for (int32_t b = 0; b < n; b++) {
			if (sk->get_bone_name(b).to_lower().begins_with(acc.tail_prefix)) { any_prefix = true; break; }
		}
	}
	for (int32_t b = 0; b < n; b++) {
		if (any_prefix) {
			if (!sk->get_bone_name(b).to_lower().begins_with(acc.tail_prefix)) continue;
			int32_t p = sk->get_bone_parent(b);
			if (p >= 0 && sk->get_bone_name(p).to_lower().begins_with(acc.tail_prefix)) continue;
			std::vector<int32_t> chain;
			walk_bone_chain(sk, b, acc.tail_prefix, true, chain, visited, chains);
		} else {
			int32_t p = sk->get_bone_parent(b);
			if (p < 0) continue;
			int32_t gp = sk->get_bone_parent(p);
			if (gp >= 0) continue;
			std::vector<int32_t> chain;
			walk_bone_chain(sk, b, "", false, chain, visited, chains);
		}
	}
	if (chains.empty()) return;

	if (acc.space == "local" && acc.mount == "chest") {
		SpringBoneCollisionCapsule3D *torso = memnew(SpringBoneCollisionCapsule3D);
		torso->set_name("JVM_TorsoCollider");
		torso->set_radius(0.28f);
		torso->set_height(1.1f);
		torso->set_position(Vector3(0.0f, -0.30f, 0.38f));
		sk->add_child(torso);
	}

	SpringBoneSimulator3D *sim = memnew(SpringBoneSimulator3D);
	sim->set_name("JVM_SpringBones");
	sim->set_setting_count(int32_t(chains.size()));
	for (size_t i = 0; i < chains.size(); i++) {
		int32_t idx = int32_t(i);
		sim->set_root_bone(idx, chains[i].front());
		sim->set_end_bone(idx, chains[i].back());
		sim->set_extend_end_bone(idx, true);
		sim->set_end_bone_direction(idx, SpringBoneSimulator3D::BONE_DIRECTION_FROM_PARENT);
		sim->set_end_bone_length(idx, 0.25f);
		sim->set_radius(idx, acc.radius);
		sim->set_stiffness(idx, acc.stiffness);
		sim->set_drag(idx, acc.drag);
		sim->set_gravity(idx, acc.gravity);
		sim->set_gravity_direction(idx, Vector3(0.0f, -1.0f, 0.0f));
		sim->set_rotation_axis(idx, SpringBoneSimulator3D::ROTATION_AXIS_ALL);
		sim->set_enable_all_child_collisions(idx, true);
	}
	sk->add_child(sim);
}

void godot::jvm_convert_gltf_materials_to_toon(Node *root) {
	std::vector<MeshInstance3D *> meshes;
	meshes_under(root, meshes);
	static const char *kAccToonShader =
		"shader_type spatial;\n"
		"render_mode unshaded, cull_back;\n"
		"uniform sampler2D albedo_tex : source_color, hint_default_white, filter_linear_mipmap_anisotropic;\n"
		"uniform vec4 albedo_color : source_color = vec4(1.0);\n"
		"uniform vec3 key_dir = vec3(-0.45, 0.62, 0.65);\n"
		"uniform float band_edge = 0.12;\n"
		"uniform float shade = 0.35;\n"
		"uniform float band_softness = 0.03;\n"
		"void fragment() {\n"
		"\tvec4 t = texture(albedo_tex, UV) * albedo_color;\n"
		"\tfloat ndl = dot(normalize(NORMAL), normalize(key_dir));\n"
		"\tfloat lit = smoothstep(band_edge - band_softness, band_edge + band_softness, ndl);\n"
		"\tALBEDO = t.rgb * mix(1.0 - shade, 1.0, lit);\n"
		"}\n";
	static Ref<Shader> acc_sh;
	if (acc_sh.is_null()) {
		acc_sh.instantiate();
		acc_sh->set_code(kAccToonShader);
	}
	for (auto *mi : meshes) {
		Ref<Mesh> mesh = mi->get_mesh();
		if (mesh.is_null()) continue;
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			Ref<BaseMaterial3D> bm = mi->get_active_material(s);
			if (bm.is_null()) continue;
			Ref<ShaderMaterial> sm;
			sm.instantiate();
			sm->set_shader(acc_sh);
			if (bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_valid())
				sm->set_shader_parameter("albedo_tex", bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO));
			sm->set_shader_parameter("albedo_color", bm->get_albedo());
			mi->set_surface_override_material(s, sm);
		}
	}
}


static bool has_grafted_model(Node *n) {
	if (!n) return false;
	if (n->has_meta("jvm_model")) return true;
	for (int i = 0; i < n->get_child_count(); i++) {
		if (has_grafted_model(n->get_child(i))) return true;
	}
	return false;
}



String godot::jvm_model_id_worn_by(Node *player) {
	if (!player || !player->is_inside_tree()) return String();



	if (!has_grafted_model(player)) return String();
	return String(jvm_textures_io().core("model_get_shown"));
}




static jvm_anchor::Overrides anchor_overrides_for(const String &model_id) {
	static Dictionary cache;
	static uint64_t cache_mtime = 0;
	static uint64_t checked_ms = 0;
	uint64_t now = Time::get_singleton() ? Time::get_singleton()->get_ticks_msec() : 0;
	if (checked_ms == 0 || now - checked_ms > 2000) {
		checked_ms = now ? now : 1;
		String path = jvm::content::model_settings_read_path();
		uint64_t mt = FileAccess::file_exists(path) ? FileAccess::get_modified_time(path) : 0;
		if (mt != cache_mtime) {
			cache_mtime = mt;
			cache = Dictionary();
			if (mt) {
				Variant v = jvm::load_json(path);
				if (v.get_type() == Variant::DICTIONARY) {
					Variant m = Dictionary(v).get("models", Dictionary());
					if (m.get_type() == Variant::DICTIONARY) {
						Dictionary md = m;
						Array k = md.keys();
						for (int i = 0; i < k.size(); i++) if (md[k[i]].get_type() == Variant::DICTIONARY) cache[String(k[i]).to_upper()] = md[k[i]];
					}
				}
			}
		}
	}
	if (model_id.is_empty()) return jvm_anchor::Overrides();
	Variant e = cache.get(model_id.to_upper(), Variant());
	if (e.get_type() != Variant::DICTIONARY) return jvm_anchor::Overrides();
	return jvm_anchor::parse_overrides(Dictionary(e).get("anchors", Variant()));
}





void JVTextureLoader::pose_game_accessory_roots(Node *player) {
	if (!player) return;
	JVM_PROF("textures.anchors");
	jvm_anchor::sync_player(player, anchor_overrides_for(jvm_model_id_worn_by(player)));
}

Dictionary JVTextureLoader::describe_accessory_anchors(Node *player) {
	pose_game_accessory_roots(player);
	return jvm_anchor::describe(player);
}

void JVTextureLoader::apply_accessory_to(Node *player, int32_t peer, const AccessorySpec &acc, const String &tag, const String &model_id) {
	if (!player || !acc.present) return;

	const AccessoryFit *fp = nullptr;
	if (!model_id.is_empty()) {
		auto it = acc.fit.find(model_id.to_upper());
		if (it != acc.fit.end()) fp = &it->second;
	}
	AccessorySpec use = acc;
	if (fp) {
		if (fp->has_offset) use.offset = fp->offset;
		if (fp->has_rotation) use.rotation_deg = fp->rotation_deg;
		if (fp->has_scale) use.scale = fp->scale;
		if (fp->has_mount) use.mount = fp->mount;
	}

	Node *rig = jvm_find_model_rig(player);
	if (use.hide_vanilla_hats && rig) {
		Variant ar = jvm_textures_io().get("player.outfit", rig, "_acc_roots");
		if (ar.get_type() == Variant::DICTIONARY) {
			Dictionary d = ar;
			for (const char *slot : { "head", "hat" }) {
				if (d.has(slot)) {
					if (Node3D *vn = Object::cast_to<Node3D>(d[slot])) {
						visibility_backups_.push_back({ vn->get_instance_id(), vn->is_visible(), peer });
						vn->set_visible(false);
					}
				}
			}
		}
	}

	String why;
	Node *gen = jvm::safe::load_glb(use.model_path, why);
	if (!gen) {
		jvm::safe::note_refusal("accessory mesh " + use.model_path + ": " + why);
		return;
	}
	Node3D *acc_root = Object::cast_to<Node3D>(gen);
	if (!acc_root) {
		acc_root = memnew(Node3D);
		acc_root->add_child(gen);
	}
	acc_root->set_name("JVM_" + tag);
	acc_root->set_meta("jvm_accessory", true);

	if (use.shader_mode == "toon") jvm_convert_gltf_materials_to_toon(acc_root);

	Node *mount_parent = player;
	Transform3D space_prefix;


	Skeleton3D *skel = nullptr;
	if (rig) {
		Variant sv = jvm_textures_io().get("player.outfit", rig, "_skel");
		if (sv.get_type() == Variant::OBJECT) skel = Object::cast_to<Skeleton3D>(sv.operator Object *());
	}
	if (!skel) skel = find_skeleton_under(player);
	if (!skel && rig) skel = find_skeleton_under(rig);
	if (skel) {
		int32_t bone_idx = resolve_bone_index(skel, use.mount);



		String slot = tag;
		jvm_anchor::Kind kind = jvm_anchor::kind_of_mount(use.mount);
		if (tag.begins_with("Face_")) slot = "face_" + tag;
		if (bone_idx >= 0 && use.space == "character")
			space_prefix = skel->get_bone_global_rest(bone_idx).affine_inverse();
		Vector3 rad_rot0 = use.rotation_deg * (float(Math_PI) / 180.0f);
		Transform3D xf0 = space_prefix * Transform3D(Basis::from_euler(rad_rot0) * Basis::from_scale(use.scale), use.offset);
		Vector3 place = bone_idx >= 0 ? (skel->get_bone_global_rest(bone_idx) * xf0).origin : xf0.origin;
		Node3D *seat = jvm_anchor::seat_for_spawn(skel, kind, bone_idx, slot, tag.get_slice("_", 1), fp && fp->has_offset,
				place, anchor_overrides_for(model_id));
		if (seat) {
			seat->set_name("JVM_Mount_" + tag);
			spawned_nodes_.push_back({ seat->get_instance_id(), peer });
			mount_parent = seat;
		}
	}

	Vector3 rad_rot = use.rotation_deg * (float(Math_PI) / 180.0f);
	Basis rs = Basis::from_euler(rad_rot) * Basis::from_scale(use.scale);
	Transform3D xf = space_prefix * Transform3D(rs, use.offset);
	acc_root->set_transform(xf);

	mount_parent->add_child(acc_root);
	spawned_nodes_.push_back({ acc_root->get_instance_id(), peer });

	setup_spring_physics(acc_root, use);
	if (fp && !fp->parts.empty()) {
		std::vector<Node *> stack{ acc_root };
		while (!stack.empty()) {
			Node *n = stack.back();
			stack.pop_back();
			String nm = String(n->get_name()).to_lower();
			const AccessoryPartFit *part = nullptr;
			for (const auto &kv : fp->parts) {
				if (nm == kv.first || nm.begins_with(kv.first) || nm.contains(kv.first)) { part = &kv.second; break; }
			}
			if (part) {
				if (Node3D *n3 = Object::cast_to<Node3D>(n)) {
					Vector3 rad = part->rotation_deg * (float(Math_PI) / 180.0f);
					Basis rs = Basis::from_euler(rad) * Basis::from_scale(part->scale);
					n3->set_transform(Transform3D(rs, part->offset));
				}
				continue;
			}
			for (int i = n->get_child_count() - 1; i >= 0; i--) stack.push_back(n->get_child(i));
		}
	}
}

void JVTextureLoader::apply_skins_to(Node *player, int32_t peer, const std::vector<sk::SkinId> &ids) {
	std::vector<sk::SkinId> have;
	for (auto id : ids) if (skins_.count(id) && !skins_[id].global) have.push_back(id);
	uint64_t pid = player ? player->get_instance_id() : 0;
	String mid = jvm_model_id_worn_by(player);
	auto it = applied_.find(peer);
	if (it != applied_.end() && it->second == have && applied_player_id_[peer] == pid && applied_model_id_[peer] == mid && player_overrides_intact(peer)) {
		return;
	}
	clear_overrides(peer);
	applied_[peer] = have;
	applied_player_id_[peer] = pid;
	applied_model_id_[peer] = mid;
	if (have.empty() || !player) return;

	std::vector<MeshInstance3D *> meshes;
	meshes_under(player, meshes);
	for (auto *m : meshes) {
		Ref<Mesh> mesh = m->get_mesh();
		if (mesh.is_null()) continue;
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			Ref<BaseMaterial3D> mat = m->get_active_material(s);
			if (mat.is_null()) continue;
			Ref<Texture2D> alb = mat->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
			if (alb.is_null()) continue;
			String path = alb->get_path();
			for (auto id : have) {
				const Skin &sk = skins_[id];
				if (!sk.map.has(path)) continue;
				Ref<BaseMaterial3D> dup = mat->duplicate();
				set_albedo_tex(dup, sk.map[path]);
				overrides_.push_back({ m->get_instance_id(), s, m->get_surface_override_material(s), peer });
				m->set_surface_override_material(s, dup);
				break;
			}
		}
	}

	auto unplaced = [&](sk::SkinId id) {
		std::string sid = S(G(sk::format_id(id)));
		for (const auto &u : unplaced_acc_) if (u == sid) return true;
		return false;
	};
	for (auto id : have) {
		const Skin &sk = skins_[id];
		if (sk.outfit.present) apply_outfit_to(player, peer, sk.outfit);
		const bool locker = accessories_injected_ && is_locker_accessory(id) && !unplaced(id);
		if (sk.hat.present && !locker) apply_accessory_to(player, peer, sk.hat, "Hat_" + G(sk::format_id(id)), mid);
		if (sk.face.present && !locker) apply_accessory_to(player, peer, sk.face, "Face_" + G(sk::format_id(id)), mid);
		if (sk.back.present && !locker) apply_accessory_to(player, peer, sk.back, "Back_" + G(sk::format_id(id)), mid);
	}
}

bool JVTextureLoader::player_overrides_intact(int32_t peer) const {
	for (const auto &b : outfit_bindings_) {
		if (b.owner_peer != peer) continue;
		MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(b.mesh_id));
		if (!mi) return false;
		Ref<Material> cur = mi->get_surface_override_material(b.surface);
		if (cur.is_null() || cur->get_instance_id() != b.custom_mat_id) return false;
	}
	for (const auto &sn : spawned_nodes_) {
		if (sn.owner_peer != peer) continue;
		if (!ObjectDB::get_instance(sn.node_id)) return false;
	}
	return true;
}

void JVTextureLoader::sync_outfit_bindings() {
	static const char *kSyncUniforms[] = {
		"albedo", "sleeve_color", "sleeve_cut", "sleeve_band",
		"hidden", "flash", "flash_color", "world_lit", "world_mix", "world_key"
	};
	for (const auto &b : outfit_bindings_) {
		ShaderMaterial *orig = Object::cast_to<ShaderMaterial>(ObjectDB::get_instance(b.orig_mat_id));
		ShaderMaterial *cust = Object::cast_to<ShaderMaterial>(ObjectDB::get_instance(b.custom_mat_id));
		if (!orig || !cust) continue;
		for (const char *u : kSyncUniforms) {
			Variant v = orig->get_shader_parameter(u);
			if (v.get_type() != Variant::NIL) cust->set_shader_parameter(u, v);
		}
	}
}

void JVTextureLoader::refresh_display_avatars() {
	if (!get_tree()) return;
	std::vector<Node *> portraits;
	jvm::collect_portrait_players(get_tree()->get_root(), portraits);
	bool any = false;
	for (Node *pl : portraits) {
		if (!jvm::display_looks_like_me(jvm_textures_io(), pl)) continue;
		apply_skins_to(pl, kPortraitPeer, equipped_);
		apply_worn_locker_accessories(pl, kPortraitPeer);
		any = true;
	}
	if (!any) {
		clear_overrides(kPortraitPeer);
		applied_locker_.erase(kPortraitPeer);
	}
}

void JVTextureLoader::request_menu_portrait_refresh() {
	portrait_refresh_t_ = 0.2;
}

void JVTextureLoader::tick_menu_portrait_refresh(double delta) {
	if (portrait_refresh_t_ < 0.0) return;
	portrait_refresh_t_ -= delta;
	if (portrait_refresh_t_ > 0.0) return;
	portrait_refresh_t_ = -1.0;
	fire_menu_portrait_refresh();
}

void JVTextureLoader::fire_menu_portrait_refresh() {
	Ref<Script> sc = jvm_textures_io().resource("player.look", "res://game/character/character_portrait.gd");
	if (sc.is_valid()) {
		Variant cache = sc->get("_head_cache");
		if (cache.get_type() == Variant::DICTIONARY) Dictionary(cache).clear();
	}
	if (!get_tree()) return;
	Node *scene = get_tree()->get_current_scene();
	if (!scene) return;
	auto reload = [&](const char *avatar, const char *loader) {
		if (!scene->has_method(loader)) return;
		Variant av = jvm_textures_io().get("ui.menu", scene, StringName(avatar));
		if (Node *n = Object::cast_to<Node>(av))
			jvm_textures_io().call("ui.menu", scene, StringName(loader), n);
	};
	reload("_pc_avatar", "_load_card_pfp");
	reload("_profile_pfp", "_load_profile_pfp");
}

void JVTextureLoader::refresh_players() {
	JVM_PROF("textures.players");
	Node *scene = get_tree()->get_current_scene();
	if (!scene) return;
	std::map<int32_t, Node *> nodes;
	Variant pp = jvm_textures_io().get("scene.players", scene, StringName(String(sync_cfg_.get("players_property", "_peer_players"))));
	if (pp.get_type() == Variant::DICTIONARY) {
		Dictionary d = pp;
		Array k = d.keys();
		for (int i = 0; i < k.size(); i++) if (Node *n = Object::cast_to<Node>(d[k[i]])) nodes[int32_t(int64_t(k[i]))] = n;
	}
	int32_t me = my_peer();
	if (!nodes.count(me)) {
		Variant lp = jvm_textures_io().get("scene.players", scene, StringName(String(sync_cfg_.get("local_player_property", "player"))));
		if (Node *n = Object::cast_to<Node>(lp)) nodes[me] = n;
	}
	for (auto &[peer, node] : nodes) {
		if (peer == me) {
			apply_skins_to(node, peer, equipped_);
			apply_worn_locker_accessories(node, peer);
		} else {
			auto r = remote_.find(peer);
			apply_skins_to(node, peer, r == remote_.end() ? std::vector<sk::SkinId>() : r->second.ids);
			pose_game_accessory_roots(node);
		}
	}
	if (Variant pv = jvm_textures_io().get("scene.players", scene, "_player"); true) {
		if (Node *pl = Object::cast_to<Node>(pv)) {
			if (!nodes.count(me) || nodes[me] != pl) {
				apply_skins_to(pl, me, equipped_);
				apply_worn_locker_accessories(pl, me);
			}
		}
	}
	sync_outfit_bindings();
	refresh_display_avatars();
	std::vector<int32_t> gone;
	for (auto &[peer, ids] : applied_) if (peer >= 0 && !nodes.count(peer)) gone.push_back(peer);
	for (int32_t p : gone) clear_overrides(p);
	const int64_t spawned = int64_t(spawned_nodes_.size());
	if (spawned != last_status_spawned_) {
		last_status_spawned_ = spawned;
		invalidate_status_cache();
	}
}


String JVTextureLoader::player_name(int32_t peer) {
	Node *net = jvm_textures_io().node("net.state", get_tree()->get_root(), String(sync_cfg_.get("net", "/root/Net")));
	if (net) {
		Variant players = jvm_textures_io().get("net.state", net, "players");
		if (players.get_type() == Variant::DICTIONARY) {
			Dictionary d = players;
			if (d.has(peer) && d[peer].get_type() == Variant::DICTIONARY) {
				String n = Dictionary(d[peer]).get("name", "");
				if (!n.is_empty()) return n;
			}
		}
	}
	return "player " + String::num_int64(peer);
}

void JVTextureLoader::send_to(int32_t peer, const sk::Packet &p) {
	if (!net_on_ || !core_sync_open()) return;
	Ref<SceneMultiplayer> sm = get_tree()->get_multiplayer();
	if (sm.is_null()) return;
	std::vector<uint8_t> b = sk::encode(p);
	PackedByteArray pb;
	pb.resize(b.size());
	memcpy(pb.ptrw(), b.data(), b.size());
	sm->send_bytes(pb, peer, MultiplayerPeer::TRANSFER_MODE_RELIABLE, 0);
}

void JVTextureLoader::broadcast_equip_of(int32_t peer, int32_t except) {
	sk::Packet p;
	p.type = sk::Type::EquipOf;
	p.peer = peer;
	p.name = S(player_name(peer));
	if (peer == my_peer()) p.ids = equipped_;
	else if (remote_.count(peer)) p.ids = remote_[peer].ids;
	for (int32_t m : modded_peers_) if (m != except && m != peer) send_to(m, p);
}

void JVTextureLoader::_on_peer_packet(int64_t id, const PackedByteArray &packet) {
	if (!follow_.applied || !net_on_ || !core_sync_open()) return;
	std::vector<uint8_t> b(packet.ptr(), packet.ptr() + packet.size());
	if (auto o = jvm::content::decode(b)) { on_offer(int32_t(id), *o); return; }
	auto p = sk::decode(b);
	if (!p) return;
	Ref<SceneMultiplayer> sm = get_tree()->get_multiplayer();
	const bool host = sm.is_valid() && sm->is_server();
	const int32_t from = int32_t(id);
	if (host) {
		if (p->type == sk::Type::Hello) {
			bool fresh = modded_peers_.insert(from).second;
			send_to(from, { sk::Type::Welcome });
			if (fresh) jvm::log("textures", "sync: modded peer " + String::num_int64(from) + " joined");
			sk::Packet mine{ sk::Type::EquipOf, my_peer(), S(player_name(my_peer())), equipped_ };
			send_to(from, mine);
			for (auto &[peer, rp] : remote_) if (peer != from) send_to(from, { sk::Type::EquipOf, peer, rp.name, rp.ids });

			send_offer_to(from, my_peer(), build_offer());
			for (auto &[peer, items] : offers_) if (peer != from) send_offer_to(from, peer, items);
		} else if (p->type == sk::Type::Equip && modded_peers_.count(from)) {
			remote_[from] = { S(player_name(from)), p->ids };
			broadcast_equip_of(from, from);
			update_missing_ui();
		}
		return;
	}
	if (from != 1) return;
	switch (p->type) {
		case sk::Type::Welcome:
			if (!welcomed_) { jvm::log("textures", "sync: host is modded -> host relay"); offer_sent_ = false; }
			welcomed_ = true;
			channel_ = "host_relay";
			break;
		case sk::Type::EquipOf:
			if (p->peer != my_peer()) remote_[p->peer] = { p->name, p->ids };
			update_missing_ui();
			break;
		case sk::Type::Leave:
			remote_.erase(p->peer);
			offers_.erase(p->peer); offer_state_.erase(p->peer); push_remote_models();
			update_missing_ui();
			break;
		default: break;
	}
}

void JVTextureLoader::_on_peer_connected(int64_t) {}

void JVTextureLoader::_on_peer_disconnected(int64_t id) {
	int32_t p = int32_t(id);
	remote_.erase(p);
	offers_.erase(p); offer_state_.erase(p); push_remote_models();
	clear_overrides(p);
	Ref<SceneMultiplayer> sm = get_tree()->get_multiplayer();
	if (sm.is_valid() && sm->is_server() && modded_peers_.erase(p)) {
		for (int32_t m : modded_peers_) send_to(m, { sk::Type::Leave, p });
	}
	update_missing_ui();
}

void JVTextureLoader::net_stop() {
	if (net_on_) jvm::log("textures", "sync: network off");
	if (!published_member_data_.is_empty()) {
		Engine *e = Engine::get_singleton();
		Node *net = get_tree() ? jvm_textures_io().node("net.state", get_tree()->get_root(), String(sync_cfg_.get("net", "/root/Net"))) : nullptr;
		if (e->has_singleton("Steam") && net) {
			Variant lid = jvm_textures_io().get("net.state", net, "steam_lobby_id");
			if (lid.get_type() == Variant::INT && int64_t(lid) != 0) jvm_textures_io().call("net.member_data", jvm_textures_io().singleton("net.member_data", "Steam"), "setLobbyMemberData", lid, "jvm_skins", "");
		}
		published_member_data_ = "";
	}
	net_on_ = false;
	welcomed_ = false;
	hello_tries_ = 0;
	channel_ = "none";
	modded_peers_.clear();
	for (auto &[peer, rp] : remote_) clear_overrides(peer);
	remote_.clear();
	content_net_stop();
	update_missing_ui();
}

void JVTextureLoader::member_data_tick() {
	JVM_PROF("textures.member_data_tick");
	if (!net_on_ || !core_sync_open()) return;
	Engine *e = Engine::get_singleton();
	Node *net = jvm_textures_io().node("net.state", get_tree()->get_root(), String(sync_cfg_.get("net", "/root/Net")));
	if (!e->has_singleton("Steam") || !net) return;
	Object *steam = jvm_textures_io().singleton("net.member_data", "Steam");
	Variant lid = jvm_textures_io().get("net.state", net, "steam_lobby_id");
	if (lid.get_type() != Variant::INT || int64_t(lid) == 0) return;
	String csv;
	for (size_t i = 0; i < equipped_.size(); i++) csv += (i ? "," : "") + G(sk::format_id(equipped_[i]));
	if (csv != published_member_data_ && steam->has_method("setLobbyMemberData")) {
		jvm_textures_io().call("net.member_data", steam, "setLobbyMemberData", lid, "jvm_skins", csv);
		published_member_data_ = csv.is_empty() ? String("-") : csv;
	}
	if (channel_ != "lobby_member_data" || !steam->has_method("getNumLobbyMembers")) return;
	Variant players = jvm_textures_io().get("net.state", net, "players");
	Dictionary pd = players.get_type() == Variant::DICTIONARY ? Dictionary(players) : Dictionary();
	int64_t mine = steam->has_method("getSteamID") ? int64_t(jvm_textures_io().call("net.member_data", steam, "getSteamID")) : 0;
	int n = int(jvm_textures_io().call("net.member_data", steam, "getNumLobbyMembers", lid));
	std::map<int32_t, sk::RemotePlayer> fresh;
	for (int i = 0; i < n; i++) {
		int64_t sid = jvm_textures_io().call("net.member_data", steam, "getLobbyMemberByIndex", lid, i);
		if (sid == 0 || sid == mine) continue;
		String data = jvm_textures_io().call("net.member_data", steam, "getLobbyMemberData", lid, sid, "jvm_skins");
		if (data.is_empty() || data == "-") continue;
		sk::RemotePlayer rp;
		rp.name = S(String(jvm_textures_io().call("net.member_data", steam, "getFriendPersonaName", sid)));
		PackedStringArray parts = data.split(",");
		for (int k = 0; k < parts.size() && rp.ids.size() < sk::kMaxIds; k++) if (auto id = sk::parse_id(S(parts[k]))) rp.ids.push_back(*id);
		int32_t peer = -(i + 1);
		Array keys = pd.keys();
		for (int k = 0; k < keys.size(); k++)
			if (pd[keys[k]].get_type() == Variant::DICTIONARY && int64_t(Dictionary(pd[keys[k]]).get("sid", 0)) == sid) peer = int32_t(int64_t(keys[k]));
		fresh[peer] = rp;
	}
	remote_ = fresh;
	update_missing_ui();
}

void JVTextureLoader::net_tick(double delta) {
	Ref<SceneMultiplayer> sm = get_tree()->get_multiplayer();
	Ref<MultiplayerPeer> peer = sm.is_valid() ? sm->get_multiplayer_peer() : Ref<MultiplayerPeer>();
	bool connected = peer.is_valid() && peer->get_class() != "OfflineMultiplayerPeer" &&
			peer->get_connection_status() == MultiplayerPeer::CONNECTION_CONNECTED;
	if (!connected) { if (net_on_) net_stop(); local_only_reason_ = ""; return; }
	if (!core_sync_open()) {
		if (net_on_) { net_stop(); jvm::log("textures", "sync: local only (mode " + core_mode("?") + ")"); }
		local_only_reason_ = String("mode ") + core_mode("unknown") + " is local only";
		return;
	}
	local_only_reason_ = "";
	if (!signals_connected_) {
		jvm_textures_io().connect("net.packets", sm, "peer_packet", Callable(this, "_on_peer_packet"));
		jvm_textures_io().connect("net.packets", sm, "peer_connected", Callable(this, "_on_peer_connected"));
		jvm_textures_io().connect("net.packets", sm, "peer_disconnected", Callable(this, "_on_peer_disconnected"));
		signals_connected_ = true;
	}
	if (!net_on_) {
		net_on_ = true;
		channel_ = sm->is_server() ? "host_relay" : "pending";
		hello_t_ = 1e9;
		jvm::log("textures", String("sync: network on as ") + (sm->is_server() ? "host" : "client"));
	}
	if (!sm->is_server() && !welcomed_) {
		const double now_s = Time::get_singleton()->get_ticks_msec() / 1000.0;
		if (hello_t_ >= 1e8) hello_t_ = now_s - 2.0;
		const double since = now_s - hello_t_;
		if (hello_tries_ < 5 && since >= 2.0) {
			hello_t_ = now_s;
			hello_tries_++;
			send_to(1, { sk::Type::Hello });
			jvm::log("textures", "sync: HELLO " + String::num_int64(hello_tries_) + "/5");
			send_to(1, { sk::Type::Equip, 0, "", equipped_ });
		} else if (hello_tries_ >= 5 && since >= 2.0 && channel_ == "pending") {
			channel_ = "lobby_member_data";
			jvm::log("textures", "sync: host did not answer (unmodded?) -> Steam lobby member data fallback");
		}
	}
	if (welcomed_ && hello_tries_ > 0 && hello_t_ >= 0) { hello_tries_ = 0; }
	member_t_ += delta;
	if (member_t_ >= 1.0) { member_t_ = 0; member_data_tick(); }
}

void JVTextureLoader::_process(double delta) {
	ensure_court_watch();
	JVM_PROF("textures.process");
	{ JVM_PROF("textures.gate_sync"); follow_.sync(this); }
	if (!follow_.applied) return;
	{ JVM_PROF("textures.net_tick"); net_tick(delta); }
	{ JVM_PROF("textures.display_avatars"); refresh_display_avatars(); }
	tick_menu_portrait_refresh(delta);
	uint64_t sid = 0;
	if (SceneTree *tree = get_tree()) {
		if (Node *scene = tree->get_current_scene()) sid = scene->get_instance_id();
	}
	if (sid != last_seen_scene_id_) {
		last_seen_scene_id_ = sid;
		if (sid != 0) {


			JVM_PROF("textures.scene_change");
			apply_scene_cosmetics(true);
			refresh_players();
		}
	}
	rescan_t_ += delta;
	if (rescan_t_ >= 0.5) {
		JVM_PROF("textures.rescan");
		rescan_t_ = 0;
		{ JVM_PROF("textures.rescan.apply_scene_cosmetics"); apply_scene_cosmetics(false); }
		if (accessories_injected_) { JVM_PROF("textures.rescan.migrate_sync_character"); migrate_and_sync_character(); }
		{ JVM_PROF("textures.rescan.refresh_players"); refresh_players(); }
		{ JVM_PROF("textures.rescan.update_missing_ui"); update_missing_ui(); }
		{ JVM_PROF("textures.rescan.content_tick"); content_tick(); }
	}
	if (SceneTree *tree = get_tree()) {
		if (Node *sc = tree->get_current_scene()) {
			if (jvm_is_locker(sc)) {
				JVM_PROF("textures.locker_pose");
				Variant pv = jvm_textures_io().get("scene.players", sc, "_player");
				if (Node *lp = Object::cast_to<Node>(pv)) pose_game_accessory_roots(lp);
			}
		}
	}
}

void JVTextureLoader::update_missing_ui() {
	std::map<sk::SkinId, std::string> installed;
	for (auto &[id, s] : skins_) installed[id] = S(s.pack);
	std::vector<std::string> lines = follow_.applied ? sk::missing_lines(installed, remote_) : std::vector<std::string>();
	if (lines == shown_lines_) return;
	shown_lines_ = lines;
	if (!ui_ && !lines.empty()) {
		ui_ = memnew(CanvasLayer);
		ui_->set_layer(110);
		add_child(ui_);
		lines_box_ = memnew(VBoxContainer);
		lines_box_->set_anchors_preset(Control::PRESET_CENTER_BOTTOM);
		lines_box_->set_h_grow_direction(Control::GROW_DIRECTION_BOTH);
		lines_box_->set_v_grow_direction(Control::GROW_DIRECTION_BEGIN);
		lines_box_->set_offset(SIDE_BOTTOM, -10);
		lines_box_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		ui_->add_child(lines_box_);
	}
	if (!lines_box_) return;
	while (lines_box_->get_child_count() > 0) { Node *c = lines_box_->get_child(0); lines_box_->remove_child(c); c->queue_free(); }
	for (auto &l : lines) {
		Label *lab = memnew(Label);
		lab->set_text(G(l));
		lab->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		lab->add_theme_font_size_override("font_size", 14);
		lab->add_theme_color_override("font_color", Color(1, 1, 1, 0.8));
		lab->add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.8));
		lab->add_theme_constant_override("outline_size", 3);
		lab->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		lines_box_->add_child(lab);
	}
}

PackedStringArray JVTextureLoader::get_missing_lines() const {
	PackedStringArray a;
	for (auto &l : shown_lines_) a.push_back(G(l));
	return a;
}

void JVTextureLoader::invalidate_mip_stats() { mip_stats_dirty_ = true; }

void JVTextureLoader::invalidate_status_cache() {
	status_cache_dirty_ = true;
	if (is_inside_tree()) {
		if (Node *g = get_node_or_null(NodePath("/root/JVModGate"))) {
			if (g->has_method("mark_status_dirty")) g->call("mark_status_dirty");
		}
	}
}

Dictionary JVTextureLoader::get_status_brief() const { return follow_.status(); }

Dictionary JVTextureLoader::get_mip_stats() const {
	if (!mip_stats_dirty_ && !mip_stats_cache_.is_empty()) return mip_stats_cache_;
	Dictionary d;
	int with = 0, without = 0;
	int32_t min_levels = 0, max_levels = 0;
	int64_t bytes = 0;
	Array missing;
	for (auto &[path, tex] : tex_cache_) {
		if (tex.is_null()) continue;



		Ref<Image> img = tex->get_image();
		if (img.is_null() || img->is_empty()) {
			without++;
			missing.push_back(path);
			continue;
		}
		bytes += img->get_data_size();
		int32_t levels = img->has_mipmaps() ? img->get_mipmap_count() : 1;
		if (img->has_mipmaps()) {
			with++;
			if (min_levels == 0 || levels < min_levels) min_levels = levels;
			if (levels > max_levels) max_levels = levels;
		} else {
			without++;
			missing.push_back(path);
		}
	}
	d["textures"] = int64_t(with + without);
	d["with_mips"] = int64_t(with);
	d["without_mips"] = int64_t(without);
	d["min_levels"] = int64_t(min_levels);
	d["max_levels"] = int64_t(max_levels);
	d["bytes"] = bytes;
	d["missing"] = missing;
	d["s3tc_state"] = int64_t(s3tc_state);
	d["s3tc_warnings"] = int64_t(s3tc_warnings);
	mip_stats_cache_ = d;
	mip_stats_dirty_ = false;
	return d;
}

Dictionary JVTextureLoader::get_status() const {
	if (!status_cache_dirty_ && !status_cache_.is_empty()) return status_cache_;
	Dictionary s = follow_.status();
	s["packs"] = int64_t(skins_.size());
	s["equipped"] = int64_t(equipped_.size());
	s["global_swaps"] = int64_t(swaps_.size());
	s["overrides"] = int64_t(overrides_.size() + mat_overrides_.size());
	s["spawned"] = int64_t(spawned_nodes_.size());
	s["picked_stage"] = picked_stage_;
	s["picked_ball"] = picked_ball_;
	s["security_refused"] = jvm::safe::refused_count();
	s["security_last_refusal"] = jvm::safe::last_refusal();
	{
		int64_t stage_n = 0, ball_n = 0;
		for (auto id : load_order_) {
			auto it = skins_.find(id);
			if (it == skins_.end() || !it->second.picker) continue;
			if (it->second.type == "stage") stage_n++;
			else if (it->second.type == "ball") ball_n++;
		}
		s["stage_packs"] = stage_n;
		s["ball_packs"] = ball_n;
	}
	s["custom_ball"] = custom_visual_alive();
	s["cache"] = int64_t(tex_cache_.size());
	s["disk_loads"] = disk_loads_;
	{
		Dictionary ms = get_mip_stats();
		s["cache_with_mips"] = ms["with_mips"];
		s["cache_without_mips"] = ms["without_mips"];
		s["cache_mip_bytes"] = ms["bytes"];
	}
	s["accessories"] = get_accessory_status();
	Array w;
	for (auto &x : registry_warnings_) w.push_back(G(x));
	s["warnings"] = w;
	status_cache_ = s;
	status_cache_dirty_ = false;
	status_rebuilds_++;
	return s;
}

Dictionary JVTextureLoader::get_sync_status() const {
	Dictionary s;
	s["network"] = net_on_ ? "on" : "off";
	if (!net_on_ && !local_only_reason_.is_empty()) s["local_only"] = local_only_reason_;
	s["channel"] = channel_;
	Array eq;
	for (auto id : equipped_) eq.push_back(G(sk::format_id(id)));
	s["equipped"] = eq;
	Dictionary rem;
	for (auto &[peer, rp] : remote_) {
		Dictionary r; r["name"] = G(rp.name);
		Array ids; for (auto id : rp.ids) ids.push_back(G(sk::format_id(id)));
		r["ids"] = ids;
		rem[String::num_int64(peer)] = r;
	}
	s["players"] = rem;
	s["modded_peers"] = int64_t(modded_peers_.size());
	s["missing"] = get_missing_lines();
	return s;
}


String JVTextureLoader::infer_pack_type(const Dictionary &man) const {
	String t = String(man.get("type", "")).strip_edges().to_lower();
	if (!t.is_empty()) return t;
	String k = String(man.get("kind", "")).strip_edges().to_lower();
	if (k == "stage" || k == "ball" || k == "net") return k;
	if (man.has("court") && man["court"].get_type() == Variant::DICTIONARY) return "stage";
	if (man.has("sky") && man["sky"].get_type() == Variant::DICTIONARY) return "stage";
	if (man.has("environment") && man["environment"].get_type() == Variant::DICTIONARY) return "stage";
	if (man.has("ball") && man["ball"].get_type() == Variant::DICTIONARY) return "ball";
	if (man.has("net") && man["net"].get_type() == Variant::DICTIONARY) return "net";
	return "";
}

Dictionary JVTextureLoader::pack_dict(sk::SkinId id, const Skin &s) const {
	Dictionary d;
	d["id"] = G(sk::format_id(id));
	d["pack"] = s.pack;
	d["name"] = s.name;
	d["label"] = s.label.is_empty() ? s.name : s.label;
	d["type"] = s.type;
	d["preview_path"] = s.preview_path;
	d["base_stage"] = s.base_stage;
	d["variants"] = s.variants;
	d["wrap"] = s.ball.wrap;
	d["has_sky"] = s.sky.present;
	d["has_environment"] = s.env.present;
	d["has_audio"] = s.audio.present;
	if (s.audio.present) {
		Dictionary a;
		a["ambience"] = String(s.audio.ambience.c_str());
		a["bus"] = String(s.audio.bus.c_str());
		a["volume"] = s.audio.volume;
		d["audio"] = a;
	}
	d["hide_env_props"] = s.env.hide_env_props || s.sky.hide_env_props;
	d["hide_ground"] = s.env.hide_ground || s.sky.hide_ground;
	d["baked_lighting"] = s.env.baked_lighting;
	if (s.preview_tex.is_valid()) d["preview"] = s.preview_tex;
	return d;
}

Array JVTextureLoader::list_picker_packs(const String &type) const {
	Array out;
	String want = type.strip_edges().to_lower();
	for (auto id : load_order_) {
		auto it = skins_.find(id);
		if (it == skins_.end()) continue;
		if (it->second.type == want && it->second.picker) out.push_back(pack_dict(id, it->second));
	}
	return out;
}

Dictionary JVTextureLoader::pack_info(const String &id) const {
	auto pid = sk::parse_id(S(id));
	if (!pid) return Dictionary();
	auto it = skins_.find(*pid);
	if (it == skins_.end()) return Dictionary();
	return pack_dict(*pid, it->second);
}

void JVTextureLoader::load_picker_state() {

	String root = jvm::mods_dir(texture_pack_rel());
	Dictionary d;
	Variant pv = jvm::load_json(root.path_join("picker.json"));
	if (pv.get_type() == Variant::DICTIONARY) d = pv;
	else {
		Variant ev = jvm::load_json(root.path_join("equipped.json"));
		if (ev.get_type() == Variant::DICTIONARY) d = ev;
	}
	auto take = [&](const char *key) -> String {
		String v = String(d.get(key, "")).strip_edges();
		if (v.is_empty()) return String();
		auto id = sk::parse_id(S(v));
		if (!id || !skins_.count(*id) || skins_[*id].type != String(key)) return String();
		return G(sk::format_id(*id));
	};
	picked_stage_ = take("stage");
	picked_ball_ = take("ball");
}

void JVTextureLoader::save_picker_state() const {
	String path = jvm::mods_dir(texture_pack_rel()).path_join("picker.json");
	Dictionary d;
	d["stage"] = picked_stage_;
	d["ball"] = picked_ball_;
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null()) { jvm::warn("textures", "could not write " + path); return; }
	f->store_string(JSON::stringify(d, "\t"));
}







static void restage_game_to_base(Node *host, const String &base) {
	if (!host || base.is_empty() || !host->get_tree()) return;
	Node *m = host->get_tree()->get_current_scene();
	if (!m) return;
	if (m->has_method("_swap_stage")) {
		jvm_textures_io().call("court.env", m, "_swap_stage", base, true, true, false);
		jvm::log("textures", "restage game kit to base_stage=" + base);
	} else if (m->has_method("_swap_stage_live")) {
		jvm_textures_io().call("court.env", m, "_swap_stage_live", base, true, true, -1);
		jvm::log("textures", "restage game kit (live) to base_stage=" + base);
	} else {
		if (jvm_textures_io().get("court.env", m, "_freeplay_stage").get_type() != Variant::NIL)
			jvm_textures_io().set("court.env", m, "_freeplay_stage", base);
	}
	Array pick;
	pick.append(base);
	pick.append(0);
	if (m->has_method("set") || true) {
		Variant fp = jvm_textures_io().get("court.env", m, "_fp_pick");
		(void)fp;
		jvm_textures_io().set("court.env", m, "_fp_pick", pick);
	}
	if (Node *gc = jvm_textures_io().autoload("court.env", "GameConfig")) {
		jvm_textures_io().set("court.env", gc, "freeplay_stage", base);
		if (gc->has_method("save_settings")) jvm_textures_io().call("court.env", gc, "save_settings");
	}
}

bool JVTextureLoader::set_picked(const String &which, const String &id) {
	String want = id.strip_edges();
	String base_stage;
	if (!want.is_empty()) {
		auto pid = sk::parse_id(S(want));
		if (!pid || !skins_.count(*pid) || skins_[*pid].type != which) {
			jvm::warn("textures", "picker " + which + ": not a " + which + " pack: " + want);
			return false;
		}
		want = G(sk::format_id(*pid));
		if (which == "stage") base_stage = skins_[*pid].base_stage;
	}
	if (which == "stage") picked_stage_ = want;
	else picked_ball_ = want;
	save_picker_state();
	if (which == "stage" && !base_stage.is_empty())
		restage_game_to_base(this, base_stage);
	apply_picked_cosmetics();
	if (which == "stage" && !base_stage.is_empty())
		call_deferred("apply_picked_cosmetics");
	invalidate_status_cache();
	jvm::log("textures", "picker " + which + " = " + (want.is_empty() ? String("(game)") : want)
		+ (base_stage.is_empty() ? String() : String(" base=") + base_stage));
	return true;
}

void JVTextureLoader::rescan_picker_packs() {



	load_packs(false);
	apply_picked_cosmetics();
}

void JVTextureLoader::apply_picked_cosmetics() {
	apply_scene_cosmetics(true);
}

void JVTextureLoader::ensure_court_watch() {
	if (court_watch_on_ || !get_tree()) return;
	get_tree()->connect("node_added", Callable(this, "_on_scene_node_added"));
	court_watch_on_ = true;
}

void JVTextureLoader::drop_court_watch() {
	if (!court_watch_on_) return;
	if (SceneTree *tree = get_tree()) {
		if (tree->is_connected("node_added", Callable(this, "_on_scene_node_added")))
			tree->disconnect("node_added", Callable(this, "_on_scene_node_added"));
	}
	court_watch_on_ = false;
	env_apply_queued_ = false;
	pending_env_nodes_.clear();
}

void JVTextureLoader::_on_scene_node_added(Node *n) {
	if (!n || !follow_.applied) return;
	String nm = String(n->get_name());


	if (!nm.begins_with("JVM_")) {
		Node *par = n->get_parent();
		bool acc_mount = par && Object::cast_to<BoneAttachment3D>(par) && Object::cast_to<Skeleton3D>(par->get_parent());
		if (acc_mount || nm.begins_with("JvmModel") || n->has_meta("jvm_model")) {
			for (Node *p = par; p; p = p->get_parent()) {
				if (jvm_textures_io().get("player.look", p, "_surface_slots").get_type() == Variant::DICTIONARY) {
					pending_anchor_rigs_.push_back(p->get_instance_id());
					if (!anchor_apply_queued_) { anchor_apply_queued_ = true; call_deferred("_apply_pending_anchors"); }
					break;
				}
			}
		}
	}
	if (nm != "Court" && nm != "Grass") return;
	pending_env_nodes_.push_back(n->get_instance_id());
	if (env_apply_queued_) return;
	env_apply_queued_ = true;
	call_deferred("_apply_pending_env");
}

void JVTextureLoader::_apply_pending_anchors() {
	anchor_apply_queued_ = false;
	std::vector<uint64_t> rigs;
	rigs.swap(pending_anchor_rigs_);
	std::set<uint64_t> done;
	for (uint64_t id : rigs) {
		if (!done.insert(id).second) continue;
		Node *rig = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(id)));
		if (!rig || !rig->is_inside_tree()) continue;
		jvm_anchor::sync_rig(rig, anchor_overrides_for(jvm_model_id_worn_by(rig)));
	}
}

void JVTextureLoader::_apply_pending_env() {
	env_apply_queued_ = false;
	apply_env_for_new_nodes();
}

void JVTextureLoader::apply_env_for_new_nodes() {



	if (!follow_.applied || !get_tree()) {
		pending_env_nodes_.clear();
		return;
	}
	if (picked_stage_.is_empty()) {
		pending_env_nodes_.clear();
		return;
	}
	auto pid = sk::parse_id(S(picked_stage_));
	if (!pid || !skins_.count(*pid)) {
		pending_env_nodes_.clear();
		return;
	}
	const Skin &sk = skins_[*pid];
	if (!sk.env.present && !sk.court.present && !sk.sky.present) {
		pending_env_nodes_.clear();
		return;
	}
	Node *scene = get_tree()->get_current_scene();
	std::vector<Node *> fresh;
	std::set<uint64_t> seen;
	for (uint64_t id : pending_env_nodes_) {
		Node *n = Object::cast_to<Node>(ObjectDB::get_instance(id));
		if (!n || !n->is_inside_tree()) continue;
		if (!seen.insert(n->get_instance_id()).second) continue;
		fresh.push_back(n);
	}
	pending_env_nodes_.clear();
	if (fresh.empty()) return;
	for (Node *n : fresh) {
		Node *court = nullptr;
		Node *grass = nullptr;
		if (String(n->get_name()) == "Grass") {
			grass = n;
			if (Node *p = n->get_parent())
				court = p->get_node_or_null(NodePath("Court"));
		} else {
			court = n;
			if (Node *p = n->get_parent()) {
				grass = p->get_node_or_null(NodePath("Grass"));
				if (!grass && p->get_parent())
					grass = p->get_parent()->get_node_or_null(NodePath("Grass"));
			}
		}
		apply_picked_stage(scene, court, grass, sk);
	}
}

void JVTextureLoader::apply_picked_stage(Node *scene, Node *court, Node *grass, const Skin &sk) {
	apply_env_count_++;
	JVM_PROF("textures.stage_apply");
	{ JVM_PROF("textures.stage_apply.court"); apply_court_spec(scene, court, grass, sk.court); }
	{ JVM_PROF("textures.stage_apply.sky"); jvm::court_sky::apply_sky(scene, grass, sk.sky); }
	{ JVM_PROF("textures.stage_apply.env"); jvm::court_env::apply_env(scene, court, grass, sk.env); }
	{ JVM_PROF("textures.stage_apply.audio");
		if (sk.audio.present) jvm::native_audio::apply_pack_audio(sk.audio);
		else jvm::native_audio::clear_pack_audio();
	}
}


void JVTextureLoader::clear_custom_ball_visual() {
	for (auto it = visibility_backups_.begin(); it != visibility_backups_.end();) {
		if (it->owner_peer != 0) { ++it; continue; }
		if (Node3D *n = Object::cast_to<Node3D>(ObjectDB::get_instance(it->node_id)))
			n->set_visible(it->was_visible);
		it = visibility_backups_.erase(it);
	}
	for (auto it = spawned_nodes_.begin(); it != spawned_nodes_.end();) {
		if (it->owner_peer != 0) { ++it; continue; }
		if (Node *n = Object::cast_to<Node>(ObjectDB::get_instance(it->node_id))) {
			if (n->get_parent()) n->get_parent()->remove_child(n);
			n->queue_free();
		}
		it = spawned_nodes_.erase(it);
	}
	custom_visual_id_ = 0;
}

bool JVTextureLoader::custom_visual_alive() const {
	if (custom_visual_id_ == 0) return false;
	Node *n = Object::cast_to<Node>(ObjectDB::get_instance(custom_visual_id_));
	return n && n->is_inside_tree();
}

static Node *ball_mesh_node(Node *ball) {
	if (!ball) return nullptr;
	if (Node *m = Object::cast_to<Node>(jvm_textures_io().get("ball.model", ball, "mesh"))) return m;
	if (Node *m = jvm_textures_io().node("ball.model", ball, "Mesh")) return m;
	return nullptr;
}

static Node *ball_model_node(Node *ball) {
	if (!ball) return nullptr;
	if (Node *m = Object::cast_to<Node>(jvm_textures_io().get("ball.model", ball, "_model"))) return m;
	return nullptr;
}

static ShaderMaterial *tuned_ball_material(Node *ball) {
	Node *mesh_n = ball_mesh_node(ball);
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(mesh_n);
	if (!mi || mi->get_mesh().is_null()) return nullptr;
	Ref<Material> mat = mi->get_material_override();
	if (mat.is_null() && mi->get_mesh()->get_surface_count() > 0)
		mat = mi->get_active_material(0);
	if (mat.is_null()) {
		Ref<Mesh> mesh = mi->get_mesh();
		mat = mesh->surface_get_material(0);
		Ref<PrimitiveMesh> pm = mesh;
		if (mat.is_null() && pm.is_valid()) mat = pm->get_material();
	}
	return Object::cast_to<ShaderMaterial>(mat.ptr());
}

static float model_local_radius(Node *model, Node *skip) {
	if (!model) return 0.0f;
	Node3D *root = Object::cast_to<Node3D>(model);
	if (!root || !root->is_inside_tree()) return 0.0f;
	Transform3D inv = root->get_global_transform().affine_inverse();
	AABB box; bool first = true;
	TypedArray<Node> ms = model->find_children("*", "MeshInstance3D", true, false);
	for (int i = 0; i < ms.size(); i++) {
		MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(Object::cast_to<Node>(ms[i]));
		if (!mi || mi->get_mesh().is_null()) continue;
		if (skip && (mi == skip || mi->has_meta("jvm_custom_ball"))) continue;
		AABB b = (inv * mi->get_global_transform()).xform(mi->get_aabb());
		box = first ? b : box.merge(b); first = false;
	}
	if (first) return 0.0f;
	return box.get_longest_axis_size() * 0.5f;
}

static float phys_radius_local(Node *ball) {
	float pr = 0.26f;
	Variant pv = jvm_textures_io().get("ball.physics", ball, "PHYS");
	if (Object *po = Object::cast_to<Object>(pv)) {
		Variant r = jvm_textures_io().get("ball.physics", po, "radius");
		if (r.get_type() == Variant::FLOAT || r.get_type() == Variant::INT) pr = float(double(r));
	}
	Node3D *b3 = Object::cast_to<Node3D>(ball);
	float s = 1.0f;
	if (b3 && b3->is_inside_tree()) s = b3->get_global_transform().basis.get_scale().x;
	if (s < 1e-4f) s = 1.0f;
	return pr / s;
}

void JVTextureLoader::apply_picked_ball(Node *ball, const Skin &sk) {
	if (!ball || !sk.ball.present) return;
	String wrap = sk.ball.wrap;
	if (wrap.is_empty()) wrap = "equirect";



	if (wrap == "equirect" || wrap == "equirectangular") {

		if (Node *oldv = custom_visual_on(ball)) {
			if (oldv->get_parent()) oldv->get_parent()->remove_child(oldv);
			oldv->queue_free();
			for (auto it = spawned_nodes_.begin(); it != spawned_nodes_.end();) {
				if (it->node_id == oldv->get_instance_id()) it = spawned_nodes_.erase(it);
				else ++it;
			}
			if (custom_visual_id_ == oldv->get_instance_id()) custom_visual_id_ = 0;
		}
		Node *model = ball_model_node(ball);
		Node *mesh_n = ball_mesh_node(ball);
		Node *parent = (model && Object::cast_to<Node>(model)->is_inside_tree()) ? model : mesh_n;
		if (!parent) {
			jvm::warn("textures", "custom ball: no Mesh/_model to attach to");
			return;
		}



		if (model) {
			TypedArray<Node> ms = model->find_children("*", "MeshInstance3D", true, false);
			for (int i = 0; i < ms.size(); i++) {
				MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(Object::cast_to<Node>(ms[i]));
				if (!mi) continue;
				visibility_backups_.push_back({ mi->get_instance_id(), mi->is_visible(), 0 });
				mi->set_visible(false);
			}
		}

		float r = model_local_radius(model, nullptr);
		if (r < 0.02f) r = phys_radius_local(ball);
		if (r < 0.02f) r = 0.26f;

		ShaderMaterial *tuned = tuned_ball_material(ball);
		Color albedo = sk.ball.albedo_color.set ? sk.ball.albedo_color.color : Color(1, 1, 1, 1);
		Ref<ShaderMaterial> mat = jvm::custom_ball::make_material(sk.ball.albedo_tex, albedo, tuned);
		if (sk.ball.shade.set) mat->set_shader_parameter("shade", sk.ball.shade.value);

		MeshInstance3D *vis = memnew(MeshInstance3D);
		vis->set_name(jvm::custom_ball::kNodeName);
		vis->set_meta("jvm_custom_ball", true);
		vis->set_mesh(jvm::custom_ball::make_mesh(jvm::custom_ball::kDivs));
		vis->set_material_override(mat);
		vis->set_scale(Vector3(r, r, r));
		vis->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
		parent->add_child(vis);
		spawned_nodes_.push_back({ vis->get_instance_id(), 0 });
		custom_visual_id_ = vis->get_instance_id();


		Variant holo = jvm_textures_io().get("ball.skin", ball, "holo");
		if ((holo.get_type() == Variant::FLOAT || holo.get_type() == Variant::INT) && double(holo) >= 0.0
				&& ball->has_method("_apply_holo")) {
			jvm_textures_io().call("ball.skin", ball, "_apply_holo");
		}
		if (Node *scene = get_tree() ? get_tree()->get_current_scene() : nullptr) {
			(void)scene;
		}
		if (Node *gc = jvm_textures_io().autoload("ball.skin", "GameConfig")) {
			if (gc->has_method("world_light")) jvm_textures_io().call("ball.skin", gc, "world_light", ball);
		}
		jvm::log("textures", "custom ball " + sk.name + " r=" + String::num(r, 3) + " wrap=" + wrap + " on " + String(ball->get_path()));
		return;
	}


	jvm::warn("textures", "custom ball wrap not supported: " + wrap);
}


bool JVTextureLoader::audio_stop(const Variant &handle_or_id) {
	if (handle_or_id.get_type() == Variant::INT || handle_or_id.get_type() == Variant::FLOAT)
		return jvm::native_audio::stop_handle((int)handle_or_id);
	String id = String(handle_or_id);
	if (id.is_empty()) return false;
	return jvm::native_audio::stop_named(id) > 0;
}

int JVTextureLoader::audio_apply_pack(const String &stage_id) {
	auto pid = sk::parse_id(S(stage_id));
	if (!pid || !skins_.count(*pid)) {
		jvm::native_audio::clear_pack_audio();
		return 0;
	}
	const Skin &sk = skins_[*pid];
	if (!sk.audio.present) {
		jvm::native_audio::clear_pack_audio();
		return 0;
	}
	return jvm::native_audio::apply_pack_audio(sk.audio);
}
