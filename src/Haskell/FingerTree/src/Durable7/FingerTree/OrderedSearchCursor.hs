-- | Immutable root-plus-rank gap cursors for ordered persistent collections.
module Durable7.FingerTree.OrderedSearchCursor
  ( CursorSearch(..)
  , CursorInsert(..)
  , SortedBagCursor
  , sortedBagCursorAt
  , sortedBagLowerBoundCursor
  , sortedBagUpperBoundCursor
  , findSortedBagCursor
  , sortedBagCursorPosition
  , sortedBagPeekPrevious
  , sortedBagPeekNext
  , sortedBagMovePrevious
  , sortedBagMoveNext
  , sortedBagSeekRank
  , sortedBagAdd
  , sortedBagDeletePrevious
  , sortedBagDeleteNext
  , sortedBagSnapshot
  , SortedSetCursor
  , sortedSetCursorAt
  , sortedSetLowerBoundCursor
  , sortedSetUpperBoundCursor
  , findSortedSetCursor
  , sortedSetCursorPosition
  , sortedSetPeekPrevious
  , sortedSetPeekNext
  , sortedSetMovePrevious
  , sortedSetMoveNext
  , sortedSetSeekRank
  , sortedSetAdd
  , sortedSetDeletePrevious
  , sortedSetDeleteNext
  , sortedSetSnapshot
  , SortedMapCursor
  , sortedMapCursorAt
  , sortedMapLowerBoundCursor
  , sortedMapUpperBoundCursor
  , findSortedMapCursor
  , sortedMapCursorPosition
  , sortedMapPeekPrevious
  , sortedMapPeekNext
  , sortedMapMovePrevious
  , sortedMapMoveNext
  , sortedMapSeekRank
  , sortedMapInsert
  , sortedMapTryInsert
  , sortedMapSetItem
  , sortedMapSetNextValue
  , sortedMapDeletePrevious
  , sortedMapDeleteNext
  , sortedMapSnapshot
  , CanonicalSortedSetCursor
  , canonicalCursorAt
  , canonicalLowerBoundCursor
  , canonicalUpperBoundCursor
  , findCanonicalCursor
  , canonicalCursorPosition
  , canonicalPeekPrevious
  , canonicalPeekNext
  , canonicalMovePrevious
  , canonicalMoveNext
  , canonicalSeekRank
  , canonicalAdd
  , canonicalDeletePrevious
  , canonicalDeleteNext
  , canonicalSnapshot
  , PrioritySearchQueueCursor
  , prioritySearchCursorAt
  , prioritySearchLowerBoundCursor
  , prioritySearchUpperBoundCursor
  , findPrioritySearchCursor
  , prioritySearchMinimumCursor
  , prioritySearchCursorPosition
  , prioritySearchPeekPrevious
  , prioritySearchPeekNext
  , prioritySearchMovePrevious
  , prioritySearchMoveNext
  , prioritySearchSeekRank
  , prioritySearchTryInsert
  , prioritySearchSetItem
  , prioritySearchSetNext
  , prioritySearchDeletePrevious
  , prioritySearchDeleteNext
  , prioritySearchSnapshot
  , IntervalTreeCursor
  , intervalTreeCursorAt
  , intervalTreeLowerBoundCursor
  , intervalTreeUpperBoundCursor
  , findIntervalCursor
  , findOverlapCursor
  , findContainingCursor
  , intervalTreeCursorPosition
  , intervalTreePeekPrevious
  , intervalTreePeekNext
  , intervalTreeMovePrevious
  , intervalTreeMoveNext
  , intervalTreeSeekRank
  , intervalTreeSeekNextOverlap
  , intervalTreeInsert
  , intervalTreeDeletePrevious
  , intervalTreeDeleteNext
  , intervalTreeSnapshot
  , IntervalMapCursor
  , intervalMapCursorAt
  , intervalMapLowerBoundCursor
  , intervalMapUpperBoundCursor
  , findIntervalMapCursor
  , findIntervalMapOverlapCursor
  , findIntervalMapContainingCursor
  , intervalMapCursorPosition
  , intervalMapPeekPrevious
  , intervalMapPeekNext
  , intervalMapMovePrevious
  , intervalMapMoveNext
  , intervalMapSeekRank
  , intervalMapSeekNextOverlap
  , intervalMapTryInsert
  , intervalMapSetNextValue
  , intervalMapDeletePrevious
  , intervalMapDeleteNext
  , intervalMapSnapshot
  , ChunkedBitSetCursor
  , chunkedBitSetCursorAt
  , chunkedBitSetCursorAtOrAfter
  , findChunkedBitSetCursor
  , chunkedBitSetCursorPosition
  , chunkedBitSetPeekPrevious
  , chunkedBitSetPeekNext
  , chunkedBitSetMovePrevious
  , chunkedBitSetMoveNext
  , chunkedBitSetSeekRank
  , chunkedBitSetInsert
  , chunkedBitSetDeletePrevious
  , chunkedBitSetDeleteNext
  , chunkedBitSetSnapshot
  ) where

import Data.Int (Int64)

