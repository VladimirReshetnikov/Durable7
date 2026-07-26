(** Implementation of the persistent set over the shared HAMT map core. *)

type 'element t = ('element, unit) Persistent_hamt.map

let empty policy = Persistent_hamt.empty policy
let singleton policy element = Persistent_hamt.singleton policy element ()

let of_list policy elements =
  List.fold_left (fun set element -> Persistent_hamt.set element () set) (empty policy) elements

let policy = Persistent_hamt.policy
let count = Persistent_hamt.count
let is_empty = Persistent_hamt.is_empty
let mem = Persistent_hamt.mem

let find_opt element set =
  Option.map Persistent_hamt.entry_key (Persistent_hamt.find_entry_opt element set)

let add element set = Persistent_hamt.set element () set
let remove = Persistent_hamt.remove
let to_list set = List.map fst (Persistent_hamt.to_list set)

let union left right =
  List.fold_left (fun result element -> add element result) left (to_list right)

let inter left right =
  List.fold_left
    (fun result element -> if mem element right then result else remove element result)
    left (to_list left)

let diff left right =
  List.fold_left (fun result element -> remove element result) left (to_list right)

let symmetric_diff left right =
  List.fold_left
    (fun result element -> if mem element result then remove element result else add element result)
    left (to_list right)

let subset left right =
  count left <= count right && List.for_all (fun value -> mem value right) (to_list left)

let proper_subset left right = count left < count right && subset left right
let disjoint left right = not (List.exists (fun value -> mem value right) (to_list left))
let equal left right = count left = count right && subset left right

module Transient = struct
  type 'element session = ('element, unit) Persistent_hamt.Transient.t

  let create = Persistent_hamt.Transient.create
  let count = Persistent_hamt.Transient.count
  let mem element session = Option.is_some (Persistent_hamt.Transient.find_opt element session)

  let subset session other =
    count session <= Persistent_hamt.count other
    && Persistent_hamt.Transient.fold
         (fun result element () -> result && Persistent_hamt.mem element other)
         true session

  let proper_subset session other =
    count session < Persistent_hamt.count other && subset session other

  let superset session other =
    count session >= Persistent_hamt.count other
    && List.for_all (fun (element, ()) -> mem element session) (Persistent_hamt.to_list other)

  let proper_superset session other =
    count session > Persistent_hamt.count other && superset session other

  let overlaps session other =
    Persistent_hamt.Transient.fold
      (fun result element () -> result || Persistent_hamt.mem element other)
      false session

  let equal session other = count session = Persistent_hamt.count other && subset session other
  let add element session = Persistent_hamt.Transient.set element () session
  let remove = Persistent_hamt.Transient.remove
  let persistent = Persistent_hamt.Transient.persistent
end
