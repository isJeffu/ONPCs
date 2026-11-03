#include "Conversation.h"

#include "Ladder.h"

#include <nlohmann/json.hpp>

#include <set>
#include <sstream>

namespace ONPCR::Conversation
{
	namespace
	{
		struct Line
		{
			RE::FormID  topic{ 0 };  // 0 = an action (no words)
			std::string time, place, stage, c;
		};
		struct Step
		{
			int               role{ 0 };  // 0 the one who starts, 1 the partner
			std::string       label;
			bool              optional{ false }, alt{ false }, branch{ false };
			std::vector<Line> lines;
		};
		struct Part
		{
			std::string              id, kind;
			std::vector<std::string> stage, time, place, weather, flags;
			std::vector<std::string> rel;  // 0.23.0 family parts: who A is to B (parent-child, child-parent, to-child, kids, adults)
			std::vector<Step>        steps;
		};
		std::vector<Part> parts;

		// parts that belong to their own events, not everyday chat
		bool EventOnly(const Part& a_p)
		{
			static const std::unordered_set<std::string> ids{ "t51", "t52", "t60", "t61", "t62" };
			return ids.contains(a_p.id) || a_p.kind == "moment" || a_p.kind == "solo" || a_p.kind == "friend" || a_p.kind == "family";
		}

