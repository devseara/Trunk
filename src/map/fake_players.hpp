// Copyright (c) Shakto Scripts - https://ronovelty.com/

#ifndef FAKEPLAYERS_HPP
#define FAKEPLAYERS_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <unordered_set>

#include "map.hpp"
#include "path.hpp"
#include "status.hpp"

#include <common/mmo.hpp>
#include <common/database.hpp>
#include <common/db.hpp>

struct mob_data;
struct flooritem_data;

extern std::atomic<uint32> g_fake_next_aid;
extern std::atomic<uint32> g_fake_next_cid;

#define old_training 1 // Set to 1 to use the pre re training - 0 to use the re training novice system 

// ---------------- Structures de données --------------------------------------

static inline bool fc_claimslot_active(const FCClaimSlot& s, uint64 now) {
	return s.bot_aid != 0 && s.until > now;
}

// Nettoie les slots expires
static inline void fc_claim_prune(FCClaimSlot slots[MAX_FP_ON_TARGET], uint64 now) {
	for (int i = 0; i < MAX_FP_ON_TARGET; ++i)
		if (!fc_claimslot_active(slots[i], now)) { slots[i].bot_aid = 0; slots[i].until = 0; }
}

// Retourne true si 'aid' occupe deja un slot
static inline bool fc_claim_is_holder(const FCClaimSlot slots[MAX_FP_ON_TARGET], int aid, uint64 now) {
	for (int i = 0; i < MAX_FP_ON_TARGET; ++i)
		if (slots[i].bot_aid == aid && fc_claimslot_active(slots[i], now))
			return true;
	return false;
}

// Compte les slots actifs
static inline int fc_claim_count(const FCClaimSlot slots[MAX_FP_ON_TARGET], uint64 now) {
	int c = 0; for (int i = 0; i < MAX_FP_ON_TARGET; ++i) if (fc_claimslot_active(slots[i], now)) ++c; return c;
}

// Essaye de prendre/raffraichir un slot (TTL en ms)
static inline bool fc_claim_try(FCClaimSlot slots[MAX_FP_ON_TARGET], uint32 aid, uint64 now, uint32 ttl_ms) {
	fc_claim_prune(slots, now);

	// Déjà détenteur ? -> refresh
	for (int i = 0; i < MAX_FP_ON_TARGET; ++i) {
		if (slots[i].bot_aid == aid) { slots[i].until = now + ttl_ms; return true; }
	}
	// Cherche slot libre
	for (int i = 0; i < MAX_FP_ON_TARGET; ++i) {
		if (slots[i].bot_aid == 0) { slots[i] = { aid, now + ttl_ms }; return true; }
	}
	return false; // plein (2/2)
}

// Libère explicitement (ex: bot annule l’action)
static inline void fc_claim_release(FCClaimSlot slots[MAX_FP_ON_TARGET], uint32 aid) {
	for (int i = 0; i < MAX_FP_ON_TARGET; ++i)
		if (slots[i].bot_aid == aid) { slots[i].bot_aid = 0; slots[i].until = 0; }
}

struct ReplyTemplateFC {
	std::string text;
	float weight = 1.0f;
};

// state -> (intent -> multiplier)
using IntentMultiplierMapFC = std::unordered_map<std::string, float>;
using StateBoostsMapFC = std::unordered_map<std::string, IntentMultiplierMapFC>;

struct FuzzyGlobalFC {
	bool   enabled = true;
	float  bonus = 0.8f;
	int    maxTokenLen = 3;
	std::unordered_set<std::string> excludeIntents;
	int    maxPerIntent = 1;
};

struct ChannelBiasFC { double pGlobal = 0.3, pPM = 0.7; };

struct ChatConfigFC {
	std::string language = "EN";
	double replyProbBase = 0.35;
	double replyProbMention = 0.85;
	int cooldownSelfMs = 4500;
	int cooldownPerPlayerMs = 6000;
	int maxTurnsSamePlayer = 3;
	int typingCpsMin = 8;
	int typingCpsMax = 16;
	double unknownFallbackWeight = 0.2;
	StateBoostsMapFC stateBoosts;
	FuzzyGlobalFC fuzzy;
	std::vector<std::string> tieBreakOrder;
	std::unordered_map<std::string, ChannelBiasFC> channelBias;
};

