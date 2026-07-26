-- | A persistent map that remembers insertion order.
--
-- Replacing an entry's value leaves its position alone, which is what distinguishes this from a
-- map rebuilt in iteration order. Every operation returns a new version and leaves its inputs
-- valid, sharing unchanged structure, so an edit copies a path rather than the whole collection.
module Durable7.Ordered.PersistentOrderedMap
  ( PersistentOrderedMap
  , empty
  , emptyWith
  , fromList
  , fromListWith
  , size
  , null
  , policy
  , member
  , lookup
  , actualKey
  , entryAt
  , first
  , last
  , indexOf
  , add
  , addFirst
  , insertAt
  , set
  , moveToFirst
  , moveToLast
  , moveTo
  , delete
  , tryRemove
  , deleteAt
  , removeFirst
  , removeLast
  , clear
  , getRange
  , take
  , drop
  , reverse
  , sortBy
  , toList
  , sharesOrderWith
  , sharesValuesWith
  , validStructure
  ) where

import Prelude hiding (drop, last, lookup, null, reverse, take)

import qualified Data.List as List
import Durable7.Hamt.Hashable (Hashable)
import Durable7.Hamt.HashMap (HashMap, HashPolicy)
import qualified Durable7.Hamt.HashMap as HashMap
import Durable7.Ordered.PersistentOrderedSet (PersistentOrderedSet)
import qualified Durable7.Ordered.PersistentOrderedSet as OrderedSet

-- | A persistent insertion-ordered map. The ordered set owns explicit key
-- order and the CHAMP owns payload lookup under the same runtime key policy.
data PersistentOrderedMap k v = PersistentOrderedMap !(PersistentOrderedSet k) !(HashMap k v)

-- | The empty map.
empty :: (Eq k, Hashable k) => PersistentOrderedMap k v
empty = emptyWith HashMap.defaultPolicy

-- | The empty map under the given policy, which it retains.
emptyWith :: HashPolicy k -> PersistentOrderedMap k v
emptyWith hashPolicy = PersistentOrderedMap (OrderedSet.emptyWith hashPolicy) (HashMap.emptyWith hashPolicy)

-- | A map holding a list's entries, built in bulk rather than by repeated insertion.
fromList :: (Eq k, Hashable k) => [(k, v)] -> PersistentOrderedMap k v
fromList = fromListWith HashMap.defaultPolicy

-- | First key representative and position win; the last payload wins.
fromListWith :: HashPolicy k -> [(k, v)] -> PersistentOrderedMap k v
fromListWith hashPolicy = List.foldl' (\current (key, value) -> set key value current) (emptyWith hashPolicy)

-- | Number of entries in the map.
size :: PersistentOrderedMap k v -> Int
size (PersistentOrderedMap keys _) = OrderedSet.size keys

-- | Whether the map holds no entries.
null :: PersistentOrderedMap k v -> Bool
null values = size values == 0

-- | The policy the map retains.
policy :: PersistentOrderedMap k v -> HashPolicy k
policy (PersistentOrderedMap keys _) = OrderedSet.policy keys

-- | Whether the entry is present.
member :: k -> PersistentOrderedMap k v -> Bool
member key (PersistentOrderedMap _ values) = HashMap.member key values

-- | The value stored for the key, or `Nothing` when absent.
lookup :: k -> PersistentOrderedMap k v -> Maybe v
lookup key (PersistentOrderedMap _ values) = HashMap.lookup key values

-- | The stored key representative, which need not be the key passed in.
actualKey :: k -> PersistentOrderedMap k v -> Maybe k
actualKey key (PersistentOrderedMap keys _) = OrderedSet.actualValue key keys

-- | The entry at the given rank.
entryAt :: Int -> PersistentOrderedMap k v -> Maybe (k, v)
entryAt index (PersistentOrderedMap keys values) = do
  key <- OrderedSet.at index keys
  value <- HashMap.lookup key values
  pure (key, value)

-- | The first entry.
first :: PersistentOrderedMap k v -> Maybe (k, v)
first = entryAt 0

-- | The last entry.
last :: PersistentOrderedMap k v -> Maybe (k, v)
last values = entryAt (size values - 1) values

-- | The rank of the given entry.
indexOf :: k -> PersistentOrderedMap k v -> Int
indexOf key (PersistentOrderedMap keys _) = OrderedSet.indexOf key keys

-- | A map containing the given entry.
add :: k -> v -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
add key value values = insertAt (size values) key value values

-- | A map with the entry placed first in insertion order.
addFirst :: k -> v -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
addFirst = insertAt 0

-- | A map with the entry inserted at the position.
insertAt :: Int -> k -> v -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
insertAt index key value values@(PersistentOrderedMap keys payloads)
  | index < 0 || index > size values = Nothing
  | member key values = Nothing
  | otherwise = do
      nextKeys <- OrderedSet.insertAt index key keys
      pure (PersistentOrderedMap nextKeys (HashMap.insert key value payloads))

