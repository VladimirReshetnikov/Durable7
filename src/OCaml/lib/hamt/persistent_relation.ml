type ('left, 'right) entry = { left : 'left; right : 'right }

type ('left, 'right) t = {
  forward : ('left, 'right) Persistent_hash_multimap.t;
  reverse : ('right, 'left) Persistent_hash_multimap.t;
}

let empty ~left_policy ~right_policy =
  {
    forward = Persistent_hash_multimap.empty ~key_policy:left_policy ~value_policy:right_policy;
    reverse = Persistent_hash_multimap.empty ~key_policy:right_policy ~value_policy:left_policy;
  }

let pair_count relation = Persistent_hash_multimap.pair_count relation.forward
let left_count relation = Persistent_hash_multimap.key_count relation.forward
let right_count relation = Persistent_hash_multimap.key_count relation.reverse
let contains left right relation = Persistent_hash_multimap.contains left right relation.forward
let rights left relation = Persistent_hash_multimap.values left relation.forward
let lefts right relation = Persistent_hash_multimap.values right relation.reverse

let add left right relation =
  let stored_left =
    Option.value ~default:left (Persistent_hash_multimap.stored_key_opt left relation.forward)
  in
  let stored_right =
    Option.value ~default:right (Persistent_hash_multimap.stored_key_opt right relation.reverse)
  in
  if contains stored_left stored_right relation then relation
  else
    {
      forward = Persistent_hash_multimap.add stored_left stored_right relation.forward;
      reverse = Persistent_hash_multimap.add stored_right stored_left relation.reverse;
    }

let remove left right relation =
  if not (contains left right relation) then relation
  else
    {
      forward = Persistent_hash_multimap.remove left right relation.forward;
      reverse = Persistent_hash_multimap.remove right left relation.reverse;
    }

let remove_left left relation =
  let values = Persistent_hash_set.to_list (rights left relation) in
  if values = [] then relation
  else
    let reverse =
      List.fold_left
        (fun current right -> Persistent_hash_multimap.remove right left current)
        relation.reverse values
    in
    { forward = Persistent_hash_multimap.remove_key left relation.forward; reverse }

let remove_right right relation =
  let values = Persistent_hash_set.to_list (lefts right relation) in
  if values = [] then relation
  else
    let forward =
      List.fold_left
        (fun current left -> Persistent_hash_multimap.remove left right current)
        relation.forward values
    in
    { forward; reverse = Persistent_hash_multimap.remove_key right relation.reverse }

let inverse relation = { forward = relation.reverse; reverse = relation.forward }

let to_list relation =
  List.map
    (fun entry ->
      { left = entry.Persistent_hash_multimap.key; right = entry.Persistent_hash_multimap.value })
    (Persistent_hash_multimap.to_list relation.forward)