struct IntentDefFC {
	std::string name;
	float weight = 1.0f;
	std::vector<std::string> keywords;
	std::vector<ReplyTemplateFC> replies;

	int   minHits = 0;
	float minScore = 0.f;
	bool  allowFuzzy = true;
	float fuzzyBonus = -1.f;      // <0 => utilise global
	int   maxFuzzyTokenLen = -1;  // <0 => global
	int   maxFuzzyPerIntent = -1; // <0 => global
};

// Etapes Training Ground
enum class TGStep : uint8 {
	ARRIVAL = 0,   // init
	WAIT,          // town behavior until gm command
	GO_WARP,       // walk to warp 1
	NPC1_1,        // walk to npc 1 Sprakki
	NPC1_2,        // npc 1 lv up
	TP_NPC1,       // teleporting from npc 1 to next zone
	NPC2_0,        // walk to npc 2 Brade
	NPC2_1,        // talk to npc 2 Brade
	NPC2_2,        // npc 2 lv up
	NPC2_3,        // npc 2 equip stuffs
	NPC2_3b,       // npc 7 old sc walk
	NPC2_3c,       // npc 7 old sc talk
	NPC2_4,        // npc 2 lv up
	NPC3_1,        // walk to npc 3 Jinha
	NPC2_5,        // walk to npc 2 again
	NPC2_5b,       // talk to npc again
	TP_NPC2,       // teleporting from npc 2 to next zone
	NPC4_1,        // talk to chocolate
	NPC4_2,        // talk to others
	NPC4_3,        // talk to chocolat + lv up
	TP_NPC4,       // teleporting from npc 4 to next zone
	COMBAT1_1,     // zone combat 1
	COMBAT1_2,     // kill poring
	COMBAT1_2b,    // walk to npc 5
	COMBAT1_3,     // equip book + reset target
	COMBAT1_4,     // kill anything until jlv 10
	COMBAT1_5,     // walk again to NPC 5
	//COMBAT2,     // zone combat 2
	DONE,          // disconnect
	DONE_2         // disconnect
};

struct FPTrainingState {
	bool   active = false;
	TGStep step = TGStep::ARRIVAL;
	t_tick nextActionTick = 0;
	uint16 oldx, oldy;
};

struct RectZone {
	const char* map;
	uint16 x1, y1, x2, y2; // inclusif
};

struct RectCoords {
	const char* map;
	int x1, y1; // inclusif
};

static inline bool in_rect(uint16 x, uint16 y, const RectZone& r) {
	return (x >= r.x1 && x <= r.x2 && y >= r.y1 && y <= r.y2);
}

static inline void rand_point_in(const RectZone& r, uint16& rx, uint16& ry) {
	rx = r.x1 + (uint16)(rnd() % (r.x2 - r.x1 + 1));
	ry = r.y1 + (uint16)(rnd() % (r.y2 - r.y1 + 1));
}

int irand(int a, int b);


