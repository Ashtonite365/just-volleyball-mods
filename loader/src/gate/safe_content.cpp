#include "safe_content.hpp"

#include <cctype>
#include <cstring>
#include <map>
#include <memory>

namespace jvm::safe {

const char *type_name(Type t) {
	switch (t) {
		case Type::Png: return "png";
		case Type::Jpeg: return "jpeg";
		case Type::Webp: return "webp";
		case Type::Glb: return "glb";
		case Type::Json: return "json";
		default: return "unknown";
	}
}

std::string lower(std::string s) {
	for (auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string ext_of(const std::string &path) {
	size_t slash = path.find_last_of("/\\");
	size_t dot = path.rfind('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
	return lower(path.substr(dot + 1));
}






std::optional<std::string> pack_rel(const std::string &in) {
	if (in.empty() || in.size() > kMaxRelPath) return std::nullopt;
	for (unsigned char c : in) {
		if (c < 0x20 || c == 0x7f) return std::nullopt;
		if (c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '%') return std::nullopt;
	}
	if (in[0] == '/' || in[0] == '~') return std::nullopt;
	std::string out, seg;
	int depth = 0;
	for (size_t i = 0; i <= in.size(); i++) {
		if (i == in.size() || in[i] == '/') {
			if (seg.empty() || seg == ".") { seg.clear(); continue; }
			if (seg[0] == '.' || seg.back() == '.' || seg.back() == ' ' || seg.find("..") != std::string::npos) return std::nullopt;
			if (++depth > kMaxRelDepth) return std::nullopt;
			out += (out.empty() ? "" : "/") + seg;
			seg.clear();
		} else seg += in[i];
	}
	if (out.empty()) return std::nullopt;
	return out;
}

bool image_ext(const std::string &e0) {
	std::string e = lower(e0);
	return e == "png" || e == "jpg" || e == "jpeg" || e == "webp";
}

bool content_ext(const std::string &e0) {
	std::string e = lower(e0);
	return image_ext(e) || e == "glb" || e == "json";
}


static const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
static uint32_t be32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
static uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
static uint32_t le24(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); }

Type sniff(const uint8_t *p, size_t n) {
	if (!p) return Type::None;
	if (n >= 8 && std::memcmp(p, kPngSig, 8) == 0) return Type::Png;
	if (n >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff) return Type::Jpeg;
	if (n >= 12 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WEBP", 4) == 0) return Type::Webp;
	if (n >= 12 && std::memcmp(p, "glTF", 4) == 0) return Type::Glb;
	size_t i = 0;
	if (n >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) i = 3;
	while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) i++;
	if (i < n && p[i] == '{') return Type::Json;
	return Type::None;
}

std::optional<ImageInfo> probe_image(const uint8_t *p, size_t n) {
	ImageInfo info;
	info.type = sniff(p, n);
	switch (info.type) {
		case Type::Png: {
			if (n < 33 || be32(p + 8) != 13 || std::memcmp(p + 12, "IHDR", 4) != 0) return std::nullopt;
			info.width = be32(p + 16); info.height = be32(p + 20);
			break;
		}
		case Type::Jpeg: {
			size_t i = 2;
			bool found = false;
			while (i + 4 <= n) {
				if (p[i] != 0xff) return std::nullopt;
				uint8_t m = p[i + 1];
				if (m == 0xff) { i++; continue; }
				if (m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7)) { i += 2; continue; }
				if (m == 0xd9 || m == 0xda) break;
				uint32_t len = (uint32_t(p[i + 2]) << 8) | p[i + 3];
				if (len < 2 || i + 2 + len > n) return std::nullopt;
				if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {
					if (len < 7) return std::nullopt;
					info.height = (uint32_t(p[i + 5]) << 8) | p[i + 6];
					info.width = (uint32_t(p[i + 7]) << 8) | p[i + 8];
					found = true;
					break;
				}
				i += 2 + len;
			}
			if (!found) return std::nullopt;
			break;
		}
		case Type::Webp: {
			if (n < 30) return std::nullopt;
			if (std::memcmp(p + 12, "VP8 ", 4) == 0) {
				if (p[23] != 0x9d || p[24] != 0x01 || p[25] != 0x2a) return std::nullopt;
				info.width = (uint32_t(p[26]) | (uint32_t(p[27]) << 8)) & 0x3fff;
				info.height = (uint32_t(p[28]) | (uint32_t(p[29]) << 8)) & 0x3fff;
			} else if (std::memcmp(p + 12, "VP8L", 4) == 0) {
				if (p[20] != 0x2f) return std::nullopt;
				uint32_t b = le32(p + 21);
				info.width = (b & 0x3fff) + 1;
				info.height = ((b >> 14) & 0x3fff) + 1;
			} else if (std::memcmp(p + 12, "VP8X", 4) == 0) {
				info.width = le24(p + 24) + 1;
				info.height = le24(p + 27) + 1;
			} else return std::nullopt;
			break;
		}
		default: return std::nullopt;
	}
	return info;
}

static bool ext_matches(const std::string &ext, Type t) {
	if (ext == "png") return t == Type::Png;
	if (ext == "jpg" || ext == "jpeg") return t == Type::Jpeg;
	if (ext == "webp") return t == Type::Webp;
	return false;
}

std::string check_image(const uint8_t *p, size_t n, const std::string &ext0, ImageInfo *out) {
	std::string ext = lower(ext0);
	if (!image_ext(ext)) return "not an allowed image type ('." + ext + "'; png, jpg, webp only)";
	if (n == 0) return "empty file";
	if (n > kMaxImageFileBytes) return "image file larger than " + std::to_string(kMaxImageFileBytes) + " bytes";
	auto info = probe_image(p, n);
	if (!info) return "not a valid png / jpeg / webp (magic bytes or header)";
	if (!ext_matches(ext, info->type)) return std::string("content is ") + type_name(info->type) + " but the name says ." + ext;
	if (info->width == 0 || info->height == 0) return "zero-sized image";
	if (info->width > kMaxImageSide || info->height > kMaxImageSide) return "image " + std::to_string(info->width) + "x" + std::to_string(info->height) + " exceeds " + std::to_string(kMaxImageSide) + " px per side";
	if (uint64_t(info->width) * info->height > kMaxImagePixels) return "image " + std::to_string(info->width) + "x" + std::to_string(info->height) + " exceeds the pixel budget";
	if (out) *out = *info;
	return "";
}


namespace {
struct JVal;
using JPtr = std::shared_ptr<JVal>;
struct JVal {
	enum K { Null, Bool, Num, Str, Arr, Obj } k = Null;
	bool b = false;
	std::string s;
	std::vector<JPtr> a;
	std::vector<std::pair<std::string, JPtr>> o;
	JPtr get(const std::string &key) const { for (auto &kv : o) if (kv.first == key) return kv.second; return nullptr; }
	bool has(const std::string &key) const { return get(key) != nullptr; }
	void erase(const std::string &key) { for (size_t i = 0; i < o.size(); i++) if (o[i].first == key) { o.erase(o.begin() + long(i)); return; } }
	bool as_index(uint64_t &v) const {
		if (k != Num || s.empty() || s.size() > 15) return false;
		v = 0;
		for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + uint64_t(c - '0'); }
		return true;
	}
};

struct Parser {
	const char *p, *e;
	int depth = 0;
	std::string err;
	void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
	bool fail(const char *w) { if (err.empty()) err = w; return false; }
	static void put_utf8(std::string &out, uint32_t cp) {
		if (cp < 0x80) out += char(cp);
		else if (cp < 0x800) { out += char(0xc0 | (cp >> 6)); out += char(0x80 | (cp & 0x3f)); }
		else if (cp < 0x10000) { out += char(0xe0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3f)); out += char(0x80 | (cp & 0x3f)); }
		else { out += char(0xf0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3f)); out += char(0x80 | ((cp >> 6) & 0x3f)); out += char(0x80 | (cp & 0x3f)); }
	}
	bool hex4(uint32_t &v) {
		if (e - p < 4) return fail("bad \\u escape");
		v = 0;
		for (int i = 0; i < 4; i++) {
			char c = *p++; v <<= 4;
			if (c >= '0' && c <= '9') v |= uint32_t(c - '0'); else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10); else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10); else return fail("bad \\u escape");
		}
		return true;
	}
	bool str(std::string &out) {
		if (p >= e || *p != '"') return fail("expected string");
		p++;
		while (p < e && *p != '"') {
			unsigned char c = static_cast<unsigned char>(*p);
			if (c < 0x20) return fail("control character in string");
			if (c == '\\') {
				if (++p >= e) return fail("bad escape");
				char x = *p++;
				switch (x) {
					case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
					case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
					case 'r': out += '\r'; break; case 't': out += '\t'; break;
					case 'u': {
						uint32_t cp; if (!hex4(cp)) return false;
						if (cp >= 0xd800 && cp <= 0xdbff) {
							uint32_t lo;
							if (e - p < 6 || p[0] != '\\' || p[1] != 'u') return fail("lone surrogate");
							p += 2; if (!hex4(lo)) return false;
							if (lo < 0xdc00 || lo > 0xdfff) return fail("bad surrogate");
							cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
						} else if (cp >= 0xdc00 && cp <= 0xdfff) return fail("lone surrogate");
						put_utf8(out, cp);
						break;
					}
					default: return fail("bad escape");
				}
			} else { out += char(c); p++; }
		}
		if (p >= e) return fail("unterminated string");
		p++;
		return true;
	}
	bool val(JPtr &v) {
		if (++depth > 64) return fail("JSON nested too deep");
		ws();
		if (p >= e) return fail("unexpected end");
		v = std::make_shared<JVal>();
		char c = *p;
		bool ok = true;
		if (c == '{') {
			v->k = JVal::Obj; p++; ws();
			if (p < e && *p == '}') p++;
			else for (;;) {
				ws(); std::string key; if (!str(key)) { ok = false; break; }
				ws(); if (p >= e || *p != ':') { ok = fail("expected ':'"); break; }
				p++; JPtr child; if (!val(child)) { ok = false; break; }
				for (auto &kv : v->o) if (kv.first == key) { ok = fail("duplicate key"); break; }
				if (!ok) break;
				v->o.push_back({ key, child });
				ws(); if (p < e && *p == ',') { p++; continue; }
				if (p < e && *p == '}') { p++; break; }
				ok = fail("expected ',' or '}'"); break;
			}
		} else if (c == '[') {
			v->k = JVal::Arr; p++; ws();
			if (p < e && *p == ']') p++;
			else for (;;) {
				JPtr child; if (!val(child)) { ok = false; break; }
				v->a.push_back(child);
				ws(); if (p < e && *p == ',') { p++; continue; }
				if (p < e && *p == ']') { p++; break; }
				ok = fail("expected ',' or ']'"); break;
			}
		} else if (c == '"') { v->k = JVal::Str; ok = str(v->s); }
		else if (e - p >= 4 && std::memcmp(p, "true", 4) == 0) { v->k = JVal::Bool; v->b = true; p += 4; }
		else if (e - p >= 5 && std::memcmp(p, "false", 5) == 0) { v->k = JVal::Bool; p += 5; }
		else if (e - p >= 4 && std::memcmp(p, "null", 4) == 0) { v->k = JVal::Null; p += 4; }
		else if (c == '-' || (c >= '0' && c <= '9')) {
			const char *st = p;
			if (*p == '-') p++;
			if (p >= e || !(*p >= '0' && *p <= '9')) return fail("bad number");
			while (p < e && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) p++;
			v->k = JVal::Num; v->s.assign(st, size_t(p - st));
			if (v->s.size() > 64) return fail("number too long");
		} else ok = fail("unexpected character");
		depth--;
		return ok;
	}
};

