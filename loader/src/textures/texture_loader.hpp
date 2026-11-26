#pragma once
#include "../common/mod_base.hpp"
#include "../common/jv_prof.hpp"
#include "../common/api_client.hpp"
#include "court_sky.hpp"
#include "court_env.hpp"
#include "../loader/native_audio.hpp"
#include "../gate/skin_sync.hpp"
#include "../gate/content_share.hpp"
#include "../common/accessory_rules.hpp"

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <deque>
#include <map>
#include <set>
#include <vector>



jvm::GameIO &jvm_textures_io();

namespace godot {

Node *jvm_find_model_rig(Node *player);
void jvm_convert_gltf_materials_to_toon(Node *root);
String jvm_model_id_worn_by(Node *player);

bool jvm_models_local_offer(String &id, String &dir);
Dictionary jvm_models_installed_dirs();
void jvm_models_rescan();
void jvm_models_set_remote(const Dictionary &peer_to_id);
String content_cache_dir();
String content_packs_dir();




















static constexpr const char *kPackDir = "resources/packs";
static constexpr const char *kOldPackDir = "resource packs";
godot::String texture_pack_rel();

class JVTextureLoader : public Node {
	GDCLASS(JVTextureLoader, Node)
	friend struct jvm::ApiFollower<JVTextureLoader>;
	jvm::ApiFollower<JVTextureLoader> follow_;

	struct ColorParam { bool set = false; Color color; };
	struct FloatParam { bool set = false; float value = 0.0f; };
	struct Vec2Param { bool set = false; Vector2 value; };

	struct OutfitSlotRule {
		Ref<Texture2D> tex;
		String tint_mode;
		ColorParam color;
	};
	struct OutfitSpec {
		bool present = false;
		String default_tint_mode = "replace";
		std::map<String, OutfitSlotRule> slots;
	};


	struct AccessoryPartFit {
		Vector3 offset;
		Vector3 rotation_deg;
		Vector3 scale = Vector3(1.0f, 1.0f, 1.0f);
	};



	struct AccessoryFit {
		bool present = false;
		bool has_offset = false, has_rotation = false, has_scale = false, has_mount = false;
		Vector3 offset;
		Vector3 rotation_deg;
		Vector3 scale = Vector3(1.0f, 1.0f, 1.0f);
		String mount;
		std::map<String, AccessoryPartFit> parts;
	};
	struct AccessorySpec {
		bool present = false;
		String model_path;
		String mount = "head";
		Vector3 offset;
		Vector3 rotation_deg;
		Vector3 scale = Vector3(1.0f, 1.0f, 1.0f);
		String space = "local";
		String shader_mode = "gltf";
		bool hide_vanilla_hats = false;
		bool jiggle = false;
		bool jiggle_explicit = false;
		float stiffness = 1.0f;
		float drag = 0.15f;
		float gravity = 0.4f;
		float radius = 0.12f;
		String tail_prefix = "tail_";
		std::map<String, AccessoryFit> fit;
	};

	struct NetSpec {
		bool present = false;
		ColorParam tape_color, cord_color, ink_color, post_color, pad_color, band_color;
		Vec2Param grid;
		FloatParam tape_top, tape_bottom, tape_side;
		Ref<Texture2D> tape_tex, net_tex, post_tex, pad_tex;
	};

	struct CourtSpec {
		bool present = false;
		Ref<Texture2D> floor_tex, grass_tex, hill_tex;
		ColorParam floor_color, checker_color, line_color;
		FloatParam checker_size;
		ColorParam grass_tint, grass_tip_color, grass_patch_color, grass_grad_top, grass_grad_bottom, hill_tint;
		FloatParam grass_brightness;
	};

	struct BallSpec {
		bool present = false;
		Ref<Texture2D> albedo_tex;
		ColorParam albedo_color, color_a, color_b;
		FloatParam shade;
		String wrap;
		Dictionary extras;
	};

	struct Swap { Ref<Texture2D> original; Ref<Image> backup; String res_path; };
	struct Skin {
		String pack;
		String name;
		String label;
		String type;
		String preview_path;
		String base_stage;
		PackedStringArray variants;
		Ref<Texture2D> preview_tex;
		bool global = false;
		bool picker = false;
		Dictionary map;
		OutfitSpec outfit;
		AccessorySpec hat;
		AccessorySpec face;
		AccessorySpec back;
		String category;
		NetSpec net;
		CourtSpec court;
		BallSpec ball;
		jvm::court_sky::SkySpec sky;
		jvm::court_env::EnvSpec env;
		jvm::native_audio::PackAudio audio;
	};
	struct Override { uint64_t mesh_id; int surface; Variant previous; int32_t owner_peer; };
	struct MatOverride { uint64_t mesh_id; Variant previous; int32_t owner_peer; };
	struct VisibilityBackup { uint64_t node_id; bool was_visible; int32_t owner_peer; };
	struct PropBackup { uint64_t obj_id; StringName prop; Variant previous; };
	struct ShaderParamBackup { uint64_t mat_id; StringName param; Variant previous; };
	struct SpawnedNode { uint64_t node_id; int32_t owner_peer; };
	struct OutfitBinding { uint64_t mesh_id; int surface; uint64_t orig_mat_id; uint64_t custom_mat_id; int32_t owner_peer; };

