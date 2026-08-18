// Copyright (c) Shakto Scripts - https://ronovelty.com/

#pragma execution_character_set("utf-8")

#include "autoattack.hpp"
#include "autoattack_script.hpp"
#include "fake_players.hpp"
#include "battle.hpp"
#include "chrif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "map.hpp" // mmysql_handle
#include "npc.hpp"
#include "party.hpp"
#include "pc.hpp"
#include "skill.hpp"
#include "status.hpp"
#include "unit.hpp"

#include <random>
#include <queue>
#include <cmath>
#include <tuple>
#include <unordered_set>
#include <set>
#include <chrono>
#include <vector>
#include <fstream>
#include <algorithm>
#include <limits>

#include <common/cbasetypes.hpp>
#include <common/database.hpp>
#include <common/malloc.hpp>
#include <common/nullpo.hpp>
#include <common/showmsg.hpp>
#include <common/socket.hpp>
#include <common/sql.hpp>
#include <common/strlib.hpp>
#include <common/utilities.hpp>
#include <common/utils.hpp>

using namespace rathena;

#define M_PI       3.14159265358979323846   // pi

std::unordered_map<uint32, FakeInitInfo> g_fake_init_infos;
std::unordered_map<uint32, map_session_data*> g_fake_sd_hold;

static std::string base36(uint64_t v) {
	static const char* d = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	char buf[16]; // 64 bits: max 13 digits en base36 + '\0' => 14, donc 16 OK
	int i = sizeof(buf) - 1; buf[i] = '\0';
	if (v == 0) { buf[--i] = '0'; }
	while (v > 0 && i > 0) {
		uint64_t r = v % 36;
		buf[--i] = d[r];
		v /= 36;
	}
	return std::string(&buf[i]);
}

// Genere un userid court et (tres) probablement unique, <= 23 chars
static std::string gen_fake_userid_candidate(int attempt)
{
	// base36() : tu l'as deja.
	// gettick()/time(nullptr) : dispos dans ton code. On mixe avec 'attempt' pour varier.
	uint64 salt = ((uint64)time(nullptr) << 20) ^ (uint64)gettick() ^ (uint64)attempt;
	std::string u = "fp" + base36(salt);   // eg. "fpk3z0..."
	if (u.size() > 23) u.resize(23);
	return u;
}

// ---------- ID POOL HELPERS ----------
static bool fake_idpool_fetch(uint32& out_cid, uint32& out_aid)
{
	out_cid = 0; out_aid = 0;
	char* s_cid = nullptr, * s_aid = nullptr;

	if (SQL_SUCCESS != Sql_Query(mmysql_handle,
		"SELECT `char_id`,`account_id` FROM `fake_id_pool` ORDER BY `char_id` ASC LIMIT 1"))
	{
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	if (Sql_NumRows(mmysql_handle) <= 0) {
		Sql_FreeResult(mmysql_handle);
		return false;
	}
	if (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		Sql_GetData(mmysql_handle, 0, &s_cid, nullptr);
		Sql_GetData(mmysql_handle, 1, &s_aid, nullptr);
		if (s_cid && s_aid) {
			out_cid = (uint32)strtoul(s_cid, nullptr, 10);
			out_aid = (uint32)strtoul(s_aid, nullptr, 10);
		}
	}
	Sql_FreeResult(mmysql_handle);
	return (out_cid != 0 && out_aid != 0);
}

static bool fake_idpool_consume(uint32 cid)
{
	// supprime l'entree du pool quand on a effectivement insere char+login
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE FROM `fake_id_pool` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	return true;
}

static bool fake_idpool_return(uint32 cid, uint32 aid)
{
	// remet la paire cid/aid dans le pool (a l'issue d'une purge, par ex.)
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT IGNORE INTO `fake_id_pool`(`char_id`,`account_id`) VALUES(%u,%u)", cid, aid))
	{
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	return true;
}

// Normalise angle en radians dans [-pi, +pi)
static inline double norm_angle(double a) {
	const double PI = 3.14159265358979323846;
	const double TWO_PI = 2.0 * PI;
	while (a >= PI)  a -= TWO_PI;
	while (a < -PI)  a += TWO_PI;
	return a;
}

// Angle (radians) de chaque direction 8-axes (y croît vers le Sud)
static inline double dir_to_angle(enum directions d) {
	// Convention: 0 rad = Est, π/2 = Sud, -π/2 = Nord (atan2(dy, dx))
	switch (d) {
	case DIR_EAST:      return 0.0;
	case DIR_SOUTHEAST: return 0.7853981633974483;   // 45°  =  π/4
	case DIR_SOUTH:     return 1.5707963267948966;   // 90°  =  π/2
	case DIR_SOUTHWEST: return 2.356194490192345;    // 135° = 3π/4
	case DIR_WEST:      return 3.141592653589793;    // 180° =  π
	case DIR_NORTHWEST: return -2.356194490192345;   // -135°
	case DIR_NORTH:     return -1.5707963267948966;  // -90°
	case DIR_NORTHEAST: return -0.7853981633974483;  // -45°
	}
	return 0.0;
}

// “Snap” l’angle (radians) vers la direction 8-axes la plus proche
static inline enum directions angle_to_dir(double a) {
	// Bins de 45° centrés sur chaque direction
	const double PI = 3.14159265358979323846;
	const double STEP = PI / 4.0; // 45°
	// On part de Est=0 et on numérote les 8 secteurs
	int sector = (int)std::floor((a + STEP / 2.0) / STEP);
	// Ramener dans [-4..+3]
	sector = (sector % 8 + 8) % 8;

	switch (sector) {
	case 0: return DIR_EAST;
	case 1: return DIR_SOUTHEAST;
	case 2: return DIR_SOUTH;
	case 3: return DIR_SOUTHWEST;
	case 4: return DIR_WEST;
	case 5: return DIR_NORTHWEST;
	case 6: return DIR_NORTH;
	case 7: return DIR_NORTHEAST;
	}
	return DIR_SOUTH; // fallback
}

// Vecteur unitaire (approx int) de la direction du corps (pour le signe gauche/droite)
static inline void dir_to_vec(enum directions d, int& vx, int& vy) {
	switch (d) {
	case DIR_NORTH:     vx = 0; vy = -1; break;
	case DIR_NORTHWEST: vx = -1; vy = -1; break;
	case DIR_WEST:      vx = -1; vy = 0; break;
	case DIR_SOUTHWEST: vx = -1; vy = 1; break;
	case DIR_SOUTH:     vx = 0; vy = 1; break;
	case DIR_SOUTHEAST: vx = 1; vy = 1; break;
	case DIR_EAST:      vx = 1; vy = 0; break;
	case DIR_NORTHEAST: vx = 1; vy = -1; break;
	}
}

// --- Fonction principale ---
// Retourne la direction 8-axes du CORPS + la direction 3-états de la TÊTE
// par rapport à la position du joueur sd->(x,y).
// y augmente vers le NORD
static inline look_result look_from_player_to_xy(const map_session_data* sd, int tx, int ty) {
	const double HEAD_FORWARD_DEG = 20.0;
	const double PI = 3.14159265358979323846;
	const double DEG2RAD = PI / 180.0;
	const double HEAD_FORWARD_RAD = HEAD_FORWARD_DEG * DEG2RAD;

	look_result out;
	if (!sd) { out.body_dir = DIR_SOUTH; out.head_dir = HEAD_FORWARD; return out; }

	int dx = tx - sd->x;
	int dy = ty - sd->y;

	if (dx == 0 && dy == 0) {
		out.body_dir = DIR_SOUTH;
		out.head_dir = HEAD_FORWARD;
		return out;
	}

	// >>> CHANGEMENT CLEF : y↑Nord => angle = atan2(-dy, dx)
	double ang_target = std::atan2(-(double)dy, (double)dx);

	out.body_dir = angle_to_dir(ang_target);

	double ang_body = dir_to_angle(out.body_dir);
	double delta = norm_angle(ang_target - ang_body);

	int bx, by; dir_to_vec(out.body_dir, bx, by);
	long long cross = (long long)bx * (long long)dy - (long long)by * (long long)dx;

	if (std::fabs(delta) <= HEAD_FORWARD_RAD) {
		out.head_dir = HEAD_FORWARD;
	}
	else {
		out.head_dir = (cross < 0) ? HEAD_RIGHT : HEAD_LEFT;
	}

	return out;
}

// Clamp longueur et escape SQL via mmysql_handle
static std::string clamp_and_escape(const char* s, size_t maxlen) {
	if (!s) s = "";
	size_t inlen = strnlen(s, maxlen); // on tronque ici si > maxlen
	// Taille max apres escape ~ 2*in + 1
	std::string out; out.resize(inlen * 2 + 1);
	size_t outlen = Sql_EscapeStringLen(mmysql_handle, &out[0], s, inlen);
	out.resize(outlen);
	return out;
}

// INSERT auto-increment sur `login` (NE PAS passer account_id !)
// - Genere un userid interne (retry si doublon)
// - Recupere LAST_INSERT_ID() dans out_aid
static bool fake_insert_login_auto(Sql* sql,
	char sex, const char* pass, const char* email, int group_id,
	uint32& out_aid, std::string* out_userid_opt /*=nullptr*/)
{
	out_aid = 0;

	std::string pw = clamp_and_escape(pass, 32);
	std::string em = clamp_and_escape(email, 39);

	const int MAX_RETRY = 20;
	for (int i = 0; i < MAX_RETRY; ++i) {
		std::string u = gen_fake_userid_candidate(i);

		// Tente l'INSERT sans account_id (AUTO_INCREMENT)
		if (SQL_ERROR == Sql_Query(sql,
			"INSERT INTO `login` "
			"(`userid`,`user_pass`,`sex`,`email`,`group_id`,`state`,`is_fake`) "
			"VALUES ('%s','%s','%c','%s',%d,0,1)",
			u.c_str(), pw.c_str(), sex, em.c_str(), group_id))
		{
			int err = Sql_GetError(sql);
			if (err == 1062) {
				// userid en doublon -> reessaye avec un autre
				continue;
			}
			Sql_ShowDebug(sql);
			return false;
		}

		// Succes: recuperer l'AID auto-incremente
		char* s_id = nullptr;
		if (SQL_ERROR == Sql_Query(sql, "SELECT LAST_INSERT_ID()")) {
			Sql_ShowDebug(sql);
			return false;
		}
		if (SQL_SUCCESS == Sql_NextRow(sql)) {
			Sql_GetData(sql, 0, &s_id, nullptr);
			if (s_id) out_aid = (uint32)strtoul(s_id, nullptr, 10);
		}
		Sql_FreeResult(sql);

		if (out_aid == 0) {
			return false;
		}

		if (out_userid_opt) *out_userid_opt = u;
		return true;
	}

	return false;
}


// INSERT avec AID impose (chemin POOL). userid est genere en interne.
static bool fake_insert_login_forced_aid(Sql* sql,
	uint32 aid, char sex, const char* pass, const char* email, int group_id,
	std::string* out_userid_opt /*=nullptr*/)
{
	std::string pw = clamp_and_escape(pass, 32);
	std::string em = clamp_and_escape(email, 39);

	const int MAX_RETRY = 20;
	for (int i = 0; i < MAX_RETRY; ++i) {
		std::string u = gen_fake_userid_candidate(i);

		if (SQL_ERROR == Sql_Query(sql,
			"INSERT INTO `login` "
			"(`account_id`,`userid`,`user_pass`,`sex`,`email`,`group_id`,`state`,`is_fake`) "
			"VALUES (%u, '%s', '%s', '%c', '%s', %d, 0, 1)",
			aid, u.c_str(), pw.c_str(), sex, em.c_str(), group_id))
		{
			int err = Sql_GetError(sql);
			if (err == 1062) {
				// collision userid -> retry avec un autre
				continue;
			}
			Sql_ShowDebug(sql);
			return false;
		}

		if (out_userid_opt) *out_userid_opt = u;
		return true;
	}

	return false;
}

int irand(int a, int b) { // inclusif
	if (a > b) std::swap(a, b);
	return a + (rnd() % (b - a + 1));
}

static int count_online_fakes_for_profile(const s_fake_profile* prof) {
	if (!prof) return 0;
	int count = 0;
	map_session_data* sd = nullptr;
	for (const auto& kv : g_fake_sd_hold) {
		map_session_data* sd = kv.second;
		if (!sd) continue;

		if (sd->fp.is_fake_player && sd->fp.prof == prof)
			++count;
	}
	return count;
}

int fakecurve_base_number(const s_fake_profile* prof) {
	if (!prof) return 0;
	if (prof->jobprofiles.empty()) return 0;

	uint64_t sum = 0;
	for (const auto& r : prof->jobprofiles) {
		// r.number est un uint16 ; additionne tel quel (0 compte pour 0).
		sum += static_cast<uint32_t>(r.number);
	}

	if (sum > static_cast<uint64_t>(std::numeric_limits<int>::max()))
		return std::numeric_limits<int>::max();

	return static_cast<int>(sum);
}

// Capacite "theorique" du profil pour un etat weekend donne.
// = base * (PercHigh/100) * (weekend ? WeekendBoost : 1), borne par le cap global.
static int fakecurve_capacity_for(const s_fake_profile* prof, bool weekend)
{
	if (!prof) return 0;
	const int base = fakecurve_base_number(prof); // somme des Number
	if (base <= 0) return 0;

	// Si la curve est off, on reste a la base.
	if (!prof->curve.enabled)
		return base;

	double peak = std::max(0.0, (double)prof->curve.perc_high) / 100.0; // ex: 150% => 1.5
	if (peak <= 0.0) peak = 1.0;

	double mult = peak;
	if (weekend)
		mult *= prof->curve.weekend_boost;

	long long cap = llround((double)base * mult);

	// borne par le cap global serveur, si tu en as un (ex: battle_config.fake_max_online)
	// adapte ce nom si different chez toi
	if (battle_config.fake_max_online > 0)
		cap = std::min<long long>(cap, battle_config.fake_max_online);

	return (int)std::max<long long>(0, cap);
}

static bool is_weekend_now() {
	time_t t = time(nullptr);
	tm lt{};
#ifdef _WIN32
	localtime_s(&lt, &t);
#else
	localtime_r(&t, &lt);
#endif
	// 0=Dimanche..6=Samedi
	int w = lt.tm_wday;
	return (w == 0 || w == 6);
}

/**
 * Check & run script bonus for a fake player
 * @param sd Player
 * @author [Shakto]
 **/
void fp_job_bonus(map_session_data *sd) {
	if (sd && sd->fp.is_fake_player && sd->fp.jobprofile) {
		run_script(sd->fp.jobprofile->script, 0, sd->id, 0);
		status_heal(sd, sd->status.max_hp, sd->status.max_sp, 2);
	}
}

// Wrapper runtime (utilise partout) : weekend = now()
int fakecurve_capacity(const s_fake_profile* prof)
{
	const bool weekend = is_weekend_now();
	return fakecurve_capacity_for(prof, weekend);
}

int fakecurve_online(const s_fake_profile* prof) {
	if (!prof) return 0;
	int on = 0;
	for (const auto& kv : g_fake_sd_hold) {
		map_session_data* sd = kv.second;
		if (!sd) continue;

		if (sd->fp.is_fake_player && sd->fp.prof == prof)
			++on;
	}
	return on;
}

static void fake_wipe_inv_and_equips(map_session_data* sd) {
	uint16 i;

	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].amount) {
			std::shared_ptr<item_data> id = item_db.find(sd->inventory.u.items_inventory[i].nameid);

			if (sd->inventory.u.items_inventory[i].equip != 0)
				pc_unequipitem(sd, i, 3);
		}
		pc_equipswitch_remove(sd, i);

		int32 amount = sd->inventory.u.items_inventory[i].amount;

		pc_delitem(sd, i, amount, 1, 0, LOG_TYPE_OTHER);
	}

	pc_setcart(sd, 0);
	pc_setfalcon(sd, 0);
	pc_setriding(sd, 0);

	for (const auto& it : status_db) {
		sc_type status = static_cast<sc_type>(it.first);

		if (!sd->sc.getSCE(status))
			continue;

		status_change_end(sd, status);
	}
}


// Purge DB pour un fake player donne par son char_id.
// - Ne fait rien si le char_id n'existe pas ou si is_fake=0 (securite).
// - Retourne TRUE si tout a ete purge (COMMIT), FALSE si erreur (ROLLBACK).
bool fake_db_purge_by_cid(uint32 cid)
{
	if (cid == 0) {
		return false;
	}

	// 1) Recupere account_id + is_fake
	uint32 aid = 0; int is_fake = 0;
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"SELECT `account_id`,`is_fake` FROM `char` WHERE `char_id`=%u LIMIT 1", cid))
	{
		Sql_ShowDebug(mmysql_handle);
		return false;
	}
	if (Sql_NumRows(mmysql_handle) <= 0) {
		Sql_FreeResult(mmysql_handle);
		return false;
	}
	Sql_NextRow(mmysql_handle);
	{
		char* s_aid = nullptr; char* s_fake = nullptr;
		Sql_GetData(mmysql_handle, 0, &s_aid, nullptr);
		Sql_GetData(mmysql_handle, 1, &s_fake, nullptr);
		aid = static_cast<uint32>(strtoul(s_aid, nullptr, 10));
		is_fake = static_cast<int>(strtol(s_fake, nullptr, 10));
	}
	Sql_FreeResult(mmysql_handle);

	if (is_fake == 0) {
		ShowWarning("FakePurge: cid=%u exists but is_fake=0 -> skip for safety.\n", cid);
		return false;
	}

	// 2) Transaction
	if (SQL_ERROR == Sql_Query(mmysql_handle, "START TRANSACTION")) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}

	// --- PURGE par char_id ---
	// autoattack
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `aa_items` WHERE `char_id`=%u", cid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `aa_skills` WHERE `char_id`=%u", cid))
	{
		Sql_ShowDebug(mmysql_handle);return false;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `aa_mobs` WHERE `char_id`=%u", cid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `aa_common_config` WHERE `char_id`=%u", cid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}

	// hotkey
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `hotkey` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle); return false;
	}
	// inventory
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `inventory` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle); return false;
	}
	// char_reg_str
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `char_reg_str` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle); return false;
	}
	// char_reg_num
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `char_reg_num` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle); return false;
	}
	// skill
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `skill` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle); return false;
	}
	// sc_data
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `sc_data` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle); return false;
	}
	// bonus_script
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `bonus_script` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle); return false;
	}
	// quest
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `quest` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle); return false;
	}
	// achievement
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `achievement` WHERE `char_id`=%u", cid)) {
		Sql_ShowDebug(mmysql_handle); return false;
	}

	// mail_attachments (par jointure sur mail.id -> dest_id == cid)
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE c FROM `mail_attachments` AS c "
		"JOIN `mail` AS m ON m.`id` = c.`id` "
		"WHERE m.`dest_id`=%u", cid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}

	// mail (destinataire = cid)
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `mail` WHERE `dest_id`=%u", cid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}

	// mails envoyes par ce perso -> on annule l'expediteur
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `mail` SET `send_id`=0 WHERE `send_id`=%u", cid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}

	// char lui-meme (securise par is_fake=1)
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `char` WHERE `char_id`=%u AND `is_fake`=1", cid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}

	// --- PURGE par account_id (si compte fake) ---
	// acc_reg_num
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE c FROM `acc_reg_num` AS c "
		"JOIN `login` AS l ON l.`account_id` = c.`account_id` "
		"WHERE l.`account_id`=%u AND l.`is_fake`=1", aid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}

	// acc_reg_str
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE c FROM `acc_reg_str` AS c "
		"JOIN `login` AS l ON l.`account_id` = c.`account_id` "
		"WHERE l.`account_id`=%u AND l.`is_fake`=1", aid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}

	// login (securise par is_fake=1)
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"DELETE FROM `login` WHERE `account_id`=%u AND `is_fake`=1", aid))
	{
		Sql_ShowDebug(mmysql_handle); return false;
	}

	// 3) Commit
	if (SQL_ERROR == Sql_Query(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		return false;
	}

	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT IGNORE INTO `fake_id_pool`(`char_id`,`account_id`) VALUES(%u,%u)", cid, aid))
	{
		// on loggue mais on ne rollback pas toute la purge pour ça
		Sql_ShowDebug(mmysql_handle);
	}

	ShowInfo("FakePurge: purged cid=%u aid=%u (is_fake=1)\n", cid, aid);
	return true;
}

bool is_valid_profile_ptr(const s_fake_profile* p)
{
	if (!p) return false;
	for (const auto& kv : fake_players_db) {        // itère DB <string, shared_ptr<s_fake_profile>>
		const std::shared_ptr<s_fake_profile>& sp = kv.second;
		if (sp.get() == p)
			return true;
	}
	return false;
}

// Retire jusqu'a `count` FAKEs ACTIFS (deja mappes) pour un profil donne.
// Retourne le nombre effectivement retire.
int fake_destroy_active_from_profile_name(const std::string& prof_name, uint16 count)
{
	if (count == 0) return 0;
	int removed = 0;

	// Parcourt le hold (qui contient aussi les sessions mappees chez toi)
	for (auto it = g_fake_sd_hold.begin(); it != g_fake_sd_hold.end() && removed < count; ) {
		map_session_data* sd = it->second;
		if (!sd) { it = g_fake_sd_hold.erase(it); ++removed; continue; }

		fake_wipe_inv_and_equips(sd);

		// Actif = pas "pending"
		const bool is_fake = sd->fp.is_fake_player != 0;
		auto* prof = sd->fp.prof;
		bool match = false;

		if (is_fake && is_valid_profile_ptr(prof)) {
			std::string prof_id_copy;
			try {
				prof_id_copy = prof->id;
			}
			catch (...) {
				prof_id_copy.clear();
				{ it = g_fake_sd_hold.erase(it); ++removed; continue; }
			}
			match = (!prof_id_copy.empty() && prof_id_copy == prof_name);
		}

		if (match) {
			const uint32 aid = sd->status.account_id;
			const uint32 cid = sd->status.char_id;

			if (sd->fp.fake_player_timer != INVALID_TIMER) delete_timer(sd->fp.fake_player_timer, fakeplayer_init_timer);
			if (sd->fp.fake_player_chat != INVALID_TIMER) delete_timer(sd->fp.fake_player_chat, fakechat_send_timer);
			if (sd->fp.fake_player_tg_quit != INVALID_TIMER) delete_timer(sd->fp.fake_player_tg_quit, fakeplayer_tg_quit_timer);

			// Retire juste le pointeur de la map (pas de delete ici !)
			it = g_fake_sd_hold.erase(it);

			// Déconnexion propre: le core va appeler chrif_auth_delete -> dtor + aFree
			set_eof(sd->fd);
			map_quit(sd);

			fake_db_purge_by_cid(cid);
			++removed;

			ShowInfo("FakeDestroy: active removed AID=%u CID=%u prof='%s' (%d/%u)\n",
				aid, cid, prof_name.c_str(), removed, (unsigned)count);
			continue;
		}
		else
			++it;
	}

	if (removed == 0)
		ShowInfo("FakeDestroy: no active fake matched profile='%s'\n", prof_name.c_str());
	else
		ShowInfo("FakeDestroy: removed=%d active fake(s) on profile='%s'\n", removed, prof_name.c_str());

	return removed;
}

static int fakecurve_peak_ceiling(const s_fake_profile* prof) {
	if (!prof) return 0;
	const int base = fakecurve_base_number(prof);
	if (base <= 0) return 0;

	const double ph = std::max(0.0, (double)prof->curve.perc_high); // ex. 150
	double mult = ph / 100.0;

	if (is_weekend_now())            // <- pour inclure le boost dans le plafond
		mult *= prof->curve.weekend_boost;

	const int high_peak = (int)std::floor(base * mult);
	const int global_cap = battle_config.fake_max_online > 0 ? battle_config.fake_max_online : INT_MAX;
	return std::max(0, std::min(high_peak, global_cap));
}


int fakecurve_target_now(const s_fake_profile* prof)
{
	if (!prof || !prof->curve.enabled) return 0;

	time_t t = time(nullptr);
	tm lt{};
#ifdef _WIN32
	localtime_s(&lt, &t);
#else
	localtime_r(&t, &lt);
#endif
	const bool wknd = is_weekend_now();

	// Cap dynamique (peak + boost WE + cap global si tu l'as code dans fakecurve_capacity_for)
	const int cap_dyn = fakecurve_capacity_for(prof, wknd);
	if (cap_dyn <= 0) return 0;

	// Interpolation h -> h+1 avec minutes+secondes
	const int h0 = std::clamp(lt.tm_hour, 0, 23);
	const int h1 = (h0 + 1) % 24;
	const double w = (std::clamp(lt.tm_min, 0, 59) + (double)std::clamp(lt.tm_sec, 0, 59) / 60.0) / 60.0;

	double p = 0.0; // 0..1

	if (prof->curve.use_table) {
		const float* TAB = (wknd && prof->curve.has_weekend_table)
			? prof->curve.weekend_table
			: prof->curve.hour_table;

		const double v0 = TAB[h0] / 100.0;
		const double v1 = TAB[h1] / 100.0;
		p = (1.0 - w) * v0 + w * v1;  // lissage minute+seconde
	}
	else {
		// Fallback sinusoïde lissee (incluant minutes+secondes)
		const double hourf = h0 + (std::clamp(lt.tm_min, 0, 59) + (double)std::clamp(lt.tm_sec, 0, 59) / 60.0) / 60.0;
		const double phase = (hourf / 24.0) * 2.0 * M_PI;
		const double day_f = 0.5 * (1.0 + std::sin(phase - M_PI * 0.25));
		const double pl = std::clamp((double)prof->curve.perc_low, 0.0, 100.0);
		const double ph = std::clamp((double)prof->curve.perc_high, 0.0, 100.0);
		p = (pl + (ph - pl) * day_f) / 100.0;
		// NB: pas de multiplication par WeekendBoost ici — il est dans cap_dyn si tu l'y integres.
	}

	const int raw = (int)std::lround(cap_dyn * p);
	int target = std::max(0, std::min(raw, cap_dyn));
	return target;
}

static bool fc_is_blank(const std::string& s) {
	for (char c : s) if (!std::isspace((unsigned char)c)) return false;
	return true;
}

