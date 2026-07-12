#include <Tools/DataStructures/Hamt/merkle_search_tree.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <threads.h>
#endif

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (0)

#define CHECK_STATUS(expression) CHECK((expression) == TDS_MERKLE_OK)

typedef bool (*test_fn)(void);

typedef struct test_case {
    const char *name;
    test_fn run;
} test_case;

static int tds_test_i32_key_tag;
static int tds_test_i32_value_tag;
static int tds_test_utf8_value_tag;
static int tds_test_alternate_i32_key_tag;

typedef struct mst_concurrent_context {
    const tds_merkle_search_tree *tree;
    tds_merkle_digest root;
} mst_concurrent_context;

static tds_merkle_status compare_i32(
    const void *left,
    const void *right,
    int *comparison,
    void *context) {
    const int32_t first = *(const int32_t *)left;
    const int32_t second = *(const int32_t *)right;
    (void)context;
    *comparison = first < second ? -1 : first > second ? 1 : 0;
    return TDS_MERKLE_OK;
}

static void configure_i32_policy(
    tds_merkle_policy_config *config,
    const char *policy_id) {
    tds_merkle_policy_config_init(config);
    config->policy_id.bytes = (const unsigned char *)policy_id;
    config->policy_id.size = strlen(policy_id);
    tds_merkle_i32_type_policy_init(&config->key_type, &tds_test_i32_key_tag);
    tds_merkle_i32_type_policy_init(&config->value_type, &tds_test_i32_value_tag);
    config->key_compare = compare_i32;
    tds_merkle_i32_codec_init(&config->key_codec);
    tds_merkle_i32_codec_init(&config->value_codec);
}

static tds_merkle_status make_i32_policy(
    const char *policy_id,
    tds_merkle_policy *policy) {
    tds_merkle_policy_config config;
    configure_i32_policy(&config, policy_id);
    return tds_merkle_policy_create(&config, policy);
}

static tds_merkle_status make_string_policy(tds_merkle_policy *policy) {
    static const char policy_id[] = "golden-int-string-v1";
    tds_merkle_policy_config config;
    tds_merkle_policy_config_init(&config);
    config.policy_id.bytes = (const unsigned char *)policy_id;
    config.policy_id.size = sizeof(policy_id) - 1;
    tds_merkle_i32_type_policy_init(&config.key_type, &tds_test_i32_key_tag);
    tds_merkle_nullable_utf8_type_policy_init(
        &config.value_type,
        &tds_test_utf8_value_tag);
    config.key_compare = compare_i32;
    tds_merkle_i32_codec_init(&config.key_codec);
    tds_merkle_nullable_utf8_codec_init(&config.value_codec);
    return tds_merkle_policy_create(&config, policy);
}

static bool parse_hex_bytes(
    const char *hex,
    unsigned char *destination,
    size_t destination_size) {
    size_t index;
    if (strlen(hex) != destination_size * 2) {
        return false;
    }
    for (index = 0; index != destination_size; ++index) {
        const char pair[3] = {hex[index * 2], hex[index * 2 + 1], '\0'};
        char *end = NULL;
        const unsigned long value = strtoul(pair, &end, 16);
        if (end != pair + 2 || value > 255) {
            return false;
        }
        destination[index] = (unsigned char)value;
    }
    return true;
}

static bool digest_equals_hex(tds_merkle_digest digest, const char *expected) {
    char actual[TDS_MERKLE_DIGEST_HEX_LENGTH + 1];
    actual[TDS_MERKLE_DIGEST_HEX_LENGTH] = '\0';
    return tds_merkle_digest_write_hex(digest, actual, TDS_MERKLE_DIGEST_HEX_LENGTH) ==
            TDS_MERKLE_OK &&
        strcmp(actual, expected) == 0;
}

static bool mst_concurrent_worker(const mst_concurrent_context *context) {
    size_t pass;
    for (pass = 0; pass != 64; ++pass) {
        const int32_t key = (int32_t)((pass * 17) % 512);
        tds_merkle_search_tree snapshot = {0};
        tds_merkle_search_entry_ref entry = {0};
        tds_merkle_search_tree_statistics statistics = {0};
        bool found = false;
        bool valid = false;
        if (tds_merkle_search_tree_copy(context->tree, &snapshot) != TDS_MERKLE_OK) {
            return false;
        }
        if (tds_merkle_search_tree_size(&snapshot) != 512 ||
            !tds_merkle_digest_equal(
                tds_merkle_search_tree_root_hash(&snapshot),
                context->root) ||
            tds_merkle_search_tree_try_get_entry_ref(
                &snapshot,
                &key,
                &found,
                &entry) != TDS_MERKLE_OK ||
            !found || *(const int32_t *)entry.value != key * 3 - 7 ||
            tds_merkle_search_tree_validate(
                &snapshot,
                &valid,
                &statistics) != TDS_MERKLE_OK ||
            !valid || statistics.count != 512) {
            tds_merkle_search_tree_dispose(&snapshot);
            return false;
        }
        tds_merkle_search_tree_dispose(&snapshot);
    }
    return true;
}

#ifdef _WIN32
static DWORD WINAPI mst_concurrent_thread(void *parameter) {
    return mst_concurrent_worker((const mst_concurrent_context *)parameter) ? 0u : 1u;
}
#endif

static bool test_digest_and_builtin_codecs(void) {
    unsigned char bytes[TDS_MERKLE_DIGEST_BYTE_LENGTH];
    unsigned char written[TDS_MERKLE_DIGEST_BYTE_LENGTH];
    char hex[TDS_MERKLE_DIGEST_HEX_LENGTH];
    char upper[TDS_MERKLE_DIGEST_HEX_LENGTH];
    tds_merkle_digest digest = {{0}};
    tds_merkle_digest roundtrip = {{0}};
    tds_merkle_codec codec;
    size_t index;
    size_t encoded_size = 0;
    int32_t decoded_i32 = 0;
    int32_t i32 = (int32_t)0x01020304;
    int64_t decoded_i64 = 0;
    int64_t i64 = INT64_C(0x0102030405060708);
    unsigned char encoded[32];
    tds_merkle_nullable_utf8 text = {true, "A\xc3\xa9\xf0\x9f\x98\x80", 7};
    tds_merkle_nullable_utf8 decoded_text = {false, NULL, 0};
    tds_merkle_type_policy text_type;
    tds_merkle_nullable_bytes nullable_bytes = {
        true,
        (const unsigned char *)"\0\x01\xff",
        3};
    tds_merkle_nullable_bytes decoded_bytes = {false, NULL, 0};
    tds_merkle_type_policy bytes_type;
    tds_merkle_guid guid;
    tds_merkle_guid decoded_guid;
    tds_merkle_policy_config default_config;
    for (index = 0; index != sizeof(bytes); ++index) {
        bytes[index] = (unsigned char)index;
    }
    CHECK_STATUS(tds_merkle_digest_parse(bytes, sizeof(bytes), &digest));
    CHECK(tds_merkle_digest_parse(bytes, sizeof(bytes) - 1, &roundtrip) ==
        TDS_MERKLE_INVALID_ENCODING);
    CHECK_STATUS(tds_merkle_digest_write(digest, written, sizeof(written)));
    CHECK(memcmp(bytes, written, sizeof(bytes)) == 0);
    CHECK_STATUS(tds_merkle_digest_write_hex(digest, hex, sizeof(hex)));
    for (index = 0; index != sizeof(hex); ++index) {
        upper[index] = hex[index] >= 'a' && hex[index] <= 'f'
            ? (char)(hex[index] - 'a' + 'A')
            : hex[index];
    }
    CHECK_STATUS(tds_merkle_digest_parse_hex(upper, sizeof(upper), &roundtrip));
    CHECK(tds_merkle_digest_equal(digest, roundtrip));
    CHECK(tds_merkle_digest_parse_hex("00", 2, &roundtrip) ==
        TDS_MERKLE_INVALID_ENCODING);
    upper[17] = 'x';
    CHECK(tds_merkle_digest_parse_hex(upper, sizeof(upper), &roundtrip) ==
        TDS_MERKLE_INVALID_ENCODING);

    tds_merkle_policy_config_init(&default_config);
    tds_merkle_i32_codec_init(&codec);
    CHECK(codec.encoding_id.size == sizeof("i32-be-v1") - 1);
    CHECK_STATUS(codec.encode(
        &i32,
        NULL,
        0,
        &encoded_size,
        &default_config.allocator,
        codec.context));
    CHECK(encoded_size == 4);
    CHECK_STATUS(codec.encode(
        &i32,
        encoded,
        4,
        &encoded_size,
        &default_config.allocator,
        codec.context));
    CHECK(memcmp(encoded, "\x01\x02\x03\x04", 4) == 0);
    CHECK_STATUS(codec.decode(
        encoded,
        4,
        &decoded_i32,
        &default_config.allocator,
        codec.context));
    CHECK(decoded_i32 == i32);
    CHECK(codec.decode(
        encoded,
        3,
        &decoded_i32,
        &default_config.allocator,
        codec.context) == TDS_MERKLE_INVALID_ENCODING);

    tds_merkle_i64_codec_init(&codec);
    CHECK_STATUS(codec.encode(
        &i64,
        encoded,
        8,
        &encoded_size,
        &default_config.allocator,
        codec.context));
    CHECK(encoded_size == 8 &&
        memcmp(encoded, "\x01\x02\x03\x04\x05\x06\x07\x08", 8) == 0);
    CHECK_STATUS(codec.decode(
        encoded,
        8,
        &decoded_i64,
        &default_config.allocator,
        codec.context));
    CHECK(decoded_i64 == i64);
    CHECK(codec.decode(
        encoded,
        7,
        &decoded_i64,
        &default_config.allocator,
        codec.context) == TDS_MERKLE_INVALID_ENCODING);

    tds_merkle_nullable_utf8_codec_init(&codec);
    CHECK_STATUS(codec.encode(
        &text,
        encoded,
        8,
        &encoded_size,
        &default_config.allocator,
        codec.context));
    CHECK(encoded_size == 8 && encoded[0] == 1 &&
        memcmp(encoded + 1, text.data, text.size) == 0);
    CHECK_STATUS(codec.decode(
        encoded,
        encoded_size,
        &decoded_text,
        &default_config.allocator,
        codec.context));
    CHECK(decoded_text.has_value && decoded_text.size == text.size &&
        memcmp(decoded_text.data, text.data, text.size) == 0);
    tds_merkle_nullable_utf8_type_policy_init(&text_type, &tds_test_utf8_value_tag);
    text_type.destroy(&decoded_text, &default_config.allocator, text_type.context);
    CHECK(codec.decode(
        (const unsigned char *)"\x01\xc0\x80",
        3,
        &decoded_text,
        &default_config.allocator,
        codec.context) == TDS_MERKLE_INVALID_ENCODING);
    CHECK(codec.decode(
        (const unsigned char *)"\0\0",
        2,
        &decoded_text,
        &default_config.allocator,
        codec.context) == TDS_MERKLE_INVALID_ENCODING);
    tds_merkle_nullable_bytes_codec_init(&codec);
    CHECK_STATUS(codec.encode(
        &nullable_bytes,
        encoded,
        4,
        &encoded_size,
        &default_config.allocator,
        codec.context));
    CHECK(encoded_size == 4 &&
        memcmp(encoded, "\x01\0\x01\xff", 4) == 0);
    CHECK_STATUS(codec.decode(
        encoded,
        encoded_size,
        &decoded_bytes,
        &default_config.allocator,
        codec.context));
    CHECK(decoded_bytes.has_value && decoded_bytes.size == 3 &&
        memcmp(decoded_bytes.data, nullable_bytes.data, 3) == 0);
    tds_merkle_nullable_bytes_type_policy_init(&bytes_type, &tds_test_utf8_value_tag);
    bytes_type.destroy(&decoded_bytes, &default_config.allocator, bytes_type.context);
    CHECK(codec.decode(
        (const unsigned char *)"\x02",
        1,
        &decoded_bytes,
        &default_config.allocator,
        codec.context) == TDS_MERKLE_INVALID_ENCODING);

    for (index = 0; index != sizeof(guid.bytes); ++index) {
        guid.bytes[index] = (unsigned char)(index * 17);
    }
    tds_merkle_guid_codec_init(&codec);
    CHECK_STATUS(codec.encode(
        &guid,
        encoded,
        sizeof(guid.bytes),
        &encoded_size,
        &default_config.allocator,
        codec.context));
    CHECK(encoded_size == sizeof(guid.bytes) &&
        memcmp(encoded, guid.bytes, sizeof(guid.bytes)) == 0);
    CHECK_STATUS(codec.decode(
        encoded,
        sizeof(guid.bytes),
        &decoded_guid,
        &default_config.allocator,
        codec.context));
    CHECK(memcmp(decoded_guid.bytes, guid.bytes, sizeof(guid.bytes)) == 0);
    CHECK(codec.decode(
        encoded,
        sizeof(guid.bytes) - 1,
        &decoded_guid,
        &default_config.allocator,
        codec.context) == TDS_MERKLE_INVALID_ENCODING);
    return true;
}

typedef struct block_capture {
    size_t count;
    tds_merkle_digest digest;
    const unsigned char *bytes;
    size_t size;
} block_capture;

static tds_merkle_status capture_block(tds_merkle_block_ref block, void *context) {
    block_capture *capture = (block_capture *)context;
    ++capture->count;
    capture->digest = block.digest;
    capture->bytes = block.bytes;
    capture->size = block.byte_count;
    return TDS_MERKLE_OK;
}

static bool test_golden_single_entry_wire(void) {
    static const char expected_hex[] =
        "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"
        "000000000100000001000000040000002a0000000a01666f7274792d74776f"
        "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
        "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3";
    tds_merkle_policy policy = {NULL};
    tds_merkle_search_tree empty = {NULL, NULL};
    tds_merkle_search_tree tree = {NULL, NULL};
    tds_merkle_nullable_utf8 value = {true, "forty-two", 9};
    int32_t key = 42;
    block_capture capture = {0};
    unsigned char expected[(sizeof(expected_hex) - 1) / 2];
    bool valid = false;
    tds_merkle_search_tree_statistics statistics;
    CHECK_STATUS(make_string_policy(&policy));
    CHECK(strcmp(tds_merkle_algorithm_id(), "mst-sha256-b16-v2") == 0);
    CHECK(digest_equals_hex(
        tds_merkle_policy_domain_digest(&policy),
        "fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"));
    CHECK(digest_equals_hex(
        tds_merkle_policy_empty_digest(&policy),
        "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"));
    CHECK_STATUS(tds_merkle_search_tree_init(&empty, &policy));
    CHECK(tds_merkle_digest_equal(
        tds_merkle_search_tree_root_hash(&empty),
        tds_merkle_policy_empty_digest(&policy)));
    CHECK_STATUS(tds_merkle_search_tree_set(&empty, &key, &value, &tree));
    CHECK(digest_equals_hex(
        tds_merkle_search_tree_root_hash(&tree),
        "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"));
    CHECK_STATUS(tds_merkle_search_tree_visit_blocks(&tree, capture_block, &capture));
    CHECK(capture.count == 1 && capture.size == sizeof(expected));
    CHECK(parse_hex_bytes(expected_hex, expected, sizeof(expected)));
    CHECK(memcmp(capture.bytes, expected, sizeof(expected)) == 0);
    CHECK(tds_merkle_digest_equal(capture.digest, tds_merkle_search_tree_root_hash(&tree)));
    CHECK_STATUS(tds_merkle_search_tree_validate(&tree, &valid, &statistics));
    CHECK(valid && statistics.count == 1 && statistics.block_count == 1 &&
        statistics.height == 1 && statistics.minimum_entries_per_block == 1 &&
        statistics.maximum_entries_per_block == 1 &&
        statistics.minimum_block_bytes == sizeof(expected));
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_search_tree_dispose(&empty);
    tds_merkle_policy_dispose(&policy);
    return true;
}

