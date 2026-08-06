(** Fully branching persistent insertion-only connectivity forest with a first-connection query.

    Vertices are the fixed integer universe [0 .. vertex_count - 1]. Every successful {!link}
    performs union by size {e without} path compression and labels the one new root-parent edge with
    the child version token. A redundant (already connected) link still publishes a distinct child
    history version, but shares the complete connectivity index, which
    {!shares_connectivity_root_with} can confirm.

    The parent cells live sparsely in a {!Persistent_hamt} map. An absent cell denotes a singleton
    root, so {!create} is [O(1)] rather than [O(vertex_count)]. Union by size limits every parent
    path to [O(log n)] cells. {!first_connected} compares the two current parent paths and takes the
    latest union-edge version strictly below their forest lowest common ancestor; it never searches
    the version history, so its cost is independent of history depth.

    Values and version tokens are immutable snapshots safe for concurrent readers. Deletion,
    confluent merging, retroactive updates, and path compression are deliberately outside this
    module's contract. The [O(log n)] parent-path factor is worst-case logarithmic in component size
    rather than the inverse-Ackermann amortized bound of an ephemeral linear-history union-find.

    {2 Why the CHAMP factor is unconditional here}

    The bounds below are worst case, not expected, because this module pins its own vertex hash
    policy instead of accepting one. The shared trie masks a key hash to [max_int] and consumes it
    five bits per level, storing hash-equal keys in a linearly scanned collision bucket; under a
    truncating or colliding hash both the bucket occupancy and the descent length are bounded only
    in expectation. This module hashes a vertex to itself. Vertices are validated into
    [0 .. vertex_count - 1] before any cell is touched, so masking is the identity and the hash is
    {e injective} on the whole key universe: no collision bucket can ever hold two distinct keys,
    and two distinct vertices must diverge within the [62] significant hash bits, that is within
    [ceil (62 / 5) = 13] levels of [32]-way nodes. One cell read or write is therefore [O(1)] in the
    worst case, and every bound stated here is unconditional. This is a deliberate divergence from
    the ports that document an expected factor because their substrate hash is truncated or
    collision-prone; only the hash policy differs.

    {2 Version identity}

    Tokens use {e physical} identity, exposed as {!same_version}. Structural identity would be wrong
    twice over: a token records only a depth, a parent, and a root, so two sibling branches that
    split at the same depth would compare equal and a witness from one branch would masquerade as a
    witness from the other; and a history root refers to itself as its own root, so a structural
    comparison would not terminate. Never compare tokens with [(=)] or pass them to a structural
    equality. That self-reference is patched in immediately after a history root is allocated,
    before any caller can observe the token, and never reassigned; it is also what stops the native
    compiler from lifting the otherwise all-constant root record into shared static data, which
    would make two independently created forests share one history root.

    Every walk over a version chain in this module is a loop or a tail call, so a history hundreds
    of thousands of links deep costs no stack. *)

type t
(** One immutable version of the connectivity forest. *)

type version
(** Identifies one immutable version in a forest's history tree. Compare tokens with
    {!same_version}; equal depths in sibling branches are different versions. *)

type statistics = {
  vertex_count : int;
  component_count : int;
  stored_cell_count : int;
  maximum_parent_path_length : int;
  history_depth : int;
}
(** Shape measurements returned by a structural audit. [stored_cell_count] counts the explicitly
    stored parent cells, absent cells being singleton roots; [maximum_parent_path_length] is the
    longest parent path found, in edges, which [floor (log2 vertex_count)] bounds; [history_depth]
    is the number of link events from the history root to the audited version. *)

val create : int -> (t, string) result
(** [create vertex_count] is an edgeless history root over vertices [0 .. vertex_count - 1], every
    one of them a singleton component. [O(1)]: a singleton is the {e absence} of a cell, so a
    universe of two vertices costs what a universe of [max_int] costs. Fails when [vertex_count] is
    negative. *)

val vertex_count : t -> int
(** The fixed number of vertices in this forest. [O(1)]. *)

val component_count : t -> int
(** The number of connected components in this version. [O(1)]. *)

val version : t -> version
(** The identity token for this history version. [O(1)]. *)

val version_depth : version -> int
(** The number of link events from the history root to this version. [O(1)]. *)

val version_parent : version -> version option
(** This version's parent, or [None] at the history root. [O(1)]. *)

val version_root : version -> version
(** The root identity of this version's history tree. [O(1)]. *)

val same_version : version -> version -> bool
(** Whether two handles denote the very same version. [O(1)] physical identity, never structural
    equality. *)

val find : int -> t -> (int, string) result
(** [find vertex forest] is the current union-find representative of [vertex], selected by
    deterministic union-by-size history. [O(log n)] worst case. The representative is an
    implementation artifact: sibling versions need not agree on it. Fails when [vertex] lies outside
    the fixed universe. *)

val connected : int -> int -> t -> (bool, string) result
(** [connected left right forest] reports whether the two vertices have the same current root.
    [O(log n)] worst case. Fails when either vertex lies outside the fixed universe. *)

val component_size : int -> t -> (int, string) result
(** [component_size vertex forest] is the number of vertices in the component containing [vertex],
    read as the union-by-size count cached at that component's current root; an isolated vertex
    reports [1]. [O(log n)] worst case, one parent-path walk plus a cached read, with no path
    compression. Fails when [vertex] lies outside the fixed universe. *)

val link : int -> int -> t -> (t, string) result
(** [link left right forest] is a child history version holding an undirected connection between
    the two vertices. The child version is always distinct. When the endpoints were already
    connected the child shares the connectivity cells unchanged; otherwise one union-by-size edge is
    added and labelled with the child version, and on a size tie the {e first} endpoint's root stays
    the representative. [O(log n)] time and [O(1)] new trie nodes for a successful union; [O(log n)]
    time and [O(1)] space when redundant. Fails when either vertex lies outside the fixed universe,
    or when the history depth could not grow further; the receiver is left untouched either way. *)

val first_connected : int -> int -> t -> (version option, string) result
(** [first_connected left right forest] is [Ok (Some witness)] where [witness] is the earliest
    version on this version's root-to-current history in which the two vertices were connected,
    [Ok None] when they are disconnected in this version, and [Error] when a vertex lies outside the
    fixed universe: absence and rejection stay distinguishable. A vertex is connected to itself at
    the history root.

    The witness is computed from the two current parent paths as the latest union-edge version
    strictly below their forest lowest common ancestor, so the version history is never searched. It
    is never a version on another branch, and a later merge of the pair's component into a bigger
    one never replaces the pair's earlier witness. [O(log n)] worst-case time and [O(log n)]
    temporary path space, independent of history depth. *)

val shares_connectivity_root_with : t -> t -> bool
(** Whether two versions retain the exact same sparse connectivity index, so neither can observe a
    union recorded in the other. A representation test, not an equality test. [O(1)]. *)

val validate : t -> (statistics, string) result
(** Audits sparse-cell canonicality, current root sizes, the resulting union-by-size height ceiling,
    union-tag ancestry and order, and the cached component count, returning the shape measurements
    on success. [O(h + m log n)] for history depth [h] and explicit-cell count [m]. A defensive
    audit; ordinary operations maintain these invariants. *)