import qualified Durable7.FingerTree.CanonicalSortedSet as Canonical
import qualified Durable7.FingerTree.IntervalMap as IntervalMap
import Durable7.FingerTree.IntervalTree (Interval(..), IntervalTree)
import qualified Durable7.FingerTree.IntervalTree as IntervalTree
import qualified Durable7.FingerTree.PersistentChunkedBitSet as BitSet
import qualified Durable7.FingerTree.PrioritySearchQueue as PrioritySearch
import qualified Durable7.FingerTree.SortedBag as SortedBag
import qualified Durable7.FingerTree.SortedMap as SortedMap
import qualified Durable7.FingerTree.SortedSet as SortedSet

data CursorSearch cursor = CursorSearch
  { cursorFound :: !Bool
  , searchCursor :: cursor
  }
  deriving (Eq, Show)

data CursorInsert cursor = CursorInsert
  { cursorAdded :: !Bool
  , insertionCursor :: cursor
  }
  deriving (Eq, Show)

validPosition :: Int -> Int -> Bool
validPosition position total = position >= 0 && position <= total

-- Sorted bag

data SortedBagCursor a = SortedBagCursor !(SortedBag.SortedBag a) !Int
  deriving (Eq, Show)

sortedBagCursorAt :: Int -> SortedBag.SortedBag a -> Maybe (SortedBagCursor a)
sortedBagCursorAt position bag
  | validPosition position (SortedBag.count bag) = Just (SortedBagCursor bag position)
  | otherwise = Nothing

sortedBagLowerBoundCursor :: Ord a => a -> SortedBag.SortedBag a -> SortedBagCursor a
sortedBagLowerBoundCursor item bag = SortedBagCursor bag (SortedBag.countLessThan item bag)

sortedBagUpperBoundCursor :: Ord a => a -> SortedBag.SortedBag a -> SortedBagCursor a
sortedBagUpperBoundCursor item bag = SortedBagCursor bag (SortedBag.countAtMost item bag)

findSortedBagCursor :: Ord a => a -> SortedBag.SortedBag a -> CursorSearch (SortedBagCursor a)
findSortedBagCursor item bag = CursorSearch (SortedBag.member item bag) (sortedBagLowerBoundCursor item bag)

sortedBagCursorPosition :: SortedBagCursor a -> Int
sortedBagCursorPosition (SortedBagCursor _ position) = position

sortedBagPeekPrevious, sortedBagPeekNext :: SortedBagCursor a -> Maybe a
sortedBagPeekPrevious (SortedBagCursor bag position) = SortedBag.index (position - 1) bag
sortedBagPeekNext (SortedBagCursor bag position) = SortedBag.index position bag

sortedBagMovePrevious, sortedBagMoveNext :: SortedBagCursor a -> Maybe (SortedBagCursor a)
sortedBagMovePrevious (SortedBagCursor bag position) = sortedBagCursorAt (position - 1) bag
sortedBagMoveNext (SortedBagCursor bag position)
  | position == SortedBag.count bag = Nothing
  | otherwise = sortedBagCursorAt (position + 1) bag

sortedBagSeekRank :: Int -> SortedBagCursor a -> Maybe (SortedBagCursor a)
sortedBagSeekRank position (SortedBagCursor bag _) = sortedBagCursorAt position bag

sortedBagAdd :: Ord a => a -> SortedBagCursor a -> SortedBagCursor a
sortedBagAdd item (SortedBagCursor bag _) =
  SortedBagCursor (SortedBag.insert item bag) (SortedBag.countAtMost item bag + 1)

sortedBagDeletePrevious, sortedBagDeleteNext :: SortedBagCursor a -> Maybe (SortedBagCursor a)
sortedBagDeletePrevious (SortedBagCursor bag position) = do
  updated <- SortedBag.deleteAt (position - 1) bag
  pure (SortedBagCursor updated (position - 1))
sortedBagDeleteNext (SortedBagCursor bag position) = do
  updated <- SortedBag.deleteAt position bag
  pure (SortedBagCursor updated position)

sortedBagSnapshot :: SortedBagCursor a -> SortedBag.SortedBag a
sortedBagSnapshot (SortedBagCursor bag _) = bag

-- Sorted set

data SortedSetCursor a = SortedSetCursor !(SortedSet.SortedSet a) !Int
  deriving (Eq, Show)

sortedSetCursorAt :: Int -> SortedSet.SortedSet a -> Maybe (SortedSetCursor a)
sortedSetCursorAt position set
  | validPosition position (SortedSet.count set) = Just (SortedSetCursor set position)
  | otherwise = Nothing

sortedSetLowerBoundCursor :: Ord a => a -> SortedSet.SortedSet a -> SortedSetCursor a
sortedSetLowerBoundCursor item set = SortedSetCursor set (SortedSet.countLessThan item set)

sortedSetUpperBoundCursor :: Ord a => a -> SortedSet.SortedSet a -> SortedSetCursor a
sortedSetUpperBoundCursor item set = SortedSetCursor set (SortedSet.countAtMost item set)

