# C++ Merkle Search Tree

- Status: Current core and wire specification
- Created (UTC): 2026-07-12T13:20:54Z
- Repository HEAD: d5a6728ace07b429e969c68494ba6f48cb4c3cde
- Audience: C++ consumers, maintainers, and cross-language port reviewers
- Scope: `mst-sha256-b16-v2` policy, canonical codecs and topology, exact `MST2` blocks, and immutable core APIs

The C++20 Merkle search tree is an immutable ordered content-addressed map. It is the header-first
C++ port of the C# reference core and implements the same `mst-sha256-b16-v2` hashing and `MST2`
block contract. Include
[`merkle_search_tree.hpp`](../include/durable7/hamt/merkle_search_tree.hpp); it includes
the public policy and codec layer from
[`merkle_encoding.hpp`](../include/durable7/hamt/merkle_encoding.hpp).

This document covers the in-memory core and exact wire output. The completed persistence, proof,
synchronization, and merge layer is specified separately in
[Merkle persistence](merkle-persistence.md); it builds on these stable policy and block contracts.

## Deterministic Policy And Hash Domain

Every `merkle_search_tree<K,V>` retains an explicit `merkle_search_tree_policy<K,V>`. A policy
combines:

1. the algorithm identifier `mst-sha256-b16-v2`;
2. a caller-supplied semantic policy identifier;
3. a `merkle_key_comparer<K>` whose zero result defines key equivalence;
4. an injective, explicitly versioned `merkle_codec<K>`; and
5. an injective, explicitly versioned `merkle_codec<V>`.

`merkle_search_tree_policy::create` takes shared comparer and codec objects.
`merkle_search_tree_policy::natural` constructs the comparer from a C++ strict weak ordering. The
policy state owns those shared pointers. The comparator and codecs may therefore carry state, but
their behavior must remain deterministic and immutable for the lifetime of every derived tree.
Equivalent keys must have one canonical key encoding; the key codec must not give the same bytes to
non-equivalent keys.

A policy ID must be nonempty canonical UTF-8 and not consist only of Unicode `White_Space` code
points. Codec IDs must be canonical UTF-8, have no Unicode `White_Space` code point at either edge,
and end in `-v` followed by one or more ASCII decimal digits. The domain digest is SHA-256 over this
exact byte sequence, where each length is a signed non-negative 32-bit big-endian integer:

```text
50
  int32be(length(utf8("mst-sha256-b16-v2"))) utf8("mst-sha256-b16-v2")
  int32be(length(utf8(policyId)))              utf8(policyId)
  int32be(length(utf8(keyCodec.encodingId)))  utf8(keyCodec.encodingId)
  int32be(length(utf8(valueCodec.encodingId))) utf8(valueCodec.encodingId)
```

`50` is the one-byte policy tag `0x50`. A canonical encoded key is bound to that domain by hashing:

```text
4b int32be(32) domainDigest int32be(length(keyBytes)) keyBytes
```

The key's level is the number of leading zero base-16 digits in this digest, from 0 through 64.
That B=16 geometric assignment is deterministic, policy-separated, and independent of update
history. `policy.domain_digest()`, `empty_digest()`, `hash_key()`, `hash_key_bytes()`, and the static
`level()` member expose these values for diagnostics and cross-language golden tests.

Policies are compatible when their domain digests match. `shares_identity_with` is stronger: it
requires both policy values to retain the same shared policy state. A matching digest is the wire
compatibility boundary; it cannot prove that two independently supplied C++ comparator or codec
objects actually obey the same semantics, so callers must assign policy IDs honestly.

## Canonical Codecs And Digests

`merkle_codec<T>` returns newly owned canonical bytes from `encode` and decodes exactly one complete
canonical representation. `decode` throws `merkle_codec_error` for malformed, trailing, or
noncanonical input. The built-in codecs are:

| Codec | Encoding ID | Canonical representation |
| --- | --- | --- |
| `int32_merkle_codec` | `i32-be-v1` | Exactly four signed two's-complement bytes, big-endian |
| `int64_merkle_codec` | `i64-be-v1` | Exactly eight signed two's-complement bytes, big-endian |
| `nullable_utf8_merkle_codec` | `nullable-utf8-v1` | `00` for null; otherwise `01` plus shortest-form scalar UTF-8 |
| `nullable_bytes_merkle_codec` | `nullable-bytes-v1` | `00` for null; otherwise `01` plus the exact byte vector |
| `rfc4122_guid_merkle_codec` | `guid-rfc4122-v1` | Exactly 16 `rfc4122_guid` bytes in RFC-4122/network order |

Nullable decoders reject a missing or unknown tag and reject trailing bytes after the null tag. A
present empty string/vector is `01`, distinct from null `00`. UTF-8 validation rejects overlong
forms, surrogate code points, truncated sequences, stray continuation bytes, and values above
U+10FFFF. Fixed-width decoders reject every other input length.

`merkle_digest` is a value-type 32-byte SHA-256 address. `from_bytes` and `from_hex` throw on an
invalid exact length or malformed hex; their `try_` variants return `std::nullopt`. Hex formatting
is lowercase, ordering is unsigned lexicographic byte order, and `write_bytes`/
`try_write_bytes` verify the complete 32-byte capacity before changing the destination. SHA-256
uses Windows CNG on Windows and OpenSSL Crypto on other platforms. MSVC consumers receive a
`bcrypt.lib` auto-link directive from the header; MinGW consumers must link `-lbcrypt`, and
non-Windows consumers must link `-lcrypto`.

## Canonical B=16 Wide Tree

Within any comparator interval, entries at that interval's maximum level become the ordered
separators in one wide block. The intervals before, between, and after those separators recursively
contain only lower-level child blocks. Multiple consecutive keys at the same level therefore share
one block instead of forming a binary chain. Bulk construction, incremental insertion, replacement,
deletion, and empty-shell contraction all converge on this same geometry.