void fakecurve_tick_once() {
	if (!battle_config.feature_fake_enable) return;

	// Hypothese: tick appele ~chaque minute. Si different, ajuste 'tick_minutes'.
	const double tick_minutes = 1.0;

	for (auto& kv : fake_players_db) {
		std::shared_ptr<s_fake_profile> prof = kv.second;
		if (!prof || !prof->curve.enabled) continue;

		const int cap = fakecurve_capacity(prof.get());
		if (cap <= 0) continue;

		const int online = fakecurve_online(prof.get());   // via g_fake_sd_hold
		const int target = fakecurve_target_now(prof.get());
		int delta = target - online;
		if (delta == 0) continue;

		// Lissage: variation max par tick en fonction de SmoothingMinutes et de la capacite.
		int max_step = 1;
		if (prof->curve.smoothing_minutes > 0) {
			const double stepf = (double)cap * (tick_minutes / (double)prof->curve.smoothing_minutes);
			max_step = std::max(1, (int)std::ceil(stepf));
		}

		if (delta > 0) {
			// Besoin d'ajouter
			const int want = std::min(delta, max_step);
			if (want > 0) {
				(void)fake_add_from_profile_name(prof->id, (uint16)want);
			}
		}
		else {
			// Besoin d'enlever (delta < 0)
			int need = std::min(-delta, max_step);

			if (need > 0) {
				// 1) d'abord on annule les pending
				int cut_pending = fake_cancel_pending_from_profile_name(prof->id, (uint16)need);
				need -= cut_pending;

				// 2) s'il reste du delta, on retire des ACTIFS mappes
				if (need > 0) {
					int cut_active = fake_destroy_active_from_profile_name(prof->id, (uint16)need);
					need -= cut_active;
				}
				// si need > 0 ici : on ne peut pas descendre davantage a ce tick
			}
		}
	}
}

static char fake_random_sex() {
	// 0 => 'M', 1 => 'F'
	return (rnd() % 2 == 0) ? 'M' : 'F';
}

static uint16 pick_index_weighted_u16(const std::vector<uint16>& w) {
	uint32 total = 0;
	for (uint16 v : w) total += v;
	if (!total) return UINT16_MAX;
	uint32 r = (uint32)irand(1, total), acc = 0;
	for (uint16 i = 0; i < w.size(); ++i) { acc += w[i]; if (r <= acc) return i; }
	return (uint16)(w.size() - 1);
}

static const s_fake_jobprofil* pick_jobprofile_weighted(const s_fake_profile* prof, std::string& out_name) {
	if (!prof || prof->jobprofiles.empty()) return nullptr;
	std::vector<uint16> w; w.reserve(prof->jobprofiles.size());
	for (auto& r : prof->jobprofiles) w.push_back(r.number ? r.number : 1);
	uint16 idx = pick_index_weighted_u16(w);
	if (idx == UINT16_MAX) return nullptr;
	out_name = prof->jobprofiles[idx].name;
	auto jp_ptr = fake_jobprofils_db.find(out_name);
	return jp_ptr ? jp_ptr.get() : nullptr;
}

#ifdef RENEWAL_STAT
/// Renewal status point cost formula
#define PC_STATUS_POINT_COST(low) (((low) < 100) ? (2 + ((low) - 1) / 10) : (16 + 4 * (((low) - 100) / 5)))
#else
/// Pre-Renewal status point cost formula
#define PC_STATUS_POINT_COST(low) (( 1 + ((low) + 9) / 10 ))
#endif

static inline int stat_cost_per_point(int cur_value) {
	// Coût pour passer de cur_value -> cur_value+1
	// C'est exactement ce que fait pc_need_status_point: cost(low) sommee.
	return PC_STATUS_POINT_COST(cur_value);
}

static int joblevel_cap_for_mapid(uint64 mapid) {
	switch (mapid) {
	case MAPID_NOVICE:
	case MAPID_SUPER_NOVICE:
	case MAPID_SUPER_NOVICE_E: // expanded
		return (mapid == MAPID_NOVICE) ? 9 : 99;
		// 1st et 2nd non-trans
	case MAPID_SWORDMAN: case MAPID_MAGE: case MAPID_ARCHER:
	case MAPID_ACOLYTE: case MAPID_MERCHANT: case MAPID_THIEF:
	case MAPID_KNIGHT: case MAPID_PRIEST: case MAPID_WIZARD:
	case MAPID_BLACKSMITH: case MAPID_HUNTER: case MAPID_ASSASSIN:
	case MAPID_CRUSADER: case MAPID_MONK: case MAPID_SAGE:
	case MAPID_ROGUE: case MAPID_ALCHEMIST:
	case MAPID_BARDDANCER:
	case MAPID_STAR_GLADIATOR:
	case MAPID_SOUL_LINKER:
		return 50;
		// Trans (High Novice / High First / 2-1/2-2 high)
	case MAPID_NOVICE_HIGH:
	case MAPID_SWORDMAN_HIGH: case MAPID_MAGE_HIGH: case MAPID_ARCHER_HIGH:
	case MAPID_ACOLYTE_HIGH: case MAPID_MERCHANT_HIGH: case MAPID_THIEF_HIGH:
	case MAPID_LORD_KNIGHT: case MAPID_HIGH_PRIEST: case MAPID_HIGH_WIZARD:
	case MAPID_WHITESMITH: case MAPID_SNIPER: case MAPID_ASSASSIN_CROSS:
	case MAPID_PALADIN: case MAPID_CHAMPION: case MAPID_PROFESSOR:
	case MAPID_STALKER: case MAPID_CREATOR:
	case MAPID_CLOWNGYPSY:
		return 70;
	case MAPID_NINJA:
	case MAPID_GUNSLINGER:
	case MAPID_TAEKWON:
		return 70;
	default:
		return 50;
	}
}

static std::unordered_set<std::string> g_used_names; // noms "reserves" pendant ce boot

static std::string to_lower_ascii(std::string s) {
	for (char& c : s) if ((unsigned char)c < 128) c = (char)std::tolower((unsigned char)c);
	return s;
}

std::string to_upper_ascii(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return s;
}
// Normalisation simple pour l'ensemble (approxime la collation *_ci)
static std::string norm_key(const std::string& s) {
	std::string k = s;
	// trim
	k.erase(k.begin(), std::find_if(k.begin(), k.end(), [](int ch) { return !std::isspace(ch); }));
	k.erase(std::find_if(k.rbegin(), k.rend(), [](int ch) { return !std::isspace(ch); }).base(), k.end());
	// lowercase ASCII
	k = to_lower_ascii(k);
	return k;
}

static void get_job_max_params_without_sd(uint64 class_mapid, char sex,
	int& max_str, int& max_agi, int& max_vit, int& max_int, int& max_dex, int& max_luk)
{
	max_str = max_agi = max_vit = max_int = max_dex = max_luk = 99;

	e_sex esex = (sex == 'F') ? SEX_FEMALE : SEX_MALE;
	int32 job = pc_mapid2jobid((e_mapid)class_mapid, esex);
	std::shared_ptr<s_job_info> ji = job_db.find(job);
	if (!ji) return;

	max_str = ji->max_param[PARAM_STR];
	max_agi = ji->max_param[PARAM_AGI];
	max_vit = ji->max_param[PARAM_VIT];
	max_int = ji->max_param[PARAM_INT];
	max_dex = ji->max_param[PARAM_DEX];
	max_luk = ji->max_param[PARAM_LUK];

	auto sane = [](int v){ return v > 0 ? v : 99; };
	max_str = sane(max_str); max_agi = sane(max_agi); max_vit = sane(max_vit);
	max_int = sane(max_int); max_dex = sane(max_dex); max_luk = sane(max_luk);
}

static uint32 bootstrap_max_ge_base(Sql* sql, const char* table, const char* idcol, uint32 base) {
	char* data = nullptr;
	uint32 out = base - 1;
	if (SQL_SUCCESS == Sql_Query(sql,
		"SELECT IFNULL(MAX(`%s`), %u) FROM `%s` WHERE `%s` >= %u",
		idcol, base - 1, table, idcol, base)
		&& SQL_SUCCESS == Sql_NextRow(sql)
		&& SQL_SUCCESS == Sql_GetData(sql, 0, &data, nullptr)
		&& data) {
		out = (uint32)strtoul(data, nullptr, 10);
	}
	else {
		Sql_ShowDebug(sql);
	}
	Sql_FreeResult(sql);
	return out;
}

// trim simple
static inline std::string trim_copy(std::string s) {
	auto notspace = [](int ch) { return !std::isspace(ch); };
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
	s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
	return s;
}

static inline void trim_spaces(std::string& s) {
	auto issp = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!s.empty() && issp(s.front())) s.erase(s.begin());
	while (!s.empty() && issp(s.back()))  s.pop_back();
}

// construit un chemin depuis db_path si "path" n'est pas absolu
static std::string resolve_db_path(const std::string& path) {
#ifdef _WIN32
	const bool is_abs = (path.size() > 1 && path[1] == ':') || (!path.empty() && (path[0] == '\\' || path[0] == '/'));
#else
	const bool is_abs = !path.empty() && path[0] == '/';
#endif
	if (is_abs) return path;
	return std::string(db_path) + "/" + path;
}

// lit un fichier de noms (une ligne = un nom), ignore vides/commentaires, clamp a 23
// lit un fichier de noms (une ligne = "Name[;Sex[;HairColor[;HairStyle]]]")
// - ignore vides/commentaires (# ou ; en 1ère colonne uniquement)
// - clamp nom a max_len
// - remplit out (liste de noms uniques) + meta (par nom)
static void load_names_file(const std::string& file,
	std::vector<std::string>& out,
	std::unordered_map<std::string, s_fake_name_meta>& meta,
	size_t max_len = 23)
{
	out.clear();
	meta.clear();

	std::ifstream in(file);
	if (!in.is_open()) {
		ShowWarning("FakePlayers: cannot open names file: %s\n", file.c_str());
		return;
	}

	std::string line;
	std::unordered_set<std::string> seen; // evite doublons *dans le fichier*

	while (std::getline(in, line)) {
		line = trim_copy(line);
		if (line.empty()) continue;
		// commentaire uniquement si la ligne COMMENCE par '#' ou ';'
		if (line[0] == '#' || line[0] == ';') continue;

		// split sur ';' (jusqu'a 4 tokens) : Name ; Sex ; HairColor ; HairStyle
		std::array<std::string, 4> tok{};
		size_t t = 0, start = 0;
		for (size_t i = 0; i <= line.size() && t < 4; ++i) {
			if (i == line.size() || line[i] == ';') {
				tok[t++] = trim_copy(line.substr(start, i - start));
				start = i + 1;
			}
		}

		std::string name = tok[0];
		if (name.empty()) continue;

		if (name.size() > max_len) name.resize(max_len);

		// dedup (insensible casse ASCII) sur le nom
		std::string key = norm_key(name);
		const bool first_time = seen.insert(key).second;
		if (first_time)
			out.emplace_back(name);

		// parse metas, même si doublon de nom -> la dernière occurrence l'emporte
		s_fake_name_meta m; // valeurs par défaut (tous has_* = false)
		// Sex
		if (!tok[1].empty()) {
			char s = (char)toupper((unsigned char)tok[1][0]);
			if (s == 'M' || s == 'F') {
				m.has_sex = true; m.sex = s;
			}
		}
		// HairColor
		if (!tok[2].empty()) {
			char* endp = nullptr;
			long v = strtol(tok[2].c_str(), &endp, 10);
			if (endp != tok[2].c_str()) {
				m.has_hair_col = true; m.hair_color = (int)v;
			}
		}
		// HairStyle
		if (!tok[3].empty()) {
			char* endp = nullptr;
			long v = strtol(tok[3].c_str(), &endp, 10);
			if (endp != tok[3].c_str()) {
				m.has_hair_style = true; m.hair_style = (int)v;
			}
		}

		// n'enregistre meta que si au moins 1 champ optionnel est present
		if (m.has_sex || m.has_hair_col || m.has_hair_style) {
			meta[key] = m;
		}
	}

	ShowInfo("FakePlayers: loaded %zu unique names (+meta for %zu) from %s\n",
		out.size(), meta.size(), file.c_str());
}

// Selection d'un nom depuis le profil (round-robin). Fallback si liste vide.
static std::string fake_pick_profile_name(s_fake_profile* prof, const std::string& jobprofile_id, uint32 seq) {
	const size_t MAXLEN = 23;
	if (prof && !prof->names.empty()) {
		// on parcourt circulairement jusqu'a tomber sur un nom non encore utilise
		const size_t N = prof->names.size();
		for (size_t t = 0; t < N; ++t) {
			const std::string& cand = prof->names[(prof->name_cursor + t) % N];
			std::string key = norm_key(cand);
			if (g_used_names.find(key) == g_used_names.end()) {
				prof->name_cursor = (uint32)((prof->name_cursor + t + 1) % N);
				return cand; // deja clampe a 23
			}
		}
		// si tous "occupes" dans cette passe, on tombera sur le fallback
	}
	// Fallback si pas de fichier ou tous pris : nom genere court
	std::string gen = "P" + jobprofile_id + std::to_string(seq);
	if (gen.size() > MAXLEN) gen.resize(MAXLEN);
	return gen;
}

static const s_fake_name_meta* fake_lookup_name_meta(const s_fake_profile* prof, const std::string& name) {
	if (!prof) return nullptr;
	auto it = prof->name_meta.find(norm_key(name));
	return (it == prof->name_meta.end()) ? nullptr : &it->second;
}

// --- Coordonnees walkable aleatoires dans le rect de la ville ---
// Coordonnees walkable aleatoires dans un rect.
// Si le rect est degenere (un point/ligne), on l'elargit automatiquement autour du start.
static bool pick_walkable_xy(const char* mapname,
	int sx, int sy, int ex, int ey,
	int& outx, int& outy,
	int maxtries = 120, bool check_npc = true)
{
	uint16 mindex = mapindex_name2id(mapname);
	if (!mindex) return false;

	int16 mid = map_mapindex2mapid(mindex);
	if (mid < 0) return false;

	map_data* md = map_getmapdata(mid);
	if (!md) return false;

	// Si aucun rect, utiliser toute la carte
	bool rect_missing = (sx == 0 && sy == 0 && ex == 0 && ey == 0);
	if (rect_missing) {
		sx = 0; sy = 0; ex = md->xs - 1; ey = md->ys - 1;
	}

	// Normalise: sx<=ex, sy<=ey
	if (sx > ex) std::swap(sx, ex);
	if (sy > ey) std::swap(sy, ey);

	// Si rect trop petit (point ou ligne), on elargit automatiquement autour du point central
	int w = ex - sx + 1;
	int h = ey - sy + 1;
	if (w <= 1 || h <= 1) {
		// rayon "intelligent" : 8..min(30, 10% de la map)
		int base_rx = std::max(8, std::min(30, (int)std::min(md->xs, md->ys) / 10));
		// ajoute un petit bruit pour ne pas faire toujours la meme boîte
		int jitter = rnd() % 7; // 0..6
		int rx = base_rx + jitter;
		int ry = base_rx + (rnd() % 7);

		int cx = (sx + ex) / 2;
		int cy = (sy + ey) / 2;

		sx = std::max(0, cx - rx);
		ex = std::min(md->xs - 1, cx + rx);
		sy = std::max(0, cy - ry);
		ey = std::min(md->ys - 1, cy + ry);
	}

	// echantillonnage aleatoire dans le rect elargi
	for (int k = 0; k < maxtries; ++k) {
		int x = sx + (rnd() % (ex - sx + 1));
		int y = sy + (rnd() % (ey - sy + 1));
		if (map_getcell(mid, x, y, CELL_CHKPASS) && (!check_npc || (check_npc && !map_getcell(mid, x, y, CELL_CHKNPC)))) {
			outx = x; outy = y;
			return true;
		}
	}

	// Derniere chance : balayage en spirale autour du centre
	int cx = (sx + ex) / 2;
	int cy = (sy + ey) / 2;
	int maxr = std::max({ cx - sx, ex - cx, cy - sy, ey - cy });
	for (int r = 1; r <= maxr; ++r) {
		for (int dx = -r; dx <= r; ++dx) {
			int x1 = cx + dx, y1 = cy - r;
			int x2 = cx + dx, y2 = cy + r;
			if (x1 >= sx && x1 <= ex && y1 >= sy && y1 <= ey && map_getcell(mid, x1, y1, CELL_CHKPASS) && (!check_npc || (!map_getcell(mid, x1, y1, CELL_CHKNPC)))) { outx = x1; outy = y1; return true; }
			if (x2 >= sx && x2 <= ex && y2 >= sy && y2 <= ey && map_getcell(mid, x2, y2, CELL_CHKPASS) && (!check_npc || (!map_getcell(mid, x2, y2, CELL_CHKNPC)))) { outx = x2; outy = y2; return true; }
		}
		for (int dy = -r + 1; dy <= r - 1; ++dy) {
			int x1 = cx - r, y1 = cy + dy;
			int x2 = cx + r, y2 = cy + dy;
			if (x1 >= sx && x1 <= ex && y1 >= sy && y1 <= ey && map_getcell(mid, x1, y1, CELL_CHKPASS) && (!check_npc || (!map_getcell(mid, x1, y1, CELL_CHKNPC)))) { outx = x1; outy = y1; return true; }
			if (x2 >= sx && x2 <= ex && y2 >= sy && y2 <= ey && map_getcell(mid, x2, y2, CELL_CHKPASS) && (!check_npc || (!map_getcell(mid, x2, y2, CELL_CHKNPC)))) { outx = x2; outy = y2; return true; }
		}
	}

	// echec
	return false;
}


// Suffixe unique base sur la CID en base36, tout en restant <= 23 chars
static std::string add_unique_suffix_name(const std::string& base, uint32 cid) {
	const size_t MAXLEN = 23;
	std::string suf = "P" + base36(cid);
	size_t keep = (suf.size() >= MAXLEN) ? 0 : (MAXLEN - suf.size());
	std::string head = base.substr(0, keep);
	std::string final = head + suf;
	if (final.size() > MAXLEN) final.resize(MAXLEN);
	return final;
}

// --- Choix d'une ville (pondere par Number) ---
static int16 pick_town_index_weighted(const std::vector<s_fake_town>& towns) {
	if (towns.empty()) return -1;
	uint32 total = 0;
	for (auto& t : towns) total += t.number;
	if (total == 0) return 0;
	uint32 r = rnd() % total;
	uint32 acc = 0;
	for (size_t i = 0; i < towns.size(); ++i) {
		acc += towns[i].number;
		if (r < acc) return (int)i;
	}
	return 0;
}

// --- Choix d'une ville (pondere par Number) ---
static int16 pick_field_index_weighted(const std::vector<s_fake_fieldzone>& fields) {
	if (fields.empty()) return -1;
	uint32 total = 0;
	for (auto& t : fields) total += t.number;
	if (total == 0) return 0;
	uint32 r = rnd() % total;
	uint32 acc = 0;
	for (size_t i = 0; i < fields.size(); ++i) {
		acc += fields[i].number;
		if (r < acc) return (int)i;
	}
	return 0;
}

static inline int fp_cell_passable_fast(const struct map_data* m, int16 x, int16 y) {
	// meme logique OOB que map_getcellp pour CHKPASS : hors borne => 0
	if (x < 0 || x >= m->xs - 1 || y < 0 || y >= m->ys - 1)
		return 0;

	const size_t idx = (size_t)(uint16_t)y * (size_t)m->xs + (size_t)(uint16_t)x;
	const struct mapcell c = m->cell[idx];

#ifdef CELL_NOSTACK
	if (c.cell_bl >= battle_config.custom_cell_stack_limit)
		return 0;
#endif
	return c.walkable; // booleen stocke dans le bitfield
}

int fp_get_random_coords(int16 m, int& x, int& y) {
	int16 i = 0;

	struct map_data* mapdata = map_getmapdata(m);

	int32 edge = battle_config.map_edge_size;
	for (int i = 0; i < 100; i++) {
		x = rnd_value<int16>(edge, mapdata->xs - edge - 1);
		y = rnd_value<int16>(edge, mapdata->ys - edge - 1);

		if (fp_cell_passable_fast(mapdata, x, y) &&
			(battle_config.teleport_on_portal || !npc_check_areanpc(1, m, x, y, 1))) {
			return 1;
		}
	}

	// Reduction de l'echec en elargissant la zone
	for (int i = 0; i < 50; i++) {
		x = rnd_value<int16>(0, mapdata->xs - 1);
		y = rnd_value<int16>(0, mapdata->ys - 1);
		if (fp_cell_passable_fast(mapdata, x, y) &&
			(battle_config.teleport_on_portal || !npc_check_areanpc(1, m, x, y, 1))) {
			return 1;
		}
	}

	return 0;
}

static void fake_pick_town_spot(const s_fake_profile* prof, FakeSpawnSpot& out) {
	if (prof && !prof->towns.empty()) {
		std::vector<uint16> wt; wt.reserve(prof->towns.size());
		for (auto& t : prof->towns) wt.push_back(t.number ? t.number : 1);
		uint16 ti = pick_index_weighted_u16(wt);
		if (ti != UINT16_MAX) {
			out.save_map = prof->towns[ti].map;
			out.sx = prof->towns[ti].startx; out.sy = prof->towns[ti].starty;
			out.ex = prof->towns[ti].endx;   out.ey = prof->towns[ti].endy;
		}
	}
	out.save_x = out.sx; out.save_y = out.sy;
	if (pick_walkable_xy(out.save_map.c_str(), out.sx, out.sy, out.ex, out.ey, out.save_x, out.save_y, 500, true)) {
		out.last_x = out.save_x;
		out.last_y = out.save_y;
	}
	else
		fp_get_random_coords(mapindex_name2id(out.save_map.c_str()), out.last_x, out.last_y);

}

// --- Estimation simple des points de stats totaux a distribuer ---
static uint32 estimate_total_stat_points(int base_lv) {
	return statpoint_db.get_table_point(base_lv);
}

// Repartition proportionnelle **au budget depense**, avec coût progressif + caps de job.
// - class_mapid = MAPID_* du profil (jp->job_mapid)
// - sex = 'M' / 'F' (impacte les caps via job_db)
// - jp = poids (Str/Agi/Vit/Int/Dex/Luk) tels que definis dans le YML (0 accepte = pas d'allocation)
static void distribute_stats_weighted_precise(int base_lv, uint64 class_mapid, char sex,
	const s_fake_jobprofil* jp,
	int& out_str, int& out_agi, int& out_vit,
	int& out_int, int& out_dex, int& out_luk)
{
	// 1) Budget total (garde ta table/fonction existante)
	int budget_total = estimate_total_stat_points(base_lv);
	// Stats officielles commencent a 1
	out_str = out_agi = out_vit = out_int = out_dex = out_luk = 1;

	// On retire le coût implicite de ces 6 points "gratuits" (si ta table compte depuis 1)
	int budget = budget_total - 6;
	if (budget < 0) budget = 0;

	// 2) Caps du job (sans sd)
	int cap_str, cap_agi, cap_vit, cap_int, cap_dex, cap_luk;
	get_job_max_params_without_sd(class_mapid, sex, cap_str, cap_agi, cap_vit, cap_int, cap_dex, cap_luk);

	// 3) Poids tels que dans le YML (on respecte 0 = pas d'allocation)
	int wstr = jp ? jp->str : 0;
	int wagi = jp ? jp->agi : 0;
	int wvit = jp ? jp->vit : 0;
	int wint = jp ? jp->_int : 0;
	int wdex = jp ? jp->dex : 0;
	int wluk = jp ? jp->luk : 0;

	int weights[6] = { wstr, wagi, wvit, wint, wdex, wluk };
	int* vals[6] = { &out_str,&out_agi,&out_vit,&out_int,&out_dex,&out_luk };
	int  caps[6] = { cap_str, cap_agi, cap_vit, cap_int, cap_dex, cap_luk };

	// somme des poids > 0 ?
	int total_w = 0;
	for (int i = 0; i < 6; ++i) total_w += std::max(0, weights[i]);

	// Si tout est a 0 (cas patho), fallback poids egaux sur STR/AGI/DEX
	if (total_w == 0) {
		weights[0] = weights[1] = weights[4] = 1; // STR,AGI,DEX
		total_w = 3;
	}

	// 4) Cible de depense par stat (en "points de budget", pas en +1)
	double target_spend[6] = { 0,0,0,0,0,0 };
	for (int i = 0; i < 6; ++i) {
		if (weights[i] <= 0) { target_spend[i] = 0.0; continue; }
		target_spend[i] = (double)budget * (double)weights[i] / (double)total_w;
	}

	double spent[6] = { 0,0,0,0,0,0 };

	// 5) Allocation point par point, choix = plus petit ratio (spent/target)
	//    Respecte coûts progressifs + caps + budget restant
	while (budget > 0) {
		int best = -1;
		double best_ratio = 0.0;

		// Cherche parmi les stats ponderees (weight>0) une action possible
		for (int i = 0; i < 6; ++i) {
			if (weights[i] <= 0) continue;               // pas d'allocation pour poids 0
			if (*vals[i] >= caps[i]) continue;           // au cap
			int cost = stat_cost_per_point(*vals[i]);
			if (cost > budget) continue;                 // pas assez de budget

			// ratio: si target==0 (theoriquement exclu car weight>0), protege quand meme
			double tgt = target_spend[i];
			double ratio = (tgt > 0.0) ? (spent[i] / tgt) : 1e9;

			// selection: ratio minimal prioritaire; a ratio egal, poids plus grand
			if (best == -1
				|| ratio < best_ratio
				|| (ratio == best_ratio && weights[i] > weights[best])) {
				best = i;
				best_ratio = ratio;
			}
		}

		if (best != -1) {
			int cost = stat_cost_per_point(*vals[best]);
			(*vals[best])++;
			budget -= cost;
			spent[best] += cost;
			continue;
		}

		// Fallback: si on ne peut plus investir dans stats ponderees (coûts trop chers ou caps),
		// on tente d'ecouler le budget (utile si arrondis) sans violer l'esprit:
		bool placed = false;
		for (int i = 0; i < 6; ++i) {
			if (weights[i] <= 0) continue; // ne depense pas sur poids 0 (sauf si tu veux autoriser)
			if (*vals[i] >= caps[i]) continue;
			int cost = stat_cost_per_point(*vals[i]);
			if (cost <= budget) {
				(*vals[i])++;
				budget -= cost;
				spent[i] += cost;
				placed = true;
				break;
			}
		}
		if (!placed) break; // plus rien de faisable
	}

	// 6) Clamp securite
	for (int i = 0; i < 6; ++i)
		*vals[i] = std::min(*vals[i], caps[i]);
}