static bool test_policy_validation_and_typed_compatibility(void) {
    static const char *const invalid_encoding_ids[] = {
        "",
        " ",
        "codec",
        "-v1",
        "codec-v",
        "codec-vx",
        " codec-v1",
        "codec-v1 "};
    tds_merkle_policy_config config;
    tds_merkle_policy first = {NULL};
    tds_merkle_policy same_domain_alternate_type = {NULL};
    tds_merkle_policy different = {NULL};
    tds_merkle_search_tree first_tree = {NULL, NULL};
    tds_merkle_search_tree alternate_tree = {NULL, NULL};
    size_t index;
    bool equal = false;
    configure_i32_policy(&config, " ");
    CHECK(tds_merkle_policy_create(&config, &different) ==
        TDS_MERKLE_INVALID_ARGUMENT);
    CHECK(different.rep == NULL);
    for (index = 0; index != sizeof(invalid_encoding_ids) / sizeof(invalid_encoding_ids[0]);
         ++index) {
        configure_i32_policy(&config, "policy-validation-v1");
        config.key_codec.encoding_id.bytes =
            (const unsigned char *)invalid_encoding_ids[index];
        config.key_codec.encoding_id.size = strlen(invalid_encoding_ids[index]);
        CHECK(tds_merkle_policy_create(&config, &different) ==
            TDS_MERKLE_INVALID_ARGUMENT);
        CHECK(different.rep == NULL);
    }
    CHECK_STATUS(make_i32_policy("policy-validation-v1", &first));
    configure_i32_policy(&config, "policy-validation-v1");
    config.key_type.type_identity = &tds_test_alternate_i32_key_tag;
    CHECK_STATUS(tds_merkle_policy_create(&config, &same_domain_alternate_type));
    CHECK_STATUS(make_i32_policy("policy-validation-v2", &different));
    CHECK(tds_merkle_policy_same_domain(&first, &same_domain_alternate_type));
    CHECK(!tds_merkle_policy_same_domain(&first, &different));
    CHECK_STATUS(tds_merkle_search_tree_init(&first_tree, &first));
    CHECK_STATUS(tds_merkle_search_tree_init(&alternate_tree, &same_domain_alternate_type));
    CHECK(tds_merkle_search_tree_content_equals(&first_tree, &alternate_tree));
    CHECK(tds_merkle_search_tree_map_equals(&first_tree, &alternate_tree, &equal) ==
        TDS_MERKLE_INCOMPATIBLE_POLICY);
    tds_merkle_search_tree_dispose(&alternate_tree);
    tds_merkle_search_tree_dispose(&first_tree);
    tds_merkle_policy_dispose(&different);
    tds_merkle_policy_dispose(&same_domain_alternate_type);
    tds_merkle_policy_dispose(&first);
    return true;
}

typedef struct ordered_visit {
    int32_t previous;
    size_t count;
    bool first;
    bool valid;
    int32_t minimum;
    int32_t maximum;
} ordered_visit;

static tds_merkle_status check_ordered_entry(
    tds_merkle_search_entry_ref entry,
    void *context) {
    ordered_visit *visit = (ordered_visit *)context;
    const int32_t key = *(const int32_t *)entry.key;
    const int32_t value = *(const int32_t *)entry.value;
    if ((!visit->first && key <= visit->previous) || value != key * 3 ||
        key < visit->minimum || key > visit->maximum) {
        visit->valid = false;
    }
    visit->first = false;
    visit->previous = key;
    ++visit->count;
    return TDS_MERKLE_OK;
}

typedef struct shape_capture {
    size_t count;
    bool has_wide_block;
    unsigned maximum_level;
} shape_capture;

static tds_merkle_status capture_shape(tds_merkle_shape_ref shape, void *context) {
    shape_capture *capture = (shape_capture *)context;
    ++capture->count;
    if (shape.entries_in_block > 1) {
        capture->has_wide_block = true;
    }
    if (shape.level > capture->maximum_level) {
        capture->maximum_level = shape.level;
    }
    if (shape.node_identity == NULL || shape.entry.level != shape.level ||
        shape.subtree_count < shape.entries_in_block) {
        return TDS_MERKLE_CALLBACK_FAILURE;
    }
    return TDS_MERKLE_OK;
}

static bool test_history_independence_and_structure(void) {
    enum { ENTRY_COUNT = 1024 };
    int32_t keys[ENTRY_COUNT];
    int32_t values[ENTRY_COUNT];
    tds_merkle_search_input forward_inputs[ENTRY_COUNT];
    tds_merkle_search_input reverse_inputs[ENTRY_COUNT];
    tds_merkle_policy first_policy = {NULL};
    tds_merkle_policy same_policy = {NULL};
    tds_merkle_search_tree forward = {NULL, NULL};
    tds_merkle_search_tree reverse = {NULL, NULL};
    tds_merkle_search_tree incremental = {NULL, NULL};
    tds_merkle_search_tree next = {NULL, NULL};
    size_t index;
    bool equal = false;
    bool valid = false;
    tds_merkle_search_tree_statistics statistics;
    ordered_visit visit = {0, 0, true, true, INT32_MIN, INT32_MAX};
    shape_capture shape = {0};
    CHECK_STATUS(make_i32_policy("canonical-i32-v1", &first_policy));
    CHECK_STATUS(make_i32_policy("canonical-i32-v1", &same_policy));
    CHECK(tds_merkle_policy_same_domain(&first_policy, &same_policy));
    CHECK(!tds_merkle_policy_same_identity(&first_policy, &same_policy));
    for (index = 0; index != ENTRY_COUNT; ++index) {
        keys[index] = (int32_t)index;
        values[index] = keys[index] * 3;
        forward_inputs[index] = (tds_merkle_search_input){&keys[index], &values[index]};
        reverse_inputs[ENTRY_COUNT - 1 - index] = forward_inputs[index];
    }
    CHECK_STATUS(tds_merkle_search_tree_from_array(
        &forward,
        &first_policy,
        forward_inputs,
        ENTRY_COUNT));
    CHECK_STATUS(tds_merkle_search_tree_from_array(
        &reverse,
        &same_policy,
        reverse_inputs,
        ENTRY_COUNT));
    CHECK(tds_merkle_search_tree_content_equals(&forward, &reverse));
    CHECK_STATUS(tds_merkle_search_tree_map_equals(&forward, &reverse, &equal));
    CHECK(equal);
    CHECK_STATUS(tds_merkle_search_tree_init(&incremental, &first_policy));
    for (index = 0; index != ENTRY_COUNT; ++index) {
        CHECK_STATUS(tds_merkle_search_tree_set(
            &incremental,
            &keys[index],
            &values[index],
            &incremental));
    }
    CHECK(tds_merkle_search_tree_content_equals(&forward, &incremental));
    CHECK_STATUS(tds_merkle_search_tree_visit(&forward, check_ordered_entry, &visit));
    CHECK(visit.valid && visit.count == ENTRY_COUNT);
    CHECK_STATUS(tds_merkle_search_tree_visit_shape(&forward, capture_shape, &shape));
    CHECK(shape.count == ENTRY_COUNT && shape.has_wide_block && shape.maximum_level > 0);
    CHECK_STATUS(tds_merkle_search_tree_validate(&forward, &valid, &statistics));
    CHECK(valid && statistics.count == ENTRY_COUNT && statistics.block_count > 1 &&
        statistics.height > 1);
    CHECK_STATUS(tds_merkle_search_tree_remove(&incremental, &keys[511], &next));
    CHECK(tds_merkle_search_tree_size(&next) == ENTRY_COUNT - 1);
    tds_merkle_search_tree_dispose(&incremental);
    incremental = next;
    memset(&next, 0, sizeof(next));
    CHECK_STATUS(tds_merkle_search_tree_set(
        &incremental,
        &keys[511],
        &values[511],
        &next));
    CHECK(tds_merkle_search_tree_content_equals(&forward, &next));
    tds_merkle_search_tree_dispose(&next);
    tds_merkle_search_tree_dispose(&incremental);
    tds_merkle_search_tree_dispose(&reverse);
    tds_merkle_search_tree_dispose(&forward);
    tds_merkle_policy_dispose(&same_policy);
    tds_merkle_policy_dispose(&first_policy);
    return true;
}

typedef struct difference_capture {
    size_t count;
    bool removed_7;
    bool changed_8;
    bool added_1000;
} difference_capture;

static tds_merkle_status capture_difference(
    tds_merkle_difference_ref difference,
    void *context) {
    difference_capture *capture = (difference_capture *)context;
    const int32_t key = *(const int32_t *)difference.key;
    ++capture->count;
    if (difference.kind == TDS_MERKLE_DIFFERENCE_REMOVED && key == 7 &&
        *(const int32_t *)difference.before == 21 && difference.after == NULL) {
        capture->removed_7 = true;
    } else if (difference.kind == TDS_MERKLE_DIFFERENCE_CHANGED && key == 8 &&
        *(const int32_t *)difference.before == 24 &&
        *(const int32_t *)difference.after == 999) {
        capture->changed_8 = true;
    } else if (difference.kind == TDS_MERKLE_DIFFERENCE_ADDED && key == 1000 &&
        difference.before == NULL && *(const int32_t *)difference.after == 3000) {
        capture->added_1000 = true;
    }
    return TDS_MERKLE_OK;
}

static bool test_persistence_range_diff_and_sharing(void) {
    enum { ENTRY_COUNT = 100 };
    int32_t keys[ENTRY_COUNT];
    int32_t values[ENTRY_COUNT];
    tds_merkle_search_input inputs[ENTRY_COUNT];
    tds_merkle_policy policy = {NULL};
    tds_merkle_policy other_policy = {NULL};
    tds_merkle_search_tree source = {NULL, NULL};
    tds_merkle_search_tree removed = {NULL, NULL};
    tds_merkle_search_tree changed = {NULL, NULL};
    tds_merkle_search_tree target = {NULL, NULL};
    tds_merkle_search_tree absent = {NULL, NULL};
    tds_merkle_search_tree incompatible = {NULL, NULL};
    int32_t key_7 = 7;
    int32_t key_8 = 8;
    int32_t key_1000 = 1000;
    int32_t value_999 = 999;
    int32_t value_3000 = 3000;
    int32_t key_absent = -1;
    int32_t range_minimum = 23;
    int32_t range_maximum = 31;
    ordered_visit range = {0, 0, true, true, 23, 31};
    difference_capture differences = {0};
    const void *identity = NULL;
    size_t shared = 0;
    size_t index;
    bool equal = true;
    CHECK_STATUS(make_i32_policy("persistence-i32-v1", &policy));
    CHECK_STATUS(make_i32_policy("different-i32-v1", &other_policy));
    for (index = 0; index != ENTRY_COUNT; ++index) {
        keys[index] = (int32_t)index;
        values[index] = keys[index] * 3;
        inputs[index] = (tds_merkle_search_input){&keys[index], &values[index]};
    }
    CHECK_STATUS(tds_merkle_search_tree_from_array(&source, &policy, inputs, ENTRY_COUNT));
    CHECK_STATUS(tds_merkle_search_tree_visit_range(
        &source,
        &range_minimum,
        &range_maximum,
        check_ordered_entry,
        &range));
    CHECK(range.valid && range.count == 9);
    CHECK(tds_merkle_search_tree_visit_range(
        &source,
        &range_maximum,
        &range_minimum,
        check_ordered_entry,
        &range) == TDS_MERKLE_INVALID_ARGUMENT);
    CHECK_STATUS(tds_merkle_search_tree_remove(&source, &key_7, &removed));
    CHECK_STATUS(tds_merkle_search_tree_set(&removed, &key_8, &value_999, &changed));
    CHECK_STATUS(tds_merkle_search_tree_set(&changed, &key_1000, &value_3000, &target));
    CHECK_STATUS(tds_merkle_search_tree_diff(
        &source,
        &target,
        capture_difference,
        &differences));
    CHECK(differences.count == 3 && differences.removed_7 &&
        differences.changed_8 && differences.added_1000);
    CHECK_STATUS(tds_merkle_search_tree_map_equals(&source, &target, &equal));
    CHECK(!equal);
    CHECK_STATUS(tds_merkle_search_tree_remove(&source, &key_absent, &absent));
    CHECK(tds_merkle_search_tree_root_identity(&source) ==
        tds_merkle_search_tree_root_identity(&absent));
    CHECK_STATUS(tds_merkle_search_tree_node_identity(&source, &key_8, &identity));
    CHECK(identity != NULL);
    CHECK_STATUS(tds_merkle_search_tree_shared_node_count(&source, &target, &shared));
    CHECK(shared > 0 && shared < tds_merkle_search_tree_block_count(&source));
    CHECK_STATUS(tds_merkle_search_tree_init(&incompatible, &other_policy));
    CHECK(tds_merkle_search_tree_diff(
        &source,
        &incompatible,
        capture_difference,
        &differences) == TDS_MERKLE_INCOMPATIBLE_POLICY);
    CHECK(tds_merkle_search_tree_shared_node_count(
        &source,
        &incompatible,
        &shared) == TDS_MERKLE_INCOMPATIBLE_POLICY);
    tds_merkle_search_tree_dispose(&incompatible);
    tds_merkle_search_tree_dispose(&absent);
    tds_merkle_search_tree_dispose(&target);
    tds_merkle_search_tree_dispose(&changed);
    tds_merkle_search_tree_dispose(&removed);
    tds_merkle_search_tree_dispose(&source);
    tds_merkle_policy_dispose(&other_policy);
    tds_merkle_policy_dispose(&policy);
    return true;
}

typedef struct counting_allocator {
    size_t attempts;
    size_t live;
    size_t fail_at;
} counting_allocator;

static void *counting_allocate(size_t size, void *context) {
    counting_allocator *allocator = (counting_allocator *)context;
    ++allocator->attempts;
    if (allocator->fail_at != 0 && allocator->attempts == allocator->fail_at) {
        return NULL;
    }
    {
        void *result = malloc(size);
        if (result != NULL) {
            ++allocator->live;
        }
        return result;
    }
}

static void counting_deallocate(void *allocation, void *context) {
    counting_allocator *allocator = (counting_allocator *)context;
    if (allocation != NULL) {
        if (allocator->live == 0) {
            abort();
        }
        --allocator->live;
        free(allocation);
    }
}

static bool test_allocation_failure_atomicity(void) {
    counting_allocator allocator = {0};
    tds_merkle_policy_config config;
    tds_merkle_policy policy = {NULL};
    tds_merkle_search_tree base = {NULL, NULL};
    int32_t key = 1;
    int32_t value = 3;
    int32_t new_key = 2;
    int32_t new_value = 6;
    int32_t array_keys[24];
    int32_t array_values[24];
    tds_merkle_search_input inputs[24];
    size_t failure_index;
    size_t baseline_live;
    tds_merkle_digest baseline_digest;
    bool saw_success = false;
    configure_i32_policy(&config, "allocation-i32-v1");
    config.allocator.allocate = counting_allocate;
    config.allocator.deallocate = counting_deallocate;
    config.allocator.context = &allocator;
    for (failure_index = 1; failure_index != 16; ++failure_index) {
        tds_merkle_policy candidate = {NULL};
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        if (tds_merkle_policy_create(&config, &candidate) == TDS_MERKLE_OK) {
            tds_merkle_policy_dispose(&candidate);
            CHECK(allocator.live == 0);
            saw_success = true;
            break;
        }
        CHECK(candidate.rep == NULL && allocator.live == 0);
    }
    CHECK(saw_success);
    allocator.attempts = 0;
    allocator.fail_at = 0;
    CHECK_STATUS(tds_merkle_policy_create(&config, &policy));
    CHECK_STATUS(tds_merkle_search_tree_init(&base, &policy));
    CHECK_STATUS(tds_merkle_search_tree_set(&base, &key, &value, &base));
    baseline_live = allocator.live;
    baseline_digest = tds_merkle_search_tree_root_hash(&base);
    saw_success = false;
    for (failure_index = 1; failure_index != 128; ++failure_index) {
        tds_merkle_search_tree result = {NULL, NULL};
        tds_merkle_status status;
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_set(
            &base,
            &new_key,
            &new_value,
            &result);
        allocator.fail_at = 0;
        CHECK(tds_merkle_digest_equal(
            baseline_digest,
            tds_merkle_search_tree_root_hash(&base)));
        if (status == TDS_MERKLE_OK) {
            CHECK(tds_merkle_search_tree_size(&result) == 2);
            tds_merkle_search_tree_dispose(&result);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY);
        CHECK(result.policy == NULL && result.root == NULL);
        CHECK(allocator.live == baseline_live);
    }
    CHECK(saw_success);
    saw_success = false;
    for (failure_index = 1; failure_index != 512; ++failure_index) {
        tds_merkle_search_tree result = {NULL, NULL};
        tds_merkle_status status;
        size_t index;
        for (index = 0; index != 24; ++index) {
            array_keys[index] = (int32_t)index;
            array_values[index] = array_keys[index] * 3;
            inputs[index] = (tds_merkle_search_input){
                &array_keys[index],
                &array_values[index]};
        }
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_from_array(&result, &policy, inputs, 24);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            CHECK(tds_merkle_search_tree_size(&result) == 24);
            tds_merkle_search_tree_dispose(&result);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY);
        CHECK(result.policy == NULL && result.root == NULL);
        CHECK(allocator.live == baseline_live);
    }
    CHECK(saw_success);
    saw_success = false;
    for (failure_index = 1; failure_index != 256; ++failure_index) {
        tds_merkle_search_tree alias = {NULL, NULL};
        tds_merkle_status status;
        CHECK_STATUS(tds_merkle_search_tree_copy(&base, &alias));
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_set(&alias, &new_key, &new_value, &alias);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            CHECK(tds_merkle_search_tree_size(&alias) == 2);
            tds_merkle_search_tree_dispose(&alias);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY);
        CHECK(tds_merkle_digest_equal(
            baseline_digest,
            tds_merkle_search_tree_root_hash(&alias)));
        tds_merkle_search_tree_dispose(&alias);
        CHECK(allocator.live == baseline_live);
    }
    CHECK(saw_success);
    {
        bool valid = true;
        tds_merkle_search_tree_statistics statistics = {9, 9, 9, 9, 9, 9, 9};
        size_t shared = 77;
        tds_merkle_search_tree updated = {NULL, NULL};
        allocator.attempts = 0;
        allocator.fail_at = 1;
        CHECK(tds_merkle_search_tree_validate(&base, &valid, &statistics) ==
            TDS_MERKLE_NO_MEMORY);
        CHECK(valid && statistics.count == 9 && statistics.block_count == 9);
        allocator.fail_at = 0;
        CHECK_STATUS(tds_merkle_search_tree_set(
            &base,
            &new_key,
            &new_value,
            &updated));
        allocator.attempts = 0;
        allocator.fail_at = 1;
        CHECK(tds_merkle_search_tree_shared_node_count(&base, &updated, &shared) ==
            TDS_MERKLE_NO_MEMORY);
        CHECK(shared == 77);
        allocator.fail_at = 0;
        tds_merkle_search_tree_dispose(&updated);
        CHECK(allocator.live == baseline_live);
    }
    tds_merkle_search_tree_dispose(&base);
    tds_merkle_policy_dispose(&policy);
    CHECK(allocator.live == 0);
    return true;
}

