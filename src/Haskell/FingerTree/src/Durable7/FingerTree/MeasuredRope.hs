{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MultiParamTypeClasses #-}

-- | A chunked persistent sequence that also caches a monoidal measure, so a measured query descends
-- without visiting the elements it skips.
module Durable7.FingerTree.MeasuredRope
  ( MeasuredRope
  , MeasuredRopeCursor
  , MeasuredRopeCursorSearch
  , searchFound
  , searchCursor
  , emptyWith
  , singletonWith
  , fromListWith
  , toList
  , count
  , null
  , measure
  , prefixMeasure
  , append
  , index
  , setAt
  , insertAt
  , insertRangeAt
  , deleteAt
  , splitAt
  , slice
  , locateByMeasure
  , splitByMeasure
  , cursor
  , cursorAt
  , cursorByMeasure
  , cursorCount
  , cursorNull
  , cursorPosition
  , isAtStart
  , isAtEnd
  , measureBefore
  , measureAfter
  , peekPrevious
  , peekNext
  , movePrevious
  , moveNext
  , seek
  , seekByMeasure
  , insert
  , insertRange
  , deletePrevious
  , deleteNext
  , replaceNext
  , snapshot
  ) where

import Prelude hiding (null, splitAt)

import qualified Prelude as P
import qualified Data.List as List
import qualified Durable7.FingerTree.Measured as FT
import qualified Durable7.FingerTree.Rope as Rope

data RopeMeasure v = RopeMeasure !Int !v

instance Semigroup v => Semigroup (RopeMeasure v) where
  RopeMeasure leftCount left <> RopeMeasure rightCount right =
    checkedAdd leftCount rightCount `seq`
      RopeMeasure (leftCount + rightCount) (left <> right)

instance Monoid v => Monoid (RopeMeasure v) where
  mempty = RopeMeasure 0 mempty

data MeasuredChunk v a = MeasuredChunk !Int !v [a]

instance Monoid v => FT.Measured (RopeMeasure v) (MeasuredChunk v a) where
  measure (MeasuredChunk lengthValue value _) = RopeMeasure lengthValue value

-- | The function is retained for editing boundary chunks. Cached chunk
-- measures let splits, concatenation, prefix queries, and measure-guided
-- location reuse the persistent finger-tree spine.
data MeasuredRope v a = MeasuredRope (a -> v) !(FT.FingerTree (RopeMeasure v) (MeasuredChunk v a))

-- | An immutable measured editing cursor over one retained 'MeasuredRope'
-- snapshot. The strict position is a validated gap in @0 .. cursorCount@.
-- This is a snapshot-plus-gap semantic checkpoint, not the C# focused cursor representation.
data MeasuredRopeCursor v a = MeasuredRopeCursor !(MeasuredRope v a) !Int

-- | The result of an absolute measure-guided cursor search. A miss retains a
-- usable cursor at the end of the same immutable snapshot.
data MeasuredRopeCursorSearch v a = MeasuredRopeCursorSearch
  { searchFound :: !Bool
  , searchCursor :: !(MeasuredRopeCursor v a)
  }

-- | The empty rope under the given policy, which it retains.
emptyWith :: Monoid v => (a -> v) -> MeasuredRope v a
emptyWith elementMeasure = MeasuredRope elementMeasure FT.empty

-- | A rope holding one element, under the given policy.
singletonWith :: Monoid v => (a -> v) -> a -> MeasuredRope v a
singletonWith elementMeasure value =
  MeasuredRope elementMeasure (FT.singleton (measuredChunk elementMeasure [value]))

-- | A rope holding a list's elements, under the given policy.
fromListWith :: Monoid v => (a -> v) -> [a] -> MeasuredRope v a
fromListWith elementMeasure values =
  MeasuredRope elementMeasure (FT.fromList (map (measuredChunk elementMeasure) (chunkify values)))

-- | The elements, in the rope's own order.
toList :: Monoid v => MeasuredRope v a -> [a]
toList (MeasuredRope _ tree) = concatMap measuredChunkItems (FT.toList tree)

-- | Number of elements in the rope.
count :: Monoid v => MeasuredRope v a -> Int
count (MeasuredRope _ tree) = measureCount (FT.measureTree tree)

-- | Whether the rope holds no elements.
null :: Monoid v => MeasuredRope v a -> Bool
null (MeasuredRope _ tree) = FT.null tree

-- | The combined measure of every element, read from the cached root measure.
measure :: Monoid v => MeasuredRope v a -> v
measure (MeasuredRope _ tree) = measureValue (FT.measureTree tree)

-- | The combined measure of the elements before the position.
prefixMeasure :: Monoid v => Int -> MeasuredRope v a -> Maybe v
prefixMeasure lengthValue measured@(MeasuredRope elementMeasure tree)
  | lengthValue < 0 || lengthValue > count measured = Nothing
  | lengthValue == count measured = Just (measure measured)
  | otherwise = do
      (left, MeasuredChunk _ _ values, _) <- FT.split (past lengthValue) tree
      let local = lengthValue - treeCount left
      pure (treeValue left <> foldMap elementMeasure (take local values))

-- | Both operands must have extensionally identical element-measure functions.
-- Haskell functions have no decidable equality, so this is the same policy
-- precondition that callers already observe when constructing related ropes.
append :: Monoid v => MeasuredRope v a -> MeasuredRope v a -> MeasuredRope v a
append left@(MeasuredRope elementMeasure leftTree) right@(MeasuredRope _ rightTree) =
  checkedAdd (count left) (count right) `seq`
    if null left
      then right
      else if null right
        then left
        else MeasuredRope elementMeasure (FT.append leftTree rightTree)

-- | The element at the given rank.
index :: Monoid v => Int -> MeasuredRope v a -> Maybe a
index position measured@(MeasuredRope _ tree)
  | position < 0 || position >= count measured = Nothing
  | otherwise = do
      (left, MeasuredChunk _ _ values, _) <- FT.split (past position) tree
      listIndex (position - treeCount left) values

-- | The element at the given rank.
setAt :: Monoid v => Int -> a -> MeasuredRope v a -> Maybe (MeasuredRope v a)
setAt position value measured@(MeasuredRope elementMeasure tree)
  | position < 0 || position >= count measured = Nothing
  | otherwise = do
      (left, MeasuredChunk _ _ values, right) <- FT.split (past position) tree
      let local = position - treeCount left
          updated = measuredChunk elementMeasure (replaceAt local value values)
      pure (MeasuredRope elementMeasure (joinWithChunks left [updated] right))

-- | A rope with the element inserted at the position.
insertAt :: Monoid v => Int -> a -> MeasuredRope v a -> Maybe (MeasuredRope v a)
insertAt position value measured@(MeasuredRope elementMeasure tree)
  | position < 0 || position > count measured = Nothing
  | otherwise =
      checkedAdd (count measured) 1 `seq`
        if position == count measured
          then Just (MeasuredRope elementMeasure (appendValue elementMeasure tree value))
          else do
            (left, MeasuredChunk _ _ values, right) <- FT.split (past position) tree
            let local = position - treeCount left
                (before, after) = P.splitAt local values
                replacements = measuredChunks elementMeasure (before ++ value : after)
            pure (MeasuredRope elementMeasure (joinWithChunks left replacements right))

-- | Inserts a list at a validated gap. An empty list returns the exact source
-- rope. Count overflow is rejected before the element-measure function or
-- monoidal combination is invoked for the new values.
insertRangeAt :: Monoid v => Int -> [a] -> MeasuredRope v a -> Maybe (MeasuredRope v a)
insertRangeAt position values measured
  | position < 0 || position > count measured = Nothing
  | P.null values = Just measured
  | otherwise =
      let !added = checkedRangeLength (count measured) values
       in insertRangeAtKnown position values added measured

-- | A rope without the element at the position.
deleteAt :: Monoid v => Int -> MeasuredRope v a -> Maybe (MeasuredRope v a)
deleteAt position measured@(MeasuredRope elementMeasure tree)
  | position < 0 || position >= count measured = Nothing
  | otherwise = do
      (left, MeasuredChunk _ _ values, right) <- FT.split (past position) tree
      let local = position - treeCount left
          (before, after) = P.splitAt local values
          replacements = measuredChunks elementMeasure (before ++ drop 1 after)
      pure (MeasuredRope elementMeasure (joinWithChunks left replacements right))

-- | Splits into the elements before the position and those from it onward.
splitAt :: Monoid v => Int -> MeasuredRope v a -> Maybe (MeasuredRope v a, MeasuredRope v a)
splitAt position measured@(MeasuredRope elementMeasure tree)
  | position < 0 || position > count measured = Nothing
  | position == 0 = Just (emptyWith elementMeasure, measured)
  | position == count measured = Just (measured, emptyWith elementMeasure)
  | otherwise = do
      (left, MeasuredChunk _ _ values, right) <- FT.split (past position) tree
      let local = position - treeCount left
          (before, after) = P.splitAt local values
          leftTree = appendChunks left (measuredChunks elementMeasure before)
          rightTree = prependChunks (measuredChunks elementMeasure after) right
      pure (MeasuredRope elementMeasure leftTree, MeasuredRope elementMeasure rightTree)

-- | The elements in the given range.
slice :: Monoid v => Int -> Int -> MeasuredRope v a -> Maybe (MeasuredRope v a)
slice position lengthValue measured
  | not (isValidRange position lengthValue (count measured)) = Nothing
  | otherwise = do
      (_, suffix) <- splitAt position measured
      fst <$> splitAt lengthValue suffix

-- | The first position where the accumulated measure satisfies the predicate.
locateByMeasure :: Monoid v => (v -> Bool) -> MeasuredRope v a -> Maybe (Int, v, a)
locateByMeasure predicate (MeasuredRope elementMeasure tree) = do
  (left, MeasuredChunk _ _ values, _) <- FT.split (predicate . measureValue) tree
  let start = treeCount left
      beforeChunk = treeValue left
  locateInChunk elementMeasure predicate start beforeChunk values

-- | Splits at the first point where the accumulated measure satisfies the predicate, descending by
-- cached measures rather than scanning.
splitByMeasure :: Monoid v => (v -> Bool) -> MeasuredRope v a -> Maybe (MeasuredRope v a, a, MeasuredRope v a)
splitByMeasure predicate measured = do
  (position, _, value) <- locateByMeasure predicate measured
  (left, suffix) <- splitAt position measured
  (_, right) <- splitAt 1 suffix
  pure (left, value, right)

-- | Creates a measured cursor at the gap before the first element. O(1).
cursor :: MeasuredRope v a -> MeasuredRopeCursor v a
cursor rope = MeasuredRopeCursor rope 0

-- | Creates a measured cursor at a gap in @0 .. count rope@. O(1).
cursorAt :: Monoid v => Int -> MeasuredRope v a -> Maybe (MeasuredRopeCursor v a)
cursorAt position rope
  | position < 0 || position > count rope = Nothing
  | otherwise = Just (MeasuredRopeCursor rope position)

-- | Locates the gap immediately before the first element whose inclusive
-- absolute prefix satisfies a lawful monotone predicate. A miss, including
-- an empty rope, returns the end cursor with 'searchFound' set to 'False'.
cursorByMeasure :: Monoid v => (v -> Bool) -> MeasuredRope v a -> MeasuredRopeCursorSearch v a
cursorByMeasure predicate rope = searchFromCursor predicate (cursor rope)

-- | Returns the retained snapshot's element count. O(1).
cursorCount :: Monoid v => MeasuredRopeCursor v a -> Int
cursorCount (MeasuredRopeCursor rope _) = count rope

-- | Reports whether the retained measured snapshot is empty. O(1).
cursorNull :: Monoid v => MeasuredRopeCursor v a -> Bool
cursorNull (MeasuredRopeCursor rope _) = null rope

-- | Returns the validated gap position. O(1).
cursorPosition :: MeasuredRopeCursor v a -> Int
cursorPosition (MeasuredRopeCursor _ position) = position

-- | Reports whether the gap precedes the first element. O(1).
isAtStart :: MeasuredRopeCursor v a -> Bool
isAtStart cursorValue = cursorPosition cursorValue == 0

-- | Reports whether the gap follows the last element. O(1).
isAtEnd :: Monoid v => MeasuredRopeCursor v a -> Bool
isAtEnd cursorValue = cursorPosition cursorValue == cursorCount cursorValue

-- | Returns the ordered measure of @[0, cursorPosition)@. O(log n) plus one
-- bounded chunk scan; the retained element-measure function may be invoked.
measureBefore :: Monoid v => MeasuredRopeCursor v a -> v
measureBefore (MeasuredRopeCursor rope position) =
  case prefixMeasure position rope of
    Just value -> value
    Nothing -> error "Durable7.FingerTree.MeasuredRope.measureBefore: invalid cursor invariant"

-- | Returns the ordered measure of @[cursorPosition, cursorCount)@. O(log n)
-- plus one bounded chunk scan; the retained element-measure function may be
-- invoked.
measureAfter :: Monoid v => MeasuredRopeCursor v a -> v
measureAfter (MeasuredRopeCursor rope position) =
  case suffixMeasure position rope of
    Just value -> value
    Nothing -> error "Durable7.FingerTree.MeasuredRope.measureAfter: invalid cursor invariant"

-- | Returns the element immediately before the gap, if one exists. O(log n).
peekPrevious :: Monoid v => MeasuredRopeCursor v a -> Maybe a
peekPrevious cursorValue@(MeasuredRopeCursor rope position)
  | isAtStart cursorValue = Nothing
  | otherwise = index (position - 1) rope

-- | Returns the element immediately after the gap, if one exists. O(log n).
peekNext :: Monoid v => MeasuredRopeCursor v a -> Maybe a
peekNext cursorValue@(MeasuredRopeCursor rope position)
  | isAtEnd cursorValue = Nothing
  | otherwise = index position rope

-- | Moves one gap toward the start, or returns 'Nothing' at the start. O(1).
movePrevious :: MeasuredRopeCursor v a -> Maybe (MeasuredRopeCursor v a)
movePrevious cursorValue@(MeasuredRopeCursor rope position)
  | isAtStart cursorValue = Nothing
  | otherwise = Just (MeasuredRopeCursor rope (position - 1))

-- | Moves one gap toward the end, or returns 'Nothing' at the end. O(1).
moveNext :: Monoid v => MeasuredRopeCursor v a -> Maybe (MeasuredRopeCursor v a)
moveNext cursorValue@(MeasuredRopeCursor rope position)
  | isAtEnd cursorValue = Nothing
  | otherwise = Just (MeasuredRopeCursor rope (position + 1))

-- | Moves to a validated absolute gap. Seeking to the current gap preserves
-- the unchanged cursor value and retained snapshot. O(1).
seek :: Monoid v => Int -> MeasuredRopeCursor v a -> Maybe (MeasuredRopeCursor v a)
seek position cursorValue@(MeasuredRopeCursor rope _)
  | position < 0 || position > cursorCount cursorValue = Nothing
  | position == cursorPosition cursorValue = Just cursorValue
  | otherwise = Just (MeasuredRopeCursor rope position)

-- | Performs an absolute measure seek over the whole retained version. The
-- predicate must be lawful and monotone over inclusive prefixes. A miss
-- returns the end cursor, and a hit at the current gap preserves this cursor.
seekByMeasure :: Monoid v => (v -> Bool) -> MeasuredRopeCursor v a -> MeasuredRopeCursorSearch v a
seekByMeasure = searchFromCursor

-- | Inserts one element at the gap and returns the gap after it. O(log n)
-- plus bounded chunk work.
insert :: Monoid v => a -> MeasuredRopeCursor v a -> MeasuredRopeCursor v a
insert value (MeasuredRopeCursor rope position) =
  let !nextPosition = checkedAdd position 1
   in case insertAt position value rope of
        Just edited -> MeasuredRopeCursor edited nextPosition
        Nothing -> error "Durable7.FingerTree.MeasuredRope.insert: invalid cursor invariant"

-- | Inserts a list at the gap in order and returns the gap after it. An empty
-- list preserves the unchanged cursor and exact retained snapshot. Inserting
-- @m@ elements costs O(m + log n).
insertRange :: Monoid v => [a] -> MeasuredRopeCursor v a -> MeasuredRopeCursor v a
insertRange [] cursorValue = cursorValue
insertRange values (MeasuredRopeCursor rope position) =
  let !added = checkedRangeLength (count rope) values
      !nextPosition = checkedAdd position added
   in case insertRangeAtKnown position values added rope of
        Just edited -> MeasuredRopeCursor edited nextPosition
        Nothing -> error "Durable7.FingerTree.MeasuredRope.insertRange: invalid cursor invariant"

-- | Deletes the element before the gap and moves left, or returns 'Nothing'
-- at the start. O(log n) plus bounded chunk work.
deletePrevious :: Monoid v => MeasuredRopeCursor v a -> Maybe (MeasuredRopeCursor v a)
deletePrevious cursorValue@(MeasuredRopeCursor rope position)
  | isAtStart cursorValue = Nothing
  | otherwise = MeasuredRopeCursor <$> deleteAt (position - 1) rope <*> pure (position - 1)

-- | Deletes the element after the gap without moving it, or returns 'Nothing'
-- at the end. O(log n) plus bounded chunk work.
deleteNext :: Monoid v => MeasuredRopeCursor v a -> Maybe (MeasuredRopeCursor v a)
deleteNext cursorValue@(MeasuredRopeCursor rope position)
  | isAtEnd cursorValue = Nothing
  | otherwise = MeasuredRopeCursor <$> deleteAt position rope <*> pure position

-- | Unconditionally replaces the element after the gap without moving it, or
-- returns 'Nothing' at the end. No element equality is required or consulted.
replaceNext :: Monoid v => a -> MeasuredRopeCursor v a -> Maybe (MeasuredRopeCursor v a)
replaceNext value cursorValue@(MeasuredRopeCursor rope position)
  | isAtEnd cursorValue = Nothing
  | otherwise = MeasuredRopeCursor <$> setAt position value rope <*> pure position

-- | Returns the exact immutable measured-rope snapshot retained by the
-- cursor. O(1).
snapshot :: MeasuredRopeCursor v a -> MeasuredRope v a
snapshot (MeasuredRopeCursor rope _) = rope

measureCount :: RopeMeasure v -> Int
measureCount (RopeMeasure total _) = total

measureValue :: RopeMeasure v -> v
measureValue (RopeMeasure _ value) = value

treeCount :: Monoid v => FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> Int
treeCount = measureCount . FT.measureTree

treeValue :: Monoid v => FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> v
treeValue = measureValue . FT.measureTree

suffixMeasure :: Monoid v => Int -> MeasuredRope v a -> Maybe v
suffixMeasure position measured@(MeasuredRope elementMeasure tree)
  | position < 0 || position > count measured = Nothing
  | position == 0 = Just (measure measured)
  | position == count measured = Just mempty
  | otherwise = do
      (left, MeasuredChunk _ _ values, right) <- FT.split (past position) tree
      let local = position - treeCount left
      pure (foldMap elementMeasure (drop local values) <> treeValue right)

past :: Int -> RopeMeasure v -> Bool
past position (RopeMeasure seen _) = seen > position

measuredChunk :: Monoid v => (a -> v) -> [a] -> MeasuredChunk v a
measuredChunk elementMeasure values = MeasuredChunk (length values) (foldMap elementMeasure values) values

measuredChunkItems :: MeasuredChunk v a -> [a]
measuredChunkItems (MeasuredChunk _ _ values) = values

measuredChunks :: Monoid v => (a -> v) -> [a] -> [MeasuredChunk v a]
measuredChunks elementMeasure = map (measuredChunk elementMeasure) . chunkify

appendValue :: Monoid v => (a -> v) -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> a -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a)
appendValue elementMeasure tree value =
  case FT.viewR tree of
    FT.EmptyR -> FT.singleton (measuredChunk elementMeasure [value])
    rest FT.:> chunk@(MeasuredChunk lengthValue _ values)
      | lengthValue < Rope.maxChunkSize -> FT.snoc rest (measuredChunk elementMeasure (values ++ [value]))
      | otherwise -> FT.snoc (FT.snoc rest chunk) (measuredChunk elementMeasure [value])