// --- ZONES "hardcodées" Training Ground ---
static const RectZone Z_WARP1 = { "new_1-1", 146,110, 148,114 };
static const RectZone Z_NPC1 = { "new_1-2",  96,16, 103,31 };
static const RectCoords ZC_NPC1 = { "new_1-2",  100,29 }; // Sprakki blv +1 + warp
static const RectZone Z_NPC2 = { "new_1-2", 90,96, 113,110 };
static const RectCoords ZC_NPC2 = { "new_1-2",  100,105 }; // Brade blv +2, jlv +1 + basic stuff to equip => then jlv +1
#if old_training == 0
static const RectCoords ZC_WNPC2 = { "new_1-2",  100,100 }; // Warp point to npc 2
static const RectZone Z_NPC3 = { "new_1-2", 105,111, 120,120 };
static const RectCoords ZC_NPC3 = { "new_1-2",  115,120 }; // Jinha effect + talk again to Brade
static const RectCoords ZC_WNPC4 = { "new_1-2",  41,172 }; // Warp point to npc4
static const RectCoords ZC_NPC4 = { "new_1-2",  33,172 }; // Chocolate (wait ~10s) + blv +1
#else
static const RectCoords ZC_WNPC2 = { "new_1-2",  100,70 }; // Warp point to npc 2
static const RectZone Z_NPC3 = { "new_1-2", 100,107, 115,115 };
static const RectCoords ZC_NPC3 = { "new_1-2",  115,111 }; // Item tutor effect + talk again to Brade
static const RectZone Z_NPC2_0 = { "new_1-2", 95,95, 105,100 }; // walk to npc 2 from pre re
static const RectZone Z_NPC7 = { "new_1-2", 86,103, 91,109 }; // walk to npc 7 old sc
static const RectCoords ZC_WNPC7 = { "new_1-2",  83,111 }; // npc 7 pos
static const RectCoords ZC_WNPC4 = { "new_1-2",  28,178 }; // Warp point to npc4
static const RectCoords ZC_NPC4 = { "new_1-2",  38,182 }; // Entrance guard (wait ~10s)
#endif
static const RectZone Z_NPC4 = { "new_1-2", 27,169, 42,179 };
static const RectZone Z_COMBAT5 = { "new_1-3",  88, 26, 108, 41 }; // kill 1 poring
static const RectCoords Z_NPC5 = { "new_1-3",  96,30 }; // Brade blv +1 jlv +1 then equip random manual and add skills to aa then go to job 9 ! no teleport !
static const RectCoords ZC_WNPC5 = { "new_1-3",  96,21 }; // Warp point to npc5
static const RectCoords ZC_NPC6 = { "new_1-3",  103,50 }; // Npc in the middle
//static const RectZone Z_COMBAT6 = { "new_2-3",  88, 26, 108, 41 }; // just in case...

struct MinMaxMs { uint32 minMs, maxMs; };

static const MinMaxMs TG_DELAY[]{
	{ 1500, 4000 },
	{  800, 2000 },
	{ 2000, 20000 },    // 2 - to wait because can't found coords or before start walking
	{ 2000, 7000 },    // 3 - time to walk to warp
	{ 4000, 10000 },    // 4 - time before lv up 1 with npc 1
	{ 3000, 11000 },    // 5 - time before teleporting from npc 1 to next zone
	{ 7000, 13000 },    // 6-0 - time to walk to npc
	{ 2000, 9000 },    // 6 - time after tp before talk to npc
	{ 2000, 9000 },    // 7 - time before 2 lv up
	{ 2000, 9000 },    // 8 - time before npc 2 equip stuffs
	{ 2000, 9000 },    // 8 - walk to npc 7 old sc
	{ 2000, 9000 },    // 8 - talk to npc 7 old sc
	{ 4000, 10000 },   // 9 - time before npc 2 lv up
	{ 2000, 9000 },    // 10 - time before walk to npc 3 Jinha
	{ 7000, 12000 },   // 11 - time before walk to npc 2 again
	{ 7000, 12000 },   // 11b - time before walk to npc 2 again
	{ 2000, 9000 },    // 12 - time before teleporting from npc 2 to next zone
	{ 2000, 9000 },    // 13 - time before talk to chocolate
	{ 3000, 10000 },    // 14 - time before talk to others
	{ 3000, 10000 },    // 15 - time before talk to chocolate again + lv up
	{ 3000, 10000 },    // 16 - time before teleporting to combat zone
	{ 3000, 13000 },    // 17 - time before talk to npc 5
	{ 1000, 7000 },    // 18 - start to lf poring
	{ 500, 1000 },     // 19 - time before walk to npc 5
	{ 2000, 9000 },    // 20 - time before fake talking & equip book & skills
	{ 5000, 15000 },   // 21 - time before hunt to lv 10
	{ 30000, 60000 },  // 22 - time before walk to npc 5 again
	{ 1000, 2000 },   // 23 - time before exit
	{ 5000, 10000 },   // 23 - time before exit
};
void tg_process(map_session_data* sd);

void fp_monster_killed(map_session_data* sd, mob_data* md);

