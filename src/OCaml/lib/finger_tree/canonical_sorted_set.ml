type rank = { geometric : int; secondary : int64; content : int64 }

type 'element rank_policy = {
  order : 'element Common.Comparator.t;
  rank_hash : 'element -> int64;
  rank_key : string;
}

type 'element t = { policy : 'element rank_policy; set : 'element Sorted_set.t }
type 'element lookup = Added of 'element t | Existing of 'element
type statistics = { count : int; canonical_height : int; structure_digest : int64 }

let int64_bytes value =
  Bytes.init 8 (fun index ->
      Char.chr
        (Int64.to_int (Int64.logand 0xffL (Int64.shift_right_logical value ((7 - index) * 8)))))

let first_int64 digest offset =
  let raw = Digestif.SHA256.to_raw_string digest in
  let result = ref 0L in
  for index = offset to offset + 7 do
    result := Int64.logor (Int64.shift_left !result 8) (Int64.of_int (Char.code raw.[index]))
  done;
  !result

let digest_rank tag payload =
  let framed =
    Bytes.concat Bytes.empty
      [ Bytes.of_string tag; int64_bytes (Int64.of_int (Bytes.length payload)); payload ]
  in
  first_int64 (Digestif.SHA256.digest_bytes framed) 0

let stable_rank_hash_bytes = digest_rank "bytes"
let stable_rank_hash_string value = digest_rank "str" (Bytes.of_string value)

let create_policy ~comparator ~rank_hash ~seed =
  let rank_key =
    Digestif.SHA256.to_raw_string
      (Digestif.SHA256.digest_bytes
         (Bytes.concat Bytes.empty [ Bytes.of_string "ZZT2"; int64_bytes seed ]))
  in
  { order = comparator; rank_hash; rank_key }

let comparator policy = policy.order

let leading_zero_bits value =
  let rec loop bit count =
    if bit < 0 then count
    else if Int64.logand value (Int64.shift_left 1L bit) <> 0L then count
    else loop (bit - 1) (count + 1)
  in
  loop 63 0

let rank policy value =
  let digest =
    Digestif.SHA256.hmac_bytes ~key:policy.rank_key (int64_bytes (policy.rank_hash value))
  in
  let primary = first_int64 digest 0 in
  {
    geometric = leading_zero_bits primary;
    secondary = first_int64 digest 8;
    content = first_int64 digest 16;
  }

let empty policy = { policy; set = Sorted_set.empty policy.order }
let count value = Sorted_set.count value.set
let is_empty value = Sorted_set.is_empty value.set
let mem element value = Sorted_set.mem element value.set
let find element value = Sorted_set.find element value.set

let add element value =
  let added, set = Sorted_set.add element value.set in
  if added then Added { value with set }
  else Existing (Option.get (Sorted_set.find element value.set))

let remove element value =
  let removed, set = Sorted_set.remove element value.set in
  (removed, { value with set })

let of_list policy values =
  List.fold_left
    (fun result value ->
      match add value result with Added successor -> successor | Existing _ -> result)
    (empty policy) values

let minimum value = Sorted_set.minimum value.set
let maximum value = Sorted_set.maximum value.set
let nth index value = Sorted_set.nth index value.set
let to_list value = Sorted_set.to_list value.set

let higher policy left_item left_rank right_item right_rank =
  if left_rank.geometric <> right_rank.geometric then left_rank.geometric > right_rank.geometric
  else
    let secondary = Int64.unsigned_compare left_rank.secondary right_rank.secondary in
    secondary > 0
    || (secondary = 0 && Common.Comparator.compare policy.order left_item right_item < 0)

let statistics value =
  let ranked =
    Array.of_list (List.map (fun item -> (item, rank value.policy item)) (to_list value))
  in
  let rec height start finish =
    if start >= finish then 0
    else
      let root = ref start in
      for index = start + 1 to finish - 1 do
        let candidate_item, candidate_rank = ranked.(index) in
        let root_item, root_rank = ranked.(!root) in
        if higher value.policy candidate_item candidate_rank root_item root_rank then root := index
      done;
      1 + Int.max (height start !root) (height (!root + 1) finish)
  in
  let structure_digest =
    Array.fold_left
      (fun digest (_, item_rank) ->
        Int64.logxor item_rank.content
          (Int64.add 0x9e3779b97f4a7c15L
             (Int64.add (Int64.shift_left digest 6) (Int64.shift_right_logical digest 2))))
      0x243f6a8885a308d3L ranked
  in
  {
    count = Array.length ranked;
    canonical_height = height 0 (Array.length ranked);
    structure_digest;
  }