-- | A map with the key bound to the value, adding or replacing as needed.
set :: k -> v -> PersistentOrderedMap k v -> PersistentOrderedMap k v
set key value values@(PersistentOrderedMap keys payloads)
  | member key values = PersistentOrderedMap keys (HashMap.insert key value payloads)
  | otherwise = PersistentOrderedMap (OrderedSet.add key keys) (HashMap.insert key value payloads)

-- | A map with the entry moved to the front of the insertion order.
moveToFirst :: k -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
moveToFirst key = moveWith (OrderedSet.moveToFirst key)

-- | A map with the entry moved to the end of the insertion order.
moveToLast :: k -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
moveToLast key = moveWith (OrderedSet.moveToLast key)

-- | A map with the entry moved to the given position in the insertion order.
moveTo :: Int -> k -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
moveTo index key = moveWith (OrderedSet.moveTo index key)

moveWith :: (PersistentOrderedSet k -> Maybe (PersistentOrderedSet k)) -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
moveWith edit (PersistentOrderedMap keys values) = (`PersistentOrderedMap` values) <$> edit keys

-- | A map without that entry.
delete :: k -> PersistentOrderedMap k v -> PersistentOrderedMap k v
delete key values = maybe values id (tryRemove key values)

-- | A map without that entry, or `Nothing` when it was absent.
tryRemove :: k -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
tryRemove key (PersistentOrderedMap keys values) = do
  nextKeys <- OrderedSet.tryRemove key keys
  pure (PersistentOrderedMap nextKeys (HashMap.delete key values))

-- | A map without the entry at the position.
deleteAt :: Int -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
deleteAt index (PersistentOrderedMap keys payloads) = do
  key <- OrderedSet.at index keys
  nextKeys <- OrderedSet.deleteAt index keys
  pure (PersistentOrderedMap nextKeys (HashMap.delete key payloads))

-- | A map without its first entry.
removeFirst :: PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
removeFirst = deleteAt 0

-- | A map without its last entry.
removeLast :: PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
removeLast values = deleteAt (size values - 1) values

-- | The empty map, retaining the same policies.
clear :: PersistentOrderedMap k v -> PersistentOrderedMap k v
clear values = emptyWith (policy values)

-- | The entries in the given range.
getRange :: Int -> Int -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
getRange index count values@(PersistentOrderedMap keys payloads) = do
  nextKeys <- OrderedSet.getRange index count keys
  let nextValues = HashMap.fromListWith (policy values)
        [ (key, value)
        | key <- OrderedSet.toList nextKeys
        , Just value <- [HashMap.lookup key payloads]
        ]
  pure (PersistentOrderedMap nextKeys nextValues)

-- | The first n entries.
take :: Int -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
take count = getRange 0 count

-- | The entries after the first n.
drop :: Int -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
drop count values = getRange count (size values - count) values

-- | The map in the opposite order.
reverse :: PersistentOrderedMap k v -> PersistentOrderedMap k v
reverse (PersistentOrderedMap keys values) = PersistentOrderedMap (OrderedSet.reverse keys) values

-- | The entries ordered by the given comparison. Stable, so equal elements keep their order.
sortBy :: ((k, v) -> (k, v) -> Ordering) -> PersistentOrderedMap k v -> PersistentOrderedMap k v
sortBy compareEntries values@(PersistentOrderedMap keys payloads) =
  PersistentOrderedMap (OrderedSet.sortBy compareKeys keys) payloads
  where
    compareKeys left right = compareEntries (attach left) (attach right)
    attach key = case lookup key values of
      Just value -> (key, value)
      Nothing -> error "PersistentOrderedMap indexes disagree"

-- | The entries, in the map's own order.
toList :: PersistentOrderedMap k v -> [(k, v)]
toList values@(PersistentOrderedMap keys _) =
  [ case lookup key values of
      Just value -> (key, value)
      Nothing -> error "PersistentOrderedMap indexes disagree"
  | key <- OrderedSet.toList keys
  ]

-- | Whether both versions share their order index.
sharesOrderWith :: PersistentOrderedMap k v -> PersistentOrderedMap k v -> Bool
sharesOrderWith (PersistentOrderedMap left _) (PersistentOrderedMap right _) = OrderedSet.sharesIndexWith left right

-- | Whether both versions share their value index.
sharesValuesWith :: PersistentOrderedMap k v -> PersistentOrderedMap k v -> Bool
sharesValuesWith (PersistentOrderedMap _ left) (PersistentOrderedMap _ right) = HashMap.sharesRootWith left right

-- | Whether the map's structural invariants hold. For tests and diagnostics.
validStructure :: PersistentOrderedMap k v -> Bool
validStructure values@(PersistentOrderedMap keys payloads) =
  OrderedSet.validStructure keys
    && HashMap.validStructure payloads
    && OrderedSet.size keys == HashMap.size payloads
    && all (`HashMap.member` payloads) (OrderedSet.toList keys)
    && all (`OrderedSet.contains` keys) (HashMap.keys payloads)
    && length (toList values) == size values
