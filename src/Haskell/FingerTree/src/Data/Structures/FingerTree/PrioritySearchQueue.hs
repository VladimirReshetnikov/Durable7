{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MagicHash #-}

-- | A persistent ordered map whose AVL nodes cache the minimum-priority entry
-- in their subtree.  Priority ties are resolved by key order, so the minimum
-- is deterministic and deletion remains a keyed O(log n) operation.
module Data.Structures.FingerTree.PrioritySearchQueue
  ( PrioritySearchQueue
  , PrioritySearchEntry(..)
  , PrioritySearchQueueStatistics(..)
  , empty
  , singleton
  , fromList
  , count
  , height
  , null
  , member
  , lookup
  , setItem
  , insertNew
  , delete
  , deleteLookup
  , minimumEntry
  , minView
  , deleteMinimum
  , enumerateAtMost
  , toAscList
  , validateStructure
  , cursorBoundRank
  , cursorIndex
  ) where

import Prelude hiding (lookup, null)

import qualified Data.List as List
import GHC.Exts (isTrue#, reallyUnsafePtrEquality#)

sameValue :: Eq a => a -> a -> Bool
sameValue left right = isTrue# (reallyUnsafePtrEquality# left right) || left == right
{-# INLINE sameValue #-}

-- | One keyed payload and its ordering priority.  Lower priorities win; equal
-- priorities are ordered by 'entryKey'.
data PrioritySearchEntry k p v = PrioritySearchEntry
  { entryKey :: !k
  , entryPriority :: !p
  , entryValue :: v
  }
  deriving (Eq, Ord, Read, Show)

data Node k p v = Node
  { nodeCount :: !Int
  , nodeHeight :: !Int
  , nodeEntry :: !(PrioritySearchEntry k p v)
  , nodeWinner :: !(PrioritySearchEntry k p v)
  , nodeLeft :: !(Maybe (Node k p v))
  , nodeRight :: !(Maybe (Node k p v))
  }
  deriving (Read, Show)

-- | An immutable winner-cached AVL priority-search queue.
newtype PrioritySearchQueue k p v = PrioritySearchQueue (Maybe (Node k p v))
  deriving (Read, Show)

-- Extensional equality is intentionally independent of AVL sharing/history.
instance (Eq k, Eq p, Eq v) => Eq (PrioritySearchQueue k p v) where
  left == right = toAscList left == toAscList right

instance (Ord k, Ord p, Ord v) => Ord (PrioritySearchQueue k p v) where
  compare left right = compare (toAscList left) (toAscList right)

-- | Statistics returned by the full representation validator.
data PrioritySearchQueueStatistics = PrioritySearchQueueStatistics
  { psqStatisticsCount :: !Int
  , psqStatisticsHeight :: !Int
  , psqStatisticsMaximumAbsoluteBalance :: !Int
  }
  deriving (Eq, Ord, Read, Show)

empty :: PrioritySearchQueue k p v
empty = PrioritySearchQueue Nothing

singleton :: k -> p -> v -> PrioritySearchQueue k p v
singleton key priority value =
  PrioritySearchQueue (Just (leaf (PrioritySearchEntry key priority value)))

-- | Builds by repeated insertion with last-wins duplicate-key semantics.
fromList :: (Ord k, Ord p, Eq v) => [(k, p, v)] -> PrioritySearchQueue k p v
fromList = List.foldl' (\queue (key, priority, value) -> setItem key priority value queue) empty

count :: PrioritySearchQueue k p v -> Int
count (PrioritySearchQueue root) = countOf root

height :: PrioritySearchQueue k p v -> Int
height (PrioritySearchQueue root) = heightOf root

null :: PrioritySearchQueue k p v -> Bool
null (PrioritySearchQueue root) = case root of
  Nothing -> True
  Just _ -> False

member :: Ord k => k -> PrioritySearchQueue k p v -> Bool
member key queue = case lookup key queue of
  Nothing -> False
  Just _ -> True

lookup :: Ord k => k -> PrioritySearchQueue k p v -> Maybe (PrioritySearchEntry k p v)
lookup key (PrioritySearchQueue root) = go root
  where
    go Nothing = Nothing
    go (Just node) = case compare key (entryKey (nodeEntry node)) of
      LT -> go (nodeLeft node)
      GT -> go (nodeRight node)
      EQ -> Just (nodeEntry node)

-- | Adds or replaces an entry.  An equivalent key retains the originally
-- stored representative; an exact priority/value replacement is a no-op.
setItem :: (Ord k, Ord p, Eq v) => k -> p -> v -> PrioritySearchQueue k p v -> PrioritySearchQueue k p v
setItem key priority value (PrioritySearchQueue root) =
  PrioritySearchQueue (Just updated)
  where
    (updated, _, _) = setNode True (PrioritySearchEntry key priority value) root

-- | Adds an entry only when its key is absent.
insertNew :: (Ord k, Ord p, Eq v) => k -> p -> v -> PrioritySearchQueue k p v -> Maybe (PrioritySearchQueue k p v)
insertNew key priority value (PrioritySearchQueue root) =
  let (updated, added, _) = setNode False (PrioritySearchEntry key priority value) root
  in if added then Just (PrioritySearchQueue (Just updated)) else Nothing

delete :: (Ord k, Ord p) => k -> PrioritySearchQueue k p v -> PrioritySearchQueue k p v
delete key queue = snd (deleteLookup key queue)

deleteLookup :: (Ord k, Ord p) => k -> PrioritySearchQueue k p v -> (Maybe (PrioritySearchEntry k p v), PrioritySearchQueue k p v)
deleteLookup key queue@(PrioritySearchQueue root) =
  case deleteNode key root of
    (_, Nothing) -> (Nothing, queue)
    (updated, removed) -> (removed, PrioritySearchQueue updated)

-- | Retrieves the globally minimum-priority entry in O(1).
minimumEntry :: PrioritySearchQueue k p v -> Maybe (PrioritySearchEntry k p v)
minimumEntry (PrioritySearchQueue Nothing) = Nothing
minimumEntry (PrioritySearchQueue (Just root)) = Just (nodeWinner root)

-- | Removes and returns the globally minimum-priority entry in O(log n).
minView :: (Ord k, Ord p) => PrioritySearchQueue k p v -> Maybe (PrioritySearchEntry k p v, PrioritySearchQueue k p v)
minView queue = do
  winner <- minimumEntry queue
  let (removed, remaining) = deleteLookup (entryKey winner) queue
  case removed of
    Just entry -> Just (entry, remaining)
    Nothing -> Nothing

deleteMinimum :: (Ord k, Ord p) => PrioritySearchQueue k p v -> PrioritySearchQueue k p v
deleteMinimum queue = case minView queue of
  Nothing -> queue
  Just (_, remaining) -> remaining

-- | Enumerates entries in the inclusive key range whose priority is at most
-- the threshold.  Winner caches prune a subtree when even its minimum priority
-- exceeds the threshold.  Results remain in ascending key order.
enumerateAtMost :: (Ord k, Ord p) => k -> k -> p -> PrioritySearchQueue k p v -> Maybe [PrioritySearchEntry k p v]
enumerateAtMost minimumKey maximumKey maximumPriority (PrioritySearchQueue root)
  | minimumKey > maximumKey = Nothing
  | otherwise = Just (go root [])
  where
    go Nothing tailValues = tailValues
    go (Just node) tailValues
      | entryPriority (nodeWinner node) > maximumPriority = tailValues
      | key < minimumKey = go (nodeRight node) tailValues
      | key > maximumKey = go (nodeLeft node) tailValues
      | otherwise =
          go (nodeLeft node) (emit (go (nodeRight node) tailValues))
      where
        entry = nodeEntry node
        key = entryKey entry
        emit rest
          | entryPriority entry <= maximumPriority = entry : rest
          | otherwise = rest

toAscList :: PrioritySearchQueue k p v -> [PrioritySearchEntry k p v]
toAscList (PrioritySearchQueue root) = go root []
  where
    go Nothing tailValues = tailValues
    go (Just node) tailValues =
      go (nodeLeft node) (nodeEntry node : go (nodeRight node) tailValues)

-- | Population rank of the lower or upper key-order bound.
cursorBoundRank :: Ord k => Bool -> k -> PrioritySearchQueue k p v -> Int
cursorBoundRank upper key (PrioritySearchQueue root) = go 0 root
  where
    go !rank Nothing = rank
    go !rank (Just node) =
      case compare (entryKey (nodeEntry node)) key of
        LT -> go (rank + countOf (nodeLeft node) + 1) (nodeRight node)
        EQ | upper -> go (rank + countOf (nodeLeft node) + 1) (nodeRight node)
        _ -> go rank (nodeLeft node)

-- | Entry at a zero-based key-order population rank.
cursorIndex :: Int -> PrioritySearchQueue k p v -> Maybe (PrioritySearchEntry k p v)
cursorIndex position (PrioritySearchQueue root)
  | position < 0 = Nothing
  | otherwise = go position root
  where
    go _ Nothing = Nothing
    go remaining (Just node)
      | remaining < leftCount = go remaining (nodeLeft node)
      | remaining == leftCount = Just (nodeEntry node)
      | otherwise = go (remaining - leftCount - 1) (nodeRight node)
      where
        leftCount = countOf (nodeLeft node)

-- | Validates key order, AVL balance, cached count/height, and every cached
-- winner.  Returns 'Nothing' on any invariant violation.
validateStructure :: (Ord k, Ord p, Eq v) => PrioritySearchQueue k p v -> Maybe PrioritySearchQueueStatistics
validateStructure (PrioritySearchQueue Nothing) = Just (PrioritySearchQueueStatistics 0 0 0)
validateStructure (PrioritySearchQueue (Just root)) = do
  validated <- validateNode Nothing Nothing root
  if validatedCount validated /= nodeCount root || validatedHeight validated /= nodeHeight root
    then Nothing
    else Just (PrioritySearchQueueStatistics
      (validatedCount validated)
      (validatedHeight validated)
      (validatedMaximumBalance validated))

data Validated k p v = Validated
  { validatedCount :: !Int
  , validatedHeight :: !Int
  , validatedWinner :: !(PrioritySearchEntry k p v)
  , validatedMaximumBalance :: !Int
  }

validateNode :: (Ord k, Ord p, Eq v) => Maybe k -> Maybe k -> Node k p v -> Maybe (Validated k p v)
validateNode lower upper node
  | maybe False (key <=) lower = Nothing
  | maybe False (key >=) upper = Nothing
  | otherwise = do
      left <- traverse (validateNode lower (Just key)) (nodeLeft node)
      right <- traverse (validateNode (Just key) upper) (nodeRight node)
      let !expectedCount = 1 + maybe 0 validatedCount left + maybe 0 validatedCount right
          !expectedHeight = 1 + max (maybe 0 validatedHeight left) (maybe 0 validatedHeight right)
          !balance = maybe 0 validatedHeight left - maybe 0 validatedHeight right
          !expectedWinner = chooseWinner
            (nodeEntry node)
            (validatedWinner <$> left)
            (validatedWinner <$> right)
          !maximumBalance = maximum
            [ abs balance
            , maybe 0 validatedMaximumBalance left
            , maybe 0 validatedMaximumBalance right
            ]
      if abs balance > 1
          || nodeCount node /= expectedCount
          || nodeHeight node /= expectedHeight
          || nodeWinner node /= expectedWinner
        then Nothing
        else Just (Validated expectedCount expectedHeight expectedWinner maximumBalance)
  where
    key = entryKey (nodeEntry node)

countOf :: Maybe (Node k p v) -> Int
countOf Nothing = 0
countOf (Just node) = nodeCount node

heightOf :: Maybe (Node k p v) -> Int
heightOf Nothing = 0
heightOf (Just node) = nodeHeight node

leaf :: PrioritySearchEntry k p v -> Node k p v
leaf entry = Node 1 1 entry entry Nothing Nothing

mkNode :: (Ord k, Ord p) => PrioritySearchEntry k p v -> Maybe (Node k p v) -> Maybe (Node k p v) -> Node k p v
mkNode entry left right = Node totalCount totalHeight entry winner left right
  where
    totalCount = 1 + countOf left + countOf right
    totalHeight = 1 + max (heightOf left) (heightOf right)
    winner = chooseWinner entry (nodeWinner <$> left) (nodeWinner <$> right)

chooseWinner :: (Ord k, Ord p) => PrioritySearchEntry k p v -> Maybe (PrioritySearchEntry k p v) -> Maybe (PrioritySearchEntry k p v) -> PrioritySearchEntry k p v
chooseWinner entry left right = choose right (choose left entry)
  where
    choose Nothing current = current
    choose (Just candidate) current
      | entryBefore candidate current = candidate
      | otherwise = current

entryBefore :: (Ord k, Ord p) => PrioritySearchEntry k p v -> PrioritySearchEntry k p v -> Bool
entryBefore left right =
  entryPriority left < entryPriority right
    || (entryPriority left == entryPriority right && entryKey left < entryKey right)

setNode :: (Ord k, Ord p, Eq v) => Bool -> PrioritySearchEntry k p v -> Maybe (Node k p v) -> (Node k p v, Bool, Bool)
setNode _ entry Nothing = (leaf entry, True, True)
setNode overwrite entry (Just node) = case compare (entryKey entry) (entryKey (nodeEntry node)) of
  LT ->
    let (left, added, changed) = setNode overwrite entry (nodeLeft node)
    in if changed
         then (balanceNode (nodeEntry node) (Just left) (nodeRight node), added, True)
         else (node, added, False)
  GT ->
    let (right, added, changed) = setNode overwrite entry (nodeRight node)
    in if changed
         then (balanceNode (nodeEntry node) (nodeLeft node) (Just right), added, True)
         else (node, added, False)
  EQ
    | not overwrite -> (node, False, False)
    | sameValue (entryPriority entry) (entryPriority (nodeEntry node))
        && sameValue (entryValue entry) (entryValue (nodeEntry node)) -> (node, False, False)
    | otherwise ->
        let retained = PrioritySearchEntry
              (entryKey (nodeEntry node))
              (entryPriority entry)
              (entryValue entry)
        in (mkNode retained (nodeLeft node) (nodeRight node), False, True)

deleteNode :: (Ord k, Ord p) => k -> Maybe (Node k p v) -> (Maybe (Node k p v), Maybe (PrioritySearchEntry k p v))
deleteNode _ Nothing = (Nothing, Nothing)
deleteNode key (Just node) = case compare key (entryKey (nodeEntry node)) of
  LT -> case deleteNode key (nodeLeft node) of
    (_, Nothing) -> (Just node, Nothing)
    (left, removed) -> (Just (balanceNode (nodeEntry node) left (nodeRight node)), removed)
  GT -> case deleteNode key (nodeRight node) of
    (_, Nothing) -> (Just node, Nothing)
    (right, removed) -> (Just (balanceNode (nodeEntry node) (nodeLeft node) right), removed)
  EQ -> case (nodeLeft node, nodeRight node) of
    (Nothing, right) -> (right, Just (nodeEntry node))
    (left, Nothing) -> (left, Just (nodeEntry node))
    (left, Just rightNode) ->
      let successor = minimumKeyNode rightNode
          (right, _) = deleteNode (entryKey successor) (Just rightNode)
      in (Just (balanceNode successor left right), Just (nodeEntry node))

minimumKeyNode :: Node k p v -> PrioritySearchEntry k p v
minimumKeyNode node = case nodeLeft node of
  Nothing -> nodeEntry node
  Just left -> minimumKeyNode left

balanceNode :: (Ord k, Ord p) => PrioritySearchEntry k p v -> Maybe (Node k p v) -> Maybe (Node k p v) -> Node k p v
balanceNode entry left right
  | factor > 1 = case left of
      Just leftNode
        | heightOf (nodeLeft leftNode) < heightOf (nodeRight leftNode) ->
            rotateRight entry (Just (rotateLeft
              (nodeEntry leftNode)
              (nodeLeft leftNode)
              (nodeRight leftNode))) right
        | otherwise -> rotateRight entry left right
      Nothing -> mkNode entry left right
  | factor < -1 = case right of
      Just rightNode
        | heightOf (nodeRight rightNode) < heightOf (nodeLeft rightNode) ->
            rotateLeft entry left (Just (rotateRight
              (nodeEntry rightNode)
              (nodeLeft rightNode)
              (nodeRight rightNode)))
        | otherwise -> rotateLeft entry left right
      Nothing -> mkNode entry left right
  | otherwise = mkNode entry left right
  where
    factor = heightOf left - heightOf right

rotateLeft :: (Ord k, Ord p) => PrioritySearchEntry k p v -> Maybe (Node k p v) -> Maybe (Node k p v) -> Node k p v
rotateLeft entry left (Just pivot) =
  mkNode (nodeEntry pivot) (Just (mkNode entry left (nodeLeft pivot))) (nodeRight pivot)
rotateLeft entry left Nothing = mkNode entry left Nothing

rotateRight :: (Ord k, Ord p) => PrioritySearchEntry k p v -> Maybe (Node k p v) -> Maybe (Node k p v) -> Node k p v
rotateRight entry (Just pivot) right =
  mkNode (nodeEntry pivot) (nodeLeft pivot) (Just (mkNode entry (nodeRight pivot) right))
rotateRight entry Nothing right = mkNode entry Nothing right
