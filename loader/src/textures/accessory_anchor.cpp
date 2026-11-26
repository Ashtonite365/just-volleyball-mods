
#include "accessory_anchor.hpp"
#include "texture_loader.hpp"

#include <godot_cpp/classes/bone_attachment3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>
#include <unordered_map>
#include <godot_cpp/variant/array.hpp>

#include <vector>

using namespace godot;

namespace jvm_anchor {

static const char *const kSeatMeta = "jvm_anchor_seat";
static const char *const kKind = "jvm_anchor_kind";
static const char *const kBone = "jvm_anchor_bone";
static const char *const kSlot = "jvm_anchor_slot";
static const char *const kAccId = "jvm_anchor_acc";
static const char *const kDup = "jvm_anchor_dup";
static const char *const kAuthored = "jvm_anchor_authored";
static const char *const kHat = "jvm_anchor_hat";
static const char *const kSpawned = "jvm_anchor_spawned";
static const char *const kSig = "jvm_anchor_sig";
static const char *const kAbs = "jvm_anchor_absolute";
static const char *const kPlace = "jvm_anchor_place";
static const char *kHeadName = "JVM_Anchor_head";
static const char *kBackName = "JVM_Anchor_back";

const Tuning &tuning() {
	static Tuning t;
	return t;
}

Kind kind_of_mount(const String &mount) {
	String m = mount.to_lower();
	if (m == "chest" || m == "back" || m == "hips" || m == "hip" || m == "extra" || m == "tail" || m == "spine")
		return kBack;
	return kHead;
}

static Override parse_one(const Variant &v) {
	Override o;
	if (v.get_type() != Variant::DICTIONARY) return o;
	Dictionary d = v;
	if (d.has("bone")) o.bone = String(d["bone"]);
	if (d.has("offset") && d["offset"].get_type() == Variant::ARRAY) {
		Array a = d["offset"];
		if (a.size() >= 3) {
			o.offset = Vector3(float(a[0]), float(a[1]), float(a[2]));
			o.has_offset = true;
		}
	}
	if (d.has("scale")) {
		float s = float(d["scale"]);
		if (s > 0.05f && s < 10.0f) { o.scale = s; o.has_scale = true; }
	}
	return o;
}

Overrides parse_overrides(const Variant &anchors) {
	Overrides ov;
	if (anchors.get_type() != Variant::DICTIONARY) return ov;
	Dictionary d = anchors;
	ov.head = parse_one(d.get("head", Variant()));
	ov.back = parse_one(d.get("back", Variant()));
	ov.key = Variant(d).stringify();
	return ov;
}



static Skeleton3D *rig_skeleton(Node *rig) {
	if (!rig) return nullptr;
	Variant v = jvm_textures_io().get("player.outfit", rig, "_skel");
	if (v.get_type() == Variant::OBJECT) {
		if (Skeleton3D *sk = Object::cast_to<Skeleton3D>(v.operator Object *())) return sk;
	}

	std::vector<Node *> st{ rig };
	while (!st.empty()) {
		Node *n = st.back(); st.pop_back();
		if (Skeleton3D *sk = Object::cast_to<Skeleton3D>(n)) return sk;
		String nm = String(n->get_name());
		if (nm.begins_with("JVM_") || nm.begins_with("JvmAcc")) continue;
		for (int i = n->get_child_count() - 1; i >= 0; i--) st.push_back(n->get_child(i));
	}
	return nullptr;
}


static Transform3D rel_to(Node *n, Node *root) {
	Transform3D t;
	for (Node *p = n; p && p != root; p = p->get_parent()) {
		if (Node3D *p3 = Object::cast_to<Node3D>(p)) t = p3->get_transform() * t;
	}
	return t;
}

static int32_t find_any(Skeleton3D *sk, std::initializer_list<const char *> names) {
	for (const char *n : names) {
		int32_t b = sk->find_bone(n);
		if (b >= 0) return b;
	}
	return -1;
}

static int32_t head_bone(Skeleton3D *sk, const Override &o, Node *rig) {
	if (!o.bone.is_empty()) {
		int32_t b = sk->find_bone(o.bone);
		if (b >= 0) return b;
	}
	int32_t b = find_any(sk, { "head", "Head", "mixamorig:Head", "J_Bip_C_Head", "head.x", "Head_M" });
	if (b >= 0) return b;
	for (int32_t i = 0; i < sk->get_bone_count(); i++) {
		String nm = sk->get_bone_name(i).to_lower();
		if (nm.contains("head") && !nm.contains("end") && !nm.contains("top") && !nm.contains("tail")) return i;
	}
	if (rig) {
		Variant hm = jvm_textures_io().get("player.outfit", rig, "_head_mount");
		if (hm.get_type() == Variant::OBJECT) {
			if (BoneAttachment3D *ba = Object::cast_to<BoneAttachment3D>(hm.operator Object *())) {
				if (ba->get_bone_idx() >= 0) return ba->get_bone_idx();
			}
		}
	}

	float top = -1e9f, maxy = 0.01f;
	for (int32_t i = 0; i < sk->get_bone_count(); i++) maxy = Math::max(maxy, sk->get_bone_global_rest(i).origin.y);
	b = -1;
	for (int32_t i = 0; i < sk->get_bone_count(); i++) {
		Vector3 o3 = sk->get_bone_global_rest(i).origin;
		if (Math::abs(o3.x) > 0.25f * maxy) continue;
		if (o3.y > top) { top = o3.y; b = i; }
	}
	return b;
}

static int32_t back_bone(Skeleton3D *sk, const Override &o, int32_t head) {
	if (!o.bone.is_empty()) {
		int32_t b = sk->find_bone(o.bone);
		if (b >= 0) return b;
	}

	int32_t b = find_any(sk, { "spine_1", "spine.001", "spine_001", "spine1", "Spine_1", "chest", "Chest",
			"UpperChest", "mixamorig:Spine1", "mixamorig:Spine2", "J_Bip_C_Chest", "spine", "Spine" });
	if (b >= 0) return b;
	for (int32_t i = 0; i < sk->get_bone_count(); i++) {
		String nm = sk->get_bone_name(i).to_lower();
		if (nm.contains("chest") || nm.contains("spine")) return i;
	}

	float hy = head >= 0 ? sk->get_bone_global_rest(head).origin.y : 1.0f;
	float best = 1e9f;
	b = head;
	for (int32_t i = 0; i < sk->get_bone_count(); i++) {
		Vector3 o3 = sk->get_bone_global_rest(i).origin;
		if (Math::abs(o3.x) > 0.25f * Math::abs(hy)) continue;
		float d = Math::abs(o3.y - 0.6f * hy);
		if (d < best) { best = d; b = i; }
	}
	return b;
}



struct Body {
	bool has_model = false, has_stock = false, has_stock_head = false, has_stock_torso = false;
	AABB model, stock, stock_head, stock_torso;
	float m2u = 1.0f;
	float fwd = 1.0f;
	String sig;
};

static void merge(AABB &box, bool &has, const AABB &a) {
	if (!has) { box = a; has = true; }
	else box = box.merge(a);
}

static bool is_model_mesh(Node *n) {
	return n->has_meta("jvm_model") || String(n->get_name()).begins_with("JvmModel");
}





static AABB rest_bounds(MeshInstance3D *mi, Skeleton3D *sk) {
	Ref<Mesh> mesh = mi->get_mesh();
	Ref<Skin> skin = mi->get_skin();
	if (mesh.is_null() || skin.is_null() || skin->get_bind_count() == 0) return rel_to(mi, sk).xform(mi->get_aabb());
	static std::unordered_map<uint64_t, AABB> cache;
	uint64_t key = uint64_t(mi->get_instance_id()) * 1000003ULL ^ uint64_t(mesh->get_instance_id()) * 7919ULL ^ uint64_t(skin->get_instance_id());
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	const int32_t nb = skin->get_bind_count();
	std::vector<Transform3D> bx(nb);
	for (int32_t i = 0; i < nb; i++) {
		int32_t bone = skin->get_bind_bone(i);
		if (bone < 0) bone = sk->find_bone(skin->get_bind_name(i));
		Transform3D r = (bone >= 0 && bone < sk->get_bone_count()) ? sk->get_bone_global_rest(bone) : Transform3D();
		bx[i] = r * skin->get_bind_pose(i);
	}
	std::vector<float> xs, ys, zs;
	for (int32_t s = 0; s < mesh->get_surface_count(); s++) {
		Array arr = mesh->surface_get_arrays(s);
		if (arr.size() <= Mesh::ARRAY_WEIGHTS) continue;
		PackedVector3Array vtx = arr[Mesh::ARRAY_VERTEX];
		PackedInt32Array bones = arr[Mesh::ARRAY_BONES];
		PackedFloat32Array wts = arr[Mesh::ARRAY_WEIGHTS];
		const int64_t n = vtx.size();
		if (n == 0 || bones.size() < n || wts.size() != bones.size()) continue;
		const int64_t per = bones.size() / n;
		const int64_t step = n > 20000 ? n / 20000 : 1;
		for (int64_t v = 0; v < n; v += step) {
			Vector3 acc;
			float wsum = 0.0f;
			for (int64_t k = 0; k < per; k++) {
				float w = wts[v * per + k];
				int32_t bi = bones[v * per + k];
				if (w <= 0.0f || bi < 0 || bi >= nb) continue;
				acc += bx[bi].xform(vtx[v]) * w;
				wsum += w;
			}
			if (wsum <= 1e-6f) continue;
			Vector3 pnt = acc / wsum;
			xs.push_back(pnt.x); ys.push_back(pnt.y); zs.push_back(pnt.z);
		}
	}
	AABB box;
	if (xs.size() < 8) {
		box = rel_to(mi, sk).xform(mi->get_aabb());
	} else {


		auto q = [](std::vector<float> &v, float f) {
			size_t k = size_t(f * float(v.size() - 1));
			std::nth_element(v.begin(), v.begin() + k, v.end());
			return v[k];
		};
		Vector3 lo(q(xs, 0.02f), q(ys, 0.005f), q(zs, 0.02f));
		Vector3 hi(q(xs, 0.98f), q(ys, 0.995f), q(zs, 0.98f));
		box = AABB(lo, hi - lo);
	}
	if (cache.size() > 512) cache.clear();
	cache[key] = box;
	return box;
}

static Body measure(Skeleton3D *sk, Node *rig) {
	Body b;

	Transform3D rel = rel_to(sk, rig);
	float s = rel.basis.get_scale().y;
	if (s > 1e-4f) b.m2u = 1.0f / s;
	int32_t foot = find_any(sk, { "foot.l", "foot.r", "foot_l", "foot_r", "LeftFoot", "mixamorig:LeftFoot" });
	int32_t toe = find_any(sk, { "toe.l", "toe.r", "toe_l", "toe_r", "LeftToeBase", "mixamorig:LeftToeBase" });
	if (foot >= 0 && toe >= 0) {
		float dz = sk->get_bone_global_rest(toe).origin.z - sk->get_bone_global_rest(foot).origin.z;
		if (Math::abs(dz) > 1e-3f) b.fwd = dz > 0 ? 1.0f : -1.0f;
	}
	b.sig = String::num_int64(sk->get_bone_count());
	AABB unskinned;
	bool has_unskinned = false;
	std::vector<Node *> st;
	for (int i = 0; i < sk->get_child_count(); i++) st.push_back(sk->get_child(i));
	while (!st.empty()) {
		Node *n = st.back(); st.pop_back();
		String nm = String(n->get_name());
		if (nm.begins_with("JVM_") || nm.begins_with("JvmAcc") || n->has_meta("jvm_accessory")) continue;
		if (Object::cast_to<BoneAttachment3D>(n)) continue;
		if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
			Ref<Mesh> m = mi->get_mesh();
			if (mi->is_set_as_top_level()) continue;
			if (m.is_valid()) {
				AABB a = rest_bounds(mi, sk);
				if (is_model_mesh(mi)) {




					if (mi->get_skin().is_valid()) {
						merge(b.model, b.has_model, a);
						b.sig += "|" + String::num_int64(int64_t(mi->get_instance_id()));
					} else {
						merge(unskinned, has_unskinned, a);
					}
				} else if (!nm.to_lower().contains("outline")) {
					String l = nm.to_lower();
					merge(b.stock, b.has_stock, a);
					if (l == "headmesh" || (l.contains("head") && !l.contains("band"))) merge(b.stock_head, b.has_stock_head, a);
					if (l == "bodymesh" || l.contains("jersey") || l == "body") merge(b.stock_torso, b.has_stock_torso, a);
				}
			}
		}
		for (int i = 0; i < n->get_child_count(); i++) st.push_back(n->get_child(i));
	}
	if (!b.has_model && has_unskinned) { b.model = unskinned; b.has_model = true; }


