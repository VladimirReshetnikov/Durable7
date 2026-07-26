{-# LANGUAGE FlexibleInstances #-}
{-# LANGUAGE MultiParamTypeClasses #-}

-- | A persistent priority queue caching the minimum-priority entry as its measure.
--
-- Peeking is a cached read and removal is a measure-directed descent. Every operation returns a
-- new version and leaves its inputs valid, sharing unchanged structure, so an edit copies a path
-- rather than the whole collection.
module Durable7.FingerTree.PriorityQueue
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
import qualified Durable7.FingerTree.Measured as FT
import Durable7.FingerTree.Measured (Measured(..))

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

-- | A persistent priority queue caching the minimum-priority entry as its measure, so peeking is a
-- cached read and removal is a measure-directed descent.
newtype PriorityQueue p a = PriorityQueue (FT.FingerTree (PrioritySummary p) (Entry p a))
  deriving (Show)

-- Extensional equality over the entry sequence in queue (insertion) order.
instance (Ord p, Eq a) => Eq (PriorityQueue p a) where
  left == right = count left == count right && toList left == toList right

instance (Ord p, Ord a) => Ord (PriorityQueue p a) where
  compare left right = compare (toList left) (toList right)

-- | The empty queue.
empty :: PriorityQueue p a
empty = PriorityQueue FT.empty

-- | A queue holding one entry.
singleton :: Ord p => p -> a -> PriorityQueue p a
singleton priority value = enqueue priority value empty

-- | A queue holding a list's entries, built in bulk rather than by repeated insertion.
fromList :: Ord p => [(p, a)] -> PriorityQueue p a
fromList = List.foldl' (\queue (priority, value) -> enqueue priority value queue) empty

-- | The entries, in the queue's own order.
toList :: Ord p => PriorityQueue p a -> [(p, a)]
toList (PriorityQueue tree) = map unwrap (FT.toList tree)
  where
    unwrap (Entry priority value) = (priority, value)

-- | Number of entries in the queue.
count :: Ord p => PriorityQueue p a -> Int
count (PriorityQueue tree) =
  case FT.measureTree tree of
    PrioritySummary total _ -> total

-- | Whether the queue holds no entries.
null :: Ord p => PriorityQueue p a -> Bool
null queue = count queue == 0

-- | A queue with the entry added.
enqueue :: Ord p => p -> a -> PriorityQueue p a -> PriorityQueue p a
enqueue priority value (PriorityQueue tree) = PriorityQueue (FT.snoc tree (Entry priority value))

-- | The queue holding both operands' entries.
meld :: Ord p => PriorityQueue p a -> PriorityQueue p a -> PriorityQueue p a
meld (PriorityQueue left) (PriorityQueue right) = PriorityQueue (FT.append left right)

-- | The minimum priority present, or `Nothing` when empty.
peekPriority :: Ord p => PriorityQueue p a -> Maybe p
peekPriority (PriorityQueue tree) =
  case FT.measureTree tree of
    PrioritySummary _ minimumPriority -> minimumPriority

-- | The minimum-priority entry, or `Nothing` when empty.
peek :: Ord p => PriorityQueue p a -> Maybe (a, p)
peek queue =
  case splitFront queue of
    Just (_, value, priority, _) -> Just (value, priority)
    Nothing -> Nothing

-- | The minimum-priority entry together with the queue remaining, or `Nothing` when empty.
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
