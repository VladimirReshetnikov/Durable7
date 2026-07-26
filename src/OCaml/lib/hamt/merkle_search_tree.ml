(** Implementation of the canonical immutable B=16 Merkle search tree with exact MST2 blocks.

    Block encoding is canonical and injective, because a second encoding of the same contents would
    produce a second digest and defeat comparison by digest. *)

type ('key, 'value) entry = { key : 'key; value : 'value }
type block = { digest : Merkle_encoding.digest; content : bytes }

let make_block digest content = { digest; content }
let block_digest block = block.digest
let block_content block = block.content
let make_entry key value = { key; value }
let entry_key entry = entry.key
let entry_value entry = entry.value

type ('key, 'value) stored_entry = {
  stored_key : 'key;
  stored_value : 'value;
  key_bytes : bytes;
  value_bytes : bytes;
  level : int;
}

type ('key, 'value) node = {
  entries : ('key, 'value) stored_entry array;
  children : ('key, 'value) node option array;
  node_count : int;
  node_height : int;
  node_block_count : int;
  node_content : bytes;
  node_digest : Merkle_encoding.digest;
}

type ('key, 'value) t = {
  tree_policy : ('key, 'value) Merkle_encoding.policy;
  root : ('key, 'value) node option;
}

let empty tree_policy = { tree_policy; root = None }
let policy tree = tree.tree_policy
let count tree = match tree.root with None -> 0 | Some root -> root.node_count
let height tree = match tree.root with None -> 0 | Some root -> root.node_height
let block_count tree = match tree.root with None -> 0 | Some root -> root.node_block_count

let root_digest tree =
  match tree.root with
  | None -> Merkle_encoding.empty_digest tree.tree_policy
  | Some root -> root.node_digest

let create_entry tree_policy key value =
  match
    ( Merkle_encoding.encode (Merkle_encoding.key_codec tree_policy) key,
      Merkle_encoding.encode (Merkle_encoding.value_codec tree_policy) value )
  with
  | Error message, _ | _, Error message -> Error message
  | Ok key_bytes, Ok value_bytes ->
      Ok
        {
          stored_key = key;
          stored_value = value;
          key_bytes;
          value_bytes;
          level = Merkle_encoding.level (Merkle_encoding.hash_key_bytes tree_policy key_bytes);
        }

let encode_node tree_policy level count entries children =
  let parts =
    ref
      [
        Bytes.of_string "MST2";
        Bytes.make 1 '\001';
        Merkle_encoding.digest_bytes (Merkle_encoding.domain_digest tree_policy);
        Bytes.make 1 (Char.chr level);
        Merkle_encoding.int32_big_endian (Int32.of_int count);
        Merkle_encoding.int32_big_endian (Int32.of_int (Array.length entries));
      ]
  in
  Array.iter
    (fun entry ->
      parts :=
        !parts
        @ [
            Merkle_encoding.int32_big_endian (Int32.of_int (Bytes.length entry.key_bytes));
            entry.key_bytes;
            Merkle_encoding.int32_big_endian (Int32.of_int (Bytes.length entry.value_bytes));
            entry.value_bytes;
          ])
    entries;
  Array.iter
    (fun child ->
      let digest =
        match child with
        | None -> Merkle_encoding.empty_digest tree_policy
        | Some node -> node.node_digest
      in
      parts := !parts @ [ Merkle_encoding.digest_bytes digest ])
    children;
  Bytes.concat Bytes.empty !parts

let new_node tree_policy level entries children =
  let node_count = ref (Array.length entries) in
  let node_height = ref 1 in
  let node_block_count = ref 1 in
  Array.iter
    (function
      | None -> ()
      | Some child ->
          node_count := !node_count + child.node_count;
          node_height := max !node_height (child.node_height + 1);
          node_block_count := !node_block_count + child.node_block_count)
    children;
  let content = encode_node tree_policy level !node_count entries children in
  {
    entries;
    children;
    node_count = !node_count;
    node_height = !node_height;
    node_block_count = !node_block_count;
    node_content = content;
    node_digest = Merkle_encoding.hash content;
  }

let rec build tree_policy entries =
  match entries with
  | [] -> None
  | _ ->
      let maximum = List.fold_left (fun result entry -> max result entry.level) 0 entries in
      let rec split segment separators children = function
        | [] ->
            let children = List.rev (build tree_policy (List.rev segment) :: children) in
            Some
              (new_node tree_policy maximum
                 (Array.of_list (List.rev separators))
                 (Array.of_list children))
        | entry :: rest when entry.level = maximum ->
            split [] (entry :: separators) (build tree_policy (List.rev segment) :: children) rest
        | entry :: rest -> split (entry :: segment) separators children rest
      in
      split [] [] [] entries

