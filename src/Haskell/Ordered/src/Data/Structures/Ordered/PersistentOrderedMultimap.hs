module Data.Structures.Ordered.PersistentOrderedMultimap
  ( PersistentOrderedMultimap
  , empty
  , emptyWith
  , fromList
  , fromListWith
  , keyCount
  , size
  , null
  , keyPolicy
  , valuePolicy
  , member
  , memberKey
  , actualKey
  , actualValue
  , lookupValues
  , insert
  , delete
  , deleteKey
  , clear
  , toList
  , sharesGroupsWith
  , validStructure
  ) where

import Prelude hiding (null)

import qualified Data.List as List
import Data.Structures.Hamt.Hashable (Hashable)
import Data.Structures.Hamt.HashMap (HashPolicy)
import qualified Data.Structures.Hamt.HashMap as HashMap
import Data.Structures.Ordered.PersistentOrderedMap (PersistentOrderedMap)
import qualified Data.Structures.Ordered.PersistentOrderedMap as OrderedMap
import Data.Structures.Ordered.PersistentOrderedSet (PersistentOrderedSet)
import qualified Data.Structures.Ordered.PersistentOrderedSet as OrderedSet

-- | An immutable key-grouped multimap preserving first-insertion order for
-- key groups and, independently, for distinct values within each group.
-- Enumeration is grouped rather than a global interleaving of pair arrivals.
data PersistentOrderedMultimap k v = PersistentOrderedMultimap
  !(HashPolicy v)
  !Int
  !(PersistentOrderedMap k (PersistentOrderedSet v))

empty :: (Eq k, Hashable k, Eq v, Hashable v) => PersistentOrderedMultimap k v
empty = emptyWith HashMap.defaultPolicy HashMap.defaultPolicy

emptyWith :: HashPolicy k -> HashPolicy v -> PersistentOrderedMultimap k v
emptyWith keys values = PersistentOrderedMultimap values 0 (OrderedMap.emptyWith keys)

fromList :: (Eq k, Hashable k, Eq v, Hashable v) => [(k, v)] -> PersistentOrderedMultimap k v
fromList = List.foldl' (\current (key, value) -> insert key value current) empty

fromListWith :: HashPolicy k -> HashPolicy v -> [(k, v)] -> PersistentOrderedMultimap k v
fromListWith keys values =
  List.foldl' (\current (key, value) -> insert key value current) (emptyWith keys values)

keyCount :: PersistentOrderedMultimap k v -> Int
keyCount (PersistentOrderedMultimap _ _ groups) = OrderedMap.size groups

size :: PersistentOrderedMultimap k v -> Int
size (PersistentOrderedMultimap _ pairCount _) = pairCount

null :: PersistentOrderedMultimap k v -> Bool
null values = size values == 0

keyPolicy :: PersistentOrderedMultimap k v -> HashPolicy k
keyPolicy (PersistentOrderedMultimap _ _ groups) = OrderedMap.policy groups

valuePolicy :: PersistentOrderedMultimap k v -> HashPolicy v
valuePolicy (PersistentOrderedMultimap values _ _) = values

member :: k -> v -> PersistentOrderedMultimap k v -> Bool
member key value values = maybe False (OrderedSet.contains value) (lookupValues key values)

memberKey :: k -> PersistentOrderedMultimap k v -> Bool
memberKey key (PersistentOrderedMultimap _ _ groups) = OrderedMap.member key groups

actualKey :: k -> PersistentOrderedMultimap k v -> Maybe k
actualKey key (PersistentOrderedMultimap _ _ groups) = OrderedMap.actualKey key groups

actualValue :: k -> v -> PersistentOrderedMultimap k v -> Maybe v
actualValue key value values = lookupValues key values >>= OrderedSet.actualValue value

lookupValues :: k -> PersistentOrderedMultimap k v -> Maybe (PersistentOrderedSet v)
lookupValues key (PersistentOrderedMultimap _ _ groups) = OrderedMap.lookup key groups

insert :: k -> v -> PersistentOrderedMultimap k v -> PersistentOrderedMultimap k v
insert key value values@(PersistentOrderedMultimap valuesPolicy pairCount groups) =
  case OrderedMap.lookup key groups of
    Nothing ->
      let group = OrderedSet.add value (OrderedSet.emptyWith valuesPolicy)
       in PersistentOrderedMultimap valuesPolicy (checkedIncrement pairCount) (OrderedMap.set key group groups)
    Just group
      | OrderedSet.contains value group -> values
      | otherwise ->
          PersistentOrderedMultimap
            valuesPolicy
            (checkedIncrement pairCount)
            (OrderedMap.set key (OrderedSet.add value group) groups)

delete :: k -> v -> PersistentOrderedMultimap k v -> PersistentOrderedMultimap k v
delete key value values@(PersistentOrderedMultimap valuesPolicy pairCount groups) =
  case OrderedMap.lookup key groups >>= OrderedSet.tryRemove value of
    Nothing -> values
    Just group ->
      let nextGroups
            | OrderedSet.null group = OrderedMap.delete key groups
            | otherwise = OrderedMap.set key group groups
       in PersistentOrderedMultimap valuesPolicy (pairCount - 1) nextGroups

deleteKey :: k -> PersistentOrderedMultimap k v -> PersistentOrderedMultimap k v
deleteKey key values@(PersistentOrderedMultimap valuesPolicy pairCount groups) =
  case OrderedMap.lookup key groups of
    Nothing -> values
    Just group ->
      PersistentOrderedMultimap valuesPolicy (pairCount - OrderedSet.size group) (OrderedMap.delete key groups)

clear :: PersistentOrderedMultimap k v -> PersistentOrderedMultimap k v
clear values
  | null values = values
  | otherwise = emptyWith (keyPolicy values) (valuePolicy values)

toList :: PersistentOrderedMultimap k v -> [(k, v)]
toList (PersistentOrderedMultimap _ _ groups) =
  [ (key, value)
  | (key, values) <- OrderedMap.toList groups
  , value <- OrderedSet.toList values
  ]

sharesGroupsWith :: PersistentOrderedMultimap k v -> PersistentOrderedMultimap k v -> Bool
sharesGroupsWith (PersistentOrderedMultimap _ _ left) (PersistentOrderedMultimap _ _ right) =
  OrderedMap.sharesOrderWith left right && OrderedMap.sharesValuesWith left right

validStructure :: PersistentOrderedMultimap k v -> Bool
validStructure values@(PersistentOrderedMultimap _ pairCount groups) =
  OrderedMap.validStructure groups
    && pairCount >= 0
    && all (not . OrderedSet.null . snd) entries
    && all (OrderedSet.validStructure . snd) entries
    && pairCount == sum (map (OrderedSet.size . snd) entries)
    && length (toList values) == pairCount
  where
    entries = OrderedMap.toList groups

checkedIncrement :: Int -> Int
checkedIncrement value
  | value == maxBound = error "PersistentOrderedMultimap pair count overflow"
  | otherwise = value + 1
