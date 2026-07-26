(** Implementation of the canonical codecs, SHA-256 digests, and policy framing for the MST2 wire.

    Decoding rejects noncanonical input rather than accepting it leniently, for the same reason
    encoding must be injective. *)

type digest = string

let digest_bytes digest = Bytes.of_string digest

let digest_hex digest =
  let alphabet = "0123456789abcdef" in
  String.init 64 (fun index ->
      let byte = Char.code digest.[index / 2] in
      alphabet.[if index mod 2 = 0 then byte lsr 4 else byte land 15])

let digest_of_bytes bytes =
  if Bytes.length bytes = 32 then Ok (Bytes.to_string bytes)
  else Error "a Merkle digest must contain exactly 32 bytes"

let hex_value = function
  | '0' .. '9' as value -> Some (Char.code value - Char.code '0')
  | 'a' .. 'f' as value -> Some (Char.code value - Char.code 'a' + 10)
  | 'A' .. 'F' as value -> Some (Char.code value - Char.code 'A' + 10)
  | _ -> None

let digest_of_hex text =
  if String.length text <> 64 then
    Error "a Merkle digest must contain exactly 64 hexadecimal digits"
  else
    let bytes = Bytes.create 32 in
    let rec decode index =
      if index = 32 then Ok (Bytes.to_string bytes)
      else
        match (hex_value text.[index * 2], hex_value text.[(index * 2) + 1]) with
        | Some high, Some low ->
            Bytes.set bytes index (Char.chr ((high lsl 4) lor low));
            decode (index + 1)
        | None, _ | _, None -> Error "invalid hexadecimal Merkle digest"
    in
    decode 0

let hash bytes = Digestif.SHA256.to_raw_string (Digestif.SHA256.digest_bytes bytes)
let digest_equal = String.equal
let digest_compare = String.compare

type 'value codec = {
  encoding_id : string;
  encode : 'value -> (bytes, string) result;
  decode : bytes -> ('value, string) result;
}

let encode codec value = codec.encode value
let decode codec bytes = codec.decode bytes
let encoding_id codec = codec.encoding_id

let int32_big_endian value =
  let bytes = Bytes.create 4 in
  for index = 0 to 3 do
    let shift = (3 - index) * 8 in
    Bytes.set bytes index
      (Char.chr (Int32.to_int (Int32.logand (Int32.shift_right_logical value shift) 0xffl)))
  done;
  bytes

let int64_big_endian value =
  let bytes = Bytes.create 8 in
  for index = 0 to 7 do
    let shift = (7 - index) * 8 in
    Bytes.set bytes index
      (Char.chr (Int64.to_int (Int64.logand (Int64.shift_right_logical value shift) 0xffL)))
  done;
  bytes

let read_int32_big_endian bytes offset =
  if offset < 0 || offset > Bytes.length bytes - 4 then Error "truncated signed 32-bit field"
  else
    let value = ref 0l in
    for index = 0 to 3 do
      value :=
        Int32.logor (Int32.shift_left !value 8)
          (Int32.of_int (Char.code (Bytes.get bytes (offset + index))))
    done;
    Ok !value

let int32_codec =
  {
    encoding_id = "i32-be-v1";
    encode = (fun value -> Ok (int32_big_endian value));
    decode =
      (fun bytes ->
        if Bytes.length bytes = 4 then read_int32_big_endian bytes 0
        else Error "i32-be-v1 requires exactly four bytes");
  }

let int64_codec =
  {
    encoding_id = "i64-be-v1";
    encode = (fun value -> Ok (int64_big_endian value));
    decode =
      (fun bytes ->
        if Bytes.length bytes <> 8 then Error "i64-be-v1 requires exactly eight bytes"
        else
          let value = ref 0L in
          for index = 0 to 7 do
            value :=
              Int64.logor (Int64.shift_left !value 8)
                (Int64.of_int (Char.code (Bytes.get bytes index)))
          done;
          Ok !value);
  }

let valid_utf8 text =
  Uutf.String.fold_utf_8
    (fun valid _ -> function `Uchar _ -> valid | `Malformed _ -> false)
    true text

