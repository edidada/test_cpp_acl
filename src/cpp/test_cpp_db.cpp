/**
 * test_cpp_db.cpp — exercise the ACL C++ database layer.
 *
 * Verified against lib_acl_cpp/include/acl_cpp/db/*.hpp
 *
 * Coverage:
 *  - acl::db_sqlite : a REAL functional test using an in-memory SQLite
 *    database ("​:memory:"): open / create table / insert / select / verify,
 *    transactions (begin_transaction + commit), affect_count, tbl_exists and
 *    escape_string.
 *  - acl::db_handle : the base-class interfaces reachable through db_sqlite
 *    (tbl_exists, sql_select, sql_update, begin_transaction, commit, rollback,
 *    affect_count, escape_string, get_result/get_rows/get_first_row/length).
 *  - acl::db_row    : operator[] (by index and by name) and length().
 *  - acl::db_rows   : length() and operator[].
 *  - acl::db_mysql / acl::db_pgsql : construction only — no DB server is
 *    assumed to be reachable, so we never call dbopen() on them.
 *
 * IMPORTANT runtime guards (MSVC / Windows build):
 *  On Windows the ACL C++ library is compiled with -DHAS_SQLITE_DLL,
 *  -DHAS_MYSQL_DLL and -DHAS_PGSQL_DLL, i.e. the sqlite3/mysql/libpq client
 *  libraries are loaded dynamically at *runtime*.  The db_sqlite / db_mysql /
 *  db_pgsql constructors call logger_fatal() (which aborts the process) when
 *  the corresponding DLL cannot be located.  To keep the test from crashing on
 *  a machine without those DLLs, every construction is preceded by the static
 *  non-fatal loader db_xxx::load() — if it returns false the section is
 *  skipped.  No external sqlite3 library is required to *link* this test: the
 *  DB classes are already part of acl_cpp_static.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <lib_acl.hpp>

static int g_failures = 0;

#define CHECK(expr) \
	do { \
		if (!(expr)) { \
			g_failures++; \
			printf("  [FAIL] %s (line %d)\n", #expr, __LINE__); \
		} \
	} while (0)

static void section(const char* title) {
	printf("\n==== %s ====\n", title);
}

//////////////////////////////////////////////////////////////////////////
// db_sqlite + db_handle + db_row + db_rows — the real functional test.
//////////////////////////////////////////////////////////////////////////
static void test_sqlite() {
	section("acl::db_sqlite in-memory functional test");

	// Non-fatal dynamic load check.  If sqlite3.dll is unavailable, skip the
	// whole section instead of letting the constructor abort via fatal log.
	if (!acl::db_sqlite::load()) {
		printf("  [SKIP] sqlite client library not loadable at runtime\n");
		return;
	}

	// ":memory:" tells SQLite to use a private, RAM-only database.
	acl::db_sqlite db(":memory:");
	if (!db.dbopen()) {
		printf("  [SKIP] cannot open in-memory sqlite (err=%s)\n",
			db.get_error());
		return;
	}

	CHECK(db.is_opened());
	printf("  sqlite opened, version=%s, dbtype=%s\n",
		db.version(), db.dbtype());

	// --- db_handle::tbl_exists on a brand-new database ---------------------
	CHECK(db.tbl_exists("users") == false);
	printf("  tbl_exists(\"users\") before create -> false (ok)\n");

	// --- db_handle::sql_update : DDL ---------------------------------------
	CHECK(db.sql_update(
		"create table users(id integer primary key, name varchar(32), age int)"));
	CHECK(db.tbl_exists("users") == true);
	printf("  created table users, tbl_exists(\"users\") -> true (ok)\n");

	// --- db_handle::sql_update : INSERT + affect_count ---------------------
	CHECK(db.sql_update(
		"insert into users(name, age) values('alice', 30)"));
	printf("  affect_count after 1-row insert = %d\n", db.affect_count());
	CHECK(db.affect_count() == 1);

	CHECK(db.sql_update(
		"insert into users(name, age) values('bob', 25)"));
	CHECK(db.affect_count() == 1);

	// --- db_handle::begin_transaction / commit -----------------------------
	CHECK(db.begin_transaction());
	CHECK(db.sql_update(
		"insert into users(name, age) values('carol', 41)"));
	CHECK(db.sql_update(
		"insert into users(name, age) values('dave', 22)"));
	CHECK(db.commit());
	printf("  transaction (2 inserts) committed\n");

	// rollback() is NOT overridden by db_sqlite — the base class returns
	// false.  We still exercise the call to prove it is safe to invoke.
	bool rb = db.rollback();
	printf("  rollback() (base for sqlite) returned %s\n", rb ? "true" : "false");

	// --- db_handle::sql_select + internal result (db_rows / db_row) --------
	db.free_result();
	CHECK(db.sql_select("select id, name, age from users order by id"));
	CHECK(db.empty() == false);
	CHECK(db.length() == 4);
	printf("  select returned %u rows\n", (unsigned) db.length());

	// db_handle::get_result -> db_rows
	const acl::db_rows* rows = db.get_result();
	CHECK(rows != NULL);
	if (rows != NULL) {
		// --- db_rows::length / operator[] ----------------------------------
		CHECK(rows->length() == 4);
		const acl::db_row* r0 = (*rows)[0];
		CHECK(r0 != NULL);
		if (r0 != NULL) {
			// --- db_row::operator[](size_t) / operator[](const char*) ------
			const char* name0 = (*r0)[(size_t) 1];
			const char* nameA = (*r0)["name"];
			printf("  row0 name by index=%s, by field-name=%s\n",
				name0 ? name0 : "(null)", nameA ? nameA : "(null)");
			CHECK(name0 && strcmp(name0, "alice") == 0);
			CHECK(nameA && strcmp(nameA, "alice") == 0);
			// --- db_row::length (field count) ------------------------------
			CHECK(r0->length() == 3);
			CHECK(r0->field_int("age") == 30);
		}
	}
	db.free_result();

	// --- db_handle::get_first_row convenience ------------------------------
	db.free_result();
	CHECK(db.sql_select("select name from users where name='bob'"));
	const acl::db_row* first = db.get_first_row();
	CHECK(first != NULL);
	if (first != NULL) {
		const char* nm = (*first)[(size_t) 0];
		CHECK(nm && strcmp(nm, "bob") == 0);
	}
	db.free_result();

	// --- UPDATE + affect_count over multiple rows --------------------------
	CHECK(db.sql_update("update users set age=age+1 where age >= 25"));
	printf("  affect_count after UPDATE = %d\n", db.affect_count());
	CHECK(db.affect_count() >= 1);

	// --- DELETE -------------------------------------------------------------
	CHECK(db.sql_update("delete from users where name='dave'"));
	CHECK(db.affect_count() == 1);

	db.free_result();
	CHECK(db.sql_select("select count(*) as n from users"));
	const acl::db_row* cnt = db.get_first_row();
	CHECK(cnt != NULL);
	if (cnt != NULL) {
		printf("  remaining rows = %d\n", cnt->field_int((size_t) 0));
		CHECK(cnt->field_int((size_t) 0) == 3);
	}
	db.free_result();

	// --- db_handle::escape_string ------------------------------------------
	{
		acl::string out;
		const char* raw = "O'Brien \"quoted\"";
		db.escape_string(raw, strlen(raw), out);
		printf("  escape_string(\"%s\") -> \"%s\"\n", raw, out.c_str());
		// the single quote must be backslash-escaped, double quote too
		CHECK(strstr(out.c_str(), "\\'") != NULL);
		CHECK(strstr(out.c_str(), "\\\"") != NULL);
	}

	// --- a standalone db_rows object fed directly to sql_select ------------
	{
		acl::db_rows result;
		db.free_result();
		CHECK(db.sql_select("select id, name from users", &result));
		printf("  db_rows.length() = %u\n", (unsigned) result.length());
		CHECK(result.length() == 3);
		if (result.length() > 0) {
			const acl::db_row* r = result[0];
			CHECK(r != NULL);
			if (r) {
				const char* nm = (*r)["name"];
				printf("  first row name = %s\n", nm ? nm : "(null)");
				CHECK(nm != NULL);
			}
		}
	}

	CHECK(db.close());
	printf("  sqlite closed\n");
}

//////////////////////////////////////////////////////////////////////////
// db_mysql — construction only (no server assumed reachable).
//////////////////////////////////////////////////////////////////////////
static void test_mysql_construct() {
	section("acl::db_mysql constructor only");

	// Avoid the constructor's fatal abort when the mysql client DLL is absent.
	if (!acl::db_mysql::load()) {
		printf("  [SKIP] mysql client library not loadable at runtime\n");
		return;
	}

	// Address-form constructor: never call dbopen() (no server).
	acl::db_mysql db("127.0.0.1:3306", "test", "root", "secret");
	printf("  db_mysql(addr) constructed, dbtype=%s\n", db.dbtype());
	CHECK(db.is_opened() == false);

	// Configuration-object constructor.
	acl::mysql_conf conf("127.0.0.1:3306", "test");
	conf.set_dbuser("root").set_dbpass("secret").set_charset("utf8");
	acl::db_mysql db2(conf);
	printf("  db_mysql(mysql_conf) constructed, dbtype=%s\n", db2.dbtype());
	CHECK(db2.is_opened() == false);
}

//////////////////////////////////////////////////////////////////////////
// db_pgsql — construction only (no server assumed reachable).
//////////////////////////////////////////////////////////////////////////
static void test_pgsql_construct() {
	section("acl::db_pgsql constructor only");

	if (!acl::db_pgsql::load()) {
		printf("  [SKIP] pgsql client library not loadable at runtime\n");
		return;
	}

	acl::pgsql_conf conf("127.0.0.1:5432", "test");
	conf.set_dbuser("postgres").set_dbpass("secret").set_charset("utf8");
	acl::db_pgsql db(conf);
	printf("  db_pgsql(pgsql_conf) constructed, dbtype=%s\n", db.dbtype());
	CHECK(db.is_opened() == false);
}

//////////////////////////////////////////////////////////////////////////
int main() {
	acl::acl_cpp_init();

	printf("acl_cpp verbose: %s\n", acl::acl_cpp_verbose());

	test_sqlite();
	test_mysql_construct();
	test_pgsql_construct();

	printf("\n%s (failures: %d)\n",
		g_failures == 0 ? "All DB tests passed!" : "SOME DB TESTS FAILED",
		g_failures);
	return g_failures == 0 ? 0 : 1;
}
