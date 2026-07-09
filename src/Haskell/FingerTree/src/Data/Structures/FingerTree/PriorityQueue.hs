{-# LANGUAGE FlexibleInstances #-}
{-# LANGUAGE MultiParamTypeClasses #-}

module Data.Structures.FingerTree.PriorityQueue
  ( PriorityQueue
  , empty
  , singleton
  , fromList
  , toList
  , count
  , null
  , enqueue
  , meld
  , peekPriority
  , peek
  , dequeue
  ) where

import Prelude hiding (null)

import qualified Data.List as List
import qualified Data.Structures.FingerTree.Measured as FT
import Data.Structures.FingerTree.Measured (Measured(..))

data PrioritySummary p = PrioritySummary !Int !(Maybe p)
  deriving (Eq, Ord, Read, Show)

instance Ord p => Semigroup (PrioritySummary p) where
  PrioritySummary leftCount leftMin <> PrioritySummary rightCount rightMin =
    PrioritySummary (leftCount + rightCount) (combineMin leftMin rightMin)
    where
      combineMin Nothing right = right
      combineMin left Nothing = left
      combineMin (Just left) (Just right) = Just (min left right)

instance Ord p => Monoid (PrioritySummary p) where
  mempty = PrioritySummary 0 Nothing

data Entry p a = Entry !p a
  deriving (Eq, Ord, Read, Show)

instance Ord p => FT.Measured (PrioritySummary p) (Entry p a) where
  measure (Entry priority _) = PrioritySummary 1 (Just priority)

newtype PriorityQueue p a = PriorityQueue (FT.FingerTree (PrioritySummary p) (Entry p a))
  deriving (Show)

-- Extensional equality over the entry sequence in queue (insertion) order.
instance (Ord p, Eq a) => Eq (PriorityQueue p a) where
  left == right = count left == count right && toList left == toList right

instance (Ord p, Ord a) => Ord (PriorityQueue p a) where
  compare left right = compare (toList left) (toList right)

empty :: PriorityQueue p a
empty = PriorityQueue FT.empty

singleton :: Ord p => p -> a -> PriorityQueue p a
singleton priority value = enqueue priority value empty

fromList :: Ord p => [(p, a)] -> PriorityQueue p a
fromList = List.foldl' (\queue (priority, value) -> enqueue priority value queue) empty

toList :: Ord p => PriorityQueue p a -> [(p, a)]
toList (PriorityQueue tree) = map unwrap (FT.toList tree)
  where
    unwrap (Entry priority value) = (priority, value)

count :: Ord p => PriorityQueue p a -> Int
count (PriorityQueue tree) =
  case FT.measureTree tree of
    PrioritySummary total _ -> total

null :: Ord p => PriorityQueue p a -> Bool
null queue = count queue == 0

enqueue :: Ord p => p -> a -> PriorityQueue p a -> PriorityQueue p a
enqueue priority value (PriorityQueue tree) = PriorityQueue (FT.snoc tree (Entry priority value))

meld :: Ord p => PriorityQueue p a -> PriorityQueue p a -> PriorityQueue p a
meld (PriorityQueue left) (PriorityQueue right) = PriorityQueue (FT.append left right)

peekPriority :: Ord p => PriorityQueue p a -> Maybe p
peekPriority (PriorityQueue tree) =
  case FT.measureTree tree of
    PrioritySummary _ minimumPriority -> minimumPriority

peek :: Ord p => PriorityQueue p a -> Maybe (a, p)
peek queue =
  case splitFront queue of
    Just (_, value, priority, _) -> Just (value, priority)
    Nothing -> Nothing

dequeue :: Ord p => PriorityQueue p a -> Maybe ((a, p), PriorityQueue p a)
dequeue queue =
  case splitFront queue of
    Just (left, value, priority, right) -> Just ((value, priority), PriorityQueue (FT.append left right))
    Nothing -> Nothing

splitFront :: Ord p => PriorityQueue p a -> Maybe (FT.FingerTree (PrioritySummary p) (Entry p a), a, p, FT.FingerTree (PrioritySummary p) (Entry p a))
splitFront (PriorityQueue tree) =
  case FT.measureTree tree of
    PrioritySummary _ Nothing -> Nothing
    PrioritySummary _ (Just target) ->
      case FT.split (hasPriorityAtMost target) tree of
        Just (left, Entry priority value, right) -> Just (left, value, priority, right)
        Nothing -> Nothing

hasPriorityAtMost :: Ord p => p -> PrioritySummary p -> Bool
hasPriorityAtMost target (PrioritySummary _ minimumPriority) =
  case minimumPriority of
    Just priority -> priority <= target
    Nothing -> False
