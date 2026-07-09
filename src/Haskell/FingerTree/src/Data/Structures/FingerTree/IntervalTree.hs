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
  ) where

import Prelude hiding (null)

import qualified Data.List as List
import qualified Data.Map.Strict as Map

data Interval a = Interval
  { low :: a
  , high :: a
  }
  deriving (Eq, Ord, Read, Show)

data IntervalTree a = IntervalTree !Int !(Map.Map a [Interval a])
  deriving (Eq, Ord, Read, Show)

mkInterval :: Ord a => a -> a -> Maybe (Interval a)
mkInterval lowValue highValue
  | lowValue <= highValue = Just (Interval lowValue highValue)
  | otherwise = Nothing

empty :: IntervalTree a
empty = IntervalTree 0 Map.empty

singleton :: Ord a => Interval a -> IntervalTree a
singleton interval = insert interval empty

fromList :: Ord a => [Interval a] -> IntervalTree a
fromList = List.foldl' (flip insert) empty

toList :: IntervalTree a -> [Interval a]
toList (IntervalTree _ values) = concat (Map.elems values)

count :: IntervalTree a -> Int
count (IntervalTree total _) = total

null :: IntervalTree a -> Bool
null tree = count tree == 0

insert :: Ord a => Interval a -> IntervalTree a -> IntervalTree a
insert interval@(Interval lowValue highValue) (IntervalTree total values)
  | lowValue > highValue = error "Data.Structures.FingerTree.IntervalTree.insert: invalid interval"
  | otherwise = IntervalTree (total + 1) (Map.alter add lowValue values)
  where
    add Nothing = Just [interval]
    -- A new interval precedes existing equal-low ones, matching the C#
    -- reference (Insert splits at the first stored low >= the new low).
    add (Just bucket) = Just (interval : bucket)

delete :: Ord a => Interval a -> IntervalTree a -> IntervalTree a
delete interval@(Interval lowValue _) tree@(IntervalTree total values) =
  case Map.lookup lowValue values of
    Nothing -> tree
    Just bucket ->
      case removeFirst interval bucket of
        Nothing -> tree
        Just remaining -> IntervalTree (total - 1) (replaceBucket lowValue remaining values)

contains :: Ord a => Interval a -> IntervalTree a -> Bool
contains interval tree = interval `elem` toList tree

findOverlap :: Ord a => Interval a -> IntervalTree a -> Maybe (Interval a)
findOverlap probe tree =
  case findOverlaps probe tree of
    firstMatch : _ -> Just firstMatch
    [] -> Nothing

findOverlaps :: Ord a => Interval a -> IntervalTree a -> [Interval a]
findOverlaps probe tree = filter (overlaps probe) (toList tree)

countOverlaps :: Ord a => Interval a -> IntervalTree a -> Int
countOverlaps probe tree = length (findOverlaps probe tree)

findContaining :: Ord a => a -> IntervalTree a -> Maybe (Interval a)
findContaining point tree =
  case filter (containsPoint point) (toList tree) of
    firstMatch : _ -> Just firstMatch
    [] -> Nothing

coalesce :: Ord a => IntervalTree a -> IntervalTree a
coalesce = fromList . mergeSorted . toList

overlaps :: Ord a => Interval a -> Interval a -> Bool
overlaps (Interval leftLow leftHigh) (Interval rightLow rightHigh) =
  leftLow <= rightHigh && rightLow <= leftHigh

containsPoint :: Ord a => a -> Interval a -> Bool
containsPoint point (Interval lowValue highValue) = lowValue <= point && point <= highValue

mergeSorted :: Ord a => [Interval a] -> [Interval a]
mergeSorted [] = []
mergeSorted (firstInterval : rest) = reverse (foldl step [firstInterval] rest)
  where
    step [] interval = [interval]
    step (current@(Interval currentLow currentHigh) : merged) next@(Interval nextLow nextHigh)
      | nextLow <= currentHigh = Interval currentLow (max currentHigh nextHigh) : merged
      | otherwise = next : current : merged

removeFirst :: Eq a => a -> [a] -> Maybe [a]
removeFirst _ [] = Nothing
removeFirst value (candidate : rest)
  | value == candidate = Just rest
  | otherwise = (candidate :) <$> removeFirst value rest

replaceBucket :: Ord k => k -> [v] -> Map.Map k [v] -> Map.Map k [v]
replaceBucket key [] values = Map.delete key values
replaceBucket key bucket values = Map.insert key bucket values
