#pragma once
#include "../common/mod_base.hpp"
#include "../common/api_client.hpp"
#include "../gate/model_rules.hpp"

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>



jvm::GameIO &jvm_models_io();

namespace godot {

bool jvm_is_locker(Object *o);
String content_cache_dir();
bool jvm_models_local_offer(String &id, String &dir);
Dictionary jvm_models_installed_dirs();
void jvm_models_rescan();
void jvm_models_set_remote(const Dictionary &peer_to_id);














class JVModelLoader : public Node {
	GDCLASS(JVModelLoader, Node)
	friend struct jvm::ApiFollower<JVModelLoader>;
	jvm::ApiFollower<JVModelLoader> follow_;

	struct Pack {
		String dir; String folder; jvm::models::ParseResult parsed; bool is_model = false;
		std::optional<jvm::models::Spec> manifest;
	};
	struct TintMat {
		Ref<Material> mat;
		bool shader = false;
		bool has_param = false;
		Color original;
		Color own;
		Variant param_orig;
		std::vector<Variant> light_orig;
		Color factor_orig;
		Ref<Texture2D> tex_orig;
		std::vector<std::pair<StringName, double>> light;
		bool has_flash = false;
	};
	struct Rig {
		ObjectID rig, scene;
		bool grafted = false, failed = false;
		String fail_reason;
		std::vector<ObjectID> grafts;
		std::set<uint64_t> graft_set;
		std::set<uint64_t> hidden;
		std::map<uint64_t, uint32_t> hidden_layers;
		std::map<uint64_t, bool> hidden_was_visible;
		std::set<uint64_t> hidden_hooked;
		std::vector<TintMat> mats;
		Color last_tint = Color(-1, -1, -1, -1);
		float last_flash = -1.0f;
		ObjectID hands[2];
		bool hands_hidden[2] = { false, false };
		double eligible_t = 1e9;
		bool eligible = false;
		bool reflection = false;
		double rest_error = 0.0;
		int frame_added = -1;

		ObjectID arm[2][5];
		int arm_anchor = -1;
		double arm_side[2] = { -1, 1 };
		double arm_gap[2] = { 0, 0 };
		bool arms_on = false;
		std::optional<jvm::skins::SkinId> remote;
		std::optional<jvm::skins::SkinId> remote_want;
	};



	struct Ctx {
		std::optional<jvm::skins::SkinId> sel;
		Ref<PackedScene> scene; Node *node = nullptr;
		Color own = Color(1, 1, 1, 1); bool have_own = false;
		String path, state;
		std::map<uint64_t, Ref<Texture2D>> grey; std::map<uint64_t, Color> avg;
	};
	struct CtxGuard {
		JVModelLoader *m; Ctx *c = nullptr;
		CtxGuard(JVModelLoader *loader, const Rig &r);
		~CtxGuard();
	};
	std::map<jvm::skins::SkinId, Ctx> remote_tmpl_;
	std::map<int32_t, jvm::skins::SkinId> remote_models_;
	void swap_ctx(Ctx &c);
	bool ensure_remote_template(jvm::skins::SkinId id);
	void free_remote_templates();
	std::optional<jvm::skins::SkinId> remote_for(Node *rig);

	std::map<jvm::skins::SkinId, Pack> packs_;
	std::vector<std::string> warnings_;
	std::optional<jvm::skins::SkinId> selected_;
	String model_state_ = "no model equipped";
	String model_path_;
	Ref<PackedScene> template_scene_;
	Node *template_node_ = nullptr;
	std::vector<Rig> rigs_;
	Ref<Script> rig_script_;
	Callable hook_;
	Callable chained_;
	String hook_state_ = "not installed";
	double hook_check_t_ = 0.0;
	bool scanned_ = false;
	bool legacy_layout_ = false;


	std::vector<ObjectID> lockers_;
	bool lockers_seeded_ = false;
	Node *tracked_locker();
	int64_t grafts_done_ = 0, fallbacks_ = 0;
	int64_t last_status_applied_ = -1;
	String last_status_equipped_;
	void notify_status_if_changed();
	String last_error_;
	std::set<std::string> logged_;

	std::optional<jvm::skins::SkinId> saved_;
	bool preview_on_ = false;
	std::optional<jvm::skins::SkinId> preview_id_;
	Dictionary settings_;
	bool settings_dirty_ = false;
	double settings_t_ = 0.0;
	std::map<uint64_t, Ref<Texture2D>> grey_cache_;
	std::map<uint64_t, Color> avg_cache_;
	Color template_own_ = Color(1, 1, 1, 1);
	bool have_template_own_ = false;
	bool rehide_guard_ = false;

	void apply();
	void revert();
	void load_packs();
	bool load_template();
	void free_template();
	void install_hook();
	void scan_tree(Node *n, int depth);
	void track(Node *rig, Node *scene);
	bool is_eligible(Node *rig);
	bool is_reflection_rig(Node *rig);
	bool graft(Rig &r);
	void ungraft(Rig &r, bool restore);
	void sweep(Rig &r);
	void hook_stock_visibility(Rig &r, Node *n, uint64_t id);
	void unhook_stock_visibility(Rig &r);
	void update_tint(Rig &r);
	void build_arms(Rig &r, Skeleton3D *base, uint32_t layers, bool have_layers);
	void update_arms(Rig &r);
	void update_hands(Rig &r, const Color &c, bool have_color);
	void paint(Rig &r, bool restore);
	Color tex_average(const Ref<Texture2D> &tex, bool &ok);
	Ref<Texture2D> grey_of(const Ref<Texture2D> &tex);
	void load_settings();
	void apply_settings(Pack &p);
	void save_settings_now();
	bool write_equipped_model(const String &id);
	bool is_locker_preview(Node *player);
	void fail(Rig &r, const String &why);
	void log_once(const String &key, const String &msg, bool warning);
	Node *rig_of(const Rig &r) const;
	const jvm::models::Spec *spec() const;

protected:
	static void _bind_methods();

public:
	JVModelLoader();
	~JVModelLoader();
	void _ready() override;
	void _process(double delta) override;
	void _on_frame_pre_draw();
	void _on_stock_visibility_changed(int64_t id);
	void _on_node_added(Node *n);
	void _exit_tree() override;
	void _on_gate_changed(bool, String) { follow_.sync(this); }
	void _on_access_changed(String, bool, String) { follow_.sync(this); }
	void _on_model_hook(Object *scene);
	void _service();
	bool reload_config();
	Dictionary get_status() const;
	Dictionary get_status_brief() const;
	int64_t get_graft_count() const;
	Array get_rig_report() const;

	Array get_model_list() const;
	String get_saved_model() const;
	String get_shown_model() const;
	bool is_previewing() const { return preview_on_; }
	bool preview_model(const String &id);
	bool apply_model(const String &id);
	void end_preview();
	Dictionary get_model_settings(const String &id) const;
	bool set_model_color(const String &id, const Color &c);
	bool reset_model_color(const String &id);
	bool set_model_option(const String &id, const String &key, const String &value);
	void flush_settings();

	bool offer_local(String &id, String &dir) const;
	Dictionary installed_dirs() const;
	void set_remote_models(const Dictionary &peer_to_id);
	Dictionary get_remote_models() const;
};

}