// --- Helper: parse un tableau YAML de 24 nombres (0..100) ---
// NOTE: on prend NodeRef PAR VALEUR (non-const) et on utilise find_child()
static bool parse_curve_hourly_array(ryml::NodeRef curveNode, const char* key, float out24[24])
{
	if (!curveNode.valid() || !key) return false;

	// chercher l'enfant "key" sans declencher d'assert
	c4::yml::NodeRef seq = curveNode.find_child(c4::to_csubstr(key));
	if (!seq.valid())
		return false; // la cle n'existe pas -> pas d'erreur, juste "pas de profil tabule"

	if (!seq.is_seq()) {
		ShowWarning("Curve: '%s' exists but is not a sequence.\n", key);
		return false;
	}

	// lire exactement 24 scalaires
	int n = 0;
	for (const auto ch : seq.children()) {
		if (!ch.has_val()) {
			ShowWarning("Curve: '%s' entry %d has no value.\n", key, n);
			return false;
		}
		c4::csubstr s = ch.val();
		char* endp = nullptr;
		float v = (float)strtod(s.str, &endp);
		if (endp == s.str) {
			ShowWarning("Curve: '%s' entry %d is not a number.\n", key, n);
			return false;
		}
		if (v < 0.f)   v = 0.f;
		if (v > 100.f) v = 100.f;

		if (n < 24) out24[n] = v;
		++n;
	}

	if (n != 24) {
		ShowWarning("Curve: '%s' must have exactly 24 numbers (got %d).\n", key, n);
		return false;
	}

	return true;
}

static int fakecurve_target_at_clock(const s_fake_profile* prof, int hour, int minute, bool weekend, int second /*=0*/)
{
	if (!prof || !prof->curve.enabled) return 0;

	const int cap_dyn = fakecurve_capacity_for(prof, weekend);
	if (cap_dyn <= 0) return 0;

	double p = 0.0;
	if (prof->curve.use_table) {
		const float* TAB = (weekend && prof->curve.has_weekend_table)
			? prof->curve.weekend_table
			: prof->curve.hour_table;

		const int h0 = std::clamp(hour, 0, 23);
		const int h1 = (h0 + 1) % 24;

		// --- lissage a la minute (et seconde) ---
		const double w_raw = (std::clamp(minute, 0, 59) + std::clamp(second, 0, 59) / 60.0) / 60.0; // 0..1

		// (Optionnel) easing pour eviter un micro "coude" a xx:00
		// Variante 1 (cosine): C1 continu
		// const double w = 0.5 - 0.5 * std::cos(M_PI * w_raw);
		// Variante 2 (smoothstep): C1 continu
		// const double w = w_raw * w_raw * (3.0 - 2.0 * w_raw);
		// Variante 3 (lineaire, si tu preferes garder exactement ce que tu avais)
		const double w = w_raw;

		const double v0 = TAB[h0] / 100.0;
		const double v1 = TAB[h1] / 100.0;
		p = (1.0 - w) * v0 + w * v1;
	}
	else {
		// fallback sinusoïde : inclure minutes+secondes pour une phase continue
		const double h = std::clamp(hour, 0, 23)
			+ (std::clamp(minute, 0, 59) + std::clamp(second, 0, 59) / 60.0) / 60.0;
		const double phase = (h / 24.0) * 2.0 * M_PI;
		const double day_f = 0.5 * (1.0 + std::sin(phase - M_PI * 0.25));
		const double pl = std::clamp((double)prof->curve.perc_low, 0.0, 100.0);
		const double ph = std::clamp((double)prof->curve.perc_high, 0.0, 100.0);
		p = (pl + (ph - pl) * day_f) / 100.0;
	}

	// NB: si tes tables "WeekendProfile" integrent deja le boost, ne multiplie pas p ici.
	// if (weekend) p *= prof->curve.weekend_boost;

	const int raw = (int)std::lround(cap_dyn * p);
	int target = std::max(0, std::min(raw, cap_dyn));

	return target;
}



void fakecurve_print_24h(const s_fake_profile* prof) {
	if (!prof) return;

	time_t t = time(nullptr); tm lt{};
#ifdef _WIN32
	localtime_s(&lt, &t);
#else
	localtime_r(&t, &lt);
#endif
	const bool weekend = (lt.tm_wday == 0 || lt.tm_wday == 6);

	const int cap_dyn = fakecurve_capacity_for(prof, weekend);
	ShowInfo("[Curve 24h] profile='%s' weekend=%d cap_dyn=%d base=%d low=%d%% high=%d%% boost=%.2f use_table=%d weekend_table=%d\n",
		prof->id.c_str(), (int)weekend, cap_dyn,
		fakecurve_base_number(prof),
		prof->curve.perc_low, prof->curve.perc_high,
		prof->curve.weekend_boost,
		(int)prof->curve.use_table, (int)prof->curve.has_weekend_table);

	for (int h = 0; h < 24; ++h) {
		const int tgt = fakecurve_target_at_clock(prof, h, 0, weekend, 0);
		ShowInfo("[FakeCurve] profile='%s' h=%02d -> forecast=%d\n",
			prof->id.c_str(), h, tgt);
	}
}


// -------------------- FakeChatDatabase --------------------

FakeChatDatabase fake_chat_db;

const std::string FakeChatDatabase::getDefaultLocation() {
	return std::string(db_path) + "/fake_chat.yml";
}

// -------------------- parse Intent --------------------
bool FakeChatDatabase::parseIntentNode_(const ryml::NodeRef& node) {
	std::string name;
	if (!this->asString(node, "Intent", name) || name.empty()) {
		this->invalidWarning(node, "FakeChat: missing/empty 'Intent'.");
		return false;
	}

	auto def = std::make_shared<IntentDefFC>();
	def->name = name;

	float w = 1.f;
	if (this->asFloat(node, "Weight", w)) def->weight = w;

	// Keywords (sequence)
	if (this->nodeExists(node, "Keywords")) {
		auto kn = node.find_child("Keywords");
		if (!kn.is_seq()) {
			this->invalidWarning(kn, "FakeChat: 'Keywords' must be a sequence for intent '%s'.", name.c_str());
		}
		else {
			for (const auto it : kn.children()) {
				if (!it.has_val()) continue;
				std::string s(it.val().str, it.val().len);
				def->keywords.emplace_back(std::move(s));
			}
		}
	}

	// Replies (sequence de scalars ou maps {Text, Weight})
	if (this->nodeExists(node, "Replies")) {
		auto rn = node.find_child("Replies");
		if (!rn.is_seq()) {
			this->invalidWarning(rn, "FakeChat: 'Replies' must be a sequence for intent '%s'.", name.c_str());
		}
		else {
			for (const auto it : rn.children()) {
				ReplyTemplateFC rt;
				if (it.is_map()) {
					// { Text: "...", Weight: 0.8 }
					if (!this->asString(it, "Text", rt.text) || rt.text.empty()) {
						this->invalidWarning(it, "FakeChat: Reply map missing 'Text' for intent '%s'.", name.c_str());
						continue;
					}
					float rw = 1.f;
					if (this->asFloat(it, "Weight", rw)) rt.weight = rw;
					def->replies.emplace_back(std::move(rt));
				}
				else if (it.has_val()) {
					// simple scalar
					rt.text.assign(it.val().str, it.val().len);
					rt.weight = 1.f;
					def->replies.emplace_back(std::move(rt));
				}
				else {
					this->invalidWarning(it, "FakeChat: invalid reply entry for intent '%s'.", name.c_str());
				}
			}
		}
	}

	// --- Intent-level tuning (with safe defaults + node existence checks) ---
	// Defaults from global config (FuzzyDefault) + safe zeros for thresholds.
	def->minHits = 0;
	def->minScore = 0.0f;
	{
		// Par defaut: fuzzy = cfg_.fuzzy.enabled ET intent pas dans ExcludeIntents
		bool excluded = (cfg_.fuzzy.excludeIntents.find(def->name) != cfg_.fuzzy.excludeIntents.end());
		def->allowFuzzy = (cfg_.fuzzy.enabled && !excluded);
		def->fuzzyBonus = (float)cfg_.fuzzy.bonus;
		def->maxFuzzyTokenLen = cfg_.fuzzy.maxTokenLen;
		def->maxFuzzyPerIntent = cfg_.fuzzy.maxPerIntent;
	}

	// Surcharges par intent si les cles existent dans le YAML
	int    i32 = 0;
	double dbl = 0.0;
	bool   b = false;

	if (this->nodeExists(node, "MinHits") && this->asInt32(node, "MinHits", i32)) {
		if (i32 < 0) i32 = 0;
		def->minHits = i32;
	}

	if (this->nodeExists(node, "MinScore") && this->asDouble(node, "MinScore", dbl)) {
		if (dbl < 0.0) dbl = 0.0;
		def->minScore = (float)dbl;
	}

	if (this->nodeExists(node, "AllowFuzzy") && this->asBool(node, "AllowFuzzy", b)) {
		def->allowFuzzy = b;
	}

	if (this->nodeExists(node, "FuzzyBonus") && this->asDouble(node, "FuzzyBonus", dbl)) {
		if (dbl < 0.0) dbl = 0.0;
		def->fuzzyBonus = (float)dbl;
	}

	if (this->nodeExists(node, "MaxFuzzyTokenLen") && this->asInt32(node, "MaxFuzzyTokenLen", i32)) {
		if (i32 < 0) i32 = 0;
		def->maxFuzzyTokenLen = i32;
	}

	if (this->nodeExists(node, "MaxFuzzyPerIntent") && this->asInt32(node, "MaxFuzzyPerIntent", i32)) {
		if (i32 < 0) i32 = 0;
		def->maxFuzzyPerIntent = i32;
	}


	// Dedoublonnage (le dernier gagne)
	if (this->exists(def->name)) {
		this->put(def->name, def); // override
	}
	else {
		this->put(def->name, def);
	}
	return true;
}

// -------------------- parse Config --------------------
bool FakeChatDatabase::parseConfigNode_(const ryml::NodeRef& node) {
	// Tous les champs sont optionnels (defaults sinon) :
	this->asString(node, "Language", cfg_.language);
	this->asDouble(node, "ReplyProbabilityBase", cfg_.replyProbBase);
	this->asDouble(node, "ReplyProbabilityOnMention", cfg_.replyProbMention);
	this->asInt32(node, "CooldownMsSelf", cfg_.cooldownSelfMs);
	this->asInt32(node, "CooldownMsPerPlayer", cfg_.cooldownPerPlayerMs);
	this->asInt32(node, "MaxTurnsWithSamePlayer", cfg_.maxTurnsSamePlayer);
	this->asInt32(node, "TypingSpeedCPSMin", cfg_.typingCpsMin);
	this->asInt32(node, "TypingSpeedCPSMax", cfg_.typingCpsMax);
	this->asDouble(node, "UnknownIntentFallbackWeight", cfg_.unknownFallbackWeight);

	// StateBoosts
	if (this->nodeExists(node, "StateBoosts")) {
		auto sb = node.find_child("StateBoosts");
		if (!sb.is_map()) {
			this->invalidWarning(sb, "FakeChat: 'StateBoosts' must be a map.");
		}
		else {
			parseStateBoosts_(sb, cfg_.stateBoosts);
		}
	}

	// FuzzyDefault
	if (this->nodeExists(node, "FuzzyDefault")) {
		auto fn = node.find_child("FuzzyDefault");
		bool b;
		double d;
		int i;

		if (this->asBool(fn, "Enabled", b)) cfg_.fuzzy.enabled = b;
		if (this->asDouble(fn, "Bonus", d)) cfg_.fuzzy.bonus = (float)d;
		if (this->asInt32(fn, "MaxTokenLen", i)) cfg_.fuzzy.maxTokenLen = i;
		if (this->nodeExists(fn, "ExcludeIntents")) {
			auto ex = fn.find_child("ExcludeIntents");
			for (auto it : ex.children()) if (it.has_val()) {
				std::string s(it.val().str, it.val().len);
				std::transform(s.begin(), s.end(), s.begin(), ::toupper);
				cfg_.fuzzy.excludeIntents.insert(std::move(s));
			}
		}
		if (this->asInt32(fn, "MaxPerIntent", i)) cfg_.fuzzy.maxPerIntent = i;
	}

	// TieBreakOrder
	if (this->nodeExists(node, "TieBreakOrder")) {
		auto tn = node.find_child("TieBreakOrder");
		cfg_.tieBreakOrder.clear();
		for (auto it : tn.children()) if (it.has_val()) {
			std::string s(it.val().str, it.val().len);
			cfg_.tieBreakOrder.push_back(std::move(s));
		}
	}

	// ChannelBias
	if (this->nodeExists(node, "ChannelBias")) {
		auto cb = node.find_child("ChannelBias");
		for (auto it : cb.children()) {
			if (!it.has_key() || !it.is_map()) continue;
			std::string intent(it.key().str, it.key().len);
			auto& dst = cfg_.channelBias[intent]; // "Default" ou nom d'intent
			double g = 0, p = 0;
			if (this->asDouble(it, "GLOBAL", g)) dst.pGlobal = g;
			if (this->asDouble(it, "PM", p))     dst.pPM = p;
		}
	}

	return true;
}

void FakeChatDatabase::parseStateBoosts_(const ryml::NodeRef& sbNode, StateBoostsMapFC& out) {
	for (auto st : sbNode.children()) {
		if (!st.has_key() || !st.is_map()) continue;
		std::string state(st.key().str, st.key().len);
		auto& intentMap = out[state];
		for (const auto in : st.children()) {
			if (!in.has_key() || !in.has_val()) continue;
			std::string intent(in.key().str, in.key().len);
			double mult = 1.0;
			if (!c4::atod(in.val(), &mult)) {
				this->invalidWarning(in, "FakeChat: invalid multiplier for StateBoosts.%s.%s", state.c_str(), intent.c_str());
				continue;
			}
			intentMap[intent] = (float)mult;
		}
	}
}

uint64 FakeChatDatabase::parseBodyNode(const ryml::NodeRef& node) {
	// Deux formats supportes dans Body:
	//  1) Intent entry:  { Intent: "GREET", Weight: 1.0, Keywords: [...], Replies: [...] }
	//  2) Config entry:  { Config: true, Language: "EN", ReplyProbabilityBase: 0.35, ... StateBoosts: {...} }

	// Cas 2) Config special
	if (this->nodeExists(node, "Config")) {
		bool isCfg = false;
		(void)this->asBool(node, "Config", isCfg);
		if (!isCfg) {
			this->invalidWarning(node, "FakeChat: 'Config' present but not true; ignoring.");
			return 0;
		}
		if (!parseConfigNode_(node)) {
			this->invalidWarning(node, "FakeChat: invalid Config entry.");
			return 0;
		}
		return 1;
	}

	// Cas 1) Intent
	if (this->nodeExists(node, "Intent")) {
		if (!parseIntentNode_(node)) {
			this->invalidWarning(node, "FakeChat: invalid Intent entry.");
			return 0;
		}
		return 1;
	}

	this->invalidWarning(node, "FakeChat: Body entry must be either an Intent or a Config node.");
	return 0;
}

void do_init_fake_chat()
{
	fake_chat_db.load();
	// Acces:
	// - intents : g_fake_chat.find("GREET") -> IntentDefFC*
	// - config  : g_fake_chat.cfg()
	const auto& cfg = fake_chat_db.cfg();
	ShowInfo("FakeChat: %zu intents (lang=%s, base=%.2f).",
		fake_chat_db.size(), cfg.language.c_str(), cfg.replyProbBase);
}

// -------------------- FakePlayersDatabase --------------------

FakePlayersDatabase fake_players_db;

const std::string FakePlayersDatabase::getDefaultLocation() {
	return std::string(db_path) + "/fake_player.yml";
}

uint64 FakePlayersDatabase::parseBodyNode(const ryml::NodeRef& node) {
	// Chaque entree Body correspond a un "Profiles: <id>"
	if (!node.has_child("Profiles")) {
		this->invalidWarning(node, "Missing 'Profiles' key.\n");
		return 0;
	}

	std::string id;
	if (!this->asString(node, "Profiles", id) || id.empty()) {
		this->invalidWarning(node["Profiles"], "Invalid profile id.\n");
		return 0;
	}

	std::shared_ptr<s_fake_profile> p = std::make_shared<s_fake_profile>();
	p->id = id;

	uint32 u;
	if (this->asUInt32(node, "LevelMin", u)) {
		if (u > MAX_LEVEL) {
			this->invalidWarning(node["EquipLevelMin"], "Minimum level %d exceeds MAX_LEVEL (%d), capping to MAX_LEVEL.\n", u, MAX_LEVEL);
			u = 1;
		}
		p->levelMin = (uint16)std::min<uint32>(u, UINT16_MAX);
	}
	else { p->levelMin = 1; } // Par defaut, 1

	if (this->asUInt32(node, "LevelMax", u)) {
		if (u > MAX_LEVEL) {
			this->invalidWarning(node["EquipLevelMin"], "Maximum level %d exceeds MAX_LEVEL (%d), capping to MAX_LEVEL.\n", u, MAX_LEVEL);
			u = MAX_LEVEL;
		}
		p->levelMax = (uint16)std::min<uint32>(u, UINT16_MAX);
	}
	else { p->levelMax = MAX_LEVEL; } // Par defaut, MAX_LEVEL

	// Curve (optionnel)
	if (this->nodeExists(node, "Curve")) {
		auto cn = node["Curve"]; // <-- pas const

		if (this->nodeExists(cn, "Enabled"))
			this->asBool(cn, "Enabled", p->curve.enabled);
		else
			p->curve.enabled = false;

		uint32 u = 0;
		if (this->nodeExists(cn, "SmoothingMinutes")) {
			if(this->asUInt32(cn, "SmoothingMinutes", u))
				p->curve.smoothing_minutes = (uint16)u;
		}
		else
			p->curve.smoothing_minutes = 60;

		float fb = 0.0f;
		if (this->nodeExists(cn, "WeekendBoost")) {
			if(this->asFloat(cn, "WeekendBoost", fb))
				p->curve.weekend_boost = (double)fb;
		}
		else
			p->curve.weekend_boost = 1.0;
			

		// On autorise >100
		if (this->nodeExists(cn, "PercLow")) {
			if (this->asUInt32(cn, "PercLow", u))  p->curve.perc_low = (uint16)u;
		}
		else
			p->curve.perc_low = 0;

		if (this->nodeExists(cn, "PercHigh")) {
			if (this->asUInt32(cn, "PercHigh", u)) p->curve.perc_high = (uint16)u;
		}
		else
			p->curve.perc_high = 0;

		if (p->curve.perc_low > p->curve.perc_high)
			std::swap(p->curve.perc_low, p->curve.perc_high);

		// Profils horaires
		bool okH = parse_curve_hourly_array(cn, "HourlyProfile", p->curve.hour_table);
		if (okH) p->curve.use_table = true;

		bool okWH = parse_curve_hourly_array(cn, "WeekendProfile", p->curve.weekend_table);
		if (okWH) p->curve.has_weekend_table = true;

		if (p->curve.use_table) {
			ShowInfo("Curve: profile '%s' uses HourlyProfile (weekend=%d)\n",
				p->id.c_str(), (int)p->curve.has_weekend_table);
		}
	}

	// JobProfiles (liste)
	if (this->nodeExists(node, "JobProfiles")) {
		for (const ryml::NodeRef& jn : node["JobProfiles"]) {
			s_fake_jobprofile_ref ref;

			if (!this->asString(jn, "JobProfile", ref.name))
				continue;

			if (ref.name.empty()) {
				this->invalidWarning(jn, "Missing JobProfile/Job name.\n");
				continue;
			}
			uint32 num = 0;
			this->asUInt32(jn, "Number", num);
			ref.number = (uint16)std::min<uint32>(num, UINT16_MAX);
			p->jobprofiles.emplace_back(std::move(ref));
		}
	}
	else {
		this->invalidWarning(node, "Profile '%s' has no JobProfiles list.\n", id.c_str());
	}

	// Towns (liste)
	if (this->nodeExists(node, "Towns")) {
		for (const ryml::NodeRef& tn : node["Towns"]) {
			s_fake_town t;
			if (!this->asString(tn, "Map", t.map) || t.map.empty()) {
				this->invalidWarning(tn, "Town without Map.\n");
				continue;
			}
			uint16 mapindex = mapindex_name2id(t.map.c_str());
			if (mapindex == 0) {
				this->invalidWarning(tn, "Unknown map \"%s\".\n", t.map.c_str());
				return 0;
			}

			int16 mapid = map_mapindex2mapid(mapindex);

			if (mapid < 0) {
				this->invalidWarning(tn, "Unknown mapid \"%s\".\n", t.map.c_str());
				return 0;
			}

			uint32 num = 0;
			this->asUInt32(tn, "Number", num);
			t.number = (uint16)std::min<uint32>(num, UINT16_MAX);

			// Coordonnees optionnelles
			this->asUInt16(tn, "Startx", t.startx);
			this->asUInt16(tn, "Starty", t.starty);
			this->asUInt16(tn, "Endx", t.endx);
			this->asUInt16(tn, "Endy", t.endy);

			if (map_getcell(mapid, t.startx, t.starty, CELL_CHKNOPASS)) {
				this->invalidWarning(tn, "Start x %d and y %d in town \"%s\" is not walkable.\n", t.startx, t.starty, t.map.c_str());
				return 0;
			}

			p->towns.emplace_back(std::move(t));
		}
	}
	else {
		this->invalidWarning(node, "Profile '%s' has no Towns.\n", id.c_str());
	}

	// Names (fichier liste de noms) - optionnel mais recommande
	if (this->nodeExists(node, "Names")) {
		this->asString(node, "Names", p->namesFile);
		if (!p->namesFile.empty()) {
			std::string full = resolve_db_path(p->namesFile);
			load_names_file(full, p->names, p->name_meta, /*max_len=*/23);
			if (p->names.empty()) {
				ShowWarning("FakePlayers: names file '%s' is empty for profile '%s'\n", full.c_str(), p->id.c_str());
			}
		}
		else {
			ShowWarning("FakePlayers: empty Names path for profile '%s'\n", p->id.c_str());
		}
	}
	else {
		ShowWarning("FakePlayers: profile '%s' has no Names key; will fallback to generated names.\n", p->id.c_str());
	}

	// Behaviors
	if (this->nodeExists(node, "Behaviors")) {
		for (const ryml::NodeRef& bn : node["Behaviors"]) {
			s_fake_behavior b;
			this->asString(bn, "Behavior", b.behavior);
			uint32 num = 0; this->asUInt32(bn, "Number", num);
			b.number = (uint16)std::min<uint32>(num, UINT16_MAX);
			if (!b.behavior.empty())
				p->behaviors.emplace_back(std::move(b));
		}
	} else {
		this->invalidWarning(node, "Profile '%s' has no Behaviors list.\n", id.c_str());
	}

	this->put(p->id, p);
	return 1;
}


// -------------------- FakeJobProfilsDatabase --------------------

FakeJobProfilsDatabase fake_jobprofils_db;

const std::string FakeJobProfilsDatabase::getDefaultLocation() {
	return std::string(db_path) + "/fake_jobprofil.yml";
}