	auto box_sig = [](bool has, const AABB &a) {
		if (!has) return String("-");
		Vector3 p0 = a.position.snapped(Vector3(0.01f, 0.01f, 0.01f)), s0 = a.size.snapped(Vector3(0.01f, 0.01f, 0.01f));
		return String(Variant(p0)) + String(Variant(s0));
	};
	b.sig += "|" + box_sig(b.has_model, b.model) + "|" + box_sig(b.has_stock_head, b.stock_head) + "|" + box_sig(b.has_stock_torso, b.stock_torso);
	(void)rig;
	return b;
}

static Vector3 head_top(const AABB &a) {
	return Vector3(a.position.x + a.size.x * 0.5f, a.position.y + a.size.y, a.position.z + a.size.z * 0.5f);
}
static Vector3 back_point(const AABB &a, float fwd) {
	float z = fwd > 0 ? a.position.z : a.position.z + a.size.z;
	return Vector3(a.position.x + a.size.x * 0.5f, a.position.y + a.size.y * 0.55f, z);
}

struct SeatParams {
	Kind kind = kHead;
	int32_t bone = -1;
	int dup = 0;
	bool authored = false;
	bool hat = false;






	bool absolute = false;
	Vector3 place;
};


static Transform3D correction(const Body &b, const SeatParams &p, const Overrides &ov) {
	const Tuning &t = tuning();
	const float u = b.m2u;
	if (p.kind == kHead) {
		AABB stock_head = b.has_stock_head ? b.stock_head : (b.has_stock ? b.stock : b.model);
		AABB model = b.has_model ? b.model : stock_head;
		Vector3 stock_top = head_top(stock_head);
		Vector3 model_top = b.has_model ? head_top(model) : stock_top;
		Vector3 seat_stock = stock_top, seat_model = model_top;
		float s = 1.0f;
		if (p.hat) {
			seat_stock.y -= t.stock_drop_m * u;
			seat_model.y -= t.hat_drop_m * u;
			s = t.hat_scale;
		}
		if (ov.head.has_scale) s = ov.head.scale;
		if (ov.head.has_offset) seat_model += ov.head.offset * u;
		seat_model.y += float(p.dup) * t.dup_head_step_m * u;
		if (p.absolute && p.hat) seat_stock = p.place;
		if (p.authored) { seat_model = seat_stock + Vector3(0, float(p.dup) * t.dup_head_step_m * u, 0); s = 1.0f; }
		return Transform3D(Basis(), seat_model) * Transform3D(Basis().scaled(Vector3(s, s, s)), Vector3()) * Transform3D(Basis(), -seat_stock);
	}
	AABB stock_t = b.has_stock_torso ? b.stock_torso : (b.has_stock ? b.stock : b.model);
	Vector3 seat_stock = back_point(stock_t, b.fwd);
	Vector3 seat_model = b.has_model ? back_point(b.model, b.fwd) : seat_stock;
	seat_model.y -= t.tail_drop_m * u;
	float s = ov.back.has_scale ? ov.back.scale : 1.0f;
	if (ov.back.has_offset) seat_model += ov.back.offset * u;
	if (p.dup > 0) {
		float side = (p.dup % 2 == 1) ? 1.0f : -1.0f;
		seat_model.x += side * float((p.dup + 1) / 2) * t.dup_back_step_m * u;
	}
	if (p.absolute) seat_stock = p.place;
	if (p.authored) { seat_model = seat_stock; s = 1.0f; }
	return Transform3D(Basis(), seat_model) * Transform3D(Basis().scaled(Vector3(s, s, s)), Vector3()) * Transform3D(Basis(), -seat_stock);
}