appendChunks :: Monoid v => FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> [MeasuredChunk v a] -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a)
appendChunks = List.foldl' FT.snoc

prependChunks :: Monoid v => [MeasuredChunk v a] -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a)
prependChunks values tree = foldr FT.cons tree values

joinWithChunks :: Monoid v => FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> [MeasuredChunk v a] -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a)
joinWithChunks left middle right = FT.append (appendChunks left middle) right

insertRangeAtKnown :: Monoid v => Int -> [a] -> Int -> MeasuredRope v a -> Maybe (MeasuredRope v a)
insertRangeAtKnown position values added measured@(MeasuredRope elementMeasure _) =
  checkedAdd (count measured) added `seq`
    let !middle = fromListWith elementMeasure values
     in insertMeasuredRopeAt position middle measured

insertMeasuredRopeAt :: Monoid v => Int -> MeasuredRope v a -> MeasuredRope v a -> Maybe (MeasuredRope v a)
insertMeasuredRopeAt _ middle measured | null middle = Just measured
insertMeasuredRopeAt position middle measured = do
  (left, right) <- splitAt position measured
  pure (append (append left middle) right)

searchFromCursor :: Monoid v => (v -> Bool) -> MeasuredRopeCursor v a -> MeasuredRopeCursorSearch v a
searchFromCursor predicate cursorValue@(MeasuredRopeCursor rope currentPosition) =
  case locateByMeasure predicate rope of
    Just (position, _, _) -> MeasuredRopeCursorSearch True (destination position)
    Nothing -> MeasuredRopeCursorSearch False (destination (count rope))
  where
    destination position
      | position == currentPosition = cursorValue
      | otherwise = MeasuredRopeCursor rope position