uint64 FakeJobProfilsDatabase::parseBodyNode(const ryml::NodeRef& node) {
	if (!node.has_child("JobProfile")) {
		this->invalidWarning(node, "Missing 'JobProfile' key.\n");
		return 0;
	}

	std::string id;
	if (!this->asString(node, "JobProfile", id) || id.empty()) {
		this->invalidWarning(node["JobProfile"], "Invalid JobProfile id.\n");
		return 0;
	}

	std::shared_ptr<s_fake_jobprofil> jp = std::make_shared<s_fake_jobprofil>();
	jp->id = id;

	std::string jobName;
	if (!this->asString(node, "Job", jobName) || jobName.empty()) {
		this->invalidWarning(node["Job"], "Invalid or missing Job name in JobProfile '%s'.\n", id.c_str());
		return 0;
	}

	int64 c_job = 0, c_eaj = 0;

	// 1) D'abord la classe
	std::string jobName_job = "JOB_" + to_upper_ascii(jobName);
	if (!script_get_constant(jobName_job.c_str(), &c_job)) {
		this->invalidWarning(node["Job"],
			"Invalid Job '%s' (constant %s not found). Try JOB_NINJA/JOB_GUNSLINGER/JOB_TAEKWON.\n",
			jobName.c_str(), jobName_job.c_str());
		return 0;
	}
	jp->job_id = c_job;

	// Stats
	int64 s;
	if (this->asInt64(node, "Str", s)) jp->str = (int16)s;
	else {
		this->invalidWarning(node, "JobProfile '%s' missing 'Str' stat. Setting to 1\n", id.c_str());
		jp->str = 1;
	}
	if (this->asInt64(node, "Agi", s)) jp->agi = (int16)s;
	else {
		this->invalidWarning(node, "JobProfile '%s' missing 'Agi' stat. Setting to 1\n", id.c_str());
		jp->agi = 1;
	}
	if (this->asInt64(node, "Vit", s)) jp->vit = (int16)s;
	else {
		this->invalidWarning(node, "JobProfile '%s' missing 'Vit' stat. Setting to 1\n", id.c_str());
		jp->vit = 1;
	}
	if (this->asInt64(node, "Int", s)) jp->_int = (int16)s;
	else {
		this->invalidWarning(node, "JobProfile '%s' missing 'Int' stat. Setting to 1\n", id.c_str());
		jp->_int = 1;
	}
	if (this->asInt64(node, "Dex", s)) jp->dex = (int16)s;
	else {
		this->invalidWarning(node, "JobProfile '%s' missing 'Dex' stat. Setting to 1\n", id.c_str());
		jp->dex = 1;
	}
	if (this->asInt64(node, "Luk", s)) jp->luk = (int16)s;
	else {
		this->invalidWarning(node, "JobProfile '%s' missing 'Luk' stat. Setting to 1\n", id.c_str());
		jp->luk = 1;
	}

	if (this->nodeExists(node, "Weapon")) {
		std::string weapon;
		if (this->asString(node, "Weapon", weapon)) {
			std::shared_ptr<item_data> item = item_db.searchname(weapon.c_str());
			if (item == nullptr) {
				this->invalidWarning(node, "Invalid weapon name '%s', skipping.\n", weapon.c_str());
			}
			else {
				jp->weapon_id = item->nameid;
			}
		}
	}
	else
		jp->weapon_id = 0;

	if (this->nodeExists(node, "Shield")) {
		std::string shield;
		if (this->asString(node, "Shield", shield)) {
			std::shared_ptr<item_data> item = item_db.searchname(shield.c_str());
			if (item == nullptr) {
				this->invalidWarning(node, "Invalid shield name '%s', skipping.\n", shield.c_str());
			}
			else {
				jp->shield_id = item->nameid;
			}
		}
	} else
		jp->shield_id = 0;

	if (this->nodeExists(node, "Arrow")) {
		this->asBool(node, "Arrow", jp->arrow);
	} else
		jp->arrow = false;

	// Skills (optionnel)
	if (this->nodeExists(node, "Skills")) {
		for (const ryml::NodeRef& sn : node["Skills"]) {
			s_fake_skill sk;
			uint32 num = 0;

			std::string skill_name;
			if(!this->asString(sn, "Skill", skill_name) || skill_name.empty()) {
				this->invalidWarning(sn, "Missing or empty Skill name, skipping.\n");
				continue;
			}
			this->asString(sn, "Skill", skill_name);

			uint16 skill_id = skill_name2id(skill_name.c_str());

			if (skill_id == 0) {
				this->invalidWarning(node["Skill"], "Invalid skill name \"%s\", skipping.\n", skill_name.c_str());
				continue;
			}

			sk.skillid = skill_id;

			if (!this->asUInt32(sn, "Number", num)) {
				this->invalidWarning(sn, "Missing 'Number' for skill '%s', setting to 1.\n", skill_name.c_str());
				num = 1;
			}

			sk.number = (uint16)std::min<uint32>(num, UINT16_MAX);
			jp->skills.emplace_back(std::move(sk));
		}
	}

	// --- Headgears ---
	const char* hgKey = nullptr;
	if (this->nodeExists(node, "Headgears"))
		hgKey = "Headgears";

	if (hgKey){
		const ryml::NodeRef H = node[c4::to_csubstr(hgKey)];

		// ---------- Top ----------
		if (this->nodeExists(H, "Top")) {
			const ryml::NodeRef n = H.find_child("Top");
			if (!n.is_seq()) {
				this->invalidWarning(n, "Headgears: 'Top' must be a sequence.");
			}
			else {
				size_t scanned = 0, pushed = 0;
				for (const ryml::NodeRef& hn : n.children()) {
					++scanned;
					s_fake_hat h{}; uint32 num = 0; std::string itemName;
					if (!this->asString(hn, "Item", itemName)) {
						this->invalidWarning(hn, "Headgears 'Top': missing 'Item', skipping.");
						continue;
					}
					if (itemName == "none") {
						h.nameid = 0;
					}
					else {
						std::shared_ptr<item_data> it = item_db.searchname(itemName.c_str());
						if (!it) {
							this->invalidWarning(hn, "Headgears 'Top': invalid item '%s', skipping.", itemName.c_str());
							continue;
						}
						h.nameid = it->nameid;
					}
					if (!this->asUInt32(hn, "Number", num)) {
						this->invalidWarning(hn, "Headgears 'Top' item '%s': missing 'Number', defaulting to 1.", itemName.c_str());
						num = 1;
					}
					h.number = (uint16)std::min<uint32>(num, UINT16_MAX);
					jp->hatsTop.emplace_back(std::move(h));
					++pushed;
				}
			}
		}

		// ---------- Mid ----------
		if (this->nodeExists(H, "Mid")) {
			const ryml::NodeRef n = H.find_child("Mid");
			if (!n.is_seq()) {
				this->invalidWarning(n, "Headgears: 'Mid' must be a sequence.");
			}
			else {
				size_t scanned = 0, pushed = 0;
				for (const ryml::NodeRef& hn : n.children()) {
					++scanned;
					s_fake_hat h{}; uint32 num = 0; std::string itemName;
					if (!this->asString(hn, "Item", itemName)) {
						this->invalidWarning(hn, "Headgears 'Mid': missing 'Item', skipping.");
						continue;
					}
					if (itemName == "none") {
						h.nameid = 0;
					}
					else {
						std::shared_ptr<item_data> it = item_db.searchname(itemName.c_str());
						if (!it) {
							this->invalidWarning(hn, "Headgears 'Mid': invalid item '%s', skipping.", itemName.c_str());
							continue;
						}
						h.nameid = it->nameid;
					}
					if (!this->asUInt32(hn, "Number", num)) {
						this->invalidWarning(hn, "Headgears 'Mid' item '%s': missing 'Number', defaulting to 1.", itemName.c_str());
						num = 1;
					}
					h.number = (uint16)std::min<uint32>(num, UINT16_MAX);
					jp->hatsMid.emplace_back(std::move(h));
					++pushed;
				}
			}
		}

		// ---------- Low ----------
		if (this->nodeExists(H, "Low")) {
			const ryml::NodeRef n = H.find_child("Low");
			if (!n.is_seq()) {
				this->invalidWarning(n, "Headgears: 'Low' must be a sequence.");
			}
			else {
				size_t scanned = 0, pushed = 0;
				for (const ryml::NodeRef& hn : n.children()) {
					++scanned;
					s_fake_hat h{}; uint32 num = 0; std::string itemName;
					if (!this->asString(hn, "Item", itemName)) {
						this->invalidWarning(hn, "Headgears 'Low': missing 'Item', skipping.");
						continue;
					}
					if (itemName == "none") {
						h.nameid = 0;
					}
					else {
						std::shared_ptr<item_data> it = item_db.searchname(itemName.c_str());
						if (!it) {
							this->invalidWarning(hn, "Headgears 'Low': invalid item '%s', skipping.", itemName.c_str());
							continue;
						}
						h.nameid = it->nameid;
					}
					if (!this->asUInt32(hn, "Number", num)) {
						this->invalidWarning(hn, "Headgears 'Low' item '%s': missing 'Number', defaulting to 1.", itemName.c_str());
						num = 1;
					}
					h.number = (uint16)std::min<uint32>(num, UINT16_MAX);
					jp->hatsBot.emplace_back(std::move(h));
					++pushed;
				}
			}
		}

		// ---------- CostumeTop / CostumeMid / CostumeLow ----------
		// ---------- CostumeTop ----------
		if (this->nodeExists(H, "CostumeTop")) {
			const ryml::NodeRef n = H.find_child("CostumeTop");
			if (!n.is_seq()) {
				this->invalidWarning(n, "Headgears: 'CostumeTop' must be a sequence.");
			}
			else {
				size_t scanned = 0, pushed = 0;
				for (const ryml::NodeRef& hn : n.children()) {
					++scanned;
					s_fake_hat h{}; uint32 num = 0; std::string itemName;
					if (!this->asString(hn, "Item", itemName)) {
						this->invalidWarning(hn, "Headgears 'CostumeTop': missing 'Item', skipping.");
						continue;
					}
					if (itemName == "none") {
						h.nameid = 0;
					}
					else {
						std::shared_ptr<item_data> it = item_db.searchname(itemName.c_str());
						if (!it) {
							this->invalidWarning(hn, "Headgears 'CostumeTop': invalid item '%s', skipping.", itemName.c_str());
							continue;
						}
						h.nameid = it->nameid;
					}
					if (!this->asUInt32(hn, "Number", num)) {
						this->invalidWarning(hn, "Headgears 'CostumeTop' item '%s': missing 'Number', defaulting to 1.", itemName.c_str());
						num = 1;
					}
					h.number = (uint16)std::min<uint32>(num, UINT16_MAX);
					jp->c_hatsTop.emplace_back(std::move(h));
					++pushed;
				}
			}
		}

		// ---------- CostumeMid ----------
		if (this->nodeExists(H, "CostumeMid")) {
			const ryml::NodeRef n = H.find_child("CostumeMid");
			if (!n.is_seq()) {
				this->invalidWarning(n, "Headgears: 'CostumeMid' must be a sequence.");
			}
			else {
				size_t scanned = 0, pushed = 0;
				for (const ryml::NodeRef& hn : n.children()) {
					++scanned;
					s_fake_hat h{}; uint32 num = 0; std::string itemName;
					if (!this->asString(hn, "Item", itemName)) {
						this->invalidWarning(hn, "Headgears 'CostumeMid': missing 'Item', skipping.");
						continue;
					}
					if (itemName == "none") {
						h.nameid = 0;
					}
					else {
						std::shared_ptr<item_data> it = item_db.searchname(itemName.c_str());
						if (!it) {
							this->invalidWarning(hn, "Headgears 'CostumeMid': invalid item '%s', skipping.", itemName.c_str());
							continue;
						}
						h.nameid = it->nameid;
					}
					if (!this->asUInt32(hn, "Number", num)) {
						this->invalidWarning(hn, "Headgears 'CostumeMid' item '%s': missing 'Number', defaulting to 1.", itemName.c_str());
						num = 1;
					}
					h.number = (uint16)std::min<uint32>(num, UINT16_MAX);
					jp->c_hatsMid.emplace_back(std::move(h)); // (field name per your struct)
					++pushed;
				}
			}
		}

		// ---------- CostumeLow ----------
		if (this->nodeExists(H, "CostumeLow")) {
			const ryml::NodeRef n = H.find_child("CostumeLow");
			if (!n.is_seq()) {
				this->invalidWarning(n, "Headgears: 'CostumeLow' must be a sequence.");
			}
			else {
				size_t scanned = 0, pushed = 0;
				for (const ryml::NodeRef& hn : n.children()) {
					++scanned;
					s_fake_hat h{}; uint32 num = 0; std::string itemName;
					if (!this->asString(hn, "Item", itemName)) {
						this->invalidWarning(hn, "Headgears 'CostumeLow': missing 'Item', skipping.");
						continue;
					}
					if (itemName == "none") {
						h.nameid = 0;
					}
					else {
						std::shared_ptr<item_data> it = item_db.searchname(itemName.c_str());
						if (!it) {
							this->invalidWarning(hn, "Headgears 'CostumeLow': invalid item '%s', skipping.", itemName.c_str());
							continue;
						}
						h.nameid = it->nameid;
					}
					if (!this->asUInt32(hn, "Number", num)) {
						this->invalidWarning(hn, "Headgears 'CostumeLow' item '%s': missing 'Number', defaulting to 1.", itemName.c_str());
						num = 1;
					}
					h.number = (uint16)std::min<uint32>(num, UINT16_MAX);
					jp->c_hatsBot.emplace_back(std::move(h));
					++pushed;
				}
			}
		}
	}

	// FieldZones (optionnel)
	if (this->nodeExists(node, "FieldZones")) {
		for (const ryml::NodeRef& fn : node["FieldZones"]) {
			s_fake_fieldzone fz;
			uint32 num = 0;

			std::string mapname;

			if (!this->asString(fn, "Map", mapname)) {
				return 0;
			}

			uint16 mapindex = mapindex_name2id(mapname.c_str());

			if (mapindex == 0) {
				this->invalidWarning(fn["Map"], "Unknown map \"%s\".\n", mapname.c_str());
				return 0;
			}

			int16 mapid = map_mapindex2mapid(mapindex);

			if (mapid < 0) {
				this->invalidWarning(fn["Map"], "Unknown mapid \"%s\".\n", mapname.c_str());
				return 0;
			}

			fz.mapid = mapindex;

			if (!this->asUInt32(fn, "Number", num)) {
				this->invalidWarning(fn, "Missing 'Number' for FieldZone on map '%s', setting to 1.\n", mapname.c_str());
				num = 1;
			}
			fz.number = (uint16)std::min<uint32>(num, UINT16_MAX);
			jp->fieldzones.emplace_back(std::move(fz));
		}
	}

	std::string script;
	if (this->nodeExists(node, "Script")) {
		if (!this->asString(node, "Script", script))
			return 0;

		if (jp->script) {
			script_free_code(jp->script);
			jp->script = nullptr;
		}

		jp->script = parse_script(script.c_str(), this->getCurrentFile().c_str(), this->getLineNumber(node["Script"]), SCRIPT_IGNORE_EXTERNAL_BRACKETS);
	} else {
		jp->script = nullptr;
	}

	this->put(jp->id, jp);
	return 1;
}

static void fake_accounts_purge(void)
{
	// Avant tout DELETE, snapshot des paires dans le pool
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"INSERT IGNORE INTO `fake_id_pool`(`char_id`,`account_id`) "
		"SELECT c.`char_id`, c.`account_id` "
		"FROM `char` AS c WHERE c.`is_fake`=1"))
	{
		Sql_ShowDebug(mmysql_handle);
	}

	// 2) Transaction
	if (SQL_ERROR == Sql_Query(mmysql_handle, "START TRANSACTION")) {
		Sql_ShowDebug(mmysql_handle);
		return;
	}

	// autoattack
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `aa_items` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `aa_skills` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `aa_mobs` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `aa_common_config` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}

	// hotkey purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `hotkey` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// inventory purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `inventory` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// char_reg_str purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `char_reg_str` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// char_reg_num purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `char_reg_num` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// skill purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `skill` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// mail_attachments purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `mail_attachments` AS c JOIN `mail` AS m ON m.id = c.id JOIN `char` AS l ON l.`char_id` = m.`dest_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// mail purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `mail` AS c JOIN `char` AS l ON l.`char_id` = c.`dest_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	/* mark mails as sent from server, if a character gets deleted */
	if (SQL_ERROR == Sql_Query(mmysql_handle,
		"UPDATE `mail` AS m JOIN `char` AS c ON c.`char_id` = m.`send_id` SET m.`send_id` = 0 WHERE c.`is_fake` = 1")){
		Sql_ShowDebug(mmysql_handle);
	}
	// sc_data purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `sc_data` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// bonus_script purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `bonus_script` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// quest purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `quest` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// achievement purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `achievement` AS c JOIN `char` AS l ON l.`char_id` = c.`char_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// char purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE FROM `char` WHERE `is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// acc_reg_num purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `acc_reg_num` AS c JOIN `login` AS l ON l.`account_id` = c.`account_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// acc_reg_str purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE c FROM `acc_reg_str` AS c JOIN `login` AS l ON l.`account_id` = c.`account_id` WHERE l.`is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}
	// login purge
	if (SQL_ERROR == Sql_Query(mmysql_handle, "DELETE FROM `login` WHERE `is_fake` = 1")) {
		Sql_ShowDebug(mmysql_handle);
	}

	// 3) Commit
	if (SQL_ERROR == Sql_Query(mmysql_handle, "COMMIT")) {
		Sql_ShowDebug(mmysql_handle);
		return;
	}
}

static uint32 fake_next_account_id(uint32 base_id)
{
	char* data = nullptr;

	// On cherche le max existant dans la plage reservee, sinon on repart de base_id
	if (SQL_SUCCESS == Sql_Query(mmysql_handle,
		"SELECT IFNULL(MAX(`account_id`), %u - 1) "
		"FROM `login` WHERE `account_id` >= %u",
		base_id, base_id)
		&& SQL_SUCCESS == Sql_NextRow(mmysql_handle)
		&& SQL_SUCCESS == Sql_GetData(mmysql_handle, 0, &data, nullptr)
		&& data) {
		uint32 maxid = (uint32)strtoul(data, nullptr, 10);
		Sql_FreeResult(mmysql_handle);
		return maxid + 1;
	} else {
		Sql_ShowDebug(mmysql_handle);
	}
	Sql_FreeResult(mmysql_handle);
	return base_id;
}

// INSERT AUTO_INCREMENT sur `char` (ne PAS passer char_id)
// Renvoie 1 succes / 0 doublon de name / -1 erreur SQL
// + out_cid = LAST_INSERT_ID()
static int fake_insert_char_auto(Sql* sql,
	uint32 aid, char sex,
	const char* name, int job, int base_lvl, int job_lvl,
	int str, int agi, int vit, int in_, int dex, int luk,
	const char* last_map, int last_x, int last_y,
	const char* save_map, int save_x, int save_y,
	int body, int hair, int hair_color, int cloth_color,
	/*out*/ uint32& out_cid)
{
	out_cid = 0;

	std::string nname = clamp_and_escape(name, 23);
	std::string lmap = clamp_and_escape(last_map, 16);
	std::string smap = clamp_and_escape(save_map, 16);

	int hp = 1, max_hp = 1, sp = 1, max_sp = 1;

	if (SQL_ERROR == Sql_Query(sql,
		"INSERT IGNORE INTO `char` "
		"(`account_id`,`sex`,`name`,`class`,`base_level`,`job_level`,"
		"`str`,`agi`,`vit`,`int`,`dex`,`luk`,"
		"`hp`,`max_hp`,`sp`,`max_sp`,"
		"`last_map`,`last_x`,`last_y`,"
		"`save_map`,`save_x`,`save_y`,"
		"`body`,`hair`,`hair_color`,`clothes_color`,`is_fake`) "
		"VALUES (%u,'%c','%s',%d,%d,%d,"
		"%d,%d,%d,%d,%d,%d,"
		"%d,%d,%d,%d,"
		"'%s',%d,%d,"
		"'%s',%d,%d,"
		"%d,%d,%d,%d,1)",
		aid, sex, nname.c_str(), job, base_lvl, job_lvl,
		str, agi, vit, in_, dex, luk,
		hp, max_hp, sp, max_sp,
		lmap.c_str(), last_x, last_y,
		smap.c_str(), save_x, save_y,
		body, hair, hair_color, cloth_color))
	{
		return -1; // vraie erreur SQL
	}

	long long affected = Sql_NumRowsAffected(sql);
	if (affected == 0) {
		// nom en doublon (UNIQUE name), on n'a rien insere
		return 0;
	}

	// succes: recuperer le LAST_INSERT_ID() => char_id
	char* s_id = nullptr;
	if (SQL_ERROR == Sql_Query(sql, "SELECT LAST_INSERT_ID()")) {
		Sql_ShowDebug(sql);
		return -1;
	}
	if (SQL_SUCCESS == Sql_NextRow(sql)) {
		Sql_GetData(sql, 0, &s_id, nullptr);
		if (s_id) out_cid = (uint32)strtoul(s_id, nullptr, 10);
	}
	Sql_FreeResult(sql);

	if (out_cid == 0) {
		return -1;
	}
	return 1;
}

static int fake_insert_char_full(Sql* sql,
	uint32 cid, uint32 aid, char sex,
	const char* name, int job, int base_lvl, int job_lvl,
	int str, int agi, int vit, int in_, int dex, int luk,
	const char* last_map, int last_x, int last_y,
	const char* save_map, int save_x, int save_y,
	int body, int hair, int hair_color, int cloth_color)
{
	std::string nname = clamp_and_escape(name, 23);
	std::string lmap = clamp_and_escape(last_map, 16);
	std::string smap = clamp_and_escape(save_map, 16);

	int hp = 1, max_hp = 1, sp = 1, max_sp = 1;

	// NB: INSERT IGNORE supprime l'erreur "Duplicate entry 'name' … name_key"
	if (SQL_ERROR == Sql_Query(sql,
		"INSERT IGNORE INTO `char` "
		"(`char_id`,`account_id`,`sex`,`name`,`class`,`base_level`,`job_level`,"
		"`str`,`agi`,`vit`,`int`,`dex`,`luk`,"
		"`hp`,`max_hp`,`sp`,`max_sp`,"
		"`last_map`,`last_x`,`last_y`,"
		"`save_map`,`save_x`,`save_y`,"
		"`body`,`hair`,`hair_color`,`clothes_color`,`is_fake`) "
		"VALUES (%u,%u,'%c','%s',%d,%d,%d,"
		"%d,%d,%d,%d,%d,%d,"
		"%d,%d,%d,%d,"
		"'%s',%d,%d,"
		"'%s',%d,%d,"
		"%d,%d,%d,%d,1)",
		cid, aid, sex, nname.c_str(), job, base_lvl, job_lvl,
		str, agi, vit, in_, dex, luk,
		hp, max_hp, sp, max_sp,
		lmap.c_str(), last_x, last_y,
		smap.c_str(), save_x, save_y,
		body, hair, hair_color, cloth_color))
	{
		// vraie erreur SQL (pas un simple duplicate)
		return -1;
	}

	// si doublon (UNIQUE name), INSERT IGNORE => 0 row; sinon 1 row
	long long affected = Sql_NumRowsAffected(sql);
	if (affected == 0)
		return 0; // duplicate name_key
	return 1;
}

size_t connect_fake_players_from_init() {
	size_t started = 0;

	for (auto it = g_fake_init_infos.begin(); it != g_fake_init_infos.end(); ) {
		const FakeInitInfo& info = it->second;
		if (!chrif_isconnected())
			break;

		map_session_data* sd = nullptr;
		CREATE(sd, map_session_data, 1);   // aMalloc
		new (sd) map_session_data();       // placement new

		pc_setnewpc(sd, info.aid, info.cid, 0, gettick(),
			(info.sex == 'F') ? SEX_FEMALE : SEX_MALE, 0);

		sd->state.autotrade = 1;
		sd->status.show_equip = false;
		sd->status.zeny = 50000;
		sd->fp.is_fake_player = true;
		sd->fp.jobprofile = info.jp;
		sd->fp.prof = info.prof;

		chrif_authreq(sd, /*is_fake=*/true);

		// insertion brute, un seul owner: le core rA libérera via chrif/map_quit
		g_fake_sd_hold.emplace(info.cid, sd);

		it = g_fake_init_infos.erase(it);
		++started;
	}
	return started;
}

static bool fake_spawn_one_internal(s_fake_profile* prof,
	const s_fake_jobprofil* jp_opt,
	const char* jp_name_opt,
	bool enforce_cap)
{
	if (!prof) return false;
	if (!battle_config.feature_fake_enable) return false;
	if (enforce_cap && (int)g_fake_sd_hold.size() >= battle_config.fake_max_online)
		return false;

	// --- JobProfile ---
	const s_fake_jobprofil* jp = jp_opt;
	std::string jp_name_buf;
	if (!jp) {
		jp = pick_jobprofile_weighted(prof, jp_name_buf);
		if (!jp) {
			ShowWarning("fake_spawn: profile '%s' has no valid JobProfile.\n", prof->id.c_str());
			return false;
		}
	}
	const char* jp_name = jp_name_opt ? jp_name_opt : jp_name_buf.c_str();

	const int job = (int)jp->job_id;   // JOB_*
	const uint64 mapid = pc_jobid2mapid(job);

	// --- Compte / ID pair (POOL d'abord) ---
	uint32 aid = 0;
	uint32 pool_cid = 0, pool_aid = 0;
	bool use_pool = false;

	uint32 seq = (uint32)irand(0, 65535);

	// --- Nom --- (DEPLACE ICI, AVANT sex/stats/cosmetiques)
	std::string base_name = fake_pick_profile_name(
		const_cast<s_fake_profile*>(prof),
		jp_name,
		seq
	);

	// --- Meta pour ce nom ---
	const s_fake_name_meta* nm = fake_lookup_name_meta(prof, base_name);

	// --- Sexe (override si meta) ---
	char sex = 0;
	if (nm && nm->has_sex) sex = nm->sex;
	else                   sex = fake_random_sex();

	// 1) tenter de prendre une paire dans le pool
	if (fake_idpool_fetch(pool_cid, pool_aid)) {
		// Essayer de creer le login avec l'AID impose
		if (fake_insert_login_forced_aid(mmysql_handle, pool_aid, sex, "x", "bot@local", 0, nullptr)) {
			aid = pool_aid;
			use_pool = true;
		}
		else {
			// pool sale/obsolete -> retirer l'entree et fallback auto-inc
			int err = Sql_GetError(mmysql_handle);
			ShowWarning("FakeSpawn: pool AID=%u unusable (sql_err=%d). Dropping this pool row.\n", pool_aid, err);
			fake_idpool_consume(pool_cid);
			use_pool = false;
		}
	}

	// 2) fallback: AUTO_INCREMENT (ne PAS forcer account_id)
	if (!use_pool) {
		if (!fake_insert_login_auto(mmysql_handle, sex, "x", "bot@local", 0, aid, nullptr))
			return false;
	}

	// --- Ville & pos ---
	FakeSpawnSpot spot;

	static bool s_fake_use_staging = true;
	if (s_fake_use_staging) {
		const char* staging_pool[] = { "pvp_y_7-1", "pvp_y_7-2", "pvp_y_7-3", "pvp_y_7-4", "pvp_y_7-5" };
		const int n = (int)(sizeof(staging_pool) / sizeof(staging_pool[0]));
		const char* m = staging_pool[rnd() % n];

		map_data* md = map_getmapdata(map_mapname2mapid(m));
		spot.last_map = m;
		spot.sx = 0;  spot.sy = 0;
		spot.ex = (int16)(md->xs - 1); spot.ey = (int16)(md->ys - 1);

		if (pick_walkable_xy(spot.last_map.c_str(), spot.sx, spot.sy, spot.ex, spot.ey, spot.last_x, spot.last_y, 200, true)) {
			spot.save_map = m;
			spot.save_x = spot.last_x;
			spot.save_y = spot.last_y;
		}
		else {
			fp_get_random_coords(mapindex_name2id(spot.last_map.c_str()), spot.last_x, spot.last_y);
			spot.save_map = m;
			spot.save_x = spot.last_x;
			spot.save_y = spot.last_y;
		}
	}

	// --- Niveaux ---
	int base_lv = irand(std::max<int>(1, prof->levelMin), std::max<int>(1, prof->levelMax));
	int jl_cap = joblevel_cap_for_mapid(mapid);
	int job_lv = std::max(1, base_lv - 5);
	if (job == JOB_NOVICE) job_lv = std::min(job_lv, 9);
	job_lv = std::min(job_lv, jl_cap);

	// --- Stats ---
	int s_str = 1, s_agi = 1, s_vit = 1, s_int = 1, s_dex = 1, s_luk = 1;
	distribute_stats_weighted_precise(
		base_lv,
		mapid,
		sex,
		const_cast<s_fake_jobprofil*>(jp),
		s_str, s_agi, s_vit, s_int, s_dex, s_luk
	);
	s_vit += 30;

	// --- Cosmetiques ---
	int hair = irand(0, 28);
	int hair_color = irand(0, 8);

	// Overrides si présents
	if (nm && nm->has_hair_style)
		hair = std::clamp(nm->hair_style, 0, MAX_HAIR_STYLE - 1);
	if (nm && nm->has_hair_col)
		hair_color = std::clamp(nm->hair_color, 0, MAX_HAIR_COLOR - 1);
	//int cloth_color = irand(0, MAX_CLOTH_COLOR - 1);
	int cloth_color = 0;

	// --- INSERT char (retry sur doublon de nom) ---
	const int MAX_RETRY = 50;

	for (int r = 0; r < MAX_RETRY; ++r) {
		// Pour le suffixe en cas de doublon, on utilise un "pepper" stable par tentative:
		// - si pool: le CID du pool
		// - sinon: l'AID qui vient d'être genere par AUTO_INCREMENT (suffisamment unique)
		uint32 pepper = use_pool ? pool_cid : aid;
		std::string candidate;
		if (r == 0)
			candidate = base_name;
		else {
			if (prof && !prof->names.empty()) {
				// On essaie un autre nom du profil.
				// fake_pick_profile_name ne reproposera pas les noms marqués dans g_used_names.
				candidate = fake_pick_profile_name(
					const_cast<s_fake_profile*>(prof),
					jp_name,
					seq + (uint32)r  // utile pour le fallback P<jobprofile_id><seq> si tous les noms de profil sont épuisés
				);
			}
			else {
				// Pas de prof / plus de noms → on garde la logique suffixe
				uint32 pepper = use_pool ? pool_cid : aid;
				candidate = add_unique_suffix_name(base_name, pepper + (uint32)r);
			}
		}

		// 2) Normalisation + filtre local
		std::string norm = norm_key(candidate);
		if (g_used_names.count(norm) != 0) {
			continue;
		}

#if PACKETVER_MAIN_NUM >= 20231220
		const int body = job;
#else
		const int body = 0;
#endif

		int ins = -1;
		uint32 cid_assigned = 0;

		if (use_pool) {
			ins = fake_insert_char_full(mmysql_handle,
				pool_cid, aid, sex,
				candidate.c_str(), job, base_lv, job_lv,
				s_str, s_agi, s_vit, s_int, s_dex, s_luk,
				spot.last_map.c_str(), spot.last_x, spot.last_y,
				spot.save_map.c_str(), spot.save_x, spot.save_y,
				body, hair, hair_color, cloth_color);
			if (ins == 1) cid_assigned = pool_cid;
		}
		else {
			// cas AUTO_INCREMENT: ne PAS fournir char_id; recuperer LAST_INSERT_ID()
			ins = fake_insert_char_auto(mmysql_handle,
				aid, sex,
				candidate.c_str(), job, base_lv, job_lv,
				s_str, s_agi, s_vit, s_int, s_dex, s_luk,
				spot.last_map.c_str(), spot.last_x, spot.last_y,
				spot.save_map.c_str(), spot.save_x, spot.save_y,
				body, hair, hair_color, cloth_color,
				cid_assigned /* OUT: le cid auto */);
		}

		if (ins == 1) {
			// succes
			g_used_names.insert(norm);
			fake_store_init_info(aid, cid_assigned, sex,
				const_cast<s_fake_jobprofil*>(jp),
				const_cast<s_fake_profile*>(prof));

			// si on a utilise le pool, on CONSOMME l'entree maintenant
			if (use_pool) {
				if (!fake_idpool_consume(pool_cid)) {
					ShowWarning("FakeSpawn: could not consume pool row CID=%u after success.\n", pool_cid);
				}
			}

			ShowInfo("FakeSpawn OK: '%s' prof='%s' jp='%s' job=%d map=%s (%d,%d)%s\n",
				candidate.c_str(), prof->id.c_str(), jp_name, job, spot.last_map, spot.last_x, spot.last_y,
				use_pool ? " [POOL]" : "");

			return true;
		}
		else if (ins == 0) {
			g_used_names.insert(norm);
			continue;
		}
		else { // ins == -1 -> vraie erreur SQL
			Sql_ShowDebug(mmysql_handle);
			break;
		}
	}

	return false;
}