findSortedSetCursor :: Ord a => a -> SortedSet.SortedSet a -> CursorSearch (SortedSetCursor a)
findSortedSetCursor item set = CursorSearch (SortedSet.member item set) (sortedSetLowerBoundCursor item set)

sortedSetCursorPosition :: SortedSetCursor a -> Int
sortedSetCursorPosition (SortedSetCursor _ position) = position

sortedSetPeekPrevious, sortedSetPeekNext :: SortedSetCursor a -> Maybe a
sortedSetPeekPrevious (SortedSetCursor set position) = SortedSet.index (position - 1) set
sortedSetPeekNext (SortedSetCursor set position) = SortedSet.index position set

sortedSetMovePrevious, sortedSetMoveNext :: SortedSetCursor a -> Maybe (SortedSetCursor a)
sortedSetMovePrevious (SortedSetCursor set position) = sortedSetCursorAt (position - 1) set
sortedSetMoveNext (SortedSetCursor set position)
  | position == SortedSet.count set = Nothing
  | otherwise = sortedSetCursorAt (position + 1) set

sortedSetSeekRank :: Int -> SortedSetCursor a -> Maybe (SortedSetCursor a)
sortedSetSeekRank position (SortedSetCursor set _) = sortedSetCursorAt position set

sortedSetAdd :: Ord a => a -> SortedSetCursor a -> SortedSetCursor a
sortedSetAdd item (SortedSetCursor set _) =
  SortedSetCursor (SortedSet.insert item set) (SortedSet.countLessThan item set + 1)

sortedSetDeletePrevious, sortedSetDeleteNext :: Ord a => SortedSetCursor a -> Maybe (SortedSetCursor a)
sortedSetDeletePrevious cursor@(SortedSetCursor set position) = do
  item <- sortedSetPeekPrevious cursor
  pure (SortedSetCursor (SortedSet.delete item set) (position - 1))
sortedSetDeleteNext cursor@(SortedSetCursor set position) = do
  item <- sortedSetPeekNext cursor
  pure (SortedSetCursor (SortedSet.delete item set) position)

sortedSetSnapshot :: SortedSetCursor a -> SortedSet.SortedSet a
sortedSetSnapshot (SortedSetCursor set _) = set

-- Sorted map

data SortedMapCursor k v = SortedMapCursor !(SortedMap.SortedMap k v) !Int
  deriving (Eq, Show)

sortedMapCursorAt :: Int -> SortedMap.SortedMap k v -> Maybe (SortedMapCursor k v)
sortedMapCursorAt position mapValue
  | validPosition position (SortedMap.count mapValue) = Just (SortedMapCursor mapValue position)
  | otherwise = Nothing

sortedMapLowerBoundCursor :: Ord k => k -> SortedMap.SortedMap k v -> SortedMapCursor k v
sortedMapLowerBoundCursor key mapValue = SortedMapCursor mapValue (SortedMap.countKeysLessThan key mapValue)

sortedMapUpperBoundCursor :: Ord k => k -> SortedMap.SortedMap k v -> SortedMapCursor k v
sortedMapUpperBoundCursor key mapValue = SortedMapCursor mapValue (SortedMap.countKeysAtMost key mapValue)

findSortedMapCursor :: Ord k => k -> SortedMap.SortedMap k v -> CursorSearch (SortedMapCursor k v)
findSortedMapCursor key mapValue = CursorSearch (SortedMap.member key mapValue) (sortedMapLowerBoundCursor key mapValue)

sortedMapCursorPosition :: SortedMapCursor k v -> Int
sortedMapCursorPosition (SortedMapCursor _ position) = position

sortedMapPeekPrevious, sortedMapPeekNext :: SortedMapCursor k v -> Maybe (k, v)
sortedMapPeekPrevious (SortedMapCursor mapValue position) = SortedMap.index (position - 1) mapValue
sortedMapPeekNext (SortedMapCursor mapValue position) = SortedMap.index position mapValue

sortedMapMovePrevious, sortedMapMoveNext :: SortedMapCursor k v -> Maybe (SortedMapCursor k v)
sortedMapMovePrevious (SortedMapCursor mapValue position) = sortedMapCursorAt (position - 1) mapValue
sortedMapMoveNext (SortedMapCursor mapValue position)
  | position == SortedMap.count mapValue = Nothing
  | otherwise = sortedMapCursorAt (position + 1) mapValue

sortedMapSeekRank :: Int -> SortedMapCursor k v -> Maybe (SortedMapCursor k v)
sortedMapSeekRank position (SortedMapCursor mapValue _) = sortedMapCursorAt position mapValue

sortedMapInsert :: Ord k => k -> v -> SortedMapCursor k v -> Maybe (SortedMapCursor k v)
sortedMapInsert key value (SortedMapCursor mapValue _) = do
  updated <- SortedMap.insertNew key value mapValue
  pure (SortedMapCursor updated (SortedMap.countKeysLessThan key mapValue + 1))

sortedMapTryInsert :: Ord k => k -> v -> SortedMapCursor k v -> CursorInsert (SortedMapCursor k v)
sortedMapTryInsert key value cursor@(SortedMapCursor mapValue _) =
  case sortedMapInsert key value cursor of
    Just updated -> CursorInsert True updated
    Nothing -> CursorInsert False (sortedMapLowerBoundCursor key mapValue)