static Transform3D seat_local(Skeleton3D *sk, int32_t anchor_bone, const Body &b, const SeatParams &p, const Overrides &ov) {
	Transform3D ra = sk->get_bone_global_rest(anchor_bone);
	Transform3D rg = p.bone >= 0 && p.bone < sk->get_bone_count() ? sk->get_bone_global_rest(p.bone) : ra;
	return ra.affine_inverse() * correction(b, p, ov) * rg;
}



static BoneAttachment3D *ensure_anchor(Skeleton3D *sk, const char *name, int32_t bone) {
	BoneAttachment3D *ba = Object::cast_to<BoneAttachment3D>(sk->get_node_or_null(NodePath(name)));
	if (!ba) {
		ba = memnew(BoneAttachment3D);
		ba->set_name(name);
		ba->set_meta("jvm_accessory", true);
		ba->set_bone_idx(bone);
		sk->add_child(ba);
	} else if (ba->get_bone_idx() != bone) {
		ba->set_bone_idx(bone);
	}
	return ba;
}

struct RigCtx {
	Skeleton3D *sk = nullptr;
	BoneAttachment3D *head = nullptr, *back = nullptr;
	int32_t head_b = -1, back_b = -1;
	Body body;
};

static bool make_ctx(Skeleton3D *sk, Node *rig, const Overrides &ov, RigCtx &c) {
	if (!sk || sk->get_bone_count() == 0) return false;
	c.sk = sk;
	c.head_b = head_bone(sk, ov.head, rig);
	if (c.head_b < 0) return false;
	c.back_b = back_bone(sk, ov.back, c.head_b);
	if (c.back_b < 0) c.back_b = c.head_b;
	c.head = ensure_anchor(sk, kHeadName, c.head_b);
	c.back = ensure_anchor(sk, kBackName, c.back_b);
	c.body = measure(sk, rig ? rig : sk->get_parent());
	c.body.sig += "|" + ov.key + "|" + String::num_int64(c.head_b) + "/" + String::num_int64(c.back_b);
	return true;
}

