(** Implementation of the strict persistent bidirectional map. *)

type ('key, 'value) conflict = Key_conflict | Value_conflict

type ('key, 'value) t = {
  forward : ('key, 'value) Persistent_hamt.map;
  inverse : ('value, 'key) Persistent_hamt.map;
}

let empty ~key_policy ~value_policy =
  { forward = Persistent_hamt.empty key_policy; inverse = Persistent_hamt.empty value_policy }

let count map = Persistent_hamt.count map.forward
let find_value_opt key map = Persistent_hamt.find_opt key map.forward
let find_key_opt value map = Persistent_hamt.find_opt value map.inverse

let try_add key value map =
  if Persistent_hamt.mem key map.forward then Error Key_conflict
  else if Persistent_hamt.mem value map.inverse then Error Value_conflict
  else
    Ok
      {
        forward = Persistent_hamt.add key value map.forward;
        inverse = Persistent_hamt.add value key map.inverse;
      }

let add key value map =
  match try_add key value map with
  | Ok successor -> successor
  | Error Key_conflict -> invalid_arg "key is already present"
  | Error Value_conflict -> invalid_arg "value is already present"

let replace key value map =
  match Persistent_hamt.find_entry_opt key map.forward with
  | None -> invalid_arg "key is not present"
  | Some existing
    when Common.Hash_policy.equal
           (Persistent_hamt.policy map.inverse)
           (Persistent_hamt.entry_value existing)
           value ->
      map
  | Some existing ->
      if Persistent_hamt.mem value map.inverse then invalid_arg "value is already present";
      let existing_key = Persistent_hamt.entry_key existing in
      let existing_value = Persistent_hamt.entry_value existing in
      {
        forward = Persistent_hamt.set existing_key value map.forward;
        inverse =
          map.inverse
          |> Persistent_hamt.remove existing_value
          |> Persistent_hamt.add value existing_key;
      }

let remove_key key map =
  let forward, removed = Persistent_hamt.remove_entry key map.forward in
  match removed with
  | None -> map
  | Some entry ->
      { forward; inverse = Persistent_hamt.remove (Persistent_hamt.entry_value entry) map.inverse }

let remove_value value map =
  match Persistent_hamt.find_entry_opt value map.inverse with
  | None -> map
  | Some entry -> remove_key (Persistent_hamt.entry_value entry) map

let to_list map = Persistent_hamt.to_list map.forward
let inverse_list map = Persistent_hamt.to_list map.inverse