sortedMapSetItem :: Ord k => k -> v -> SortedMapCursor k v -> SortedMapCursor k v
sortedMapSetItem key value (SortedMapCursor mapValue _) =
  SortedMapCursor (SortedMap.insert key value mapValue) position
  where
    lower = SortedMap.countKeysLessThan key mapValue
    position = lower + if SortedMap.member key mapValue then 0 else 1

sortedMapSetNextValue :: Ord k => v -> SortedMapCursor k v -> Maybe (SortedMapCursor k v)
sortedMapSetNextValue value cursor@(SortedMapCursor mapValue position) = do
  (key, _) <- sortedMapPeekNext cursor
  pure (SortedMapCursor (SortedMap.insert key value mapValue) position)

sortedMapDeletePrevious, sortedMapDeleteNext :: Ord k => SortedMapCursor k v -> Maybe (SortedMapCursor k v)
sortedMapDeletePrevious cursor@(SortedMapCursor mapValue position) = do
  (key, _) <- sortedMapPeekPrevious cursor
  pure (SortedMapCursor (SortedMap.delete key mapValue) (position - 1))
sortedMapDeleteNext cursor@(SortedMapCursor mapValue position) = do
  (key, _) <- sortedMapPeekNext cursor
  pure (SortedMapCursor (SortedMap.delete key mapValue) position)

sortedMapSnapshot :: SortedMapCursor k v -> SortedMap.SortedMap k v
sortedMapSnapshot (SortedMapCursor mapValue _) = mapValue

-- Canonical sorted set

data CanonicalSortedSetCursor a = CanonicalSortedSetCursor !(Canonical.CanonicalSortedSet a) !Int
  deriving (Show)

canonicalCursorAt :: Int -> Canonical.CanonicalSortedSet a -> Maybe (CanonicalSortedSetCursor a)
canonicalCursorAt position set
  | validPosition position (Canonical.count set) = Just (CanonicalSortedSetCursor set position)
  | otherwise = Nothing

canonicalLowerBoundCursor :: a -> Canonical.CanonicalSortedSet a -> CanonicalSortedSetCursor a
canonicalLowerBoundCursor item set = CanonicalSortedSetCursor set (Canonical.cursorBoundRank False item set)

canonicalUpperBoundCursor :: a -> Canonical.CanonicalSortedSet a -> CanonicalSortedSetCursor a
canonicalUpperBoundCursor item set = CanonicalSortedSetCursor set (Canonical.cursorBoundRank True item set)

findCanonicalCursor :: a -> Canonical.CanonicalSortedSet a -> CursorSearch (CanonicalSortedSetCursor a)
findCanonicalCursor item set = CursorSearch (Canonical.contains item set) (canonicalLowerBoundCursor item set)

canonicalCursorPosition :: CanonicalSortedSetCursor a -> Int
canonicalCursorPosition (CanonicalSortedSetCursor _ position) = position

canonicalPeekPrevious, canonicalPeekNext :: CanonicalSortedSetCursor a -> Maybe a
canonicalPeekPrevious (CanonicalSortedSetCursor set position) = Canonical.cursorIndex (position - 1) set
canonicalPeekNext (CanonicalSortedSetCursor set position) = Canonical.cursorIndex position set

canonicalMovePrevious, canonicalMoveNext :: CanonicalSortedSetCursor a -> Maybe (CanonicalSortedSetCursor a)
canonicalMovePrevious (CanonicalSortedSetCursor set position) = canonicalCursorAt (position - 1) set
canonicalMoveNext (CanonicalSortedSetCursor set position)
  | position == Canonical.count set = Nothing
  | otherwise = canonicalCursorAt (position + 1) set

canonicalSeekRank :: Int -> CanonicalSortedSetCursor a -> Maybe (CanonicalSortedSetCursor a)
canonicalSeekRank position (CanonicalSortedSetCursor set _) = canonicalCursorAt position set

canonicalAdd :: a -> CanonicalSortedSetCursor a -> Either Canonical.CanonicalSetError (CanonicalSortedSetCursor a)
canonicalAdd item (CanonicalSortedSetCursor set _) = do
  updated <- Canonical.insert item set
  pure (CanonicalSortedSetCursor updated (Canonical.cursorBoundRank False item set + 1))

canonicalDeletePrevious, canonicalDeleteNext :: CanonicalSortedSetCursor a -> Maybe (CanonicalSortedSetCursor a)
canonicalDeletePrevious cursor@(CanonicalSortedSetCursor set position) = do
  item <- canonicalPeekPrevious cursor
  pure (CanonicalSortedSetCursor (Canonical.delete item set) (position - 1))
canonicalDeleteNext cursor@(CanonicalSortedSetCursor set position) = do
  item <- canonicalPeekNext cursor
  pure (CanonicalSortedSetCursor (Canonical.delete item set) position)

canonicalSnapshot :: CanonicalSortedSetCursor a -> Canonical.CanonicalSortedSet a
canonicalSnapshot (CanonicalSortedSetCursor set _) = set