static SeatParams params_of(Node *seat) {
	SeatParams p;
	p.kind = Kind(int(seat->get_meta(kKind, 0)));
	p.bone = int32_t(int64_t(seat->get_meta(kBone, -1)));
	p.dup = int(seat->get_meta(kDup, 0));
	p.authored = bool(seat->get_meta(kAuthored, false));
	p.hat = bool(seat->get_meta(kHat, false));
	p.absolute = bool(seat->get_meta(kAbs, false));
	p.place = Vector3(seat->get_meta(kPlace, Vector3()));
	return p;
}

static Node3D *new_seat(RigCtx &c, const SeatParams &p, const String &slot, const String &acc_id, bool spawned) {
	Node3D *seat = memnew(Node3D);
	seat->set_name("JVM_Seat_" + slot);
	seat->set_meta("jvm_accessory", true);
	seat->set_meta(kSeatMeta, true);
	seat->set_meta(kKind, int(p.kind));
	seat->set_meta(kBone, p.bone);
	seat->set_meta(kSlot, slot);
	seat->set_meta(kAccId, acc_id);
	seat->set_meta(kDup, p.dup);
	seat->set_meta(kAuthored, p.authored);
	seat->set_meta(kHat, p.hat);
	seat->set_meta(kSpawned, spawned);
	seat->set_meta(kAbs, p.absolute);
	seat->set_meta(kPlace, p.place);
	(p.kind == kHead ? c.head : c.back)->add_child(seat);
	return seat;
}