typedef struct failing_compare_state {
    bool fail;
} failing_compare_state;

static tds_merkle_status failing_compare(
    const void *left,
    const void *right,
    int *comparison,
    void *context) {
    failing_compare_state *state = (failing_compare_state *)context;
    if (state->fail) {
        return TDS_MERKLE_CALLBACK_FAILURE;
    }
    return compare_i32(left, right, comparison, NULL);
}

typedef struct failing_encode_state {
    tds_merkle_codec inner;
    bool fail;
    bool inconsistent;
    size_t calls;
} failing_encode_state;

static tds_merkle_status failing_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const tds_merkle_allocator *allocator,
    void *context) {
    failing_encode_state *state = (failing_encode_state *)context;
    tds_merkle_status status;
    ++state->calls;
    if (state->fail) {
        return TDS_MERKLE_CALLBACK_FAILURE;
    }
    status = state->inner.encode(
        value,
        destination,
        destination_size,
        bytes_written,
        allocator,
        state->inner.context);
    if (status == TDS_MERKLE_OK && state->inconsistent && destination != NULL) {
        ++*bytes_written;
    }
    return status;
}

static bool test_callback_failure_atomicity(void) {
    static const unsigned char encoding_id[] = "failing-i32-v1";
    tds_merkle_policy_config config;
    tds_merkle_policy policy = {NULL};
    tds_merkle_search_tree base = {NULL, NULL};
    tds_merkle_search_tree result = {NULL, NULL};
    failing_compare_state compare_state = {false};
    failing_encode_state encode_state = {0};
    int32_t key = 1;
    int32_t value = 3;
    int32_t new_key = 2;
    int32_t new_value = 6;
    tds_merkle_digest baseline;
    configure_i32_policy(&config, "callback-i32-v1");
    config.key_compare = failing_compare;
    config.key_compare_context = &compare_state;
    tds_merkle_i32_codec_init(&encode_state.inner);
    tds_merkle_codec_init(
        &config.value_codec,
        encoding_id,
        sizeof(encoding_id) - 1,
        failing_encode,
        encode_state.inner.decode);
    config.value_codec.context = &encode_state;
    CHECK_STATUS(tds_merkle_policy_create(&config, &policy));
    CHECK_STATUS(tds_merkle_search_tree_init(&base, &policy));
    CHECK_STATUS(tds_merkle_search_tree_set(&base, &key, &value, &base));
    baseline = tds_merkle_search_tree_root_hash(&base);
    compare_state.fail = true;
    CHECK(tds_merkle_search_tree_set(&base, &new_key, &new_value, &result) ==
        TDS_MERKLE_CALLBACK_FAILURE);
    CHECK(result.policy == NULL && tds_merkle_digest_equal(
        baseline,
        tds_merkle_search_tree_root_hash(&base)));
    compare_state.fail = false;
    encode_state.fail = true;
    CHECK(tds_merkle_search_tree_set(&base, &new_key, &new_value, &result) ==
        TDS_MERKLE_CALLBACK_FAILURE);
    CHECK(result.policy == NULL && tds_merkle_digest_equal(
        baseline,
        tds_merkle_search_tree_root_hash(&base)));
    encode_state.fail = false;
    encode_state.inconsistent = true;
    CHECK(tds_merkle_search_tree_set(&base, &new_key, &new_value, &result) ==
        TDS_MERKLE_INCONSISTENT_POLICY);
    CHECK(result.policy == NULL && tds_merkle_digest_equal(
        baseline,
        tds_merkle_search_tree_root_hash(&base)));
    tds_merkle_search_tree_dispose(&base);
    tds_merkle_policy_dispose(&policy);
    return true;
}

typedef struct representative_key {
    int32_t logical;
    int32_t representative;
} representative_key;

static int tds_test_representative_key_tag;

static tds_merkle_status compare_representative_key(
    const void *left,
    const void *right,
    int *comparison,
    void *context) {
    const representative_key *first = (const representative_key *)left;
    const representative_key *second = (const representative_key *)right;
    (void)context;
    *comparison = first->logical < second->logical
        ? -1
        : first->logical > second->logical ? 1 : 0;
    return TDS_MERKLE_OK;
}

static tds_merkle_status encode_representative_key(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const tds_merkle_allocator *allocator,
    void *context) {
    const representative_key *key = (const representative_key *)value;
    tds_merkle_codec codec;
    (void)context;
    tds_merkle_i32_codec_init(&codec);
    return codec.encode(
        &key->logical,
        destination,
        destination_size,
        bytes_written,
        allocator,
        codec.context);
}

static tds_merkle_status decode_representative_key(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const tds_merkle_allocator *allocator,
    void *context) {
    representative_key decoded = {0, 0};
    tds_merkle_codec codec;
    tds_merkle_status status;
    (void)context;
    tds_merkle_i32_codec_init(&codec);
    status = codec.decode(
        encoding,
        encoding_size,
        &decoded.logical,
        allocator,
        codec.context);
    if (status == TDS_MERKLE_OK) {
        *(representative_key *)destination = decoded;
    }
    return status;
}

static bool test_equivalent_key_representatives(void) {
    static const unsigned char policy_id[] = "representatives-v1";
    static const unsigned char encoding_id[] = "representative-key-v1";
    representative_key first = {5, 1};
    representative_key middle = {2, 2};
    representative_key duplicate = {5, 3};
    representative_key query = {5, 99};
    representative_key replacement = {5, 4};
    int32_t first_value = 10;
    int32_t middle_value = 20;
    int32_t duplicate_value = 30;
    int32_t replacement_value = 40;
    tds_merkle_search_input inputs[] = {
        {&first, &first_value},
        {&middle, &middle_value},
        {&duplicate, &duplicate_value}};
    tds_merkle_policy_config config;
    tds_merkle_policy policy = {NULL};
    tds_merkle_search_tree tree = {NULL, NULL};
    tds_merkle_search_tree replaced = {NULL, NULL};
    tds_merkle_search_entry_ref entry;
    bool found = false;
    tds_merkle_policy_config_init(&config);
    config.policy_id = (tds_merkle_identifier){policy_id, sizeof(policy_id) - 1};
    tds_merkle_type_policy_init(
        &config.key_type,
        sizeof(representative_key),
        &tds_test_representative_key_tag);
    tds_merkle_i32_type_policy_init(&config.value_type, &tds_test_i32_value_tag);
    config.key_compare = compare_representative_key;
    tds_merkle_codec_init(
        &config.key_codec,
        encoding_id,
        sizeof(encoding_id) - 1,
        encode_representative_key,
        decode_representative_key);
    tds_merkle_i32_codec_init(&config.value_codec);
    CHECK_STATUS(tds_merkle_policy_create(&config, &policy));
    CHECK_STATUS(tds_merkle_search_tree_from_array(
        &tree,
        &policy,
        inputs,
        sizeof(inputs) / sizeof(inputs[0])));
    CHECK(tds_merkle_search_tree_size(&tree) == 2);
    CHECK_STATUS(tds_merkle_search_tree_try_get_entry_ref(
        &tree,
        &query,
        &found,
        &entry));
    CHECK(found && ((const representative_key *)entry.key)->representative == 1 &&
        *(const int32_t *)entry.value == 30);
    CHECK_STATUS(tds_merkle_search_tree_set(
        &tree,
        &replacement,
        &replacement_value,
        &replaced));
    CHECK_STATUS(tds_merkle_search_tree_try_get_entry_ref(
        &replaced,
        &query,
        &found,
        &entry));
    CHECK(found && ((const representative_key *)entry.key)->representative == 1 &&
        *(const int32_t *)entry.value == 40);
    tds_merkle_search_tree_dispose(&replaced);
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_policy_dispose(&policy);
    return true;
}

typedef struct failing_visitor_state {
    size_t calls;
    size_t fail_at;
} failing_visitor_state;

static tds_merkle_status fail_entry_visit(
    tds_merkle_search_entry_ref entry,
    void *context) {
    failing_visitor_state *state = (failing_visitor_state *)context;
    (void)entry;
    ++state->calls;
    return state->calls == state->fail_at
        ? TDS_MERKLE_CALLBACK_FAILURE
        : TDS_MERKLE_OK;
}

static tds_merkle_status fail_difference_visit(
    tds_merkle_difference_ref difference,
    void *context) {
    failing_visitor_state *state = (failing_visitor_state *)context;
    (void)difference;
    ++state->calls;
    return state->calls == state->fail_at
        ? TDS_MERKLE_CALLBACK_FAILURE
        : TDS_MERKLE_OK;
}

static tds_merkle_status fail_shape_visit(
    tds_merkle_shape_ref shape,
    void *context) {
    failing_visitor_state *state = (failing_visitor_state *)context;
    (void)shape;
    ++state->calls;
    return state->calls == state->fail_at
        ? TDS_MERKLE_CALLBACK_FAILURE
        : TDS_MERKLE_OK;
}

static tds_merkle_status fail_block_visit(
    tds_merkle_block_ref block,
    void *context) {
    failing_visitor_state *state = (failing_visitor_state *)context;
    (void)block;
    ++state->calls;
    return state->calls == state->fail_at
        ? TDS_MERKLE_CALLBACK_FAILURE
        : TDS_MERKLE_OK;
}

static bool test_streaming_visitor_failures(void) {
    enum { ENTRY_COUNT = 64 };
    int32_t keys[ENTRY_COUNT];
    int32_t values[ENTRY_COUNT];
    tds_merkle_search_input inputs[ENTRY_COUNT];
    tds_merkle_policy policy = {NULL};
    tds_merkle_search_tree tree = {NULL, NULL};
    tds_merkle_search_tree empty = {NULL, NULL};
    failing_visitor_state state;
    int32_t minimum = 0;
    int32_t maximum = ENTRY_COUNT - 1;
    size_t index;
    CHECK_STATUS(make_i32_policy("visitor-i32-v1", &policy));
    for (index = 0; index != ENTRY_COUNT; ++index) {
        keys[index] = (int32_t)index;
        values[index] = keys[index] * 3;
        inputs[index] = (tds_merkle_search_input){&keys[index], &values[index]};
    }
    CHECK_STATUS(tds_merkle_search_tree_from_array(&tree, &policy, inputs, ENTRY_COUNT));
    CHECK_STATUS(tds_merkle_search_tree_init(&empty, &policy));
    state = (failing_visitor_state){0, 3};
    CHECK(tds_merkle_search_tree_visit(&tree, fail_entry_visit, &state) ==
        TDS_MERKLE_CALLBACK_FAILURE);
    CHECK(state.calls == 3);
    state = (failing_visitor_state){0, 4};
    CHECK(tds_merkle_search_tree_visit_range(
        &tree,
        &minimum,
        &maximum,
        fail_entry_visit,
        &state) == TDS_MERKLE_CALLBACK_FAILURE);
    CHECK(state.calls == 4);
    state = (failing_visitor_state){0, 5};
    CHECK(tds_merkle_search_tree_diff(
        &empty,
        &tree,
        fail_difference_visit,
        &state) == TDS_MERKLE_CALLBACK_FAILURE);
    CHECK(state.calls == 5);
    state = (failing_visitor_state){0, 2};
    CHECK(tds_merkle_search_tree_visit_shape(&tree, fail_shape_visit, &state) ==
        TDS_MERKLE_CALLBACK_FAILURE);
    CHECK(state.calls == 2);
    state = (failing_visitor_state){0, 1};
    CHECK(tds_merkle_search_tree_visit_blocks(&tree, fail_block_visit, &state) ==
        TDS_MERKLE_CALLBACK_FAILURE);
    CHECK(state.calls == 1);
    tds_merkle_search_tree_dispose(&empty);
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_policy_dispose(&policy);
    return true;
}

static bool test_randomized_model_and_snapshots(void) {
    enum { KEY_COUNT = 128, STEPS = 2500, SNAPSHOT_COUNT = 8 };
    tds_merkle_policy policy = {NULL};
    tds_merkle_search_tree tree = {NULL, NULL};
    tds_merkle_search_tree snapshots[SNAPSHOT_COUNT] = {{NULL, NULL}};
    bool present[KEY_COUNT] = {false};
    int32_t values[KEY_COUNT] = {0};
    uint64_t state = UINT64_C(0x4d595df4d0f33173);
    size_t snapshot_count = 0;
    size_t step;
    CHECK_STATUS(make_i32_policy("random-i32-v1", &policy));
    CHECK_STATUS(tds_merkle_search_tree_init(&tree, &policy));
    for (step = 0; step != STEPS; ++step) {
        int32_t key;
        int32_t value;
        bool found = false;
        tds_merkle_search_entry_ref entry;
        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        key = (int32_t)((state >> 19) % KEY_COUNT);
        value = (int32_t)(state >> 32);
        if ((state & 7) == 0) {
            CHECK_STATUS(tds_merkle_search_tree_remove(&tree, &key, &tree));
            present[key] = false;
        } else {
            CHECK_STATUS(tds_merkle_search_tree_set(&tree, &key, &value, &tree));
            present[key] = true;
            values[key] = value;
        }
        CHECK_STATUS(tds_merkle_search_tree_try_get_entry_ref(&tree, &key, &found, &entry));
        CHECK(found == present[key]);
        if (found) {
            CHECK(*(const int32_t *)entry.value == values[key]);
        }
        if (snapshot_count != SNAPSHOT_COUNT && step % 311 == 0) {
            CHECK_STATUS(tds_merkle_search_tree_copy(&tree, &snapshots[snapshot_count]));
            ++snapshot_count;
        }
        if (step % 127 == 0) {
            bool valid = false;
            tds_merkle_search_tree_statistics statistics;
            CHECK_STATUS(tds_merkle_search_tree_validate(&tree, &valid, &statistics));
            CHECK(valid && statistics.count == tds_merkle_search_tree_size(&tree));
        }
    }
    while (snapshot_count != 0) {
        bool valid = false;
        tds_merkle_search_tree_statistics statistics;
        --snapshot_count;
        CHECK_STATUS(tds_merkle_search_tree_validate(
            &snapshots[snapshot_count],
            &valid,
            &statistics));
        CHECK(valid);
        tds_merkle_search_tree_dispose(&snapshots[snapshot_count]);
    }
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_policy_dispose(&policy);
    return true;
}