-- Priority-search queue

data PrioritySearchQueueCursor k p v = PrioritySearchQueueCursor !(PrioritySearch.PrioritySearchQueue k p v) !Int
  deriving (Eq, Show)

prioritySearchCursorAt :: Int -> PrioritySearch.PrioritySearchQueue k p v -> Maybe (PrioritySearchQueueCursor k p v)
prioritySearchCursorAt position queue
  | validPosition position (PrioritySearch.count queue) = Just (PrioritySearchQueueCursor queue position)
  | otherwise = Nothing

prioritySearchLowerBoundCursor :: Ord k => k -> PrioritySearch.PrioritySearchQueue k p v -> PrioritySearchQueueCursor k p v
prioritySearchLowerBoundCursor key queue = PrioritySearchQueueCursor queue (PrioritySearch.cursorBoundRank False key queue)

prioritySearchUpperBoundCursor :: Ord k => k -> PrioritySearch.PrioritySearchQueue k p v -> PrioritySearchQueueCursor k p v
prioritySearchUpperBoundCursor key queue = PrioritySearchQueueCursor queue (PrioritySearch.cursorBoundRank True key queue)

findPrioritySearchCursor :: Ord k => k -> PrioritySearch.PrioritySearchQueue k p v -> CursorSearch (PrioritySearchQueueCursor k p v)
findPrioritySearchCursor key queue = CursorSearch (PrioritySearch.member key queue) (prioritySearchLowerBoundCursor key queue)

prioritySearchMinimumCursor :: Ord k => PrioritySearch.PrioritySearchQueue k p v -> PrioritySearchQueueCursor k p v
prioritySearchMinimumCursor queue =
  case PrioritySearch.minimumEntry queue of
    Nothing -> PrioritySearchQueueCursor queue 0
    Just entry -> prioritySearchLowerBoundCursor (PrioritySearch.entryKey entry) queue

prioritySearchCursorPosition :: PrioritySearchQueueCursor k p v -> Int
prioritySearchCursorPosition (PrioritySearchQueueCursor _ position) = position

prioritySearchPeekPrevious, prioritySearchPeekNext :: PrioritySearchQueueCursor k p v -> Maybe (PrioritySearch.PrioritySearchEntry k p v)
prioritySearchPeekPrevious (PrioritySearchQueueCursor queue position) = PrioritySearch.cursorIndex (position - 1) queue
prioritySearchPeekNext (PrioritySearchQueueCursor queue position) = PrioritySearch.cursorIndex position queue

prioritySearchMovePrevious, prioritySearchMoveNext :: PrioritySearchQueueCursor k p v -> Maybe (PrioritySearchQueueCursor k p v)
prioritySearchMovePrevious (PrioritySearchQueueCursor queue position) = prioritySearchCursorAt (position - 1) queue
prioritySearchMoveNext (PrioritySearchQueueCursor queue position)
  | position == PrioritySearch.count queue = Nothing
  | otherwise = prioritySearchCursorAt (position + 1) queue

prioritySearchSeekRank :: Int -> PrioritySearchQueueCursor k p v -> Maybe (PrioritySearchQueueCursor k p v)
prioritySearchSeekRank position (PrioritySearchQueueCursor queue _) = prioritySearchCursorAt position queue

prioritySearchTryInsert :: (Ord k, Ord p, Eq v) => k -> p -> v -> PrioritySearchQueueCursor k p v -> CursorInsert (PrioritySearchQueueCursor k p v)
prioritySearchTryInsert key priority value (PrioritySearchQueueCursor queue _) =
  case PrioritySearch.insertNew key priority value queue of
    Just updated -> CursorInsert True (PrioritySearchQueueCursor updated (lower + 1))
    Nothing -> CursorInsert False (PrioritySearchQueueCursor queue lower)
  where
    lower = PrioritySearch.cursorBoundRank False key queue

prioritySearchSetItem :: (Ord k, Ord p, Eq v) => k -> p -> v -> PrioritySearchQueueCursor k p v -> PrioritySearchQueueCursor k p v
prioritySearchSetItem key priority value (PrioritySearchQueueCursor queue _) =
  PrioritySearchQueueCursor (PrioritySearch.setItem key priority value queue) position
  where
    lower = PrioritySearch.cursorBoundRank False key queue
    position = lower + if PrioritySearch.member key queue then 0 else 1

prioritySearchSetNext :: (Ord k, Ord p, Eq v) => p -> v -> PrioritySearchQueueCursor k p v -> Maybe (PrioritySearchQueueCursor k p v)
prioritySearchSetNext priority value cursor@(PrioritySearchQueueCursor queue position) = do
  entry <- prioritySearchPeekNext cursor
  pure (PrioritySearchQueueCursor (PrioritySearch.setItem (PrioritySearch.entryKey entry) priority value queue) position)

prioritySearchDeletePrevious, prioritySearchDeleteNext :: (Ord k, Ord p) => PrioritySearchQueueCursor k p v -> Maybe (PrioritySearchQueueCursor k p v)
prioritySearchDeletePrevious cursor@(PrioritySearchQueueCursor queue position) = do
  entry <- prioritySearchPeekPrevious cursor
  pure (PrioritySearchQueueCursor (PrioritySearch.delete (PrioritySearch.entryKey entry) queue) (position - 1))