static int refresh_seats(RigCtx &c, const Overrides &ov, bool force) {
	bool changed = force || String(c.head->get_meta(kSig, String())) != c.body.sig;
	int live = 0;
	for (BoneAttachment3D *anchor : { c.head, c.back }) {
		std::vector<std::pair<String, int>> seen;
		for (int i = 0; i < anchor->get_child_count(); i++) {
			Node3D *seat = Object::cast_to<Node3D>(anchor->get_child(i));
			if (!seat || !seat->has_meta(kSeatMeta) || seat->is_queued_for_deletion()) continue;
			bool empty = true;
			for (int k = 0; k < seat->get_child_count(); k++)
				if (!seat->get_child(k)->is_queued_for_deletion()) { empty = false; break; }
			if (empty) { seat->queue_free(); continue; }
			live++;
			String id = String(seat->get_meta(kAccId, String()));
			int dup = 0;
			if (!id.is_empty()) {
				bool hit = false;
				for (auto &e : seen) if (e.first == id) { dup = e.second++; hit = true; break; }
				if (!hit) seen.push_back({ id, 1 });
			}
			if (int(seat->get_meta(kDup, 0)) != dup) { seat->set_meta(kDup, dup); changed = true; }
			if (changed || !seat->has_meta(kSig)) {
				seat->set_transform(seat_local(c.sk, anchor == c.head ? c.head_b : c.back_b, c.body, params_of(seat), ov));
				seat->set_meta(kSig, true);
			}
		}
	}
	if (changed) {

		for (BoneAttachment3D *anchor : { c.head, c.back }) {
			for (int i = 0; i < anchor->get_child_count(); i++) {
				Node3D *seat = Object::cast_to<Node3D>(anchor->get_child(i));
				if (!seat || !seat->has_meta(kSeatMeta) || seat->is_queued_for_deletion()) continue;
				seat->set_transform(seat_local(c.sk, anchor == c.head ? c.head_b : c.back_b, c.body, params_of(seat), ov));
			}
		}
		c.head->set_meta(kSig, c.body.sig);
	}
	return live;
}