static void esc(std::string &out, const std::string &s) {
	out += '"';
	static const char *hx = "0123456789abcdef";
	for (unsigned char c : s) {
		if (c == '"') out += "\\\"";
		else if (c == '\\') out += "\\\\";
		else if (c < 0x20) { out += "\\u00"; out += hx[c >> 4]; out += hx[c & 15]; }
		else out += char(c);
	}
	out += '"';
}
static void dump(std::string &out, const JPtr &v) {
	if (!v) { out += "null"; return; }
	switch (v->k) {
		case JVal::Null: out += "null"; break;
		case JVal::Bool: out += v->b ? "true" : "false"; break;
		case JVal::Num: out += v->s; break;
		case JVal::Str: esc(out, v->s); break;
		case JVal::Arr: out += '['; for (size_t i = 0; i < v->a.size(); i++) { if (i) out += ','; dump(out, v->a[i]); } out += ']'; break;
		case JVal::Obj: out += '{'; for (size_t i = 0; i < v->o.size(); i++) { if (i) out += ','; esc(out, v->o[i].first); out += ':'; dump(out, v->o[i].second); } out += '}'; break;
	}
}

static bool parse_json(const uint8_t *b, size_t n, JPtr &root, std::string &err) {
	Parser ps{ reinterpret_cast<const char *>(b), reinterpret_cast<const char *>(b) + n, 0, std::string() };
	if (n >= 3 && b[0] == 0xef && b[1] == 0xbb && b[2] == 0xbf) ps.p += 3;
	if (!ps.val(root)) { err = ps.err; return false; }
	ps.ws();
	while (ps.p < ps.e && *ps.p == '\0') ps.p++;
	if (ps.p != ps.e) { err = "trailing data after JSON"; return false; }
	return true;
}