prioritySearchDeleteNext cursor@(PrioritySearchQueueCursor queue position) = do
  entry <- prioritySearchPeekNext cursor
  pure (PrioritySearchQueueCursor (PrioritySearch.delete (PrioritySearch.entryKey entry) queue) position)

prioritySearchSnapshot :: PrioritySearchQueueCursor k p v -> PrioritySearch.PrioritySearchQueue k p v
prioritySearchSnapshot (PrioritySearchQueueCursor queue _) = queue

-- Interval tree

data IntervalTreeCursor a = IntervalTreeCursor !(IntervalTree a) !Int
  deriving (Eq, Show)

intervalTreeCursorAt :: Ord a => Int -> IntervalTree a -> Maybe (IntervalTreeCursor a)
intervalTreeCursorAt position tree
  | validPosition position (IntervalTree.count tree) = Just (IntervalTreeCursor tree position)
  | otherwise = Nothing

intervalTreeLowerBoundCursor :: Ord a => a -> IntervalTree a -> IntervalTreeCursor a
intervalTreeLowerBoundCursor lowValue tree = IntervalTreeCursor tree (IntervalTree.lowerBoundRank lowValue tree)

intervalTreeUpperBoundCursor :: Ord a => a -> IntervalTree a -> IntervalTreeCursor a
intervalTreeUpperBoundCursor lowValue tree = IntervalTreeCursor tree (IntervalTree.upperBoundRank lowValue tree)

findIntervalCursor :: Ord a => Interval a -> IntervalTree a -> CursorSearch (IntervalTreeCursor a)
findIntervalCursor interval tree =
  case firstRank matching [start .. IntervalTree.count tree - 1] of
    Just position -> CursorSearch True (IntervalTreeCursor tree position)
    Nothing -> CursorSearch False (IntervalTreeCursor tree start)
  where
    start = IntervalTree.lowerBoundRank (low interval) tree
    matching position = case IntervalTree.index position tree of
      Just stored -> low stored == low interval && high stored == high interval
      Nothing -> False

findOverlapCursor :: Ord a => Interval a -> IntervalTree a -> CursorSearch (IntervalTreeCursor a)
findOverlapCursor = findOverlapCursorFrom 0

findContainingCursor :: Ord a => a -> IntervalTree a -> CursorSearch (IntervalTreeCursor a)
findContainingCursor point = findOverlapCursor (Interval point point)

intervalTreeCursorPosition :: IntervalTreeCursor a -> Int
intervalTreeCursorPosition (IntervalTreeCursor _ position) = position

intervalTreePeekPrevious, intervalTreePeekNext :: Ord a => IntervalTreeCursor a -> Maybe (Interval a)
intervalTreePeekPrevious (IntervalTreeCursor tree position) = IntervalTree.index (position - 1) tree
intervalTreePeekNext (IntervalTreeCursor tree position) = IntervalTree.index position tree

intervalTreeMovePrevious, intervalTreeMoveNext :: Ord a => IntervalTreeCursor a -> Maybe (IntervalTreeCursor a)
intervalTreeMovePrevious (IntervalTreeCursor tree position) = intervalTreeCursorAt (position - 1) tree
intervalTreeMoveNext (IntervalTreeCursor tree position)
  | position == IntervalTree.count tree = Nothing
  | otherwise = intervalTreeCursorAt (position + 1) tree

intervalTreeSeekRank :: Ord a => Int -> IntervalTreeCursor a -> Maybe (IntervalTreeCursor a)
intervalTreeSeekRank position (IntervalTreeCursor tree _) = intervalTreeCursorAt position tree

intervalTreeSeekNextOverlap :: Ord a => Interval a -> IntervalTreeCursor a -> CursorSearch (IntervalTreeCursor a)
intervalTreeSeekNextOverlap probe (IntervalTreeCursor tree position) =
  findOverlapCursorFrom (min (IntervalTree.count tree) (position + 1)) probe tree

intervalTreeInsert :: Ord a => Interval a -> IntervalTreeCursor a -> IntervalTreeCursor a
intervalTreeInsert interval (IntervalTreeCursor tree _) =
  IntervalTreeCursor (IntervalTree.insert interval tree) (IntervalTree.lowerBoundRank (low interval) tree + 1)

intervalTreeDeletePrevious, intervalTreeDeleteNext :: Ord a => IntervalTreeCursor a -> Maybe (IntervalTreeCursor a)
intervalTreeDeletePrevious (IntervalTreeCursor tree position) = do
  updated <- IntervalTree.deleteAt (position - 1) tree
  pure (IntervalTreeCursor updated (position - 1))
intervalTreeDeleteNext (IntervalTreeCursor tree position) = do
  updated <- IntervalTree.deleteAt position tree
  pure (IntervalTreeCursor updated position)

intervalTreeSnapshot :: IntervalTreeCursor a -> IntervalTree a
intervalTreeSnapshot (IntervalTreeCursor tree _) = tree

