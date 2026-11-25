#pragma once
#include "../common/api_client.hpp"
#include "../common/jv_prof.hpp"
#include "../gate/touch_rules.hpp"
#include "../gate/goofy_layers.hpp"
#include "../gate/player_consts.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <set>
#include <vector>

namespace godot {






























class JVGoofyMods : public Node {
	GDCLASS(JVGoofyMods, Node)
	friend struct jvm::ApiFollower<JVGoofyMods>;
	jvm::ApiFollower<JVGoofyMods> follow_;
	Dictionary cfg_;
	bool time_changed_ = false;
	double orig_time_scale_ = 1.0;
	double rescan_ = 1e9;

	jvm::goofy::Stack stack_;
	Ref<Resource> phys_, feel_;
	Dictionary phys_orig_;
	Dictionary feel_orig_;
	Dictionary stock_;
	Dictionary pushed_;
	double ext_[5] = { 0, 1, 0, 0, 1 };
	bool physics_allowed_ = false, library_loaded_ = false;
	String ball_mode_ = "normal";
	struct BallSeen { uint64_t id; int64_t strike; };
	std::vector<BallSeen> balls_seen_;
	int ext_frames_ = 0;
	bool open_resources();
	Variant stock_of(const jvm::goofy::Param &p);
	jvm::goofy::ParseResult parse_values(const Dictionary &d, std::map<std::string, jvm::goofy::Value> &out) const;
	Dictionary result_dict(const jvm::goofy::ParseResult &r) const;
	void build_user_layer();
	void push_library();
	void restore_library();
	void physics_gate_tick();
	void ext_tick(double delta);
	void live_copy_tick(bool undo = false);
	Array schema_of(int which) const;
	Node *ball_at(int64_t index);
	Node *player_at(int64_t index);
	int64_t index_of(const Array &list, Object *o) const;

	String sync_role_ = "off";
	bool host_dirty_ = false, sync_hooked_ = false, reloading_ = false;
	uint64_t sync_mp_id_ = 0;
	int64_t sync_seq_ = 0, sync_sid_ = 0, recv_seq_ = -1, recv_sid_ = 0;
	double sync_hb_ = 0.0, sync_hello_ = 1e9;
	std::set<int64_t> sync_peers_;
	int64_t sync_sent_ = 0, sync_recv_ = 0, sync_rejected_ = 0, sync_applied_ = 0;
	String sync_last_error_;

	bool host_live_ = false;
	bool host_gone_ = false;
	bool host_in_match_ = false;
	double net_poll_ = 1e9;
	double cfg_ts_ = 1.0;
	bool ts_synced_ = false;
	double ts_host_ = 1.0, ts_client_orig_ = 1.0;
	Dictionary gameplay_snapshot() const;
	void apply_host_gameplay(bool on);
	void session_tick(double delta);
	void time_scale_tick();
	void set_sync_role(const String &role);
	void host_sync_tick(double delta);
	void sync_send(int64_t peer, const PackedByteArray &bytes);
	PackedByteArray host_payload();
	Object *sync_mp();

	jvm::pconsts::Lock pc_;
	Dictionary pc_verify_;
	bool pc_checked_ = false, pc_in_match_ = false;
	double pc_tick_t_ = 0.0;
	uint64_t pc_match_seq_ = 0;
	std::map<int64_t, String> pc_peer_ids_;
	String pc_notice_;
	void pc_tick();
	bool pc_available();
	String pc_manifest_id();
	void pc_write(const std::map<std::string, double> &vals, bool undo);
	void pc_match_start();
	void pc_match_end();
	void pc_on_plan(int64_t from, const PackedByteArray &packet);
	void pc_load_config();
	String pc_peer_name(int64_t peer);

	std::set<uint64_t> ev_hooked_;
	void hook_events();
	void emit_game_event(const String &kind, const String &name, uint64_t id, const Array &args);
	void load_ball_mode();
	bool save_ball_mode();

	struct Scaled { uint64_t id; Vector3 orig; bool ball; };
	std::vector<Scaled> scaled_;
	double big_ball_ = 1.0, player_scale_ = 1.0;

	int max_hits_ = 0;
	bool no_double_ = false;
	jvm::rules::Shadow shadow_;
	std::set<uint64_t> touch_hooked_;
	int rule_writes_ = 0, double_clears_ = 0;
	String published_rules_;




	bool gs_registered_ = false;
	double gs_try_ = 0.0;
	double gs_time_scale_ = 1.0;
	bool rule_foul_ = true;
	int rule_consec_ = 1;
	int rule_team_ = 3;
	jvm::rules::Streak streak_;
	uint64_t streak_rally_ = 0;
	int consec_clears_ = 0;
	bool match_off_ = false;
	double match_t_ = 0.0;
	void follow_sync();