int sync_rig(Node *rig, const Overrides &ov) {
	if (!rig || !rig->is_inside_tree()) return 0;
	Skeleton3D *sk = rig_skeleton(rig);
	RigCtx c;
	if (!make_ctx(sk, rig, ov, c)) return 0;
	Variant ar = jvm_textures_io().get("player.outfit", rig, "_acc_roots");
	Variant ai = jvm_textures_io().get("player.outfit", rig, "_acc_ids");
	Dictionary ids = ai.get_type() == Variant::DICTIONARY ? Dictionary(ai) : Dictionary();
	BoneAttachment3D *head_mount = nullptr;
	{
		Variant hm = jvm_textures_io().get("player.outfit", rig, "_head_mount");
		if (hm.get_type() == Variant::OBJECT) head_mount = Object::cast_to<BoneAttachment3D>(hm.operator Object *());
	}
	bool adopted = false;
	if (ar.get_type() == Variant::DICTIONARY) {
		Dictionary roots = ar;
		Array keys = roots.keys();
		for (int i = 0; i < keys.size(); i++) {
			Node3D *root = Object::cast_to<Node3D>(roots[keys[i]]);
			if (!root || !root->is_inside_tree() || root->is_queued_for_deletion()) continue;
			Node *par = root->get_parent();
			if (!par || par->has_meta(kSeatMeta)) continue;
			BoneAttachment3D *mount = Object::cast_to<BoneAttachment3D>(par);

			if (!mount || mount->get_parent() != sk) continue;
			String slot = String(keys[i]);
			SeatParams p;
			p.bone = mount->get_bone_idx();
			bool head_like = (mount == head_mount) || p.bone == c.head_b || String(mount->get_name()).to_lower().contains("head");
			p.kind = head_like ? kHead : kBack;
			p.hat = head_like && slot.begins_with("head");
			String id = ids.has(keys[i]) ? String(ids[keys[i]]) : String();
			p.absolute = String(root->get_name()).begins_with("JvmAcc_") || root->has_meta("jvm_acc_id");
			p.place = (c.sk->get_bone_global_rest(p.bone) * root->get_transform()).origin;
			Node3D *seat = new_seat(c, p, slot, id, false);

			Transform3D local = root->get_transform();
			par->remove_child(root);
			seat->add_child(root);
			root->set_transform(local);
			adopted = true;
		}
	}
	return refresh_seats(c, ov, adopted);
}

static void find_rigs(Node *n, std::vector<Node *> &out, int depth) {
	if (!n || depth > 10) return;
	if (Object::cast_to<Skeleton3D>(n)) return;
	String nm = String(n->get_name());
	if (nm.begins_with("JVM_") || nm.begins_with("JvmAcc")) return;
	if (jvm_textures_io().get("player.look", n, "_surface_slots").get_type() == Variant::DICTIONARY) {
		out.push_back(n);
		return;
	}
	for (int i = 0; i < n->get_child_count(); i++) find_rigs(n->get_child(i), out, depth + 1);
}

int sync_player(Node *player, const Overrides &ov) {
	if (!player) return 0;
	std::vector<Node *> rigs;
	find_rigs(player, rigs, 0);
	int n = 0;
	for (Node *r : rigs) n += sync_rig(r, ov);
	return n;
}