findOverlapCursorFrom :: Ord a => Int -> Interval a -> IntervalTree a -> CursorSearch (IntervalTreeCursor a)
findOverlapCursorFrom start probe tree =
  case firstRank matching [start .. IntervalTree.count tree - 1] of
    Just position -> CursorSearch True (IntervalTreeCursor tree position)
    Nothing -> CursorSearch False (IntervalTreeCursor tree (IntervalTree.count tree))
  where
    matching position = case IntervalTree.index position tree of
      Just stored -> low stored <= high probe && low probe <= high stored
      Nothing -> False

firstRank :: (Int -> Bool) -> [Int] -> Maybe Int
firstRank _ [] = Nothing
firstRank predicate (position : rest)
  | predicate position = Just position
  | otherwise = firstRank predicate rest

-- Interval map

data IntervalMapCursor a v = IntervalMapCursor !(IntervalMap.IntervalMap a v) !Int
  deriving (Eq, Show)

intervalMapCursorAt :: Int -> IntervalMap.IntervalMap a v -> Maybe (IntervalMapCursor a v)
intervalMapCursorAt position mapValue
  | validPosition position (IntervalMap.size mapValue) = Just (IntervalMapCursor mapValue position)
  | otherwise = Nothing

intervalMapLowerBoundCursor :: Ord a => Interval a -> IntervalMap.IntervalMap a v -> IntervalMapCursor a v
intervalMapLowerBoundCursor interval mapValue = IntervalMapCursor mapValue (IntervalMap.lowerBoundRank interval mapValue)

intervalMapUpperBoundCursor :: Ord a => Interval a -> IntervalMap.IntervalMap a v -> IntervalMapCursor a v
intervalMapUpperBoundCursor interval mapValue = IntervalMapCursor mapValue (IntervalMap.upperBoundRank interval mapValue)

findIntervalMapCursor :: Ord a => Interval a -> IntervalMap.IntervalMap a v -> CursorSearch (IntervalMapCursor a v)
findIntervalMapCursor interval mapValue = CursorSearch (IntervalMap.member interval mapValue) (intervalMapLowerBoundCursor interval mapValue)

findIntervalMapOverlapCursor :: Ord a => Interval a -> IntervalMap.IntervalMap a v -> CursorSearch (IntervalMapCursor a v)
findIntervalMapOverlapCursor = findIntervalMapOverlapCursorFrom 0

findIntervalMapContainingCursor :: Ord a => a -> IntervalMap.IntervalMap a v -> CursorSearch (IntervalMapCursor a v)
findIntervalMapContainingCursor point = findIntervalMapOverlapCursor (Interval point point)

intervalMapCursorPosition :: IntervalMapCursor a v -> Int
intervalMapCursorPosition (IntervalMapCursor _ position) = position

intervalMapPeekPrevious, intervalMapPeekNext :: IntervalMapCursor a v -> Maybe (Interval a, v)
intervalMapPeekPrevious (IntervalMapCursor mapValue position) = IntervalMap.entryAt (position - 1) mapValue
intervalMapPeekNext (IntervalMapCursor mapValue position) = IntervalMap.entryAt position mapValue

intervalMapMovePrevious, intervalMapMoveNext :: IntervalMapCursor a v -> Maybe (IntervalMapCursor a v)
intervalMapMovePrevious (IntervalMapCursor mapValue position) = intervalMapCursorAt (position - 1) mapValue
intervalMapMoveNext (IntervalMapCursor mapValue position)
  | position == IntervalMap.size mapValue = Nothing
  | otherwise = intervalMapCursorAt (position + 1) mapValue

intervalMapSeekRank :: Int -> IntervalMapCursor a v -> Maybe (IntervalMapCursor a v)
intervalMapSeekRank position (IntervalMapCursor mapValue _) = intervalMapCursorAt position mapValue

intervalMapSeekNextOverlap :: Ord a => Interval a -> IntervalMapCursor a v -> CursorSearch (IntervalMapCursor a v)
intervalMapSeekNextOverlap probe (IntervalMapCursor mapValue position) =
  findIntervalMapOverlapCursorFrom (min (IntervalMap.size mapValue) (position + 1)) probe mapValue

intervalMapTryInsert :: Ord a => Interval a -> v -> IntervalMapCursor a v -> CursorInsert (IntervalMapCursor a v)
intervalMapTryInsert interval value (IntervalMapCursor mapValue _) =
  case IntervalMap.add interval value mapValue of
    Just updated -> CursorInsert True (IntervalMapCursor updated (lower + 1))
    Nothing -> CursorInsert False (IntervalMapCursor mapValue lower)
  where
    lower = IntervalMap.lowerBoundRank interval mapValue

intervalMapSetNextValue :: Ord a => v -> IntervalMapCursor a v -> Maybe (IntervalMapCursor a v)
intervalMapSetNextValue value cursor@(IntervalMapCursor mapValue position) = do
  (interval, _) <- intervalMapPeekNext cursor
  pure (IntervalMapCursor (IntervalMap.set interval value mapValue) position)

