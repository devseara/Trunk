// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "item_transparency.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/database.hpp>
#include <common/nullpo.hpp>
#include <common/showmsg.hpp>
#include <common/sql.hpp>

using std::string;

extern Sql* mmysql_handle;

// ------------------------------------------------------------
// Internal state
// ------------------------------------------------------------

static int32 item_transparency_timer_id = INVALID_TIMER;

static int32 item_transparency_refresh_interval_ms = 300000; // 5 minutes default

struct s_item_transparency_config {
	bool enable = true;
	int32 refresh_interval_ms = 300000;
	std::vector<std::string> tables;
	std::vector<uint32> mvp_cards;
	std::vector<uint32> rare_items;
	bool ignore_gms = true;
	bool ignore_banned_accounts = true;
	bool ignore_deleted_characters = true;
};

class ItemTransparencyDatabase : public YamlDatabase {
public:
	ItemTransparencyDatabase() : YamlDatabase("ITEM_TRANSPARENCY_DB", 1) {}

	const std::string getDefaultLocation() override {
		return std::string(db_path) + "/item_transparency.yml";
	}

	uint64 parseBodyNode(const ryml::NodeRef& node) override {
		std::string keyName;
		c4::from_chars(node.key(), &keyName);

		if (keyName == "Settings") {
			bool b;
			if (this->nodeExists(node, "Enable") && this->asBool(node, "Enable", b))
				this->config.enable = b;
			int32 refresh;
			if (this->nodeExists(node, "RefreshIntervalMs") && this->asInt32(node, "RefreshIntervalMs", refresh)) {
				if (refresh >= 1000) // safety: >= 1s
					this->config.refresh_interval_ms = refresh;
			}
			if (this->nodeExists(node, "IgnoreGMs") && this->asBool(node, "IgnoreGMs", b))
				this->config.ignore_gms = b;
			if (this->nodeExists(node, "IgnoreBannedAccounts") && this->asBool(node, "IgnoreBannedAccounts", b))
				this->config.ignore_banned_accounts = b;
			if (this->nodeExists(node, "IgnoreDeletedCharacters") && this->asBool(node, "IgnoreDeletedCharacters", b))
				this->config.ignore_deleted_characters = b;

			if (this->nodeExists(node, "Tables")) {
				const auto& tablesNode = node["Tables"];
				if (!tablesNode.is_seq()) {
					this->invalidWarning(tablesNode, "Settings.Tables must be a sequence.\n");
				} else {
					this->config.tables.clear();
					for (const auto& t : tablesNode) {
						std::string s;
						try {
							c4::from_chars(t.val(), &s);
							if (!s.empty())
								this->config.tables.push_back(s);
						} catch (std::runtime_error const&) {
							this->invalidWarning(t, "Invalid table name in Settings.Tables, skipping.\n");
							continue;
						}
					}
				}
			}

			return 1;
		}

		if (keyName == "Groups") {
			// MvpCards
			if (this->nodeExists(node, "MvpCards")) {
				const auto& ids = node["MvpCards"];
				if (ids.is_seq()) {
					this->config.mvp_cards.clear();
					for (const auto& idNode : ids) {
						uint32 id;
						try {
							idNode >> id;
							this->config.mvp_cards.push_back(id);
						} catch (std::runtime_error const&) {
							this->invalidWarning(idNode, "Invalid MvpCards item id, skipping.\n");
							continue;
						}
					}
				} else {
					this->invalidWarning(ids, "Groups.MvpCards must be a sequence.\n");
				}
			}

			// RareItems
			if (this->nodeExists(node, "RareItems")) {
				const auto& ids = node["RareItems"];
				if (ids.is_seq()) {
					this->config.rare_items.clear();
					for (const auto& idNode : ids) {
						uint32 id;
						try {
							idNode >> id;
							this->config.rare_items.push_back(id);
						} catch (std::runtime_error const&) {
							this->invalidWarning(idNode, "Invalid RareItems item id, skipping.\n");
							continue;
						}
					}
				} else {
					this->invalidWarning(ids, "Groups.RareItems must be a sequence.\n");
				}
			}

			return 1;
		}

		return 0;
	}

