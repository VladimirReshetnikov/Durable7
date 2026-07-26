{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MultiParamTypeClasses #-}

-- | A persistent bag of closed intervals, with overlap and containment queries.
--
-- Each subtree caches the maximum high endpoint below it, so a query visits only the subtrees
-- that can still hold a match. Every operation returns a new version and leaves its inputs valid,
-- sharing unchanged structure, so an edit copies a path rather than the whole collection.
module Durable7.FingerTree.IntervalTree
  ( Interval(..)
  , IntervalTree
  , mkInterval
  , empty
  , singleton
  , fromList
  , toList
  , count
  , null
  , insert
  , delete
  , contains
  , findOverlap
  , findOverlaps
  , countOverlaps
  , findContaining
  , coalesce
  , lowerBoundRank
  , upperBoundRank
  , index
  , deleteAt
  ) where

import Prelude hiding (null)

import qualified Data.List as List
import qualified Durable7.FingerTree.Measured as FT
import Durable7.FingerTree.Measured (ViewL(..))

-- | A closed interval. The low endpoint never exceeds the high one; construction rejects an
-- inverted interval rather than letting it produce silently empty query results.
data Interval a = Interval
  { low :: a
  , high :: a
  }
  deriving (Eq, Ord, Read, Show)

-- | Count supports O(1) size, maximumHigh prunes non-overlapping prefixes,
-- and lastLow supports lower-bound splits because the sequence is low-sorted.
data IntervalMeasure a = IntervalMeasure !Int !(Maybe a) !(Maybe a)
  deriving (Eq, Ord, Read, Show)

instance Ord a => Semigroup (IntervalMeasure a) where
  IntervalMeasure leftCount leftHigh leftLow <> IntervalMeasure rightCount rightHigh rightLow =
    IntervalMeasure
      (leftCount + rightCount)
      (maxMaybe leftHigh rightHigh)
      (case rightLow of Nothing -> leftLow; Just _ -> rightLow)

instance Ord a => Monoid (IntervalMeasure a) where
  mempty = IntervalMeasure 0 Nothing Nothing

newtype IntervalEntry a = IntervalEntry { getInterval :: Interval a }
  deriving (Eq, Ord, Read, Show)

instance Ord a => FT.Measured (IntervalMeasure a) (IntervalEntry a) where
  measure (IntervalEntry interval) = IntervalMeasure 1 (Just (high interval)) (Just (low interval))

-- | A persistent bag of closed intervals. Each subtree's maximum high endpoint is cached, so a
-- query visits only the subtrees that can still contain a match.
newtype IntervalTree a = IntervalTree (FT.FingerTree (IntervalMeasure a) (IntervalEntry a))
  deriving (Show)

instance Ord a => Eq (IntervalTree a) where
  left == right = count left == count right && toList left == toList right

instance Ord a => Ord (IntervalTree a) where
  compare left right = compare (toList left) (toList right)

-- | An interval from its endpoints, rejecting an inverted pair.
mkInterval :: Ord a => a -> a -> Maybe (Interval a)
mkInterval lowValue highValue
  | lowValue <= highValue = Just (Interval lowValue highValue)
  | otherwise = Nothing

-- | The empty tree.
empty :: IntervalTree a
empty = IntervalTree FT.empty

-- | A tree holding one interval.
singleton :: Ord a => Interval a -> IntervalTree a
singleton interval = insert interval empty

-- | A tree holding a list's intervals, built in bulk rather than by repeated insertion.
fromList :: Ord a => [Interval a] -> IntervalTree a
fromList = List.foldl' (flip insert) empty

-- | The intervals, in the tree's own order.
toList :: Ord a => IntervalTree a -> [Interval a]
toList (IntervalTree tree) = map getInterval (FT.toList tree)

-- | Number of intervals in the tree.
count :: Ord a => IntervalTree a -> Int
count (IntervalTree tree) = intervalCount (FT.measureTree tree)

-- | Whether the tree holds no intervals.
null :: Ord a => IntervalTree a -> Bool
null (IntervalTree tree) = FT.null tree

-- | A tree containing the given interval.
insert :: Ord a => Interval a -> IntervalTree a -> IntervalTree a
insert interval@(Interval lowValue highValue) (IntervalTree tree)
  | lowValue > highValue = error "Durable7.FingerTree.IntervalTree.insert: invalid interval"
  | otherwise =
      let (left, right) = splitLowerBound lowValue tree
       in IntervalTree (FT.append left (FT.cons (IntervalEntry interval) right))

-- | A tree without that interval.
delete :: Ord a => Interval a -> IntervalTree a -> IntervalTree a
delete interval tree@(IntervalTree values) =
  let (left, candidates) = splitLowerBound (low interval) values
   in case deleteFromEqualRun interval FT.empty candidates of
        Nothing -> tree
        Just remaining -> IntervalTree (FT.append left remaining)

-- | Whether the interval is present.
contains :: Ord a => Interval a -> IntervalTree a -> Bool
contains interval (IntervalTree values) =
  let (_, candidates) = splitLowerBound (low interval) values
   in containsInEqualRun interval candidates

-- | An interval overlapping the probe, or `Nothing` when none does.
findOverlap :: Ord a => Interval a -> IntervalTree a -> Maybe (Interval a)
findOverlap probe (IntervalTree values) = fst <$> nextOverlap probe values

-- | Every interval overlapping the probe. Subtrees whose cached maximum endpoint falls short of the
-- probe are skipped whole.
findOverlaps :: Ord a => Interval a -> IntervalTree a -> [Interval a]
findOverlaps probe (IntervalTree values) = go values
  where
    go tree =
      case nextOverlap probe tree of
        Nothing -> []
        Just (interval, rest) -> interval : go rest

-- | How many stored intervals overlap the probe.
countOverlaps :: Ord a => Interval a -> IntervalTree a -> Int
countOverlaps probe (IntervalTree values) = go 0 values
  where
    go !total tree =
      case nextOverlap probe tree of
        Nothing -> total
        Just (_, rest) -> go (total + 1) rest

-- | Every interval containing the point.
findContaining :: Ord a => a -> IntervalTree a -> Maybe (Interval a)
findContaining point = findOverlap (Interval point point)

-- | Merges adjacent pieces small enough to be represented as one, so repeated edits do not leave
-- the structure fragmented.
coalesce :: Ord a => IntervalTree a -> IntervalTree a
coalesce = fromList . mergeSorted . toList

-- | The rank of the first key not less than the probe.
lowerBoundRank :: Ord a => a -> IntervalTree a -> Int
lowerBoundRank lowValue (IntervalTree tree) = intervalCount (FT.measureTree left)
  where
    (left, _) = splitLowerBound lowValue tree

-- | The rank after any key equal to the probe.
upperBoundRank :: Ord a => a -> IntervalTree a -> Int
upperBoundRank lowValue tree = go (lowerBoundRank lowValue tree)
  where
    go position =
      case index position tree of
        Just interval | compare (low interval) lowValue == EQ -> go (position + 1)
        _ -> position

-- | The interval at the given rank.
index :: Ord a => Int -> IntervalTree a -> Maybe (Interval a)
index position tree@(IntervalTree values)
  | position < 0 || position >= count tree = Nothing
  | otherwise = do
      (_, IntervalEntry interval) <- FT.locate (\measureValue -> intervalCount measureValue > position) values
      pure interval

-- | A tree without the interval at the position.
deleteAt :: Ord a => Int -> IntervalTree a -> Maybe (IntervalTree a)
deleteAt position tree@(IntervalTree values)
  | position < 0 || position >= count tree = Nothing
  | otherwise = do
      (left, _, right) <- FT.split (\measureValue -> intervalCount measureValue > position) values
      pure (IntervalTree (FT.append left right))

splitLowerBound :: Ord a => a -> FT.FingerTree (IntervalMeasure a) (IntervalEntry a) -> (FT.FingerTree (IntervalMeasure a) (IntervalEntry a), FT.FingerTree (IntervalMeasure a) (IntervalEntry a))
splitLowerBound lowValue tree =
  case FT.split (atOrAbove lowValue) tree of
    Nothing -> (tree, FT.empty)
    Just (left, entry, right) -> (left, FT.cons entry right)

atOrAbove :: Ord a => a -> IntervalMeasure a -> Bool
atOrAbove lowValue (IntervalMeasure _ _ lastLow) = maybe False (>= lowValue) lastLow

deleteFromEqualRun :: Ord a => Interval a -> FT.FingerTree (IntervalMeasure a) (IntervalEntry a) -> FT.FingerTree (IntervalMeasure a) (IntervalEntry a) -> Maybe (FT.FingerTree (IntervalMeasure a) (IntervalEntry a))
deleteFromEqualRun target skipped candidates =
  case FT.viewL candidates of
    EmptyL -> Nothing
    entry@(IntervalEntry candidate) :< rest
      | compare (low candidate) (low target) /= EQ -> Nothing
      | sameInterval candidate target -> Just (FT.append skipped rest)
      | otherwise -> deleteFromEqualRun target (FT.snoc skipped entry) rest

containsInEqualRun :: Ord a => Interval a -> FT.FingerTree (IntervalMeasure a) (IntervalEntry a) -> Bool
containsInEqualRun target candidates =
  case FT.viewL candidates of
    EmptyL -> False
    IntervalEntry candidate :< rest
      | compare (low candidate) (low target) /= EQ -> False
      | sameInterval candidate target -> True
      | otherwise -> containsInEqualRun target rest

sameInterval :: Ord a => Interval a -> Interval a -> Bool
sameInterval left right =
  compare (low left) (low right) == EQ && compare (high left) (high right) == EQ

-- | Split at the first prefix whose cached maximum high can reach the probe's
-- low endpoint. The returned candidate itself caused the threshold crossing,
-- so it overlaps unless its (sorted) low is already beyond the probe high.
nextOverlap :: Ord a => Interval a -> FT.FingerTree (IntervalMeasure a) (IntervalEntry a) -> Maybe (Interval a, FT.FingerTree (IntervalMeasure a) (IntervalEntry a))
nextOverlap probe tree = do
  (_, IntervalEntry candidate, right) <- FT.split reachesProbe tree
  if low candidate <= high probe
    then Just (candidate, right)
    else Nothing
  where
    reachesProbe (IntervalMeasure _ maximumHigh _) = maybe False (>= low probe) maximumHigh

intervalCount :: IntervalMeasure a -> Int
intervalCount (IntervalMeasure total _ _) = total

maxMaybe :: Ord a => Maybe a -> Maybe a -> Maybe a
maxMaybe Nothing right = right
maxMaybe left Nothing = left
maxMaybe (Just left) (Just right) = Just (max left right)

mergeSorted :: Ord a => [Interval a] -> [Interval a]
mergeSorted [] = []
mergeSorted (firstInterval : rest) = reverse (List.foldl' step [firstInterval] rest)
  where
    step [] interval = [interval]
    step (current@(Interval currentLow currentHigh) : merged) next@(Interval nextLow nextHigh)
      | nextLow <= currentHigh = Interval currentLow (max currentHigh nextHigh) : merged
      | otherwise = next : current : merged
