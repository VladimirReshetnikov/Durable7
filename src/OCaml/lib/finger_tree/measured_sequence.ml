(** Implementation of the public facade for general monoid-measured persistent sequences. *)

type ('element, 'measure) t = ('element, 'measure) Measured_tree.t

let empty = Measured_tree.empty
let of_list = Measured_tree.of_list
let length = Measured_tree.length
let measure = Measured_tree.measure
let to_list = Measured_tree.to_list
let cons = Measured_tree.cons
let snoc = Measured_tree.snoc
let concat = Measured_tree.concat
let nth = Measured_tree.nth
let split_at = Measured_tree.split_at
let measure_range = Measured_tree.measure_range
let locate = Measured_tree.locate
let insert_at = Measured_tree.insert_at
let remove_at = Measured_tree.remove_at
let validate = Measured_tree.validate
