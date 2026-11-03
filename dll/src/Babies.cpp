#include "Babies.h"

#include <nlohmann/json.hpp>

namespace ONPCR::Babies
{
	namespace
	{
		// one "where is the child" reason; the filters keep each place sensible for the family (all must fit)
		struct Reason
		{
			std::string              id;
			std::string              place;
			std::vector<std::string> races;    // a parent's race EditorID contains one of these
			std::vector<std::string> home;     // a parent's home (or a location above it) carries one of these keywords
			std::vector<std::string> notHome;  // no parent's home carries any of these keywords
			std::vector<std::string> nearby;     // a parent's home name (or one above it) contains one of these words
			std::vector<std::string> notNearby;  // no parent's home name contains any of these words
			std::vector<int>         classes;    // 0.25.0: a parent's M.A.R.A.S social class is one of these (0 Outcast ... 7 Rulers)
		};

		// 0.25.0: the "How is the little one?" answers in the Letters esp are tied to these slots (faction rank = slot + 1)
		constexpr std::array<std::string_view, 19> kExpectedIds{ "grandparents", "temple", "aunt", "nurse", "healers", "kin_cyrodiil",
			"kin_highrock", "kin_hammerfell", "kin_morrowind", "kin_solstheim", "kin_summerset", "kin_valenwood", "kin_wrothgar",
			"kin_elsweyr", "kin_blackmarsh", "coast", "hills", "estate", "kwidow" };

		std::vector<Reason> reasons;  // loaded once at kDataLoaded, read-only afterwards

		std::mutex                                     noteLock;  // Papyrus writes (VM thread), the Couples page reads (render thread)
		std::unordered_map<std::uint64_t, std::string> notes;

		std::uint64_t Key(RE::FormID a_a, RE::FormID a_b)
		{
			const auto lo = std::min(a_a, a_b);
			const auto hi = std::max(a_a, a_b);
			return (static_cast<std::uint64_t>(hi) << 32) | lo;
		}

		std::vector<std::string> Strings(const nlohmann::json& a_j, const char* a_key)
		{
			std::vector<std::string> out;
			if (a_j.contains(a_key) && a_j[a_key].is_array()) {
				for (const auto& s : a_j[a_key]) {
					if (s.is_string()) {
						out.push_back(s.get<std::string>());
					}
				}
			}
			return out;
		}

