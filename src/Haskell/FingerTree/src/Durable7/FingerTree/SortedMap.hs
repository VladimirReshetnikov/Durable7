{-# LANGUAGE MultiParamTypeClasses #-}

-- | A persistent sorted map with rank and select over its keys.
--
-- Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
-- so an edit copies a path rather than the whole collection.
--
-- The representation is the workspace's own lazy measured finger tree of entries in ascending key
-- order under a (count, last key) measure, exactly as 'Durable7.FingerTree.SortedBag' is built.
-- Keyed searches are measure-directed descents, extremes are digit reads at the root, and rank and
-- key windows are structural splits. Entries are strict in their values, matching the strict map
-- this module used to wrap.
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
  , keyRange
  , slice
  , keys
  , elems
  ) where

import Prelude hiding (lookup, null)

import qualified Data.List as List
import qualified Durable7.FingerTree.Measured as Measured

-- | The measure combines positional weight with the last key in each prefix.
-- Key predicates are monotone because entries are stored in ascending key order.
data MapMeasure k = MapMeasure !Int !(Maybe k)
  deriving (Eq, Ord, Read, Show)

instance Semigroup (MapMeasure k) where
  MapMeasure leftCount leftKey <> MapMeasure rightCount rightKey =
    MapMeasure
      (leftCount + rightCount)
      (case rightKey of
        Just _ -> rightKey
        Nothing -> leftKey)

instance Monoid (MapMeasure k) where
  mempty = MapMeasure 0 Nothing

-- | One stored entry. The value is strict, preserving the strict-map semantics of the substrate
-- this module replaced.
data MapEntry k v = MapEntry !k !v
  deriving (Eq, Ord, Read, Show)

instance Measured.Measured (MapMeasure k) (MapEntry k v) where
  measure (MapEntry key _) = MapMeasure 1 (Just key)

type MapTree k v = Measured.FingerTree (MapMeasure k) (MapEntry k v)

-- | A persistent sorted map with rank and select over its keys.
newtype SortedMap k v = SortedMap (MapTree k v)
  deriving (Read, Show)

instance (Eq k, Eq v) => Eq (SortedMap k v) where
  left == right = toList left == toList right

instance (Ord k, Ord v) => Ord (SortedMap k v) where
  compare left right = compare (toList left) (toList right)

-- | The empty map. O(1).
empty :: SortedMap k v
empty = SortedMap Measured.empty

-- | A map holding one entry. O(1).
singleton :: k -> v -> SortedMap k v
singleton key value = SortedMap (Measured.singleton (MapEntry key value))

-- | A map holding a list's entries. @O(m log m)@ by repeated insertion, so the last supplied
-- entry of each key class provides both the retained key representative and the value.
fromList :: Ord k => [(k, v)] -> SortedMap k v
fromList = List.foldl' (\stored (key, value) -> insert key value stored) empty

-- | The entries, in the map's own order. Lazy; Theta(n) fully consumed and comparing no keys.
toList :: SortedMap k v -> [(k, v)]
toList (SortedMap entries) = map entryPair (Measured.toList entries)

-- | Number of entries in the map. O(1): the count is cached in the root measure.
count :: SortedMap k v -> Int
count (SortedMap entries) = measureCount (Measured.measureTree entries)

-- | Whether the map holds no entries. O(1).
null :: SortedMap k v -> Bool
null (SortedMap entries) = Measured.null entries

-- | Whether the entry is present. O(log n): one measure-directed descent.
member :: Ord k => k -> SortedMap k v -> Bool
member key (SortedMap entries) =
  case locateAtLeast key entries of
    Just (_, stored) -> sameClass stored key
    Nothing -> False

-- | The value stored for the key, or `Nothing` when absent. O(log n).
lookup :: Ord k => k -> SortedMap k v -> Maybe v
lookup key (SortedMap entries) =
  case locateAtLeast key entries of
    Just (_, stored)
      | sameClass stored key -> Just (entryValue stored)
    _ -> Nothing

-- | The value stored for the key, or the supplied default when absent. O(log n).
findWithDefault :: Ord k => v -> k -> SortedMap k v -> v
findWithDefault defaultValue key mapValue =
  case lookup key mapValue of
    Just value -> value
    Nothing -> defaultValue