let of_list tree_policy items =
  let indexed = List.mapi (fun sequence (key, value) -> (key, value, sequence)) items in
  let sorted =
    List.stable_sort
      (fun (left, _, _) (right, _, _) -> Merkle_encoding.compare tree_policy left right)
      indexed
  in
  let rec group result = function
    | [] -> List.rev result
    | (key, value, _) :: rest ->
        let rec consume last_value = function
          | (candidate, candidate_value, _) :: tail
            when Merkle_encoding.compare tree_policy key candidate = 0 ->
              consume candidate_value tail
          | remaining -> (last_value, remaining)
        in
        let last_value, remaining = consume value rest in
        group ((key, last_value) :: result) remaining
  in
  let rec encode result = function
    | [] -> Ok (List.rev result)
    | (key, value) :: rest -> (
        match create_entry tree_policy key value with
        | Error message -> Error message
        | Ok entry -> encode (entry :: result) rest)
  in
  match encode [] (group [] sorted) with
  | Error message -> Error message
  | Ok entries -> Ok { tree_policy; root = build tree_policy entries }

let rec find_node tree_policy key = function
  | None -> None
  | Some node ->
      let rec search low high =
        if low >= high then low
        else
          let middle = (low + high) / 2 in
          if Merkle_encoding.compare tree_policy node.entries.(middle).stored_key key < 0 then
            search (middle + 1) high
          else search low middle
      in
      let position = search 0 (Array.length node.entries) in
      if
        position < Array.length node.entries
        && Merkle_encoding.compare tree_policy node.entries.(position).stored_key key = 0
      then Some node.entries.(position)
      else find_node tree_policy key node.children.(position)

let find_entry_opt key tree = find_node tree.tree_policy key tree.root
let find_opt key tree = Option.map (fun entry -> entry.stored_value) (find_entry_opt key tree)
let mem key tree = Option.is_some (find_entry_opt key tree)

let rec collect result = function
  | None -> result
  | Some node ->
      let result = ref result in
      for index = Array.length node.entries - 1 downto 0 do
        result := collect !result node.children.(index + 1);
        result := node.entries.(index) :: !result
      done;
      collect !result node.children.(0)

let stored_entries tree = collect [] tree.root

let to_list tree =
  List.map
    (fun entry -> { key = entry.stored_key; value = entry.stored_value })
    (stored_entries tree)

let set key value tree =
  match create_entry tree.tree_policy key value with
  | Error message -> Error message
  | Ok replacement -> (
      let existing = find_entry_opt key tree in
      match existing with
      | Some entry when Bytes.equal entry.value_bytes replacement.value_bytes -> Ok tree
      | _ ->
          let rec replace result = function
            | [] -> List.rev (replacement :: result)
            | entry :: rest ->
                let comparison = Merkle_encoding.compare tree.tree_policy entry.stored_key key in
                if comparison = 0 then
                  List.rev_append result
                    ({
                       replacement with
                       stored_key = entry.stored_key;
                       key_bytes = entry.key_bytes;
                       level = entry.level;
                     }
                    :: rest)
                else if comparison > 0 then List.rev_append result (replacement :: entry :: rest)
                else replace (entry :: result) rest
          in
          let entries = replace [] (stored_entries tree) in
          Ok { tree with root = build tree.tree_policy entries })

let add key value tree =
  if mem key tree then Error "an equivalent Merkle key is already present" else set key value tree

let remove key tree =
  if not (mem key tree) then Ok tree
  else
    let entries =
      List.filter
        (fun entry -> Merkle_encoding.compare tree.tree_policy entry.stored_key key <> 0)
        (stored_entries tree)
    in
    Ok { tree with root = build tree.tree_policy entries }

let range ~minimum ~maximum tree =
  if Merkle_encoding.compare tree.tree_policy minimum maximum > 0 then
    invalid_arg "minimum exceeds maximum";
  List.filter
    (fun entry ->
      Merkle_encoding.compare tree.tree_policy entry.key minimum >= 0
      && Merkle_encoding.compare tree.tree_policy entry.key maximum <= 0)
    (to_list tree)

let blocks_preorder tree =
  let rec visit result = function
    | None -> result
    | Some node ->
        let result = { digest = node.node_digest; content = node.node_content } :: result in
        Array.fold_left visit result node.children
  in
  List.rev (visit [] tree.root)

let validate tree =
  match
    of_list tree.tree_policy (List.map (fun entry -> (entry.key, entry.value)) (to_list tree))
  with
  | Error message -> Error message
  | Ok rebuilt ->
      if Merkle_encoding.digest_equal (root_digest tree) (root_digest rebuilt) then Ok ()
      else Error "Merkle root does not match its canonical contents"