	bool follow_dirty_ = true;
	bool panel_dirty_ = true;
	bool gate_dirty_ = true;
	bool hook_watch_ = false;
	double follow_poll_ = 0.0;
	double gate_poll_ = 0.0;
	bool last_panel_show_ = false;
	int last_panel_hits_ = -1;
	bool last_panel_nd_ = false;
	int override_count_ = 0;
	bool live_copy_on_ = false;
	void mark_work_dirty();
	void _on_node_added(Node *n);
	bool has_game_event_subscribers() const;
	bool scales_active() const;
	bool ext_active() const;
	bool pc_tick_needed() const;
	void refresh_physics_process();
	bool block_classic_set_ = false;
	Variant block_classic_orig_;
	void gs_register();
	void gs_apply();
	void gs_sync_legacy(const Dictionary &cfg);
	double gs_num(const char *id, double dflt);
	double gs_num_local(const char *id, double dflt);
	bool use_local_gs_layer() const;
	bool gs_bool(const char *id, bool dflt);
	void apply_time_scale(double ts);
	void apply_block_classic(bool on, bool restore = false);
	bool rules_stock() const { return rule_foul_ && rule_consec_ == 1 && rule_team_ == 3; }

	uint64_t panel_id_ = 0;
	Object *api() const { return jvm::find_api(const_cast<JVGoofyMods *>(this)); }
	void apply();
	void revert();
	void apply_ball_physics();
	void revert_ball_physics();
	void apply_scales();
	void revert_scales();
	void hook_players();
	Object *rally_for(Node *player);
	std::set<uint64_t> rallies_;
	bool enforcing();
	void rules_tick();
	void panel_tick();
	void sync_tick();

protected:
	static void _bind_methods();
public:
	JVM_PROF_TAP_DECL("goofy")
protected:

public:
	void _process(double delta) override;
	void _physics_process(double delta) override;

	bool reload_config() { if (!follow_.applied) return false; reloading_ = true; revert(); apply(); reloading_ = false; return true; }
	void _on_access_changed(String, bool, String) { follow_dirty_ = true; gate_dirty_ = true; panel_dirty_ = true; follow_sync(); }
	JVGoofyMods();
	void _ready() override;

	String api_version() const { return "1.2"; }
	bool is_enabled() const { return follow_.applied; }
	bool is_physics_allowed() const { return physics_allowed_; }
	Array get_schema() const;
	Array get_ball_schema() const;
	Array get_player_schema() const;

	Array list_balls();
	Dictionary get_ball_state(int64_t index);
	bool set_ball_velocity(int64_t index, const Vector3 &v);
	bool set_ball_curve(int64_t index, double curve);
	bool apply_ball_impulse(int64_t index, const Vector3 &dv);

	Array list_players();
	Dictionary get_player_state(int64_t index);
	Dictionary get_player_cosmetics(int64_t index);
	Dictionary set_player_override(const String &owner, const Dictionary &params, int priority = 500, const String &scope = "session");

	Dictionary player_consts_set(const String &name, double value, const String &source = "api");
	Dictionary player_consts_reset(const String &name, const String &source = "api");
	Dictionary player_consts_get();
	bool player_consts_locked() const { return pc_.locked(); }

	Dictionary get_sync_status() const;
	void _on_sync_packet(int64_t from, const PackedByteArray &packet);
	void _on_sync_peer_gone(int64_t id);
	void _on_ev0(const String &kind, const String &name, int64_t id) { emit_game_event(kind, name, uint64_t(id), Array()); }
	void _on_ev1(const Variant &a, const String &kind, const String &name, int64_t id) { Array x; x.push_back(a); emit_game_event(kind, name, uint64_t(id), x); }
	void _on_ev2(const Variant &a, const Variant &b, const String &kind, const String &name, int64_t id) { Array x; x.push_back(a); x.push_back(b); emit_game_event(kind, name, uint64_t(id), x); }
	void _on_ev3(const Variant &a, const Variant &b, const Variant &c, const String &kind, const String &name, int64_t id) { Array x; x.push_back(a); x.push_back(b); x.push_back(c); emit_game_event(kind, name, uint64_t(id), x); }
	Variant get_param(const String &id);
	Dictionary get_params();
	Variant get_stock(const String &id);
	Array get_layers() const;
	bool get_force_defaults() const { return stack_.force_defaults(); }
	String get_ball_mode() const { return ball_mode_; }
	String get_loader_map() const { return String(stack_.loader_map().c_str()); }
	Dictionary apply_pack_params(const String &pack_id, const Dictionary &params, int version = 1);
	bool clear_pack_params(const String &pack_id);
	void set_loader_map(const String &pack_id);
	Dictionary set_override(const String &owner, const Dictionary &params, int priority = 500, const String &scope = "session");
	bool clear_override(const String &owner);
	bool set_ball_mode(const String &mode);
	void _on_player_touched(int64_t kind, Object *who);
	void _on_gs_changed(const String &id, const Variant &value, const String &source);
	Dictionary _gs_migrate(int64_t from, const Dictionary &values);
	Dictionary get_rules() const;
	void _on_panel_changed(String key, Variant value);
	Dictionary get_status() const;
	String validate_config(const Dictionary &d) const;
	void _exit_tree() override { if (follow_.applied) { follow_.applied = false; revert(); } }
	void set_max_hits(int v);
	int get_max_hits() const { return max_hits_; }
	void set_no_double_hit(bool v);
	bool get_no_double_hit() const { return no_double_; }
	static String rules_text(int max_hits, bool no_double);
	static bool parse_rules_text(const String &t, int &max_hits, bool &no_double);
};

}