	std::vector<Swap> swaps_;
	Array added_;
	std::map<jvm::skins::SkinId, Skin> skins_;
	std::vector<jvm::skins::SkinId> load_order_;
	std::vector<std::string> registry_warnings_;
	std::vector<jvm::skins::SkinId> equipped_;
	std::vector<Override> overrides_;
	std::vector<MatOverride> mat_overrides_;
	std::vector<VisibilityBackup> visibility_backups_;
	std::vector<PropBackup> prop_backups_;
	std::vector<ShaderParamBackup> shader_param_backups_;
	std::vector<SpawnedNode> spawned_nodes_;
	std::vector<OutfitBinding> outfit_bindings_;
	std::map<int32_t, std::vector<jvm::skins::SkinId>> applied_;
	std::map<int32_t, uint64_t> applied_player_id_;
	std::map<int32_t, String> applied_model_id_;
	std::map<uint64_t, Ref<Shader>> shader_cache_;
	std::map<String, Ref<Texture2D>> tex_cache_;
	int64_t disk_loads_ = 0;



	mutable Dictionary mip_stats_cache_;
	mutable bool mip_stats_dirty_ = true;
	mutable Dictionary status_cache_;
	mutable bool status_cache_dirty_ = true;
	mutable int64_t status_rebuilds_ = 0;
	int64_t last_status_spawned_ = -1;
	void invalidate_mip_stats();
	void invalidate_status_cache();
	bool packs_loaded_ = false;
	bool cache_force_ = false;
	uint64_t last_seen_scene_id_ = 0;
	Dictionary sync_cfg_;


	uint64_t styled_scene_id_ = 0;
	uint64_t styled_court_id_ = 0;
	uint64_t styled_grass_id_ = 0;
	uint64_t styled_ball_id_ = 0;
	uint64_t styled_grass_mat_id_ = 0;
	int64_t styled_node_count_ = 0;
	String styled_picked_stage_;
	String styled_picked_ball_;
	int64_t apply_env_count_ = 0;
	int64_t cosmetics_force_count_ = 0;
	int64_t cosmetics_periodic_count_ = 0;
	std::set<uint64_t> styled_applied_ids_;
	bool court_watch_on_ = false;
	bool env_apply_queued_ = false;
	std::vector<uint64_t> pending_env_nodes_;


	bool net_on_ = false;
	String channel_ = "none";
	bool welcomed_ = false;
	int hello_tries_ = 0;
	godot::String local_only_reason_;
	double hello_t_ = 0.0, rescan_t_ = 0.0, member_t_ = 0.25;
	std::set<int32_t> modded_peers_;
	std::map<int32_t, jvm::skins::RemotePlayer> remote_;
	bool signals_connected_ = false;
	String published_member_data_;


	String picked_stage_;
	String picked_ball_;
	uint64_t custom_visual_id_ = 0;


	struct PackRef { String dir; bool cached = false; };
	std::map<jvm::skins::SkinId, PackRef> pack_refs_;
	std::vector<jvm::content::Item> sent_offer_;
	bool offer_sent_ = false;
	std::map<int32_t, std::vector<jvm::content::Item>> offers_;
	std::map<int32_t, std::map<uint16_t, String>> offer_state_;
	jvm::content::Catalog catalog_;
	int cat_state_ = 0;
	String cat_error_;
	double cat_retry_at_ = 0.0;
	Node *http_ = nullptr;
	String http_what_;
	std::deque<uint16_t> dl_queue_;
	std::map<uint16_t, String> dl_state_;
	int64_t dl_ok_ = 0, dl_rejected_ = 0, offers_sent_ = 0, offers_received_ = 0;
	String host_ball_, host_stage_;
	String shown_ball_, shown_stage_;
	String content_base() const;
	bool content_auto_download() const;
	std::vector<jvm::content::Item> build_offer();
	bool pack_item(const String &dir, jvm::content::Kind kind, uint16_t id, jvm::content::Item &out);
	std::map<uint16_t, jvm::content::Hash> installed_hashes();
	void content_tick();
	void content_net_stop();
	void send_offer_to(int32_t peer, int32_t about, const std::vector<jvm::content::Item> &items);
	void on_offer(int32_t from, const jvm::content::Offer &o);
	void consider_offer(int32_t peer);
	uint16_t file_dl_id_ = 0;
	size_t file_dl_index_ = 0;
	String file_dl_part_;
	bool file_dl_active_ = false;
	void start_http(const String &url, int64_t limit, const String &what);
	void _on_http_done(int64_t result, int64_t code, const PackedStringArray &headers, const PackedByteArray &body);
	void on_manifest(const PackedByteArray &body);
	void start_file_fetch(uint16_t id);
	String finish_file_fetch(uint16_t id, size_t index, const PackedByteArray &body);
	void abort_file_fetch(uint16_t id, const String &err);
	String install_pack(uint16_t id, const PackedByteArray &zip);
	void content_reload(bool models);
	void push_remote_models();
	String shown_pick(const String &which) const;


