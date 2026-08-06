(** Implementation of the law-gated persistent sequence with range updates and cached range
    measures.

    The representation is the path-copied implicit-key AVL with composable pending tags every
    sibling workspace ships. A node's own value and cached measure already reflect its pending tag;
    its children do not. A range update splits out the covered subtree, records the tag at its root
    — composing [compose newer older] over any tag already pending there — and joins the pieces
    back, so the cost is O(log n) regardless of the range's length. Structural edits push pending
    tags one level down along the descent path by path copying; reads carry inherited tags as local
    traversal state and allocate no persistent nodes. The pending marker is the node's [option]
    slot, never a sentinel tag value, so every tag the algebra can produce is storable. *)

type ('element, 'measure, 'tag) algebra = {
  id : string;
  identity : 'measure;
  combine : 'measure -> 'measure -> 'measure;
  measure_element : 'element -> 'measure;
  apply_element : 'tag -> 'element -> 'element;
  apply_measure : 'tag -> length:int -> 'measure -> 'measure;
  compose : 'tag -> 'tag -> 'tag;
}

type ('element, 'measure, 'tag) node = {
  value : 'element;
  left : ('element, 'measure, 'tag) node option;
  right : ('element, 'measure, 'tag) node option;
  height : int;
  node_count : int;
  node_measure : 'measure;
  pending : 'tag option;
}

type ('element, 'measure, 'tag) t = {
  algebra : ('element, 'measure, 'tag) algebra;
  root : ('element, 'measure, 'tag) node option;
}

type ('element, 'measure, 'tag) cursor = {
  cursor_snapshot : ('element, 'measure, 'tag) t;
  cursor_position : int;
}

let create_algebra ~id ~identity ~combine ~measure ~apply_element ~apply_measure ~compose
    ~laws_verified () =
  if String.trim id = "" then Error "range-update algebra id must not be empty"
  else if not laws_verified then Error "range-update algebra laws must be explicitly verified"
  else
    Ok { id; identity; combine; measure_element = measure; apply_element; apply_measure; compose }

let algebra_id algebra = algebra.id
let compose_tags algebra = algebra.compose
let node_height = function None -> 0 | Some node -> node.height
let node_count = function None -> 0 | Some node -> node.node_count

(* A fresh node carries no pending tag; its measure is the in-order combination of its children's
   cached measures around its own element. *)
let make algebra value left right =
  let own = algebra.measure_element value in
  let with_left =
    match left with None -> own | Some l -> algebra.combine l.node_measure own
  in
  let measure =
    match right with None -> with_left | Some r -> algebra.combine with_left r.node_measure
  in
  {
    value;
    left;
    right;
    height = 1 + Int.max (node_height left) (node_height right);
    node_count = 1 + node_count left + node_count right;
    node_measure = measure;
    pending = None;
  }

(* Records [tag] at a subtree root in O(1): the node's own value and cached measure absorb the tag
   eagerly, and the deferral for its descendants composes onto the pending slot, newer over
   older. *)
let apply_subtree algebra tag node =
  let pending =
    match node.pending with None -> tag | Some older -> algebra.compose tag older
  in
  {
    node with
    value = algebra.apply_element tag node.value;
    node_measure = algebra.apply_measure tag ~length:node.node_count node.node_measure;
    pending = Some pending;
  }

(* Pushes a node's pending tag one level down before a structural edit copies through it. The
   node's own value and measure already reflect the tag, so only the children change. *)
let push algebra node =
  match node.pending with
  | None -> node
  | Some tag ->
      {
        node with
        left = Option.map (apply_subtree algebra tag) node.left;
        right = Option.map (apply_subtree algebra tag) node.right;
        pending = None;
      }

let require_child = function
  | Some child -> child
  | None -> invalid_arg "range-update descent reached a missing child"

let rotate_left algebra node =
  let node = push algebra node in
  let pivot = push algebra (require_child node.right) in
  make algebra pivot.value (Some (make algebra node.value node.left pivot.left)) pivot.right

let rotate_right algebra node =
  let node = push algebra node in
  let pivot = push algebra (require_child node.left) in
  make algebra pivot.value pivot.left (Some (make algebra node.value pivot.right node.right))

let balance algebra node =
  let factor = node_height node.left - node_height node.right in
  if factor > 1 then
    let node =
      let left = require_child node.left in
      if node_height left.left < node_height left.right then begin
        let node = push algebra node in
        make algebra node.value (Some (rotate_left algebra (require_child node.left))) node.right
      end
      else node
    in
    rotate_right algebra node
  else if factor < -1 then
    let node =
      let right = require_child node.right in
      if node_height right.right < node_height right.left then begin
        let node = push algebra node in
        make algebra node.value node.left (Some (rotate_right algebra (require_child node.right)))
      end
      else node
    in
    rotate_left algebra node
  else node

let rec insert_node algebra index item = function
  | None -> make algebra item None None
  | Some node ->
      let node = push algebra node in
      let left_count = node_count node.left in
      if index <= left_count then
        balance algebra
          (make algebra node.value (Some (insert_node algebra index item node.left)) node.right)
      else
        balance algebra
          (make algebra node.value node.left
             (Some (insert_node algebra (index - left_count - 1) item node.right)))

let rec set_node algebra index item node =
  let node = push algebra node in
  let left_count = node_count node.left in
  if index < left_count then
    make algebra node.value (Some (set_node algebra index item (require_child node.left))) node.right
  else if index > left_count then
    make algebra node.value node.left
      (Some (set_node algebra (index - left_count - 1) item (require_child node.right)))
  else make algebra item node.left node.right

let rec extract_minimum algebra node =
  let node = push algebra node in
  match node.left with
  | None -> (node.value, node.right)
  | Some left ->
      let minimum, rest = extract_minimum algebra left in
      (minimum, Some (balance algebra (make algebra node.value rest node.right)))

let rec remove_node algebra index node =
  let node = push algebra node in
  let left_count = node_count node.left in
  if index < left_count then
    Some
      (balance algebra
         (make algebra node.value (remove_node algebra index (require_child node.left)) node.right))
  else if index > left_count then
    Some
      (balance algebra
         (make algebra node.value node.left
            (remove_node algebra (index - left_count - 1) (require_child node.right))))
  else
    match (node.left, node.right) with
    | None, other | other, None -> other
    | Some _, Some right ->
        let successor, rest = extract_minimum algebra right in
        Some (balance algebra (make algebra successor node.left rest))

let rec join algebra left pivot right =
  let left_height = node_height left in
  let right_height = node_height right in
  if left_height <= right_height + 1 && right_height <= left_height + 1 then
    make algebra pivot left right
  else if left_height > right_height then
    let node = push algebra (require_child left) in
    balance algebra (make algebra node.value node.left (Some (join algebra node.right pivot right)))
  else
    let node = push algebra (require_child right) in
    balance algebra (make algebra node.value (Some (join algebra left pivot node.left)) node.right)

let join_concatenated algebra left right =
  match (left, right) with
  | None, other | other, None -> other
  | Some _, Some right ->
      let pivot, remainder = extract_minimum algebra right in
      Some (join algebra left pivot remainder)

let rec split algebra node index =
  match node with
  | None -> (None, None)
  | Some inner ->
      if index = 0 then (None, node)
      else if index = inner.node_count then (node, None)
      else
        let inner = push algebra inner in
        let left_count = node_count inner.left in
        if index <= left_count then
          let left, middle = split algebra inner.left index in
          (left, Some (join algebra middle inner.value inner.right))
        else
          let middle, right = split algebra inner.right (index - left_count - 1) in
          (Some (join algebra inner.left inner.value middle), right)

(* The tag a child inherits during a read-only walk: ancestors' inherited tag composed over the
   node's own pending tag, newer over older. No persistent node is allocated. *)
let child_inheritance algebra node inherited =
  match (node.pending, inherited) with
  | None, inherited -> inherited
  | (Some _ as pending), None -> pending
  | Some older, Some newer -> Some (algebra.compose newer older)

let rec get_element algebra node index inherited =
  let left_count = node_count node.left in
  if index = left_count then
    match inherited with None -> node.value | Some tag -> algebra.apply_element tag node.value
  else
    let child_tag = child_inheritance algebra node inherited in
    if index < left_count then get_element algebra (require_child node.left) index child_tag
    else get_element algebra (require_child node.right) (index - left_count - 1) child_tag

(* The combined measure of [wanted] elements starting at [index] of this subtree, reading cached
   subtree measures wherever the range covers a subtree whole. Read-only: inherited tags are local
   traversal state. *)
let rec measure_subrange algebra node index wanted inherited =
  if index = 0 && wanted = node.node_count then
    match inherited with
    | None -> node.node_measure
    | Some tag -> algebra.apply_measure tag ~length:node.node_count node.node_measure
  else begin
    let left_count = node_count node.left in
    let ends = index + wanted in
    let child_tag = child_inheritance algebra node inherited in
    let total = ref None in
    let accumulate part =
      total := Some (match !total with None -> part | Some sum -> algebra.combine sum part)
    in
    if index < left_count then begin
      let covered = Int.min wanted (left_count - index) in
      accumulate (measure_subrange algebra (require_child node.left) index covered child_tag)
    end;
    if index <= left_count && ends > left_count then begin
      let own = algebra.measure_element node.value in
      accumulate
        (match inherited with
        | None -> own
        | Some tag -> algebra.apply_measure tag ~length:1 own)
    end;
    let right_start = left_count + 1 in
    if ends > right_start then begin
      let local_start = Int.max 0 (index - right_start) in
      accumulate
        (measure_subrange algebra (require_child node.right) local_start
           (ends - right_start - local_start) child_tag)
    end;
    match !total with
    | Some total -> total
    | None -> invalid_arg "a nonempty validated measure query produced no result"
  end

let rec collect algebra node inherited suffix =
  match node with
  | None -> suffix
  | Some node ->
      let child_tag = child_inheritance algebra node inherited in
      let value =
        match inherited with None -> node.value | Some tag -> algebra.apply_element tag node.value
      in
      collect algebra node.left child_tag (value :: collect algebra node.right child_tag suffix)

let empty algebra = { algebra; root = None }

let of_list algebra values =
  let values = Array.of_list values in
  let rec build start count =
    if count = 0 then None
    else
      let left_count = count / 2 in
      let middle = start + left_count in
      Some
        (make algebra values.(middle) (build start left_count)
           (build (middle + 1) (count - left_count - 1)))
  in
  { algebra; root = build 0 (Array.length values) }

let length sequence = node_count sequence.root

let nth index sequence =
  if index < 0 || index >= length sequence then None
  else Some (get_element sequence.algebra (require_child sequence.root) index None)

let to_list sequence = collect sequence.algebra sequence.root None []

let measure sequence =
  match sequence.root with None -> sequence.algebra.identity | Some root -> root.node_measure

let valid_range start count sequence = start >= 0 && count >= 0 && start <= length sequence - count

let measure_range ~start ~length:count sequence =
  if not (valid_range start count sequence) then Error "range measure is out of bounds"
  else if count = 0 then Ok sequence.algebra.identity
  else Ok (measure_subrange sequence.algebra (require_child sequence.root) start count None)

let update_range ~start ~length:count tag sequence =
  if not (valid_range start count sequence) then Error "range update is out of bounds"
  else if count = 0 then Ok sequence
  else begin
    let algebra = sequence.algebra in
    let root = require_child sequence.root in
    if count = length sequence then Ok { sequence with root = Some (apply_subtree algebra tag root) }
    else
      let left, tail = split algebra sequence.root start in
      let middle, right = split algebra tail count in
      let middle = apply_subtree algebra tag (require_child middle) in
      let rejoined = join_concatenated algebra left (Some middle) in
      Ok { sequence with root = join_concatenated algebra rejoined right }
  end

let insert index value sequence =
  if index < 0 || index > length sequence then
    Error "range sequence insertion index is out of bounds"
  else Ok { sequence with root = Some (insert_node sequence.algebra index value sequence.root) }

let remove index sequence =
  if index < 0 || index >= length sequence then
    Error "range sequence removal index is out of bounds"
  else
    let value = get_element sequence.algebra (require_child sequence.root) index None in
    Ok (value, { sequence with root = remove_node sequence.algebra index (require_child sequence.root) })

let split_at index sequence =
  let index = Int.max 0 (Int.min index (length sequence)) in
  let left, right = split sequence.algebra sequence.root index in
  ({ sequence with root = left }, { sequence with root = right })

let cursor sequence = { cursor_snapshot = sequence; cursor_position = 0 }

let cursor_at position sequence =
  if position < 0 || position > length sequence then None
  else Some { cursor_snapshot = sequence; cursor_position = position }

let cursor_position value = value.cursor_position
let cursor_length value = length value.cursor_snapshot
let cursor_is_at_start value = value.cursor_position = 0
let cursor_is_at_end value = value.cursor_position = cursor_length value

let cursor_measure_before value =
  Result.get_ok (measure_range ~start:0 ~length:value.cursor_position value.cursor_snapshot)

let cursor_measure_after value =
  Result.get_ok
    (measure_range ~start:value.cursor_position
       ~length:(cursor_length value - value.cursor_position)
       value.cursor_snapshot)

let cursor_peek_previous value = nth (value.cursor_position - 1) value.cursor_snapshot
let cursor_peek_next value = nth value.cursor_position value.cursor_snapshot
let cursor_move_previous value = cursor_at (value.cursor_position - 1) value.cursor_snapshot

let cursor_move_next value =
  if cursor_is_at_end value then None
  else cursor_at (value.cursor_position + 1) value.cursor_snapshot

let cursor_seek position value = cursor_at position value.cursor_snapshot

let cursor_insert element value =
  {
    cursor_snapshot = Result.get_ok (insert value.cursor_position element value.cursor_snapshot);
    cursor_position = value.cursor_position + 1;
  }

let cursor_delete_previous value =
  if cursor_is_at_start value then None
  else
    let _, snapshot = Result.get_ok (remove (value.cursor_position - 1) value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position - 1 }

let cursor_delete_next value =
  if cursor_is_at_end value then None
  else
    let _, snapshot = Result.get_ok (remove value.cursor_position value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position }

let cursor_replace_next element value =
  if cursor_is_at_end value then None
  else
    let snapshot = value.cursor_snapshot in
    let root =
      set_node snapshot.algebra value.cursor_position element (require_child snapshot.root)
    in
    Some
      {
        cursor_snapshot = { snapshot with root = Some root };
        cursor_position = value.cursor_position;
      }

let cursor_measure_previous count value =
  if count < 0 || count > value.cursor_position then Error "cursor previous range is out of bounds"
  else measure_range ~start:(value.cursor_position - count) ~length:count value.cursor_snapshot

let cursor_measure_next count value =
  if count < 0 || count > cursor_length value - value.cursor_position then
    Error "cursor next range is out of bounds"
  else measure_range ~start:value.cursor_position ~length:count value.cursor_snapshot

let cursor_update_previous count tag value =
  if count < 0 || count > value.cursor_position then Error "cursor previous range is out of bounds"
  else if count = 0 then Ok value
  else
    Result.map
      (fun snapshot -> { cursor_snapshot = snapshot; cursor_position = value.cursor_position })
      (update_range ~start:(value.cursor_position - count) ~length:count tag value.cursor_snapshot)

let cursor_update_next count tag value =
  if count < 0 || count > cursor_length value - value.cursor_position then
    Error "cursor next range is out of bounds"
  else if count = 0 then Ok value
  else
    Result.map
      (fun snapshot -> { cursor_snapshot = snapshot; cursor_position = value.cursor_position })
      (update_range ~start:value.cursor_position ~length:count tag value.cursor_snapshot)

let cursor_snapshot value = value.cursor_snapshot
