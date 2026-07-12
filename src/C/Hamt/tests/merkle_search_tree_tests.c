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
    };
    size_t index;
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