	CanvasLayer *ui_ = nullptr;
	VBoxContainer *lines_box_ = nullptr;
	std::vector<std::string> shown_lines_;



	struct AccRuntimeEntry { std::string id, category, part, pack; };
	struct AccSceneCache { String path; Ref<PackedScene> scene; };
	struct AppliedLocker { uint64_t player_id = 0; std::string model_id; std::vector<std::string> ids; };
	std::vector<AccRuntimeEntry> acc_entries_;
	std::map<std::string, AccSceneCache> acc_scenes_;
	std::map<int32_t, AppliedLocker> applied_locker_;
	std::vector<std::string> last_worn_acc_;
	std::vector<std::string> unplaced_acc_;
	int catalog_injected_ = 0;
	int catalog_total_ = 0;
	bool accessories_injected_ = false;
	bool accessories_migrated_ = false;
	double portrait_refresh_t_ = -1.0;
	static constexpr int32_t kPortraitPeer = -1001;

	void apply();
	void revert();
	void collect(const String &pack_dir, Dictionary &out);
	void load_packs(bool force_reload = false);
	Ref<Texture2D> load_pack_texture(const String &pack_dir, const String &rel_path);
	Ref<Texture2D> cache_or_load_texture(const String &full_path, bool force = false);
	void prune_texture_cache(const std::set<String> &keep);
	void parse_skin_extensions(const String &pack_dir, const Dictionary &man, Skin &s);
	void apply_global(const Skin &s);
	void apply_scene_cosmetics(bool force = false);
	void clear_scene_cosmetics();
	void apply_net_spec(Node *court, const NetSpec &net);
	void apply_court_spec(Node *scene, Node *court, Node *grass, const CourtSpec &court_spec);
	void apply_ball_spec(Node *ball, const BallSpec &ball_spec);
	void apply_picked_ball(Node *ball, const Skin &sk);
	void apply_picked_stage(Node *scene, Node *court, Node *grass, const Skin &sk);
	void clear_custom_ball_visual();
	bool custom_visual_alive() const;
	void net_tick(double delta);
	void net_stop();
	void send_to(int32_t peer, const jvm::skins::Packet &p);
	void broadcast_equip_of(int32_t peer, int32_t except);
	String player_name(int32_t peer);
	int32_t my_peer();
	void refresh_players();
	void refresh_display_avatars();
	void request_menu_portrait_refresh();
	void tick_menu_portrait_refresh(double delta);
	void fire_menu_portrait_refresh();
	void sync_outfit_bindings();
	bool player_overrides_intact(int32_t peer) const;
	void apply_skins_to(Node *player, int32_t peer, const std::vector<jvm::skins::SkinId> &ids);
	void apply_outfit_to(Node *player, int32_t peer, const OutfitSpec &outfit);
	void sync_locker_accessories();
	void rebuild_acc_entries();
	void migrate_and_sync_character();
	void save_equipped() const;
	void apply_worn_locker_accessories(Node *player, int32_t peer);
	bool inject_catalog(const Array &items);
	Ref<PackedScene> pack_accessory_scene(const AccessorySpec &spec, const String &id);
	Ref<Resource> make_accessory_item(const String &id, const String &name, const String &category, const AccessorySpec &spec);
	std::set<std::string> locker_accessory_ids() const;
	bool is_locker_accessory(jvm::skins::SkinId id) const;
	const Skin *skin_by_entry(const std::string &entry_id) const;
	const AccessorySpec *spec_for_entry(const AccRuntimeEntry &e) const;
	void apply_accessory_to(Node *player, int32_t peer, const AccessorySpec &acc, const String &tag, const String &model_id);
	void pose_game_accessory_roots(Node *player);
	Dictionary describe_accessory_anchors(Node *player);
	void _apply_pending_anchors();
	std::vector<uint64_t> pending_anchor_rigs_;
	bool anchor_apply_queued_ = false;
	static void setup_spring_physics(Node *acc_root, const AccessorySpec &acc);
	Ref<Shader> get_textured_toon_shader(const Ref<Shader> &orig);
	Ref<Shader> get_textured_net_shader(const Ref<Shader> &orig);
	Ref<Shader> get_textured_post_shader(const Ref<Shader> &orig);
	void clear_overrides(int32_t peer);
	void update_missing_ui();
	void member_data_tick();
	void load_picker_state();
	void save_picker_state() const;
	String infer_pack_type(const Dictionary &man) const;
	Dictionary pack_dict(jvm::skins::SkinId id, const Skin &s) const;
	bool set_picked(const String &which, const String &id);

protected:
	static void _bind_methods();
public:
	JVM_PROF_TAP_DECL("loader")
protected:

public:
	JVTextureLoader();
	void _process(double delta) override;

