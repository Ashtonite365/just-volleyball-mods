#include "model_tab.hpp"
#include "../common/jv_prof.hpp"
#include "model_loader.hpp"
#include "../common/jvm_common.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/container.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <set>

using namespace godot;

static constexpr const char *kTag = "models";
static constexpr const char *kBox = "JvmFullModel";
static constexpr const char *kLabel = "full model";

static const char *kPresets[] = { "#D80836", "#FF8A1F", "#FFD23F", "#5BD34A", "#2EC4E6", "#2F6BFF", "#8E5CF7", "#FF6FB5", "#F2F0EA", "#232B3B" };

static Object *obj(const Variant &v) { return v.get_type() == Variant::OBJECT ? v.operator Object *() : nullptr; }

void JVModelTab::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_tile"), &JVModelTab::_on_tile);
	ClassDB::bind_method(D_METHOD("_on_locker_exit"), &JVModelTab::_on_locker_exit);
	ClassDB::bind_method(D_METHOD("_rebuild"), &JVModelTab::_rebuild);
	ClassDB::bind_method(D_METHOD("_on_pick", "idx"), &JVModelTab::_on_pick);
	ClassDB::bind_method(D_METHOD("_on_apply"), &JVModelTab::_on_apply);
	ClassDB::bind_method(D_METHOD("_on_swatch", "idx"), &JVModelTab::_on_swatch);
	ClassDB::bind_method(D_METHOD("_on_color", "color"), &JVModelTab::_on_color);
	ClassDB::bind_method(D_METHOD("_on_reset"), &JVModelTab::_on_reset);
	ClassDB::bind_method(D_METHOD("_on_hands", "idx"), &JVModelTab::_on_hands);
	ClassDB::bind_method(D_METHOD("_on_target", "idx"), &JVModelTab::_on_target);
	ClassDB::bind_method(D_METHOD("get_info"), &JVModelTab::get_info);
}

JVModelLoader *JVModelTab::loader() const { return Object::cast_to<JVModelLoader>(get_parent()); }
Node *JVModelTab::locker() const { return locker_.is_valid() ? Object::cast_to<Node>(ObjectDB::get_instance(locker_)) : nullptr; }

void JVModelTab::_ready() {
	set_process_mode(PROCESS_MODE_ALWAYS);
	set_process(true);
}

Node *JVModelTab::find_locker() const {
	Node *scene = jvm::current_scene(const_cast<JVModelTab *>(this));
	if (!scene) return nullptr;
	if (jvm_is_locker(scene)) return scene;
	for (int i = 0; i < scene->get_child_count(); i++) if (jvm_is_locker(scene->get_child(i))) return scene->get_child(i);
	return nullptr;
}

void JVModelTab::skip(Node *l, const String &why) {
	if (skipped_ == uint64_t(l->get_instance_id())) return;
	skipped_ = uint64_t(l->get_instance_id());
	jvm::log(kTag, "locker \"full model\" tab not added: " + why);
}

void JVModelTab::_process(double) {
	JVM_PROF("models.tab_process");
	JVModelLoader *ml = loader();
	if (!ml) return;
	Node *l = find_locker();
	if (!l) {
		if (locker_.is_valid()) _on_locker_exit();
		return;
	}
	if (ObjectID(l->get_instance_id()) != locker_) {
		if (l->has_meta("jvm_full_model")) { locker_ = ObjectID(l->get_instance_id()); return; }
		if (!try_inject(l)) return;
	}


	fit_rail();
	if (String(jvm_models_io().get("ui.locker", l, "_section")) == kTab) {
		Node *content = Object::cast_to<Node>(obj(jvm_models_io().get("ui.locker", l, "_content")));
		if (content && !content->get_node_or_null(NodePath(kBox))) build(l);
	}
}

