(** Implementation of the persistent order-statistic sorted multiset. *)

type 'element t = { order : 'element Common.Comparator.t; values : 'element array }

let compare bag = Common.Comparator.compare bag.order
let empty order = { order; values = [||] }

let of_list order values =
  let values = Array.of_list values in
  Array.sort (Common.Comparator.compare order) values;
  { order; values }

let comparator bag = bag.order
let count bag = Array.length bag.values
let is_empty bag = count bag = 0
let minimum bag = if is_empty bag then None else Some bag.values.(0)
let maximum bag = if is_empty bag then None else Some bag.values.(count bag - 1)
let nth index bag = if index < 0 || index >= count bag then None else Some bag.values.(index)
let count_less_than value bag = Sorted_helpers.lower_bound (compare bag) value bag.values
let count_at_most value bag = Sorted_helpers.upper_bound (compare bag) value bag.values
let count_of value bag = count_at_most value bag - count_less_than value bag
let mem value bag = count_of value bag <> 0

let add value bag =
  { bag with values = Sorted_helpers.insert (count_at_most value bag) value bag.values }

let remove value bag =
  let index = count_less_than value bag in
  if index = count bag || compare bag bag.values.(index) value <> 0 then bag
  else { bag with values = Sorted_helpers.remove index bag.values }

let remove_at index bag =
  if index < 0 || index >= count bag then Error "sorted-bag index is out of bounds"
  else Ok { bag with values = Sorted_helpers.remove index bag.values }

let remove_all value bag =
  let first = count_less_than value bag in
  let last = count_at_most value bag in
  if first = last then bag
  else
    {
      bag with
      values =
        Array.append
          (Sorted_helpers.slice 0 first bag.values)
          (Sorted_helpers.slice last (count bag - last) bag.values);
    }

let range ~start ~count:range_count bag =
  if start < 0 || range_count < 0 || start > count bag - range_count then
    Error "sorted-bag range is out of bounds"
  else Ok { bag with values = Sorted_helpers.slice start range_count bag.values }

let value_range ~minimum:low ~maximum:high bag =
  if compare bag low high > 0 then empty bag.order
  else
    let first = count_less_than low bag in
    let last = count_at_most high bag in
    { bag with values = Sorted_helpers.slice first (last - first) bag.values }

let to_list bag = Array.to_list bag.values