Node3D *seat_for_spawn(Skeleton3D *sk, Kind kind, int32_t mount_bone, const String &slot, const String &acc_id,
		bool authored, const Vector3 &place, const Overrides &ov) {
	if (!sk) return nullptr;
	Node *rig = sk->get_parent();

	for (Node *p = sk->get_parent(); p; p = p->get_parent()) {
		if (jvm_textures_io().get("player.look", p, "_surface_slots").get_type() == Variant::DICTIONARY) { rig = p; break; }
	}
	RigCtx c;
	if (!make_ctx(sk, rig, ov, c)) return nullptr;
	SeatParams p;
	p.kind = kind;
	p.bone = mount_bone >= 0 ? mount_bone : (kind == kHead ? c.head_b : c.back_b);
	p.authored = authored;
	p.hat = kind == kHead && !slot.to_lower().begins_with("face");
	p.absolute = !authored;
	p.place = place;
	BoneAttachment3D *anchor = kind == kHead ? c.head : c.back;
	if (!acc_id.is_empty()) {
		for (int i = 0; i < anchor->get_child_count(); i++) {
			Node *s = anchor->get_child(i);
			if (s->has_meta(kSeatMeta) && !s->is_queued_for_deletion() && String(s->get_meta(kAccId, String())) == acc_id) p.dup++;
		}
	}
	Node3D *seat = new_seat(c, p, slot, acc_id, true);

	seat->set_transform(seat_local(c.sk, kind == kHead ? c.head_b : c.back_b, c.body, p, ov));
	seat->set_meta(kSig, true);
	return seat;
}

Dictionary describe(Node *player) {
	Dictionary out;
	std::vector<Node *> rigs;
	find_rigs(player, rigs, 0);
	Array ra;
	for (Node *rig : rigs) {
		Dictionary rd;
		Skeleton3D *sk = rig_skeleton(rig);
		rd["rig"] = String(rig->get_path());
		if (!sk) { ra.append(rd); continue; }
		rd["skeleton"] = String(sk->get_path());
		{
			Body b = measure(sk, rig);
			rd["m2u"] = b.m2u;
			rd["fwd"] = b.fwd;
			rd["model_aabb"] = b.has_model ? Variant(b.model) : Variant();
			rd["stock_head_aabb"] = b.has_stock_head ? Variant(b.stock_head) : Variant();
			rd["stock_torso_aabb"] = b.has_stock_torso ? Variant(b.stock_torso) : Variant();
			Array parts;
			for (int i = 0; i < sk->get_child_count(); i++) {
				MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(sk->get_child(i));
				if (!mi || !is_model_mesh(mi) || mi->get_mesh().is_null()) continue;
				Dictionary pd;
				pd["name"] = String(mi->get_name());
				pd["id"] = int64_t(mi->get_instance_id());
				pd["mesh"] = int64_t(mi->get_mesh()->get_instance_id());
				pd["skin"] = mi->get_skin().is_valid() ? int64_t(mi->get_skin()->get_instance_id()) : int64_t(0);
				pd["rest"] = rest_bounds(mi, sk);
				pd["aabb"] = mi->get_aabb();
				pd["xf"] = mi->get_transform();
				parts.append(pd);
			}
			rd["model_parts"] = parts;
		}
		Array seats;
		for (const char *an : { kHeadName, kBackName }) {
			BoneAttachment3D *ba = Object::cast_to<BoneAttachment3D>(sk->get_node_or_null(NodePath(an)));
			if (!ba) continue;
			rd[String(an) + "_bone"] = ba->get_bone_idx() >= 0 ? sk->get_bone_name(ba->get_bone_idx()) : String();
			for (int i = 0; i < ba->get_child_count(); i++) {
				Node3D *seat = Object::cast_to<Node3D>(ba->get_child(i));
				if (!seat || !seat->has_meta(kSeatMeta)) continue;
				Dictionary sd;
				sd["anchor"] = String(an);
				sd["slot"] = seat->get_meta(kSlot, "");
				sd["acc"] = seat->get_meta(kAccId, "");
				sd["dup"] = seat->get_meta(kDup, 0);
				sd["spawned"] = seat->get_meta(kSpawned, false);
				sd["local"] = seat->get_transform();
				sd["absolute"] = seat->get_meta(kAbs, false);
				sd["place"] = seat->get_meta(kPlace, Vector3());
				Array kids;
				for (int k = 0; k < seat->get_child_count(); k++) kids.append(String(seat->get_child(k)->get_name()));
				sd["children"] = kids;
				seats.append(sd);
			}
		}
		rd["seats"] = seats;
		ra.append(rd);
	}
	out["rigs"] = ra;
	return out;
}

}