static bool test_concurrent_retained_snapshot_reads(void) {
    tds_merkle_policy policy = {0};
    tds_merkle_search_tree tree = {0};
    mst_concurrent_context context;
    int32_t key;
    CHECK_STATUS(make_i32_policy("c-concurrent-v1", &policy));
    CHECK_STATUS(tds_merkle_search_tree_init(&tree, &policy));
    for (key = 0; key != 512; ++key) {
        const int32_t value = key * 3 - 7;
        CHECK_STATUS(tds_merkle_search_tree_set(&tree, &key, &value, &tree));
    }
    context.tree = &tree;
    context.root = tds_merkle_search_tree_root_hash(&tree);
#ifdef _WIN32
    {
        enum { thread_count = 8 };
        HANDLE threads[thread_count];
        DWORD index;
        for (index = 0; index != thread_count; ++index) {
            threads[index] = CreateThread(NULL, 0, mst_concurrent_thread, &context, 0, NULL);
            CHECK(threads[index] != NULL);
        }
        CHECK(WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
        for (index = 0; index != thread_count; ++index) {
            DWORD exit_code = 1;
            CHECK(GetExitCodeThread(threads[index], &exit_code));
            CHECK(exit_code == 0);
            CHECK(CloseHandle(threads[index]));
        }
    }
#else
    for (key = 0; key != 8; ++key) {
        CHECK(mst_concurrent_worker(&context));
    }
#endif
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_policy_dispose(&policy);
    return true;
}

static tds_merkle_status make_i32_sequence_tree(
    const tds_merkle_policy *policy,
    size_t count,
    tds_merkle_search_tree *tree) {
    enum { maximum_count = 128 };
    int32_t keys[maximum_count];
    int32_t values[maximum_count];
    tds_merkle_search_input inputs[maximum_count];
    size_t index;
    if (count > maximum_count) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    for (index = 0; index != count; ++index) {
        keys[index] = (int32_t)index;
        values[index] = (int32_t)(index * 17 + 3);
        inputs[index] = (tds_merkle_search_input){&keys[index], &values[index]};
    }
    return tds_merkle_search_tree_from_array(tree, policy, inputs, count);
}

typedef struct digest_order_capture {
    tds_merkle_digest previous;
    size_t count;
    bool sorted;
} digest_order_capture;

static tds_merkle_status capture_digest_order(
    tds_merkle_digest digest,
    void *context) {
    digest_order_capture *capture = (digest_order_capture *)context;
    if (capture->count != 0 &&
        tds_merkle_digest_compare(capture->previous, digest) >= 0) {
        capture->sorted = false;
    }
    capture->previous = digest;
    ++capture->count;
    return TDS_MERKLE_OK;
}

static bool test_verified_persistence_store_and_iterative_sync(void) {
    tds_merkle_policy policy = {0};
    tds_merkle_search_tree tree = {0};
    tds_merkle_search_tree loaded = {0};
    tds_merkle_search_tree synchronized = {0};
    tds_merkle_search_tree local = {0};
    tds_merkle_memory_block_store memory = {0};
    tds_merkle_memory_block_store receiver = {0};
    tds_merkle_memory_block_store tampered_memory = {0};
    tds_merkle_block_store store;
    tds_merkle_block_store receiver_store;
    tds_merkle_block_store tampered_store;
    tds_merkle_block_pack pack = {0};
    tds_merkle_block_pack sync_pack = {0};
    tds_merkle_sync_plan plan = {0};
    tds_merkle_verification_error error;
    size_t added = SIZE_MAX;
    size_t count = 0;
    size_t round;
    digest_order_capture order = {{{0}}, 0, true};
    CHECK_STATUS(make_i32_policy("c-persistence-i32-v1", &policy));
    CHECK_STATUS(make_i32_sequence_tree(&policy, 96, &tree));
    CHECK(tds_merkle_search_tree_block_count(&tree) > 1);
    CHECK(tds_merkle_search_tree_height(&tree) > 1);
    CHECK_STATUS(tds_merkle_memory_block_store_init(&memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(&memory, &store));
    CHECK_STATUS(tds_merkle_search_tree_save(&tree, &store, &added, &error));
    CHECK(added == tds_merkle_search_tree_block_count(&tree));
    CHECK_STATUS(tds_merkle_block_store_count(&store, &count));
    CHECK(count == added);
    added = SIZE_MAX;
    CHECK_STATUS(tds_merkle_search_tree_save(&tree, &store, &added, &error));
    CHECK(added == 0);
    CHECK_STATUS(tds_merkle_memory_block_store_visit_digests(
        &memory,
        capture_digest_order,
        &order));
    CHECK(order.sorted && order.count == count);
    CHECK_STATUS(tds_merkle_search_tree_load(
        tds_merkle_search_tree_root_hash(&tree),
        &policy,
        &store,
        NULL,
        &loaded,
        &error));
    CHECK(tds_merkle_search_tree_size(&loaded) == 96);
    CHECK(tds_merkle_digest_equal(
        tds_merkle_search_tree_root_hash(&loaded),
        tds_merkle_search_tree_root_hash(&tree)));
    CHECK(error.kind == TDS_MERKLE_VERIFY_NONE &&
        error.verified_block_count == count);

    CHECK_STATUS(tds_merkle_search_tree_export_pack(&tree, &pack));
    CHECK(tds_merkle_block_pack_block_count(&pack) == count);
    CHECK(tds_merkle_block_pack_contains_root_block(&pack));
    CHECK(tds_merkle_block_pack_total_byte_count(&pack) > 0);
    CHECK(tds_merkle_digest_equal(
        tds_merkle_block_digest(tds_merkle_block_pack_block_at(&pack, 0)),
        tds_merkle_search_tree_root_hash(&tree)));

    /* A store lookup returns an owned snapshot that remains alive after clear. */
    {
        tds_merkle_block snapshot = {0};
        bool found = false;
        const tds_merkle_block *first = tds_merkle_block_pack_block_at(&pack, 0);
        CHECK_STATUS(tds_merkle_block_store_try_get(
            &store,
            tds_merkle_block_digest(first),
            &found,
            &snapshot));
        CHECK(found && tds_merkle_block_equal(first, &snapshot));
        CHECK_STATUS(tds_merkle_block_store_clear(&store));
        CHECK(tds_merkle_block_byte_count(&snapshot) > 0 &&
            memcmp(tds_merkle_block_bytes(&snapshot), "MST2", 4) == 0);
        tds_merkle_block_dispose(&snapshot);
        CHECK_STATUS(tds_merkle_search_tree_save(&tree, &store, &added, &error));
    }

    CHECK_STATUS(tds_merkle_memory_block_store_init(&receiver, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(&receiver, &receiver_store));
    CHECK_STATUS(tds_merkle_search_tree_init(&local, &policy));
    for (round = 0; round != 66; ++round) {
        const tds_merkle_digest *requests;
        size_t request_count;
        size_t block_index;
        CHECK_STATUS(tds_merkle_search_tree_plan_sync(
            &tree,
            &local,
            &receiver_store,
            &plan));
        if (!tds_merkle_sync_plan_requires_blocks(&plan)) {
            break;
        }
        request_count = tds_merkle_sync_plan_requested_block_count(&plan);
        requests = tds_merkle_sync_plan_requested_blocks(&plan);
        CHECK(request_count != 0 && requests != NULL);
        CHECK_STATUS(tds_merkle_search_tree_export_blocks(
            &tree,
            requests,
            request_count,
            &sync_pack,
            &error));
        CHECK(tds_merkle_block_pack_block_count(&sync_pack) == request_count);
        for (block_index = 0; block_index != request_count; ++block_index) {
            tds_merkle_store_put_result put_result;
            CHECK_STATUS(tds_merkle_block_store_put(
                &receiver_store,
                tds_merkle_block_pack_block_at(&sync_pack, block_index),
                &put_result));
            CHECK(put_result == TDS_MERKLE_STORE_ADDED ||
                put_result == TDS_MERKLE_STORE_PRESENT_IDENTICAL);
        }
        tds_merkle_block_pack_dispose(&sync_pack);
        tds_merkle_sync_plan_dispose(&plan);
    }
    CHECK(round < 66);
    CHECK(!tds_merkle_sync_plan_roots_match(&plan));
    CHECK(!tds_merkle_sync_plan_requires_blocks(&plan));
    CHECK_STATUS(tds_merkle_search_tree_load(
        tds_merkle_search_tree_root_hash(&tree),
        &policy,
        &receiver_store,
        NULL,
        &synchronized,
        &error));
    CHECK(tds_merkle_digest_equal(
        tds_merkle_search_tree_root_hash(&synchronized),
        tds_merkle_search_tree_root_hash(&tree)));
    tds_merkle_sync_plan_dispose(&plan);
    CHECK_STATUS(tds_merkle_search_tree_create_sync_pack(
        &tree,
        &receiver_store,
        &sync_pack));
    CHECK(tds_merkle_block_pack_block_count(&sync_pack) == 0);
    tds_merkle_block_pack_dispose(&sync_pack);

    /* A validly owned block with a forged digest is rejected before decoding. */
    CHECK_STATUS(tds_merkle_memory_block_store_init(&tampered_memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(
        &tampered_memory,
        &tampered_store));
    {
        const tds_merkle_block *root_block =
            tds_merkle_block_pack_block_at(&pack, 0);
        const size_t byte_count = tds_merkle_block_byte_count(root_block);
        unsigned char *bytes = (unsigned char *)malloc(byte_count);
        tds_merkle_block forged = {0};
        tds_merkle_store_put_result put_result;
        tds_merkle_search_tree rejected = {0};
        CHECK(bytes != NULL);
        memcpy(bytes, tds_merkle_block_bytes(root_block), byte_count);
        bytes[byte_count - 1] ^= 1;
        CHECK_STATUS(tds_merkle_block_init(
            tds_merkle_block_digest(root_block),
            bytes,
            byte_count,
            NULL,
            &forged));
        free(bytes);
        CHECK_STATUS(tds_merkle_block_store_put(
            &tampered_store,
            &forged,
            &put_result));
        CHECK(put_result == TDS_MERKLE_STORE_ADDED);
        CHECK(tds_merkle_search_tree_load(
            tds_merkle_search_tree_root_hash(&tree),
            &policy,
            &tampered_store,
            NULL,
            &rejected,
            &error) == TDS_MERKLE_VERIFICATION_FAILURE);
        CHECK(error.kind == TDS_MERKLE_VERIFY_DIGEST_MISMATCH &&
            rejected.policy == NULL);
        tds_merkle_block_dispose(&forged);
    }

    tds_merkle_memory_block_store_dispose(&tampered_memory);
    tds_merkle_block_pack_dispose(&pack);
    tds_merkle_search_tree_dispose(&local);
    tds_merkle_search_tree_dispose(&synchronized);
    tds_merkle_search_tree_dispose(&loaded);
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_memory_block_store_dispose(&receiver);
    tds_merkle_memory_block_store_dispose(&memory);
    tds_merkle_policy_dispose(&policy);
    return true;
}

static bool test_msp2_proofs_and_budget_preflight(void) {
    tds_merkle_policy policy = {0};
    tds_merkle_search_tree tree = {0};
    tds_merkle_proof membership = {0};
    tds_merkle_proof nonmembership = {0};
    tds_merkle_proof range = {0};
    tds_merkle_proof forged = {0};
    tds_merkle_proof_verification_result verification;
    tds_merkle_verification_budget budget;
    tds_merkle_verification_error error;
    int32_t key = 47;
    int32_t absent = 1001;
    int32_t minimum = 20;
    int32_t maximum = 75;
    size_t index;
    CHECK_STATUS(make_i32_policy("c-proof-i32-v1", &policy));
    CHECK_STATUS(make_i32_sequence_tree(&policy, 96, &tree));
    CHECK_STATUS(tds_merkle_search_tree_create_proof(&tree, &key, &membership));
    CHECK(tds_merkle_proof_kind_value(&membership) == TDS_MERKLE_PROOF_MEMBERSHIP);
    CHECK(tds_merkle_proof_query_byte_count(&membership) > 5 &&
        memcmp(tds_merkle_proof_query(&membership), "MSP2", 4) == 0);
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &membership,
        &policy,
        NULL,
        &verification));
    CHECK(verification.is_valid && verification.failure_kind == TDS_MERKLE_VERIFY_NONE &&
        verification.verified_block_count == tds_merkle_proof_step_count(&membership));

    CHECK_STATUS(tds_merkle_search_tree_create_proof(&tree, &absent, &nonmembership));
    CHECK(tds_merkle_proof_kind_value(&nonmembership) ==
        TDS_MERKLE_PROOF_NONMEMBERSHIP);
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &nonmembership,
        &policy,
        NULL,
        &verification));
    CHECK(verification.is_valid);
    CHECK_STATUS(tds_merkle_search_tree_create_range_proof(
        &tree,
        &minimum,
        &maximum,
        &range));
    CHECK(tds_merkle_proof_kind_value(&range) == TDS_MERKLE_PROOF_RANGE);
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &range,
        &policy,
        NULL,
        &verification));
    CHECK(verification.is_valid);

    /* Query bytes are gated before step/block allocation, hashing, or codecs. */
    tds_merkle_verification_budget_init_default(&budget);
    budget.max_proof_query_byte_count = 1;
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &membership,
        &policy,
        &budget,
        &verification));
    CHECK(!verification.is_valid &&
        verification.failure_kind == TDS_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED &&
        verification.verified_block_count == 0 &&
        verification.verified_byte_count == 0);

    /* Step count is checked after query accounting but before verifier work. */
    tds_merkle_verification_budget_init_default(&budget);
    CHECK(tds_merkle_proof_step_count(&membership) > 1);
    budget.max_block_count = 1;
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &membership,
        &policy,
        &budget,
        &verification));
    CHECK(!verification.is_valid &&
        verification.failure_kind == TDS_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED &&
        verification.verified_block_count == 0 &&
        verification.verified_byte_count ==
            tds_merkle_proof_query_byte_count(&membership));

    /* Replacing one authenticated proof block with forged bytes invalidates it. */
    {
        const size_t step_count = tds_merkle_proof_step_count(&membership);
        tds_merkle_proof_step_input *steps = (tds_merkle_proof_step_input *)calloc(
            step_count,
            sizeof(*steps));
        const tds_merkle_block *original =
            tds_merkle_proof_step_block(&membership, 0);
        const size_t byte_count = tds_merkle_block_byte_count(original);
        unsigned char *bytes = (unsigned char *)malloc(byte_count);
        tds_merkle_block bad_block = {0};
        CHECK(steps != NULL && bytes != NULL);
        memcpy(bytes, tds_merkle_block_bytes(original), byte_count);
        bytes[byte_count - 1] ^= 0x80;
        CHECK_STATUS(tds_merkle_block_init(
            tds_merkle_block_digest(original),
            bytes,
            byte_count,
            NULL,
            &bad_block));
        free(bytes);
        for (index = 0; index != step_count; ++index) {
            size_t expanded_count;
            steps[index].block = index == 0
                ? &bad_block
                : tds_merkle_proof_step_block(&membership, index);
            steps[index].expanded_child_indexes =
                tds_merkle_proof_step_expanded_children(
                    &membership,
                    index,
                    &expanded_count);
            steps[index].expanded_child_count = expanded_count;
        }
        CHECK_STATUS(tds_merkle_proof_init(
            tds_merkle_proof_algorithm_id(&membership),
            tds_merkle_proof_domain_digest(&membership),
            tds_merkle_proof_root_hash(&membership),
            tds_merkle_proof_kind_value(&membership),
            tds_merkle_proof_query(&membership),
            tds_merkle_proof_query_byte_count(&membership),
            steps,
            step_count,
            NULL,
            &forged,
            &error));
        CHECK_STATUS(tds_merkle_search_tree_verify_proof(
            &forged,
            &policy,
            NULL,
            &verification));
        CHECK(!verification.is_valid &&
            verification.failure_kind == TDS_MERKLE_VERIFY_DIGEST_MISMATCH);
        tds_merkle_block_dispose(&bad_block);
        free(steps);
    }

    tds_merkle_proof_dispose(&forged);
    tds_merkle_proof_dispose(&range);
    tds_merkle_proof_dispose(&nonmembership);
    tds_merkle_proof_dispose(&membership);
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_policy_dispose(&policy);
    return true;
}

static uint32_t test_read_be32(const unsigned char *bytes) {
    return ((uint32_t)bytes[0] << 24) |
        ((uint32_t)bytes[1] << 16) |
        ((uint32_t)bytes[2] << 8) |
        (uint32_t)bytes[3];
}

static bool load_reaches_resource_limit(
    tds_merkle_digest root,
    const tds_merkle_policy *policy,
    const tds_merkle_block_store *store,
    const tds_merkle_verification_budget *budget) {
    tds_merkle_search_tree tree = {0};
    tds_merkle_verification_error error;
    const tds_merkle_status status = tds_merkle_search_tree_load(
        root,
        policy,
        store,
        budget,
        &tree,
        &error);
    const bool result = status == TDS_MERKLE_VERIFICATION_FAILURE &&
        error.kind == TDS_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED &&
        tree.policy == NULL;
    tds_merkle_search_tree_dispose(&tree);
    return result;
}

