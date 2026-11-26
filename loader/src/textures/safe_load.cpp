#include "safe_load.hpp"

#include "common/jvm_common.hpp"
#include "gate/safe_content.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/node3d.hpp>

using namespace godot;

namespace jvm::safe {

static std::string S(const String &s) { return std::string(s.utf8().get_data()); }
static String G(const std::string &s) { return String::utf8(s.c_str()); }

static int64_t g_refused = 0;
static std::string g_last;

int64_t refused_count() { return g_refused; }
String last_refusal() { return G(g_last); }
void note_refusal(const String &what) {
	g_refused++;
	g_last = S(what);
	jvm::warn("security", "refused: " + what);
}

String pack_file(const String &pack_dir, const String &rel, bool images_only) {
	if (pack_dir.is_empty() || rel.is_empty()) return "";
	auto p = pack_rel(S(rel));
	if (!p) { note_refusal("unsafe path '" + rel.left(120) + "' in " + pack_dir.get_file() + " (must be a plain path inside the pack)"); return ""; }
	String e = G(ext_of(*p));
	if (images_only ? !image_ext(S(e)) : !content_ext(S(e))) {
		note_refusal("'" + rel.left(120) + "' in " + pack_dir.get_file() + ": ." + e + " is not an allowed pack file type");
		return "";
	}
	return pack_dir.path_join(G(*p));
}

bool no_links(const String &pack_dir, const String &full) {
	if (!full.begins_with(pack_dir + String("/"))) return false;
	String rel = full.substr(pack_dir.length() + 1);
	PackedStringArray parts = rel.split("/", false);
	String cur = pack_dir;
	for (int i = 0; i < parts.size(); i++) {
		Ref<DirAccess> d = DirAccess::open(cur);
		if (d.is_null()) return true;
		if (d->is_link(parts[i])) return false;
		cur = cur.path_join(parts[i]);
	}
	return true;
}

static bool read_capped(const String &path, uint64_t cap, PackedByteArray &out, String &why) {
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (f.is_null()) { why = "cannot open"; return false; }
	uint64_t len = f->get_length();
	if (len == 0) { why = "empty file"; return false; }
	if (len > cap) { why = "file larger than " + String::num_int64(int64_t(cap)) + " bytes"; return false; }
	out = f->get_buffer(int64_t(len));
	if (uint64_t(out.size()) != len) { why = "short read"; return false; }
	return true;
}

Ref<Image> load_image(const String &path, String &why) {
	why = "";
	if (path.begins_with("res://") || path.begins_with("user://") || path.contains("://")) { why = "not a pack file path"; return Ref<Image>(); }
	PackedByteArray b;
	if (!read_capped(path, kMaxImageFileBytes, b, why)) return Ref<Image>();
	ImageInfo info;
	std::string e = check_image(b.ptr(), size_t(b.size()), S(path.get_extension()), &info);
	if (!e.empty()) { why = G(e); return Ref<Image>(); }
	Ref<Image> img;
	img.instantiate();
	Error err = FAILED;
	switch (info.type) {
		case Type::Png: err = img->load_png_from_buffer(b); break;
		case Type::Jpeg: err = img->load_jpg_from_buffer(b); break;
		case Type::Webp: err = img->load_webp_from_buffer(b); break;
		default: break;
	}
	if (err != OK || img->is_empty()) { why = "decoder rejected the data"; return Ref<Image>(); }
	if (uint32_t(img->get_width()) != info.width || uint32_t(img->get_height()) != info.height) { why = "decoded size differs from the header"; return Ref<Image>(); }
	return img;
}

Node *load_glb(const String &path, String &why) {
	why = "";
	if (path.is_empty()) { why = "empty path"; return nullptr; }
	if (path.get_extension().to_lower() != "glb") { why = "only binary glTF (.glb) meshes are allowed"; return nullptr; }
	if (path.begins_with("res://") || path.begins_with("user://") || path.contains("://")) { why = "not a pack file path"; return nullptr; }
	PackedByteArray b;
	if (!read_capped(path, kMaxGlbBytes, b, why)) return nullptr;
	GlbResult r = sanitize_glb(b.ptr(), size_t(b.size()));
	if (!r.error.empty()) { why = G(r.error); return nullptr; }
	if (r.stripped_extensions || r.stripped_extras || r.dropped_chunks)
		jvm::log("security", path.get_file() + ": stripped " + String::num_int64(r.stripped_extensions) + " glTF extension(s), " +
				String::num_int64(r.stripped_extras) + " extras block(s), " + String::num_int64(r.dropped_chunks) + " unknown chunk(s)");
	PackedByteArray clean;
	clean.resize(int64_t(r.glb.size()));
	if (!r.glb.empty()) memcpy(clean.ptrw(), r.glb.data(), r.glb.size());
	Ref<GLTFDocument> doc;
	doc.instantiate();
	Ref<GLTFState> state;
	state.instantiate();
	state->set_handle_binary_image(GLTFState::HANDLE_BINARY_EMBED_AS_UNCOMPRESSED);
	Error err = doc->append_from_buffer(clean, "", state, 0);
	if (err != OK) { why = "GLTF parse failed (" + String::num_int64(err) + ")"; return nullptr; }
	Node *gen = doc->generate_scene(state);
	if (!gen) { why = "generate_scene returned null"; return nullptr; }
	return gen;
}

}