`create_range` accepts a vector by value and stable-sorts it under the policy comparator. For an
equivalent-key run it retains the first key object and the last supplied value. `set_item` preserves
the already stored key representative. A replacement whose canonical value bytes equal the stored
bytes, removal of an absent key, and clearing an empty tree all return a root-sharing value. Real
updates copy only affected block paths and retain every untouched subtree through
`std::shared_ptr<const node>`.

Keys and values are moved into independently retained `shared_ptr<const T>` representatives, so
move-only types are supported. `merkle_search_tree_entry<K,V>` exposes const references, owning
`key_handle()` and `value_handle()` pointers, immutable encoded-byte snapshots, and the derived
level. Copying an entry retains its representative after the source tree is destroyed. Iterators
and raw pointers from `try_get`, `try_get_key`, and `get_entry` do not own nodes; a tree snapshot
retaining the containing node must remain alive while they are used.

The read/update surface consists of:

- `create`, `create_range`, `set_item`, `remove`, and `clear`;
- `get_entry`, `try_get`, `try_get_key`, `contains_key`, and throwing `at`;
- `size`/`count`, `empty`, `height`, `block_count`, `root_hash`, and `policy`;
- forward in-order iteration and materialized inclusive `enumerate_range`;
- `content_equals`, `map_equals`, and ordered typed `diff`;
- `shares_root_with`, `shares_policy_with`, and `shared_block_count`; and
- `shape`, `blocks_preorder`, and `validate_structure`.

`content_equals` compares the policy domain and root digest in O(1), using SHA-256 collision
resistance as the content-addressing assumption. `map_equals` requires compatible domains and
checks semantic key/value equality through the policy comparator and a supplied value relation.
`diff` also requires compatible domains, prunes shared pointers and equal block digests, and returns
`added`, `removed`, and `changed` records in key order. Its records own shared key/old-value/new-value
handles, so nullable and move-only values remain unambiguous. An incompatible diff throws
`merkle_policy_mismatch`; an inverted inclusive range throws `merkle_range_error`.

## Exact `MST2` Blocks

The empty-tree address is SHA-256 over this 37-byte manifest:

```text
"MST2" 00 domainDigest
```

Each nonempty node is addressed by SHA-256 over its complete canonical block:

```text
"MST2"
01
domainDigest[32]
level:u8
subtreeCount:int32be
entryCount:int32be
repeat entryCount times:
  keyLength:int32be keyBytes[keyLength]
  valueLength:int32be valueBytes[valueLength]
repeat entryCount + 1 times:
  childDigest[32]
```

Counts and lengths are non-negative signed 32-bit values and are rejected above `INT32_MAX`.
Entries are in comparator order. Every child interval lies between its neighboring separators and
has a strictly lower level. A missing child interval contributes the policy's empty digest.
`blocks_preorder` returns each exact immutable byte vector paired with the SHA-256 digest embedded
by its parent; no storage envelope or host-endian field is present.

The shared single-entry golden policy is `golden-int-string-v1`, with `i32-be-v1` keys and
`nullable-utf8-v1` values. Its policy-domain, empty-tree, and `{42: "forty-two"}` root digests are:

```text
fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2
98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3
1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94
```

The complete one-entry block is:

```text
4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2000000000100000001000000040000002a0000000a01666f7274792d74776f98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb398900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3
```

The native C++ tests pin every byte of these values to the C#, C, Haskell, Kotlin, and Rust vectors.

## Structural Validation

`validate_structure` walks the complete tree before returning
`merkle_search_tree_statistics`. It checks:

- nonempty blocks, levels 0 through 64, and `entries + 1` child arity;
- fresh canonical encoding of every retained key and value against its captured byte snapshot;
- each key's policy-bound level and its equality with the containing block level;
- strict separator ordering, lower child levels, and child key-interval bounds;
- cached minimum/maximum keys, subtree counts, heights, and block counts;
- exact canonical reconstruction of every `MST2` block; and
- SHA-256 of every stored block byte vector.

This catches internal construction defects and caller-visible representative drift in types whose
logically const objects can still expose mutable state. It throws `merkle_tree_invariant_error` on
the first disagreement. Successful statistics include entry count, block count, height, and the
minimum/maximum entries and encoded-byte sizes among blocks.

## Complexity, Lifetime, And Concurrency

Tree height is at most 65 because child levels strictly decrease. Lookup binary-searches the wide
separator vector at each visited block. A point update copies and re-encodes only changed blocks;
its concrete cost is proportional to their total vector and encoded-byte sizes. `create_range` is
O(n log n) for sorting plus a linear canonical build. Ordered iteration, shape inspection, exact
block enumeration, and validation are linear in represented entries/blocks and, where bytes are
checked, encoded size. Range enumeration prunes by separator intervals and is O(n) in the worst
case. Size, height, block count, policy digest, and root hash are O(1).

Published policy state, entries, blocks, and nodes are immutable. Independent tree values and
retained entry handles may be read concurrently. Updates construct new values without mutating
their sources. Ordinary C++ object-lifetime rules still apply: do not destroy or reassign a local
tree object concurrently with another thread using that same object, and make custom comparator or
codec implementations safe for the concurrent calls they permit.

## Validation Commands

From `src/Cpp/Hamt`, run the strict MSVC Debug and Release lanes:

```powershell
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests
```

The workspace validation policy also compiles and runs the public headers under strict GCC and
Clang C++20 warning gates. See [validation](validation.md) and the [test map](../tests/README.md) for
the codec, golden-wire, history-convergence, model, sharing, exception-safety, validation, and
concurrent-reader coverage.