static bool test_all_budgets_import_closure_and_preflight(void) {
    tds_merkle_policy policy = {0};
    tds_merkle_search_tree tree = {0};
    tds_merkle_search_tree alternate = {0};
    tds_merkle_search_tree imported = {0};
    tds_merkle_memory_block_store source_memory = {0};
    tds_merkle_memory_block_store destination_memory = {0};
    tds_merkle_memory_block_store conflict_memory = {0};
    tds_merkle_block_store source_store;
    tds_merkle_block_store destination_store;
    tds_merkle_block_store conflict_store;
    tds_merkle_block_pack pack = {0};
    tds_merkle_block_pack alternate_pack = {0};
    tds_merkle_block_pack partial_pack = {0};
    tds_merkle_block_pack extended_pack = {0};
    tds_merkle_verification_budget budget;
    tds_merkle_verification_error error;
    tds_merkle_digest root;
    size_t added;
    size_t block_count;
    size_t maximum_block_bytes = 0;
    size_t maximum_child_references = 0;
    size_t index;
    int32_t extra_key = 1000;
    int32_t extra_value = 17003;
    CHECK_STATUS(make_i32_policy("c-budget-import-i32-v1", &policy));
    CHECK_STATUS(make_i32_sequence_tree(&policy, 96, &tree));
    root = tds_merkle_search_tree_root_hash(&tree);
    CHECK_STATUS(tds_merkle_memory_block_store_init(&source_memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(&source_memory, &source_store));
    CHECK_STATUS(tds_merkle_search_tree_save(&tree, &source_store, &added, &error));
    CHECK_STATUS(tds_merkle_search_tree_export_pack(&tree, &pack));
    block_count = tds_merkle_block_pack_block_count(&pack);
    CHECK(block_count > 1 && added == block_count);
    for (index = 0; index != block_count; ++index) {
        const tds_merkle_block *block = tds_merkle_block_pack_block_at(&pack, index);
        const unsigned char *bytes = tds_merkle_block_bytes(block);
        const size_t byte_count = tds_merkle_block_byte_count(block);
        const size_t child_count = (size_t)test_read_be32(bytes + 42) + 1;
        if (byte_count > maximum_block_bytes) {
            maximum_block_bytes = byte_count;
        }
        if (child_count > maximum_child_references) {
            maximum_child_references = child_count;
        }
    }
    CHECK(maximum_child_references > 1 &&
        tds_merkle_block_pack_total_byte_count(&pack) > maximum_block_bytes);

    tds_merkle_verification_budget_init_default(&budget);
    budget.max_block_count = block_count - 1;
    CHECK(load_reaches_resource_limit(root, &policy, &source_store, &budget));

    tds_merkle_verification_budget_init_default(&budget);
    budget.max_total_byte_count =
        tds_merkle_block_pack_total_byte_count(&pack) - 1;
    budget.max_block_byte_count = maximum_block_bytes;
    budget.max_proof_query_byte_count = maximum_block_bytes;
    CHECK(load_reaches_resource_limit(root, &policy, &source_store, &budget));

    tds_merkle_verification_budget_init_default(&budget);
    budget.max_block_byte_count = maximum_block_bytes - 1;
    CHECK(load_reaches_resource_limit(root, &policy, &source_store, &budget));

    tds_merkle_verification_budget_init_default(&budget);
    budget.max_depth = 1;
    CHECK(load_reaches_resource_limit(root, &policy, &source_store, &budget));

    tds_merkle_verification_budget_init_default(&budget);
    budget.max_entry_count = tds_merkle_search_tree_size(&tree) - 1;
    CHECK(load_reaches_resource_limit(root, &policy, &source_store, &budget));

    tds_merkle_verification_budget_init_default(&budget);
    budget.max_child_references_per_block = maximum_child_references - 1;
    CHECK(load_reaches_resource_limit(root, &policy, &source_store, &budget));

    /* The seventh field is the MSP2 query limit and is exercised before all
     * block work, including expansion accounting. */
    {
        tds_merkle_proof proof = {0};
        tds_merkle_proof_verification_result verification;
        int32_t key = 40;
        CHECK_STATUS(tds_merkle_search_tree_create_proof(&tree, &key, &proof));
        tds_merkle_verification_budget_init_default(&budget);
        budget.max_proof_query_byte_count = 1;
        CHECK_STATUS(tds_merkle_search_tree_verify_proof(
            &proof,
            &policy,
            &budget,
            &verification));
        CHECK(!verification.is_valid && verification.verified_block_count == 0 &&
            verification.verified_byte_count == 0);
        tds_merkle_proof_dispose(&proof);
    }
    {
        tds_merkle_proof range = {0};
        tds_merkle_proof_verification_result verification;
        int32_t minimum = 0;
        int32_t maximum = 95;
        size_t maximum_expansion = 0;
        CHECK_STATUS(tds_merkle_search_tree_create_range_proof(
            &tree,
            &minimum,
            &maximum,
            &range));
        for (index = 0; index != tds_merkle_proof_step_count(&range); ++index) {
            size_t expansion_count;
            (void)tds_merkle_proof_step_expanded_children(
                &range,
                index,
                &expansion_count);
            if (expansion_count > maximum_expansion) {
                maximum_expansion = expansion_count;
            }
        }
        CHECK(maximum_expansion > 1);
        tds_merkle_verification_budget_init_default(&budget);
        budget.max_child_references_per_block = maximum_expansion - 1;
        CHECK_STATUS(tds_merkle_search_tree_verify_proof(
            &range,
            &policy,
            &budget,
            &verification));
        CHECK(!verification.is_valid &&
            verification.failure_kind == TDS_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED &&
            verification.verified_block_count == 0 &&
            verification.verified_byte_count ==
                tds_merkle_proof_query_byte_count(&range));
        tds_merkle_proof_dispose(&range);
    }

    CHECK_STATUS(tds_merkle_memory_block_store_init(&destination_memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(
        &destination_memory,
        &destination_store));
    CHECK_STATUS(tds_merkle_search_tree_import_pack(
        &pack,
        &policy,
        &destination_store,
        NULL,
        &imported,
        &error));
    CHECK(tds_merkle_digest_equal(
        tds_merkle_search_tree_root_hash(&imported),
        root));
    tds_merkle_search_tree_dispose(&imported);
    CHECK_STATUS(tds_merkle_block_store_clear(&destination_store));

    /* A root-only pack is authenticated but cannot publish or write because the
     * declared root closure is incomplete. */
    CHECK_STATUS(tds_merkle_block_pack_init(
        tds_merkle_block_pack_algorithm_id(&pack),
        tds_merkle_block_pack_domain_digest(&pack),
        root,
        tds_merkle_block_pack_block_at(&pack, 0),
        1,
        NULL,
        &partial_pack,
        &error));
    CHECK(tds_merkle_search_tree_import_pack(
        &partial_pack,
        &policy,
        &destination_store,
        NULL,
        &imported,
        &error) == TDS_MERKLE_VERIFICATION_FAILURE);
    CHECK(error.kind == TDS_MERKLE_VERIFY_MISSING_BLOCK && imported.policy == NULL);
    CHECK_STATUS(tds_merkle_block_store_count(&destination_store, &added));
    CHECK(added == 0);
    tds_merkle_block_pack_dispose(&partial_pack);

    /* Canonical blocks not reachable from the declared root are legal partial
     * synchronization state and are committed after the root closure verifies. */
    CHECK_STATUS(tds_merkle_search_tree_copy(&tree, &alternate));
    CHECK_STATUS(tds_merkle_search_tree_set(
        &alternate,
        &extra_key,
        &extra_value,
        &alternate));
    CHECK_STATUS(tds_merkle_search_tree_export_pack(&alternate, &alternate_pack));
    {
        const tds_merkle_block *unreachable = NULL;
        tds_merkle_block *blocks = (tds_merkle_block *)calloc(
            block_count + 1,
            sizeof(*blocks));
        size_t alternate_index;
        CHECK(blocks != NULL);
        for (alternate_index = 0;
            alternate_index != tds_merkle_block_pack_block_count(&alternate_pack);
            ++alternate_index) {
            const tds_merkle_block *candidate =
                tds_merkle_block_pack_block_at(&alternate_pack, alternate_index);
            bool found = false;
            for (index = 0; index != block_count; ++index) {
                if (tds_merkle_digest_equal(
                        tds_merkle_block_digest(candidate),
                        tds_merkle_block_digest(
                            tds_merkle_block_pack_block_at(&pack, index)))) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                unreachable = candidate;
                break;
            }
        }
        CHECK(unreachable != NULL);
        for (index = 0; index != block_count; ++index) {
            CHECK_STATUS(tds_merkle_block_copy(
                tds_merkle_block_pack_block_at(&pack, index),
                &blocks[index]));
        }
        CHECK_STATUS(tds_merkle_block_copy(unreachable, &blocks[block_count]));
        CHECK_STATUS(tds_merkle_block_pack_init(
            tds_merkle_block_pack_algorithm_id(&pack),
            tds_merkle_block_pack_domain_digest(&pack),
            root,
            blocks,
            block_count + 1,
            NULL,
            &extended_pack,
            &error));
        for (index = 0; index != block_count + 1; ++index) {
            tds_merkle_block_dispose(&blocks[index]);
        }
        free(blocks);
    }
    CHECK_STATUS(tds_merkle_search_tree_import_pack(
        &extended_pack,
        &policy,
        &destination_store,
        NULL,
        &imported,
        &error));
    CHECK_STATUS(tds_merkle_block_store_count(&destination_store, &added));
    CHECK(added == block_count + 1);
    tds_merkle_search_tree_dispose(&imported);
    CHECK_STATUS(tds_merkle_block_store_clear(&destination_store));

    /* A conflict at the final preflight item prevents every destination put. */
    CHECK_STATUS(tds_merkle_memory_block_store_init(&conflict_memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(
        &conflict_memory,
        &conflict_store));
    {
        const tds_merkle_block *last =
            tds_merkle_block_pack_block_at(&pack, block_count - 1);
        const size_t byte_count = tds_merkle_block_byte_count(last);
        unsigned char *bytes = (unsigned char *)malloc(byte_count);
        tds_merkle_block conflict_block = {0};
        tds_merkle_store_put_result put_result;
        CHECK(bytes != NULL);
        memcpy(bytes, tds_merkle_block_bytes(last), byte_count);
        bytes[byte_count - 1] ^= 1;
        CHECK_STATUS(tds_merkle_block_init(
            tds_merkle_block_digest(last),
            bytes,
            byte_count,
            NULL,
            &conflict_block));
        free(bytes);
        CHECK_STATUS(tds_merkle_block_store_put(
            &conflict_store,
            &conflict_block,
            &put_result));
        CHECK(put_result == TDS_MERKLE_STORE_ADDED);
        CHECK(tds_merkle_search_tree_import_pack(
            &pack,
            &policy,
            &conflict_store,
            NULL,
            &imported,
            &error) == TDS_MERKLE_VERIFICATION_FAILURE);
        CHECK(error.kind == TDS_MERKLE_VERIFY_CONFLICTING_BLOCK &&
            imported.policy == NULL);
        CHECK_STATUS(tds_merkle_block_store_count(&conflict_store, &added));
        CHECK(added == 1);
        CHECK(tds_merkle_block_store_put(
            &conflict_store,
            last,
            &put_result) == TDS_MERKLE_VERIFICATION_FAILURE);
        CHECK(put_result == TDS_MERKLE_STORE_CONFLICT);
        tds_merkle_block_dispose(&conflict_block);
    }

    tds_merkle_memory_block_store_dispose(&conflict_memory);
    tds_merkle_block_pack_dispose(&extended_pack);
    tds_merkle_block_pack_dispose(&alternate_pack);
    tds_merkle_search_tree_dispose(&alternate);
    tds_merkle_memory_block_store_dispose(&destination_memory);
    tds_merkle_block_pack_dispose(&pack);
    tds_merkle_memory_block_store_dispose(&source_memory);
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_policy_dispose(&policy);
    return true;
}

typedef struct bomb_codec_state {
    tds_merkle_codec inner;
    size_t encode_calls;
    size_t decode_calls;
    bool fail_encode;
    bool fail_decode;
} bomb_codec_state;

static tds_merkle_status bomb_codec_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const tds_merkle_allocator *allocator,
    void *context) {
    bomb_codec_state *state = (bomb_codec_state *)context;
    ++state->encode_calls;
    if (state->fail_encode) {
        return TDS_MERKLE_CALLBACK_FAILURE;
    }
    return state->inner.encode(
        value,
        destination,
        destination_size,
        bytes_written,
        allocator,
        state->inner.context);
}

static tds_merkle_status bomb_codec_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const tds_merkle_allocator *allocator,
    void *context) {
    bomb_codec_state *state = (bomb_codec_state *)context;
    ++state->decode_calls;
    if (state->fail_decode) {
        return TDS_MERKLE_CALLBACK_FAILURE;
    }
    return state->inner.decode(
        encoding,
        encoding_size,
        destination,
        allocator,
        state->inner.context);
}

static void install_bomb_codec(
    tds_merkle_codec *codec,
    bomb_codec_state *state) {
    state->inner = *codec;
    state->encode_calls = 0;
    state->decode_calls = 0;
    state->fail_encode = false;
    state->fail_decode = false;
    codec->encode = bomb_codec_encode;
    codec->decode = bomb_codec_decode;
    codec->context = state;
}

static tds_merkle_proof_step_input *copy_proof_step_inputs(
    const tds_merkle_proof *proof,
    size_t extra_count) {
    const size_t count = tds_merkle_proof_step_count(proof);
    tds_merkle_proof_step_input *steps =
        (tds_merkle_proof_step_input *)calloc(count + extra_count, sizeof(*steps));
    size_t index;
    if (steps == NULL) {
        return NULL;
    }
    for (index = 0; index != count; ++index) {
        steps[index].block = tds_merkle_proof_step_block(proof, index);
        steps[index].expanded_child_indexes =
            tds_merkle_proof_step_expanded_children(
                proof,
                index,
                &steps[index].expanded_child_count);
    }
    return steps;
}

static bool proof_verifies_invalid(
    const tds_merkle_proof *proof,
    const tds_merkle_policy *policy) {
    tds_merkle_proof_verification_result result;
    return tds_merkle_search_tree_verify_proof(proof, policy, NULL, &result) ==
            TDS_MERKLE_OK &&
        !result.is_valid;
}

static bool test_msp2_structural_tamper_and_bomb_precedence(void) {
    static const unsigned char foreign_algorithm_bytes[] = "foreign-mst-v1";
    tds_merkle_policy policy = {0};
    tds_merkle_policy bomb_policy = {0};
    tds_merkle_policy_config bomb_config;
    bomb_codec_state bomb_key;
    bomb_codec_state bomb_value;
    counting_allocator bomb_allocator = {0};
    tds_merkle_search_tree tree = {0};
    tds_merkle_proof point = {0};
    tds_merkle_proof range = {0};
    tds_merkle_proof variant = {0};
    tds_merkle_proof unsupported = {0};
    tds_merkle_proof foreign_domain = {0};
    tds_merkle_proof foreign_range = {0};
    tds_merkle_block_pack pack = {0};
    tds_merkle_verification_error error;
    tds_merkle_proof_verification_result verification;
    tds_merkle_verification_budget budget;
    tds_merkle_digest changed_domain;
    int32_t key = 47;
    int32_t minimum = 10;
    int32_t maximum = 80;
    size_t point_count;
    size_t range_count;
    size_t maximum_expansion = 0;
    size_t index;
    const tds_merkle_block *unrelated = NULL;
    CHECK_STATUS(make_i32_policy("c-proof-tamper-i32-v1", &policy));
    CHECK_STATUS(make_i32_sequence_tree(&policy, 96, &tree));
    CHECK_STATUS(tds_merkle_search_tree_create_proof(&tree, &key, &point));
    CHECK_STATUS(tds_merkle_search_tree_create_range_proof(
        &tree,
        &minimum,
        &maximum,
        &range));
    CHECK_STATUS(tds_merkle_search_tree_export_pack(&tree, &pack));
    point_count = tds_merkle_proof_step_count(&point);
    range_count = tds_merkle_proof_step_count(&range);
    CHECK(point_count > 1 && range_count > 1);
    for (index = 0; index != tds_merkle_block_pack_block_count(&pack); ++index) {
        const tds_merkle_block *candidate =
            tds_merkle_block_pack_block_at(&pack, index);
        size_t step;
        bool present = false;
        for (step = 0; step != point_count; ++step) {
            if (tds_merkle_digest_equal(
                    tds_merkle_block_digest(candidate),
                    tds_merkle_block_digest(
                        tds_merkle_proof_step_block(&point, step)))) {
                present = true;
                break;
            }
        }
        if (!present) {
            unrelated = candidate;
            break;
        }
    }
    CHECK(unrelated != NULL);

    /* Canonical query tamper. */
    {
        tds_merkle_proof_step_input *steps = copy_proof_step_inputs(&point, 0);
        const size_t query_count = tds_merkle_proof_query_byte_count(&point);
        unsigned char *query = (unsigned char *)malloc(query_count);
        CHECK(steps != NULL && query != NULL);
        memcpy(query, tds_merkle_proof_query(&point), query_count);
        query[query_count - 1] ^= 1;
        CHECK_STATUS(tds_merkle_proof_init(
            tds_merkle_proof_algorithm_id(&point),
            tds_merkle_proof_domain_digest(&point),
            tds_merkle_proof_root_hash(&point),
            tds_merkle_proof_kind_value(&point),
            query,
            query_count,
            steps,
            point_count,
            NULL,
            &variant,
            &error));
        CHECK(proof_verifies_invalid(&variant, &policy));
        tds_merkle_proof_dispose(&variant);
        free(query);
        free(steps);
    }

    /* Wrong authenticated step, omitted step, and extra authenticated step. */
    {
        tds_merkle_proof_step_input *steps = copy_proof_step_inputs(&point, 1);
        CHECK(steps != NULL);
        steps[0].block = unrelated;
        CHECK_STATUS(tds_merkle_proof_init(
            tds_merkle_proof_algorithm_id(&point),
            tds_merkle_proof_domain_digest(&point),
            tds_merkle_proof_root_hash(&point),
            tds_merkle_proof_kind_value(&point),
            tds_merkle_proof_query(&point),
            tds_merkle_proof_query_byte_count(&point),
            steps,
            point_count,
            NULL,
            &variant,
            &error));
        CHECK(proof_verifies_invalid(&variant, &policy));
        tds_merkle_proof_dispose(&variant);
        steps[0].block = tds_merkle_proof_step_block(&point, 0);
        CHECK_STATUS(tds_merkle_proof_init(
            tds_merkle_proof_algorithm_id(&point),
            tds_merkle_proof_domain_digest(&point),
            tds_merkle_proof_root_hash(&point),
            tds_merkle_proof_kind_value(&point),
            tds_merkle_proof_query(&point),
            tds_merkle_proof_query_byte_count(&point),
            steps,
            point_count - 1,
            NULL,
            &variant,
            &error));
        CHECK(proof_verifies_invalid(&variant, &policy));
        tds_merkle_proof_dispose(&variant);
        steps[point_count].block = unrelated;
        steps[point_count].expanded_child_indexes = NULL;
        steps[point_count].expanded_child_count = 0;
        CHECK_STATUS(tds_merkle_proof_init(
            tds_merkle_proof_algorithm_id(&point),
            tds_merkle_proof_domain_digest(&point),
            tds_merkle_proof_root_hash(&point),
            tds_merkle_proof_kind_value(&point),
            tds_merkle_proof_query(&point),
            tds_merkle_proof_query_byte_count(&point),
            steps,
            point_count + 1,
            NULL,
            &variant,
            &error));
        CHECK(proof_verifies_invalid(&variant, &policy));
        tds_merkle_proof_dispose(&variant);
        free(steps);
    }

    /* Wrong range expansion: omit one required expansion edge. */
    {
        tds_merkle_proof_step_input *steps = copy_proof_step_inputs(&range, 0);
        size_t changed_index = SIZE_MAX;
        CHECK(steps != NULL);
        for (index = 0; index != range_count; ++index) {
            if (steps[index].expanded_child_count != 0) {
                if (steps[index].expanded_child_count > maximum_expansion) {
                    maximum_expansion = steps[index].expanded_child_count;
                }
                if (changed_index == SIZE_MAX) {
                    changed_index = index;
                }
            }
        }
        CHECK(changed_index != SIZE_MAX && maximum_expansion > 1);
        --steps[changed_index].expanded_child_count;
        CHECK_STATUS(tds_merkle_proof_init(
            tds_merkle_proof_algorithm_id(&range),
            tds_merkle_proof_domain_digest(&range),
            tds_merkle_proof_root_hash(&range),
            tds_merkle_proof_kind_value(&range),
            tds_merkle_proof_query(&range),
            tds_merkle_proof_query_byte_count(&range),
            steps,
            range_count,
            NULL,
            &variant,
            &error));
        CHECK(proof_verifies_invalid(&variant, &policy));
        tds_merkle_proof_dispose(&variant);
        free(steps);
    }

    changed_domain = tds_merkle_proof_domain_digest(&point);
    changed_domain.bytes[0] ^= 1;
    {
        tds_merkle_proof_step_input *point_steps = copy_proof_step_inputs(&point, 0);
        tds_merkle_proof_step_input *range_steps = copy_proof_step_inputs(&range, 0);
        const tds_merkle_identifier foreign_algorithm = {
            foreign_algorithm_bytes,
            sizeof(foreign_algorithm_bytes) - 1};
        CHECK(point_steps != NULL && range_steps != NULL);
        CHECK_STATUS(tds_merkle_proof_init(
            foreign_algorithm,
            tds_merkle_proof_domain_digest(&point),
            tds_merkle_proof_root_hash(&point),
            tds_merkle_proof_kind_value(&point),
            tds_merkle_proof_query(&point),
            tds_merkle_proof_query_byte_count(&point),
            point_steps,
            point_count,
            NULL,
            &unsupported,
            &error));
        CHECK_STATUS(tds_merkle_proof_init(
            tds_merkle_proof_algorithm_id(&point),
            changed_domain,
            tds_merkle_proof_root_hash(&point),
            tds_merkle_proof_kind_value(&point),
            tds_merkle_proof_query(&point),
            tds_merkle_proof_query_byte_count(&point),
            point_steps,
            point_count,
            NULL,
            &foreign_domain,
            &error));
        CHECK_STATUS(tds_merkle_proof_init(
            tds_merkle_proof_algorithm_id(&range),
            changed_domain,
            tds_merkle_proof_root_hash(&range),
            tds_merkle_proof_kind_value(&range),
            tds_merkle_proof_query(&range),
            tds_merkle_proof_query_byte_count(&range),
            range_steps,
            range_count,
            NULL,
            &foreign_range,
            &error));
        free(range_steps);
        free(point_steps);
    }
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &unsupported,
        &policy,
        NULL,
        &verification));
    CHECK(!verification.is_valid &&
        verification.failure_kind == TDS_MERKLE_VERIFY_UNSUPPORTED_ALGORITHM &&
        verification.verified_block_count == 0);
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &foreign_domain,
        &policy,
        NULL,
        &verification));
    CHECK(!verification.is_valid &&
        verification.failure_kind == TDS_MERKLE_VERIFY_DOMAIN_MISMATCH &&
        verification.verified_block_count == 0);

    /* Same identifiers, but bomb codecs and an allocator that fails its first
     * attempt prove the three structural preflights precede verifier work. */
    configure_i32_policy(&bomb_config, "c-proof-tamper-i32-v1");
    install_bomb_codec(&bomb_config.key_codec, &bomb_key);
    install_bomb_codec(&bomb_config.value_codec, &bomb_value);
    bomb_key.fail_encode = true;
    bomb_key.fail_decode = true;
    bomb_value.fail_encode = true;
    bomb_value.fail_decode = true;
    bomb_config.allocator.allocate = counting_allocate;
    bomb_config.allocator.deallocate = counting_deallocate;
    bomb_config.allocator.context = &bomb_allocator;
    CHECK_STATUS(tds_merkle_policy_create(&bomb_config, &bomb_policy));
    bomb_allocator.attempts = 0;
    bomb_allocator.fail_at = 1;
    tds_merkle_verification_budget_init_default(&budget);
    budget.max_proof_query_byte_count = 1;
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &unsupported,
        &bomb_policy,
        &budget,
        &verification));
    CHECK(!verification.is_valid &&
        verification.failure_kind == TDS_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED &&
        verification.verified_byte_count == 0);
    tds_merkle_verification_budget_init_default(&budget);
    budget.max_block_count = 1;
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &foreign_domain,
        &bomb_policy,
        &budget,
        &verification));
    CHECK(!verification.is_valid &&
        verification.failure_kind == TDS_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED &&
        verification.verified_byte_count ==
            tds_merkle_proof_query_byte_count(&foreign_domain));
    tds_merkle_verification_budget_init_default(&budget);
    budget.max_child_references_per_block = maximum_expansion - 1;
    CHECK_STATUS(tds_merkle_search_tree_verify_proof(
        &foreign_range,
        &bomb_policy,
        &budget,
        &verification));
    CHECK(!verification.is_valid &&
        verification.failure_kind == TDS_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED &&
        verification.verified_byte_count ==
            tds_merkle_proof_query_byte_count(&foreign_range));
    CHECK(bomb_allocator.attempts == 0 &&
        bomb_key.encode_calls == 0 && bomb_key.decode_calls == 0 &&
        bomb_value.encode_calls == 0 && bomb_value.decode_calls == 0);
    bomb_allocator.fail_at = 0;

    tds_merkle_policy_dispose(&bomb_policy);
    CHECK(bomb_allocator.live == 0);
    tds_merkle_proof_dispose(&foreign_range);
    tds_merkle_proof_dispose(&foreign_domain);
    tds_merkle_proof_dispose(&unsupported);
    tds_merkle_block_pack_dispose(&pack);
    tds_merkle_proof_dispose(&range);
    tds_merkle_proof_dispose(&point);
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_policy_dispose(&policy);
    return true;
}

