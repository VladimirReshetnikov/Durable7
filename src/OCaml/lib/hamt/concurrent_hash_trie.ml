type ('key, 'value) snapshot = {
  snapshot_root : ('key, 'value) Persistent_hamt.map;
  snapshot_generation_value : int64;
}

type ('key, 'value) t = {
  mutex : Mutex.t;
  mutable root : ('key, 'value) Persistent_hamt.map;
  mutable generation : int64;
}

let create ?value_equal policy =
  { mutex = Mutex.create (); root = Persistent_hamt.empty ?value_equal policy; generation = 0L }

let locked trie action =
  Mutex.lock trie.mutex;
  Fun.protect ~finally:(fun () -> Mutex.unlock trie.mutex) action

let count trie = locked trie (fun () -> Persistent_hamt.count trie.root)
let generation trie = locked trie (fun () -> trie.generation)
let find_opt key trie = locked trie (fun () -> Persistent_hamt.find_opt key trie.root)

let set key value trie =
  locked trie (fun () ->
      let successor = Persistent_hamt.set key value trie.root in
      if successor != trie.root then (
        trie.root <- successor;
        trie.generation <- Int64.succ trie.generation))

let remove key trie =
  locked trie (fun () ->
      let successor = Persistent_hamt.remove key trie.root in
      if successor != trie.root then (
        trie.root <- successor;
        trie.generation <- Int64.succ trie.generation))

let snapshot trie =
  locked trie (fun () -> { snapshot_root = trie.root; snapshot_generation_value = trie.generation })

let snapshot_generation snapshot = snapshot.snapshot_generation_value
let snapshot_find_opt key snapshot = Persistent_hamt.find_opt key snapshot.snapshot_root
let snapshot_to_list snapshot = Persistent_hamt.to_list snapshot.snapshot_root
let snapshot_to_persistent snapshot = snapshot.snapshot_root