void fake_warp_to_town(map_session_data* sd) {
	if (!sd) return;
	// --- Ville & pos ---
	FakeSpawnSpot spot;
	fake_pick_town_spot(sd->fp.prof, spot);

	memset(sd->status.save_point.map, '\0', sizeof(sd->status.save_point.map));
	safestrncpy(sd->status.save_point.map, spot.save_map.c_str(), sizeof(sd->status.save_point.map) );
	sd->status.save_point.x = spot.last_x;
	sd->status.save_point.y = spot.last_y;

	pc_setpos(sd, mapindex_name2id(sd->status.save_point.map), sd->status.save_point.x, sd->status.save_point.y, CLR_TELEPORT); // return to save point

	pc_delinvincibletimer(sd);
	clif_parse_LoadEndAck(0, sd);

	map_data* mapdata = map_getmapdata(sd->m);
	if (map_getcell(sd->m, sd->x, sd->y, CELL_CHKSTACK)) {
		int stepMin = battle_config.fake_idle_step_min, stepMax = battle_config.fake_idle_step_max;
		if (stepMax < stepMin) stepMax = stepMin;
		int step = stepMin + (rnd() % (stepMax - stepMin + 1));
		int tx = sd->x, ty = sd->y;
		int sx = 0, sy = 0;
		int ex = 0, ey = 0;
		for (auto& town : sd->fp.prof->towns) {
			if (std::strncmp(town.map.c_str(), map->name, MAP_NAME_LENGTH) == 0) {
				sx = (tx - step) < town.startx ? town.startx : (tx - step);
				sy = (ty + step) > town.starty ? town.starty : (ty + step);
				ex = (tx + step) > town.endx ? town.endx : (tx + step);
				ey = (ty - step) > town.endy ? town.endy : (ty - step);
				break;
			}
		}

		if (pick_walkable_xy(map->name, sx, sy, ex, ey, tx, ty, /*tries*/500, true)) {
			if (map_getcell(sd->m, tx, ty, CELL_CHKPASS) && !map_getcell(sd->m, tx, ty, CELL_CHKNPC))
				unit_walktoxy(sd, tx, ty, 1);
		}
	}
	else {
		int r2 = rnd() % 4;
		if (r2 == 0 && sd->fp.behavior != BEHAVIOR_TRAINING) {
			pc_setsit(sd);
			skill_sit(sd, 1);
			clif_sitting(*sd);
		}
	}
}

int fakeplayer_is_attack(uint16 skill_id) {
	auto skill = skill_db.find(skill_id);
	if (!skill) return 0;

	e_cast_type type = skill_get_casttype(skill_id);
	if ((skill->skill_type != BF_NONE
		&& skill->inf != INF_PASSIVE_SKILL
		&& !skill_get_nk(skill_id, NK_NODAMAGE)
		&& (type != CAST_NODAMAGE || skill->inf & INF_SELF_SKILL)
		)
		// Add skills like the following lines to force the skill to show in the list
		|| skill->nameid == KN_BRANDISHSPEAR
		|| skill->nameid == CG_TAROTCARD
		|| skill->nameid == TK_JUMPKICK
		|| skill->nameid == AL_HEAL
		) {
		return 1; // attack skill
	}
	else if ((type == CAST_NODAMAGE
		&& skill->inf & (INF_SUPPORT_SKILL | INF_SELF_SKILL)
		&& skill_get_sc(skill->nameid) != SC_NONE)
		|| skill->nameid == MO_CALLSPIRITS
		|| skill->nameid == CH_SOULCOLLECT
		|| skill->nameid == SA_AUTOSPELL) {

		return 2; // buff skill
	}
	return 0;
}

TIMER_FUNC(fakeplayer_init_timer) {
	map_session_data* sd = map_id2sd(id);

	if (!sd)
		return 0;

	sd->fp.fake_player_timer = INVALID_TIMER;

	if (sd->fp.is_fake_player) {
		sd->fp.last_tp = gettick();

		fake_wipe_inv_and_equips(sd);

		/*
		struct s_fake_profile* prof = sd->fp.prof;
		struct s_fake_jobprofil* jp = sd->fp.jobprofile;
		ShowError("Check: sd id %d name %s class %d prof=%s jp=%s \n", sd->id, sd->status.name, sd->class_,
			prof ? prof->id.c_str() : "(null)",
			jp ? jp->id.c_str() : "(null)");
		*/

		// Genere un nombre aleatoire : 4 (50%), 3 (25%), 5 (25%)
		int r = rnd() % 4; // valeurs possibles : 0, 1, 2, 3
		if (r == 0)
			sd->ud.dir = 3;
		else if (r == 1)
			sd->ud.dir = 5;
		else
			sd->ud.dir = 4;
		unit_setdir(sd, sd->ud.dir, true);

		r = rnd() % 6;
		if (r == 0)
			sd->head_dir = 1;
		else if (r == 1)
			sd->head_dir = 2;
		else
			sd->head_dir = 0;
		clif_changed_dir(*sd, AREA_WOS);

		// 3.3 Field (ponderee) + coords walkables
		if (sd->fp.jobprofile && sd->fp.prof) {
			int total = 0;
			for (auto& b : sd->fp.prof->behaviors)
				total += b.number;

			r = rnd() % total; // tirage entre 0 et total-1

			int cumulative = 0;
			for (auto& b : sd->fp.prof->behaviors) {
				cumulative += b.number;
				if (r < cumulative) {
					if (b.behavior == "grind") {
						sd->fp.behavior = BEHAVIOR_GRIND;
						uint16 field_idx = pick_field_index_weighted(sd->fp.jobprofile->fieldzones);
						if (field_idx >= 0) {
							pc_setpos(sd, sd->fp.jobprofile->fieldzones[field_idx].mapid, 0, 0, CLR_TELEPORT);

							pc_delinvincibletimer(sd);
							clif_parse_LoadEndAck(0, sd);
						}
					}
					else if (b.behavior == "afk_town") {
						sd->fp.behavior = BEHAVIOR_TOWN;
						fake_warp_to_town(sd);
					}
					else if (b.behavior == "training_ground") {
						sd->status.clothes_color = 0;
						sd->vd.look[LOOK_CLOTHES_COLOR] = 0;
						sd->fp.behavior = BEHAVIOR_TRAINING;
						sd->fp.tg.step = TGStep::ARRIVAL;
						sd->fp.tg.nextActionTick = 0;
						sd->fp.tg.active = true;
						fake_warp_to_town(sd);
					}
					break;
				}
			}

			// weapon
			if (sd->fp.jobprofile->weapon_id != 0) {
				struct item item_tmp = {};

				item_tmp.nameid = sd->fp.jobprofile->weapon_id;
				item_tmp.identify = 1;
				item_tmp.bound = 0;
				char flag = 0;
				if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
					clif_additem(sd, 0, 0, flag);
				}
				pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
			}

			// shield 
			if (sd->fp.jobprofile->shield_id != 0) {
				struct item item_tmp = {};

				item_tmp.nameid = sd->fp.jobprofile->shield_id;
				item_tmp.identify = 1;
				item_tmp.bound = 0;
				char flag = 0;
				if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
					clif_additem(sd, 0, 0, flag);
				}
				pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));

			}

			// arrow
			if (sd->fp.jobprofile->arrow) {
				struct item item_tmp = {};

				int base = sd->class_ & MAPID_FIRSTMASK;
				if (base == MAPID_GUNSLINGER)
					item_tmp.nameid = 13200; // Bullet
				else
					item_tmp.nameid = 1770; // Iron Arrow
				item_tmp.identify = 1;
				item_tmp.bound = 0;
				char flag = 0;
				if ((flag = pc_additem(sd, &item_tmp, 10, LOG_TYPE_COMMAND))) {
					clif_additem(sd, 0, 0, flag);
				}
				pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
			}

			// ---- TOP ----
			total = 0;
			for (auto& b : sd->fp.jobprofile->hatsTop)
				total += b.number;
			if (total > 0) {
				r = rnd() % total;
				cumulative = 0;
				for (auto& b : sd->fp.jobprofile->hatsTop) {
					cumulative += b.number;
					if (r < cumulative) {
						if (b.nameid == 0) break; // none
						struct item item_tmp = {};
						item_tmp.nameid = b.nameid;
						item_tmp.identify = 1;
						item_tmp.bound = 0;
						char flag = 0;
						if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
							clif_additem(sd, 0, 0, flag);
						}
						pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
						break;
					}
				}
			}

			// ---- MID ----
			total = 0;
			for (auto& b : sd->fp.jobprofile->hatsMid)
				total += b.number;
			if (total > 0) {
				r = rnd() % total;
				cumulative = 0;
				for (auto& b : sd->fp.jobprofile->hatsMid) {
					cumulative += b.number;
					if (r < cumulative) {
						if (b.nameid == 0) break; // none
						struct item item_tmp = {};
						item_tmp.nameid = b.nameid;
						item_tmp.identify = 1;
						item_tmp.bound = 0;
						char flag = 0;
						if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
							clif_additem(sd, 0, 0, flag);
						}
						pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
						break;
					}
				}
			}

			// ---- LOW ----
			total = 0;
			for (auto& b : sd->fp.jobprofile->hatsBot)
				total += b.number;
			if (total > 0) {
				r = rnd() % total;
				cumulative = 0;
				for (auto& b : sd->fp.jobprofile->hatsBot) {
					cumulative += b.number;
					if (r < cumulative) {
						if (b.nameid == 0) break; // none
						struct item item_tmp = {};
						item_tmp.nameid = b.nameid;
						item_tmp.identify = 1;
						item_tmp.bound = 0;
						char flag = 0;
						if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
							clif_additem(sd, 0, 0, flag);
						}
						pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
						break;
					}
				}
			}

			// ---- COSTUME TOP ----
			total = 0;
			for (auto& b : sd->fp.jobprofile->c_hatsTop)
				total += b.number;
			if (total > 0) {
				r = rnd() % total;
				cumulative = 0;
				for (auto& b : sd->fp.jobprofile->c_hatsTop) {
					cumulative += b.number;
					if (r < cumulative) {
						if (b.nameid == 0) break; // none
						struct item item_tmp = {};
						item_tmp.nameid = b.nameid;
						item_tmp.identify = 1;
						item_tmp.bound = 0;
						char flag = 0;
						if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
							clif_additem(sd, 0, 0, flag);
						}
						pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
						break;
					}
				}
			}

			// ---- COSTUME MID ---- (champ avec le 's' double : c_hatsMid)
			total = 0;
			for (auto& b : sd->fp.jobprofile->c_hatsMid)
				total += b.number;
			if (total > 0) {
				r = rnd() % total;
				cumulative = 0;
				for (auto& b : sd->fp.jobprofile->c_hatsMid) {
					cumulative += b.number;
					if (r < cumulative) {
						if (b.nameid == 0) break; // none
						struct item item_tmp = {};
						item_tmp.nameid = b.nameid;
						item_tmp.identify = 1;
						item_tmp.bound = 0;
						char flag = 0;
						if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
							clif_additem(sd, 0, 0, flag);
						}
						pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
						break;
					}
				}
			}

			// ---- COSTUME LOW ----
			total = 0;
			for (auto& b : sd->fp.jobprofile->c_hatsBot)
				total += b.number;
			if (total > 0) {
				r = rnd() % total;
				cumulative = 0;
				for (auto& b : sd->fp.jobprofile->c_hatsBot) {
					cumulative += b.number;
					if (r < cumulative) {
						if (b.nameid == 0) break; // none
						struct item item_tmp = {};
						item_tmp.nameid = b.nameid;
						item_tmp.identify = 1;
						item_tmp.bound = 0;
						char flag = 0;
						if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
							clif_additem(sd, 0, 0, flag);
						}
						pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
						break;
					}
				}
			}

			pc_allskillup(sd); // all skills

			/*
			if (pc_checkskill(sd, AC_VULTURE) > 0 && sd->status.base_level < 50) {
				uint16 idx = skill_get_index(AC_VULTURE);
				sd->status.skill[idx].lv = 5;
			}
			*/
			
			sd->status.skill_point = 0; // 0 skill points
			if(battle_config.fake_loot_priority)
				sd->aa.prio_item_config = true;
			if (battle_config.fake_reject_pm)
				sd->state.ignoreAll = 1; // ignore pm message

			for (auto& sk : sd->fp.jobprofile->skills) {
				uint16 skill_lv = pc_checkskill(sd, sk.skillid);
				if (skill_lv <= 0)
					continue;

				uint16 skill_id = sk.skillid;

				int is_attack = fakeplayer_is_attack(sk.skillid);
				if (!is_attack) continue;

				if (fakeplayer_is_attack(sk.skillid) == 1) {
					auto itAttackSkill = std::find_if(sd->aa.autoattackskills.begin(), sd->aa.autoattackskills.end(), [skill_id](const s_autoattackskills& v) {
						return v.skill_id == skill_id;
						});

					if (itAttackSkill != sd->aa.autoattackskills.end()) {
						itAttackSkill->is_active = true;
						itAttackSkill->skill_lv = skill_lv;
					}
					else {
						s_autoattackskills attackskills = { true, skill_id, skill_lv };
						sd->aa.autoattackskills.push_back(attackskills);
					}
				}
				else {
					auto itBuffSkill = std::find_if(sd->aa.autobuffskills.begin(), sd->aa.autobuffskills.end(), [skill_id](const s_autobuffskills& v) {
						return v.skill_id == skill_id;
						});

					if (itBuffSkill != sd->aa.autobuffskills.end()) {
						itBuffSkill->is_active = true;
						itBuffSkill->skill_lv = skill_lv;
					}
					else {
						s_autobuffskills buffskills = { true, skill_id, skill_lv };
						sd->aa.autobuffskills.push_back(buffskills);
					}
				}
			}
		}
		run_script(sd->fp.jobprofile->script, 0, sd->id, 0);
		status_heal(sd, sd->status.max_hp, sd->status.max_sp, 2);

		status_change_start(sd, sd, SC_AUTOATTACK, 10000, 0, 0, 0, 0, INFINITE_TICK, SCSTART_NOAVOID);

	}

	return 0;
}

int fakecurve_apply_additions(const std::string& prof_name, int want) {
	if (want <= 0) return 0;
	auto prof = fake_players_db.find(prof_name);
	if (!prof) return 0;

	// Essaie d'ajouter 'want' joueurs; s'appuie sur ta file + connect
	int spawned = fake_add_from_profile(prof.get(), (uint16)want); // deja log + connect
	return spawned;
}

void fakeplayer_dead_tp (map_session_data* sd) {
	if (!sd)
		return;

	if (sd->fp.tg.active) {
		pc_setpos(sd, mapindex_name2id(sd->status.save_point.map), sd->status.save_point.x, sd->status.save_point.y, CLR_TELEPORT); // return to save point

		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);
	} else if (sd->fp.jobprofile) {

		uint16 field_idx = pick_field_index_weighted(sd->fp.jobprofile->fieldzones);
		if (field_idx >= 0) {
			pc_setpos(sd, sd->fp.jobprofile->fieldzones[field_idx].mapid, 0, 0, CLR_TELEPORT);

			pc_delinvincibletimer(sd);
			clif_parse_LoadEndAck(0, sd);
		}
	}
}

void fakeplayer_town_behavior(map_session_data* sd) {
	t_tick now = gettick();

	if (DIFF_TICK(now, sd->fp.next_idle_action_tick) > 0) {
		int wS = battle_config.fake_idle_sit_weight;
		int wT = battle_config.fake_idle_turn_weight;
		int wM = battle_config.fake_idle_move_weight;
		int total = (wS * 2) + wT + wM; if (total <= 0) total = 1;
		int r = rnd() % total;

		if (r < wS) {
			if (sd->fp.behavior != BEHAVIOR_TRAINING && !pc_issit(sd)) {
				pc_setsit(sd);
				skill_sit(sd, 1);
				clif_sitting(*sd);
			}
		}
		else if (r < (wS * 2)) {
			if (pc_issit(sd)) {
				pc_setstand(sd, false);
				skill_sit(sd, 0);
				clif_standing(*sd);
			}
		}
		else if (r < (wS *2) + wT) {
			// Genere un nombre aleatoire : 4 (50%), 3 (25%), 5 (25%)
			int r = rnd() % 4; // valeurs possibles : 0, 1, 2, 3
			if (r == 0)
				sd->ud.dir = 3;
			else if (r == 1)
				sd->ud.dir = 5;
			else
				sd->ud.dir = 4;
			unit_setdir(sd, sd->ud.dir, true);
		} else {
			int stepMin = battle_config.fake_idle_step_min, stepMax = battle_config.fake_idle_step_max;
			if (stepMax < stepMin) stepMax = stepMin;
			int step = stepMin + (rnd() % (stepMax - stepMin + 1));
			int tx = sd->x, ty = sd->y;
			int sx = 0, sy = 0;
			int ex = 0, ey = 0;
			map_data* map = map_getmapdata(sd->m);

			for (auto& town : sd->fp.prof->towns) {
				if(std::strncmp(town.map.c_str(), map->name, MAP_NAME_LENGTH) == 0) {
					// Normalisation (a faire avant clamp, ou une fois au chargement DB)
					const int xmin = std::min(town.startx, town.endx);
					const int xmax = std::max(town.startx, town.endx);
					const int ymin = std::min(town.starty, town.endy);
					const int ymax = std::max(town.starty, town.endy);

					// Clamp d'un "pas" autour de (tx, ty)
					sx = std::max(xmin, tx - step);
					ex = std::min(xmax, tx + step);
					sy = std::max(ymin, ty - step);
					ey = std::min(ymax, ty + step);

					// (Facultatif, ceinture & bretelles)
					if (sx > ex) std::swap(sx, ex);
					if (sy > ey) std::swap(sy, ey);
					break;
				}
			}

			if (pick_walkable_xy(map->name, sx, sy, ex, ey, tx, ty, /*tries*/500, true)) {
				if (map_getcell(sd->m, tx, ty, CELL_CHKPASS) && !map_getcell(sd->m, tx, ty, CELL_CHKNPC))
					unit_walktoxy(sd, tx, ty, 1);
			}
		}

		int imin = battle_config.fake_idle_action_min_ms;
		int imax = battle_config.fake_idle_action_max_ms;
		if (imax < imin) imax = imin;
		int jitter = imin + (rnd() % (imax - imin + 1));
		sd->fp.next_idle_action_tick = now + jitter;
	}
}

struct FC_GlobalThrottle {
	uint64 window_start = 0;
	int    count = 0;
};
static FC_GlobalThrottle s_fc_gt;
static const int FC_GT_WINDOW_MS = 1500; // 1.5s
static const int FC_GT_LIMIT = 6;    // max 6 msgs / fenêtre

static bool fakechat_global_throttle_ok(uint64 now) {
	if (now - s_fc_gt.window_start > (uint64)FC_GT_WINDOW_MS) {
		s_fc_gt.window_start = now;
		s_fc_gt.count = 0;
	}
	if (s_fc_gt.count >= FC_GT_LIMIT) return false;
	++s_fc_gt.count;
	return true;
}

int fake_add_from_profile(s_fake_profile* prof, uint16 count) {
    if (!prof || count == 0) return 0;
    int spawned = 0;
    for (uint16 i = 0; i < count; ++i) {
        if (!fake_spawn_one_internal(prof, /*jp_opt*/nullptr, /*name*/nullptr, /*cap*/true))
            break;
        ++spawned;
    }
    if (spawned > 0) {
        size_t started = connect_fake_players_from_init(); // <- indispensable pour voir les connexions
        ShowInfo("AddFake: enqueued=%d started_now=%zu total_hold=%zu\n",
            spawned, started, g_fake_sd_hold.size());
    }
    return spawned;
}

int fake_add_from_profile_name(const std::string& prof_name, uint16 count) {
	auto p = fake_players_db.find(prof_name);
	if (!p) return 0;
	return fake_add_from_profile(p.get(), count);
}

void fake_boot_cycle_on_map() {
	int total_spawned = 0;

	for (auto& kv : fake_players_db) {
		auto prof = kv.second;
		if (!prof) continue;

		// --- CURVE BRANCH ---
		if (prof->curve.enabled) {
			const int online = fakecurve_online(prof.get());
			const int target = fakecurve_target_now(prof.get());
			const int ceiling = fakecurve_peak_ceiling(prof.get());
			int want = std::max(0, std::min(target, ceiling) - online);

			if (want > 0) {
				int spawned = fake_add_from_profile_name(prof->id, (uint16)want);
				total_spawned += spawned;
				ShowInfo("fake_boot(curve): profile='%s' target=%d ceil=%d spawned=%d (online=%d)\n",
					prof->id.c_str(), target, ceiling, spawned, online);
			}
			continue;
		}
		// --- LEGACY BRANCH (Curve disabled): keep your current behavior ---
		for (const auto& ref : prof->jobprofiles) {
			auto jp_ptr = fake_jobprofils_db.find(ref.name);
			if (!jp_ptr) {
				ShowWarning("fake_boot: unknown JobProfile '%s' in profile '%s'\n",
					ref.name.c_str(), prof->id.c_str());
				continue;
			}
			for (uint16 i = 0; i < ref.number; ++i) {
				if (fake_spawn_one_internal(prof.get(), jp_ptr.get(), ref.name.c_str(), /*cap*/false))
					++total_spawned;
			}
		}
	}

	if (total_spawned > 0) {
		size_t started = connect_fake_players_from_init();
		ShowInfo("fake_boot: started %zu connection(s) from queue (hold=%zu)\n",
			started, g_fake_sd_hold.size());
	}
}

// Annule jusqu'a `count` fakes du profil donne qui ne sont pas encore actifs en jeu.
// - Supprime de g_fake_init_infos (crees mais pas pc_setnewpc)
// - Supprime de g_fake_sd_hold les sessions en attente d'auth (pas encore mappees)
// - Nettoie la DB: table `char` (char_id) et `login` (account_id)
//
// Retourne le nombre effectivement annule.
int fake_cancel_pending_from_profile_name(const std::string& prof_name, uint16 count)
{
	if (count == 0) return 0;
	int removed = 0;

	for (auto it = g_fake_init_infos.begin(); it != g_fake_init_infos.end() && removed < count; ) {
		const FakeInitInfo& info = it->second;
		if (info.prof && info.prof->id == prof_name) {
			fake_db_purge_by_cid(info.cid);
			it = g_fake_init_infos.erase(it);
			++removed;
		}
		else {
			++it;
		}
	}

	// 2) Annuler les sessions en attente d'auth (sd cree mais pas encore en map)
	//    Critere "pas mappe": sd->prev == NULL (NB: selon les versions, c'est souvent sd->bl.prev)
	if (removed < count) {
		for (auto it = g_fake_sd_hold.begin(); it != g_fake_sd_hold.end() && removed < count; ) {
			map_session_data* sd = it->second;
			if (!sd) { it = g_fake_sd_hold.erase(it); ++removed; continue; }

			fake_wipe_inv_and_equips(sd);

			bool is_fake = (sd->fp.is_fake_player != 0);
			auto* prof = sd->fp.prof;
			bool match = false;

			if (is_fake && is_valid_profile_ptr(prof)) {
				std::string prof_id_copy;
				try {
					prof_id_copy = prof->id;
				}
				catch (...) {
					prof_id_copy.clear();
					{ it = g_fake_sd_hold.erase(it); ++removed; continue; }
				}
				match = (!prof_id_copy.empty() && prof_id_copy == prof_name);
			}

			if (match) {
				const uint32 cid = sd->status.char_id;

				if (sd->fp.fake_player_timer != INVALID_TIMER) delete_timer(sd->fp.fake_player_timer, fakeplayer_init_timer);
				if (sd->fp.fake_player_chat != INVALID_TIMER) delete_timer(sd->fp.fake_player_chat, fakechat_send_timer);
				if (sd->fp.fake_player_tg_quit != INVALID_TIMER) delete_timer(sd->fp.fake_player_tg_quit, fakeplayer_tg_quit_timer);

				// Retire juste le pointeur de la map (pas de delete ici !)
				it = g_fake_sd_hold.erase(it);

				// Déconnexion propre: le core va appeler chrif_auth_delete -> dtor + aFree
				set_eof(sd->fd);
				map_quit(sd);

				fake_db_purge_by_cid(cid);
				++removed;
			}
			else {
				++it;
			}
		}
	}
	return removed;
}