locateInChunk :: Monoid v => (a -> v) -> (v -> Bool) -> Int -> v -> [a] -> Maybe (Int, v, a)
locateInChunk elementMeasure predicate = go
  where
    go _ _ [] = Nothing
    go !position !before (value : rest)
      | predicate after = Just (position, before, value)
      | otherwise = go (position + 1) after rest
      where
        after = before <> elementMeasure value

chunkify :: [a] -> [[a]]
chunkify [] = []
chunkify values =
  let (chunk, rest) = P.splitAt Rope.maxChunkSize values
   in chunk : chunkify rest

replaceAt :: Int -> a -> [a] -> [a]
replaceAt 0 value (_ : rest) = value : rest
replaceAt position value (x : rest) = x : replaceAt (position - 1) value rest
replaceAt _ _ [] = []

listIndex :: Int -> [a] -> Maybe a
listIndex 0 (value : _) = Just value
listIndex position (_ : rest)
  | position > 0 = listIndex (position - 1) rest
listIndex _ _ = Nothing

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position

checkedRangeLength :: Int -> [a] -> Int
checkedRangeLength initial = go initial 0
  where
    go !_ !added [] = added
    go !total !added (_ : rest) =
      let !nextTotal = checkedAdd total 1
          !nextAdded = checkedAdd added 1
       in go nextTotal nextAdded rest

checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | left < 0 || right < 0 || right > maxBound - left =
      error "Durable7.FingerTree.MeasuredRope: length overflow"
  | otherwise = left + right