-- | A map containing the given entry. The supplied key replaces an equivalent stored key, exactly
-- as the strict map this module used to wrap did. O(log n): one measured split and a
-- bounded-depth rejoin.
insert :: Ord k => k -> v -> SortedMap k v -> SortedMap k v
insert key value (SortedMap entries) =
  case splitAtLeast key entries of
    Nothing -> SortedMap (Measured.snoc entries (MapEntry key value))
    Just (left, stored, right)
      | sameClass stored key ->
          SortedMap (joinWithEntry left (MapEntry key value) right)
      | otherwise ->
          SortedMap
            (Measured.append
              (Measured.snoc left (MapEntry key value))
              (Measured.cons stored right))

-- | A map containing the given entry, or `Nothing` when an equivalent key is present. O(log n).
insertNew :: Ord k => k -> v -> SortedMap k v -> Maybe (SortedMap k v)
insertNew key value mapValue
  | member key mapValue = Nothing
  | otherwise = Just (insert key value mapValue)

-- | A map without that entry; the receiver itself when it is absent. O(log n).
delete :: Ord k => k -> SortedMap k v -> SortedMap k v
delete key mapValue@(SortedMap entries) =
  case splitAtLeast key entries of
    Just (left, stored, right)
      | sameClass stored key -> SortedMap (Measured.append left right)
    _ -> mapValue

-- | The entry with the smallest key. O(1): a digit read at the tree's root, never a descent.
-- Forcing a suspended spine left by earlier edits is charged to the edits that suspended it.
minEntry :: SortedMap k v -> Maybe (k, v)
minEntry (SortedMap entries) = entryPair <$> Measured.head entries

-- | The entry with the largest key. O(1): a digit read at the tree's root, never a descent.
-- Forcing a suspended spine left by earlier edits is charged to the edits that suspended it.
maxEntry :: SortedMap k v -> Maybe (k, v)
maxEntry (SortedMap entries) = entryPair <$> Measured.last entries

-- | The entry whose key is the largest not greater than the probe. O(log n).
floorEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
floorEntry key (SortedMap entries) =
  case splitAtLeast key entries of
    Nothing -> entryPair <$> Measured.last entries
    Just (left, stored, _)
      | sameClass stored key -> Just (entryPair stored)
      | otherwise -> entryPair <$> Measured.last left

-- | The entry whose key is the smallest not less than the probe. O(log n).
ceilingEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
ceilingEntry key (SortedMap entries) = entryPair . snd <$> locateAtLeast key entries

-- | The entry whose key is the largest strictly less than the probe. O(log n).
lowerEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
lowerEntry key (SortedMap entries) =
  case splitAtLeast key entries of
    Nothing -> entryPair <$> Measured.last entries
    Just (left, _, _) -> entryPair <$> Measured.last left

-- | The entry whose key is the smallest strictly greater than the probe. O(log n).
higherEntry :: Ord k => k -> SortedMap k v -> Maybe (k, v)
higherEntry key (SortedMap entries) = entryPair . snd <$> locateAbove key entries

-- | The entry at the given rank. O(log n): one descent directed by the cached counts, comparing
-- no keys.
index :: Int -> SortedMap k v -> Maybe (k, v)
index position mapValue@(SortedMap entries)
  | position < 0 || position >= count mapValue = Nothing
  | otherwise =
      entryPair . snd <$> Measured.locate (\measureValue -> measureCount measureValue > position) entries

-- | The rank of the given key. O(log n).
indexOfKey :: Ord k => k -> SortedMap k v -> Maybe Int
indexOfKey key (SortedMap entries) =
  case locateAtLeast key entries of
    Just (before, stored)
      | sameClass stored key -> Just (measureCount before)
    _ -> Nothing

-- | How many keys order before the probe. O(log n).
countKeysLessThan :: Ord k => k -> SortedMap k v -> Int
countKeysLessThan key (SortedMap entries) =
  measureBeforeCount (locateAtLeast key entries) (Measured.measureTree entries)