bool fakecurve_enable_profile(const std::string& id, bool enable)
{
	auto prof = fake_players_db.find(id);
	if (!prof) {
		ShowWarning("Curve: enable(%d) failed: unknown profile '%s'\n", (int)enable, id.c_str());
		return false;
	}
	if (prof->curve.enabled == enable) {
		ShowInfo("Curve: profile '%s' already %s\n", id.c_str(), enable ? "enabled" : "disabled");
		return true;
	}
	prof->curve.enabled = enable;
	ShowInfo("Curve: profile '%s' -> %s\n", id.c_str(), enable ? "ENABLED" : "DISABLED");
	return true;
}

bool fakecurve_set_profile(const std::string& id,
	uint16 perc_low, uint16 perc_high,
	uint16 smoothing_min, double weekend_boost)
{
	auto prof = fake_players_db.find(id);
	if (!prof) {
		ShowWarning("Curve: set failed: unknown profile '%s'\n", id.c_str());
		return false;
	}

	// clamps de secu
	if (perc_low > perc_high) std::swap(perc_low, perc_high);
	perc_low = (uint16)std::min<uint32>(perc_low, 1000); // tolere >100 si tu veux (ex: 150)
	perc_high = (uint16)std::min<uint32>(perc_high, 1000);
	if (smoothing_min == 0) smoothing_min = 1;
	if (weekend_boost <= 0.0) weekend_boost = 1.0;

	prof->curve.perc_low = perc_low;
	prof->curve.perc_high = perc_high;
	prof->curve.smoothing_minutes = smoothing_min;
	prof->curve.weekend_boost = weekend_boost;

	ShowInfo("Curve: set '%s' -> low=%u high=%u smooth=%u min boost=%.2f (use_table=%d weekend_table=%d)\n",
		id.c_str(), perc_low, perc_high, smoothing_min, weekend_boost,
		(int)prof->curve.use_table, (int)prof->curve.has_weekend_table);

	return true;
}

bool fakeplayer_set_enabled(bool enable)
{
	// Deja dans l'etat demande ?
	if (enable && battle_config.feature_fake_enable) {
		ShowInfo("FakePlayer: already ENABLED\n");
		// on peut declencher un tick pour s'aligner si besoin
		fakecurve_tick_once();
		return true;
	}
	if (!enable && !battle_config.feature_fake_enable) {
		ShowInfo("FakePlayer: already DISABLED\n");
		// être gentil: tenter un dernier nettoyage de pendings
		int total = 0;
		for (auto& kv : fake_players_db) {
			const auto& prof = kv.second;
			if (!prof) continue;
			total += fake_cancel_pending_from_profile_name(prof->id, 0xFFFF);
		}
		if (total > 0)
			ShowInfo("FakePlayer: cleaned %d pending fake(s) while already disabled.\n", total);
		return true;
	}

	if (!enable) {
		// PASSAGE -> OFF : couper la feature, puis KICK/PURGE tous les fakes
		battle_config.feature_fake_enable = 0;

		int total_removed = 0;
		for (auto& kv : fake_players_db) {
			const auto& prof = kv.second;
			if (!prof) continue;

			// 1) pending (init + sessions non mappees)
			int cut_pending = fake_cancel_pending_from_profile_name(prof->id, 0xFFFF);

			// 2) actifs (deja mappes)
			int cut_active = fake_destroy_active_from_profile_name(prof->id, 0xFFFF);

			total_removed += (cut_pending + cut_active);
		}

		ShowInfo("FakePlayer: DISABLED. Kicked %d fake(s); no reconnection while OFF.\n", total_removed);
		return true;
	}
	else {
		// PASSAGE -> ON : reactiver et laisser le smoothing ramener vers la target
		battle_config.feature_fake_enable = 1;
		ShowInfo("FakePlayer: ENABLED. Returning smoothly to curve target.\n");

		// Amorcer immediatement un premier ajustement (respecte smoothing)
		fakecurve_tick_once();
		return true;
	}
}


TIMER_FUNC(fakecurve_global_timer)
{
	// one tick of the curve logic
	fakecurve_tick_once();

	// reschedule ~every 60s
	add_timer(gettick() + battle_config.fake_adjust_period_sec * 1000, fakecurve_global_timer, 0, 0);
	return 0;
}

std::string fakechat_normalize(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	trim_spaces(s);
	return s;
}

// Ponderation via ReplyTemplateFC.weight
static const std::string* fakechat_pick_reply_weighted(const IntentDefFC& in) {
	if (in.replies.empty()) return nullptr;

	// calc somme des poids sur repliques non vides
	double sum = 0.0;
	for (const auto& r : in.replies) {
		if (!r.text.empty() && !fc_is_blank(r.text))
			sum += (r.weight > 0.f ? r.weight : 0.f);
	}

	// si toutes vides => fallback a la premiere non-nulle, sinon nullptr
	if (sum <= 0.0) {
		for (const auto& r : in.replies) {
			if (!r.text.empty() && !fc_is_blank(r.text))
				return &r.text;
		}
		return nullptr;
	}

	double roll = (double)(rnd() % 10000) / 10000.0 * sum; // [0,sum)
	double acc = 0.0;
	for (const auto& r : in.replies) {
		if (r.text.empty() || fc_is_blank(r.text)) continue;
		double w = (r.weight > 0.f ? r.weight : 0.f);
		acc += w;
		if (roll <= acc) return &r.text;
	}
	// securite
	for (const auto& r : in.replies) {
		if (!r.text.empty() && !fc_is_blank(r.text))
			return &r.text;
	}
	return nullptr;
}

static void fakechat_replace_all(std::string& s, const char* key, const std::string& val) {
	if (!key || !*key) return;
	const std::string k = key;
	size_t pos = 0;
	while ((pos = s.find(k, pos)) != std::string::npos) {
		s.replace(pos, k.size(), val);
		pos += val.size();
	}
}

static const char* fc_get_mapname(map_session_data* sd) {
	if (!sd) return "";
	// Selon ta base rAthena, l'un des deux fonctionnera :
	if (sd->mapindex) {
		const char* nm = mapindex_id2name(sd->mapindex);
		return nm ? nm : "";
	}
	map_data* md = map_getmapdata(sd->m);
#if defined(HAVE_MAPDATA_NAME)
	return (md && md->name) ? md->name : "";
#else
	return (md) ? mapindex_id2name(md->index) : "";
#endif
}

static std::string fc_dir_hint(map_session_data* sd) {
	if (!sd)
		return "";

	switch (sd->ud.dir) {
	case DIR_NORTH:      return "north";
	case DIR_NORTHWEST:  return "northwest";
	case DIR_WEST:       return "west";
	case DIR_SOUTHWEST:  return "southwest";
	case DIR_SOUTH:      return "south";
	case DIR_SOUTHEAST:  return "southeast";
	case DIR_EAST:       return "east";
	case DIR_NORTHEAST:  return "northeast";
	default:             return ""; // unknown / not set
	}
}

static inline const char* fc_mob_name(const mob_data* md) {
	if (!md) return nullptr;
	if (md->name && md->name[0]) return md->name;
	if (md->db) {
		if (md->db->jname.c_str() && md->db->jname[0]) return md->db->jname.c_str();
		if (md->db->name.c_str() && md->db->name[0])  return md->db->name.c_str();
	}
	return nullptr;
}

static int fc_pick_random_mob_cb(block_list* bl, va_list ap) {
	if (!bl || bl->type != BL_MOB) return 0;

	int* p_seen = va_arg(ap, int*);         // nb de mobs vus jusqu'ici
	const char** p_out = va_arg(ap, const char**);// nom choisi (peut être remplace)

	mob_data* md = (mob_data*)bl;
	const char* nm = fc_mob_name(md);
	if (!nm) return 0;

	(*p_seen)++;

	if ((int)(rnd() % (*p_seen)) == 0) {
		*p_out = nm;
	}
	return 0;
}

static std::string fc_last_mob(map_session_data* sd) {
	if (!sd) return std::string();

	map_data* mdp = map_getmapdata(sd->m);
	if (!mdp) return std::string();

	// En ville: pas de mob contextuel
	if (mdp->getMapFlag(MF_TOWN))
		return std::string();

	int seen = 0;
	const char* picked = nullptr;

	// Une seule passe sur la map
	map_foreachinmap(fc_pick_random_mob_cb, sd->m, BL_MOB, &seen, &picked);

	return (picked && seen > 0) ? std::string(picked) : std::string();
}

static void fakechat_fill_placeholders(std::string& t, map_session_data* bot, map_session_data* from) {
	// :name => nom du joueur source
	fakechat_replace_all(t, ":name", from ? std::string(from->status.name) : std::string());
	// :map
	fakechat_replace_all(t, ":map", std::string(fc_get_mapname(bot)));
	// :lvl
	fakechat_replace_all(t, ":lvl", std::to_string(bot ? bot->status.base_level : 0));
	// :dir
	fakechat_replace_all(t, ":dir", fc_dir_hint(bot));
	// :mob
	fakechat_replace_all(t, ":mob", fc_last_mob(bot));
	// ::class
	fakechat_replace_all(t, ":class", job_name(bot ? bot->status.class_ : 0));
	// ::price
	fakechat_replace_all(t, ":price", std::to_string(rnd() % 200000));
}

bool fakechat_is_mentioned(const map_session_data* bot, const std::string& msgNorm, bool is_private) {
	if (is_private) return true; // PM => considere comme mention
	if (!bot) return false;
	if (!bot->status.name[0]) return false;
	std::string name = bot->status.name;
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return msgNorm.find(name) != std::string::npos;
}

bool fakechat_should_reply(const ChatConfigFC& cfg, BotChatMemory& mem, uint32 fromCid, bool mentioned, uint64 now) {
	// Cooldown bot
	uint64 dtSelf = now - mem.lastSpeakTick;
	if (dtSelf < (uint64)cfg.cooldownSelfMs) {
		return false;
	}

	// Cooldown envers ce joueur
	auto it = mem.lastReplyTo.find(fromCid);
	if (it != mem.lastReplyTo.end()) {
		uint64 dtPlayer = now - it->second;
		if (dtPlayer < (uint64)cfg.cooldownPerPlayerMs) {
			return false;
		}
	}

	// Proba
	double p = mentioned ? cfg.replyProbMention : cfg.replyProbBase;

	// Tirage clair: 0..9999 vs p*10000
	int pr = (int)(p * 10000.0 + 0.5);        // attendu: 0..10000
	int roll = (int)(rnd() % 10000);          // 0..9999
	int pass = (roll < pr);
	return pass;
}

const IntentDefFC* fakechat_detect_intent_basic(const FakeChatDatabase& db, const std::string& msgNorm) {
	const IntentDefFC* best = nullptr;
	float bestScore = 0.f;

	for (auto it = fake_chat_db.begin(); it != fake_chat_db.end(); ++it) {
		const IntentDefFC& in = *(it->second);
		int hits = 0;
		for (const auto& kw : in.keywords) {
			if (kw.empty()) continue;
			std::string kwl = kw;
			std::transform(kwl.begin(), kwl.end(), kwl.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			if (msgNorm.find(kwl) != std::string::npos) ++hits;
		}
		if (hits <= 0) continue;

		float score = hits * in.weight;
		if (!best || score > bestScore) {
			best = &in;
			bestScore = score;
		}
	}

	if (!best) {
		auto unk = fake_chat_db.find("UNKNOWN");
		if (unk) best = unk.get();
	}
	return best;
}

const std::string* fakechat_pick_reply_uniform(const IntentDefFC& intent) {
	if (intent.replies.empty()) return nullptr;
	int idx = (int)(rnd() % intent.replies.size());
	const std::string* chosen = &intent.replies[idx].text;
	return chosen;
}

int fakechat_typing_delay_ms(const ChatConfigFC& cfg, size_t text_len) {
	int cpsMin = std::max(1, cfg.typingCpsMin);
	int cpsMax = std::max(cpsMin, cfg.typingCpsMax);
	int cps = cpsMin + (int)(rnd() % (cpsMax - cpsMin + 1));
	int delay = (int)((int)text_len * 1000 / std::max(1, cps));
	if (delay < 150) delay = 150;
	if (delay > 4000) delay = 4000;
	return delay;
}

static FCChannel fakechat_pick_channel(map_session_data* bot, map_session_data* from,
	bool is_private, const std::string& intentName)
{
	if (is_private) return FCChannel::PM;
	if (!from) return FCChannel::SELF;

	const auto& cfg = fake_chat_db.cfg();

	auto pick_by_bias = [&](const std::string& name)->FCChannel {
		const ChannelBiasFC* bias = nullptr;
		auto itI = cfg.channelBias.find(name);
		if (itI != cfg.channelBias.end()) bias = &itI->second;
		else {
			auto itD = cfg.channelBias.find("Default");
			if (itD != cfg.channelBias.end()) bias = &itD->second;
		}
		if (!bias) return FCChannel::GLOBAL;

		int r = rnd() % 100;
		// on ne gère que GLOBAL/PM ici, mais tu peux etendre
		return (r < (int)(bias->pGlobal * 100.0)) ? FCChannel::GLOBAL : FCChannel::PM;
		};

	return pick_by_bias(intentName);
}

static void fc_send_pm(map_session_data* from, map_session_data* to, const char* text) {
	int32 i;

	if (!from || !to || !text || !*text)
		return;

    const size_t msglen = strlen(text) + 1;
    const int gmlvl = pc_get_group_level(from);

	// if player ignores everyone
	if (to->state.ignoreAll && pc_get_group_level(from) <= pc_get_group_level(to)) {
		if (pc_isinvisible(to) && pc_get_group_level(from) < pc_get_group_level(to))
			clif_wis_end(*from, ACKWHISPER_TARGET_OFFLINE);
		else
			clif_wis_end(*from, ACKWHISPER_ALL_IGNORED);
		return;
	}

	if (pc_get_group_level(from) <= pc_get_group_level(to)) {
		// if player ignores the source character
		ARR_FIND(0, MAX_IGNORE_LIST, i, to->ignore[i].name[0] == '\0' || strcmp(to->ignore[i].name, from->status.name) == 0);
		if (i < MAX_IGNORE_LIST && to->ignore[i].name[0] != '\0') { // source char present in ignore list
			clif_wis_end(*from, ACKWHISPER_IGNORED);

			return;
		}
	}

	clif_wis_message(/*sd=dest*/to, /*nick=sender*/from->status.name, /*mes*/text, /*len*/msglen, /*gm*/gmlvl);
}

// --- Envoi GLOBAL: "Name : message" comme clif_parse_GlobalMessage ---
static void fc_send_global(map_session_data* sd, const char* message) {
	if (!sd || !message || !*message) return;

	// Construit "Name : message" (même format que 'output' dans clif_parse_GlobalMessage)
	char output[CHAT_SIZE_MAX + NAME_LENGTH * 2] = { 0 };
	safesnprintf(output, sizeof(output), "%s : %s", sd->status.name, message);

	// Même logique de target que l'implementation existante
	enum send_target target = sd->chatID ? CHAT_WOS : AREA_CHAT_WOC;

	// Envoi
	clif_GlobalMessage(*sd, output, target);

	// NB: on ne renvoie PAS l'echo au bot (contrairement a clif_parse_GlobalMessage),
	// ce qui evite un double-affichage côte bot. Ajoute-le si tu le souhaites.
}

static void fc_send_party(map_session_data* sd, const char* text) {
	if (!sd || !text || !*text) return;
	size_t len = strlen(text);
	party_send_message(sd, text, len); // -> intif + echo local + log, comme ton code
}

// Timer d'envoi multi-canaux
TIMER_FUNC(fakechat_send_timer)
{
	map_session_data* sd = map_id2sd(id);
	FakeChatSendPayload* p = (FakeChatSendPayload*)data;
	if (!p) return 0;

	if (sd && sd->fp.is_fake_player) {
		switch (p->chan) {
		case FCChannel::PM: {
			map_session_data* to = map_id2sd(p->target_id);
			if (!to && !p->target_name.empty()) {
				to = map_nick2sd(p->target_name.c_str(), false);
			}
			if (!to) {
				break; // n'envoie pas ; pas de fallback SELF en PM
			}
			fc_send_pm(sd, to, p->text.c_str());
		} break;
		case FCChannel::PARTY:
			fc_send_party(sd, p->text.c_str());
			break;
		case FCChannel::GUILD:
			// TODO: clif_guild_message(...) 
			break;
		case FCChannel::GLOBAL:
			fc_send_global(sd, p->text.c_str());
			break;
		case FCChannel::SELF:
		default:
			clif_messagecolor(sd, color_table[COLOR_DEFAULT], p->text.c_str(), false, SELF);
			break;
		}
	}

	delete p;
	return 0;
}

// Timer de map quit du training ground
TIMER_FUNC(fakeplayer_tg_quit_timer)
{
	map_session_data* sd = map_id2sd(id);

	if (!sd)
		return 0;

	if(sd->state.autoattack)
		status_change_end(sd, SC_AUTOATTACK);

	fake_wipe_inv_and_equips(sd);

	sd->fp.fake_player_tg_quit = INVALID_TIMER;

	const uint32 aid = sd->status.account_id;
	const uint32 cid = sd->status.char_id;

	if (sd->fp.fake_player_tg_quit != INVALID_TIMER)
		delete_timer(sd->fp.fake_player_tg_quit, fakeplayer_tg_quit_timer);

	if (sd->fp.fake_player_chat != INVALID_TIMER)
		delete_timer(sd->fp.fake_player_chat, fakechat_send_timer);

	//if(sd->fp.tg.step == TGStep::DONE_2) fake_add_from_profile_name("training_world", 1);

	// Deconnexion propre
	set_eof(sd->fd);
	map_quit(sd);
	sd = nullptr;

	// Nettoyage DB (on utilise aid/cid sauvegardes)
	fake_db_purge_by_cid(cid); // ou sd->status.char_id / info.cid
	
	return 0;
}

// ===== etape 3: Aho-Corasick + fuzzy =====
FC_ACMatcher g_fc_matcher;

static inline int fc_tokId(char c) {
	if (c >= 'a' && c <= 'z') return c - 'a';
	if (c >= '0' && c <= '9') return 26 + (c - '0');
	if (c == ' ') return 36;
	return -1;
}

static std::string fc_norm_kw(std::string s) {
	// même normalisation que les messages
	s = fakechat_normalize(s);
	// collapse multiple spaces
	std::string out; out.reserve(s.size());
	bool sp = false;
	for (char ch : s) {
		if (std::isspace((unsigned char)ch)) {
			if (!sp) { out.push_back(' '); sp = true; }
		}
		else {
			out.push_back((char)std::tolower((unsigned char)ch));
			sp = false;
		}
	}
	return out;
}

static void ac_add_keyword(FC_ACMatcher& M, const std::string& kw, int intentIdx) {
	int v = 0;
	for (char ch : kw) {
		int t = fc_tokId(ch);
		if (t < 0) continue;
		if (M.trie[v].next[t] == -1) {
			M.trie[v].next[t] = (int)M.trie.size();
			M.trie.emplace_back();
		}
		v = M.trie[v].next[t];
	}
	M.trie[v].hits.push_back(intentIdx);
}

static void ac_build(FC_ACMatcher& M) {
	std::queue<int> q;
	// root transitions
	for (int t = 0; t < 37; ++t) {
		int u = M.trie[0].next[t];
		if (u != -1) { M.trie[u].link = 0; q.push(u); }
		else M.trie[0].next[t] = 0;
	}
	while (!q.empty()) {
		int v = q.front(); q.pop();
		for (int t = 0; t < 37; ++t) {
			int u = M.trie[v].next[t];
			if (u != -1) {
				M.trie[u].link = M.trie[M.trie[v].link].next[t];
				// merge hits
				auto& H = M.trie[u].hits, & G = M.trie[M.trie[u].link].hits;
				H.insert(H.end(), G.begin(), G.end());
				q.push(u);
			}
			else {
				M.trie[v].next[t] = M.trie[M.trie[v].link].next[t];
			}
		}
	}
}

// Levenshtein <=1 pour tokens courts (bjr/slt/hi/yo, etc.)
static bool fc_lev1_match(const std::string& text, const std::string& token) {
	const size_t n = text.size(), m = token.size();
	if (token.empty()) return false;
	if (n == m) {
		int diff = 0;
		for (size_t i = 0; i < n; ++i) if (text[i] != token[i] && ++diff > 1) return false;
		return diff <= 1;
	}
	if (n + 1 == m) { // insertion in text (or deletion in token)
		size_t i = 0, j = 0; int edits = 0;
		while (i < n && j < m) {
			if (text[i] == token[j]) { ++i; ++j; }
			else { ++j; if (++edits > 1) return false; }
		}
		return true;
	}
	if (n == m + 1) { // deletion in text
		size_t i = 0, j = 0; int edits = 0;
		while (i < n && j < m) {
			if (text[i] == token[j]) { ++i; ++j; }
			else { ++i; if (++edits > 1) return false; }
		}
		return true;
	}
	return false;
}

void fakechat_build_matcher() {
	g_fc_matcher = FC_ACMatcher{};
	g_fc_matcher.trie.clear();
	g_fc_matcher.trie.emplace_back(); // root

	// snapshot des intents et indexation
	g_fc_matcher.intents.clear();
	g_fc_matcher.name2idx.clear();

	int idx = 0;
	for (auto it = fake_chat_db.begin(); it != fake_chat_db.end(); ++it) {
		const IntentDefFC* in = it->second.get();
		if (!in) continue;
		g_fc_matcher.name2idx[in->name] = idx;
		g_fc_matcher.intents.push_back(in);

		// keywords
		for (const auto& kw : in->keywords) {
			if (kw.empty()) continue;
			std::string k = fc_norm_kw(kw);
			if (k.empty()) continue;
			ac_add_keyword(g_fc_matcher, k, idx);
		}
		++idx;
	}
	ac_build(g_fc_matcher);
	g_fc_matcher.built = true;
}

static double fc_state_multiplier(const ChatConfigFC& cfg, const std::string& state, const std::string& intentName) {
	if (state.empty()) return 1.0;
	auto itS = cfg.stateBoosts.find(state);
	if (itS == cfg.stateBoosts.end()) return 1.0;
	auto itI = itS->second.find(intentName);
	if (itI == itS->second.end()) return 1.0;
	return (double)itI->second;
}

const IntentDefFC* fakechat_detect_intent_ac(
	const FakeChatDatabase& db,
	const std::string& msgNorm,
	const std::string& stateName
) {
	if (!g_fc_matcher.built) {
		// fallback au basic
		return fakechat_detect_intent_basic(db, msgNorm);
	}

	// 1) AC pass: score par intent = hits * intent.weight
	std::vector<double> scores(g_fc_matcher.intents.size(), 0.0);
	std::vector<int>    hits(g_fc_matcher.intents.size(), 0); // pour MinHits eventuel

	int v = 0;
	for (char ch : msgNorm) {
		int t = fc_tokId(ch);
		if (t < 0) { v = 0; continue; }
		v = g_fc_matcher.trie[v].next[t];
		for (int intentIdx : g_fc_matcher.trie[v].hits) {
			const IntentDefFC* in = g_fc_matcher.intents[intentIdx];
			if (!in) continue;
			hits[intentIdx] += 1;
			scores[intentIdx] += std::max(1.0f, in->weight);
		}
	}

	auto split_tokens = [](const std::string& s) {
		std::vector<std::string> T;
		size_t p = 0;
		while (p < s.size()) {
			while (p < s.size() && s[p] == ' ') ++p;
			size_t b = p;
			while (p < s.size() && s[p] != ' ') ++p;
			if (b < p) T.emplace_back(s.substr(b, p - b));
		}
		return T;
		};
	auto is_alnum_str = [](const std::string& s)->bool {
		if (s.empty()) return false;
		for (unsigned char c : s) if (!std::isalnum(c)) return false;
		return true;
		};
	const std::vector<std::string> msgTokens = split_tokens(msgNorm);

	for (size_t i = 0; i < g_fc_matcher.intents.size(); ++i) {
		auto* in = g_fc_matcher.intents[i];
		if (!in) continue;
		if (scores[i] <= 0.0) continue;

		bool hasShortKW = false;
		bool shortTokenHit = false;
		bool longSubstringHit = false;

		for (const auto& kw : in->keywords) {
			if (kw.empty()) continue;
			std::string k = fc_norm_kw(kw);
			if (k.empty()) continue;

			if ((int)k.size() <= 2) {
				hasShortKW = true;
				// exige un token egal (alphanum)
				if (is_alnum_str(k)) {
					for (const auto& tok : msgTokens) {
						if (tok == k) { shortTokenHit = true; break; }
					}
				}
			}
			else { // >=3 chars: simple test de sous-chaîne (comme AC)
				if (msgNorm.find(k) != std::string::npos) {
					longSubstringHit = true;
				}
			}
			if (shortTokenHit && longSubstringHit) break;
		}

		// Si l'intent n'est porte QUE par des courts en sous-chaîne, on coupe
		if (hasShortKW && !shortTokenHit && !longSubstringHit) {
			scores[i] = 0.0;
			hits[i] = 0;
		}
	}

	// 3) Boost par etat
	const auto& cfg = db.cfg();
	for (size_t i = 0; i < g_fc_matcher.intents.size(); ++i) {
		auto* in = g_fc_matcher.intents[i];
		if (!in) continue;
		double mult = fc_state_multiplier(cfg, stateName, in->name);
		double before = scores[i];
		scores[i] *= mult;
		if (before > 0.0 && mult != 1.0) {
		}
	}

	// 3.b) Filtrage generique : MinHits / MinScore (si ces champs existent sur l'intent)
	//     (si non initialises ailleurs, ils valent 0 et n'affectent pas)
	for (size_t i = 0; i < g_fc_matcher.intents.size(); ++i) {
		auto* in = g_fc_matcher.intents[i];
		if (!in) continue;
		if (in->minHits > 0 && hits[i] < in->minHits) {
			scores[i] = 0.0;
		}
		if (in->minScore > 0.f && scores[i] < in->minScore) {
			scores[i] = 0.0;
		}
	}

	// 4) Choix (+ tie-break si egalite)
	int best = -1; double b = 0.0;
	for (size_t i = 0; i < scores.size(); ++i) {
		if (scores[i] > b) { b = scores[i]; best = (int)i; }
	}

	if (best >= 0) {
		// egalites ? on applique un tie-break optionnel si defini en config
		const double eps = 1e-6;
		std::unordered_set<std::string> ties;
		for (size_t i = 0; i < scores.size(); ++i) {
			if (std::fabs(scores[i] - b) <= eps && scores[i] > 0.0) {
				ties.insert(g_fc_matcher.intents[i]->name);
			}
		}
		if (ties.size() > 1 && !cfg.tieBreakOrder.empty()) {
			for (const auto& name : cfg.tieBreakOrder) {
				if (ties.count(name)) {
					auto it = g_fc_matcher.name2idx.find(name);
					if (it != g_fc_matcher.name2idx.end()) {
						best = it->second;
						break;
					}
				}
			}
		}
		return g_fc_matcher.intents[best];
	}

	// fallback UNKNOWN
	auto unk = fake_chat_db.find("UNKNOWN");
	if (unk) {
		return unk.get();
	}
	return nullptr;
}

static std::string fc_state_name(map_session_data* sd) {
	if (!sd) return "";
	if (sd->state.autotrade || sd->state.vending) return "Vending";
	map_data* mapdata = map_getmapdata(sd->m);
	if (mapdata->getMapFlag(MF_TOWN))
		return "Town";
	if (unit_is_walking(sd)) return "Walking";
	return "Farming";
}

bool fc_item_is_available_for_bot(flooritem_data* it, map_session_data* bot, uint64 now) {
	if (!it || !bot || !bot->fp.is_fake_player) return true;
	fc_claim_prune(it->fc_claim, now);

	if (!fc_claim_is_holder(it->fc_claim, bot->status.account_id, now) &&
		fc_claim_count(it->fc_claim, now) >= MAX_FP_ON_TARGET)
		return false;

	const uint32 TTL_PICK_MS = 5000; // 1.2s
	return fc_claim_try(it->fc_claim, bot->status.account_id, now, TTL_PICK_MS);
}

bool fc_target_mob_is_available_for_bot(mob_data* md, map_session_data* bot, uint64 now) {
	if (!md || !bot || !bot->fp.is_fake_player) return true; // ne jamais bloquer les vrais joueurs
	fc_claim_prune(md->fc_claim, now);

	// Si je ne suis pas dejà reservataire et que 2 slots actifs -> skip
	if (!fc_claim_is_holder(md->fc_claim, bot->status.account_id, now) &&
		fc_claim_count(md->fc_claim, now) >= MAX_FP_ON_TARGET)
		return false;

	// Essaye de claim/rafraîchir avec TTL attaque
	const uint32 TTL_ATTACK_MS = 5000; // 1.5s (à ajuster)
	return fc_claim_try(md->fc_claim, bot->status.account_id, now, TTL_ATTACK_MS);
}

bool fakechat_try_handle_message_step2(
	map_session_data* from,
	map_session_data* bot,
	std::string msg,
	bool is_private,
	std::string& out_reply,
	int& out_delay_ms,
	std::string& out_intent,
	FCChannel channel_hint,   // = FCChannel::AUTO (defaut dans le header)
	int target_id_hint        // = 0
) {
	if (!bot || !from || msg.empty()) return false;
	const auto& cfg = fake_chat_db.cfg();
	const uint64 now = gettick();

	BotChatMemory& mem = bot->fp.mem;

	// Cape le nombre d'echanges consecutifs avec le même joueur
	if (mem.lastInterlocutor == from->status.char_id && mem.consecutiveTurns >= 3) {
		return false;
	}

	std::string norm = fakechat_normalize(msg);
	bool mentioned = fakechat_is_mentioned(bot, norm, is_private);

	if (from->fp.is_fake_player)
		return false;

	if (channel_hint == FCChannel::GLOBAL && map_getmapflag(bot->m, MF_TOWN) && distance_bl(bot, from) > 5)
		return false;

	if (!fakechat_should_reply(cfg, mem, from->status.char_id, mentioned, now)) {
		return false;
	}

	// Intent via AC + etat
	const IntentDefFC* intent = fakechat_detect_intent_ac(fake_chat_db, norm, fc_state_name(bot));
	if (!intent) {
		return false;
	}

	// Reply ponderee
	const std::string* chosen = fakechat_pick_reply_weighted(*intent);
	if (!chosen || chosen->empty()) {
		return false;
	}

	// Placeholders
	std::string final = *chosen;
	fakechat_fill_placeholders(final, bot, from);
	if (final.empty()) final = "ok";

	// Delai de "typage"
	int delay_ms = fakechat_typing_delay_ms(cfg, final.size());

	// apres: int delay_ms = fakechat_typing_delay_ms(cfg, final.size());
	int jitter_ms = 0;

	// Suggestion: config ou constante
	const int kGlobalJitterMaxMs = 3000; // 0..3000ms de spread

	// --- Selection du canal (NOUVEAU) ---
	FCChannel chan = channel_hint;
	if (chan == FCChannel::AUTO) {
		// Laisse l'algo choisir si aucun hint explicite
		chan = fakechat_pick_channel(bot, from, is_private, intent->name);
	}
	if (chan == FCChannel::GLOBAL) {
		jitter_ms = rnd() % kGlobalJitterMaxMs;
	}
	delay_ms += jitter_ms;

	// Rate-limit global (serveur)
	const uint64 now2 = gettick();
	if (!fakechat_global_throttle_ok(now))
		return false;

	uint32 target_id = 0;
	if (chan == FCChannel::PM) {
		// Si l'appel fournit un target explicite, respecte-le; sinon from->id
		target_id = (target_id_hint != 0) ? target_id_hint : (from ? from->id : 0);
	}

	// Planifie l'envoi avec canal choisi
	FakeChatSendPayload* payload = new FakeChatSendPayload{ final, chan, target_id, from->status.name};
	int tid = add_timer(now2 + delay_ms, fakechat_send_timer, bot->id, (intptr)payload);
	bot->fp.fake_player_timer = tid;

	out_reply = final;
	out_intent = intent->name;
	out_delay_ms = delay_ms;

	// Maj memoire (utilise now2 pour rester coherent avec le timer)
	mem.lastSpeakTick = now2;
	mem.lastReplyTo[from->status.char_id] = now2;
	if (mem.lastInterlocutor == from->status.char_id) {
		if (mem.consecutiveTurns < 255) ++mem.consecutiveTurns;
	}
	else {
		mem.lastInterlocutor = from->status.char_id;
		mem.consecutiveTurns = 1;
	}

	return true;
}

void fp_monster_killed(map_session_data* sd, mob_data* md) {
	if (sd->fp.behavior == BEHAVIOR_TRAINING && sd->fp.tg.step == TGStep::COMBAT1_2) {
		if (md && md->mob_id == 1002) {
			sd->fp.tg.step = TGStep::COMBAT1_2b;
			sd->fp.tg.nextActionTick = gettick() + irand(TG_DELAY[(int)TGStep::COMBAT1_2b].minMs, TG_DELAY[(int)TGStep::COMBAT1_2b].maxMs);
		}
	}
}

void tg_process(map_session_data* sd) {
	if (sd->status.class_ != JOB_NOVICE) return;

	auto& tg = sd->fp.tg;
	if (!tg.active) return;

	t_tick now = gettick();

	const struct TimerData* timer_data = get_timer(sd->fp.fake_player_tg_quit);

	if (sd->fp.tg.nextActionTick && sd->fp.tg.nextActionTick > now)
		return;

	RectCoords rc;
	int32 jlvl = 0, blvl = 0;

	switch (tg.step) {
	case TGStep::ARRIVAL: {
		tg.step = TGStep::WAIT;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::WAIT].minMs, TG_DELAY[(int)TGStep::WAIT].maxMs);

		//if (sd->fp.fake_player_tg_quit == INVALID_TIMER) // dc protection
		//	sd->fp.fake_player_tg_quit = add_timer(gettick() + irand(500000,600000), fakeplayer_tg_quit_timer, sd->id, 0);

		break;
	}

	case TGStep::WAIT: {
		fakeplayer_town_behavior(sd);
		break;
	}

	case TGStep::GO_WARP: {
		if (unit_is_walking(sd)) return; // not yet to npc

		if (pc_issit(sd)) {
			pc_setstand(sd, false);
			skill_sit(sd, 0);
			clif_standing(*sd);
		}

		if (sd->m != map_mapname2mapid(Z_WARP1.map)) {
			tg.step = TGStep::NPC1_1;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC1_1].minMs, TG_DELAY[(int)TGStep::NPC1_1].maxMs);
			return;
		}

		rc.map = Z_WARP1.map;
		rc.x1 = 0;
		rc.y1 = 0;

		if (!sd->aa.path.empty()) {
			sd->aa.path.clear();
			if (!pick_walkable_xy(rc.map, Z_WARP1.x1, Z_WARP1.y1, Z_WARP1.x2, Z_WARP1.y2, rc.x1, rc.y1, 200, false))
				break;

			int __ret = aa_move_to_path(sd->aa.path, sd);

			if (__ret == 0) {
				sd->aa.path.clear();
			}
			else {
				if (sd->fp.tg.oldx == 0 && sd->fp.tg.oldy == 0) {
					sd->fp.tg.oldx = sd->x;
					sd->fp.tg.oldy = sd->y;
				}
				else if (sd->fp.tg.oldx == sd->x && sd->fp.tg.oldy == sd->y) {
					sd->aa.path.clear();
				}
				else
					clif_walkok(*sd);
			}
			return;
		}

		// Tente une coordonnee marchable
		pick_walkable_xy(rc.map, Z_WARP1.x1, Z_WARP1.y1, Z_WARP1.x2, Z_WARP1.y2, rc.x1, rc.y1, 200, false);

		if (rc.x1 <= 0 || rc.y1 <= 0) {
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::GO_WARP].minMs, TG_DELAY[(int)TGStep::GO_WARP].maxMs);
			break;
		}

		algorithm_path_finding(sd, sd->m, sd->x, sd->y, rc.x1, rc.y1);

		int __ret = aa_move_to_path(sd->aa.path, sd);
		if (__ret == 0) {
			sd->aa.path.clear();

			// try to force walk to warp
			unit_walktoxy(sd, rc.x1, rc.y1, 4);
		}
		else {
			if (sd->x != sd->fp.tg.oldx || sd->y != sd->fp.tg.oldy) {
				clif_walkok(*sd);
				sd->fp.tg.oldx = sd->x;
				sd->fp.tg.oldy = sd->y;
			}
		}
		break;
	}

	case TGStep::NPC1_1: {
		if (unit_is_walking(sd)) return; // Le perso marche encore, on ne fait rien.

		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);

		bool in_target_zone = (sd->x >= Z_NPC1.x1 && sd->x <= Z_NPC1.x2 && sd->y >= Z_NPC1.y1 && sd->y <= Z_NPC1.y2);

		if (in_target_zone && sd->aa.path.empty() && map_count_oncell(sd->m, sd->x, sd->y, BL_PC, 0) <= 1) {
			// C'est bon, il est arrivé à destination et s'est arrêté.
			tg.step = TGStep::NPC1_2;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC1_2].minMs, TG_DELAY[(int)TGStep::NPC1_2].maxMs);

			struct look_result lr = look_from_player_to_xy(sd, ZC_NPC1.x1, ZC_NPC1.y1);
			pc_setdir(sd, lr.body_dir, lr.head_dir);
			clif_changed_dir(*sd, AREA_WOS);

			// Reset variables de mouvement
			sd->fp.tg.oldx = 0;
			sd->fp.tg.oldy = 0;

			return;
		}

		// Si on est ici, c'est qu'on doit bouger (soit on n'a pas de chemin, soit on est bloqué).
		rc.map = Z_NPC1.map;
		rc.x1 = 0;
		rc.y1 = 0;

		// Si un chemin existe déjà mais qu'on n'est pas arrivé (cas où on traverse la map)
		if (!sd->aa.path.empty()) {
			// On continue simplement d'avancer sur le chemin existant
			int __ret = aa_move_to_path(sd->aa.path, sd);

			if (__ret == 0) {
				// Le chemin est fini (ou bloqué) mais on n'est pas dans la condition de victoire du haut ?
				// On clear pour forcer un recalcul au prochain tick.
				sd->aa.path.clear();
			}
			else {
				// Gestion anti-bloquage (si le perso frotte un mur ou lag)
				if (sd->fp.tg.oldx == sd->x && sd->fp.tg.oldy == sd->y) {
					// Il n'a pas bougé depuis la dernière fois -> bloqué -> on clear pour trouver un nouveau point.
					sd->aa.path.clear();
				}
				else {
					// Tout va bien, on met à jour la position précédente
					sd->fp.tg.oldx = sd->x;
					sd->fp.tg.oldy = sd->y;
					clif_walkok(*sd);
				}
			}
			return;
		}

		int attempts = 0;
		bool found = false;
		while (attempts < 20 && !found) {
			// Utilise ta fonction existante (mise à jour nécessaire, voir plus bas)
			if (pick_walkable_xy(rc.map, Z_NPC1.x1, Z_NPC1.y1, Z_NPC1.x2, Z_NPC1.y2, rc.x1, rc.y1, 50, true)) {
				// Vérification supplémentaire : Est-ce qu'il y a un joueur sur cette case précise ?
				// map_count_oncell permet de vérifier s'il y a des BL_PC (players) sur la case.
				if (map_count_oncell(sd->m, rc.x1, rc.y1, BL_PC, 0) == 0) {
					found = true;
				}
			}
			attempts++;
		}

		if (!found || rc.x1 <= 0 || rc.y1 <= 0) {
			// Pas de case trouvée, on attend un peu avant de réessayer pour pas spammer le CPU
			tg.nextActionTick = now + irand(100, 300);
			break;
		}

		// Calcul du chemin vers ce point validé
		algorithm_path_finding(sd, sd->m, sd->x, sd->y, rc.x1, rc.y1);

		if (aa_move_to_path(sd->aa.path, sd) == 0) {
			sd->aa.path.clear();
		}
		else {
			clif_walkok(*sd);
			sd->fp.tg.oldx = sd->x;
			sd->fp.tg.oldy = sd->y;
		}
		break;
	}

	case TGStep::NPC1_2: {
		if (unit_is_walking(sd)) return; // not yet to npc

#if old_training == 0
		blvl = 1;

		sd->status.base_level += (uint32)blvl;

		status_calc_pc(sd, SCO_FORCE);
		status_percent_heal(sd, 100, 100);
		clif_misceffect(*sd, NOTIFYEFFECT_BASE_LEVEL_UP);
		clif_updatestatus(*sd, SP_BASELEVEL);
		pc_baselevelchanged(sd);
#endif

		tg.step = TGStep::TP_NPC1;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::TP_NPC1].minMs, TG_DELAY[(int)TGStep::TP_NPC1].maxMs);

		break;
	}

	case TGStep::TP_NPC1: {
		if (pc_setpos(sd, mapindex_name2id(ZC_WNPC2.map), ZC_WNPC2.x1, ZC_WNPC2.y1, CLR_TELEPORT) == SETPOS_OK) {
#if old_training == 0
			tg.step = TGStep::NPC2_1;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_1].minMs, TG_DELAY[(int)TGStep::NPC2_1].maxMs);