bool JVModelTab::try_inject(Node *l) {
	JVModelLoader *ml = loader();
	Dictionary st = ml->get_status();
	if (String(st.get("state", "")) != "active") { skip(l, "models library is off (" + String(st.get("reason", "?")) + ")"); return false; }
	if (bool(jvm_models_io().get("ui.locker", l, "section_mode"))) { skip(l, "split-screen locker (models are for your own player only)"); return false; }
	for (const char *m : { "_select_section", "_rebuild_content", "_paint_rail", "_group_header", "_labeled", "_choice_grid", "_swatch_grid", "_color_field" })
		if (!l->has_method(m)) { skip(l, String("locker has no ") + m + " (game changed?)"); return false; }
	Variant rb = jvm_models_io().get("ui.locker", l, "_rail_btns");
	if (rb.get_type() != Variant::DICTIONARY) { skip(l, "locker has no _rail_btns"); return false; }
	Dictionary rail = rb;
	Button *anchor = Object::cast_to<Button>(obj(rail.get("uniforms", Variant())));
	Node *col = anchor ? anchor->get_parent() : nullptr;
	if (!anchor || !col) { skip(l, "\"outfits\" rail tile not found"); return false; }
	if (!Object::cast_to<Container>(obj(jvm_models_io().get("ui.locker", l, "_content")))) { skip(l, "locker editor panel (_content) not found"); return false; }
	if (ui_style_.is_null()) {
		TypedArray<Dictionary> gl = ProjectSettings::get_singleton()->get_global_class_list();
		for (int i = 0; i < gl.size(); i++) { Dictionary d = gl[i]; if (String(d.get("class", "")) == "UiStyle") { ui_style_ = jvm_models_io().resource("ui.style", String(d.get("path", ""))); break; } }
	}
	if (ui_style_.is_null()) { skip(l, "UiStyle not found"); return false; }
	{
		std::set<String> have;
		TypedArray<Dictionary> ms = ui_style_->get_script_method_list();
		for (int i = 0; i < ms.size(); i++) have.insert(String(Dictionary(ms[i]).get("name", "")));
		for (const char *m : { "rail_tile", "icon", "style_button", "style_label", "slim_rail_tile" }) if (!have.count(m)) { skip(l, String("UiStyle.") + m + " missing"); return false; }
	}

	Variant icon = jvm_models_io().call("ui.style", ui_style_, "icon", "singleplayer", true);
	Button *tile = Object::cast_to<Button>(obj(jvm_models_io().call("ui.style", ui_style_, "rail_tile", kLabel, icon)));
	if (!tile) { skip(l, "UiStyle.rail_tile returned no button"); return false; }
	tile->set_name("JvmFullModelTile");
	tile->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	String rs = l->has_method("_rail_size") ? String(jvm_models_io().call("ui.locker", l, "_rail_size")) : String();
	if (!rs.is_empty()) jvm_models_io().call("ui.style", ui_style_, "slim_rail_tile", tile, rs);
	tile->set_tooltip_text("full model: swap your whole player for a model pack (only you see it)");
	tile->connect("pressed", Callable(this, "_on_tile"));
	col->add_child(tile);
	col->move_child(tile, anchor->get_index() + 1);

	Array keys = rail.keys();
	Array after;
	bool past = false;
	for (int i = 0; i < keys.size(); i++) { if (past) after.push_back(keys[i]); if (String(keys[i]) == "uniforms") past = true; }
	Array vals;
	for (int i = 0; i < after.size(); i++) { vals.push_back(rail[after[i]]); rail.erase(after[i]); }
	rail[kTab] = tile;
	for (int i = 0; i < after.size(); i++) rail[after[i]] = vals[i];
	jvm_models_io().call("ui.locker", l, "_paint_rail");
	l->set_meta("jvm_full_model", true);
	jvm_models_io().connect("ui.locker", l, "tree_exiting", Callable(this, "_on_locker_exit"));
	locker_ = ObjectID(l->get_instance_id());
	tile_ = ObjectID(tile->get_instance_id());
	slim_ = 0;
	jvm::log(kTag, "locker: \"full model\" tab added after \"outfits\"");
	return true;
}




void JVModelTab::fit_rail() {
	Button *tile = tile_.is_valid() ? Object::cast_to<Button>(ObjectDB::get_instance(tile_)) : nullptr;
	Control *col = tile ? Object::cast_to<Control>(tile->get_parent()) : nullptr;
	Control *rail = col ? Object::cast_to<Control>(col->get_parent()) : nullptr;
	if (!rail || slim_ >= 2 || ui_style_.is_null()) return;
	float avail = rail->get_size().y - col->get_offset(SIDE_TOP) + col->get_offset(SIDE_BOTTOM);
	float need = col->get_combined_minimum_size().y;
	if (avail <= 1.0f || need <= avail + 0.5f) return;
	slim_++;
	String size = slim_ == 1 ? "mid" : "short";
	col->add_theme_constant_override("separation", 4);
	for (int i = 0; i < col->get_child_count(); i++) if (Button *b = Object::cast_to<Button>(col->get_child(i))) jvm_models_io().call("ui.style", ui_style_, "slim_rail_tile", b, size);
	jvm::log(kTag, "locker: rail did not fit " + String::num_int64(col->get_child_count()) + " tiles (" + String::num(need, 0) + " > " + String::num(avail, 0) + " px) -> slim tiles (" + size + ")");
}

void JVModelTab::_on_tile() {
	Node *l = locker();
	if (!l) return;
	jvm_models_io().call("ui.locker", l, "_select_section", kTab);
	build(l);
}

void JVModelTab::_on_locker_exit() {
	locker_ = ObjectID();
	tile_ = ObjectID();



	if (JVModelLoader *ml = loader()) {
		if (ml->is_previewing() && ml->apply_model(ml->get_shown_model())) return;
		ml->end_preview();
	}
}