		std::vector<std::string> StrList(const nlohmann::json& a_j, const char* a_key)
		{
			std::vector<std::string> out;
			if (a_j.contains(a_key) && a_j[a_key].is_array()) {
				for (const auto& v : a_j[a_key]) {
					if (v.is_string()) {
						out.push_back(v.get<std::string>());
					}
				}
			}
			return out;
		}
		std::string Str(const nlohmann::json& a_j, const char* a_key)
		{
			return a_j.contains(a_key) && a_j[a_key].is_string() ? a_j[a_key].get<std::string>() : std::string();
		}
		bool In(const std::vector<std::string>& a_list, const std::string& a_v)
		{
			return std::find(a_list.begin(), a_list.end(), a_v) != a_list.end();
		}
		bool InCsv(const std::string& a_csv, const std::string& a_v)
		{
			if (a_csv.empty()) {
				return true;
			}
			std::stringstream ss(a_csv);
			std::string       item;
			while (std::getline(ss, item, ',')) {
				item.erase(0, item.find_first_not_of(' '));
				if (item == a_v) {
					return true;
				}
			}
			return false;
		}
		RE::FormID ResolveId(const std::string& a_id)
		{
			const auto colon = a_id.find(':');
			if (colon == std::string::npos) {
				return 0;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			const auto local = static_cast<RE::FormID>(std::stoul(a_id.substr(0, colon), nullptr, 16));
			return dh ? dh->LookupFormID(local, a_id.substr(colon + 1)) : 0;
		}

		// the moment
		struct Context
		{
			std::string           stage, time, weather;
			std::set<std::string> places, flags;
			bool                  aMale{ false }, bMale{ false };
			bool                  bHasDrink{ false };
			bool                  together{ false };
		};

		bool HasDrink(RE::Actor* a_x)
		{
			static auto* list = RE::TESForm::LookupByID<RE::BGSListForm>(0x00072EA2);  // AlcoholicDrinksList
			if (!a_x || !list) {
				return false;
			}
			for (const auto& [item, data] : a_x->GetInventory()) {
				if (item && data.first > 0 && list->HasForm(item)) {
					return true;
				}
			}
			return false;
		}
		bool IsIll(RE::Actor* a_x)
		{
			// 0.17.1: through AsMagicTarget() - the MagicTarget base sits at 0xA0 on 1.6.629+ (0x98 on SE); calling it straight on Actor
			// used the SE offset and crashed on 1.6.1170 (null vfunc, Maramal + Dinya Balu, 2026-10-16)
			auto* mt = a_x ? a_x->AsMagicTarget() : nullptr;
			auto* effects = mt ? mt->GetActiveEffectList() : nullptr;
			if (!effects) {
				return false;
			}
			for (auto* e : *effects) {
				auto* mgef = e ? e->GetBaseObject() : nullptr;
				if (mgef && !e->flags.any(RE::ActiveEffect::Flag::kInactive) && mgef->HasKeywordString("MagicDisease")) {
					return true;
				}
			}
			return false;
		}
		bool IsHurt(RE::Actor* a_x)
		{
			auto* av = a_x ? a_x->AsActorValueOwner() : nullptr;
			if (!av) {
				return false;
			}
			const float max = av->GetPermanentActorValue(RE::ActorValue::kHealth);
			return max > 0.0f && av->GetActorValue(RE::ActorValue::kHealth) / max < 0.6f;
		}
		bool LocHas(RE::BGSLocation* a_loc, const char* a_kw)
		{
			for (auto* l = a_loc; l; l = l->parentLoc) {
				if (l->HasKeywordString(a_kw)) {
					return true;
				}
			}
			return false;
		}

		// the interior belongs to this person: owned by them, or by a faction they are in (Maramal and Dinya live in the Temple of
		// Mara, which is a town building, not a house)
		bool OwnsCell(RE::Actor* a_actor, RE::TESObjectCELL* a_cell)
		{
			auto* owner = a_cell ? a_cell->GetOwner() : nullptr;
			if (!owner || !a_actor) {
				return false;
			}
			if (auto* npc = owner->As<RE::TESNPC>()) {
				return a_actor->GetActorBase() == npc;
			}
			if (auto* faction = owner->As<RE::TESFaction>()) {
				return a_actor->IsInFaction(faction);
			}
			return false;
		}

		void ReadMoment(RE::Actor* a_a, RE::Actor* a_b, RE::FormID a_home, Context& c);

		Context Read(RE::Actor* a_a, RE::Actor* a_b)
		{
			Context c;
			const auto st = Ladder::GetStage(a_a, a_b);
			bool       cold = false;
			RE::FormID home = 0;
			{
				const RE::FormID ba = a_a->GetActorBase()->GetFormID(), bb = a_b->GetActorBase()->GetFormID();
				for (const auto& p : Ladder::Snapshot()) {
					if ((p.a == ba && p.b == bb) || (p.a == bb && p.b == ba)) {
						cold = st == Ladder::Stage::Married && p.cold;
						home = p.home;
						break;
					}
				}
			}
			switch (st) {
			case Ladder::Stage::Casual: c.stage = "casual"; break;
			case Ladder::Stage::Candidate: c.stage = "dating"; break;
			case Ladder::Stage::Engaged: c.stage = "engaged"; break;
			case Ladder::Stage::Married: c.stage = cold ? "cold" : "married"; break;
			case Ladder::Stage::Jilted:
			case Ladder::Stage::Divorced: c.stage = "ex"; break;
			default: c.stage = ""; break;
			}
			ReadMoment(a_a, a_b, home, c);
			if (IsIll(a_b)) {
				c.flags.insert("ill");
			}
			if (IsHurt(a_b)) {
				c.flags.insert("injured");
			}
			if (Ladder::IsPregnant(a_b->GetActorBase()->GetFormID())) {
				c.flags.insert("pregnant");
			}
			static auto* merchant = RE::TESForm::LookupByID<RE::TESFaction>(0x00051596);  // JobMerchantFaction
			if (merchant && (a_a->IsInFaction(merchant) || a_b->IsInFaction(merchant))) {
				c.flags.insert("merchant");
			}
			if (HasDrink(a_a)) {
				c.flags.insert("has drink");
			}
			c.bHasDrink = HasDrink(a_b);
			if (a_a->IsPlayerTeammate()) {
				c.flags.insert("traveller");
			}
			c.together = a_a->GetPosition().GetDistance(a_b->GetPosition()) < 300.0f;
			return c;
		}

		// time of day, place, weather and the two sexes - all a family chat reads (0.23.0: split out of Read, no couple data)
		void ReadMoment(RE::Actor* a_a, RE::Actor* a_b, RE::FormID a_home, Context& c)
		{
			const RE::FormID home = a_home;
			auto*       cal = RE::Calendar::GetSingleton();
			const float hour = cal ? cal->GetHour() : 12.0f;
			c.time = hour >= 5.0f && hour < 12.0f ? "morning" : hour < 17.0f && hour >= 12.0f ? "afternoon" : hour >= 17.0f && hour < 22.0f ? "evening" : "night";

			auto* cell = a_a->GetParentCell();
			auto* loc = a_a->GetCurrentLocation();
			const bool interior = cell && cell->IsInteriorCell();
			const bool inTown = LocHas(loc, "LocTypeCity") || LocHas(loc, "LocTypeTown") || LocHas(loc, "LocTypeSettlement");
			bool atOwnHome = false;  // inside a place that is theirs: the couple's home, or a building one of them owns or lives in
			if (interior) {
				for (auto* l = loc; l && home && !atOwnHome; l = l->parentLoc) {
					atOwnHome = l->GetFormID() == home;
				}
				atOwnHome = atOwnHome || OwnsCell(a_a, cell) || OwnsCell(a_b, cell);
			}
			if (!interior) {
				c.places.insert("outdoors");
				if (inTown) {
					c.places.insert("town");
				}
			} else if (atOwnHome) {
				c.places.insert("home");  // never "town" or "inn" here, so "see you at home" lines do not play at home
			} else if (loc && (loc->HasKeywordString("LocTypeInn"))) {
				c.places.insert("inn");
			} else if (loc && (loc->HasKeywordString("LocTypeDwelling") || loc->HasKeywordString("LocTypeHouse") || loc->HasKeywordString("LocTypePlayerHouse"))) {
				c.places.insert("home");
			} else if (inTown) {
				c.places.insert("town");
			}
			if (!interior) {
				auto* sky = RE::Sky::GetSingleton();
				auto* w = sky ? sky->currentWeather : nullptr;
				if (w) {
					const auto f = w->data.flags;
					if (f.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
						c.weather = "rain";
					} else if (f.any(RE::TESWeather::WeatherDataFlag::kSnow) || f.any(RE::TESWeather::WeatherDataFlag::kCloudy)) {
						c.weather = "cold";
					} else if (f.any(RE::TESWeather::WeatherDataFlag::kPleasant)) {
						c.weather = "clear";
					}
				}
			}
			c.aMale = a_a->GetActorBase()->GetSex() == RE::SEX::kMale;
			c.bMale = a_b->GetActorBase()->GetSex() == RE::SEX::kMale;
		}

		bool PartFits(const Part& a_p, const Context& a_c)
		{
			if (EventOnly(a_p) || a_c.stage.empty()) {
				return false;
			}
			if (!a_p.stage.empty() && !In(a_p.stage, a_c.stage)) {
				return false;
			}
			if (a_p.stage.empty() && a_p.kind == "topic" && a_c.stage == "ex") {
				return false;
			}
			if (!a_p.time.empty() && !In(a_p.time, a_c.time)) {
				return false;
			}
			if (!a_p.place.empty() && std::none_of(a_p.place.begin(), a_p.place.end(), [&](const std::string& s) { return a_c.places.contains(s); })) {
				return false;
			}
			if (!a_p.weather.empty() && !In(a_p.weather, a_c.weather)) {
				return false;
			}
			for (const auto& f : a_p.flags) {
				if (f == "brute voice" || f == "old") {
					continue;  // the line itself is limited to those voices
				}
				if (!a_c.flags.contains(f)) {
					return false;
				}
			}
			if (a_p.id == "t12" && !a_c.together) {
				return false;  // nobody walks over for "did you hear that?"
			}
			return true;
		}

		bool CanSay(RE::Actor* a_speaker, RE::Actor* a_target, RE::FormID a_topic)
		{
			auto* topic = RE::TESForm::LookupByID<RE::TESTopic>(a_topic);
			if (!topic || !topic->topicInfos) {
				return false;
			}
			for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
				auto* info = topic->topicInfos[i];
				if (info && info->objConditions.IsTrue(a_speaker, a_target)) {
					return true;
				}
			}
			return false;
		}

