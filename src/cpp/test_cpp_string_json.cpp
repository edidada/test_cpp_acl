/**
 * test_cpp_string_json.cpp — test acl::string / acl::json / acl::json_node /
 * acl::xml / acl::xml_node wrappers.
 *
 * Verified against lib_acl_cpp/include/acl_cpp/stdlib/{string,json,xml,xml1}.hpp
 *
 * API notes (real header names differ slightly from some docs):
 *  - acl::string has no to_int()/to_int64()/to_lower()/to_upper()/trim():
 *      use lower()/upper(), trim_left_space()/trim_right_space()/trim_space()
 *      and parse with atoi()/strtoll() on c_str().
 *  - acl::json has no complete()/getFirstElementByTag()/getElementById():
 *      use finish() / getFirstElementByTagName(); getElementById() is XML-only.
 *  - acl::json_node has no get_int()/add_object():
 *      use get_int64() (returns a pointer) and add_child(tag, node).
 *  - abstract acl::xml is instantiated through concrete acl::xml1.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <list>
#include <vector>

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

// ---------------------------------------------------------------------------
// acl::string
// ---------------------------------------------------------------------------
static void test_string_basic() {
	section("acl::string basic");

	acl::string s;
	CHECK(s.empty());
	CHECK(s.length() == 0);
	CHECK(s.c_str() != NULL);        // never NULL, may point to ""

	s = "hello";
	CHECK(!s.empty());
	CHECK(s.length() == 5);
	CHECK(strcmp(s.c_str(), "hello") == 0);

	// append()
	s.append(" ");
	s.append(acl::string("world"));
	s.append("!!", 2);
	CHECK(strcmp(s.c_str(), "hello world!!") == 0);
	CHECK(s.size() == s.length());

	// clear()
	acl::string t(s);
	t.clear();
	CHECK(t.empty());
	CHECK(t.length() == 0);

	// format() / format_append()
	acl::string f;
	f.format("%d-%s-%.2f", 42, "abc", 3.14);
	CHECK(strcmp(f.c_str(), "42-abc-3.14") == 0);
	f.format_append(" %c!", 'Z');
	CHECK(strcmp(f.c_str(), "42-abc-3.14 Z!") == 0);

	// substr(out, pos, len) — result is appended into out.
	// NOTE: substr() returns the amount of data available after position p,
	// not the actually copied length (see lib_acl_cpp/src/stdlib/string.cpp).
	acl::string sub;
	size_t n = f.substr(sub, 0, 2);
	CHECK(n == f.length());
	CHECK(strcmp(sub.c_str(), "42") == 0);

	// find(char) returns position or -1
	acl::string h("abcdef");
	CHECK(h.find('c') == 2);
	CHECK(h.find('z') < 0);

	// find(const char*) returns pointer into buffer or NULL
	char* pos = h.find("cde");
	CHECK(pos != NULL);
	CHECK(pos == h.c_str() + 2);

	// replace(char, char) — replaces all occurrences
	acl::string r("a-b-c-d");
	r.replace('-', '+');
	CHECK(strcmp(r.c_str(), "a+b+c+d") == 0);

	// trim_left_space() / trim_right_space() / trim_space()
	acl::string ts("  abc def  ");
	ts.trim_left_space();
	CHECK(strcmp(ts.c_str(), "abc def  ") == 0);
	ts.trim_right_space();
	CHECK(strcmp(ts.c_str(), "abc def") == 0);
	ts.trim_space();       // removes ALL spaces incl. inner ones
	CHECK(strcmp(ts.c_str(), "abcdef") == 0);

	// lower()/upper() — docs call these to_lower()/to_upper()
	acl::string lo("HeLLo WorLd");
	lo.lower();
	CHECK(strcmp(lo.c_str(), "hello world") == 0);
	lo.upper();
	CHECK(strcmp(lo.c_str(), "HELLO WORLD") == 0);

	// integer parsing — acl::string has no to_int()/to_int64(); parse via c_str()
	// NOTE: atoi() saturates to INT_MAX on overflow (MSVC), so only an
	// int-sized literal may be checked against the exact value.
	acl::string num("1234567890123");
	acl::string smallnum("1234567890");
	int i = atoi(smallnum.c_str());                     // like string::to_int()
	long long ll = strtoll(num.c_str(), NULL, 10);   // like string::to_int64()
	CHECK(i == 1234567890);
	CHECK(ll == 1234567890123LL);
	// binary mode: += int appends raw bytes, operator>> reads them back
	acl::string bin(0, true);      // (size, bin=true) -> binary append mode
	bin += (int) 77;
	CHECK(bin.length() == sizeof(int));
	int back = 0;
	size_t used = bin >> back;
	CHECK(used == sizeof(int) && back == 77);

	// push_back()/pop_back()/back()/truncate()
	acl::string p("ab");
	p.push_back('c');
	CHECK(p.back() == 'c');
	CHECK(p.pop_back());
	CHECK(strcmp(p.c_str(), "ab") == 0);
	p.truncate(1);
	CHECK(strcmp(p.c_str(), "a") == 0);
}

static void test_string_split() {
	section("acl::string split");

	acl::string s("one:two:three");
	std::list<acl::string>& parts = s.split(":");
	CHECK(parts.size() == 3);
	if (parts.size() == 3) {
		CHECK(strcmp(parts.front().c_str(), "one") == 0);
		CHECK(strcmp(parts.back().c_str(), "three") == 0);
	}

	acl::string s2("a,b;c");
	std::vector<acl::string>& v = s2.split2(",;");   // each char is a separator
	CHECK(v.size() == 3);

	acl::string s3("name = zsx");
	std::pair<acl::string, acl::string>& kv = s3.split_nameval('=');
	CHECK(strcmp(kv.first.c_str(), "name") == 0);
	CHECK(strcmp(kv.second.c_str(), "zsx") == 0);
}

static void test_string_encode() {
	section("acl::string encode/decode");

	// url_encode()/url_decode() — source given as arg, result replaces buffer
	const char* raw = "hello world&x=1";
	acl::string enc;
	enc.url_encode(raw);
	CHECK(!enc.empty());
	CHECK(strstr(enc.c_str(), "hello world&x=1") == NULL); // encoded form
	acl::string dec;
	dec.url_decode(enc.c_str());
	CHECK(strcmp(dec.c_str(), raw) == 0);

	// base64_encode()/base64_decode() — in-place round trip
	acl::string b64("ACL base64 round-trip test data");
	acl::string original = b64;
	b64.base64_encode();
	CHECK(strcmp(b64.c_str(), original.c_str()) != 0);
	b64.base64_decode();
	CHECK(b64 == original);

	// hex (H2B) encode/decode round trip
	acl::string hx;
	hx.hex_encode("xyz", 3);
	acl::string back;
	back.hex_decode(hx.c_str(), hx.length());
	CHECK(strcmp(back.c_str(), "xyz") == 0);
}

// ---------------------------------------------------------------------------
// acl::json / acl::json_node
// ---------------------------------------------------------------------------
static void test_json_parse() {
	section("acl::json parse");

	const char* data =
		"{\"name\":\"zsx\",\"age\":40,\"score\":99.5,\"ok\":true,"
		 "\"tags\":[\"a\",\"b\"],\"addr\":{\"city\":\"BJ\"}}";

	// streaming update(): feed the document in two chunks
	acl::json j;
	size_t len = strlen(data);
	acl::string part1(data, len / 2), part2(data + len / 2);
	j.update(part1.c_str());
	CHECK(!j.finish());
	j.update(part2.c_str());
	CHECK(j.finish());

	acl::json j2(data);              // one-shot parse in constructor
	CHECK(j2.finish());

	// get_root() — virtual root holding all members
	acl::json_node& root = j2.get_root();
	CHECK(root.is_object());
	CHECK(root.children_count() == 6);

	// getElementsByTagName() / getFirstElementByTagName() / operator[]
	const std::vector<acl::json_node*>& names = j2.getElementsByTagName("name");
	CHECK(names.size() == 1);
	acl::json_node* nm = j2.getFirstElementByTagName("name");
	CHECK(nm != NULL);
	acl::json_node* byop = j2["age"];
	CHECK(byop != NULL);

	if (nm != NULL) {
		CHECK(strcmp(nm->tag_name(), "name") == 0);
		CHECK(strcmp(nm->get_text(), "zsx") == 0);         // text value
		CHECK(nm->is_string());
		CHECK(strcmp(nm->get_string(), "zsx") == 0);
	}
	if (byop != NULL) {
		CHECK(byop->is_number());
		const long long* iv = byop->get_int64();           // docs' get_int()
		CHECK(iv != NULL && *iv == 40);
	}

	acl::json_node* sc = j2.getFirstElementByTagName("score");
	CHECK(sc != NULL);
	if (sc != NULL && sc->get_double() != NULL) {
		CHECK(*sc->get_double() > 99.4 && *sc->get_double() < 99.6);
	}

	acl::json_node* ok = j2.getFirstElementByTagName("ok");
	CHECK(ok != NULL && ok->is_bool());
	if (ok != NULL && ok->get_bool() != NULL) {
		CHECK(*ok->get_bool() == true);
	}

	acl::json_node* city = j2.getFirstElementByTagName("city");
	CHECK(city != NULL);
	if (city != NULL) {
		CHECK(strcmp(city->get_text(), "BJ") == 0);
		CHECK(city->depth() >= 1);
		CHECK(city->get_type() != NULL);
	}

	// get_obj(): child node object of an object node.
	// NOTE: for a parsed document, the node tagged "addr" is the object's
	// KEY node (its own get_type() is not "object"); the value node returned
	// by get_obj() is the actual JSON object.
	acl::json_node* addr = j2.getFirstElementByTagName("addr");
	CHECK(addr != NULL);
	if (addr != NULL) {
		acl::json_node* child = addr->get_obj();
		CHECK(child != NULL);          // value obj of "addr"
		CHECK(child == NULL || child->is_object());
	}

	// traverse root children with first_child()/next_child(); next_child()
	// iterates the parent's child list, so it must be called on the parent
	int cnt = 0;
	for (acl::json_node* it = root.first_child(); it; it = root.next_child()) {
		it->tag_name();
		cnt++;
	}
	CHECK(cnt == 6);
	root.clear();   // release temporary traversal node objects

	// reset() then reparse
	j2.reset();
	j2.update("{\"k\":\"v\"}");
	CHECK(j2.finish());
	CHECK(j2.getFirstElementByTagName("k") != NULL);

	// to_string() of parsed tree
	const acl::string& s = j.to_string();
	CHECK(s.length() > 0);
	printf("  parsed tree -> %.80s\n", s.c_str());
}

static void test_json_build() {
	section("acl::json build");

	acl::json j;
	acl::json_node& root = j.get_root();

	// leaf nodes
	root.add_text("name", "zsx");
	root.add_number("age", 40);
	root.add_double("score", 99.5);
	root.add_bool("ok", true);
	root.add_null("nothing");

	// create_node() + add_child(tag, node) — docs' "add_object" equivalent
	acl::json_node& city = j.create_node("city", "Beijing");
	acl::json_node& addr = j.create_node("address", &city);
	root.add_child(&addr);

	// inline tagged child object (add_child(tag, return_child))
	acl::json_node& extra = root.add_child("extra", true);
	extra.add_text("x", "1");

	// array node with untagged element children
	acl::json ja;
	acl::json_node& arr = ja.create_array();
	arr.add_array_text("a");
	arr.add_array_number(1);
	arr.add_array_double(2.5);
	arr.add_array_bool(false);
	arr.add_array_null();
	ja.get_root().add_child("list", &arr);

	acl::string out;
	j.build_json(out, true);
	CHECK(!out.empty());
	printf("  built json: %s\n", out.c_str());

	const acl::string& s2 = ja.to_string();
	printf("  array json: %s\n", s2.c_str());

	// round-trip: parse what we built
	acl::json j2(out.c_str());
	CHECK(j2.finish());
	CHECK(j2.getFirstElementByTagName("name") != NULL);
	CHECK(j2.getFirstElementByTagName("address") != NULL);

	acl::json j3(s2.c_str());
	CHECK(j3.getFirstElementByTagName("list") != NULL);

	// duplicate_node(): copy a node from one json into another json
	acl::json j4;
	acl::json_node* orig = j2.getFirstElementByTagName("city");
	if (orig != NULL) {
		acl::json_node& dup = j4.duplicate_node(orig);
		dup.set_tag("town");
		j4.get_root().add_child(&dup);
		CHECK(j4.to_string().find("town") != NULL);
	}
}

// ---------------------------------------------------------------------------
// acl::xml / acl::xml_node (abstract wrappers; concrete impl: acl::xml1)
// ---------------------------------------------------------------------------
static void test_xml_parse() {
	section("acl::xml parse");

	const char* data =
		"<root>"
		"<person id=\"p1\" name=\"zsx\"><age>18</age><city>BJ</city></person>"
		"<person id=\"p2\"><age>20</age></person>"
		"</root>";

	// streaming: feed a partial document — not complete yet
	acl::xml1 x;
	x.update("<root><a>1</a>");
	CHECK(!x.complete("root"));
	x.update("</root>");
	CHECK(x.complete("root"));

	acl::xml1 x2(data);
	CHECK(x2.complete("root"));

	// getElementsByTagName / getFirstElementByTag / getElementById
	const std::vector<acl::xml_node*>& ages = x2.getElementsByTagName("age");
	CHECK(ages.size() == 2);
	if (ages.size() == 2 && ages[0]->text() != NULL) {
		CHECK(strcmp(ages[0]->text(), "18") == 0);
	}

	acl::xml_node* p1 = x2.getFirstElementByTag("person");
	CHECK(p1 != NULL);
	if (p1 != NULL) {
		CHECK(strcmp(p1->tag_name(), "person") == 0);
		CHECK(p1->id() != NULL && strcmp(p1->id(), "p1") == 0);
		CHECK(p1->attr_value("name") != NULL
			&& strcmp(p1->attr_value("name"), "zsx") == 0);
		CHECK(p1->children_count() == 2);
		CHECK(p1->depth() >= 1);
		CHECK(!p1->is_root());

		// first_child()/next_child() traversal — next_child() iterates the
		// parent's child list, so it must be called on the parent (p1), not
		// on the returned child; calling it on the child corrupts iteration.
		int cnt = 0;
		for (acl::xml_node* c = p1->first_child(); c; c = p1->next_child()) {
			if (c->tag_name() != NULL && strcmp(c->tag_name(), "age") == 0) {
				CHECK(strcmp(c->text(), "18") == 0);
			}
			cnt++;
		}
		CHECK(cnt == 2);
		p1->clear();     // release temp traversal objects

		// attribute traversal via xml_attr
		int attr_cnt = 0;
		const acl::xml_attr* a = p1->first_attr();
		while (a != NULL) {
			a->get_name();
			a->get_value();
			attr_cnt++;
			a = p1->next_attr();
		}
		CHECK(attr_cnt == 2);
	}

	acl::xml_node* byid = x2.getElementById("p2");
	CHECK(byid != NULL);
	if (byid != NULL) {
		const char* v = byid->attr_value("name");
		CHECK(v == NULL || *v == 0);       // absent attr
	}

	// virtual root holds the document root as its child
	acl::xml_node& vroot = x2.get_root();
	CHECK(vroot.first_child() != NULL);
	vroot.clear();     // release the temporary traversal node

	printf("  xml to_string: %.70s\n", x2.to_string());

	// reset and reparse
	x2.reset();
	x2.update("<other><a>1</a></other>");
	CHECK(x2.complete("other"));
	CHECK(x2.getFirstElementByTag("a") != NULL);
}

static void test_xml_build() {
	section("acl::xml build");

	acl::xml1 x;
	acl::xml_node& root = x.create_node("root");
	x.get_root().add_child(&root);

	acl::xml_node& p = x.create_node("person");
	p.add_attr("id", "u1");
	p.add_attr("age", 18);                 // int-valued attr overload
	p.add_child("note", "note-text");
	root.add_child(&p);
	p.add_child("city", "Shanghai");       // (tag, txt) child node

	acl::string out;
	x.build_xml(out);
	CHECK(!out.empty());
	CHECK(strstr(out.c_str(), "<person") != NULL);
	printf("  built xml: %s\n", out.c_str());

	// re-parse the generated document
	acl::xml1 x2(out.c_str());
	CHECK(x2.complete("root"));
	acl::xml_node* node = x2.getElementById("u1");
	CHECK(node != NULL);
	if (node != NULL) {
		CHECK(strcmp(node->tag_name(), "person") == 0);
	}
}

int main() {
	setvbuf(stdout, NULL, _IONBF, 0);   // keep markers visible if a crash occurs
	acl::acl_cpp_init();   // the API lives in namespace acl (no global alias)

	printf("acl_cpp verbose: %s\n", acl::acl_cpp_verbose());

	test_string_basic();
	test_string_split();
	test_string_encode();
	test_json_parse();
	test_json_build();
	test_xml_parse();
	test_xml_build();

	// NOTE: the real API provides no acl_cpp_end() — acl_cpp_init() only does
	// WSAStartup-like init on Windows and requires no explicit deinit.
	acl::log::close();

	printf("\n%s (failures: %d)\n",
		g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", g_failures);
	return g_failures == 0 ? 0 : 1;
}
