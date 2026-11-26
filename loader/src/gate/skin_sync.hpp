

#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace jvm::skins {

using SkinId = uint16_t;

std::optional<SkinId> parse_id(const std::string &s);
std::string format_id(SkinId id);

struct PackInfo { std::string pack; std::string id_text; };
struct Registry {
	std::map<SkinId, std::string> by_id;
	std::vector<std::string> warnings;
};

Registry build_registry(const std::vector<PackInfo> &packs);



enum class Type : uint8_t { Hello = 1, Welcome = 2, Equip = 3, EquipOf = 4, Leave = 5 };
constexpr uint8_t kVersion = 1;
constexpr size_t kMaxIds = 32;
constexpr size_t kMaxName = 64;

struct Packet {
	Type type = Type::Hello;
	int32_t peer = 0;
	std::string name;
	std::vector<SkinId> ids;
};

std::vector<uint8_t> encode(const Packet &p);
std::optional<Packet> decode(const std::vector<uint8_t> &bytes);


struct RemotePlayer { std::string name; std::vector<SkinId> ids; };

std::vector<std::string> missing_lines(const std::map<SkinId, std::string> &installed,
		const std::map<int32_t, RemotePlayer> &remote);

}