		// 0.16.0 friend chats: "own:married,wife,warm" = the SPEAKER's own couple (not the pair talking): married / engaged / dating,
		// warm (not a cold marriage) / cold, wife / husband (the partner's sex) - every token must hold
		bool OwnFits(const std::string& a_c, RE::Actor* a_speaker)
		{
			auto* base = a_speaker ? a_speaker->GetActorBase() : nullptr;
			if (!base) {
				return false;
			}
			Ladder::Stage st = Ladder::Stage::None;
			std::uint8_t  strikes = 0;
			bool          cold = false;
			const auto    partner = Ladder::CommittedPartnerOf(base->GetFormID(), &st, &strikes, &cold);
			if (!partner) {
				return false;
			}
			auto*       pnpc = RE::TESForm::LookupByID<RE::TESNPC>(partner);
			std::string rest = a_c.substr(4);
			std::size_t pos = 0;
			while (pos <= rest.size()) {
				const auto  comma = rest.find(',', pos);
				std::string tok = rest.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
				tok.erase(0, tok.find_first_not_of(' '));
				tok.erase(tok.find_last_not_of(' ') + 1);
				bool ok = true;
				if (tok == "married") {
					ok = st == Ladder::Stage::Married;
				} else if (tok == "engaged") {
					ok = st == Ladder::Stage::Engaged;
				} else if (tok == "dating") {
					ok = st == Ladder::Stage::Candidate;
				} else if (tok == "warm") {
					ok = !cold;
				} else if (tok == "cold") {
					ok = cold;
				} else if (tok == "wife") {
					ok = pnpc && pnpc->IsFemale();
				} else if (tok == "husband") {
					ok = pnpc && !pnpc->IsFemale();
				}
				if (!ok) {
					return false;
				}
				if (comma == std::string::npos) {
					break;
				}
				pos = comma + 1;
			}
			return true;
		}