let entry_at_rank requested_rank tree =
  if requested_rank < 0 || requested_rank >= count tree then None
  else
    let rec descend rank node = scan rank node 0
    and scan rank node index =
      let entry_count = Array.length node.entries in
      if index > entry_count then None
      else
        let child = node.children.(index) in
        let child_count = match child with None -> 0 | Some value -> value.node_count in
        if rank < child_count then Option.bind child (descend rank)
        else if index = entry_count then None
        else if rank = child_count then
          let entry = node.entries.(index) in
          Some { key = entry.stored_key; value = entry.stored_value }
        else scan (rank - child_count - 1) node (index + 1)
    in
    Option.bind tree.root (descend requested_rank)

let lower_bound_rank key tree =
  let rec descend rank = function
    | None -> (rank, false)
    | Some node ->
        let rec search low high =
          if low >= high then low
          else
            let middle = (low + high) / 2 in
            if Merkle_encoding.compare tree.tree_policy node.entries.(middle).stored_key key < 0
            then search (middle + 1) high
            else search low middle
        in
        let position = search 0 (Array.length node.entries) in
        let preceding = ref rank in
        for index = 0 to position - 1 do
          preceding :=
            !preceding
            + (match node.children.(index) with None -> 0 | Some child -> child.node_count)
            + 1
        done;
        let found =
          position < Array.length node.entries
          && Merkle_encoding.compare tree.tree_policy node.entries.(position).stored_key key = 0
        in
        if found then
          ( (!preceding
            + match node.children.(position) with None -> 0 | Some child -> child.node_count),
            true )
        else descend !preceding node.children.(position)
  in
  descend 0 tree.root

let cursor_tree_count = count
let cursor_tree_add = add
let cursor_tree_set = set
let cursor_tree_remove = remove

module Cursor = struct
  type ('key, 'value) cursor = { tree : ('key, 'value) t; position : int }

  let start tree = { tree; position = 0 }

  let at position tree =
    if position < 0 || position > count tree then None else Some { tree; position }

  let at_end tree = { tree; position = count tree }

  let lower_bound key tree =
    let position, _ = lower_bound_rank key tree in
    { tree; position }

  let upper_bound key tree =
    let position, found = lower_bound_rank key tree in
    { tree; position = (position + if found then 1 else 0) }

  let exact key tree =
    let position, found = lower_bound_rank key tree in
    ({ tree; position }, found)

  let count cursor = cursor_tree_count cursor.tree
  let position cursor = cursor.position
  let is_at_start cursor = cursor.position = 0
  let is_at_end cursor = cursor.position = count cursor

  let peek_previous cursor =
    if is_at_start cursor then None else entry_at_rank (cursor.position - 1) cursor.tree

  let peek_next cursor = entry_at_rank cursor.position cursor.tree

  let move_previous cursor =
    if is_at_start cursor then None else Some { cursor with position = cursor.position - 1 }

  let move_next cursor =
    if is_at_end cursor then None else Some { cursor with position = cursor.position + 1 }

  let seek position cursor =
    if position < 0 || position > count cursor then None
    else if position = cursor.position then Some cursor
    else Some { cursor with position }

  let insert key value cursor =
    let expected, found = lower_bound_rank key cursor.tree in
    if found then Error "the Merkle key is already present"
    else if expected <> cursor.position then
      Error
        (Printf.sprintf "the key belongs at gap %d, not at the current gap %d" expected
           cursor.position)
    else
      match cursor_tree_add key value cursor.tree with
      | Error message -> Error message
      | Ok tree -> Ok { tree; position = cursor.position + 1 }

  let set key value cursor =
    let expected, found = lower_bound_rank key cursor.tree in
    if expected <> cursor.position then
      Error
        (Printf.sprintf "the key belongs at gap %d, not at the current gap %d" expected
           cursor.position)
    else
      match cursor_tree_set key value cursor.tree with
      | Error message -> Error message
      | Ok tree -> Ok { tree; position = (cursor.position + if found then 0 else 1) }

  let set_next_value value cursor =
    match peek_next cursor with
    | None -> Ok None
    | Some entry -> (
        match cursor_tree_set entry.key value cursor.tree with
        | Error message -> Error message
        | Ok tree -> Ok (Some { cursor with tree }))

  let delete_previous cursor =
    match peek_previous cursor with
    | None -> Ok None
    | Some entry -> (
        match cursor_tree_remove entry.key cursor.tree with
        | Error message -> Error message
        | Ok tree -> Ok (Some { tree; position = cursor.position - 1 }))

  let delete_next cursor =
    match peek_next cursor with
    | None -> Ok None
    | Some entry -> (
        match cursor_tree_remove entry.key cursor.tree with
        | Error message -> Error message
        | Ok tree -> Ok (Some { cursor with tree }))

  let snapshot cursor = cursor.tree
end