typedef struct merge_resolution_context {
    tds_merkle_merge_resolution_kind kind;
    int32_t value;
    size_t calls;
} merge_resolution_context;

static tds_merkle_status resolve_i32_conflict(
    tds_merkle_three_way_merge_conflict_ref conflict,
    tds_merkle_merge_resolution *resolution,
    void *context) {
    merge_resolution_context *state = (merge_resolution_context *)context;
    CHECK(conflict.key != NULL);
    ++state->calls;
    resolution->kind = state->kind;
    resolution->value = state->kind == TDS_MERKLE_MERGE_SET_VALUE
        ? &state->value
        : NULL;
    return TDS_MERKLE_OK;
}

static bool tree_has_i32_value(
    const tds_merkle_search_tree *tree,
    int32_t key,
    bool expected_found,
    int32_t expected_value) {
    bool found = false;
    tds_merkle_search_entry_ref entry = {0};
    if (tds_merkle_search_tree_try_get_entry_ref(tree, &key, &found, &entry) !=
        TDS_MERKLE_OK || found != expected_found) {
        return false;
    }
    return !found || *(const int32_t *)entry.value == expected_value;
}

static bool test_three_way_merge_results_and_policy_identity(void) {
    tds_merkle_policy policy = {0};
    tds_merkle_policy equivalent_policy = {0};
    tds_merkle_search_tree base = {0};
    tds_merkle_search_tree left = {0};
    tds_merkle_search_tree right = {0};
    tds_merkle_search_tree equivalent = {0};
    tds_merkle_search_tree merged = {0};
    tds_merkle_three_way_merge_result result = {0};
    tds_merkle_three_way_merge_conflict_ref conflict;
    merge_resolution_context resolver = {
        TDS_MERKLE_MERGE_USE_RIGHT,
        999,
        0};
    int32_t key1 = 1;
    int32_t key2 = 2;
    int32_t key3 = 3;
    int32_t key4 = 4;
    int32_t base1 = 10;
    int32_t base2 = 20;
    int32_t left1 = 11;
    int32_t left3 = 30;
    int32_t right1 = 12;
    int32_t right4 = 40;
    CHECK_STATUS(make_i32_policy("c-merge-i32-v1", &policy));
    CHECK_STATUS(make_i32_policy("c-merge-i32-v1", &equivalent_policy));
    CHECK_STATUS(tds_merkle_search_tree_init(&base, &policy));
    CHECK_STATUS(tds_merkle_search_tree_set(&base, &key1, &base1, &base));
    CHECK_STATUS(tds_merkle_search_tree_set(&base, &key2, &base2, &base));
    CHECK_STATUS(tds_merkle_search_tree_copy(&base, &left));
    CHECK_STATUS(tds_merkle_search_tree_copy(&base, &right));
    CHECK_STATUS(tds_merkle_search_tree_set(&left, &key1, &left1, &left));
    CHECK_STATUS(tds_merkle_search_tree_set(&left, &key3, &left3, &left));
    CHECK_STATUS(tds_merkle_search_tree_set(&right, &key1, &right1, &right));
    CHECK_STATUS(tds_merkle_search_tree_remove(&right, &key2, &right));
    CHECK_STATUS(tds_merkle_search_tree_set(&right, &key4, &right4, &right));

    CHECK_STATUS(tds_merkle_search_tree_merge(
        &base,
        &left,
        &right,
        NULL,
        NULL,
        &result));
    CHECK(!tds_merkle_three_way_merge_result_success(&result));
    CHECK(tds_merkle_three_way_merge_result_conflict_count(&result) == 1);
    CHECK(tds_merkle_three_way_merge_result_copy_tree(&result, &merged) ==
        TDS_MERKLE_INVALID_ARGUMENT);
    CHECK_STATUS(tds_merkle_three_way_merge_result_conflict_at(
        &result,
        0,
        &conflict));
    CHECK(*(const int32_t *)conflict.key == key1 &&
        conflict.base.present && *(const int32_t *)conflict.base.value == base1 &&
        conflict.left.present && *(const int32_t *)conflict.left.value == left1 &&
        conflict.right.present && *(const int32_t *)conflict.right.value == right1);
    tds_merkle_three_way_merge_result_dispose(&result);

    CHECK_STATUS(tds_merkle_search_tree_merge(
        &base,
        &left,
        &right,
        resolve_i32_conflict,
        &resolver,
        &result));
    CHECK(tds_merkle_three_way_merge_result_success(&result) && resolver.calls == 1);
    CHECK_STATUS(tds_merkle_three_way_merge_result_copy_tree(&result, &merged));
    CHECK(tree_has_i32_value(&merged, key1, true, right1));
    CHECK(tree_has_i32_value(&merged, key2, false, 0));
    CHECK(tree_has_i32_value(&merged, key3, true, left3));
    CHECK(tree_has_i32_value(&merged, key4, true, right4));
    tds_merkle_search_tree_dispose(&merged);
    tds_merkle_three_way_merge_result_dispose(&result);

    resolver.kind = TDS_MERKLE_MERGE_USE_BASE;
    resolver.calls = 0;
    CHECK_STATUS(tds_merkle_search_tree_merge(
        &base,
        &left,
        &right,
        resolve_i32_conflict,
        &resolver,
        &result));
    CHECK_STATUS(tds_merkle_three_way_merge_result_copy_tree(&result, &merged));
    CHECK(tree_has_i32_value(&merged, key1, true, base1));
    tds_merkle_search_tree_dispose(&merged);
    tds_merkle_three_way_merge_result_dispose(&result);

    resolver.kind = TDS_MERKLE_MERGE_USE_LEFT;
    resolver.calls = 0;
    CHECK_STATUS(tds_merkle_search_tree_merge(
        &base,
        &left,
        &right,
        resolve_i32_conflict,
        &resolver,
        &result));
    CHECK_STATUS(tds_merkle_three_way_merge_result_copy_tree(&result, &merged));
    CHECK(tree_has_i32_value(&merged, key1, true, left1));
    tds_merkle_search_tree_dispose(&merged);
    tds_merkle_three_way_merge_result_dispose(&result);

    resolver.kind = TDS_MERKLE_MERGE_SET_VALUE;
    resolver.calls = 0;
    CHECK_STATUS(tds_merkle_search_tree_merge(
        &base,
        &left,
        &right,
        resolve_i32_conflict,
        &resolver,
        &result));
    CHECK(tds_merkle_three_way_merge_result_success(&result));
    CHECK_STATUS(tds_merkle_three_way_merge_result_copy_tree(&result, &merged));
    CHECK(tree_has_i32_value(&merged, key1, true, resolver.value));
    tds_merkle_search_tree_dispose(&merged);
    tds_merkle_three_way_merge_result_dispose(&result);

    resolver.kind = TDS_MERKLE_MERGE_DELETE;
    resolver.calls = 0;
    CHECK_STATUS(tds_merkle_search_tree_merge(
        &base,
        &left,
        &right,
        resolve_i32_conflict,
        &resolver,
        &result));
    CHECK_STATUS(tds_merkle_three_way_merge_result_copy_tree(&result, &merged));
    CHECK(tree_has_i32_value(&merged, key1, false, 0));
    tds_merkle_search_tree_dispose(&merged);
    tds_merkle_three_way_merge_result_dispose(&result);

    CHECK_STATUS(tds_merkle_search_tree_init(&equivalent, &equivalent_policy));
    CHECK(tds_merkle_search_tree_merge(
        &base,
        &left,
        &equivalent,
        NULL,
        NULL,
        &result) == TDS_MERKLE_INCOMPATIBLE_POLICY);
    CHECK(result.rep == NULL);

    tds_merkle_search_tree_dispose(&equivalent);
    tds_merkle_search_tree_dispose(&right);
    tds_merkle_search_tree_dispose(&left);
    tds_merkle_search_tree_dispose(&base);
    tds_merkle_policy_dispose(&equivalent_policy);
    tds_merkle_policy_dispose(&policy);
    return true;
}

