#pragma once

// Everyday couple conversations (0.13.0): built from reviewed parts - a greeting, one or two topics, a parting - read from
// SKSE\Plugins\OStimNPCsReforged_Conversations.json. The planner checks the couple and the moment (stage, time, place,
// weather, illness, a drink in the pack...) and which lines each partner's voice can say, and hands Papyrus the order.
namespace ONPCR::Conversation
{
	enum Op : std::int32_t
	{
		kSay = 0,   // role says the topic
		kLook = 1,  // role looks around, says nothing
		kHand = 2   // role hands a drink to the other one (who drinks it before the next line)
	};

	void Load();  // at kDataLoaded

	// a = the one who starts, b = the partner. Returns op, role (0 = a, 1 = b), topic FormID triples - empty when nothing fits.
	[[nodiscard]] std::vector<std::int32_t> Plan(RE::Actor* a_a, RE::Actor* a_b);
	// one named part (a moment: "m5" at the temple, "m1" just married) for these two - same triples, no context check
	[[nodiscard]] std::vector<std::int32_t> PlanPart(RE::Actor* a_a, RE::Actor* a_b, const std::string& a_id);
	[[nodiscard]] std::size_t PartCount();

	// 0.23.0 family chats - blood kin by the game's relationship records (never Spouse / Courting / in-laws)
	enum class Kin : std::int32_t
	{
		kNone = 0,
		kParent,       // a is b's parent
		kChild,        // a is b's child
		kSibling,
		kGrandparent,  // a is b's (great-)grandparent
		kGrandchild,
		kOther         // cousins, aunt / uncle
	};
	[[nodiscard]] Kin KinOf(RE::Actor* a_a, RE::Actor* a_b);
	// one family part for two relatives (either may open): same triples as Plan, empty when nothing fits. a_rel 1 = Papyrus
	// vouches a is b's parent (Beeing Female children carry no relationship record); records win when they exist.
	[[nodiscard]] std::vector<std::int32_t> PlanFamily(RE::Actor* a_a, RE::Actor* a_b, std::int32_t a_rel);
	// loaded relatives in the player's cell within a_maxDist: refA, refB, code triples (1 parent -> child, 2 siblings,
	// 3 grandparent -> grandchild, 4 other kin); A is the parent / grandparent / aunt side. Never the player, a follower or a
	// reserved custom follower.
	[[nodiscard]] std::vector<std::int32_t> FamilyPairs(float a_maxDist);
}