// Head dir (3 états)
enum head_dir3 { HEAD_FORWARD = 0, HEAD_RIGHT = 1, HEAD_LEFT = 2 };

// Résultat combiné
struct look_result {
	directions body_dir;
	head_dir3  head_dir;
};

struct s_fake_name_meta {
	bool has_sex = false;
	bool has_hair_col = false;
	bool has_hair_style = false;
	char sex = 0;    // 'M' ou 'F' si has_sex=true
	int  hair_color = -1;   // 0..MAX_HAIR_COLOR-1 si has_hair_col=true
	int  hair_style = -1;   // 0..MAX_HAIR_STYLE-1 si has_hair_style=true
};

struct FakeSpawnSpot { //in case of coords issue... it s safe here
	std::string last_map = "prontera";
	std::string save_map = "prontera";
	int sx = 140, sy = 192, ex = 171, ey = 160;
	int last_x = 140, last_y = 192;
	int save_x = 140, save_y = 192;
};

struct FakeInitInfo {
	uint32 aid;
	uint32 cid;
	char   sex; // 'F' ou 'M'
	struct s_fake_profile* prof;
	struct s_fake_jobprofil* jp;
};

class FakeChatDatabase : public TypesafeYamlDatabase<std::string, IntentDefFC> {
public:
	FakeChatDatabase() : TypesafeYamlDatabase("FAKE_CHAT_DB", 1) {}

	void clear() override {
		TypesafeYamlDatabase<std::string, IntentDefFC>::clear();
		cfg_ = ChatConfigFC{}; // reset config aux defaults
	}

	const std::string getDefaultLocation() override;
	uint64 parseBodyNode(const ryml::NodeRef& node) override;

	// Accès config chargée
	const ChatConfigFC& cfg() const { return cfg_; }
	ChatConfigFC& cfg() { return cfg_; } // si tu veux la modifier ailleurs
private:
	ChatConfigFC cfg_;

	// sous-parsers
	bool parseConfigNode_(const ryml::NodeRef& node);
	bool parseIntentNode_(const ryml::NodeRef& node);

	// helpers pour StateBoosts
	void parseStateBoosts_(const ryml::NodeRef& sbNode, StateBoostsMapFC& out);
};

extern FakeChatDatabase fake_chat_db;

struct BotChatMemory {
	uint64 lastSpeakTick = 0;
	std::unordered_map<uint32/*char_id*/, uint64/*tick*/> lastReplyTo;
	uint32 lastInterlocutor = 0;
	uint8  consecutiveTurns = 0;
};

// ===== Étape 3: Aho-Corasick matcher =====
struct FC_ACNode {
	int next[37]; // a..z (26), 0..9 (10), space (1) => 37
	int link = 0;
	// liste (intentIndex, weight=1 par hit)
	std::vector<int> hits; // store intent indices
	FC_ACNode() { std::fill(std::begin(next), std::end(next), -1); }
};
struct FC_ACMatcher {
	std::vector<FC_ACNode> trie;
	// mapping intent index <-> name
	std::vector<const IntentDefFC*> intents; // stable pointers into DB (snapshot at build)
	std::unordered_map<std::string, int> name2idx;
	bool built = false;
};

extern FC_ACMatcher g_fc_matcher;

// Construire le matcher depuis la DB (à appeler après le load YML)
void fakechat_build_matcher();

// Détection avancée: AC + fuzzy + boosts par état
// stateName: "", "Farming", "Walking", etc. (si vide, pas de boost)
const IntentDefFC* fakechat_detect_intent_ac(
	const FakeChatDatabase& db,
	const std::string& msgNorm,
	const std::string& stateName /* peut être "" */
);

enum class FCChannel : uint8 {
	AUTO = 255,  // <- nouveau: laisser l’algo choisir (par défaut)
	SELF = 0,
	PM,
	PARTY,
	GUILD,
	GLOBAL
};

bool fakechat_should_reply(const ChatConfigFC& cfg, BotChatMemory& mem, uint32 fromCid, bool mentioned, uint64 now);