static bool test_merge_present_null_is_not_deletion(void) {
    tds_merkle_policy policy = {0};
    tds_merkle_search_tree base = {0};
    tds_merkle_search_tree left = {0};
    tds_merkle_search_tree right = {0};
    tds_merkle_three_way_merge_result result = {0};
    tds_merkle_three_way_merge_conflict_ref conflict;
    tds_merkle_nullable_utf8 semantic_null = {false, NULL, 0};
    tds_merkle_nullable_utf8 text = {true, "right", 5};
    int32_t key = 7;
    CHECK_STATUS(make_string_policy(&policy));
    CHECK_STATUS(tds_merkle_search_tree_init(&base, &policy));
    CHECK_STATUS(tds_merkle_search_tree_set(
        &base,
        &key,
        &semantic_null,
        &base));
    CHECK_STATUS(tds_merkle_search_tree_copy(&base, &left));
    CHECK_STATUS(tds_merkle_search_tree_copy(&base, &right));
    CHECK_STATUS(tds_merkle_search_tree_remove(&left, &key, &left));
    CHECK_STATUS(tds_merkle_search_tree_set(&right, &key, &text, &right));
    CHECK_STATUS(tds_merkle_search_tree_merge(
        &base,
        &left,
        &right,
        NULL,
        NULL,
        &result));
    CHECK(!tds_merkle_three_way_merge_result_success(&result));
    CHECK(tds_merkle_three_way_merge_result_conflict_count(&result) == 1);
    CHECK_STATUS(tds_merkle_three_way_merge_result_conflict_at(
        &result,
        0,
        &conflict));
    CHECK(conflict.base.present && conflict.base.value != NULL);
    CHECK(!((const tds_merkle_nullable_utf8 *)conflict.base.value)->has_value);
    CHECK(!conflict.left.present && conflict.left.value == NULL);
    CHECK(conflict.right.present && conflict.right.value != NULL &&
        ((const tds_merkle_nullable_utf8 *)conflict.right.value)->has_value &&
        ((const tds_merkle_nullable_utf8 *)conflict.right.value)->size == 5);
    tds_merkle_three_way_merge_result_dispose(&result);
    tds_merkle_search_tree_dispose(&right);
    tds_merkle_search_tree_dispose(&left);
    tds_merkle_search_tree_dispose(&base);
    tds_merkle_policy_dispose(&policy);
    return true;
}

typedef struct concurrent_store_context {
    const tds_merkle_block_store *store;
    const tds_merkle_block *put_block;
    const tds_merkle_block *expected_block;
    bool expect_conflict;
} concurrent_store_context;

static bool concurrent_store_worker(const concurrent_store_context *context) {
    size_t iteration;
    for (iteration = 0; iteration != 128; ++iteration) {
        tds_merkle_store_put_result put_result = TDS_MERKLE_STORE_CONFLICT;
        tds_merkle_block snapshot = {0};
        bool found = false;
        const tds_merkle_status status = tds_merkle_block_store_put(
            context->store,
            context->put_block,
            &put_result);
        if (context->expect_conflict) {
            if (status != TDS_MERKLE_VERIFICATION_FAILURE ||
                put_result != TDS_MERKLE_STORE_CONFLICT) {
                return false;
            }
        } else if (status != TDS_MERKLE_OK ||
            (put_result != TDS_MERKLE_STORE_ADDED &&
                put_result != TDS_MERKLE_STORE_PRESENT_IDENTICAL)) {
            return false;
        }
        if (tds_merkle_block_store_try_get(
                context->store,
                tds_merkle_block_digest(context->expected_block),
                &found,
                &snapshot) != TDS_MERKLE_OK ||
            !found || !tds_merkle_block_equal(&snapshot, context->expected_block)) {
            tds_merkle_block_dispose(&snapshot);
            return false;
        }
        tds_merkle_block_dispose(&snapshot);
    }
    return true;
}

#ifdef _WIN32
static DWORD WINAPI concurrent_store_thread(void *parameter) {
    return concurrent_store_worker((const concurrent_store_context *)parameter)
        ? 0u
        : 1u;
}
#else
static int concurrent_store_thread(void *parameter) {
    return concurrent_store_worker((const concurrent_store_context *)parameter)
        ? 0
        : 1;
}
#endif

static bool run_concurrent_store_phase(const concurrent_store_context *context) {
#ifdef _WIN32
    enum { thread_count = 8 };
    HANDLE threads[thread_count];
    DWORD index;
    for (index = 0; index != thread_count; ++index) {
        threads[index] = CreateThread(
            NULL,
            0,
            concurrent_store_thread,
            (void *)context,
            0,
            NULL);
        if (threads[index] == NULL) {
            return false;
        }
    }
    if (WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE) !=
        WAIT_OBJECT_0) {
        return false;
    }
    for (index = 0; index != thread_count; ++index) {
        DWORD exit_code = 1;
        if (!GetExitCodeThread(threads[index], &exit_code) || exit_code != 0 ||
            !CloseHandle(threads[index])) {
            return false;
        }
    }
#else
    enum { thread_count = 8 };
    thrd_t threads[thread_count];
    bool success = true;
    size_t created = 0;
    size_t index;
    for (index = 0; index != thread_count; ++index) {
        if (thrd_create(
                &threads[index],
                concurrent_store_thread,
                (void *)context) != thrd_success) {
            int ignored;
            while (created != 0) {
                --created;
                (void)thrd_join(threads[created], &ignored);
            }
            return false;
        }
        ++created;
    }
    for (index = 0; index != thread_count; ++index) {
        int exit_code = 1;
        if (thrd_join(threads[index], &exit_code) != thrd_success ||
            exit_code != 0) {
            success = false;
        }
    }
    if (!success) {
        return false;
    }
#endif
    return true;
}

static bool test_concurrent_memory_store_puts_and_owned_snapshots(void) {
    tds_merkle_memory_block_store memory = {0};
    tds_merkle_memory_block_store owner_copy = {0};
    tds_merkle_block_store store;
    tds_merkle_block original = {0};
    tds_merkle_block conflicting = {0};
    tds_merkle_block retained = {0};
    tds_merkle_digest digest = {{0}};
    concurrent_store_context context;
    bool found = false;
    size_t count = 0;
    digest.bytes[0] = 0x5a;
    CHECK_STATUS(tds_merkle_block_init(
        digest,
        (const unsigned char *)"original",
        8,
        NULL,
        &original));
    CHECK_STATUS(tds_merkle_block_init(
        digest,
        (const unsigned char *)"forged!!",
        8,
        NULL,
        &conflicting));
    CHECK_STATUS(tds_merkle_memory_block_store_init(&memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_copy(&memory, &owner_copy));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(&memory, &store));
    context = (concurrent_store_context){&store, &original, &original, false};
    CHECK(run_concurrent_store_phase(&context));
    CHECK_STATUS(tds_merkle_block_store_count(&store, &count));
    CHECK(count == 1);
    context = (concurrent_store_context){&store, &conflicting, &original, true};
    CHECK(run_concurrent_store_phase(&context));
    CHECK_STATUS(tds_merkle_block_store_count(&store, &count));
    CHECK(count == 1);
    CHECK_STATUS(tds_merkle_block_store_try_get(
        &store,
        digest,
        &found,
        &retained));
    CHECK(found && tds_merkle_block_equal(&retained, &original));
    /* The adapter remains usable through an owning copied handle. */
    tds_merkle_memory_block_store_dispose(&memory);
    CHECK_STATUS(tds_merkle_block_store_count(&store, &count));
    CHECK(count == 1);
    tds_merkle_memory_block_store_dispose(&owner_copy);
    /* Store destruction cannot invalidate a snapshot already returned owned. */
    CHECK(tds_merkle_block_byte_count(&retained) == 8 &&
        memcmp(tds_merkle_block_bytes(&retained), "original", 8) == 0);
    tds_merkle_block_dispose(&retained);
    tds_merkle_block_dispose(&conflicting);
    tds_merkle_block_dispose(&original);
    return true;
}

typedef struct reentrant_store_allocator {
    const tds_merkle_block_store *store;
    bool inside_callback;
    size_t allocation_calls;
    size_t deallocation_calls;
    size_t live_allocations;
    size_t reentrant_store_calls;
    size_t reentrant_failures;
} reentrant_store_allocator;

static void reentrant_store_probe(reentrant_store_allocator *state) {
    if (state->store != NULL && !state->inside_callback) {
        size_t count = SIZE_MAX;
        state->inside_callback = true;
        ++state->reentrant_store_calls;
        if (tds_merkle_block_store_count(state->store, &count) != TDS_MERKLE_OK) {
            ++state->reentrant_failures;
        }
        state->inside_callback = false;
    }
}

static void *reentrant_store_allocate(size_t size, void *context) {
    reentrant_store_allocator *state = (reentrant_store_allocator *)context;
    void *allocation;
    ++state->allocation_calls;
    reentrant_store_probe(state);
    allocation = malloc(size);
    if (allocation != NULL) {
        ++state->live_allocations;
    }
    return allocation;
}

static void reentrant_store_deallocate(void *allocation, void *context) {
    reentrant_store_allocator *state = (reentrant_store_allocator *)context;
    if (allocation != NULL) {
        ++state->deallocation_calls;
        reentrant_store_probe(state);
        if (state->live_allocations == 0) {
            abort();
        }
        --state->live_allocations;
        free(allocation);
    }
}

typedef struct reentrant_digest_visitor_state {
    const tds_merkle_block_store *store;
    digest_order_capture order;
    size_t calls;
} reentrant_digest_visitor_state;

static tds_merkle_status reentrant_digest_visitor(
    tds_merkle_digest digest,
    void *context) {
    reentrant_digest_visitor_state *state =
        (reentrant_digest_visitor_state *)context;
    size_t count = SIZE_MAX;
    tds_merkle_status status = tds_merkle_block_store_count(
        state->store,
        &count);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    ++state->calls;
    return capture_digest_order(digest, &state->order);
}

static bool test_memory_store_never_calls_user_code_under_lock(void) {
    reentrant_store_allocator state = {0};
    tds_merkle_allocator allocator = {
        reentrant_store_allocate,
        reentrant_store_deallocate,
        &state};
    tds_merkle_memory_block_store memory = {0};
    tds_merkle_block_store store;
    tds_merkle_digest digests[9] = {{{0}}};
    reentrant_digest_visitor_state visitor_state;
    size_t count = SIZE_MAX;
    size_t index;
    bool removed = false;
    memset(&visitor_state, 0, sizeof(visitor_state));
    visitor_state.order.sorted = true;
    CHECK_STATUS(tds_merkle_memory_block_store_init(&memory, &allocator));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(&memory, &store));
    state.store = &store;
    visitor_state.store = &store;
    for (index = 0; index != 9; ++index) {
        tds_merkle_block block = {0};
        tds_merkle_store_put_result put_result;
        size_t deallocations_before_put;
        digests[index].bytes[0] = (unsigned char)(0x20 + index);
        CHECK_STATUS(tds_merkle_block_init(
            digests[index],
            (const unsigned char *)"reentrant",
            9,
            &allocator,
            &block));
        deallocations_before_put = state.deallocation_calls;
        CHECK_STATUS(tds_merkle_block_store_put(&store, &block, &put_result));
        CHECK(put_result == TDS_MERKLE_STORE_ADDED);
        if (index == 8) {
            /* The ninth insert grows 8 -> 16 and retires the old array. */
            CHECK(state.deallocation_calls > deallocations_before_put);
        }
        tds_merkle_block_dispose(&block);
    }
    CHECK_STATUS(tds_merkle_memory_block_store_visit_digests(
        &memory,
        reentrant_digest_visitor,
        &visitor_state));
    CHECK(visitor_state.calls == 9 && visitor_state.order.count == 9 &&
        visitor_state.order.sorted);
    CHECK_STATUS(tds_merkle_block_store_remove(
        &store,
        digests[4],
        &removed));
    CHECK(removed);
    CHECK_STATUS(tds_merkle_block_store_count(&store, &count));
    CHECK(count == 8);
    CHECK_STATUS(tds_merkle_block_store_clear(&store));
    CHECK_STATUS(tds_merkle_block_store_count(&store, &count));
    CHECK(count == 0 && state.reentrant_store_calls >= 20 &&
        state.reentrant_failures == 0);
    /* Final-handle disposal ends adapter lifetime; disable deliberate reentry. */
    state.store = NULL;
    tds_merkle_memory_block_store_dispose(&memory);
    CHECK(state.live_allocations == 0 &&
        state.allocation_calls == state.deallocation_calls);
    return true;
}

typedef struct failing_store_context {
    const tds_merkle_block_store *inner;
    size_t try_get_calls;
    size_t fail_try_get_at;
    size_t put_calls;
    size_t fail_put_at;
} failing_store_context;

static tds_merkle_status failing_store_count(size_t *count, void *context) {
    failing_store_context *state = (failing_store_context *)context;
    return tds_merkle_block_store_count(state->inner, count);
}

static tds_merkle_status failing_store_contains(
    tds_merkle_digest digest,
    bool *contains,
    void *context) {
    failing_store_context *state = (failing_store_context *)context;
    return tds_merkle_block_store_contains(state->inner, digest, contains);
}

static tds_merkle_status failing_store_try_get(
    tds_merkle_digest digest,
    bool *found,
    tds_merkle_block *block,
    void *context) {
    failing_store_context *state = (failing_store_context *)context;
    ++state->try_get_calls;
    if (state->fail_try_get_at != 0 &&
        state->try_get_calls == state->fail_try_get_at) {
        return TDS_MERKLE_CALLBACK_FAILURE;
    }
    return tds_merkle_block_store_try_get(state->inner, digest, found, block);
}

static tds_merkle_status failing_store_put(
    const tds_merkle_block *block,
    tds_merkle_store_put_result *result,
    void *context) {
    failing_store_context *state = (failing_store_context *)context;
    ++state->put_calls;
    if (state->fail_put_at != 0 && state->put_calls == state->fail_put_at) {
        return TDS_MERKLE_CALLBACK_FAILURE;
    }
    return tds_merkle_block_store_put(state->inner, block, result);
}

static tds_merkle_status failing_store_remove(
    tds_merkle_digest digest,
    bool *removed,
    void *context) {
    failing_store_context *state = (failing_store_context *)context;
    return tds_merkle_block_store_remove(state->inner, digest, removed);
}

static tds_merkle_status failing_store_clear(void *context) {
    failing_store_context *state = (failing_store_context *)context;
    return tds_merkle_block_store_clear(state->inner);
}

static tds_merkle_status malicious_store_found_without_block(
    tds_merkle_digest digest,
    bool *found,
    tds_merkle_block *block,
    void *context) {
    (void)digest;
    (void)block;
    (void)context;
    *found = true;
    return TDS_MERKLE_OK;
}

static tds_merkle_status malicious_store_ok_conflict(
    const tds_merkle_block *block,
    tds_merkle_store_put_result *result,
    void *context) {
    (void)block;
    (void)context;
    *result = TDS_MERKLE_STORE_CONFLICT;
    return TDS_MERKLE_OK;
}

static tds_merkle_block_store make_failing_store(failing_store_context *context) {
    tds_merkle_block_store result = {
        failing_store_count,
        failing_store_contains,
        failing_store_try_get,
        failing_store_put,
        failing_store_remove,
        failing_store_clear,
        context};
    return result;
}

static tds_merkle_status fail_merge_resolver(
    tds_merkle_three_way_merge_conflict_ref conflict,
    tds_merkle_merge_resolution *resolution,
    void *context) {
    (void)conflict;
    (void)resolution;
    (void)context;
    return TDS_MERKLE_CALLBACK_FAILURE;
}

