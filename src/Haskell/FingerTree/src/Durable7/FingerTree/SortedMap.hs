-- | A persistent sorted map with rank and select over its keys.
--
-- Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
-- so an edit copies a path rather than the whole collection.
module Durable7.FingerTree.SortedMap
  ( SortedMap
  , empty
  , singleton
  , fromList
  , toList
  , count
  , null
  , member
  , lookup
  , findWithDefault
  , insert
  , insertNew
  , delete
  , minEntry
  , maxEntry
  , floorEntry
  , ceilingEntry
  , lowerEntry
  , higherEntry
  , index
  , indexOfKey
  , countKeysLessThan
  , countKeysAtMost
  , slice
  , keys
  , elems
  ) where

import Prelude hiding (lookup, null)

import qualified Data.Map.Strict as Map

-- | A persistent sorted map with rank and select over its keys.
newtype SortedMap k v = SortedMap (Map.Map k v)
  deriving (Eq, Ord, Read, Show)

-- | The empty map.
empty :: SortedMap k v
empty = SortedMap Map.empty

-- | A map holding one entry.
singleton :: k -> v -> SortedMap k v
singleton key value = SortedMap (Map.singleton key value)

-- | A map holding a list's entries, built in bulk rather than by repeated insertion.
fromList :: Ord k => [(k, v)] -> SortedMap k v
fromList entries = SortedMap (Map.fromList entries)

-- | The entries, in the map's own order.
toList :: SortedMap k v -> [(k, v)]
toList (SortedMap values) = Map.toAscList values

-- | Number of entries in the map.
count :: SortedMap k v -> Int
count (SortedMap values) = Map.size values

-- | Whether the map holds no entries.
null :: SortedMap k v -> Bool
null (SortedMap values) = Map.null values

-- | Whether the entry is present.
member :: Ord k => k -> SortedMap k v -> Bool
member key (SortedMap values) = Map.member key values

-- | The value stored for the key, or `Nothing` when absent.
lookup :: Ord k => k -> SortedMap k v -> Maybe v
lookup key (SortedMap values) = Map.lookup key values

-- | The value stored for the key, or the supplied default when absent.
findWithDefault :: Ord k => v -> k -> SortedMap k v -> v
findWithDefault defaultValue key mapValue =
  case lookup key mapValue of
    Just value -> value
    Nothing -> defaultValue

-- | A map containing the given entry.
insert :: Ord k => k -> v -> SortedMap k v -> SortedMap k v
insert key value (SortedMap values) = SortedMap (Map.insert key value values)

-- | A map containing the given entry, unchanged when an equivalent one is present.
insertNew :: Ord k => k -> v -> SortedMap k v -> Maybe (SortedMap k v)
insertNew key value mapValue@(SortedMap values)
  | Map.member key values = Nothing
  | otherwise = Just (insert key value mapValue)

-- | A map without that entry.
delete :: Ord k => k -> SortedMap k v -> SortedMap k v
delete key (SortedMap values) = SortedMap (Map.delete key values)

-- | The entry with the smallest key.
minEntry :: SortedMap k v -> Maybe (k, v)
minEntry (SortedMap values) = Map.lookupMin values

-- | The entry with the largest key.
maxEntry :: SortedMap k v -> Maybe (k, v)
maxEntry (SortedMap values) = Map.lookupMax values

-- | The entry whose key is the largest not greater than the probe.
floorEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
floorEntry key (SortedMap values) = Map.lookupLE key values

-- | The entry whose key is the smallest not less than the probe.
ceilingEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
ceilingEntry key (SortedMap values) = Map.lookupGE key values

-- | The entry whose key is the largest strictly less than the probe.
lowerEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
lowerEntry key (SortedMap values) = Map.lookupLT key values

-- | The entry whose key is the smallest strictly greater than the probe.
higherEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
higherEntry key (SortedMap values) = Map.lookupGT key values

-- | The entry at the given rank.
index :: Int -> SortedMap k v -> Maybe (k, v)
index position (SortedMap values)
  | position < 0 || position >= Map.size values = Nothing
  | otherwise = Just (Map.elemAt position values)

-- | The rank of the given key.
indexOfKey :: Ord k => k -> SortedMap k v -> Maybe Int
indexOfKey key (SortedMap values) = Map.lookupIndex key values

-- | How many keys order before the probe.
countKeysLessThan :: Ord k => k -> SortedMap k v -> Int
countKeysLessThan key (SortedMap values) = Map.size lower
  where
    (lower, _) = Map.split key values

-- | How many keys order at or before the probe.
countKeysAtMost :: Ord k => k -> SortedMap k v -> Int
countKeysAtMost key mapValue = countKeysLessThan key mapValue + if member key mapValue then 1 else 0

-- | The entries in the given range.
slice :: Ord k => Int -> Int -> SortedMap k v -> Maybe (SortedMap k v)
slice position lengthValue mapValue
  | not (isValidRange position lengthValue (count mapValue)) = Nothing
  | otherwise = Just (fromList (take lengthValue (drop position (toList mapValue))))

-- | The keys, in the map's own order.
keys :: SortedMap k v -> [k]
keys = fmap fst . toList

-- | The values.
elems :: SortedMap k v -> [v]
elems = fmap snd . toList

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position
