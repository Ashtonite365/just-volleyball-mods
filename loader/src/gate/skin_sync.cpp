#include "skin_sync.hpp"
#include <cctype>
#include <cstring>

namespace jvm::skins {

std::optional<SkinId> parse_id(const std::string &s) {
	if (s.size() != 4) return std::nullopt;
	unsigned v = 0;
	for (char c : s) {
		int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else return std::nullopt;
		v = v * 16 + (unsigned)d;
	}
	return (SkinId)v;
}

std::string format_id(SkinId id) {
	static const char *h = "0123456789ABCDEF";
	std::string s(4, '0');
	for (int i = 3; i >= 0; --i) { s[i] = h[id & 0xF]; id >>= 4; }
	return s;
}

Registry build_registry(const std::vector<PackInfo> &packs) {
	Registry r;
	for (const auto &p : packs) {
		auto id = parse_id(p.id_text);
		if (!id) { r.warnings.push_back("pack '" + p.pack + "' has invalid id '" + p.id_text + "' (need 4 hex chars); skipped"); continue; }
		auto it = r.by_id.find(*id);
		if (it != r.by_id.end()) { r.warnings.push_back("id " + format_id(*id) + " collision: '" + p.pack + "' ignored, '" + it->second + "' kept"); continue; }
		r.by_id[*id] = p.pack;
	}
	return r;
}

static void put_u16(std::vector<uint8_t> &b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
static void put_i32(std::vector<uint8_t> &b, int32_t v) { uint32_t u = (uint32_t)v; for (int i = 0; i < 4; i++) b.push_back((u >> (8 * i)) & 0xFF); }

std::vector<uint8_t> encode(const Packet &p) {
	std::vector<uint8_t> b = { 'J', 'V', 'M', 'S', kVersion, (uint8_t)p.type };
	auto put_ids = [&]() {
		size_t n = p.ids.size() < kMaxIds ? p.ids.size() : kMaxIds;
		b.push_back((uint8_t)n);
		for (size_t i = 0; i < n; i++) put_u16(b, p.ids[i]);
	};
	switch (p.type) {
		case Type::Hello: case Type::Welcome: break;
		case Type::Equip: put_ids(); break;
		case Type::EquipOf: {
			put_i32(b, p.peer);
			std::string n = p.name.substr(0, kMaxName);
			b.push_back((uint8_t)n.size());
			b.insert(b.end(), n.begin(), n.end());
			put_ids();
			break;
		}
		case Type::Leave: put_i32(b, p.peer); break;
	}
	return b;
}

std::optional<Packet> decode(const std::vector<uint8_t> &b) {
	if (b.size() < 6 || std::memcmp(b.data(), "JVMS", 4) != 0 || b[4] != kVersion) return std::nullopt;
	Packet p;
	size_t i = 6;
	auto need = [&](size_t n) { return i + n <= b.size(); };
	auto get_i32 = [&](int32_t &v) { if (!need(4)) return false; uint32_t u = 0; for (int k = 0; k < 4; k++) u |= (uint32_t)b[i + k] << (8 * k); v = (int32_t)u; i += 4; return true; };
	auto get_ids = [&]() {
		if (!need(1)) return false;
		size_t n = b[i++];
		if (n > kMaxIds || !need(2 * n)) return false;
		for (size_t k = 0; k < n; k++) { p.ids.push_back((SkinId)(b[i] | (b[i + 1] << 8))); i += 2; }
		return true;
	};
	switch (b[5]) {
		case 1: p.type = Type::Hello; break;
		case 2: p.type = Type::Welcome; break;
		case 3: p.type = Type::Equip; if (!get_ids()) return std::nullopt; break;
		case 4: {
			p.type = Type::EquipOf;
			if (!get_i32(p.peer) || !need(1)) return std::nullopt;
			size_t n = b[i++];
			if (n > kMaxName || !need(n)) return std::nullopt;
			p.name.assign((const char *)b.data() + i, n); i += n;
			if (!get_ids()) return std::nullopt;
			break;
		}
		case 5: p.type = Type::Leave; if (!get_i32(p.peer)) return std::nullopt; break;
		default: return std::nullopt;
	}
	if (i != b.size()) return std::nullopt;
	return p;
}

std::vector<std::string> missing_lines(const std::map<SkinId, std::string> &installed, const std::map<int32_t, RemotePlayer> &remote) {
	std::vector<std::string> out;
	for (const auto &[peer, rp] : remote) {
		for (SkinId id : rp.ids)
			if (!installed.count(id)) out.push_back("You're missing " + format_id(id) + " from " + (rp.name.empty() ? "player " + std::to_string(peer) : rp.name));
	}
	return out;
}

}
