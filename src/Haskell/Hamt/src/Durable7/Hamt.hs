-- | The durable7-hamt package's umbrella module: the persistent hash collections and everything
-- built on them.
--
-- Re-exports the CHAMP-backed map and set together with the multiset, multimap, bimap, relation,
-- graph, indexed map, patch, Patricia tries and Merkle search tree.
module Durable7.Hamt
  ( HashMap.HashMap
  , BiMap.BiMap
  , BiMap.BiMapConflict(..)
  , HashSet.HashSet
  , HashBag.HashBag
  , HashBag.HashBagError(..)
  , HashMultimap.HashMultimap
  , Relation.Relation
  , PersistentDirectedGraph.PersistentDirectedGraph
  , PersistentIndexedMap.PersistentIndexedMap
  , PersistentMapPatch.PersistentMapPatch
  , PersistentMapPatch.MapPatchEntry(..)
  , HashMap.HashPolicy(..)
  , HashMap.defaultPolicy
  , module Durable7.Hamt.Hashable
  , module Durable7.Hamt.Patricia
  , module Durable7.Hamt.Transient
  ) where

-- Integer-specialized Patricia collections live in
-- Durable7.Hamt.Patricia to avoid colliding with the hash-map names.

import Durable7.Hamt.Hashable
import Durable7.Hamt.Patricia
import Durable7.Hamt.Transient
import qualified Durable7.Hamt.BiMap as BiMap
import qualified Durable7.Hamt.HashBag as HashBag
import qualified Durable7.Hamt.HashMap as HashMap
import qualified Durable7.Hamt.HashMultimap as HashMultimap
import qualified Durable7.Hamt.HashSet as HashSet
import qualified Durable7.Hamt.Relation as Relation
import qualified Durable7.Hamt.PersistentDirectedGraph as PersistentDirectedGraph
import qualified Durable7.Hamt.PersistentIndexedMap as PersistentIndexedMap
import qualified Durable7.Hamt.PersistentMapPatch as PersistentMapPatch
