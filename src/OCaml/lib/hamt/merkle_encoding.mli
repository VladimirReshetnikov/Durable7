(** Canonical codecs, SHA-256 digests, and policy framing for the MST2 wire. *)

type digest

val digest_bytes : digest -> bytes
val digest_hex : digest -> string
val digest_of_bytes : bytes -> (digest, string) result
val digest_of_hex : string -> (digest, string) result
val hash : bytes -> digest
val digest_equal : digest -> digest -> bool
val digest_compare : digest -> digest -> int

type 'value codec = {
  encoding_id : string;
  encode : 'value -> (bytes, string) result;
  decode : bytes -> ('value, string) result;
}

val encode : 'value codec -> 'value -> (bytes, string) result
val decode : 'value codec -> bytes -> ('value, string) result
val encoding_id : 'value codec -> string
val int32_codec : int32 codec
val int64_codec : int64 codec
val nullable_utf8_codec : string option codec
val nullable_bytes_codec : bytes option codec

type ('key, 'value) policy

val algorithm_id : string

val create_policy :
  policy_id:string ->
  compare:('key -> 'key -> int) ->
  key_codec:'key codec ->
  value_codec:'value codec ->
  unit ->
  (('key, 'value) policy, string) result

val policy_id : ('key, 'value) policy -> string
val compare : ('key, 'value) policy -> 'key -> 'key -> int
val key_codec : ('key, 'value) policy -> 'key codec
val value_codec : ('key, 'value) policy -> 'value codec
val domain_digest : ('key, 'value) policy -> digest
val empty_digest : ('key, 'value) policy -> digest
val compatible : ('key, 'value) policy -> ('key, 'value) policy -> bool
val hash_key_bytes : ('key, 'value) policy -> bytes -> digest
val level : digest -> int
val int32_big_endian : int32 -> bytes
val int64_big_endian : int64 -> bytes
val read_int32_big_endian : bytes -> int -> (int32, string) result