let nullable_utf8_codec =
  {
    encoding_id = "nullable-utf8-v1";
    encode =
      (function
      | None -> Ok (Bytes.make 1 '\000')
      | Some value ->
          if not (valid_utf8 value) then Error "nullable UTF-8 value is malformed"
          else Ok (Bytes.cat (Bytes.make 1 '\001') (Bytes.of_string value)));
    decode =
      (fun bytes ->
        if Bytes.length bytes = 0 then Error "missing nullable UTF-8 tag"
        else
          match Char.code (Bytes.get bytes 0) with
          | 0 ->
              if Bytes.length bytes = 1 then Ok None
              else Error "null UTF-8 encoding has trailing bytes"
          | 1 ->
              let value = Bytes.sub_string bytes 1 (Bytes.length bytes - 1) in
              if valid_utf8 value then Ok (Some value)
              else Error "nullable UTF-8 payload is malformed"
          | _ -> Error "invalid nullable UTF-8 tag");
  }

let nullable_bytes_codec =
  {
    encoding_id = "nullable-bytes-v1";
    encode =
      (function
      | None -> Ok (Bytes.make 1 '\000')
      | Some value -> Ok (Bytes.cat (Bytes.make 1 '\001') value));
    decode =
      (fun bytes ->
        if Bytes.length bytes = 0 then Error "missing nullable byte tag"
        else
          match Char.code (Bytes.get bytes 0) with
          | 0 ->
              if Bytes.length bytes = 1 then Ok None
              else Error "null byte encoding has trailing bytes"
          | 1 -> Ok (Some (Bytes.sub bytes 1 (Bytes.length bytes - 1)))
          | _ -> Error "invalid nullable byte tag");
  }

type ('key, 'value) policy = {
  policy_id_value : string;
  compare_value : 'key -> 'key -> int;
  key_codec_value : 'key codec;
  value_codec_value : 'value codec;
  domain_digest_value : digest;
  empty_digest_value : digest;
}

let algorithm_id = "mst-sha256-b16-v2"

let framed tag fields =
  let parts =
    Bytes.make 1 (Char.chr tag)
    :: List.concat_map
         (fun field -> [ int32_big_endian (Int32.of_int (Bytes.length field)); field ])
         fields
  in
  hash (Bytes.concat Bytes.empty parts)

let valid_encoding_id identifier =
  let length = String.length identifier in
  length >= 4
  && (not (String.contains identifier ' '))
  &&
  match String.rindex_opt identifier '-' with
  | Some index when index + 2 < length && identifier.[index + 1] = 'v' ->
      let rec digits position =
        position = length
        || match identifier.[position] with '0' .. '9' -> digits (position + 1) | _ -> false
      in
      digits (index + 2)
  | Some _ | None -> false

let create_policy ~policy_id ~compare ~key_codec ~value_codec () =
  if String.trim policy_id = "" then Error "a Merkle policy ID must not be blank"
  else if not (valid_encoding_id key_codec.encoding_id && valid_encoding_id value_codec.encoding_id)
  then Error "Merkle codec IDs must be nonblank and versioned"
  else
    let strings = [ algorithm_id; policy_id; key_codec.encoding_id; value_codec.encoding_id ] in
    if not (List.for_all valid_utf8 strings) then
      Error "Merkle policy fields must be canonical UTF-8"
    else
      let domain_digest_value = framed 0x50 (List.map Bytes.of_string strings) in
      let empty_digest_value =
        hash
          (Bytes.concat Bytes.empty
             [ Bytes.of_string "MST2"; Bytes.make 1 '\000'; digest_bytes domain_digest_value ])
      in
      Ok
        {
          policy_id_value = policy_id;
          compare_value = compare;
          key_codec_value = key_codec;
          value_codec_value = value_codec;
          domain_digest_value;
          empty_digest_value;
        }

let policy_id policy = policy.policy_id_value
let compare policy = policy.compare_value
let key_codec policy = policy.key_codec_value
let value_codec policy = policy.value_codec_value
let domain_digest policy = policy.domain_digest_value
let empty_digest policy = policy.empty_digest_value
let compatible left right = digest_equal left.domain_digest_value right.domain_digest_value
let hash_key_bytes policy key = framed 0x4b [ digest_bytes policy.domain_digest_value; key ]

let level digest =
  let rec count index result =
    if index = String.length digest then result
    else
      let byte = Char.code digest.[index] in
      if byte lsr 4 <> 0 then result
      else if byte land 15 <> 0 then result + 1
      else count (index + 1) (result + 2)
  in
  count 0 0