static void strip_tree(const JPtr &v, GlbResult &r, int depth = 0) {
	if (!v || depth > 64) return;
	if (v->k == JVal::Obj) {
		if (v->has("extras")) { v->erase("extras"); r.stripped_extras++; }
		if (JPtr ex = v->get("extensions")) {
			if (ex->k != JVal::Obj) { v->erase("extensions"); r.stripped_extensions++; }
			else {
				for (size_t i = 0; i < ex->o.size();) {
					if (glb_extension_allowed(ex->o[i].first)) i++;
					else { ex->o.erase(ex->o.begin() + long(i)); r.stripped_extensions++; }
				}
				if (ex->o.empty()) v->erase("extensions");
			}
		}
		for (auto &kv : v->o) strip_tree(kv.second, r, depth + 1);
	} else if (v->k == JVal::Arr) {
		for (auto &c : v->a) strip_tree(c, r, depth + 1);
	}
}
}

bool glb_extension_allowed(const std::string &n) {
	static const char *ok[] = { "KHR_materials_unlit", "KHR_materials_emissive_strength", "KHR_materials_clearcoat",
		"KHR_texture_transform", "KHR_mesh_quantization", "GODOT_single_root" };
	for (const char *o : ok) if (n == o) return true;
	return false;
}

GlbResult sanitize_glb(const uint8_t *p, size_t n) {
	GlbResult r;
	auto bad = [&](const std::string &w) { r.error = w; r.glb.clear(); return r; };
	if (!p || n < 20) return bad("not a GLB (too short)");
	if (n > kMaxGlbBytes) return bad("GLB larger than " + std::to_string(kMaxGlbBytes) + " bytes");
	if (std::memcmp(p, "glTF", 4) != 0) return bad("not a GLB (magic bytes)");
	if (le32(p + 4) != 2) return bad("GLB version is not 2");
	if (le32(p + 8) != n) return bad("GLB length header does not match the file size");
	uint32_t jlen = le32(p + 12);
	if (le32(p + 16) != 0x4E4F534Au) return bad("first GLB chunk is not JSON");
	if (jlen == 0 || uint64_t(jlen) > n - 20) return bad("GLB JSON chunk out of bounds");
	const uint8_t *jp = p + 20;
	const uint8_t *bin = nullptr;
	uint64_t bin_len = 0;
	uint64_t off = 20 + uint64_t(jlen);
	while (off + 8 <= n) {
		uint32_t clen = le32(p + off), ctype = le32(p + off + 4);
		if (uint64_t(clen) > n - off - 8) return bad("GLB chunk out of bounds");
		if (ctype == 0x004E4942u && !bin) { bin = p + off + 8; bin_len = clen; }
		else r.dropped_chunks++;
		off += 8 + uint64_t(clen);
	}

	JPtr root;
	std::string jerr;
	if (!parse_json(jp, jlen, root, jerr)) return bad("GLB JSON: " + jerr);
	if (!root || root->k != JVal::Obj) return bad("GLB JSON root is not an object");
	JPtr asset = root->get("asset");
	if (!asset || asset->k != JVal::Obj) return bad("GLB has no asset block");
	JPtr ver = asset->get("version");
	if (!ver || ver->k != JVal::Str || ver->s.rfind("2", 0) != 0) return bad("glTF asset.version is not 2.x");

	if (JPtr req = root->get("extensionsRequired")) {
		if (req->k != JVal::Arr) return bad("extensionsRequired is not a list");
		for (auto &x : req->a) {
			if (!x || x->k != JVal::Str) return bad("extensionsRequired entry is not a string");
			if (!glb_extension_allowed(x->s)) return bad("requires glTF extension '" + x->s + "' (not on the data-only allow-list)");
		}
	}
	if (JPtr used = root->get("extensionsUsed")) {
		if (used->k != JVal::Arr) root->erase("extensionsUsed");
		else {
			for (size_t i = 0; i < used->a.size();) {
				if (used->a[i] && used->a[i]->k == JVal::Str && glb_extension_allowed(used->a[i]->s)) i++;
				else { used->a.erase(used->a.begin() + long(i)); r.stripped_extensions++; }
			}
		}
	}

	std::vector<uint64_t> buf_len;
	if (JPtr bufs = root->get("buffers")) {
		if (bufs->k != JVal::Arr) return bad("buffers is not a list");
		for (size_t i = 0; i < bufs->a.size(); i++) {
			JPtr b = bufs->a[i];
			if (!b || b->k != JVal::Obj) return bad("buffers[" + std::to_string(i) + "] is not an object");
			if (b->has("uri")) return bad("buffers[" + std::to_string(i) + "] has a uri (external or data: references are refused; pack the mesh as a self-contained .glb)");
			if (i != 0 || !bin) return bad("buffers[" + std::to_string(i) + "] has no data in the GLB");
			uint64_t bl;
			JPtr blv = b->get("byteLength");
			if (!blv || !blv->as_index(bl) || bl > bin_len) return bad("buffers[0].byteLength is larger than the GLB binary chunk");
			buf_len.push_back(bl);
		}
	}
	JPtr views = root->get("bufferViews");
	if (views && views->k != JVal::Arr) return bad("bufferViews is not a list");
	if (views) {
		for (size_t i = 0; i < views->a.size(); i++) {
			JPtr v = views->a[i];
			uint64_t bi, bo = 0, bl;
			if (!v || v->k != JVal::Obj) return bad("bufferViews[" + std::to_string(i) + "] is not an object");
			JPtr biv = v->get("buffer"), bov = v->get("byteOffset"), blv = v->get("byteLength");
			if (!biv || !biv->as_index(bi) || bi >= buf_len.size()) return bad("bufferViews[" + std::to_string(i) + "] names a missing buffer");
			if (bov && !bov->as_index(bo)) return bad("bufferViews[" + std::to_string(i) + "].byteOffset invalid");
			if (!blv || !blv->as_index(bl) || bo + bl > buf_len[bi]) return bad("bufferViews[" + std::to_string(i) + "] is out of bounds");
		}
	}
	if (JPtr imgs = root->get("images")) {
		if (imgs->k != JVal::Arr) return bad("images is not a list");
		for (size_t i = 0; i < imgs->a.size(); i++) {
			JPtr im = imgs->a[i];
			std::string tag = "images[" + std::to_string(i) + "]";
			if (!im || im->k != JVal::Obj) return bad(tag + " is not an object");
			if (im->has("uri")) return bad(tag + " has a uri (external file or data: image references are refused)");
			JPtr mt = im->get("mimeType"), bvv = im->get("bufferView");
			uint64_t bv;
			if (!mt || mt->k != JVal::Str || (mt->s != "image/png" && mt->s != "image/jpeg")) return bad(tag + " mimeType must be image/png or image/jpeg");
			if (!bvv || !bvv->as_index(bv) || !views || bv >= views->a.size()) return bad(tag + " has no valid bufferView");
			JPtr v = views->a[bv];
			uint64_t bo = 0, bl = 0;
			if (JPtr x = v->get("byteOffset")) x->as_index(bo);
			v->get("byteLength")->as_index(bl);
			ImageInfo info;
			std::string ierr = check_image(bin + bo, size_t(bl), mt->s == "image/png" ? "png" : "jpg", &info);
			if (!ierr.empty()) return bad(tag + ": " + ierr);
			r.images++;
		}
	}
	strip_tree(root, r);

	std::string js;
	dump(js, root);
	while (js.size() % 4) js += ' ';
	uint64_t bin_pad = (bin_len + 3) & ~uint64_t(3);
	uint64_t total = 12 + 8 + js.size() + (bin ? 8 + bin_pad : 0);
	if (total > kMaxGlbBytes + 64) return bad("GLB too large after sanitising");
	r.glb.reserve(size_t(total));
	auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) r.glb.push_back(uint8_t((v >> (8 * i)) & 0xff)); };
	r.glb.insert(r.glb.end(), { 'g', 'l', 'T', 'F' });
	put32(2); put32(uint32_t(total));
	put32(uint32_t(js.size())); put32(0x4E4F534Au);
	r.glb.insert(r.glb.end(), js.begin(), js.end());
	if (bin) {
		put32(uint32_t(bin_pad)); put32(0x004E4942u);
		r.glb.insert(r.glb.end(), bin, bin + bin_len);
		for (uint64_t i = bin_len; i < bin_pad; i++) r.glb.push_back(0);
	}
	return r;
}