		bool LineFits(const Line& a_l, int a_role, const Context& a_c, RE::Actor* a_speaker, RE::Actor* a_other)
		{
			if (a_l.c.rfind("own:", 0) == 0 && !OwnFits(a_l.c, a_speaker)) {
				return false;
			}
			if (!InCsv(a_l.time, a_c.time)) {
				return false;
			}
			if (!a_l.place.empty() && std::none_of(a_c.places.begin(), a_c.places.end(), [&](const std::string& s) { return InCsv(a_l.place, s); })) {
				return false;
			}
			if (!InCsv(a_l.stage, a_c.stage)) {
				return false;
			}
			const bool otherMale = a_role == 0 ? a_c.bMale : a_c.aMale;
			if (a_l.c == "to a man" && !otherMale) {
				return false;
			}
			if (a_l.c == "to a woman" && otherMale) {
				return false;
			}
			if (a_l.c == "to a husband" && (!otherMale || a_c.stage != "married")) {
				return false;
			}
			if (a_l.c == "cold couples" && a_c.stage != "cold") {
				return false;
			}
			return a_l.topic == 0 || CanSay(a_speaker, a_other, a_l.topic);
		}

		std::mt19937& Rng()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}
		bool Chance(float a_p) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng()) < a_p; }

		// part ids each couple used in its last chats (pair of base FormIDs -> ids, newest last); not saved - a fresh session may repeat once.
		// Plan() is a Papyrus native, so two scripts can call it at once: every access goes through RecentLock().
		std::unordered_map<std::uint64_t, std::vector<std::string>>& RecentTopics()
		{
			static std::unordered_map<std::uint64_t, std::vector<std::string>> recent;
			return recent;
		}
		std::mutex& RecentLock()
		{
			static std::mutex m;
			return m;
		}

		// one part played out as op/role/topic triples; false when a required step has no line this couple can say
		bool Play(const Part& a_p, const Context& a_c, RE::Actor* a_a, RE::Actor* a_b, std::vector<std::int32_t>& a_out)
		{
			std::vector<const Step*> steps;
			const auto               first = std::find_if(a_p.steps.begin(), a_p.steps.end(), [](const Step& s) { return s.branch; });
			if (first == a_p.steps.end()) {
				for (const auto& s : a_p.steps) {
					steps.push_back(&s);
				}
			} else {
				// the drink favour: B's pack decides; a drink offer is accepted 3 times in 4
				const bool main = a_p.id == "t25" ? a_c.bHasDrink : Chance(0.75f);
				for (auto it = a_p.steps.begin(); it != first; ++it) {
					steps.push_back(&*it);
				}
				for (auto it = first; it != a_p.steps.end(); ++it) {
					if (it->alt != main) {
						steps.push_back(&*it);
					}
				}
			}
			std::vector<std::int32_t> out;
			for (const auto* s : steps) {
				if (s->optional && Chance(0.35f)) {
					continue;
				}
				RE::Actor*         speaker = s->role == 0 ? a_a : a_b;
				RE::Actor*         other = s->role == 0 ? a_b : a_a;
				std::vector<const Line*> ok;
				for (const auto& l : s->lines) {
					if (LineFits(l, s->role, a_c, speaker, other)) {
						ok.push_back(&l);
					}
				}
				if (ok.empty()) {
					if (s->optional) {
						continue;
					}
					return false;
				}
				const Line* l = ok[std::uniform_int_distribution<std::size_t>(0, ok.size() - 1)(Rng())];
				if (s->label.rfind("drinks it", 0) == 0 || s->label.rfind("after a drink", 0) == 0) {
					out.insert(out.end(), { kHand, 1 - s->role, 0 });  // the other one hands the drink over first
				}
				if (l->topic == 0) {
					out.insert(out.end(), { kLook, s->role, 0 });
				} else {
					out.insert(out.end(), { kSay, s->role, static_cast<std::int32_t>(l->topic) });
				}
			}
			if (out.empty()) {
				return false;
			}
			a_out.insert(a_out.end(), out.begin(), out.end());
			return true;
		}

		bool Pick(const std::string& a_kind, const Context& a_c, RE::Actor* a_a, RE::Actor* a_b, std::vector<std::string>& a_used,
			std::vector<std::int32_t>& a_out, const std::vector<std::string>& a_avoid = {})
		{
			std::vector<const Part*> cands;
			for (const auto& p : parts) {
				if (p.kind == a_kind && !In(a_used, p.id) && !In(a_avoid, p.id) && PartFits(p, a_c)) {
					cands.push_back(&p);
				}
			}
			std::shuffle(cands.begin(), cands.end(), Rng());
			for (const auto* p : cands) {
				if (Play(*p, a_c, a_a, a_b, a_out)) {
					a_used.push_back(p->id);
					return true;
				}
			}
			return false;
		}

		// ---- 0.23.0 family chats ----
		// blood kin only (the game's association types): never Spouse / Courting, never in-laws
		constexpr RE::FormID kParentChild = 0x0142C6, kSiblings = 0x0142C5, kGrandparent = 0x01E803, kGreatGrandparent = 0x01E90B,
							 kCousins = 0x0142C8, kAuntUncle = 0x0142C7, kGrandAuntUncle = 0x01E80B;

		// what a_a is to a_b by the relationship records of their bases (npc1 is the parent / grandparent / aunt side)
		Kin KinByRecords(RE::TESNPC* a_a, RE::TESNPC* a_b)
		{
			if (!a_a || !a_b || a_a == a_b || !a_a->relationships) {
				return Kin::kNone;
			}
			for (auto* rel : *a_a->relationships) {
				if (!rel || !rel->assocType || !((rel->npc1 == a_a && rel->npc2 == a_b) || (rel->npc1 == a_b && rel->npc2 == a_a))) {
					continue;
				}
				const bool aFirst = rel->npc1 == a_a;
				switch (rel->assocType->GetFormID()) {
				case kParentChild: return aFirst ? Kin::kParent : Kin::kChild;
				case kSiblings: return Kin::kSibling;
				case kGrandparent:
				case kGreatGrandparent: return aFirst ? Kin::kGrandparent : Kin::kGrandchild;
				case kCousins:
				case kAuntUncle:
				case kGrandAuntUncle: return Kin::kOther;
				default: break;
				}
			}
			return Kin::kNone;
		}
		Kin Flip(Kin a_k)
		{
			switch (a_k) {
			case Kin::kParent: return Kin::kChild;
			case Kin::kChild: return Kin::kParent;
			case Kin::kGrandparent: return Kin::kGrandchild;
			case Kin::kGrandchild: return Kin::kGrandparent;
			default: return a_k;
			}
		}
		const char* KinName(Kin a_k)
		{
			switch (a_k) {
			case Kin::kParent: return "parent";
			case Kin::kChild: return "child";
			case Kin::kSibling: return "siblings";
			case Kin::kGrandparent: return "grandparent";
			case Kin::kGrandchild: return "grandchild";
			case Kin::kOther: return "kin";
			default: return "none";
			}
		}
		// the "rel" tags a family part can ask for, for A = a_a speaking first to B = a_b
		std::set<std::string> RelTags(RE::Actor* a_a, RE::Actor* a_b, Kin a_k)
		{
			std::set<std::string> t;
			const bool aKid = a_a->IsChild(), bKid = a_b->IsChild();
			if (!aKid && !bKid) {
				t.insert("adults");
			} else if (aKid && bKid) {
				t.insert("kids");
			} else if (bKid) {
				t.insert("to-child");
				if (a_k == Kin::kParent) {
					t.insert("parent-child");
				}
			} else {
				t.insert("from-child");
				if (a_k == Kin::kChild) {
					t.insert("child-parent");
				}
			}
			return t;
		}
		bool FamilyFits(const Part& a_p, const Context& a_c, const std::set<std::string>& a_tags)
		{
			if (a_p.kind != "family" || std::none_of(a_p.rel.begin(), a_p.rel.end(), [&](const std::string& r) { return a_tags.contains(r); })) {
				return false;
			}
			if (!a_p.time.empty() && !In(a_p.time, a_c.time)) {
				return false;
			}
			if (!a_p.place.empty() && std::none_of(a_p.place.begin(), a_p.place.end(), [&](const std::string& s) { return a_c.places.contains(s); })) {
				return false;
			}
			return a_p.weather.empty() || In(a_p.weather, a_c.weather);
		}
	}

	Kin KinOf(RE::Actor* a_a, RE::Actor* a_b)
	{
		return a_a && a_b ? KinByRecords(a_a->GetActorBase(), a_b->GetActorBase()) : Kin::kNone;
	}

	void Load()
	{
		parts.clear();
		std::error_code ec;
		const auto      path = std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OStimNPCsReforged_Conversations.json";
		std::ifstream   in(path);
		if (!in) {
			SKSE::log::warn("Conversations: {} not found - couples use the old chat lines", path.string());
			return;
		}
		try {
			const auto j = nlohmann::json::parse(in);
			for (const auto& jp : j.at("parts")) {
				Part p;
				p.id = Str(jp, "id");
				p.kind = Str(jp, "kind");
				if (jp.contains("when") && jp["when"].is_object()) {
					const auto& w = jp["when"];
					p.stage = StrList(w, "stage");
					p.time = StrList(w, "time");
					p.place = StrList(w, "place");
					p.weather = StrList(w, "weather");
					p.flags = StrList(w, "flags");
					p.rel = StrList(w, "rel");
				}
				for (const auto& js : jp.at("steps")) {
					Step       s;
					const auto who = Str(js, "who");
					s.role = (who == "B" || who == "Visitor") ? 1 : 0;
					s.label = Str(js, "label");
					s.optional = js.value("optional", false);
					s.alt = js.value("alt", false);
					s.branch = js.value("branch", false);
					for (const auto& jl : js.at("lines")) {
						Line l;
						if (jl.is_object()) {
							if (jl.value("action", false)) {
								l.topic = 0;
							} else {
								l.topic = ResolveId(Str(jl, "formid"));
								if (!l.topic) {
									continue;  // a line with no voiced source was left out at build time
								}
							}
							l.time = Str(jl, "time");
							l.place = Str(jl, "place");
							l.stage = Str(jl, "stage");
							l.c = Str(jl, "c");
						}
						s.lines.push_back(std::move(l));
					}
					p.steps.push_back(std::move(s));
				}
				parts.push_back(std::move(p));
			}
			SKSE::log::info("Conversations: {} part(s) loaded", parts.size());
		} catch (const std::exception& e) {
			parts.clear();
			SKSE::log::error("Conversations: could not read {} ({})", path.string(), e.what());
		}
	}

	std::size_t PartCount() { return parts.size(); }

	std::vector<std::int32_t> PlanPart(RE::Actor* a_a, RE::Actor* a_b, const std::string& a_id)
	{
		std::vector<std::int32_t> out;
		if (!a_a || !a_b || !a_a->GetActorBase() || !a_b->GetActorBase()) {
			return out;
		}
		const Context c = Read(a_a, a_b);
		for (const auto& p : parts) {
			if (p.id == a_id) {
				// try both ways round: whoever has the opening line in their voice starts
				if (!Play(p, c, a_a, a_b, out)) {
					std::vector<std::int32_t> swapped;
					if (Play(p, c, a_b, a_a, swapped)) {
						for (std::size_t i = 1; i < swapped.size(); i += 3) {
							swapped[i] = 1 - swapped[i];
						}
						out = std::move(swapped);
					}
				}
				break;
			}
		}
		return out;
	}

	std::vector<std::int32_t> Plan(RE::Actor* a_a, RE::Actor* a_b)
	{
		std::vector<std::int32_t> out;
		if (!a_a || !a_b || parts.empty() || !a_a->GetActorBase() || !a_b->GetActorBase()) {
			return out;
		}
		// 0.23.0 guard rail: relatives never get couple lines, whatever put them into a couple chat
		if (const auto k = KinOf(a_a, a_b); k != Kin::kNone) {
			SKSE::log::warn("Conversations: {} + {} are family ({}) - no couple chat", a_a->GetDisplayFullName(), a_b->GetDisplayFullName(), KinName(k));
			return out;
		}
		const Context            c = Read(a_a, a_b);
		std::vector<std::string> used;
		std::vector<std::int32_t> greet, topics, parting;
		// 0.18.0: the same couple does not repeat the topics of its last two chats (Maramal + Dinya Balu ran the same one 3 times
		// in a row in the temple); when nothing else fits they may repeat rather than not talk at all
		const auto ka = a_a->GetActorBase()->GetFormID();
		const auto kb = a_b->GetActorBase()->GetFormID();
		const auto key = (static_cast<std::uint64_t>(std::min(ka, kb)) << 32) | std::max(ka, kb);
		std::vector<std::string> recent;
		{
			std::scoped_lock lock(RecentLock());
			recent = RecentTopics()[key];  // a copy: the picks below run without holding the lock
		}
		Pick("greeting", c, a_a, a_b, used, greet, recent);
		if (greet.empty()) {
			Pick("greeting", c, a_a, a_b, used, greet);
		}
		if (!Pick("topic", c, a_a, a_b, used, topics, recent) && !Pick("topic", c, a_a, a_b, used, topics)) {
			return out;  // no topic fits this couple right now - no chat
		}
		if (Chance(0.55f)) {
			Pick("topic", c, a_a, a_b, used, topics, recent);
		}
		Pick("parting", c, a_a, a_b, used, parting);
		{
			std::scoped_lock lock(RecentLock());
			auto& kept = RecentTopics()[key];
			kept.insert(kept.end(), used.begin(), used.end());
			if (kept.size() > 8) {
				kept.erase(kept.begin(), kept.end() - 8);
			}
		}
		out.insert(out.end(), greet.begin(), greet.end());
		out.insert(out.end(), topics.begin(), topics.end());
		out.insert(out.end(), parting.begin(), parting.end());
		std::string ids;
		for (const auto& u : used) {
			ids += u + " ";
		}
		SKSE::log::info("Conversations: {} + {} ({}; {}) -> {}", a_a->GetDisplayFullName(), a_b->GetDisplayFullName(), c.stage, c.time, ids);
		return out;
	}

	std::vector<std::int32_t> PlanFamily(RE::Actor* a_a, RE::Actor* a_b, std::int32_t a_rel)
	{
		std::vector<std::int32_t> out;
		if (!a_a || !a_b || a_a == a_b || parts.empty() || !a_a->GetActorBase() || !a_b->GetActorBase()) {
			return out;
		}
		// the Beeing Female route (rel 1) skips FamilyPairs' filter: a follower parent must not be walked away from you
		for (auto* x : { a_a, a_b }) {
			if (x->IsPlayerRef() || x->IsPlayerTeammate() || Ladder::IsReservedFollower(x)) {
				return out;
			}
		}
		// guard rail: two people with a couple stage between them never get a family chat (impossible by the import rules)
		if (Ladder::GetStage(a_a, a_b) != Ladder::Stage::None) {
			SKSE::log::warn("Conversations: {} + {} have a couple stage - no family chat", a_a->GetDisplayFullName(), a_b->GetDisplayFullName());
			return out;
		}
		Kin k = KinOf(a_a, a_b);
		if (k == Kin::kNone && a_rel == 1) {
			k = Kin::kParent;  // a Beeing Female child of the household: no record, Papyrus vouches for it
		}
		if (k == Kin::kNone) {
			return out;
		}
		Context ca, cb;
		ReadMoment(a_a, a_b, 0, ca);
		ReadMoment(a_b, a_a, 0, cb);
		const auto tagsA = RelTags(a_a, a_b, k);
		const auto tagsB = RelTags(a_b, a_a, Flip(k));

		const auto ka = a_a->GetActorBase()->GetFormID();
		const auto kb = a_b->GetActorBase()->GetFormID();
		const auto key = (static_cast<std::uint64_t>(std::min(ka, kb)) << 32) | std::max(ka, kb);
		std::vector<std::string> recent;
		{
			std::scoped_lock lock(RecentLock());
			recent = RecentTopics()[key];
		}
		// (part, b speaks first) - either one may open, whoever the part's A role fits
		std::vector<std::pair<const Part*, bool>> cands, repeats;
		for (const auto& p : parts) {
			for (const bool swapped : { false, true }) {
				if (FamilyFits(p, swapped ? cb : ca, swapped ? tagsB : tagsA)) {
					(In(recent, p.id) ? repeats : cands).emplace_back(&p, swapped);
				}
			}
		}
		std::shuffle(cands.begin(), cands.end(), Rng());
		std::shuffle(repeats.begin(), repeats.end(), Rng());
		cands.insert(cands.end(), repeats.begin(), repeats.end());  // same as couples: repeat rather than say nothing
		for (const auto& [p, swapped] : cands) {
			std::vector<std::int32_t> plan;
			if (!(swapped ? Play(*p, cb, a_b, a_a, plan) : Play(*p, ca, a_a, a_b, plan))) {
				continue;
			}
			if (swapped) {
				for (std::size_t i = 1; i < plan.size(); i += 3) {
					plan[i] = 1 - plan[i];
				}
			}
			{
				std::scoped_lock lock(RecentLock());
				auto& kept = RecentTopics()[key];
				kept.push_back(p->id);
				if (kept.size() > 8) {
					kept.erase(kept.begin(), kept.end() - 8);
				}
			}
			SKSE::log::info("Conversations: family {} + {} ({}; {}) -> {}", swapped ? a_b->GetDisplayFullName() : a_a->GetDisplayFullName(),
				swapped ? a_a->GetDisplayFullName() : a_b->GetDisplayFullName(), KinName(swapped ? Flip(k) : k), ca.time, p->id);
			return plan;
		}
		return out;
	}

	std::vector<std::int32_t> FamilyPairs(float a_maxDist)
	{
		std::vector<std::int32_t> out;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* lists = RE::ProcessLists::GetSingleton();
		if (!player || !lists) {
			return out;
		}
		const auto  ppos = player->GetPosition();
		const float max2 = a_maxDist * a_maxDist;
		std::unordered_map<RE::TESNPC*, RE::Actor*> nearby;  // not "near": a Windows macro
		for (auto& h : lists->highActorHandles) {
			auto  actor = h.get();
			auto* base = actor ? actor->GetActorBase() : nullptr;
			if (!base || actor->IsPlayerRef() || actor->IsDead() || !actor->Is3DLoaded() || actor->IsPlayerTeammate() || Ladder::IsReservedFollower(actor.get()) ||
				!Ladder::IsPersonRace(actor->GetRace()) || actor->GetParentCell() != player->GetParentCell()) {
				continue;
			}
			const auto d = actor->GetPosition() - ppos;
			if (d.x * d.x + d.y * d.y + d.z * d.z <= max2) {
				nearby.emplace(base, actor.get());
			}
		}
		std::set<std::pair<RE::Actor*, RE::Actor*>> seen;
		for (const auto& [base, actor] : nearby) {
			if (!base->relationships) {
				continue;
			}
			for (auto* rel : *base->relationships) {
				if (!rel || !rel->npc1 || !rel->npc2) {
					continue;
				}
				auto it1 = nearby.find(rel->npc1);
				auto it2 = nearby.find(rel->npc2);
				if (it1 == nearby.end() || it2 == nearby.end() || it1->second == it2->second) {
					continue;
				}
				const Kin k = KinByRecords(rel->npc1, rel->npc2);  // npc1 first: parent / grandparent / aunt side opens
				if (k == Kin::kNone || !seen.emplace(std::min(it1->second, it2->second), std::max(it1->second, it2->second)).second) {
					continue;
				}
				const std::int32_t code = k == Kin::kParent ? 1 : k == Kin::kSibling ? 2 : k == Kin::kGrandparent ? 3 : 4;
				out.push_back(static_cast<std::int32_t>(it1->second->GetFormID()));
				out.push_back(static_cast<std::int32_t>(it2->second->GetFormID()));
				out.push_back(code);
			}
		}
		return out;
	}
}
