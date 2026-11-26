#pragma once
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include "../common/mod_base.hpp"
#include "../common/jv_prof.hpp"
#include "../common/api_client.hpp"
#include "../gate/shot_math.hpp"

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node.hpp>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace godot {




















class JVExtraShots : public Node {
	GDCLASS(JVExtraShots, Node)
	friend struct jvm::ApiFollower<JVExtraShots>;
	jvm::ApiFollower<JVExtraShots> follow_;
	std::vector<jvm::shots::ShotDef> shots_;
	Dictionary cfg_;
	PackedStringArray actions_;
	String backend_ = "jv_ball";

	int armed_ = -1;
	double armed_at_ = 0.0, arm_window_ = 2.0;
	double serving_seen_at_ = -100.0;
	std::map<uint64_t, double> serving_seen_;
	double serving_seen_for(Node *me) const;
	double scan_t_ = 1e9;
	std::set<uint64_t> hooked_;
	int applied_count_ = 0, skipped_count_ = 0;
	String last_event_;
	PackedStringArray held_actions_;
	double curve_amount_ = 12.0, last_curve_ = 0.0;
	jvm::shots::SpinParams spin_;
	Dictionary last_spin_;



	struct RangeBox { bool on = false; uint64_t ball = 0; Vector3 center, pos; double charge = 0, seen = 0, spread = 0; int64_t fseed = 0, seq = -1; bool placed = false; double last_fade = 1.0; String why; };
	RangeBox box_;
	void update_range_box(double delta);










	String curve_display_remote_ = "grey";
	String spin_display_ = "question";
	struct SpinDisplay {
		bool on = false;
		uint64_t ball = 0;
		int64_t seq = -1, fseed = 0;
		bool local = false, shimmer = false, shake = false;
		bool question_on = false, path_on = false, box_on = false;
		String style, mode, why;
		Vector3 exact, bias, center, question_pos, box_start, box_end;
		double seen = 0, last_fade = 1, charge = 0, box_half_w = 0;
		Array path_points;
	};
	SpinDisplay spin_disp_;
	MeshInstance3D *path_mi_ = nullptr;
	MeshInstance3D *question_mi_ = nullptr;
	Label3D *question_lbl_ = nullptr;
	Ref<StandardMaterial3D> path_mat_, question_mat_;

	MeshInstance3D *curve_box_mi_ = nullptr;
	MeshInstance3D *curve_box_shell_ = nullptr;
	MeshInstance3D *curve_arrow_mi_ = nullptr;
	Ref<StandardMaterial3D> curve_box_mat_, curve_box_shell_mat_;
	Ref<ShaderMaterial> curve_arrow_mat_;
	void update_spin_display(double delta);
	void clear_spin_display(const String &why = "");
	void ensure_display_nodes(Node *ball);
	void drop_curve_box_nodes();
	void hide_native_landing_marker(Node *ball);
	void hide_game_marker(Node *ball, bool hide);
	void restore_remote_box_marker(Node *ball);
	bool resolve_curve_range(Node *ball, const Vector3 &land, Vector3 &start, Vector3 &end) const;
	void drive_remote_uncertainty_box(Node *ball, const Vector3 &start, const Vector3 &end, double fade, const Color &col);
	Vector3 display_bias(int64_t seed) const;
	bool is_local_hitter(Node *ball);
	bool is_spin_flight(Node *ball) const;
	PackedVector3Array sample_ground_path(Node *ball, Vector3 *out_land) const;
	Ref<ArrayMesh> make_path_mesh(const PackedVector3Array &pts, const Vector3 &origin) const;
	Ref<ArrayMesh> make_question_mesh() const;
	Ref<ArrayMesh> make_curve_box_mesh(double half_w, double half_l, double inset) const;
	void place_float_arrow(Node *ball, MeshInstance3D *lp, const Vector3 &land, const Basis &basis, const Vector3 &origin, bool show);

	Ref<ArrayMesh> bar_mesh_, bar_shell_;
	uint64_t bar_ball_ = 0;
	bool bar_on_ = false;
	Array bar_points_;
	void build_bar(Node *ball, const jvm::shots::SpinPath &path, double s_land, double w, Vector3 center);
	void restore_marker();

	struct Ramp { bool on = false; uint64_t ball = 0; double A = 0, k = 0, T = 1, sgn = 1; int steps = 0; int64_t seq = -1; double seen = 0; Vector3 last_pos; String why; };
	Ramp ramp_;
	void update_ramp(double delta);
	Dictionary get_spin_bar() const;
	Dictionary last_float_;

	const jvm::shots::ShotDef *float_shot_ = nullptr;
	double float_t_ = 0.0;
	jvm::shots::Vec3 float_fwd_;
	void apply();
	void revert();
	void fire_rigid(const jvm::shots::ShotDef &s);
	void scan_balls();
	void hook_ball(Node *ball);
	Node *local_player();
	Node *local_player_for(Node *ball);
	double now() const;

	jvm::shots::TouchState build_touch(Node *me, double power, int64_t kind);
	int decide(Node *me, jvm::shots::TouchState &t, String &why);
	String court_mode();
	void shot_log(const String &line) { last_detail_ = line; jvm::log("shots", line); }
	String last_detail_;
	int64_t touches_ = 0;
	Dictionary last_topspin_;
	jvm::shots::FrameRaw frame_;
	Camera3D *cam_for(Node *me) const;
	jvm::shots::FrameRaw frame_of(Node *me, Vector2 raw) const;
	double float_lateral_ = 3.0;

	bool dbg_on_ = false, dbg_log_ = true;
	int dbg_n_ = 8;
	Key dbg_key_ = KEY_F7;
	Node *dbg_layer_ = nullptr;
	Node *dbg_label_ = nullptr;
	std::deque<String> dbg_events_;
	String dbg_logged_state_;
	double dbg_log_at_ = -100.0;
	void dbg_event(const String &line);
	void dbg_update();
	String dbg_text(String &state_line);
	Node *dbg_player();

protected:
	static void _bind_methods();
public:
	JVM_PROF_TAP_DECL("shots")
protected:

public:
	void _process(double) override;
	void _input(const Ref<InputEvent> &e) override;
	void _physics_process(double delta) override;

	bool reload_config() { if (!follow_.applied) return false; revert(); apply(); return true; }
	void _on_gate_changed(bool, String) { follow_.sync(this); }
	void _on_access_changed(String, bool, String) { follow_.sync(this); }
	JVExtraShots();
	void _on_struck(Vector3 pos, double power, int64_t kind, Object *ball);
	Dictionary get_status() const;
	void _exit_tree() override { if (follow_.applied) { follow_.applied = false; revert(); } }
	PackedStringArray get_shot_ids() const;

	String validate_config(const Dictionary &d) const;
	void apply_side_spin(Node *ball, Node *me, Camera3D *cam, double raw_x, Vector3 aim, int kind, double power, const jvm::shots::ShotDef &s);
	Dictionary get_last_spin() const { return last_spin_; }
	void apply_topspin_cursor(Node *ball, Node *me, godot::Camera3D *cam, double raw_x, int side, Vector3 aim, int kind, double power, const jvm::shots::ShotDef &s, double spike_boost = 1.0);
	double spike_boost_setting();
	void apply_rally_float(Node *ball, Node *me, Vector3 aim, int kind, double power, const jvm::shots::ShotDef &s, int side = 0);
	Dictionary get_range_box() const;
	Dictionary get_spin_display() const;
	Dictionary get_last_float() const { return last_float_; }
	String get_armed() const { return armed_ >= 0 && armed_ < int(shots_.size()) ? String(shots_[armed_].id.c_str()) : String(); }
	int get_applied_count() const { return applied_count_; }
	int get_skipped_count() const { return skipped_count_; }
	Dictionary get_last_topspin() const { return last_topspin_; }
	int64_t get_touch_count() const { return touches_; }
	String get_last_event() const { return last_event_; }
	void arm(const String &id);
	double get_last_curve() const { return last_curve_; }
	void set_debug_visible(bool on);
	bool is_debug_visible() const { return dbg_on_; }
	String get_debug_text();
	PackedStringArray get_debug_events() const { PackedStringArray a; for (auto &e : dbg_events_) a.push_back(e); return a; }
};

}
