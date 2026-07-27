(** Canonical codecs, SHA-256 digests, and policy framing for the MST2 wire. *)

type digest
(** A 256-bit SHA-256 content digest. Two trees holding the same entries under the same policy
    produce the same digest, which is what makes the structure authenticated. *)

val digest_bytes : digest -> bytes
(** The digest's raw bytes. *)

val digest_hex : digest -> string
(** The digest as lowercase hexadecimal. *)

val digest_of_bytes : bytes -> (digest, string) result
(** A digest from its raw bytes, rejecting a wrong length. *)

val digest_of_hex : string -> (digest, string) result
(** A digest parsed from lowercase hexadecimal. *)

val hash : bytes -> digest
(** The digest of the given bytes under the policy's algorithm. *)

val digest_equal : digest -> digest -> bool
(** Whether both codecs hold the same values. *)

val digest_compare : digest -> digest -> int
(** Orders two values. *)

type 'value codec = {
  encoding_id : string;
  encode : 'value -> (bytes, string) result;
  decode : bytes -> ('value, string) result;
(** A canonical, injective encoding for a key or value type. Injectivity is essential: a second
    encoding of the same value would produce a second digest and defeat comparison by digest. *)

}

val encode : 'value codec -> 'value -> (bytes, string) result
(** The one canonical byte representation of the value. *)

val decode : 'value codec -> bytes -> ('value, string) result
(** The value these bytes encode, rejecting noncanonical input rather than accepting it
    leniently. *)

val encoding_id : 'value codec -> string
(** Stable identifier ending in [-v] followed by decimal digits, mixed into the digest domain so
    changing an encoding changes every digest derived from it. *)

val int32_codec : int32 codec
(** The canonical codec for 32-bit integers. *)

val int64_codec : int64 codec
(** The canonical codec for 64-bit integers. *)

val nullable_utf8_codec : string option codec
(** The canonical codec for optional UTF-8 text, which encodes absence distinguishably from an empty
    string. *)

val nullable_bytes_codec : bytes option codec
(** The canonical codec for optional byte strings. *)

type ('key, 'value) policy
(** The hashing and encoding policy a Merkle tree is built under. Its identity fixes every digest
    the tree produces. *)

val algorithm_id : string
(** The hash algorithm's identifier, mixed into every digest so a change of algorithm cannot be
    mistaken for a change of data. *)

val create_policy :
  policy_id:string ->
  compare:('key -> 'key -> int) ->
  key_codec:'key codec ->
  value_codec:'value codec ->
  unit ->
  (('key, 'value) policy, string) result
(** A policy built from the supplied callbacks, or an error when they are inconsistent. The
    [policy_id] and both codec identities are mixed into every digest, so two trees are combinable
    only when their policies agree; a codec change is therefore never mistaken for a data change. *)

val policy_id : ('key, 'value) policy -> string
(** The policy's identity, which decides whether two collections may be combined. *)

val compare : ('key, 'value) policy -> 'key -> 'key -> int
(** Orders two values. *)

val key_codec : ('key, 'value) policy -> 'key codec
(** The policy's key codec. *)

val value_codec : ('key, 'value) policy -> 'value codec
(** The policy's value codec. *)

val domain_digest : ('key, 'value) policy -> digest
(** The digest of the policy's domain: its algorithm and its key and value encodings together. Two
    trees whose domain digests differ describe incomparable data and must not be synchronized. *)

val empty_digest : ('key, 'value) policy -> digest
(** The digest of the empty tree under this policy. *)

val compatible : ('key, 'value) policy -> ('key, 'value) policy -> bool
(** Whether the two operands' policies allow them to be combined. *)

val hash_key_bytes : ('key, 'value) policy -> bytes -> digest
(** The digest of a key's encoded bytes. *)

val level : digest -> int
(** The level a key's hash places it at, which is what fixes the tree's shape from its contents
    alone. *)

val int32_big_endian : int32 -> bytes
(** A 32-bit integer in the canonical big-endian framing. *)

val int64_big_endian : int64 -> bytes
(** A 64-bit integer in the canonical big-endian framing. *)

val read_int32_big_endian : bytes -> int -> (int32, string) result
(** Reads a 32-bit integer from the canonical big-endian framing. *)