#else
			tg.step = TGStep::NPC2_0;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_0].minMs, TG_DELAY[(int)TGStep::NPC2_0].maxMs);
#endif
		}
		else
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::TP_NPC1].minMs, TG_DELAY[(int)TGStep::TP_NPC1].maxMs);

		break;
	}

#if old_training == 1
	case TGStep::NPC2_0: {
		// 1. Si le perso marche déjà physiquement (animation en cours), on attend.
		if (unit_is_walking(sd)) return;

		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);

		// --- VERIFICATION DE L'ARRIVÉE ---
		bool in_zone = (sd->x >= Z_NPC2_0.x1 && sd->x <= Z_NPC2_0.x2 && sd->y >= Z_NPC2_0.y1 && sd->y <= Z_NPC2_0.y2);

		// On valide l'étape SEULEMENT si on est dans la zone ET qu'on a fini le chemin (arrivé sur la case précise)
		if (in_zone && sd->aa.path.empty()) {
			tg.step = TGStep::NPC2_1;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_1].minMs, TG_DELAY[(int)TGStep::NPC2_1].maxMs);

			// On regarde le NPC cible pour faire "vivant"
			struct look_result lr = look_from_player_to_xy(sd, ZC_NPC2.x1, ZC_NPC2.y1);
			pc_setdir(sd, lr.body_dir, lr.head_dir);
			clif_changed_dir(*sd, AREA_WOS);

			// Reset variables de mouvement pour la prochaine étape
			sd->fp.tg.oldx = 0;
			sd->fp.tg.oldy = 0;
			return;
		}

		rc.map = Z_NPC2_0.map; // S'assurer que map data est init

		// --- CONTINUER LE MOUVEMENT EXISTANT ---
		// Si on a déjà un chemin calculé, on continue d'avancer dessus.
		if (!sd->aa.path.empty()) {
			int move_ret = aa_move_to_path(sd->aa.path, sd);

			if (move_ret == 0) {
				// Chemin terminé ou invalide -> on clear pour forcer la recherche d'une nouvelle case au prochain tick
				sd->aa.path.clear();
			}
			else {
				// Vérification Anti-Bloquage (Stuck check)
				if (sd->fp.tg.oldx == sd->x && sd->fp.tg.oldy == sd->y) {
					// On n'a pas bougé depuis la dernière vérif alors qu'on devrait -> Bloqué -> On clear
					sd->aa.path.clear();
				}
				else {
					// Tout va bien, on avance
					clif_walkok(*sd);
					sd->fp.tg.oldx = sd->x;
					sd->fp.tg.oldy = sd->y;
				}
			}
			return; // On a géré le mouvement pour ce tick, on s'arrête là.
		}

		// --- NOUVELLE DESTINATION (Si pas de chemin) ---
		// On cherche une case valide dans la zone.
		rc.x1 = 0;
		rc.y1 = 0;

		int attempts = 0;
		bool found = false;

		// On essaie 15 fois de trouver une case libre de tout joueur
		while (attempts < 15 && !found) {
			// On cherche une case marchable (Murs + NPC vérifiés par ta fonction)
			if (pick_walkable_xy(rc.map, Z_NPC2_0.x1, Z_NPC2_0.y1, Z_NPC2_0.x2, Z_NPC2_0.y2, rc.x1, rc.y1, 50, true)) {
				// Vérification supplémentaire : Est-ce qu'il y a un JOUEUR sur cette case ?
				// BL_PC = Players. Si la fonction retourne 0, la case est libre de joueurs.
				if (map_count_oncell(sd->m, rc.x1, rc.y1, BL_PC, 0) == 0) {
					found = true;
				}
			}
			attempts++;
		}

		// Si après les essais on a rien trouvé ou coord invalide
		if (!found || rc.x1 <= 0 || rc.y1 <= 0) {
			// On met un petit délai avant de réessayer pour ne pas surcharger le serveur
			tg.nextActionTick = now + irand(200, 500);
			break;
		}

		// Calcul du chemin vers la case trouvée
		algorithm_path_finding(sd, sd->m, sd->x, sd->y, rc.x1, rc.y1);

		// Lancement du premier pas
		if (aa_move_to_path(sd->aa.path, sd) == 0) {
			sd->aa.path.clear(); // Echec immédiat (ex: case accessible mais chemin bloqué)
		}
		else {
			clif_walkok(*sd);
			sd->fp.tg.oldx = sd->x;
			sd->fp.tg.oldy = sd->y;
		}
		break;
	}
#endif

	case TGStep::NPC2_1: {
#if old_training == 0
		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);
#endif

		struct look_result lr = look_from_player_to_xy(sd, ZC_NPC2.x1, ZC_NPC2.y1);
		pc_setdir(sd, lr.body_dir, lr.head_dir);
		clif_changed_dir(*sd, AREA_WOS);

		tg.step = TGStep::NPC2_2;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_2].minMs, TG_DELAY[(int)TGStep::NPC2_2].maxMs);

		break;
	}

	case TGStep::NPC2_2: {
		blvl = 2;
		jlvl = 1;

#if old_training == 0
		sd->status.base_level += (uint32)blvl;
#endif
		sd->status.job_level += (uint32)jlvl;

		status_calc_pc(sd, SCO_FORCE);
		status_percent_heal(sd, 100, 100);
		clif_misceffect(*sd, NOTIFYEFFECT_BASE_LEVEL_UP);

		// Stats to help for following kills
		pc_setstat(sd, SP_STR, 15);
		pc_setstat(sd, SP_DEX, 20);
		pc_setstat(sd, SP_AGI, 10);
		pc_setstat(sd, SP_VIT, 30);
		pc_setstat(sd, SP_INT, 15);

		clif_misceffect(*sd, NOTIFYEFFECT_JOB_LEVEL_UP);

		clif_updatestatus(*sd, SP_STATUSPOINT);
		clif_updatestatus(*sd, SP_BASELEVEL);
		clif_updatestatus(*sd, SP_JOBLEVEL);

		pc_baselevelchanged(sd);

#if old_training == 0
		tg.step = TGStep::NPC2_3;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_3].minMs, TG_DELAY[(int)TGStep::NPC2_3].maxMs);
#else
		tg.step = TGStep::NPC2_3b;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_3b].minMs, TG_DELAY[(int)TGStep::NPC2_3b].maxMs);
#endif

		break;
	}

#if old_training == 1
	case TGStep::NPC2_3b: {
		// 1. Si le joueur marche, on le laisse finir son mouvement actuel
		if (unit_is_walking(sd)) return;

		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);

		// On vérifie si on est dans la zone Z_NPC7 ET si le chemin est fini (path.empty)
		bool in_target_zone = (sd->x >= Z_NPC7.x1 && sd->x <= Z_NPC7.x2 && sd->y >= Z_NPC7.y1 && sd->y <= Z_NPC7.y2);

		if (in_target_zone && sd->aa.path.empty() && map_count_oncell(sd->m, sd->x, sd->y, BL_PC, 0) <= 1) {
			tg.step = TGStep::NPC2_3c;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_3c].minMs, TG_DELAY[(int)TGStep::NPC2_3c].maxMs);

			// Orientation vers le NPC cible (ZC_WNPC7)
			struct look_result lr = look_from_player_to_xy(sd, ZC_WNPC7.x1, ZC_WNPC7.y1);
			pc_setdir(sd, lr.body_dir, lr.head_dir);
			clif_changed_dir(*sd, AREA_WOS);

			// Reset
			sd->fp.tg.oldx = 0;
			sd->fp.tg.oldy = 0;
			return;
		}

		rc.map = Z_NPC7.map;
		rc.x1 = 0;
		rc.y1 = 0;


		// Si un chemin existe, on le suit au lieu de le recalculer
		if (!sd->aa.path.empty()) {
			int move_ret = aa_move_to_path(sd->aa.path, sd);

			if (move_ret == 0) {
				sd->aa.path.clear(); // Fin du chemin ou bloqué
			}
			else {
				// Anti-Stuck check
				if (sd->fp.tg.oldx == sd->x && sd->fp.tg.oldy == sd->y) {
					sd->aa.path.clear(); // Bloqué physiquement -> reset
				}
				else {
					clif_walkok(*sd);
					sd->fp.tg.oldx = sd->x;
					sd->fp.tg.oldy = sd->y;
				}
			}
			return;
		}

		int attempts = 0;
		bool found = false;

		// On cherche une case dans Z_NPC7 libre de tout joueur
		while (attempts < 15 && !found) {
			if (pick_walkable_xy(rc.map, Z_NPC7.x1, Z_NPC7.y1, Z_NPC7.x2, Z_NPC7.y2, rc.x1, rc.y1, 50, true)) {
				// Vérifie qu'il n'y a personne (BL_PC) sur la case cible
				if (map_count_oncell(sd->m, rc.x1, rc.y1, BL_PC, 0) == 0) {
					found = true;
				}
			}
			attempts++;
		}

		// Gestion de l'échec de recherche
		if (!found || rc.x1 <= 0 || rc.y1 <= 0) {
			// J'ai remplacé le délai NPC2_0 par un délai court générique pour réessayer rapidement
			tg.nextActionTick = now + irand(200, 500);
			break;
		}

		// Calcul du pathfinding vers la case trouvée
		algorithm_path_finding(sd, sd->m, sd->x, sd->y, rc.x1, rc.y1);

		if (aa_move_to_path(sd->aa.path, sd) == 0) {
			sd->aa.path.clear();
		}
		else {
			clif_walkok(*sd);
			sd->fp.tg.oldx = sd->x;
			sd->fp.tg.oldy = sd->y;
		}
		break;
	}

	case TGStep::NPC2_3c: {
		if (unit_is_walking(sd)) return; // not yet to npc

		blvl = 2;

		sd->status.base_level += (uint32)blvl;

		status_calc_pc(sd, SCO_FORCE);
		status_percent_heal(sd, 100, 100);
		clif_misceffect(*sd, NOTIFYEFFECT_BASE_LEVEL_UP);
		clif_updatestatus(*sd, SP_BASELEVEL);
		pc_baselevelchanged(sd);

		tg.step = TGStep::NPC3_1;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC3_1].minMs, TG_DELAY[(int)TGStep::NPC3_1].maxMs);

		break;
	}
#else
	case TGStep::NPC2_3: {
		std::vector<t_itemid> novice_set = { 5055, 2352, 1243, 2112, 2510, 2414 };

		for (t_itemid item_set : novice_set) {
			struct item item_tmp = {};

			item_tmp.nameid = item_set;
			item_tmp.identify = 1;
			item_tmp.bound = 0;
			char flag = 0;
			if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
				clif_additem(sd, 0, 0, flag);
			}
			pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
		}

		tg.step = TGStep::NPC2_4;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_4].minMs, TG_DELAY[(int)TGStep::NPC2_4].maxMs);

		break;
	}

	case TGStep::NPC2_4: {
		jlvl = 1;

		sd->status.job_level += (uint32)jlvl;

		clif_misceffect(*sd, NOTIFYEFFECT_JOB_LEVEL_UP);
		clif_updatestatus(*sd, SP_JOBLEVEL);

		tg.step = TGStep::NPC3_1;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC3_1].minMs, TG_DELAY[(int)TGStep::NPC3_1].maxMs);

		break;
	}
#endif

	case TGStep::NPC3_1: {
		// 1. Si le joueur est en train de marcher, on attend.
		if (unit_is_walking(sd)) return;

		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);

		// --- VERIFICATION DE L'ARRIVÉE ---
		// On vérifie qu'il est DANS la zone ET qu'il a FINI son chemin.
		bool in_target_zone = (sd->x >= Z_NPC3.x1 && sd->x <= Z_NPC3.x2 && sd->y >= Z_NPC3.y1 && sd->y <= Z_NPC3.y2);

		if (in_target_zone && sd->aa.path.empty() && map_count_oncell(sd->m, sd->x, sd->y, BL_PC, 0) <= 1) {

			// --- LOGIQUE DE CHANGEMENT D'ÉTAPE (Conservée) ---
#if old_training == 0
			tg.step = TGStep::NPC2_5;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_5].minMs, TG_DELAY[(int)TGStep::NPC2_5].maxMs);
