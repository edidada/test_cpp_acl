// test_c_json_xml.cpp
// C-core JSON and XML parser tests for the ACL library.
//
// Build: link against acl_static and include lib_acl/include.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

extern "C" {
#include <lib_acl.h>
#include <json/acl_json.h>
#include <xml/acl_xml.h>
#include <xml/acl_xml2.h>
#include <xml/acl_xml3.h>
}

// Convenience accessors: node fields are ACL_VSTRING*, may be NULL.
#define JTAG(n)   ((n) && (n)->ltag ? acl_vstring_str((n)->ltag)   : "")
#define JTEXT(n)  ((n) && (n)->text ? acl_vstring_str((n)->text)   : "")
#define XTAG(n)   ((n) && (n)->ltag ? acl_vstring_str((n)->ltag)   : "")
#define XTEXT(n)  ((n) && (n)->text ? acl_vstring_str((n)->text)   : "")

static const char *TEST_JSON =
	"{\"name\":\"acl\",\"age\":10,\"tags\":[\"cpp\",\"network\"],\"config\":{\"port\":8080}}";

static const char *TEST_XML =
	"<root attr=\"val\"><item id=\"1\">hello</item><item id=\"2\">world</item></root>";

// ---------------------------------------------------------------------------
// JSON tests (<json/acl_json.h>)
// ---------------------------------------------------------------------------
static void test_json(void)
{
	printf("\n==== JSON tests ====\n");

	// Requested name -> real ACL C API mapping (see report):
	//  acl_json_complete -> acl_json_finish
	//  acl_json_root     -> json->root
	//  acl_json_first_node-> root->iter_head
	//  acl_json_tag      -> node->ltag (acl_vstring_str)
	//  acl_json_text     -> node->text (acl_vstring_str)
	//  acl_json_node_to_string -> acl_json_node_build
	//  acl_json_get_elements   -> acl_json_getElementsByTagName
	//  acl_json_get_first_element_by_tag -> acl_json_getFirstElementByTagName
	//  acl_json_get_element_by_id -> acl_json_getElementsByTags (JSON has no id)
	//  acl_json_node_append_text -> acl_vstring_strcat on node->text

	ACL_JSON *json = acl_json_alloc();
	assert(json != NULL);

	// acl_json_update: parse the JSON string; then check completion.
	const char *left = acl_json_update(json, TEST_JSON);
	assert(left != NULL);
	assert(acl_json_finish(json) != 0); // acl_json_complete equivalent
	printf("parsed ok, finish=%d node_cnt=%d\n",
		acl_json_finish(json), json->node_cnt);

	// acl_json_root equivalent
	ACL_JSON_NODE *root = json->root;
	assert(root != NULL);

	// Scalar lookups by tag.
	ACL_JSON_NODE *name = acl_json_getFirstElementByTagName(json, "name");
	assert(name != NULL);
	printf("name tag=%s text=%s\n", JTAG(name), JTEXT(name));
	assert(strcmp(JTEXT(name), "acl") == 0);

	ACL_JSON_NODE *age = acl_json_getFirstElementByTagName(json, "age");
	assert(age != NULL);
	assert(strcmp(JTEXT(age), "10") == 0);

	// nested config.port
	ACL_JSON_NODE *port = acl_json_getFirstElementByTagName(json, "port");
	assert(port != NULL);
	assert(strcmp(JTEXT(port), "8080") == 0);

	// acl_json_get_elements equivalent: collect every "name" node.
	ACL_ARRAY *elems = acl_json_getElementsByTagName(json, "name");
	assert(elems != NULL);
	printf("getElementsByTagName(name) size=%d\n", acl_array_size(elems));
	acl_json_free_array(elems);

	// acl_json_get_element_by_id equivalent (path lookup).
	ACL_ARRAY *ports = acl_json_getElementsByTags(json, "config/port");
	assert(ports != NULL && acl_array_size(ports) >= 1);
	acl_json_free_array(ports);

	// Walk top-level members: first_node + node_next, print tag/text.
	ACL_ITER it;
	int count = 0;
	ACL_JSON_NODE *node = root->iter_head(&it, root); // first_node
	while (node != NULL) {
		printf(" member[%d] tag=%s text=%s\n", count, JTAG(node), JTEXT(node));
		count++;
		node = acl_json_node_next(node);
	}
	printf("top-level members=%d\n", count);
	assert(count >= 4); // name, age, tags, config

	// acl_json_node_to_string equivalent.
	ACL_VSTRING *s = acl_json_node_build(name, NULL);
	assert(s != NULL);
	printf("name node built: %s\n", acl_vstring_str(s));
	acl_vstring_free(s);

	// whole json to string
	ACL_VSTRING *all = acl_json_build(json, NULL);
	assert(all != NULL);
	assert(strstr(acl_vstring_str(all), "acl") != NULL);
	acl_vstring_free(all);

	// acl_json_reset: reset then re-parse same data.
	acl_json_reset(json);
	acl_json_update(json, TEST_JSON);
	assert(acl_json_finish(json) != 0);

	acl_json_free(json);

	// ---- building a fresh JSON tree ----
	ACL_JSON *j2 = acl_json_alloc();
	ACL_JSON_NODE *obj = acl_json_create_obj(j2);
	ACL_JSON_NODE *leaf = acl_json_create_text(j2, "key", "value");
	// acl_json_node_append_child
	acl_json_node_append_child(obj, leaf);
	// acl_json_node_append_text equivalent (append into the leaf text).
	acl_vstring_strcat(leaf->text, "-more");
	assert(strcmp(JTEXT(leaf), "value-more") == 0);
	ACL_VSTRING *b2 = acl_json_build(j2, NULL);
	printf("built json2: %s\n", acl_vstring_str(b2));
	acl_vstring_free(b2);

	// acl_json_create: make a new JSON object rooted at an existing node.
	ACL_JSON *j3 = acl_json_create(leaf);
	assert(j3 != NULL);
	acl_json_free(j3);

	acl_json_free(j2);
	printf("JSON tests passed.\n");
}