intervalMapDeletePrevious, intervalMapDeleteNext :: Ord a => IntervalMapCursor a v -> Maybe (IntervalMapCursor a v)
intervalMapDeletePrevious cursor@(IntervalMapCursor mapValue position) = do
  (interval, _) <- intervalMapPeekPrevious cursor
  pure (IntervalMapCursor (IntervalMap.delete interval mapValue) (position - 1))
intervalMapDeleteNext cursor@(IntervalMapCursor mapValue position) = do
  (interval, _) <- intervalMapPeekNext cursor
  pure (IntervalMapCursor (IntervalMap.delete interval mapValue) position)

intervalMapSnapshot :: IntervalMapCursor a v -> IntervalMap.IntervalMap a v
intervalMapSnapshot (IntervalMapCursor mapValue _) = mapValue

findIntervalMapOverlapCursorFrom :: Ord a => Int -> Interval a -> IntervalMap.IntervalMap a v -> CursorSearch (IntervalMapCursor a v)
findIntervalMapOverlapCursorFrom start probe mapValue =
  case firstRank matching [start .. IntervalMap.size mapValue - 1] of
    Just position -> CursorSearch True (IntervalMapCursor mapValue position)
    Nothing -> CursorSearch False (IntervalMapCursor mapValue (IntervalMap.size mapValue))
  where
    matching position = case IntervalMap.entryAt position mapValue of
      Just (stored, _) -> low stored <= high probe && low probe <= high stored
      Nothing -> False

-- Chunked bit set

data ChunkedBitSetCursor = ChunkedBitSetCursor !BitSet.PersistentChunkedBitSet !Int64

chunkedBitSetCursorAt :: Int64 -> BitSet.PersistentChunkedBitSet -> Maybe ChunkedBitSetCursor
chunkedBitSetCursorAt position set
  | position >= 0 && position <= BitSet.size set = Just (ChunkedBitSetCursor set position)
  | otherwise = Nothing

chunkedBitSetCursorAtOrAfter :: Int -> BitSet.PersistentChunkedBitSet -> ChunkedBitSetCursor
chunkedBitSetCursorAtOrAfter bitIndex set = ChunkedBitSetCursor set position
  where
    position = if bitIndex <= 0 then 0 else BitSet.rank (bitIndex - 1) set

findChunkedBitSetCursor :: Int -> BitSet.PersistentChunkedBitSet -> CursorSearch ChunkedBitSetCursor
findChunkedBitSetCursor bitIndex set = CursorSearch (BitSet.member bitIndex set) (chunkedBitSetCursorAtOrAfter bitIndex set)

chunkedBitSetCursorPosition :: ChunkedBitSetCursor -> Int64
chunkedBitSetCursorPosition (ChunkedBitSetCursor _ position) = position

chunkedBitSetPeekPrevious, chunkedBitSetPeekNext :: ChunkedBitSetCursor -> Maybe Int
chunkedBitSetPeekPrevious (ChunkedBitSetCursor set position) = BitSet.select (position - 1) set
chunkedBitSetPeekNext (ChunkedBitSetCursor set position) = BitSet.select position set

chunkedBitSetMovePrevious, chunkedBitSetMoveNext :: ChunkedBitSetCursor -> Maybe ChunkedBitSetCursor
chunkedBitSetMovePrevious (ChunkedBitSetCursor set position) = chunkedBitSetCursorAt (position - 1) set
chunkedBitSetMoveNext (ChunkedBitSetCursor set position)
  | position == BitSet.size set = Nothing
  | otherwise = chunkedBitSetCursorAt (position + 1) set

chunkedBitSetSeekRank :: Int64 -> ChunkedBitSetCursor -> Maybe ChunkedBitSetCursor
chunkedBitSetSeekRank position (ChunkedBitSetCursor set _) = chunkedBitSetCursorAt position set

chunkedBitSetInsert :: Int -> ChunkedBitSetCursor -> ChunkedBitSetCursor
chunkedBitSetInsert bitIndex cursor@(ChunkedBitSetCursor set _)
  | BitSet.member bitIndex set = cursor
  | otherwise = ChunkedBitSetCursor (BitSet.insert bitIndex set) (lower + 1)
  where
    lower = if bitIndex == 0 then 0 else BitSet.rank (bitIndex - 1) set

chunkedBitSetDeletePrevious, chunkedBitSetDeleteNext :: ChunkedBitSetCursor -> Maybe ChunkedBitSetCursor
chunkedBitSetDeletePrevious cursor@(ChunkedBitSetCursor set position) = do
  bitIndex <- chunkedBitSetPeekPrevious cursor
  pure (ChunkedBitSetCursor (BitSet.delete bitIndex set) (position - 1))
chunkedBitSetDeleteNext cursor@(ChunkedBitSetCursor set position) = do
  bitIndex <- chunkedBitSetPeekNext cursor
  pure (ChunkedBitSetCursor (BitSet.delete bitIndex set) position)

chunkedBitSetSnapshot :: ChunkedBitSetCursor -> BitSet.PersistentChunkedBitSet
chunkedBitSetSnapshot (ChunkedBitSetCursor set _) = set
