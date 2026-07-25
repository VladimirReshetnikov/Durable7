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

empty :: (Eq k, Hashable k) => PersistentOrderedMap k v
empty = emptyWith HashMap.defaultPolicy

emptyWith :: HashPolicy k -> PersistentOrderedMap k v
emptyWith hashPolicy = PersistentOrderedMap (OrderedSet.emptyWith hashPolicy) (HashMap.emptyWith hashPolicy)

fromList :: (Eq k, Hashable k) => [(k, v)] -> PersistentOrderedMap k v
fromList = fromListWith HashMap.defaultPolicy

-- First key representative and position win; the last payload wins.
fromListWith :: HashPolicy k -> [(k, v)] -> PersistentOrderedMap k v
fromListWith hashPolicy = List.foldl' (\current (key, value) -> set key value current) (emptyWith hashPolicy)

size :: PersistentOrderedMap k v -> Int
size (PersistentOrderedMap keys _) = OrderedSet.size keys

null :: PersistentOrderedMap k v -> Bool
null values = size values == 0

policy :: PersistentOrderedMap k v -> HashPolicy k
policy (PersistentOrderedMap keys _) = OrderedSet.policy keys

member :: k -> PersistentOrderedMap k v -> Bool
member key (PersistentOrderedMap _ values) = HashMap.member key values

lookup :: k -> PersistentOrderedMap k v -> Maybe v
lookup key (PersistentOrderedMap _ values) = HashMap.lookup key values

actualKey :: k -> PersistentOrderedMap k v -> Maybe k
actualKey key (PersistentOrderedMap keys _) = OrderedSet.actualValue key keys

entryAt :: Int -> PersistentOrderedMap k v -> Maybe (k, v)
entryAt index (PersistentOrderedMap keys values) = do
  key <- OrderedSet.at index keys
  value <- HashMap.lookup key values
  pure (key, value)

first :: PersistentOrderedMap k v -> Maybe (k, v)
first = entryAt 0

last :: PersistentOrderedMap k v -> Maybe (k, v)
last values = entryAt (size values - 1) values

indexOf :: k -> PersistentOrderedMap k v -> Int
indexOf key (PersistentOrderedMap keys _) = OrderedSet.indexOf key keys

add :: k -> v -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
add key value values = insertAt (size values) key value values

addFirst :: k -> v -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
addFirst = insertAt 0

insertAt :: Int -> k -> v -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
insertAt index key value values@(PersistentOrderedMap keys payloads)
  | index < 0 || index > size values = Nothing
  | member key values = Nothing
  | otherwise = do
      nextKeys <- OrderedSet.insertAt index key keys
      pure (PersistentOrderedMap nextKeys (HashMap.insert key value payloads))

set :: k -> v -> PersistentOrderedMap k v -> PersistentOrderedMap k v
set key value values@(PersistentOrderedMap keys payloads)
  | member key values = PersistentOrderedMap keys (HashMap.insert key value payloads)
  | otherwise = PersistentOrderedMap (OrderedSet.add key keys) (HashMap.insert key value payloads)

moveToFirst :: k -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
moveToFirst key = moveWith (OrderedSet.moveToFirst key)

moveToLast :: k -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
moveToLast key = moveWith (OrderedSet.moveToLast key)

moveTo :: Int -> k -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
moveTo index key = moveWith (OrderedSet.moveTo index key)

moveWith :: (PersistentOrderedSet k -> Maybe (PersistentOrderedSet k)) -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
moveWith edit (PersistentOrderedMap keys values) = (`PersistentOrderedMap` values) <$> edit keys

delete :: k -> PersistentOrderedMap k v -> PersistentOrderedMap k v
delete key values = maybe values id (tryRemove key values)

tryRemove :: k -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
tryRemove key (PersistentOrderedMap keys values) = do
  nextKeys <- OrderedSet.tryRemove key keys
  pure (PersistentOrderedMap nextKeys (HashMap.delete key values))

deleteAt :: Int -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
deleteAt index (PersistentOrderedMap keys payloads) = do
  key <- OrderedSet.at index keys
  nextKeys <- OrderedSet.deleteAt index keys
  pure (PersistentOrderedMap nextKeys (HashMap.delete key payloads))

removeFirst :: PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
removeFirst = deleteAt 0

removeLast :: PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
removeLast values = deleteAt (size values - 1) values

clear :: PersistentOrderedMap k v -> PersistentOrderedMap k v
clear values = emptyWith (policy values)

getRange :: Int -> Int -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
getRange index count values@(PersistentOrderedMap keys payloads) = do
  nextKeys <- OrderedSet.getRange index count keys
  let nextValues = HashMap.fromListWith (policy values)
        [ (key, value)
        | key <- OrderedSet.toList nextKeys
        , Just value <- [HashMap.lookup key payloads]
        ]
  pure (PersistentOrderedMap nextKeys nextValues)

take :: Int -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
take count = getRange 0 count

drop :: Int -> PersistentOrderedMap k v -> Maybe (PersistentOrderedMap k v)
drop count values = getRange count (size values - count) values

reverse :: PersistentOrderedMap k v -> PersistentOrderedMap k v
reverse (PersistentOrderedMap keys values) = PersistentOrderedMap (OrderedSet.reverse keys) values

sortBy :: ((k, v) -> (k, v) -> Ordering) -> PersistentOrderedMap k v -> PersistentOrderedMap k v
sortBy compareEntries values@(PersistentOrderedMap keys payloads) =
  PersistentOrderedMap (OrderedSet.sortBy compareKeys keys) payloads
  where
    compareKeys left right = compareEntries (attach left) (attach right)
    attach key = case lookup key values of
      Just value -> (key, value)
      Nothing -> error "PersistentOrderedMap indexes disagree"

toList :: PersistentOrderedMap k v -> [(k, v)]
toList values@(PersistentOrderedMap keys _) =
  [ case lookup key values of
      Just value -> (key, value)
      Nothing -> error "PersistentOrderedMap indexes disagree"
  | key <- OrderedSet.toList keys
  ]

sharesOrderWith :: PersistentOrderedMap k v -> PersistentOrderedMap k v -> Bool
sharesOrderWith (PersistentOrderedMap left _) (PersistentOrderedMap right _) = OrderedSet.sharesIndexWith left right

sharesValuesWith :: PersistentOrderedMap k v -> PersistentOrderedMap k v -> Bool
sharesValuesWith (PersistentOrderedMap _ left) (PersistentOrderedMap _ right) = HashMap.sharesRootWith left right

validStructure :: PersistentOrderedMap k v -> Bool
validStructure values@(PersistentOrderedMap keys payloads) =
  OrderedSet.validStructure keys
    && HashMap.validStructure payloads
    && OrderedSet.size keys == HashMap.size payloads
    && all (`HashMap.member` payloads) (OrderedSet.toList keys)
    && all (`OrderedSet.contains` keys) (HashMap.keys payloads)
    && length (toList values) == size values