#else
			tg.step = TGStep::NPC2_5b;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_5b].minMs, TG_DELAY[(int)TGStep::NPC2_5b].maxMs);
#endif

			struct look_result lr = look_from_player_to_xy(sd, ZC_NPC3.x1, ZC_NPC3.y1);
			pc_setdir(sd, lr.body_dir, lr.head_dir);
			clif_changed_dir(*sd, AREA_WOS);

			// Reset positions
			sd->fp.tg.oldx = 0;
			sd->fp.tg.oldy = 0;

			return;
		}

		rc.map = Z_NPC3.map;
		rc.x1 = 0;
		rc.y1 = 0;

		// --- CONTINUER LE MOUVEMENT EXISTANT ---
		// Si un chemin est déjà calculé, on continue dessus.
		// L'ancien code faisait un sd->aa.path.clear() ici, ce qui cassait le mouvement.
		if (!sd->aa.path.empty()) {
			int move_ret = aa_move_to_path(sd->aa.path, sd);

			if (move_ret == 0) {
				sd->aa.path.clear(); // Fin de chemin ou bloqué
			}
			else {
				// Anti-Stuck : Si on n'a pas bougé depuis la dernière fois
				if (sd->fp.tg.oldx == sd->x && sd->fp.tg.oldy == sd->y) {
					sd->aa.path.clear();
				}
				else {
					clif_walkok(*sd);
					sd->fp.tg.oldx = sd->x;
					sd->fp.tg.oldy = sd->y;
				}
			}
			return;
		}

		// --- RECHERCHE DE CASE LIBRE (ANTI-EMPILEMENT) ---
		int attempts = 0;
		bool found = false;

		// On cherche une case dans Z_NPC3
		while (attempts < 15 && !found) {
			if (pick_walkable_xy(rc.map, Z_NPC3.x1, Z_NPC3.y1, Z_NPC3.x2, Z_NPC3.y2, rc.x1, rc.y1, 50, true)) {
				// On vérifie qu'aucun joueur (BL_PC) n'est sur la case
				if (map_count_oncell(sd->m, rc.x1, rc.y1, BL_PC, 0) == 0) {
					found = true;
				}
			}
			attempts++;
		}

		// Gestion de l'échec de recherche
		if (!found || rc.x1 <= 0 || rc.y1 <= 0) {
			// Petit délai avant de réessayer pour ne pas spammer
			tg.nextActionTick = now + irand(200, 500);
			break;
		}

		// Calcul du pathfinding
		algorithm_path_finding(sd, sd->m, sd->x, sd->y, rc.x1, rc.y1);

		// Lancement du mouvement
		if (aa_move_to_path(sd->aa.path, sd) == 0) {
			sd->aa.path.clear();
		}
		else {
			if (sd->x != sd->fp.tg.oldx || sd->y != sd->fp.tg.oldy) {
				clif_walkok(*sd);
				sd->fp.tg.oldx = sd->x;
				sd->fp.tg.oldy = sd->y;
			}
		}
		break;
	}

#if old_training == 1
	case TGStep::NPC2_5b: {
		std::vector<t_itemid> novice_set = { 5055, 2352, 2510, 2414 };

		for (t_itemid item_set : novice_set) {
			struct item item_tmp = {};

			item_tmp.nameid = item_set;
			item_tmp.identify = 1;
			item_tmp.bound = 0;
			char flag = 0;
			if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
				clif_additem(sd, 0, 0, flag);
			}
			pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
		}

		jlvl = 1;

		sd->status.job_level += (uint32)jlvl;

		clif_misceffect(*sd, NOTIFYEFFECT_JOB_LEVEL_UP);
		clif_updatestatus(*sd, SP_JOBLEVEL);
		status_percent_heal(sd, 100, 100);

		tg.step = TGStep::NPC2_5;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC2_5].minMs, TG_DELAY[(int)TGStep::NPC2_5].maxMs);

		break;
	}
#endif

	case TGStep::NPC2_5: {
		// 1. Si le joueur marche physiquement, on attend qu'il arrive sur la case suivante.
		if (unit_is_walking(sd)) return;

		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);

		// --- VERIFICATION DE L'ARRIVÉE ---
		// On vérifie qu'il est dans la zone Z_NPC2 ET qu'il a terminé son trajet.
		bool in_target_zone = (sd->x >= Z_NPC2.x1 && sd->x <= Z_NPC2.x2 && sd->y >= Z_NPC2.y1 && sd->y <= Z_NPC2.y2);

		if (in_target_zone && sd->aa.path.empty() && map_count_oncell(sd->m, sd->x, sd->y, BL_PC, 0) <= 1) {
			tg.step = TGStep::TP_NPC2;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::TP_NPC2].minMs, TG_DELAY[(int)TGStep::TP_NPC2].maxMs);

			struct look_result lr = look_from_player_to_xy(sd, ZC_NPC2.x1, ZC_NPC2.y1);
			pc_setdir(sd, lr.body_dir, lr.head_dir);
			clif_changed_dir(*sd, AREA_WOS);

			// Reset des anciennes positions
			sd->fp.tg.oldx = 0;
			sd->fp.tg.oldy = 0;
			return;
		}

		rc.map = Z_NPC2.map;
		rc.x1 = 0;
		rc.y1 = 0;

		// --- CONTINUITÉ DU MOUVEMENT ---
		// Si un chemin existe, on continue dessus au lieu de le casser.
		if (!sd->aa.path.empty()) {
			int move_ret = aa_move_to_path(sd->aa.path, sd);

			if (move_ret == 0) {
				sd->aa.path.clear(); // Fin de chemin ou bloqué
			}
			else {
				// Anti-Stuck : Si on n'a pas bougé depuis le dernier tick alors qu'on devrait
				if (sd->fp.tg.oldx == sd->x && sd->fp.tg.oldy == sd->y) {
					sd->aa.path.clear(); // On force le recalcul
				}
				else {
					clif_walkok(*sd);
					sd->fp.tg.oldx = sd->x;
					sd->fp.tg.oldy = sd->y;
				}
			}
			return;
		}

		// --- RECHERCHE DE CASE LIBRE (ANTI-EMPILEMENT) ---
		int attempts = 0;
		bool found = false;

		// On cherche une case dans Z_NPC2 libre de tout joueur
		while (attempts < 15 && !found) {
			if (pick_walkable_xy(rc.map, Z_NPC2.x1, Z_NPC2.y1, Z_NPC2.x2, Z_NPC2.y2, rc.x1, rc.y1, 50, true)) {
				// Si la case est marchable, on vérifie qu'il n'y a pas déjà un joueur (BL_PC) dessus
				if (map_count_oncell(sd->m, rc.x1, rc.y1, BL_PC, 0) == 0) {
					found = true;
				}
			}
			attempts++;
		}

		// Gestion de l'échec de recherche (map trop pleine ou pas de chance)
		if (!found || rc.x1 <= 0 || rc.y1 <= 0) {
			// Petit délai avant de réessayer
			tg.nextActionTick = now + irand(200, 500);
			break;
		}

		// Calcul du pathfinding vers la case trouvée
		algorithm_path_finding(sd, sd->m, sd->x, sd->y, rc.x1, rc.y1);

		// Lancement du mouvement
		if (aa_move_to_path(sd->aa.path, sd) == 0) {
			sd->aa.path.clear();
		}
		else {
			if (sd->x != sd->fp.tg.oldx || sd->y != sd->fp.tg.oldy) {
				clif_walkok(*sd);
				sd->fp.tg.oldx = sd->x;
				sd->fp.tg.oldy = sd->y;
			}
		}
		break;
	}

	case TGStep::TP_NPC2: {
		struct look_result lr = look_from_player_to_xy(sd, ZC_NPC2.x1, ZC_NPC2.y1);
		pc_setdir(sd, lr.body_dir, lr.head_dir);
		clif_changed_dir(*sd, AREA_WOS);

		if (pc_setpos(sd, mapindex_name2id(ZC_WNPC4.map), ZC_WNPC4.x1, ZC_WNPC4.y1, CLR_TELEPORT) == SETPOS_OK) {
			tg.step = TGStep::NPC4_1;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC4_1].minMs, TG_DELAY[(int)TGStep::NPC4_1].maxMs);
		}
		else
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::TP_NPC2].minMs, TG_DELAY[(int)TGStep::TP_NPC2].maxMs);

		break;
	}

	case TGStep::NPC4_1: {
		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);

		struct look_result lr = look_from_player_to_xy(sd, ZC_NPC4.x1, ZC_NPC4.y1);
		pc_setdir(sd, lr.body_dir, lr.head_dir);
		clif_changed_dir(*sd, AREA_WOS);

		tg.step = TGStep::NPC4_2;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC4_2].minMs, TG_DELAY[(int)TGStep::NPC4_2].maxMs);

		break;
	}

	case TGStep::NPC4_2: {
#if old_training == 0
		struct look_result lr = look_from_player_to_xy(sd, ZC_NPC4.x1, ZC_NPC4.y1);
		pc_setdir(sd, lr.body_dir, lr.head_dir);
		clif_changed_dir(*sd, AREA_WOS);

		tg.step = TGStep::NPC4_3;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::NPC4_3].minMs, TG_DELAY[(int)TGStep::NPC4_3].maxMs);
#else
		std::vector<t_itemid> novice_set = { 1243, 2112 };

		for (t_itemid item_set : novice_set) {
			struct item item_tmp = {};

			item_tmp.nameid = item_set;
			item_tmp.identify = 1;
			item_tmp.bound = 0;
			char flag = 0;
			if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
				clif_additem(sd, 0, 0, flag);
			}
			pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));
		}

		tg.step = TGStep::TP_NPC4;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::TP_NPC4].minMs, TG_DELAY[(int)TGStep::TP_NPC4].maxMs);
#endif

		break;
	}

	case TGStep::NPC4_3: {
		blvl = 1;

		sd->status.base_level += (uint32)blvl;

		status_calc_pc(sd, SCO_FORCE);
		status_percent_heal(sd, 100, 100);
		clif_misceffect(*sd, NOTIFYEFFECT_BASE_LEVEL_UP);
		clif_updatestatus(*sd, SP_BASELEVEL);
		pc_baselevelchanged(sd);

		tg.step = TGStep::TP_NPC4;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::TP_NPC4].minMs, TG_DELAY[(int)TGStep::TP_NPC4].maxMs);

		break;
	}

	case TGStep::TP_NPC4: {
		if (pc_setpos(sd, mapindex_name2id(ZC_WNPC5.map), ZC_WNPC5.x1, ZC_WNPC5.y1, CLR_TELEPORT) == SETPOS_OK) {
			// in case of die
			safestrncpy(sd->status.save_point.map, ZC_WNPC5.map, sizeof(ZC_WNPC5.map) + 1);
			sd->status.save_point.x = ZC_WNPC5.x1;
			sd->status.save_point.y = ZC_WNPC5.y1;

			tg.step = TGStep::COMBAT1_1;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::COMBAT1_1].minMs, TG_DELAY[(int)TGStep::COMBAT1_1].maxMs);
		}
		else
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::TP_NPC4].minMs, TG_DELAY[(int)TGStep::TP_NPC4].maxMs);

		break;
	}

	case TGStep::COMBAT1_1: {
		pc_delinvincibletimer(sd);
		clif_parse_LoadEndAck(0, sd);

#if old_training == 0
		//sd->aa.mobs.id.push_back(1002); // kill poring so ignore others
		sd->aa.mobs.id.push_back(1113);
		sd->aa.mobs.id.push_back(1063);
		sd->aa.mobs.id.push_back(1011);
		sd->aa.mobs.id.push_back(1009);
		sd->aa.mobs.id.push_back(1050);
		sd->aa.mobs.id.push_back(1010);
		sd->aa.mobs.id.push_back(1012);
		sd->aa.mobs.id.push_back(1184);

		tg.step = TGStep::COMBAT1_2;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::COMBAT1_2].minMs, TG_DELAY[(int)TGStep::COMBAT1_2].maxMs);
#else
		tg.step = TGStep::COMBAT1_2b;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::COMBAT1_2b].minMs, TG_DELAY[(int)TGStep::COMBAT1_2b].maxMs);
#endif
		break;
	}

	case TGStep::COMBAT1_2: {
		//break because he needs to kill a poring
		break;
	}

	case TGStep::COMBAT1_2b: {
		// 1. Si le personnage est en mouvement physique, on ne fait rien
		if (unit_is_walking(sd)) return;

		bool in_target_zone = (sd->x >= Z_COMBAT5.x1 && sd->x <= Z_COMBAT5.x2 && sd->y >= Z_COMBAT5.y1 && sd->y <= Z_COMBAT5.y2);

		// On valide l'étape si on est DANS la zone ET qu'on a fini de marcher
		if (in_target_zone && sd->aa.path.empty() && map_count_oncell(sd->m, sd->x, sd->y, BL_PC, 0) <= 1) {
			tg.step = TGStep::COMBAT1_3;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::COMBAT1_3].minMs, TG_DELAY[(int)TGStep::COMBAT1_3].maxMs);

			// Note: Pas de changement de direction (look_at) dans ton code original ici, 
			// donc je n'en ai pas ajouté, mais c'est ici qu'il faudrait le mettre si besoin.

			// Reset positions
			sd->fp.tg.oldx = 0;
			sd->fp.tg.oldy = 0;
			return;
		}

		rc.map = Z_COMBAT5.map;
		rc.x1 = 0;
		rc.y1 = 0;

		// Si un chemin est déjà calculé, on continue de le suivre
		if (!sd->aa.path.empty()) {
			int move_ret = aa_move_to_path(sd->aa.path, sd);

			if (move_ret == 0) {
				sd->aa.path.clear(); // Chemin fini ou invalide
			}
			else {
				// Anti-Stuck : Si la position n'a pas changé alors qu'on devrait bouger
				if (sd->fp.tg.oldx == sd->x && sd->fp.tg.oldy == sd->y) {
					sd->aa.path.clear(); // On force le recalcul
				}
				else {
					clif_walkok(*sd);
					sd->fp.tg.oldx = sd->x;
					sd->fp.tg.oldy = sd->y;
				}
			}
			return;
		}

		// --- RECHERCHE DE CASE LIBRE (ANTI-EMPILEMENT) ---
		int attempts = 0;
		bool found = false;

		// On cherche une case dans Z_COMBAT5 libre de tout joueur
		while (attempts < 15 && !found) {
			if (pick_walkable_xy(rc.map, Z_COMBAT5.x1, Z_COMBAT5.y1, Z_COMBAT5.x2, Z_COMBAT5.y2, rc.x1, rc.y1, 50, true)) {
				// Vérifie qu'il n'y a pas de joueur (BL_PC) sur la case cible
				if (map_count_oncell(sd->m, rc.x1, rc.y1, BL_PC, 0) == 0) {
					found = true;
				}
			}
			attempts++;
		}

		// Gestion de l'échec de recherche (Zone pleine ou pas de chemin trouvé)
		if (!found || rc.x1 <= 0 || rc.y1 <= 0) {
			// Petit délai (200-500ms) pour réessayer rapidement sans spammer le CPU
			// Ton code original utilisait le délai de COMBAT1_2 ici, ce qui semblait être une erreur de copier-coller.
			tg.nextActionTick = now + irand(200, 500);
			break;
		}

		// Calcul du pathfinding vers la case trouvée
		algorithm_path_finding(sd, sd->m, sd->x, sd->y, rc.x1, rc.y1);

		// Lancement du mouvement
		if (aa_move_to_path(sd->aa.path, sd) == 0) {
			sd->aa.path.clear();
		}
		else {
			if (sd->x != sd->fp.tg.oldx || sd->y != sd->fp.tg.oldy) {
				clif_walkok(*sd);
				sd->fp.tg.oldx = sd->x;
				sd->fp.tg.oldy = sd->y;
			}
		}
		break;
	}

	case TGStep::COMBAT1_3: {
		if (unit_is_walking(sd)) return; // not yet to npc

		struct look_result lr = look_from_player_to_xy(sd, ZC_NPC6.x1, ZC_NPC6.y1);
		pc_setdir(sd, lr.body_dir, lr.head_dir);
		clif_changed_dir(*sd, AREA_WOS);

#if old_training == 0
		std::vector<std::pair<t_itemid, uint16>> pool = {
			{2821, 15},  // Acolyte Manual
			{2822, 5},   // Archer Manual
			{2824, 40},  // Mage Manual
			{2819, 25},  // Swordsman Manual
			{2820, 25},  // Thief Manual
		};

		t_itemid picked = 0;

		uint64_t total = 0;
		for (const auto& p : pool)
			if (p.second > 0) total += p.second;

		if (total > 0) {
			int r = rnd()%static_cast<int>(total); // 0..total-1

			uint64_t acc = 0;
			for (const auto& p : pool) {
				if (p.second <= 0) continue;
				acc += p.second;
				if (r < acc) {
					picked = p.first; // item_id choisi
					break;
				}
			}
		}

		if (picked > 0) {
			struct item item_tmp = {};

			item_tmp.nameid = picked;
			item_tmp.identify = 1;
			item_tmp.bound = 0;
			char flag = 0;
			if ((flag = pc_additem(sd, &item_tmp, 1, LOG_TYPE_COMMAND))) {
				clif_additem(sd, 0, 0, flag);
			}
			pc_equipitem(sd, sd->last_addeditem_index, itemdb_equip(item_tmp.nameid));

			struct s_autoattackskills autoattackskills;
			autoattackskills.is_active = 1;
			autoattackskills.skill_id = 0;
			autoattackskills.skill_lv = 1;
			autoattackskills.last_use = 1;

			struct s_autobuffskills autobuffskills;
			autobuffskills.is_active = 1;
			autobuffskills.skill_id = 0;
			autobuffskills.skill_lv = 1;
			autobuffskills.last_use = 1;

			switch (picked) {
				case 2821: // acolyte
					autobuffskills.skill_id = AL_HEAL;
					sd->aa.autobuffskills.push_back(autobuffskills);
					autobuffskills.skill_id = AL_INCAGI;
					sd->aa.autobuffskills.push_back(autobuffskills);
					autobuffskills.skill_id = AL_BLESSING;
					sd->aa.autobuffskills.push_back(autobuffskills);
					break;
				case 2822: // archer
					autobuffskills.skill_id = AC_CONCENTRATION;
					sd->aa.autobuffskills.push_back(autobuffskills);
					break;
				case 2824: // mage
					autoattackskills.skill_id = MG_COLDBOLT;
					sd->aa.autoattackskills.push_back(autoattackskills);
					autoattackskills.skill_id = MG_FIREBOLT;
					sd->aa.autoattackskills.push_back(autoattackskills);
					break;
				case 2819: // swordman
					autoattackskills.skill_id = SM_BASH;
					sd->aa.autoattackskills.push_back(autoattackskills);
					break;
				case 2820: // thief
					autoattackskills.skill_id = TF_POISON;
					sd->aa.autoattackskills.push_back(autoattackskills);
					break;
			}
		}

		sd->aa.mobs.id.clear();
#endif

		uint16 changemap = (uint16)((uint32)rnd() % 6);
		switch (changemap) {
		case 0:
		case 1:
			// Nothing
			break;
		case 2:
			if (pc_setpos(sd, mapindex_name2id("new_2-3"), sd->x, sd->y, CLR_TELEPORT) == SETPOS_OK) {
				pc_delinvincibletimer(sd);
				clif_parse_LoadEndAck(0, sd);
			}
			break;
		case 3:
			if (pc_setpos(sd, mapindex_name2id("new_3-3"), sd->x, sd->y, CLR_TELEPORT) == SETPOS_OK) {
				pc_delinvincibletimer(sd);
				clif_parse_LoadEndAck(0, sd);
			}
			break;
		case 4:
			if (pc_setpos(sd, mapindex_name2id("new_4-3"), sd->x, sd->y, CLR_TELEPORT) == SETPOS_OK) {
				pc_delinvincibletimer(sd);
				clif_parse_LoadEndAck(0, sd);
			}
			break;
		case 5:
			if (pc_setpos(sd, mapindex_name2id("new_5-3"), sd->x, sd->y, CLR_TELEPORT) == SETPOS_OK) {
				pc_delinvincibletimer(sd);
				clif_parse_LoadEndAck(0, sd);
			}
			break;
		}

		tg.step = TGStep::COMBAT1_4;
		tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::COMBAT1_4].minMs, TG_DELAY[(int)TGStep::COMBAT1_4].maxMs);

		break;
	}

	case TGStep::COMBAT1_4: {
		if (sd->status.job_level >= 10) {
			rc.map = Z_COMBAT5.map;
			rc.x1 = 0;
			rc.y1 = 0;

			// Tente une coordonnee marchable
			pick_walkable_xy(rc.map, Z_COMBAT5.x1, Z_COMBAT5.y1, Z_COMBAT5.x2, Z_COMBAT5.y2, rc.x1, rc.y1, 200, true);

			if (rc.x1 <= 0 || rc.y1 <= 0) {
				tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::COMBAT1_2].minMs, TG_DELAY[(int)TGStep::COMBAT1_2].maxMs);
				break;
			}

			algorithm_path_finding(sd, sd->m, sd->x, sd->y, rc.x1, rc.y1);

			if (aa_move_to_path(sd->aa.path, sd) == 0) {
				sd->aa.path.clear();
			}
			else {
				clif_walkok(*sd);
				tg.step = TGStep::DONE;
				tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::DONE].minMs, TG_DELAY[(int)TGStep::DONE].maxMs);
			}
		}
		break;
	}

	case TGStep::DONE: {
		// 1. Si le joueur marche encore, on attend.
		if (unit_is_walking(sd)) return;

		// On vérifie qu'il est dans la zone Z_COMBAT5 ET qu'il a terminé son chemin.
		bool in_target_zone = (sd->x >= Z_COMBAT5.x1 && sd->x <= Z_COMBAT5.x2 && sd->y >= Z_COMBAT5.y1 && sd->y <= Z_COMBAT5.y2);

		if (in_target_zone && sd->aa.path.empty() && map_count_oncell(sd->m, sd->x, sd->y, BL_PC, 0) <= 1) {
			// Orientation vers le NPC final (Z_NPC5)
			struct look_result lr = look_from_player_to_xy(sd, Z_NPC5.x1, Z_NPC5.y1);
			pc_setdir(sd, lr.body_dir, lr.head_dir);
			clif_changed_dir(*sd, AREA_WOS);

			// Changement d'étape
			tg.step = TGStep::DONE_2;
			tg.nextActionTick = now + irand(TG_DELAY[(int)TGStep::DONE_2].minMs, TG_DELAY[(int)TGStep::DONE_2].maxMs);

			// --- GESTION DU TIMER DE DECONNEXION ---
			if (sd->fp.fake_player_tg_quit != INVALID_TIMER)
				delete_timer(sd->fp.fake_player_tg_quit, fakeplayer_tg_quit_timer);

			// Disconnect reach it ! (Délai aléatoire entre 3s et 15s)
			sd->fp.fake_player_tg_quit = add_timer(gettick() + irand(3000, 15000), fakeplayer_tg_quit_timer, sd->id, 0);

			// Reset positions
			sd->fp.tg.oldx = 0;
			sd->fp.tg.oldy = 0;
			return;
		}

		rc.map = Z_COMBAT5.map;
		rc.x1 = 0;
		rc.y1 = 0;

		// Si un chemin est déjà calculé, on le termine.
		if (!sd->aa.path.empty()) {
			int move_ret = aa_move_to_path(sd->aa.path, sd);

			if (move_ret == 0) {
				sd->aa.path.clear(); // Chemin terminé ou bloqué
			}
			else {
				// Anti-Stuck : Si on n'a pas bougé depuis le dernier tick
				if (sd->fp.tg.oldx == sd->x && sd->fp.tg.oldy == sd->y) {
					sd->aa.path.clear(); // On force le recalcul
				}
				else {
					clif_walkok(*sd);
					sd->fp.tg.oldx = sd->x;
					sd->fp.tg.oldy = sd->y;
				}
			}
			return;
		}

		int attempts = 0;
		bool found = false;

		// On cherche une case dans Z_COMBAT5 libre de tout joueur pour la pose finale
		while (attempts < 15 && !found) {
			if (pick_walkable_xy(rc.map, Z_COMBAT5.x1, Z_COMBAT5.y1, Z_COMBAT5.x2, Z_COMBAT5.y2, rc.x1, rc.y1, 50, true)) {
				// On vérifie qu'il n'y a pas de joueur (BL_PC) sur la case cible
				if (map_count_oncell(sd->m, rc.x1, rc.y1, BL_PC, 0) == 0) {
					found = true;
				}
			}
			attempts++;
		}

		// Gestion de l'échec de recherche
		if (!found || rc.x1 <= 0 || rc.y1 <= 0) {
			// Petit délai avant retry (Correction du TGStep::COMBAT1_2 qui était probablement une erreur de copier-coller)
			tg.nextActionTick = now + irand(200, 500);
			break;
		}

		// Calcul du pathfinding vers la case trouvée
		algorithm_path_finding(sd, sd->m, sd->x, sd->y, rc.x1, rc.y1);

		// Lancement du mouvement
		if (aa_move_to_path(sd->aa.path, sd) == 0) {
			sd->aa.path.clear();
		}
		else {
			if (sd->x != sd->fp.tg.oldx || sd->y != sd->fp.tg.oldy) {
				clif_walkok(*sd);
				sd->fp.tg.oldx = sd->x;
				sd->fp.tg.oldy = sd->y;
			}
		}
		break;
	}

	case TGStep::DONE_2: {
		// nothing yet
		break;
	}
	}
}


void fakecurve_start_timer()
{
	// first fire in 30s so the server finishes coming online
	add_timer(gettick() + battle_config.fake_adjust_period_sec * 1000, fakecurve_global_timer, 0, 0);
}
/**
 * Destroy the fakeprofils module
 * called in map::do_init
 */
void do_final_fakeprofils(void) {
	fake_chat_db.clear();
	fake_players_db.clear();
	fake_jobprofils_db.clear();
}

/**
 * Initialise the fakeprofils module
 * called in map::do_final
 */
void do_init_fakeprofils(void) {
	fake_jobprofils_db.load();
	fake_players_db.load();

	fake_chat_db.load();
	fakechat_build_matcher();

	fake_accounts_purge();
	fake_boot_cycle_on_map();

	add_timer_func_list(fakeplayer_init_timer, "fakeplayer_init_timer");
	add_timer_func_list(fakecurve_global_timer, "fakecurve_global_timer");
	add_timer_func_list(fakechat_send_timer, "fakechat_send_timer");
	add_timer_func_list(fakeplayer_tg_quit_timer, "fakeplayer_tg_quit_timer");

	fakecurve_start_timer();
}