// Normalisation très simple (ToLower + trim spaces)
std::string fakechat_normalize(std::string s);

// Mention (@name dans le message) ou PM (tu passes is_private)
bool fakechat_is_mentioned(const map_session_data* bot, const std::string& msgNorm, bool is_private);

// Décision de répondre (cooldowns + proba)
bool fakechat_should_reply(const ChatConfigFC& cfg, BotChatMemory& mem, uint32 fromCid, bool mentioned, uint64 now);

// Détection d’intent (simple scan de keywords, pondéré par Intent.Weight)
const IntentDefFC* fakechat_detect_intent_basic(const FakeChatDatabase& db, const std::string& msgNorm);

// Sélection de reply (uniforme pour l’instant)
const std::string* fakechat_pick_reply_uniform(const IntentDefFC& intent);

// Calcul d’un délai “typage” simple
int fakechat_typing_delay_ms(const ChatConfigFC& cfg, size_t text_len);

bool fakechat_try_handle_message_step2(
	map_session_data* bot,
	map_session_data* from,
	std::string msg,
	bool is_private,
	std::string& out_reply,
	int& out_delay_ms,
	std::string& out_intent,
	FCChannel channel_hint = FCChannel::AUTO,  // <- ajout, valeur par défaut
	int target_id_hint = 0                     // <- pour PM forcé, sinon ignoré
);


//////////////////////////////

extern std::unordered_map<uint32 /*cid*/, FakeInitInfo> g_fake_init_infos;
extern std::unordered_map<uint32 /*cid*/, map_session_data*> g_fake_sd_hold;

// API
inline void fake_store_init_info(uint32 aid, uint32 cid, char sex, struct s_fake_jobprofil* jp, struct s_fake_profile* prof) {
	g_fake_init_infos.emplace(cid, FakeInitInfo{ aid, cid, sex, prof, jp });
}

// Lance les connexions pour toutes les entrées en attente ; retourne le nombre démarré
size_t connect_fake_players_from_init();

// purge des infos en attente
inline void fake_clear_init_infos() { g_fake_init_infos.clear(); }

struct s_fake_town {
	std::string map;
	uint16 number = 0;
	uint16 startx = 0, starty = 0, endx = 0, endy = 0;
};

struct s_fake_behavior {
	std::string behavior;
	uint16 number = 0;
};

struct s_fake_jobprofile_ref {
	std::string name; // reference vers Fake JobProfil
	uint16 number = 0;
};

enum e_fake_behavior : uint16 {
	BEHAVIOR_GRIND = 0,
	BEHAVIOR_TOWN,
	BEHAVIOR_TRAINING
};

struct FakeItem {
	std::string name;
	uint32_t quantity;
	uint32_t price;
};

struct s_fake_curve {
	bool enabled = false;
	uint16 smoothing_minutes = 30; // non utilisé en étape 1
	double weekend_boost = 1.10;
	uint16 perc_low = 10;
	uint16 perc_high = 100;

	bool  use_table = false;           // vrai si HourlyProfile est présent/valide
	bool  has_weekend_table = false;   // vrai si WeekendProfile est présent/valide
	float hour_table[24] = {};         // % base → cible (0..100)
	float weekend_table[24] = {};      // % base → cible (0..100) (optionnel)
};

struct s_fake_profile {
	std::string id; // Profiles
	uint16 levelMin = 1, levelMax = 1;

	std::vector<s_fake_jobprofile_ref> jobprofiles;
	std::vector<s_fake_town> towns;
	std::vector<s_fake_behavior> behaviors;

	std::string namesFile;                 // chemin relatif/absolu
	std::vector<std::string> names;        // noms charges du fichier
	size_t name_cursor = 0;                // pointeur de parcours (round-robin)
	bool randomAbsences = false;

	std::unordered_map<std::string, s_fake_name_meta> name_meta; // key = norm_key(name)

	s_fake_curve curve;
};

class FakePlayersDatabase : public TypesafeYamlDatabase<std::string, s_fake_profile> {
public:
	FakePlayersDatabase() : TypesafeYamlDatabase("FAKE_PLAYERS_DB", 1) {}