		bool ContainsNoCase(std::string_view a_hay, std::string_view a_needle)
		{
			const auto it = std::search(a_hay.begin(), a_hay.end(), a_needle.begin(), a_needle.end(),
				[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
			return !a_needle.empty() && it != a_hay.end();
		}

		// what the filters look at for one parent: race EditorID, home location names and keywords (home and every location above it)
		struct Facts
		{
			std::string              race;
			std::vector<std::string> places;
			std::vector<std::string> keywords;
			int                      socialClass{ -1 };
		};

		Facts FactsOf(RE::Actor* a_actor)
		{
			Facts f;
			if (!a_actor) {
				return f;
			}
			if (auto* race = a_actor->GetRace()) {
				f.race = race->GetFormEditorID() ? race->GetFormEditorID() : "";
			}
			int depth = 0;
			for (auto* loc = a_actor->GetEditorLocation(); loc && depth < 8; loc = loc->parentLoc, ++depth) {
				if (loc->GetName() && *loc->GetName()) {
					f.places.emplace_back(loc->GetName());
				}
				loc->ForEachKeyword([&](const RE::BGSKeyword* a_kw) {
					if (a_kw && a_kw->GetFormEditorID()) {
						f.keywords.emplace_back(a_kw->GetFormEditorID());
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return f;
		}

		bool AnyMatch(const std::vector<std::string>& a_values, const std::vector<std::string>& a_words, bool a_exact)
		{
			for (const auto& v : a_values) {
				for (const auto& w : a_words) {
					if (a_exact ? _stricmp(v.c_str(), w.c_str()) == 0 : ContainsNoCase(v, w)) {
						return true;
					}
				}
			}
			return false;
		}

		bool Fits(const Reason& a_r, const std::array<Facts, 2>& a_parents)
		{
			auto any = [&](auto&& a_test) { return a_test(a_parents[0]) || a_test(a_parents[1]); };
			if (!a_r.races.empty() && !any([&](const Facts& f) { return AnyMatch({ f.race }, a_r.races, false); })) {
				return false;
			}
			if (!a_r.home.empty() && !any([&](const Facts& f) { return AnyMatch(f.keywords, a_r.home, true); })) {
				return false;
			}
			if (!a_r.notHome.empty() && any([&](const Facts& f) { return AnyMatch(f.keywords, a_r.notHome, true); })) {
				return false;
			}
			if (!a_r.nearby.empty() && !any([&](const Facts& f) { return AnyMatch(f.places, a_r.nearby, false); })) {
				return false;
			}
			if (!a_r.notNearby.empty() && any([&](const Facts& f) { return AnyMatch(f.places, a_r.notNearby, false); })) {
				return false;
			}
			if (!a_r.classes.empty() && !any([&](const Facts& f) { return std::ranges::find(a_r.classes, f.socialClass) != a_r.classes.end(); })) {
				return false;
			}
			return true;
		}
	}

	void LoadReasons()
	{
		reasons.clear();
		std::error_code ec;
		const auto      path = std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OStimNPCsReforged_ChildAway.json";
		std::ifstream   in(path);
		if (!in) {
			SKSE::log::warn("Baby stages: {} not found - away children get no place", path.string());
			return;
		}
		try {
			const auto j = nlohmann::json::parse(in);
			for (const auto& jr : j.at("reasons")) {
				Reason r;
				r.place = jr.value("place", std::string{});
				r.races = Strings(jr, "races");
				r.home = Strings(jr, "home");
				r.notHome = Strings(jr, "notHome");
				r.nearby = Strings(jr, "near");
				r.notNearby = Strings(jr, "notNear");
				if (jr.contains("class") && jr["class"].is_array()) {
					for (const auto& c : jr["class"]) {
						if (c.is_number_integer()) {
							r.classes.push_back(c.get<int>());
						}
					}
				}
				const auto id = jr.value("id", std::string{});
				r.id = id;
				const auto slot = reasons.size();
				if (slot < kExpectedIds.size() && id != kExpectedIds[slot]) {
					SKSE::log::warn("Baby stages: place {} is \"{}\" but the dialogue expects \"{}\" there - parents will answer for the wrong place",
						slot + 1, id, kExpectedIds[slot]);
				}
				reasons.push_back(std::move(r));
			}
		} catch (const std::exception& e) {
			SKSE::log::error("Baby stages: {} could not be read ({})", path.string(), e.what());
			reasons.clear();
		}
		SKSE::log::info("Baby stages: {} reason(s) loaded", reasons.size());
		if (!reasons.empty() && reasons.size() != kExpectedIds.size()) {
			SKSE::log::warn("Baby stages: the dialogue has {} places, the JSON {} - extra places get no line, missing ones are never picked",
				kExpectedIds.size(), reasons.size());
		}
	}

	std::string ReasonId(int a_index)
	{
		return a_index >= 0 && a_index < ReasonCount() ? reasons[a_index].id : std::string{};
	}

	std::string PlaceById(const std::string& a_id)
	{
		for (const auto& r : reasons) {
			if (!a_id.empty() && r.id == a_id) {
				return r.place;
			}
		}
		return {};
	}

	int DialogueRank(const std::string& a_id)
	{
		for (std::size_t i = 0; i < kExpectedIds.size(); ++i) {
			if (!a_id.empty() && kExpectedIds[i] == a_id) {
				return static_cast<int>(i) + 1;
			}
		}
		return 0;
	}

	int ReasonCount()
	{
		return static_cast<int>(reasons.size());
	}

	std::string ReasonPlace(int a_index)
	{
		return a_index >= 0 && a_index < ReasonCount() ? reasons[a_index].place : std::string{};
	}

	int PickReason(RE::Actor* a_mother, RE::Actor* a_father)
	{
		return PickReason(a_mother, a_father, -1, -1);
	}

	int PickReason(RE::Actor* a_mother, RE::Actor* a_father, int a_classMother, int a_classFather)
	{
		if (reasons.empty()) {
			return -1;
		}
		std::array<Facts, 2> parents{ FactsOf(a_mother), FactsOf(a_father) };
		parents[0].socialClass = a_classMother;
		parents[1].socialClass = a_classFather;
		std::vector<int>           fits;
		for (int i = 0; i < ReasonCount(); ++i) {
			if (Fits(reasons[i], parents)) {
				fits.push_back(i);
			}
		}
		static std::mt19937 rng{ std::random_device{}() };
		if (fits.empty()) {
			return std::uniform_int_distribution<int>(0, ReasonCount() - 1)(rng);  // nothing fits (a broken JSON filter): any place
		}
		const int pick = fits[std::uniform_int_distribution<std::size_t>(0, fits.size() - 1)(rng)];
		SKSE::log::info("Baby stages: {} of {} place(s) fit {} + {} - picked \"{}\"", fits.size(), reasons.size(),
			a_mother ? a_mother->GetDisplayFullName() : "?", a_father ? a_father->GetDisplayFullName() : "?", reasons[pick].place);
		return pick;
	}

	void ClearNotes()
	{
		std::scoped_lock l(noteLock);
		notes.clear();
	}

	void AddNote(RE::FormID a_baseA, RE::FormID a_baseB, const std::string& a_text)
	{
		if (!a_baseA || !a_baseB || a_text.empty()) {
			return;
		}
		std::scoped_lock l(noteLock);
		auto&            n = notes[Key(a_baseA, a_baseB)];
		n += n.empty() ? a_text : "; " + a_text;
	}

	std::string Note(RE::FormID a_baseA, RE::FormID a_baseB)
	{
		std::scoped_lock l(noteLock);
		const auto       it = notes.find(Key(a_baseA, a_baseB));
		return it != notes.end() ? it->second : std::string{};
	}
}