-- | How many keys order at or before the probe. O(log n).
countKeysAtMost :: Ord k => k -> SortedMap k v -> Int
countKeysAtMost key (SortedMap entries) =
  measureBeforeCount (locateAbove key entries) (Measured.measureTree entries)

-- | The entries whose keys lie in the inclusive range @[low, high]@, sharing the source's untouched
-- subtrees.  An inverted range (@low > high@) yields the empty map.
--
-- O(log n), independent of how many entries the range selects or excludes: one measured boundary
-- split drops everything below @low@, and a second split over the remainder — cheap near its left
-- edge, so its cost tracks the window rather than the map — cuts everything above @high@.  Stored
-- key representatives are preserved exactly — the probes select boundaries and are never stored —
-- so a caller whose comparison is coarser than equality still sees the keys the map retained.
keyRange :: Ord k => k -> k -> SortedMap k v -> SortedMap k v
keyRange low high (SortedMap entries)
  | low > high = empty
  | otherwise =
      case splitAtLeast low entries of
        Nothing -> empty
        Just (_, stored, right) ->
          let fromLow = Measured.cons stored right
           in case Measured.split (maybe False (> high) . measureLastKey) fromLow of
                Nothing -> SortedMap fromLow
                Just (kept, _, _) -> SortedMap kept

-- | The entries in the given rank range, sharing the source's untouched subtrees. O(log n): two
-- structural splits directed by the cached counts, independent of the window's position and
-- comparing no keys at all.
slice :: Ord k => Int -> Int -> SortedMap k v -> Maybe (SortedMap k v)
slice position lengthValue mapValue@(SortedMap entries)
  | not (isValidRange position lengthValue (count mapValue)) = Nothing
  | otherwise =
      let (_, afterPosition) = splitAtRank position entries
          (selected, _) = splitAtRank lengthValue afterPosition
       in Just (SortedMap selected)

-- | The keys, in the map's own order. Lazy; Theta(n) fully consumed.
keys :: SortedMap k v -> [k]
keys = fmap fst . toList

-- | The values. Lazy; Theta(n) fully consumed.
elems :: SortedMap k v -> [v]
elems = fmap snd . toList

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position

entryPair :: MapEntry k v -> (k, v)
entryPair (MapEntry key value) = (key, value)

entryValue :: MapEntry k v -> v
entryValue (MapEntry _ value) = value

measureCount :: MapMeasure k -> Int
measureCount (MapMeasure total _) = total

measureLastKey :: MapMeasure k -> Maybe k
measureLastKey (MapMeasure _ key) = key

-- Ordered containers decide identity by comparison, never by (==), so a coarse Ord instance
-- keeps working even when Eq is finer.
sameClass :: Ord k => MapEntry k v -> k -> Bool
sameClass (MapEntry stored _) key = compare stored key == EQ

locateAtLeast :: Ord k => k -> MapTree k v -> Maybe (MapMeasure k, MapEntry k v)
locateAtLeast key = Measured.locate (maybe False (>= key) . measureLastKey)

locateAbove :: Ord k => k -> MapTree k v -> Maybe (MapMeasure k, MapEntry k v)
locateAbove key = Measured.locate (maybe False (> key) . measureLastKey)

splitAtLeast :: Ord k => k -> MapTree k v -> Maybe (MapTree k v, MapEntry k v, MapTree k v)
splitAtLeast key = Measured.split (maybe False (>= key) . measureLastKey)

joinWithEntry :: MapTree k v -> MapEntry k v -> MapTree k v -> MapTree k v
joinWithEntry left entry right = Measured.append (Measured.snoc left entry) right

measureBeforeCount :: Maybe (MapMeasure k, MapEntry k v) -> MapMeasure k -> Int
measureBeforeCount located whole =
  case located of
    Just (before, _) -> measureCount before
    Nothing -> measureCount whole

splitAtRank :: Int -> MapTree k v -> (MapTree k v, MapTree k v)
splitAtRank position entries
  | position <= 0 = (Measured.empty, entries)
  | position >= measureCount (Measured.measureTree entries) = (entries, Measured.empty)
  | otherwise =
      case Measured.split (\measureValue -> measureCount measureValue > position) entries of
        Nothing -> (entries, Measured.empty)
        Just (left, stored, right) -> (left, Measured.cons stored right)