void JVModelTab::_rebuild() {
	rebuild_queued_ = false;
	Node *l = locker();
	if (!l || String(jvm_models_io().get("ui.locker", l, "_section")) != kTab) return;
	jvm_models_io().call("ui.locker", l, "_rebuild_content");
	build(l);
}

static Label *note(const Ref<Script> &ui, const String &text, int size = 14) {
	Label *n = memnew(Label);
	n->set_text(text);
	n->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	jvm_models_io().call("ui.style", ui, "style_label", n, "body");
	n->add_theme_font_size_override("font_size", size);
	return n;
}

void JVModelTab::build(Node *l) {
	JVModelLoader *ml = loader();
	Container *content = Object::cast_to<Container>(obj(jvm_models_io().get("ui.locker", l, "_content")));
	if (!ml || !content) return;
	if (Node *old = content->get_node_or_null(NodePath(kBox))) { content->remove_child(old); old->queue_free(); }
	VBoxContainer *box = memnew(VBoxContainer);
	box->set_name(kBox);
	box->add_theme_constant_override("separation", 9);
	box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content->add_child(box);
	auto add = [&](const Variant &v) { if (Node *n = Object::cast_to<Node>(obj(v))) box->add_child(n); };

	String shown = ml->get_shown_model(), saved = ml->get_saved_model();
	Array list = ml->get_model_list();
	add(jvm_models_io().call("ui.locker", l, "_group_header", "full model"));
	box->add_child(note(ui_style_, "swap your whole player for a model pack. your pick is saved straight away. only you see it - nothing is sent to other players."));

	ids_.clear();
	Array labels;
	ids_.push_back(""); labels.push_back("none (vanilla)");
	int sel = 0;
	String shown_name = "none (vanilla)", saved_name = "none (vanilla)";
	Dictionary shown_entry;
	for (int i = 0; i < list.size(); i++) {
		Dictionary d = list[i];
		String id = d.get("id", ""), name = d.get("name", id);
		ids_.push_back(id);
		labels.push_back(bool(d.get("usable", false)) ? name : name + " (broken)");
		if (id == shown) { sel = int(ids_.size()) - 1; shown_name = name; shown_entry = d; }
		if (id == saved) saved_name = name;
	}
	add(jvm_models_io().call("ui.locker", l, "_choice_grid", labels, sel, Callable(this, "_on_pick")));
	String state = "in use: " + saved_name;
	if (ml->is_previewing() && shown != saved) state += "\nshowing: " + shown_name + " (not saved - equipped.json could not be written)";
	box->add_child(note(ui_style_, state, 15));
	Button *use = memnew(Button);
	use->set_name("JvmUseModel");
	bool in_use = shown == saved;
	use->set_text(in_use ? "in use" : "save this model");
	use->set_disabled(in_use);
	jvm_models_io().call("ui.style", ui_style_, "style_button", use, in_use ? "secondary" : "primary", 16);
	use->connect("pressed", Callable(this, "_on_apply"));
	box->add_child(use);
	if (shown.is_empty()) {
		if (shown_entry.has("error")) box->add_child(note(ui_style_, String(shown_entry["error"]), 13));
		return;
	}
	Dictionary s = ml->get_model_settings(shown);
	if (s.is_empty()) { box->add_child(note(ui_style_, "this pack cannot be used: " + String(shown_entry.get("error", "invalid skin.json")), 13)); return; }

	add(jvm_models_io().call("ui.locker", l, "_group_header", "colour"));
	String def = s.get("default_color", ""), cur = s.get("color", "");
	bool custom = s.get("custom", false);
	swatches_.clear();
	swatch0_default_ = !def.is_empty();
	if (swatch0_default_) swatches_.push_back(Color::html(def));
	for (const char *p : kPresets) if (def.to_upper() != p) swatches_.push_back(Color::html(p));
	int ssel = -1;
	if (!custom && swatch0_default_) ssel = 0;
	else if (custom) for (int i = swatch0_default_ ? 1 : 0; i < swatches_.size(); i++) if (Color(swatches_[i]).to_html(false).to_upper() == cur.trim_prefix("#").to_upper()) { ssel = i; break; }
	Variant grid = jvm_models_io().call("ui.locker", l, "_swatch_grid", swatches_, ssel, Callable(this, "_on_swatch"));
	if (Node *g = Object::cast_to<Node>(obj(grid))) {
		g->set_name("JvmSwatches");
		for (int i = 0; i < g->get_child_count() && i < swatches_.size(); i++) {
			g->get_child(i)->set_meta("jvm_color", swatches_[i]);
			if (Control *c = Object::cast_to<Control>(g->get_child(i))) c->set_tooltip_text((i == 0 && swatch0_default_ ? String("model colour ") : String()) + "#" + Color(swatches_[i]).to_html(false).to_upper());
		}
	}
	add(grid);
	HBoxContainer *row = memnew(HBoxContainer);
	row->add_theme_constant_override("separation", 10);
	Variant field = jvm_models_io().call("ui.locker", l, "_color_field", cur.is_empty() ? Color(1, 1, 1) : Color::html(cur), Callable(this, "_on_color"));
	if (Node *lab = Object::cast_to<Node>(obj(jvm_models_io().call("ui.locker", l, "_labeled", "custom", field)))) row->add_child(lab);
	Button *reset = memnew(Button);
	reset->set_name("JvmResetColour");
	reset->set_text("reset to model default");
	reset->set_disabled(!custom);
	jvm_models_io().call("ui.style", ui_style_, "style_button", reset, "tertiary", 13);
	reset->connect("pressed", Callable(this, "_on_reset"));
	row->add_child(reset);
	box->add_child(row);

	add(jvm_models_io().call("ui.locker", l, "_group_header", "hands"));
	String h = s.get("game_hands", "keep");
	add(jvm_models_io().call("ui.locker", l, "_choice_grid", Array::make("keep", "hide", "model colour"), h == "hide" ? 1 : (h == "tint" ? 2 : 0), Callable(this, "_on_hands")));
	add(jvm_models_io().call("ui.locker", l, "_group_header", "shown on"));
	add(jvm_models_io().call("ui.locker", l, "_choice_grid", Array::make("just me", "everyone on my screen"), String(s.get("apply_to", "local")) == "all" ? 1 : 0, Callable(this, "_on_target")));
	box->add_child(note(ui_style_, String(s.get("id", "")) + "  -  " + String(s.get("model", "")) + (def.is_empty() ? String() : "  -  model colour " + def) +
			"\nteam colours are not used on models; the game's own model keeps them.", 13));
}