std::string check_json_bytes(const uint8_t *p, size_t n) {
	if (n > kMaxJsonBytes) return "JSON larger than " + std::to_string(kMaxJsonBytes) + " bytes";
	JPtr root;
	std::string err;
	if (!parse_json(p, n, root, err)) return "invalid JSON: " + err;
	if (!root || root->k != JVal::Obj) return "JSON root is not an object";
	return "";
}

std::string check_content_file(const std::string &rel, const uint8_t *p, size_t n) {
	std::string e = ext_of(rel);
	if (!content_ext(e)) return "'." + e + "' files are not allowed in a pack (png, jpg, webp, glb, json only)";
	if (image_ext(e)) return check_image(p, n, e);
	if (e == "glb") { GlbResult g = sanitize_glb(p, n); return g.error; }
	return check_json_bytes(p, n);
}




bool game_texture_target(const std::string &rp) {
	if (rp.size() < 7 || rp.size() > 240 || rp.compare(0, 6, "res://") != 0) return false;
	std::string rest = rp.substr(6);
	if (rest.empty() || rest[0] == '/') return false;
	for (unsigned char c : rest) if (c < 0x20 || c == '\\' || c == ':' || c == '%') return false;
	std::string low = lower(rest);
	if (low.rfind("mods/", 0) == 0 || low == "mods" || low.rfind(".godot/", 0) == 0) return false;
	if (rest.find("..") != std::string::npos || rest.find("//") != std::string::npos) return false;
	std::string e = ext_of(rest);
	return e == "png" || e == "jpg" || e == "jpeg" || e == "webp" || e == "svg" || e == "tga" || e == "bmp";
}

}
