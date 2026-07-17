type t = { snapshot : Text_rope.t; cursor_position : int }

let create ?(position = 0) snapshot =
  if position < 0 || position > Text_rope.length snapshot then
    Error "text cursor position is out of bounds"
  else Ok { snapshot; cursor_position = position }

let text cursor = cursor.snapshot
let position cursor = cursor.cursor_position

let line_column cursor =
  Result.get_ok (Text_rope.line_column cursor.cursor_position cursor.snapshot)

let move_to cursor_position cursor =
  if cursor_position < 0 || cursor_position > Text_rope.length cursor.snapshot then
    Error "text cursor position is out of bounds"
  else Ok { cursor with cursor_position }

let move_to_line_column ~line ~column cursor =
  Result.bind (Text_rope.index_of_line_column ~line ~column cursor.snapshot) (fun cursor_position ->
      Ok { cursor with cursor_position })

let peek_before cursor = Text_rope.nth (cursor.cursor_position - 1) cursor.snapshot
let peek_after cursor = Text_rope.nth cursor.cursor_position cursor.snapshot

let insert_utf8 insertion cursor =
  Result.bind (Text_rope.of_utf8 insertion) (fun inserted ->
      Result.map
        (fun snapshot ->
          { snapshot; cursor_position = cursor.cursor_position + Text_rope.length inserted })
        (Text_rope.insert_utf8 cursor.cursor_position insertion cursor.snapshot))

let delete_before count cursor =
  if count < 0 || count > cursor.cursor_position then
    Error "text cursor backward deletion is out of bounds"
  else
    let cursor_position = cursor.cursor_position - count in
    Result.map
      (fun snapshot -> { snapshot; cursor_position })
      (Text_rope.remove ~start:cursor_position ~length:count cursor.snapshot)

let delete_after count cursor =
  if count < 0 || count > Text_rope.length cursor.snapshot - cursor.cursor_position then
    Error "text cursor forward deletion is out of bounds"
  else
    Result.map
      (fun snapshot -> { cursor with snapshot })
      (Text_rope.remove ~start:cursor.cursor_position ~length:count cursor.snapshot)

let find_subsequence needle haystack start =
  let needle_length = Array.length needle in
  let haystack_length = Array.length haystack in
  let rec matches offset index =
    index = needle_length
    || (Uchar.equal haystack.(offset + index) needle.(index) && matches offset (index + 1))
  in
  let rec search offset =
    if offset + needle_length > haystack_length then None
    else if matches offset 0 then Some offset
    else search (offset + 1)
  in
  search start

let find_forward query cursor =
  Result.bind (Text_rope.of_utf8 query) (fun needle ->
      let haystack = Array.of_list (Text_rope.to_uchars cursor.snapshot) in
      let needle = Array.of_list (Text_rope.to_uchars needle) in
      Ok (find_subsequence needle haystack cursor.cursor_position))
