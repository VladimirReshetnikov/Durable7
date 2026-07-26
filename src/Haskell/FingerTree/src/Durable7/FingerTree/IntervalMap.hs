-- | A persistent map keyed by closed intervals, using the same cached-maximum-endpoint trick as the
-- interval tree with a payload per interval.
--
-- Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
-- so an edit copies a path rather than the whole collection.
module Durable7.FingerTree.IntervalMap
  ( IntervalMap
  , empty
  , singleton
  , fromList
  , size
  , null
  , member
  , lookup
  , entryAt
  , add
  , set
  , delete
  , clear
  , findOverlap
  , findOverlaps
  , countOverlaps
  , toList
  , validStructure
  , lowerBoundRank
  , upperBoundRank
  ) where

import Prelude hiding (lookup, null)

import qualified Data.List as List
import qualified Data.Map.Strict as Map
import Durable7.FingerTree.IntervalTree (Interval(..), IntervalTree)
import qualified Durable7.FingerTree.IntervalTree as IntervalTree

-- | Unique closed intervals with payloads. The augmented interval tree drives
-- overlap navigation while the ordered map supplies exact keys and values.
data IntervalMap a v = IntervalMap !(IntervalTree a) !(Map.Map (Interval a) v)
  deriving (Eq, Ord, Show)

-- | The empty map.
empty :: IntervalMap a v
empty = IntervalMap IntervalTree.empty Map.empty

-- | A map holding one entry.
singleton :: Ord a => Interval a -> v -> IntervalMap a v
singleton interval value = set interval value empty

-- | A map holding a list's entries, built in bulk rather than by repeated insertion.
fromList :: Ord a => [(Interval a, v)] -> IntervalMap a v
fromList = List.foldl' (\current (interval, value) -> set interval value current) empty

-- | Number of entries in the map.
size :: IntervalMap a v -> Int
size (IntervalMap _ values) = Map.size values

-- | Whether the map holds no entries.
null :: IntervalMap a v -> Bool
null (IntervalMap _ values) = Map.null values

-- | Whether the entry is present.
member :: Ord a => Interval a -> IntervalMap a v -> Bool
member interval (IntervalMap _ values) = Map.member interval values

-- | The value stored for the key, or `Nothing` when absent.
lookup :: Ord a => Interval a -> IntervalMap a v -> Maybe v
lookup interval (IntervalMap _ values) = Map.lookup interval values

-- | The entry at the given rank.
entryAt :: Int -> IntervalMap a v -> Maybe (Interval a, v)
entryAt index (IntervalMap _ values)
  | index < 0 || index >= Map.size values = Nothing
  | otherwise = Just (Map.elemAt index values)

-- | A map containing the given entry.
add :: Ord a => Interval a -> v -> IntervalMap a v -> Maybe (IntervalMap a v)
add interval value values
  | invalid interval = error "IntervalMap.add: invalid interval"
  | member interval values = Nothing
  | otherwise = Just (set interval value values)

-- | A map with the key bound to the value, adding or replacing as needed.
set :: Ord a => Interval a -> v -> IntervalMap a v -> IntervalMap a v
set interval value (IntervalMap intervals payloads)
  | invalid interval = error "IntervalMap.set: invalid interval"
  | Map.member interval payloads = IntervalMap intervals (Map.insert interval value payloads)
  | otherwise = IntervalMap (IntervalTree.insert interval intervals) (Map.insert interval value payloads)

-- | A map without that entry.
delete :: Ord a => Interval a -> IntervalMap a v -> IntervalMap a v
delete interval values@(IntervalMap intervals payloads)
  | not (Map.member interval payloads) = values
  | otherwise = IntervalMap (IntervalTree.delete interval intervals) (Map.delete interval payloads)

-- | The empty map, retaining the same policies.
clear :: IntervalMap a v -> IntervalMap a v
clear _ = empty

-- | An interval overlapping the probe, or `Nothing` when none does.
findOverlap :: Ord a => Interval a -> IntervalMap a v -> Maybe (Interval a, v)
findOverlap probe (IntervalMap intervals payloads) = do
  interval <- IntervalTree.findOverlap probe intervals
  value <- Map.lookup interval payloads
  pure (interval, value)

-- | Every interval overlapping the probe. Subtrees whose cached maximum endpoint falls short of the
-- probe are skipped whole.
findOverlaps :: Ord a => Interval a -> IntervalMap a v -> [(Interval a, v)]
findOverlaps probe (IntervalMap intervals payloads) =
  map attach (IntervalTree.findOverlaps probe intervals)
  where
    attach interval =
      case Map.lookup interval payloads of
        Just value -> (interval, value)
        Nothing -> error "IntervalMap indexes disagree"

-- | How many stored intervals overlap the probe.
countOverlaps :: Ord a => Interval a -> IntervalMap a v -> Int
countOverlaps probe (IntervalMap intervals _) = IntervalTree.countOverlaps probe intervals

-- | The entries, in the map's own order.
toList :: IntervalMap a v -> [(Interval a, v)]
toList (IntervalMap _ values) = Map.toAscList values

-- | Whether the map's structural invariants hold. For tests and diagnostics.
validStructure :: Ord a => IntervalMap a v -> Bool
validStructure (IntervalMap intervals values) =
  IntervalTree.count intervals == Map.size values
    && all (`Map.member` values) (IntervalTree.toList intervals)
    && all (`IntervalTree.contains` intervals) (Map.keys values)

-- | The rank of the first key not less than the probe.
lowerBoundRank :: Ord a => Interval a -> IntervalMap a v -> Int
lowerBoundRank interval (IntervalMap _ values) = Map.size lower
  where
    (lower, _) = Map.split interval values

-- | The rank after any key equal to the probe.
upperBoundRank :: Ord a => Interval a -> IntervalMap a v -> Int
upperBoundRank interval mapValue = lowerBoundRank interval mapValue + if member interval mapValue then 1 else 0

invalid :: Ord a => Interval a -> Bool
invalid interval = low interval > high interval
