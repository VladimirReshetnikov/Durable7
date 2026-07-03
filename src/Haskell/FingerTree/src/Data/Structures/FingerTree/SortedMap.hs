module Data.Structures.FingerTree.SortedMap
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
  , slice
  , keys
  , elems
  ) where

import Prelude hiding (lookup, null)

import qualified Data.Map.Strict as Map

newtype SortedMap k v = SortedMap (Map.Map k v)
  deriving (Eq, Ord, Read, Show)

empty :: SortedMap k v
empty = SortedMap Map.empty

singleton :: k -> v -> SortedMap k v
singleton key value = SortedMap (Map.singleton key value)

fromList :: Ord k => [(k, v)] -> SortedMap k v
fromList entries = SortedMap (Map.fromList entries)

toList :: SortedMap k v -> [(k, v)]
toList (SortedMap values) = Map.toAscList values

count :: SortedMap k v -> Int
count (SortedMap values) = Map.size values

null :: SortedMap k v -> Bool
null (SortedMap values) = Map.null values

member :: Ord k => k -> SortedMap k v -> Bool
member key (SortedMap values) = Map.member key values

lookup :: Ord k => k -> SortedMap k v -> Maybe v
lookup key (SortedMap values) = Map.lookup key values

findWithDefault :: Ord k => v -> k -> SortedMap k v -> v
findWithDefault defaultValue key mapValue =
  case lookup key mapValue of
    Just value -> value
    Nothing -> defaultValue

insert :: Ord k => k -> v -> SortedMap k v -> SortedMap k v
insert key value (SortedMap values) = SortedMap (Map.insert key value values)

insertNew :: Ord k => k -> v -> SortedMap k v -> Maybe (SortedMap k v)
insertNew key value mapValue@(SortedMap values)
  | Map.member key values = Nothing
  | otherwise = Just (insert key value mapValue)

delete :: Ord k => k -> SortedMap k v -> SortedMap k v
delete key (SortedMap values) = SortedMap (Map.delete key values)

minEntry :: SortedMap k v -> Maybe (k, v)
minEntry (SortedMap values) = Map.lookupMin values

maxEntry :: SortedMap k v -> Maybe (k, v)
maxEntry (SortedMap values) = Map.lookupMax values

floorEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
floorEntry key (SortedMap values) = Map.lookupLE key values

ceilingEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
ceilingEntry key (SortedMap values) = Map.lookupGE key values

lowerEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
lowerEntry key (SortedMap values) = Map.lookupLT key values

higherEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
higherEntry key (SortedMap values) = Map.lookupGT key values

index :: Int -> SortedMap k v -> Maybe (k, v)
index position (SortedMap values)
  | position < 0 || position >= Map.size values = Nothing
  | otherwise = Just (Map.elemAt position values)

indexOfKey :: Ord k => k -> SortedMap k v -> Maybe Int
indexOfKey key (SortedMap values) = Map.lookupIndex key values

slice :: Ord k => Int -> Int -> SortedMap k v -> Maybe (SortedMap k v)
slice position lengthValue mapValue
  | not (isValidRange position lengthValue (count mapValue)) = Nothing
  | otherwise = Just (fromList (take lengthValue (drop position (toList mapValue))))

keys :: SortedMap k v -> [k]
keys = fmap fst . toList

elems :: SortedMap k v -> [v]
elems = fmap snd . toList

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position