	bool reload_config();
	void _on_gate_changed(bool, String) { follow_.sync(this); }
	void _on_access_changed(String, bool, String) { follow_.sync(this); }
	void _exit_tree() override { drop_court_watch(); if (follow_.applied) { follow_.applied = false; revert(); } }
	void _on_peer_packet(int64_t id, const PackedByteArray &packet);
	void _on_peer_connected(int64_t id);
	void _on_peer_disconnected(int64_t id);
	int get_swapped_count() const { return int(swaps_.size()); }
	Dictionary get_status() const;
	Dictionary get_status_brief() const;
	int64_t get_status_rebuild_count() const { return status_rebuilds_; }
	Dictionary get_sky_status() const { return jvm::court_sky::sky_status(); }
	Dictionary get_env_status() const { return jvm::court_env::env_status(); }
	Dictionary get_audio_status() const { return jvm::native_audio::status_dict(); }
	Array audio_list() { return jvm::native_audio::list_native(); }
	PackedStringArray audio_buses() const { return jvm::native_audio::list_buses(); }
	Dictionary audio_info(const String &id) { return jvm::native_audio::info_dict(id); }
	int audio_play(const String &id, const Dictionary &opts) { return jvm::native_audio::play_one_shot(id, opts); }
	int audio_play_loop(const String &id, const Dictionary &opts) { return jvm::native_audio::play_loop(id, opts); }
	bool audio_stop(const Variant &handle_or_id);
	void audio_stop_all() { jvm::native_audio::stop_everything(); }
	bool audio_set_volume(int handle, float volume) { return jvm::native_audio::set_voice_volume(handle, volume); }
	bool audio_set_bus(int handle, const String &bus) { return jvm::native_audio::set_voice_bus(handle, bus); }
	int audio_apply_pack(const String &stage_id);
	Dictionary get_sync_status() const;
	Dictionary get_content_status() const;
	bool content_fetch(const String &id);
	Dictionary get_accessory_status() const;
	PackedStringArray get_missing_lines() const;
	int get_override_count() const { return int(overrides_.size() + mat_overrides_.size()); }
	int get_spawned_count() const { return int(spawned_nodes_.size()); }
	int get_cache_count() const { return int(tex_cache_.size()); }
	int get_disk_load_count() const { return int(disk_loads_); }
	int get_apply_env_count() const { return int(apply_env_count_); }
	int get_cosmetics_force_count() const { return int(cosmetics_force_count_); }
	int get_cosmetics_periodic_count() const { return int(cosmetics_periodic_count_); }
	void apply_worn_locker_accessories_for_test(Node *player) { apply_worn_locker_accessories(player, my_peer()); }
	void apply_scene_cosmetics_for_test(bool force) { apply_scene_cosmetics(force); }
	Dictionary get_cache_ids() const;
	Ref<Texture2D> get_cached_texture(const String &path) const;
	Dictionary get_mip_stats() const;

	Array list_picker_packs(const String &type) const;
	Dictionary pack_info(const String &id) const;
	String get_picked_stage() const { return picked_stage_; }
	String get_picked_ball() const { return picked_ball_; }
	bool set_picked_stage(const String &id) { return set_picked("stage", id); }
	bool set_picked_ball(const String &id) { return set_picked("ball", id); }
	void rescan_picker_packs();


	void apply_picked_cosmetics();
	void _on_scene_node_added(Node *n);
	void _apply_pending_env();
	void ensure_court_watch();
	void drop_court_watch();
	void apply_env_for_new_nodes();
};

}