static bool test_persistence_allocation_failure_sweeps(void) {
    counting_allocator allocator = {0};
    tds_merkle_policy_config config;
    tds_merkle_policy policy = {0};
    tds_merkle_search_tree tree = {0};
    tds_merkle_search_tree local = {0};
    tds_merkle_search_tree left = {0};
    tds_merkle_search_tree right = {0};
    tds_merkle_memory_block_store source_memory = {0};
    tds_merkle_memory_block_store destination_memory = {0};
    tds_merkle_memory_block_store empty_memory = {0};
    tds_merkle_block_store source_store;
    tds_merkle_block_store destination_store;
    tds_merkle_block_store empty_store;
    tds_merkle_block_pack pack = {0};
    tds_merkle_proof verification_proof = {0};
    tds_merkle_verification_error error;
    size_t added;
    size_t failure_index;
    size_t baseline_live;
    bool saw_success;
    int32_t key = 7;
    int32_t minimum = 2;
    int32_t maximum = 18;
    int32_t left_value = 700;
    int32_t right_value = 701;
    configure_i32_policy(&config, "c-persistence-failpoint-i32-v1");
    config.allocator.allocate = counting_allocate;
    config.allocator.deallocate = counting_deallocate;
    config.allocator.context = &allocator;
    CHECK_STATUS(tds_merkle_policy_create(&config, &policy));
    CHECK_STATUS(make_i32_sequence_tree(&policy, 24, &tree));
    CHECK_STATUS(tds_merkle_search_tree_init(&local, &policy));
    CHECK_STATUS(tds_merkle_search_tree_copy(&tree, &left));
    CHECK_STATUS(tds_merkle_search_tree_copy(&tree, &right));
    CHECK_STATUS(tds_merkle_search_tree_set(&left, &key, &left_value, &left));
    CHECK_STATUS(tds_merkle_search_tree_set(&right, &key, &right_value, &right));
    CHECK_STATUS(tds_merkle_memory_block_store_init(&source_memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_init(&destination_memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_init(&empty_memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(&source_memory, &source_store));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(
        &destination_memory,
        &destination_store));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(&empty_memory, &empty_store));
    CHECK_STATUS(tds_merkle_search_tree_save(&tree, &source_store, &added, &error));
    CHECK_STATUS(tds_merkle_search_tree_export_pack(&tree, &pack));
    CHECK_STATUS(tds_merkle_search_tree_create_proof(
        &tree,
        &key,
        &verification_proof));
    baseline_live = allocator.live;

    saw_success = false;
    for (failure_index = 1; failure_index != 2048; ++failure_index) {
        tds_merkle_block_pack result = {0};
        tds_merkle_status status;
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_export_pack(&tree, &result);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            tds_merkle_block_pack_dispose(&result);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY && result.rep == NULL &&
            allocator.live == baseline_live);
    }
    CHECK(saw_success);

    saw_success = false;
    for (failure_index = 1; failure_index != 2048; ++failure_index) {
        tds_merkle_proof_verification_result result = {
            true,
            TDS_MERKLE_VERIFY_NONE,
            true,
            {{0}},
            777,
            888};
        tds_merkle_status status;
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_verify_proof(
            &verification_proof,
            &policy,
            NULL,
            &result);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            CHECK(result.is_valid && allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY && result.is_valid &&
            result.verified_block_count == 777 &&
            result.verified_byte_count == 888 &&
            allocator.live == baseline_live);
    }
    CHECK(saw_success);

    saw_success = false;
    for (failure_index = 1; failure_index != 2048; ++failure_index) {
        tds_merkle_proof result = {0};
        tds_merkle_status status;
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_create_proof(&tree, &key, &result);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            tds_merkle_proof_dispose(&result);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY && result.rep == NULL &&
            allocator.live == baseline_live);
    }
    CHECK(saw_success);

    saw_success = false;
    for (failure_index = 1; failure_index != 4096; ++failure_index) {
        tds_merkle_proof result = {0};
        tds_merkle_status status;
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_create_range_proof(
            &tree,
            &minimum,
            &maximum,
            &result);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            tds_merkle_proof_dispose(&result);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY && result.rep == NULL &&
            allocator.live == baseline_live);
    }
    CHECK(saw_success);

    saw_success = false;
    for (failure_index = 1; failure_index != 4096; ++failure_index) {
        tds_merkle_search_tree result = {0};
        tds_merkle_status status;
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_load(
            tds_merkle_search_tree_root_hash(&tree),
            &policy,
            &source_store,
            NULL,
            &result,
            &error);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            tds_merkle_search_tree_dispose(&result);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY && result.policy == NULL &&
            allocator.live == baseline_live);
    }
    CHECK(saw_success);

    saw_success = false;
    for (failure_index = 1; failure_index != 4096; ++failure_index) {
        tds_merkle_search_tree result = {0};
        tds_merkle_status status;
        size_t destination_count = SIZE_MAX;
        CHECK_STATUS(tds_merkle_block_store_clear(&destination_store));
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_import_pack(
            &pack,
            &policy,
            &destination_store,
            NULL,
            &result,
            &error);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            tds_merkle_search_tree_dispose(&result);
            CHECK_STATUS(tds_merkle_block_store_clear(&destination_store));
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY && result.policy == NULL);
        CHECK_STATUS(tds_merkle_block_store_count(
            &destination_store,
            &destination_count));
        CHECK(destination_count == 0 && allocator.live == baseline_live);
    }
    CHECK(saw_success);

    saw_success = false;
    for (failure_index = 1; failure_index != 2048; ++failure_index) {
        tds_merkle_block_pack result = {0};
        tds_merkle_status status;
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_create_sync_pack(
            &tree,
            &empty_store,
            &result);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            tds_merkle_block_pack_dispose(&result);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY && result.rep == NULL &&
            allocator.live == baseline_live);
    }
    CHECK(saw_success);

    saw_success = false;
    for (failure_index = 1; failure_index != 2048; ++failure_index) {
        tds_merkle_sync_plan result = {0};
        tds_merkle_status status;
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_plan_sync(
            &tree,
            &local,
            &empty_store,
            &result);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            tds_merkle_sync_plan_dispose(&result);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY && result.rep == NULL &&
            allocator.live == baseline_live);
    }
    CHECK(saw_success);

    saw_success = false;
    for (failure_index = 1; failure_index != 4096; ++failure_index) {
        tds_merkle_three_way_merge_result result = {0};
        tds_merkle_status status;
        allocator.attempts = 0;
        allocator.fail_at = failure_index;
        status = tds_merkle_search_tree_merge(
            &tree,
            &left,
            &right,
            NULL,
            NULL,
            &result);
        allocator.fail_at = 0;
        if (status == TDS_MERKLE_OK) {
            CHECK(!tds_merkle_three_way_merge_result_success(&result));
            tds_merkle_three_way_merge_result_dispose(&result);
            CHECK(allocator.live == baseline_live);
            saw_success = true;
            break;
        }
        CHECK(status == TDS_MERKLE_NO_MEMORY && result.rep == NULL &&
            allocator.live == baseline_live);
    }
    CHECK(saw_success);

    tds_merkle_proof_dispose(&verification_proof);
    tds_merkle_block_pack_dispose(&pack);
    tds_merkle_memory_block_store_dispose(&empty_memory);
    tds_merkle_memory_block_store_dispose(&destination_memory);
    tds_merkle_memory_block_store_dispose(&source_memory);
    tds_merkle_search_tree_dispose(&right);
    tds_merkle_search_tree_dispose(&left);
    tds_merkle_search_tree_dispose(&local);
    tds_merkle_search_tree_dispose(&tree);
    tds_merkle_policy_dispose(&policy);
    CHECK(allocator.live == 0);
    return true;
}

static bool test_persistence_callback_failures_leave_no_result(void) {
    tds_merkle_policy source_policy = {0};
    tds_merkle_policy bomb_policy = {0};
    tds_merkle_policy compare_policy = {0};
    tds_merkle_policy_config config;
    tds_merkle_search_tree source = {0};
    tds_merkle_search_tree base = {0};
    tds_merkle_search_tree left = {0};
    tds_merkle_search_tree right = {0};
    tds_merkle_memory_block_store memory = {0};
    tds_merkle_memory_block_store failed_destination_memory = {0};
    tds_merkle_block_store store;
    tds_merkle_block_store failed_destination_store;
    tds_merkle_block_store failing_store;
    failing_store_context store_state;
    bomb_codec_state key_codec;
    bomb_codec_state value_codec;
    failing_compare_state compare_state = {false};
    tds_merkle_proof proof = {0};
    tds_merkle_verification_error error;
    size_t added;
    int32_t conflict_key = 3;
    int32_t left_value = 301;
    int32_t right_value = 302;
    CHECK_STATUS(make_i32_policy("c-persistence-callback-i32-v1", &source_policy));
    CHECK_STATUS(make_i32_sequence_tree(&source_policy, 24, &source));
    CHECK_STATUS(tds_merkle_memory_block_store_init(&memory, NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_init(
        &failed_destination_memory,
        NULL));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(&memory, &store));
    CHECK_STATUS(tds_merkle_memory_block_store_as_store(
        &failed_destination_memory,
        &failed_destination_store));
    CHECK_STATUS(tds_merkle_search_tree_save(&source, &store, &added, &error));
    CHECK_STATUS(tds_merkle_search_tree_create_proof(
        &source,
        &conflict_key,
        &proof));

    configure_i32_policy(&config, "c-persistence-callback-i32-v1");
    install_bomb_codec(&config.key_codec, &key_codec);
    install_bomb_codec(&config.value_codec, &value_codec);
    CHECK_STATUS(tds_merkle_policy_create(&config, &bomb_policy));
    key_codec.fail_decode = true;
    {
        tds_merkle_proof_verification_result result = {
            true,
            TDS_MERKLE_VERIFY_NONE,
            true,
            {{0}},
            91,
            92};
        CHECK(tds_merkle_search_tree_verify_proof(
            &proof,
            &bomb_policy,
            NULL,
            &result) == TDS_MERKLE_CALLBACK_FAILURE);
        CHECK(result.is_valid && result.verified_block_count == 91 &&
            result.verified_byte_count == 92);
    }
    {
        tds_merkle_search_tree result = {0};
        CHECK(tds_merkle_search_tree_load(
            tds_merkle_search_tree_root_hash(&source),
            &bomb_policy,
            &store,
            NULL,
            &result,
            &error) == TDS_MERKLE_CALLBACK_FAILURE);
        CHECK(result.policy == NULL);
    }
    key_codec.fail_decode = false;
    key_codec.fail_encode = true;
    {
        tds_merkle_search_tree result = {0};
        CHECK(tds_merkle_search_tree_load(
            tds_merkle_search_tree_root_hash(&source),
            &bomb_policy,
            &store,
            NULL,
            &result,
            &error) == TDS_MERKLE_CALLBACK_FAILURE);
        CHECK(result.policy == NULL);
    }
    key_codec.fail_encode = false;

    configure_i32_policy(&config, "c-persistence-callback-i32-v1");
    config.key_compare = failing_compare;
    config.key_compare_context = &compare_state;
    CHECK_STATUS(tds_merkle_policy_create(&config, &compare_policy));
    compare_state.fail = true;
    {
        tds_merkle_search_tree result = {0};
        CHECK(tds_merkle_search_tree_load(
            tds_merkle_search_tree_root_hash(&source),
            &compare_policy,
            &store,
            NULL,
            &result,
            &error) == TDS_MERKLE_CALLBACK_FAILURE);
        CHECK(result.policy == NULL);
    }

    memset(&store_state, 0, sizeof(store_state));
    store_state.inner = &store;
    store_state.fail_try_get_at = 1;
    failing_store = make_failing_store(&store_state);
    {
        tds_merkle_search_tree result = {0};
        CHECK(tds_merkle_search_tree_load(
            tds_merkle_search_tree_root_hash(&source),
            &source_policy,
            &failing_store,
            NULL,
            &result,
            &error) == TDS_MERKLE_CALLBACK_FAILURE);
        CHECK(result.policy == NULL && store_state.try_get_calls == 1);
    }

    memset(&store_state, 0, sizeof(store_state));
    store_state.inner = &store;
    failing_store = make_failing_store(&store_state);
    failing_store.try_get = malicious_store_found_without_block;
    {
        tds_merkle_search_tree result = {0};
        bool found = false;
        tds_merkle_block block = {0};
        CHECK(tds_merkle_block_store_try_get(
            &failing_store,
            tds_merkle_search_tree_root_hash(&source),
            &found,
            &block) == TDS_MERKLE_CALLBACK_FAILURE);
        CHECK(!found && block.rep == NULL);
        CHECK(tds_merkle_search_tree_load(
            tds_merkle_search_tree_root_hash(&source),
            &source_policy,
            &failing_store,
            NULL,
            &result,
            &error) == TDS_MERKLE_CALLBACK_FAILURE);
        CHECK(result.policy == NULL);
    }
    {
        tds_merkle_block root_block = {0};
        bool found = false;
        tds_merkle_store_put_result put_result = TDS_MERKLE_STORE_ADDED;
        CHECK_STATUS(tds_merkle_block_store_try_get(
            &store,
            tds_merkle_search_tree_root_hash(&source),
            &found,
            &root_block));
        CHECK(found);
        failing_store.put = malicious_store_ok_conflict;
        CHECK(tds_merkle_block_store_put(
            &failing_store,
            &root_block,
            &put_result) == TDS_MERKLE_CALLBACK_FAILURE);
        CHECK(put_result == TDS_MERKLE_STORE_ADDED);
        tds_merkle_block_dispose(&root_block);
    }

    CHECK_STATUS(tds_merkle_search_tree_copy(&source, &base));
    CHECK_STATUS(tds_merkle_search_tree_copy(&source, &left));
    CHECK_STATUS(tds_merkle_search_tree_copy(&source, &right));
    CHECK_STATUS(tds_merkle_search_tree_set(
        &left,
        &conflict_key,
        &left_value,
        &left));
    CHECK_STATUS(tds_merkle_search_tree_set(
        &right,
        &conflict_key,
        &right_value,
        &right));
    {
        tds_merkle_three_way_merge_result result = {0};
        CHECK(tds_merkle_search_tree_merge(
            &base,
            &left,
            &right,
            fail_merge_resolver,
            NULL,
            &result) == TDS_MERKLE_CALLBACK_FAILURE);
        CHECK(result.rep == NULL);
    }

    memset(&store_state, 0, sizeof(store_state));
    store_state.inner = &failed_destination_store;
    store_state.fail_put_at = 1;
    failing_store = make_failing_store(&store_state);
    added = SIZE_MAX;
    CHECK(tds_merkle_search_tree_save(
        &source,
        &failing_store,
        &added,
        &error) == TDS_MERKLE_CALLBACK_FAILURE);
    CHECK(added == SIZE_MAX && store_state.put_calls == 1);
    {
        size_t destination_count = SIZE_MAX;
        CHECK_STATUS(tds_merkle_block_store_count(
            &failed_destination_store,
            &destination_count));
        CHECK(destination_count == 0);
    }

    tds_merkle_proof_dispose(&proof);
    tds_merkle_search_tree_dispose(&right);
    tds_merkle_search_tree_dispose(&left);
    tds_merkle_search_tree_dispose(&base);
    tds_merkle_policy_dispose(&compare_policy);
    tds_merkle_policy_dispose(&bomb_policy);
    tds_merkle_memory_block_store_dispose(&failed_destination_memory);
    tds_merkle_memory_block_store_dispose(&memory);
    tds_merkle_search_tree_dispose(&source);
    tds_merkle_policy_dispose(&source_policy);
    return true;
}

int main(void) {
    static const test_case tests[] = {
        {"digest and built-in codecs", test_digest_and_builtin_codecs},
        {"MST2 single-entry golden wire", test_golden_single_entry_wire},
        {"policy validation and typed compatibility", test_policy_validation_and_typed_compatibility},
        {"history independence and structure", test_history_independence_and_structure},
        {"persistence range diff and sharing", test_persistence_range_diff_and_sharing},
        {"allocation failure atomicity", test_allocation_failure_atomicity},
        {"callback failure atomicity", test_callback_failure_atomicity},
        {"equivalent keys retain first representative", test_equivalent_key_representatives},
        {"streaming visitor failures", test_streaming_visitor_failures},
        {"randomized model and snapshots", test_randomized_model_and_snapshots},
        {"concurrent retained snapshot reads", test_concurrent_retained_snapshot_reads},
        {"verified persistence store and iterative sync", test_verified_persistence_store_and_iterative_sync},
        {"MSP2 proofs and budget preflight", test_msp2_proofs_and_budget_preflight},
        {"all budgets import closure and preflight", test_all_budgets_import_closure_and_preflight},
        {"MSP2 structural tamper and bomb precedence", test_msp2_structural_tamper_and_bomb_precedence},
        {"three-way merge results and policy identity", test_three_way_merge_results_and_policy_identity},
        {"merge present null is not deletion", test_merge_present_null_is_not_deletion},
        {"concurrent memory store puts and owned snapshots", test_concurrent_memory_store_puts_and_owned_snapshots},
        {"memory store never calls user code under lock", test_memory_store_never_calls_user_code_under_lock},
        {"persistence allocation failure sweeps", test_persistence_allocation_failure_sweeps},
        {"persistence callback failures leave no result", test_persistence_callback_failures_leave_no_result},
    };
    size_t index;
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    for (index = 0; index != sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(stderr, "[FAIL] %s\n", tests[index].name);
            return EXIT_FAILURE;
        }
        printf("[PASS] %s\n", tests[index].name);
    }
    printf("%zu test(s) passed\n", sizeof(tests) / sizeof(tests[0]));
    return EXIT_SUCCESS;
}