	void clear() override {
		config = {};
	}

	s_item_transparency_config config;
};

static ItemTransparencyDatabase item_transparency_db;

// One row for Community Checker: who holds this item (or has it compounded), refine, equipment nameid, cards
struct s_item_transparency_holder_row {
	int refine = 0;
	std::string owner_name;
	std::string source;
	uint32 nameid = 0;
	uint32 card0 = 0, card1 = 0, card2 = 0, card3 = 0;
};

struct s_item_transparency_group_state {
	std::vector<uint32> item_ids;
	std::unordered_map<uint32, int64> counts;
	std::unordered_map<uint32, std::vector<s_item_transparency_holder_row>> holders;
	t_tick last_refresh_tick = 0;
	bool refresh_in_progress = false;
};

static std::vector<string> item_transparency_tables = {
	"inventory",
	"cart_inventory",
	"storage",
	"guild_storage",
	"mail_attachments",
};

static s_item_transparency_group_state g_mvp_cards;
static s_item_transparency_group_state g_rare_items;

static inline string tolower_copy(const string& s) {
	string out = s;
	std::transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

static bool parse_group(const char* group_str, e_item_transparency_group& out) {
	if (group_str == nullptr)
		return false;

	const string g = tolower_copy(string(group_str));
	if (g == "mvp" || g == "mvp_cards" || g == "mvpcards" || g == "mvp_card" || g == "mvpcard") {
		out = e_item_transparency_group::MVP_CARDS;
		return true;
	}
	if (g == "rare" || g == "rare_items" || g == "rareitems" || g == "rare_item" || g == "rareitem") {
		out = e_item_transparency_group::RARE_ITEMS;
		return true;
	}
	return false;
}

static s_item_transparency_group_state& group_state(e_item_transparency_group group) {
	return (group == e_item_transparency_group::MVP_CARDS) ? g_mvp_cards : g_rare_items;
}

static string build_in_list(const std::vector<uint32>& ids) {
	string in;
	in.reserve(ids.size() * 6);
	for (size_t i = 0; i < ids.size(); ++i) {
		if (i)
			in += ",";
		in += std::to_string(ids[i]);
	}
	return in;
}

static void refresh_group(s_item_transparency_group_state& st, const char* group_name) {
	if (st.refresh_in_progress)
		return;

	st.refresh_in_progress = true;

	st.counts.clear();
	st.counts.reserve(st.item_ids.size());
	for (uint32 id : st.item_ids)
		st.counts.emplace(id, 0);

	const string in_list = build_in_list(st.item_ids);
	if (in_list.empty()) {
		st.last_refresh_tick = gettick();
		st.refresh_in_progress = false;
		return;
	}

	const s_item_transparency_config& cfg = item_transparency_db.config;
	std::string login_filter;
	if (cfg.ignore_gms) login_filter += " AND l.group_id NOT BETWEEN 10 AND 99";
	if (cfg.ignore_banned_accounts) login_filter += " AND l.state = 0";
	if (login_filter.empty()) login_filter = " AND 1=1";
	std::string char_filter = cfg.ignore_deleted_characters ? " AND c.delete_date = 0" : "";
	std::string storage_exists = cfg.ignore_deleted_characters
		? "EXISTS (SELECT 1 FROM `char` ch WHERE ch.account_id = s.account_id AND ch.delete_date = 0)"
		: "EXISTS (SELECT 1 FROM `char` ch WHERE ch.account_id = s.account_id)";

	auto add_stack_result = [&st](uint32 nameid, int64 amount) {
		auto it = st.counts.find(nameid);
		if (it != st.counts.end())
			it->second += amount;
	};
	auto add_compounded_result = [&st](uint32 cid, int64 cnt) {
		auto it = st.counts.find(cid);
		if (it != st.counts.end())
			it->second += cnt;
	};

	// inventory
	string q_inv_stack = "SELECT i.nameid, SUM(i.amount) FROM inventory i "
		"JOIN `char` c ON i.char_id = c.char_id JOIN login l ON c.account_id = l.account_id "
		"WHERE i.nameid IN (%s)" + login_filter + char_filter + " GROUP BY i.nameid";
	if (Sql_Query(mmysql_handle, q_inv_stack.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 nameid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 amount = data ? static_cast<int64>(atoll(data)) : 0;
			add_stack_result(nameid, amount);
		}
	} else { ShowError("item_transparency: failed query (stack inventory) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	string q_inv_comp = "SELECT cid, SUM(cnt) FROM ("
		"  SELECT i.card0 AS cid, COUNT(*) AS cnt FROM inventory i JOIN `char` c ON i.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE i.card0 IN (%s)" + login_filter + char_filter + " GROUP BY i.card0 "
		"  UNION ALL SELECT i.card1, COUNT(*) FROM inventory i JOIN `char` c ON i.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE i.card1 IN (%s)" + login_filter + char_filter + " GROUP BY i.card1 "
		"  UNION ALL SELECT i.card2, COUNT(*) FROM inventory i JOIN `char` c ON i.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE i.card2 IN (%s)" + login_filter + char_filter + " GROUP BY i.card2 "
		"  UNION ALL SELECT i.card3, COUNT(*) FROM inventory i JOIN `char` c ON i.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE i.card3 IN (%s)" + login_filter + char_filter + " GROUP BY i.card3 "
		") x GROUP BY cid";
	if (Sql_Query(mmysql_handle, q_inv_comp.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 cid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 cnt = data ? static_cast<int64>(atoll(data)) : 0;
			add_compounded_result(cid, cnt);
		}
	} else { ShowError("item_transparency: failed query (compounded inventory) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	// cart_inventory
	string q_cart_stack = "SELECT ci.nameid, SUM(ci.amount) FROM cart_inventory ci "
		"JOIN `char` c ON ci.char_id = c.char_id JOIN login l ON c.account_id = l.account_id "
		"WHERE ci.nameid IN (%s)" + login_filter + char_filter + " GROUP BY ci.nameid";
	if (Sql_Query(mmysql_handle, q_cart_stack.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 nameid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 amount = data ? static_cast<int64>(atoll(data)) : 0;
			add_stack_result(nameid, amount);
		}
	} else { ShowError("item_transparency: failed query (stack cart_inventory) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	string q_cart_comp = "SELECT cid, SUM(cnt) FROM ("
		"  SELECT ci.card0 AS cid, COUNT(*) AS cnt FROM cart_inventory ci JOIN `char` c ON ci.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE ci.card0 IN (%s)" + login_filter + char_filter + " GROUP BY ci.card0 "
		"  UNION ALL SELECT ci.card1, COUNT(*) FROM cart_inventory ci JOIN `char` c ON ci.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE ci.card1 IN (%s)" + login_filter + char_filter + " GROUP BY ci.card1 "
		"  UNION ALL SELECT ci.card2, COUNT(*) FROM cart_inventory ci JOIN `char` c ON ci.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE ci.card2 IN (%s)" + login_filter + char_filter + " GROUP BY ci.card2 "
		"  UNION ALL SELECT ci.card3, COUNT(*) FROM cart_inventory ci JOIN `char` c ON ci.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE ci.card3 IN (%s)" + login_filter + char_filter + " GROUP BY ci.card3 "
		") x GROUP BY cid";
	if (Sql_Query(mmysql_handle, q_cart_comp.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 cid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 cnt = data ? static_cast<int64>(atoll(data)) : 0;
			add_compounded_result(cid, cnt);
		}
	} else { ShowError("item_transparency: failed query (compounded cart_inventory) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	// storage
	string q_stor_stack = "SELECT s.nameid, SUM(s.amount) FROM storage s "
		"JOIN login l ON s.account_id = l.account_id "
		"WHERE s.nameid IN (%s)" + login_filter + " AND " + storage_exists + " GROUP BY s.nameid";
	if (Sql_Query(mmysql_handle, q_stor_stack.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 nameid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 amount = data ? static_cast<int64>(atoll(data)) : 0;
			add_stack_result(nameid, amount);
		}
	} else { ShowError("item_transparency: failed query (stack storage) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	string q_stor_comp = "SELECT cid, SUM(cnt) FROM ("
		"  SELECT s.card0 AS cid, COUNT(*) AS cnt FROM storage s JOIN login l ON s.account_id = l.account_id WHERE s.card0 IN (%s)" + login_filter + " AND " + storage_exists + " GROUP BY s.card0 "
		"  UNION ALL SELECT s.card1, COUNT(*) FROM storage s JOIN login l ON s.account_id = l.account_id WHERE s.card1 IN (%s)" + login_filter + " AND " + storage_exists + " GROUP BY s.card1 "
		"  UNION ALL SELECT s.card2, COUNT(*) FROM storage s JOIN login l ON s.account_id = l.account_id WHERE s.card2 IN (%s)" + login_filter + " AND " + storage_exists + " GROUP BY s.card2 "
		"  UNION ALL SELECT s.card3, COUNT(*) FROM storage s JOIN login l ON s.account_id = l.account_id WHERE s.card3 IN (%s)" + login_filter + " AND " + storage_exists + " GROUP BY s.card3 "
		") x GROUP BY cid";
	if (Sql_Query(mmysql_handle, q_stor_comp.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 cid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 cnt = data ? static_cast<int64>(atoll(data)) : 0;
			add_compounded_result(cid, cnt);
		}
	} else { ShowError("item_transparency: failed query (compounded storage) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	// guild_storage
	string q_guild_stack = "SELECT gs.nameid, SUM(gs.amount) FROM guild_storage gs "
		"JOIN guild g ON gs.guild_id = g.guild_id JOIN `char` c ON g.char_id = c.char_id JOIN login l ON c.account_id = l.account_id "
		"WHERE gs.nameid IN (%s)" + login_filter + char_filter + " GROUP BY gs.nameid";
	if (Sql_Query(mmysql_handle, q_guild_stack.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 nameid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 amount = data ? static_cast<int64>(atoll(data)) : 0;
			add_stack_result(nameid, amount);
		}
	} else { ShowError("item_transparency: failed query (stack guild_storage) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	string q_guild_comp = "SELECT cid, SUM(cnt) FROM ("
		"  SELECT gs.card0 AS cid, COUNT(*) AS cnt FROM guild_storage gs JOIN guild g ON gs.guild_id = g.guild_id JOIN `char` c ON g.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE gs.card0 IN (%s)" + login_filter + char_filter + " GROUP BY gs.card0 "
		"  UNION ALL SELECT gs.card1, COUNT(*) FROM guild_storage gs JOIN guild g ON gs.guild_id = g.guild_id JOIN `char` c ON g.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE gs.card1 IN (%s)" + login_filter + char_filter + " GROUP BY gs.card1 "
		"  UNION ALL SELECT gs.card2, COUNT(*) FROM guild_storage gs JOIN guild g ON gs.guild_id = g.guild_id JOIN `char` c ON g.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE gs.card2 IN (%s)" + login_filter + char_filter + " GROUP BY gs.card2 "
		"  UNION ALL SELECT gs.card3, COUNT(*) FROM guild_storage gs JOIN guild g ON gs.guild_id = g.guild_id JOIN `char` c ON g.char_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE gs.card3 IN (%s)" + login_filter + char_filter + " GROUP BY gs.card3 "
		") x GROUP BY cid";
	if (Sql_Query(mmysql_handle, q_guild_comp.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 cid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 cnt = data ? static_cast<int64>(atoll(data)) : 0;
			add_compounded_result(cid, cnt);
		}
	} else { ShowError("item_transparency: failed query (compounded guild_storage) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	// mail_attachments
	string q_mail_stack = "SELECT ma.nameid, SUM(ma.amount) FROM mail_attachments ma "
		"JOIN mail m ON ma.id = m.id JOIN `char` c ON m.dest_id = c.char_id JOIN login l ON c.account_id = l.account_id "
		"WHERE ma.nameid IN (%s)" + login_filter + char_filter + " GROUP BY ma.nameid";
	if (Sql_Query(mmysql_handle, q_mail_stack.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 nameid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 amount = data ? static_cast<int64>(atoll(data)) : 0;
			add_stack_result(nameid, amount);
		}
	} else { ShowError("item_transparency: failed query (stack mail_attachments) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	string q_mail_comp = "SELECT cid, SUM(cnt) FROM ("
		"  SELECT ma.card0 AS cid, COUNT(*) AS cnt FROM mail_attachments ma JOIN mail m ON ma.id = m.id JOIN `char` c ON m.dest_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE ma.card0 IN (%s)" + login_filter + char_filter + " GROUP BY ma.card0 "
		"  UNION ALL SELECT ma.card1, COUNT(*) FROM mail_attachments ma JOIN mail m ON ma.id = m.id JOIN `char` c ON m.dest_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE ma.card1 IN (%s)" + login_filter + char_filter + " GROUP BY ma.card1 "
		"  UNION ALL SELECT ma.card2, COUNT(*) FROM mail_attachments ma JOIN mail m ON ma.id = m.id JOIN `char` c ON m.dest_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE ma.card2 IN (%s)" + login_filter + char_filter + " GROUP BY ma.card2 "
		"  UNION ALL SELECT ma.card3, COUNT(*) FROM mail_attachments ma JOIN mail m ON ma.id = m.id JOIN `char` c ON m.dest_id = c.char_id JOIN login l ON c.account_id = l.account_id WHERE ma.card3 IN (%s)" + login_filter + char_filter + " GROUP BY ma.card3 "
		") x GROUP BY cid";
	if (Sql_Query(mmysql_handle, q_mail_comp.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* data = nullptr;
			Sql_GetData(mmysql_handle, 0, &data, nullptr);
			uint32 cid = data ? static_cast<uint32>(atoi(data)) : 0;
			Sql_GetData(mmysql_handle, 1, &data, nullptr);
			int64 cnt = data ? static_cast<int64>(atoll(data)) : 0;
			add_compounded_result(cid, cnt);
		}
	} else { ShowError("item_transparency: failed query (compounded mail_attachments) for group '%s'.\n", group_name); Sql_ShowDebug(mmysql_handle); }
	Sql_FreeResult(mmysql_handle);

	// Holder list for Community Checker
	st.holders.clear();
	for (uint32 id : st.item_ids)
		st.holders[id] = {};

	auto add_row_to_holders_correct = [&st](uint32 nameid, int refine, const char* owner, const char* source,
		uint32 c0, uint32 c1, uint32 c2, uint32 c3) {
		s_item_transparency_holder_row row;
		row.nameid = nameid;
		row.refine = refine;
		row.owner_name = owner ? owner : "";
		row.source = source ? source : "";
		row.card0 = c0; row.card1 = c1; row.card2 = c2; row.card3 = c3;
		for (uint32 id : st.item_ids) {
			if (id == nameid || id == c0 || id == c1 || id == c2 || id == c3)
				st.holders[id].push_back(row);
		}
	};

	string q_hold_inv = "SELECT i.nameid, i.refine, c.name, 'Inventory', i.card0, i.card1, i.card2, i.card3 "
		"FROM inventory i JOIN `char` c ON i.char_id = c.char_id JOIN login l ON c.account_id = l.account_id "
		"WHERE (i.nameid IN (%s) OR i.card0 IN (%s) OR i.card1 IN (%s) OR i.card2 IN (%s) OR i.card3 IN (%s)) "
		+ login_filter + char_filter;
	if (Sql_Query(mmysql_handle, q_hold_inv.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* d = nullptr;
			Sql_GetData(mmysql_handle, 0, &d, nullptr); uint32 nameid = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 1, &d, nullptr); int ref = d ? atoi(d) : 0;
			Sql_GetData(mmysql_handle, 2, &d, nullptr); char* owner = d;
			Sql_GetData(mmysql_handle, 3, &d, nullptr); char* src = d;
			Sql_GetData(mmysql_handle, 4, &d, nullptr); uint32 c0 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 5, &d, nullptr); uint32 c1 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 6, &d, nullptr); uint32 c2 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 7, &d, nullptr); uint32 c3 = d ? static_cast<uint32>(atoi(d)) : 0;
			add_row_to_holders_correct(nameid, ref, owner, src, c0, c1, c2, c3);
		}
	}
	Sql_FreeResult(mmysql_handle);

	string q_hold_stor = string("SELECT s.nameid, s.refine, c.name, 'Storage', s.card0, s.card1, s.card2, s.card3 ")
		+ "FROM storage s JOIN `char` c ON s.account_id = c.account_id AND c.char_id = (SELECT MIN(c2.char_id) FROM `char` c2 WHERE c2.account_id = s.account_id"
		+ (cfg.ignore_deleted_characters ? " AND c2.delete_date = 0" : "") + ") "
		+ "JOIN login l ON s.account_id = l.account_id "
		+ "WHERE (s.nameid IN (%s) OR s.card0 IN (%s) OR s.card1 IN (%s) OR s.card2 IN (%s) OR s.card3 IN (%s)) "
		+ login_filter;
	if (Sql_Query(mmysql_handle, q_hold_stor.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* d = nullptr;
			Sql_GetData(mmysql_handle, 0, &d, nullptr); uint32 nameid = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 1, &d, nullptr); int ref = d ? atoi(d) : 0;
			Sql_GetData(mmysql_handle, 2, &d, nullptr); char* owner = d;
			Sql_GetData(mmysql_handle, 3, &d, nullptr); char* src = d;
			Sql_GetData(mmysql_handle, 4, &d, nullptr); uint32 c0 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 5, &d, nullptr); uint32 c1 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 6, &d, nullptr); uint32 c2 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 7, &d, nullptr); uint32 c3 = d ? static_cast<uint32>(atoi(d)) : 0;
			add_row_to_holders_correct(nameid, ref, owner, src, c0, c1, c2, c3);
		}
	}
	Sql_FreeResult(mmysql_handle);

	string q_hold_cart = "SELECT ci.nameid, ci.refine, c.name, 'Cart', ci.card0, ci.card1, ci.card2, ci.card3 "
		"FROM cart_inventory ci JOIN `char` c ON ci.char_id = c.char_id JOIN login l ON c.account_id = l.account_id "
		"WHERE (ci.nameid IN (%s) OR ci.card0 IN (%s) OR ci.card1 IN (%s) OR ci.card2 IN (%s) OR ci.card3 IN (%s)) "
		+ login_filter + char_filter;
	if (Sql_Query(mmysql_handle, q_hold_cart.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* d = nullptr;
			Sql_GetData(mmysql_handle, 0, &d, nullptr); uint32 nameid = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 1, &d, nullptr); int ref = d ? atoi(d) : 0;
			Sql_GetData(mmysql_handle, 2, &d, nullptr); char* owner = d;
			Sql_GetData(mmysql_handle, 3, &d, nullptr); char* src = d;
			Sql_GetData(mmysql_handle, 4, &d, nullptr); uint32 c0 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 5, &d, nullptr); uint32 c1 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 6, &d, nullptr); uint32 c2 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 7, &d, nullptr); uint32 c3 = d ? static_cast<uint32>(atoi(d)) : 0;
			add_row_to_holders_correct(nameid, ref, owner, src, c0, c1, c2, c3);
		}
	}
	Sql_FreeResult(mmysql_handle);

	string q_hold_guild = "SELECT gs.nameid, gs.refine, (SELECT name FROM guild WHERE guild_id = gs.guild_id LIMIT 1), 'Guild Storage', gs.card0, gs.card1, gs.card2, gs.card3 "
		"FROM guild_storage gs JOIN guild g ON gs.guild_id = g.guild_id JOIN `char` c ON g.char_id = c.char_id JOIN login l ON c.account_id = l.account_id "
		"WHERE (gs.nameid IN (%s) OR gs.card0 IN (%s) OR gs.card1 IN (%s) OR gs.card2 IN (%s) OR gs.card3 IN (%s)) "
		+ login_filter + char_filter;
	if (Sql_Query(mmysql_handle, q_hold_guild.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* d = nullptr;
			Sql_GetData(mmysql_handle, 0, &d, nullptr); uint32 nameid = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 1, &d, nullptr); int ref = d ? atoi(d) : 0;
			Sql_GetData(mmysql_handle, 2, &d, nullptr); char* owner = d;
			Sql_GetData(mmysql_handle, 3, &d, nullptr); char* src = d;
			Sql_GetData(mmysql_handle, 4, &d, nullptr); uint32 c0 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 5, &d, nullptr); uint32 c1 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 6, &d, nullptr); uint32 c2 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 7, &d, nullptr); uint32 c3 = d ? static_cast<uint32>(atoi(d)) : 0;
			add_row_to_holders_correct(nameid, ref, owner, src, c0, c1, c2, c3);
		}
	}
	Sql_FreeResult(mmysql_handle);

	string q_hold_mail = "SELECT ma.nameid, ma.refine, c.name, 'Mail', ma.card0, ma.card1, ma.card2, ma.card3 "
		"FROM mail_attachments ma JOIN mail m ON ma.id = m.id JOIN `char` c ON m.dest_id = c.char_id JOIN login l ON c.account_id = l.account_id "
		"WHERE (ma.nameid IN (%s) OR ma.card0 IN (%s) OR ma.card1 IN (%s) OR ma.card2 IN (%s) OR ma.card3 IN (%s)) "
		+ login_filter + char_filter;
	if (Sql_Query(mmysql_handle, q_hold_mail.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str(), in_list.c_str()) == SQL_SUCCESS) {
		while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
			char* d = nullptr;
			Sql_GetData(mmysql_handle, 0, &d, nullptr); uint32 nameid = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 1, &d, nullptr); int ref = d ? atoi(d) : 0;
			Sql_GetData(mmysql_handle, 2, &d, nullptr); char* owner = d;
			Sql_GetData(mmysql_handle, 3, &d, nullptr); char* src = d;
			Sql_GetData(mmysql_handle, 4, &d, nullptr); uint32 c0 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 5, &d, nullptr); uint32 c1 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 6, &d, nullptr); uint32 c2 = d ? static_cast<uint32>(atoi(d)) : 0;
			Sql_GetData(mmysql_handle, 7, &d, nullptr); uint32 c3 = d ? static_cast<uint32>(atoi(d)) : 0;
			add_row_to_holders_correct(nameid, ref, owner, src, c0, c1, c2, c3);
		}
	}
	Sql_FreeResult(mmysql_handle);

	for (auto& kv : st.holders) {
		auto& vec = kv.second;
		std::sort(vec.begin(), vec.end(), [](const s_item_transparency_holder_row& a, const s_item_transparency_holder_row& b) {
			return a.refine > b.refine;
		});
		if (vec.size() > 50)
			vec.resize(50);
	}

	st.last_refresh_tick = gettick();
	st.refresh_in_progress = false;
}

int64 item_transparency_get_count(e_item_transparency_group group, uint32 item_id) {
	auto& st = group_state(group);
	auto it = st.counts.find(item_id);
	return (it != st.counts.end()) ? it->second : 0;
}

uint32 item_transparency_get_size(e_item_transparency_group group) {
	auto& st = group_state(group);
	return static_cast<uint32>(st.item_ids.size());
}

uint32 item_transparency_get_itemid(e_item_transparency_group group, uint32 index) {
	auto& st = group_state(group);
	if (index >= st.item_ids.size())
		return 0;
	return st.item_ids[index];
}

int32 item_transparency_get_age_sec(e_item_transparency_group group) {
	auto& st = group_state(group);
	if (st.last_refresh_tick == 0)
		return INT32_MAX;
	const t_tick now = gettick();
	if (now < st.last_refresh_tick)
		return 0;
	return static_cast<int32>((now - st.last_refresh_tick) / 1000);
}

bool item_transparency_has_item(e_item_transparency_group group, uint32 item_id) {
	auto& st = group_state(group);
	return std::find(st.item_ids.begin(), st.item_ids.end(), item_id) != st.item_ids.end();
}

uint32 item_transparency_get_holder_count(e_item_transparency_group group, uint32 item_id) {
	auto& st = group_state(group);
	auto it = st.holders.find(item_id);
	return (it != st.holders.end()) ? static_cast<uint32>(it->second.size()) : 0;
}

int item_transparency_get_holder_refine(e_item_transparency_group group, uint32 item_id, uint32 index) {
	auto& st = group_state(group);
	auto it = st.holders.find(item_id);
	if (it == st.holders.end() || index >= static_cast<uint32>(it->second.size()))
		return 0;
	return it->second[index].refine;
}

const char* item_transparency_get_holder_name(e_item_transparency_group group, uint32 item_id, uint32 index) {
	auto& st = group_state(group);
	auto it = st.holders.find(item_id);
	if (it == st.holders.end() || index >= static_cast<uint32>(it->second.size()))
		return "";
	return it->second[index].owner_name.c_str();
}

const char* item_transparency_get_holder_source(e_item_transparency_group group, uint32 item_id, uint32 index) {
	auto& st = group_state(group);
	auto it = st.holders.find(item_id);
	if (it == st.holders.end() || index >= static_cast<uint32>(it->second.size()))
		return "";
	return it->second[index].source.c_str();
}

uint32 item_transparency_get_holder_nameid(e_item_transparency_group group, uint32 item_id, uint32 index) {
	auto& st = group_state(group);
	auto it = st.holders.find(item_id);
	if (it == st.holders.end() || index >= static_cast<uint32>(it->second.size()))
		return 0;
	return it->second[index].nameid;
}

uint32 item_transparency_get_holder_card(e_item_transparency_group group, uint32 item_id, uint32 index, uint8 slot) {
	auto& st = group_state(group);
	auto it = st.holders.find(item_id);
	if (it == st.holders.end() || index >= static_cast<uint32>(it->second.size()) || slot > 3)
		return 0;
	const auto& row = it->second[index];
	switch (slot) {
		case 0: return row.card0;
		case 1: return row.card1;
		case 2: return row.card2;
		case 3: return row.card3;
		default: return 0;
	}
}

void item_transparency_force_refresh(void) {
	refresh_group(g_mvp_cards, "mvp_cards");
	refresh_group(g_rare_items, "rare_items");
}

TIMER_FUNC(item_transparency_timer) {
	item_transparency_force_refresh();
	return 0;
}

void do_init_item_transparency(void) {
	item_transparency_db.load();

	if (!item_transparency_db.config.enable) {
		ShowInfo("Item Transparency is disabled (Settings.Enable: False).\n");
		return;
	}

	std::vector<uint32> default_mvp = {
		4399,4236,4128,4408,4430,4441,4407,4403,4145,4302,
		4148,4419,4386,4276,4147,4142,4132,4143,4137,4123,
		4146,4131,4121,4135,4318,4324,4330,4342,4372,4352,
		4374,4305,4425,4376,4144
	};
	std::vector<uint32> default_rare = {
		40003,40000,1230,13017,1228,2655,2357,2524,2115,2374,2375,2433,2537,2729,2423,2554,2701
	};
	std::vector<std::string> default_tables = {
		"inventory","cart_inventory","storage","guild_storage","mail_attachments"
	};

	item_transparency_refresh_interval_ms = item_transparency_db.config.refresh_interval_ms;
	if (item_transparency_db.config.tables.empty())
		item_transparency_tables = default_tables;
	else
		item_transparency_tables = item_transparency_db.config.tables;

	g_mvp_cards.item_ids = item_transparency_db.config.mvp_cards.empty() ? default_mvp : item_transparency_db.config.mvp_cards;
	g_rare_items.item_ids = item_transparency_db.config.rare_items.empty() ? default_rare : item_transparency_db.config.rare_items;

	item_transparency_force_refresh();

	item_transparency_timer_id = add_timer_interval(
		gettick() + item_transparency_refresh_interval_ms,
		item_transparency_timer,
		0,
		0,
		item_transparency_refresh_interval_ms
	);

	ShowInfo("Item Transparency cache initialized (interval: %d ms).\n", item_transparency_refresh_interval_ms);
}

void do_final_item_transparency(void) {
	if (item_transparency_timer_id != INVALID_TIMER) {
		delete_timer(item_transparency_timer_id, item_transparency_timer);
		item_transparency_timer_id = INVALID_TIMER;
	}

	g_mvp_cards = {};
	g_rare_items = {};
}

void do_reload_item_transparency(void) {
	do_final_item_transparency();
	do_init_item_transparency();
}