	const std::string getDefaultLocation() override;
	uint64 parseBodyNode(const ryml::NodeRef& node) override;
};

extern FakePlayersDatabase fake_players_db;


// ===== JobProfils =====

struct s_fake_skill {
	uint16 skillid; // e.g. MG_FIREBOLT
	uint16 number = 0;
};

struct s_fake_hat {
	t_itemid nameid;
	uint16 number = 0;
};

struct s_fake_fieldzone {
	int16 mapid;
	uint16 number = 0;
};

struct s_fake_jobprofil {
	std::string id;   // JobProfile:
	std::string job;  // Job:
	int64 job_mapid = -1; // e_mapid (résolu via constantes)
	int64 job_id = -1; // job id (résolu via constantes)

	// Stats
	int16 str = 0, agi = 0, vit = 0, _int = 0, dex = 0, luk = 0;

	std::vector<s_fake_skill> skills;
	std::vector<s_fake_fieldzone> fieldzones;

	std::vector<s_fake_hat> hatsTop;
	std::vector<s_fake_hat> hatsMid;
	std::vector<s_fake_hat> hatsBot;

	std::vector<s_fake_hat> c_hatsTop;
	std::vector<s_fake_hat> c_hatsMid;
	std::vector<s_fake_hat> c_hatsBot;

	t_itemid weapon_id;
	t_itemid shield_id;
	bool arrow = false;

	struct script_code* script;
	~s_fake_jobprofil() {
		if (this->script){
			script_free_code(this->script);
			this->script = nullptr;
		}
	}
};

struct FakeChatSendPayload {
	std::string text;
	FCChannel chan = FCChannel::SELF;
	uint32 target_id = 0; // pour PM: id du destinataire (from->id)
	std::string target_name;
};

class FakeJobProfilsDatabase : public TypesafeYamlDatabase<std::string, s_fake_jobprofil> {
public:
	FakeJobProfilsDatabase() : TypesafeYamlDatabase("FAKE_JOBPROFILS_DB", 1) {}

	const std::string getDefaultLocation() override;
	uint64 parseBodyNode(const ryml::NodeRef& node) override;
};

extern FakeJobProfilsDatabase fake_jobprofils_db;

void fp_job_bonus(map_session_data* sd);

TIMER_FUNC(fakeplayer_init_timer);
TIMER_FUNC(fakecurve_global_timer);
TIMER_FUNC(fakechat_send_timer);
TIMER_FUNC(fakeplayer_tg_quit_timer);

void fakecurve_start_timer();  // call this once at startup
void fakeplayer_dead_tp(map_session_data* sd);
void fakeplayer_town_behavior(map_session_data* sd);

int fake_add_from_profile(s_fake_profile* prof, uint16 count);
int fake_add_from_profile_name(const std::string& prof_name, uint16 count);
int fake_cancel_pending_from_profile_name(const std::string& prof_name, uint16 count);
void fake_warp_to_town(map_session_data* sd);

// Curve utilities (step 1)
int  fakecurve_capacity(const s_fake_profile* prof);                    // somme des JobProfiles[].Number
int  fakecurve_online(const s_fake_profile* prof);                      // combien en sd_hold pour ce profil
int  fakecurve_target_now(const s_fake_profile* prof);                  // calcule target attendu maintenant
int  fakecurve_apply_additions(const std::string& prof_name, int want); // n'ajoute que le manque (>=0)
void fakecurve_tick_once();

void fakecurve_print_24h(const s_fake_profile* prof);

bool fakecurve_enable_profile(const std::string& id, bool enable);
bool fakecurve_set_profile(const std::string& id, uint16 perc_low, uint16 perc_high, uint16 smoothing_min, double weekend_boost);
bool fakeplayer_set_enabled(bool enable);

bool fc_target_mob_is_available_for_bot(mob_data* md, map_session_data* bot, uint64 now);
bool fc_item_is_available_for_bot(flooritem_data* it, map_session_data* bot, uint64 now);

void do_final_fakeprofils(void);
void do_init_fakeprofils(void);

#endif
/* FAKEPLAYERS_HPP */
