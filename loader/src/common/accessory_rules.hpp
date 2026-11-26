#pragma once


#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace jvm::acc {

inline const char *kHead = "head";
inline const char *kFace = "face";
inline const char *kExtra = "extra";
inline constexpr int kSlotsPerCategory = 3;

inline std::string lower(std::string s) {
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

inline bool is_locker_category(const std::string &c) {
	return c == kHead || c == kFace || c == kExtra;
}


inline std::string slot_name(const std::string &category, int n) {
	if (n <= 0) return category;
	return category + std::to_string(n + 1);
}

inline std::string category_of_slot(const std::string &slot) {
	std::string cat = slot;
	while (!cat.empty() && std::isdigit(static_cast<unsigned char>(cat.back()))) cat.pop_back();
	return cat;
}






inline std::string locker_category(bool has_hat, bool has_face, bool has_back,
		const std::string &explicit_category, const std::string &type_or_kind = "") {
	const std::string ex = lower(explicit_category);
	if (is_locker_category(ex)) return ex;
	const std::string typ = lower(type_or_kind);
	if (typ == kFace && has_face) return kFace;
	if (has_hat) return kHead;
	if (has_face || typ == kFace) return kFace;
	if (has_back) return kExtra;
	return "";
}

struct AccEntry {
	std::string id;
	std::string category;
	std::string part;
};




inline std::vector<AccEntry> entries_for_pack(const std::string &pack_id,
		bool has_hat, bool has_face, bool has_back,
		const std::string &explicit_category = "", const std::string &type_or_kind = "") {
	std::vector<AccEntry> parts;
	if (has_hat) parts.push_back({pack_id, kHead, "hat"});
	if (has_face) parts.push_back({pack_id, kFace, "face"});
	if (has_back) parts.push_back({pack_id, kExtra, "back"});
	if (parts.empty()) {
		const std::string c = locker_category(false, false, false, explicit_category, type_or_kind);
		if (c == kFace) parts.push_back({pack_id, kFace, "face"});
		return parts;
	}
	const std::string ex = lower(explicit_category);
	if (is_locker_category(ex) && parts.size() == 1) parts[0].category = ex;
	if (parts.size() == 1) return parts;
	for (AccEntry &e : parts) e.id = pack_id + "_" + e.part;
	return parts;
}

struct Migration {
	std::map<std::string, std::string> set_slots;
	std::vector<std::string> unplaced;
	std::vector<std::string> already;
};



inline Migration migrate(const std::map<std::string, std::string> &current_slots,
		const std::vector<std::string> &equipped_ids,
		const std::map<std::string, std::string> &id_to_category,
		int slots_per_category = kSlotsPerCategory) {
	Migration m;
	std::set<std::string> worn;
	for (const auto &kv : current_slots) if (!kv.second.empty()) worn.insert(kv.second);
	auto occupied = [&](const std::string &slot) {
		auto it = current_slots.find(slot);
		if (it != current_slots.end() && !it->second.empty()) return true;
		auto jt = m.set_slots.find(slot);
		return jt != m.set_slots.end() && !jt->second.empty();
	};
	for (const std::string &id : equipped_ids) {
		auto cat_it = id_to_category.find(id);
		if (cat_it == id_to_category.end() || !is_locker_category(cat_it->second)) continue;
		if (worn.count(id)) { m.already.push_back(id); continue; }
		bool placed = false;
		for (int n = 0; n < slots_per_category; n++) {
			const std::string slot = slot_name(cat_it->second, n);
			if (occupied(slot)) continue;
			m.set_slots[slot] = id;
			worn.insert(id);
			placed = true;
			break;
		}
		if (!placed) m.unplaced.push_back(id);
	}
	return m;
}




inline std::vector<std::string> sync_equipped(
		const std::vector<std::string> &equipped_ids,
		const std::vector<std::string> &worn_accessory_ids,
		const std::set<std::string> &accessory_pack_ids) {
	std::set<std::string> worn(worn_accessory_ids.begin(), worn_accessory_ids.end());
	std::vector<std::string> out;
	std::set<std::string> seen;
	for (const std::string &id : equipped_ids) {
		const bool acc = accessory_pack_ids.count(id) != 0;
		if (acc && !worn.count(id)) continue;
		if (seen.count(id)) continue;
		out.push_back(id);
		seen.insert(id);
	}
	for (const std::string &id : worn_accessory_ids) {
		if (!accessory_pack_ids.count(id) || seen.count(id)) continue;
		out.push_back(id);
		seen.insert(id);
	}
	return out;
}

}
