{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MultiParamTypeClasses #-}

module Data.Structures.FingerTree.IntervalTree
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
import qualified Data.Structures.FingerTree.Measured as FT
import Data.Structures.FingerTree.Measured (ViewL(..))

data Interval a = Interval
  { low :: a
  , high :: a
  }
  deriving (Eq, Ord, Read, Show)

-- Count supports O(1) size, maximumHigh prunes non-overlapping prefixes,
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

newtype IntervalTree a = IntervalTree (FT.FingerTree (IntervalMeasure a) (IntervalEntry a))
  deriving (Show)

instance Ord a => Eq (IntervalTree a) where
  left == right = count left == count right && toList left == toList right

instance Ord a => Ord (IntervalTree a) where
  compare left right = compare (toList left) (toList right)

mkInterval :: Ord a => a -> a -> Maybe (Interval a)
mkInterval lowValue highValue
  | lowValue <= highValue = Just (Interval lowValue highValue)
  | otherwise = Nothing

empty :: IntervalTree a
empty = IntervalTree FT.empty

singleton :: Ord a => Interval a -> IntervalTree a
singleton interval = insert interval empty

fromList :: Ord a => [Interval a] -> IntervalTree a
fromList = List.foldl' (flip insert) empty

toList :: Ord a => IntervalTree a -> [Interval a]
toList (IntervalTree tree) = map getInterval (FT.toList tree)

count :: Ord a => IntervalTree a -> Int
count (IntervalTree tree) = intervalCount (FT.measureTree tree)

null :: Ord a => IntervalTree a -> Bool
null (IntervalTree tree) = FT.null tree

insert :: Ord a => Interval a -> IntervalTree a -> IntervalTree a
insert interval@(Interval lowValue highValue) (IntervalTree tree)
  | lowValue > highValue = error "Data.Structures.FingerTree.IntervalTree.insert: invalid interval"
  | otherwise =
      let (left, right) = splitLowerBound lowValue tree
       in IntervalTree (FT.append left (FT.cons (IntervalEntry interval) right))

delete :: Ord a => Interval a -> IntervalTree a -> IntervalTree a
delete interval tree@(IntervalTree values) =
  let (left, candidates) = splitLowerBound (low interval) values
   in case deleteFromEqualRun interval FT.empty candidates of
        Nothing -> tree
        Just remaining -> IntervalTree (FT.append left remaining)

contains :: Ord a => Interval a -> IntervalTree a -> Bool
contains interval (IntervalTree values) =
  let (_, candidates) = splitLowerBound (low interval) values
   in containsInEqualRun interval candidates

findOverlap :: Ord a => Interval a -> IntervalTree a -> Maybe (Interval a)
findOverlap probe (IntervalTree values) = fst <$> nextOverlap probe values

findOverlaps :: Ord a => Interval a -> IntervalTree a -> [Interval a]
findOverlaps probe (IntervalTree values) = go values
  where
    go tree =
      case nextOverlap probe tree of
        Nothing -> []
        Just (interval, rest) -> interval : go rest

countOverlaps :: Ord a => Interval a -> IntervalTree a -> Int
countOverlaps probe (IntervalTree values) = go 0 values
  where
    go !total tree =
      case nextOverlap probe tree of
        Nothing -> total
        Just (_, rest) -> go (total + 1) rest

findContaining :: Ord a => a -> IntervalTree a -> Maybe (Interval a)
findContaining point = findOverlap (Interval point point)

coalesce :: Ord a => IntervalTree a -> IntervalTree a
coalesce = fromList . mergeSorted . toList

lowerBoundRank :: Ord a => a -> IntervalTree a -> Int
lowerBoundRank lowValue (IntervalTree tree) = intervalCount (FT.measureTree left)
  where
    (left, _) = splitLowerBound lowValue tree

upperBoundRank :: Ord a => a -> IntervalTree a -> Int
upperBoundRank lowValue tree = go (lowerBoundRank lowValue tree)
  where
    go position =
      case index position tree of
        Just interval | compare (low interval) lowValue == EQ -> go (position + 1)
        _ -> position

index :: Ord a => Int -> IntervalTree a -> Maybe (Interval a)
index position tree@(IntervalTree values)
  | position < 0 || position >= count tree = Nothing
  | otherwise = do
      (_, IntervalEntry interval) <- FT.locate (\measureValue -> intervalCount measureValue > position) values
      pure interval

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

-- Split at the first prefix whose cached maximum high can reach the probe's
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