// ---------------------------------------------------------------------------
// XML tests (<xml/acl_xml.h>)
// ---------------------------------------------------------------------------
static int xml_children_count(ACL_XML_NODE *node)
{
	ACL_ITER it;
	int n = 0;
	ACL_FOREACH(it, node) {
		(void)it;
		n++;
	}
	return n;
}

static void test_xml(void)
{
	printf("\n==== XML tests ====\n");

	// Requested name -> real ACL C API mapping:
	//  acl_xml_complete -> acl_xml_is_closure / acl_xml_is_complete
	//  acl_xml_root     -> xml->root
	//  acl_xml_node_first_child -> node->iter_head
	//  acl_xml_node_tag -> node->ltag (acl_vstring_str)
	//  acl_xml_node_text-> node->text (acl_vstring_str)
	//  acl_xml_node_attr-> acl_xml_getElementAttr(Val)
	//  acl_xml_node_children_count -> iterate children
	//  acl_xml_get_elements_by_tagname -> acl_xml_getElementsByTagName
	//  acl_xml_get_first_element_by_tag -> acl_xml_getFirstElementByTagName
	//  acl_xml_get_element_by_id -> acl_xml_getElementById

	ACL_XML *xml = acl_xml_alloc();
	assert(xml != NULL);

	// acl_xml_update: parse.
	const char *left = acl_xml_update(xml, TEST_XML);
	assert(left != NULL);
	// acl_xml_complete equivalent.
	assert(acl_xml_is_closure(xml) == 1);
	printf("xml parsed ok, closure=%d node_cnt=%d\n",
		acl_xml_is_closure(xml), xml->node_cnt);

	// root element <root attr="val">
	ACL_XML_NODE *root = acl_xml_getFirstElementByTagName(xml, "root");
	assert(root != NULL);
	printf("root tag=%s\n", XTAG(root));
	assert(strcmp(XTAG(root), "root") == 0);

	// acl_xml_node_attr -> attribute value "val"
	const char *attr = acl_xml_getElementAttrVal(root, "attr");
	printf("root.attr=%s\n", attr ? attr : "(null)");
	assert(attr != NULL && strcmp(attr, "val") == 0);
	ACL_XML_ATTR *aobj = acl_xml_getElementAttr(root, "attr");
	assert(aobj != NULL);

	// children count of <root> == 2 (<item> x2)
	int cc = xml_children_count(root);
	printf("root children=%d\n", cc);
	assert(cc == 2);

	// first child + node_next + tag/text + attr id
	ACL_ITER it;
	ACL_XML_NODE *first = root->iter_head(&it, root); // node_first_child
	assert(first != NULL);
	assert(strcmp(XTAG(first), "item") == 0);
	printf("item1 tag=%s text=%s id=%s\n",
		XTAG(first), XTEXT(first),
		acl_xml_getElementAttrVal(first, "id"));
	assert(strcmp(XTEXT(first), "hello") == 0);

	ACL_XML_NODE *second = acl_xml_node_next(first); // node_next
	assert(second != NULL);
	assert(strcmp(XTEXT(second), "world") == 0);
	assert(strcmp(acl_xml_getElementAttrVal(second, "id"), "2") == 0);

	// acl_xml_get_elements_by_tagname
	ACL_ARRAY *items = acl_xml_getElementsByTagName(xml, "item");
	assert(items != NULL);
	printf("getElementsByTagName(item) size=%d\n", acl_array_size(items));
	assert(acl_array_size(items) == 2);
	acl_xml_free_array(items);

	// acl_xml_get_element_by_id
	ACL_XML_NODE *byid = acl_xml_getElementById(xml, "1");
	assert(byid != NULL);
	printf("getElementById(1) text=%s\n", XTEXT(byid));
	assert(strcmp(XTEXT(byid), "hello") == 0);

	// acl_xml_root equivalent
	assert(xml->root != NULL);

	// acl_xml_reset
	acl_xml_reset(xml);
	acl_xml_update(xml, TEST_XML);
	assert(acl_xml_is_closure(xml) == 1);

	acl_xml_free(xml);
	printf("XML tests passed.\n");
}

int main(void)
{
	acl_lib_init();

	test_json();
	test_xml();

	acl_lib_end();

	printf("\nALL C JSON/XML TESTS PASSED\n");
	return 0;
}
