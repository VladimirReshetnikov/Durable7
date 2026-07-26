-- | A persistent multimap: each key bound to a persistent set of values, so keys whose values did
-- not change are shared outright between versions.
--
-- Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
-- so an edit copies a path rather than the whole collection.
module Durable7.Hamt.HashMultimap
  ( HashMultimap
  , empty
  , emptyWith
  , size
  , keyCount
  , null
  , keyPolicy
  , valuePolicy
  , member
  , memberKey
  , actualKey
  , lookupValues
  , insert
  , delete
  , deleteKey
  , clear
  , union
  , intersection
  , difference
  , toList
  , validStructure
  ) where

import Prelude hiding (null)

import qualified Data.List as List
import Durable7.Hamt.Hashable (Hashable)
import Durable7.Hamt.HashMap (HashMap, HashPolicy)
import qualified Durable7.Hamt.HashMap as HashMap
import Durable7.Hamt.HashSet (HashSet)
import qualified Durable7.Hamt.HashSet as HashSet

-- | A persistent set-valued multimap. Empty value groups are never stored.
data HashMultimap k v = HashMultimap
  !(HashPolicy v)
  !Int
  !(HashMap k (HashSet v))

-- | The empty multimap.
empty :: (Eq k, Hashable k, Eq v, Hashable v) => HashMultimap k v
empty = emptyWith HashMap.defaultPolicy HashMap.defaultPolicy

-- | The empty multimap under the given policy, which it retains.
emptyWith :: HashPolicy k -> HashPolicy v -> HashMultimap k v
emptyWith keys values = HashMultimap values 0 (HashMap.emptyWith keys)

-- | Number of pairs in the multimap.
size :: HashMultimap k v -> Int
size (HashMultimap _ pairCount _) = pairCount

-- | How many distinct keys are present.
keyCount :: HashMultimap k v -> Int
keyCount (HashMultimap _ _ groups) = HashMap.size groups

-- | Whether the multimap holds no pairs.
null :: HashMultimap k v -> Bool
null values = size values == 0

-- | The retained key policy.
keyPolicy :: HashMultimap k v -> HashPolicy k
keyPolicy (HashMultimap _ _ groups) = HashMap.policy groups

-- | The retained value policy.
valuePolicy :: HashMultimap k v -> HashPolicy v
valuePolicy (HashMultimap values _ _) = values

-- | Whether the pair is present.
member :: k -> v -> HashMultimap k v -> Bool
member key value values = maybe False (HashSet.member value) (lookupValues key values)

-- | Whether the key is present.
memberKey :: k -> HashMultimap k v -> Bool
memberKey key (HashMultimap _ _ groups) = HashMap.member key groups

-- | The stored key representative, which need not be the key passed in.
actualKey :: k -> HashMultimap k v -> Maybe k
actualKey key (HashMultimap _ _ groups) = HashMap.actualKey key groups

-- | The values bound to the key, or `Nothing` when the key is absent.
lookupValues :: k -> HashMultimap k v -> Maybe (HashSet v)
lookupValues key (HashMultimap _ _ groups) = HashMap.lookup key groups

-- | A multimap containing the given pair.
insert :: k -> v -> HashMultimap k v -> HashMultimap k v
insert key value values@(HashMultimap valuesPolicy pairCount groups) =
  case HashMap.lookup key groups of
    Nothing -> HashMultimap valuesPolicy (checkedIncrement pairCount)
      (HashMap.insert key (HashSet.singletonWith valuesPolicy value) groups)
    Just group
      | HashSet.member value group -> values
      | otherwise -> HashMultimap valuesPolicy (checkedIncrement pairCount)
          (HashMap.insert key (HashSet.insert value group) groups)

-- | A multimap without that pair.
delete :: k -> v -> HashMultimap k v -> HashMultimap k v
delete key value values@(HashMultimap valuesPolicy pairCount groups) =
  case HashMap.lookup key groups of
    Nothing -> values
    Just group ->
      case HashSet.tryRemove value group of
        Nothing -> values
        Just rest
          | HashSet.null rest -> HashMultimap valuesPolicy (pairCount - 1) (HashMap.delete key groups)
          | otherwise -> HashMultimap valuesPolicy (pairCount - 1) (HashMap.insert key rest groups)

-- | A multimap without that key.
deleteKey :: k -> HashMultimap k v -> HashMultimap k v
deleteKey key values@(HashMultimap valuesPolicy pairCount groups) =
  case HashMap.lookup key groups of
    Nothing -> values
    Just group -> HashMultimap valuesPolicy (pairCount - HashSet.size group) (HashMap.delete key groups)

-- | The empty multimap, retaining the same policies.
clear :: HashMultimap k v -> HashMultimap k v
clear values = emptyWith (keyPolicy values) (valuePolicy values)

-- | Algebra normalizes the complete right operand under the receiver policies.
union :: HashMultimap k v -> HashMultimap k v -> HashMultimap k v
union left right = List.foldl' (\current (key, value) -> insert key value current) left (toList right)

-- | The pairs present in both multimaps.
intersection :: HashMultimap k v -> HashMultimap k v -> HashMultimap k v
intersection left right = List.foldl' keep (clear left) (toList left)
  where
    normalized = normalizeFor left right
    keep current pair@(key, value)
      | member key value normalized = uncurry insert pair current
      | otherwise = current

-- | This multimap's pairs that are absent from the other.
difference :: HashMultimap k v -> HashMultimap k v -> HashMultimap k v
difference left right =
  let normalized = normalizeFor left right
   in List.foldl' (\current (key, value) -> delete key value current) left (toList normalized)

-- | The pairs, in the multimap's own order.
toList :: HashMultimap k v -> [(k, v)]
toList (HashMultimap _ _ groups) =
  [ (key, value)
  | (key, values) <- HashMap.toList groups
  , value <- HashSet.toList values
  ]

-- | Whether the multimap's structural invariants hold. For tests and diagnostics.
validStructure :: HashMultimap k v -> Bool
validStructure values@(HashMultimap _ pairCount groups) =
  HashMap.validStructure groups
    && pairCount >= 0
    && all (not . HashSet.null . snd) entries
    && pairCount == sum (map (HashSet.size . snd) entries)
    && length (toList values) == pairCount
  where
    entries = HashMap.toList groups

normalizeFor :: HashMultimap k v -> HashMultimap k v -> HashMultimap k v
normalizeFor receiver =
  List.foldl' (\current (key, value) -> insert key value current) (clear receiver) . toList

checkedIncrement :: Int -> Int
checkedIncrement value
  | value == maxBound = error "HashMultimap pair count overflow"
  | otherwise = value + 1
