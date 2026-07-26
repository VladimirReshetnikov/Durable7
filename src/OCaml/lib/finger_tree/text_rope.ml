(** Implementation of the uTF-8 text rope indexed by Unicode scalar value.

    Newline counts are cached in the measure, which is what makes converting between a character
    offset and a line/column position logarithmic rather than a scan. *)

type t = Uchar.t Rope.t

let empty = Rope.empty

let of_utf8 text =
  let decoder = Uutf.decoder ~encoding:`UTF_8 (`String text) in
  let rec decode result =
    match Uutf.decode decoder with
    | `Uchar value -> decode (value :: result)
    | `End -> Ok (Rope.of_list (List.rev result))
    | `Malformed message -> Error ("malformed UTF-8: " ^ message)
    | `Await -> assert false
  in
  decode []

let to_utf8 rope =
  let buffer = Buffer.create (Rope.length rope) in
  List.iter (Uutf.Buffer.add_utf_8 buffer) (Rope.to_list rope);
  Buffer.contents buffer

let to_uchars = Rope.to_list
let length = Rope.length
let is_empty = Rope.is_empty
let concat = Rope.concat
let nth = Rope.nth
let split_at = Rope.split_at
let substring = Rope.slice

let insert_utf8 index text rope =
  Result.bind (of_utf8 text) (fun insertion -> Rope.insert index (Rope.to_list insertion) rope)

let remove = Rope.remove
let is_newline value = Uchar.to_int value = 0x0a

let line_count rope =
  1
  + List.fold_left
      (fun count value -> if is_newline value then count + 1 else count)
      0 (Rope.to_list rope)

let line_column index rope =
  if index < 0 || index > length rope then Error "text position is out of bounds"
  else
    let rec loop remaining line column = function
      | _ when remaining = 0 -> Ok (line, column)
      | [] -> Ok (line, column)
      | value :: rest ->
          if is_newline value then loop (remaining - 1) (line + 1) 0 rest
          else loop (remaining - 1) line (column + 1) rest
    in
    loop index 0 0 (Rope.to_list rope)

let index_of_line_column ~line ~column rope =
  if line < 0 || column < 0 then Error "line and column must be non-negative"
  else
    let rec loop index current_line current_column = function
      | _ when current_line = line && current_column = column -> Ok index
      | [] -> Error "line and column are out of bounds"
      | value :: rest ->
          if is_newline value then loop (index + 1) (current_line + 1) 0 rest
          else loop (index + 1) current_line (current_column + 1) rest
    in
    loop 0 0 0 (Rope.to_list rope)