void JVModelTab::_on_pick(int idx) {
	JVModelLoader *ml = loader();
	if (!ml || idx < 0 || idx >= int(ids_.size())) return;

	if (!ml->apply_model(ids_[idx])) ml->preview_model(ids_[idx]);
	if (Node *l = locker(); l && l->has_method("_react_to_pick"))
		callable_mp_static(&JVModelTab::react_to_pick_now).call_deferred(int64_t(l->get_instance_id()));
	if (!rebuild_queued_) { rebuild_queued_ = true; call_deferred("_rebuild"); }
}

void JVModelTab::_on_apply() {
	JVModelLoader *ml = loader();
	if (!ml) return;
	ml->apply_model(ml->get_shown_model());
	if (!rebuild_queued_) { rebuild_queued_ = true; call_deferred("_rebuild"); }
}

void JVModelTab::_on_swatch(int idx) {
	JVModelLoader *ml = loader();
	if (!ml || idx < 0 || idx >= swatches_.size()) return;
	String id = ml->get_shown_model();
	if (idx == 0 && swatch0_default_) ml->reset_model_color(id);
	else ml->set_model_color(id, swatches_[idx]);
	if (!rebuild_queued_) { rebuild_queued_ = true; call_deferred("_rebuild"); }
}

void JVModelTab::_on_color(const Color &c) {
	if (JVModelLoader *ml = loader()) ml->set_model_color(ml->get_shown_model(), c);
}

void JVModelTab::_on_reset() {
	JVModelLoader *ml = loader();
	if (!ml) return;
	ml->reset_model_color(ml->get_shown_model());
	if (!rebuild_queued_) { rebuild_queued_ = true; call_deferred("_rebuild"); }
}

void JVModelTab::_on_hands(int idx) {
	if (JVModelLoader *ml = loader()) ml->set_model_option(ml->get_shown_model(), "game_hands", idx == 1 ? "hide" : (idx == 2 ? "tint" : "keep"));
}

void JVModelTab::_on_target(int idx) {
	if (JVModelLoader *ml = loader()) ml->set_model_option(ml->get_shown_model(), "apply_to", idx == 1 ? "all" : "local");
}

Dictionary JVModelTab::get_info() const {
	Dictionary d;
	Node *l = locker();
	d["locker"] = l != nullptr;
	d["section"] = l ? String(jvm_models_io().get("ui.locker", l, "_section")) : String();
	d["ids"] = Array();
	Array a;
	for (auto &i : ids_) a.push_back(i);
	d["ids"] = a;
	return d;
}

void JVModelTab::react_to_pick_now(int64_t locker_id) {
	if (Object *l = ObjectDB::get_instance(ObjectID(uint64_t(locker_id)))) jvm_models_io().call("ui.locker", l, "_react_to_pick");
}
