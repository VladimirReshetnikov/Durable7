{-# LANGUAGE FlexibleInstances #-}
{-# LANGUAGE MultiParamTypeClasses #-}

-- | A persistent catenable deque: push, pop, concatenate, and split at either end.
--
-- Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
-- so an edit copies a path rather than the whole collection.
module Durable7.FingerTree.Deque
  ( Deque
  , Cursor
  , SearchResult(..)
  , empty
  , singleton
  , fromList
  , toList
  , count
  , null
  , cons
  , snoc
  , append
  , viewL
  , viewR
  , first
  , last
  , index
  , setAt
  , updateAt
  , insertAt
  , deleteAt
  , splitAt
  , slice
  , removeRange
  , cursor
  , cursorAt
  , cursorPosition
  , cursorCount
  , cursorIsAtStart
  , cursorIsAtEnd
  , cursorPeekPrevious
  , cursorPeekNext
  , cursorMovePrevious
  , cursorMoveNext
  , cursorSeek
  , cursorInsert
  , cursorInsertList
  , cursorDeletePrevious
  , cursorDeleteNext
  , cursorReplaceNext
  , cursorSnapshot
  , sortedLowerBound
  , sortedLowerBoundBy
  , sortedUpperBound
  , sortedUpperBoundBy
  , sortedBinarySearch
  , sortedBinarySearchBy
  , insertSorted
  , insertSortedBy
  , removeAllSorted
  , removeAllSortedBy
  ) where

import Prelude hiding (last, null, splitAt)

import qualified Durable7.FingerTree.Measured as FT
import Durable7.FingerTree.Measured (Measured(..), ViewL(..), ViewR(..))
import Durable7.FingerTree.Measures (Elem(..))

data DequeMeasure a = DequeMeasure !Int !(Maybe a)
  deriving (Eq, Ord, Read, Show)

instance Semigroup (DequeMeasure a) where
  DequeMeasure leftCount leftLast <> DequeMeasure rightCount rightLast =
    DequeMeasure (checkedAdd leftCount rightCount) (case rightLast of Just _ -> rightLast; Nothing -> leftLast)

instance Monoid (DequeMeasure a) where
  mempty = DequeMeasure 0 Nothing

instance Measured (DequeMeasure a) (Elem a) where
  measure (Elem value) = DequeMeasure 1 (Just value)

-- | A persistent catenable deque. Every operation returns a new version and leaves its input valid,
-- sharing unchanged structure, so an edit copies a path rather than the sequence.
newtype Deque a = Deque (FT.FingerTree (DequeMeasure a) (Elem a))
  deriving (Show)

-- | Immutable root-plus-position gap cursor over a persistent deque.
data Cursor a = Cursor !(Deque a) !Int
  deriving (Show)

-- Extensional equality: two deques are equal exactly when they contain the
-- same element sequence, regardless of internal tree shape (a derived
-- structural instance distinguishes shape-different equal sequences).
instance Eq a => Eq (Deque a) where
  left == right = count left == count right && toList left == toList right

instance Ord a => Ord (Deque a) where
  compare left right = compare (toList left) (toList right)

-- | Where a measured search landed: the elements before it, the element found, and those after.
data SearchResult
  = Found !Int
  | Missing !Int
  deriving (Eq, Ord, Read, Show)

-- | The empty deque.
empty :: Deque a
empty = Deque FT.empty

-- | A deque holding one element.
singleton :: a -> Deque a
singleton value = Deque (FT.singleton (Elem value))

-- | A deque holding a list's elements, built in bulk rather than by repeated insertion.
fromList :: [a] -> Deque a
fromList values = Deque (FT.fromList (map Elem values))

-- | The elements, in the deque's own order.
toList :: Deque a -> [a]
toList (Deque tree) = map getElem (FT.toList tree)

-- | Number of elements in the deque.
count :: Deque a -> Int
count (Deque tree) = measureCount (FT.measureTree tree)

-- | Whether the deque holds no elements.
null :: Deque a -> Bool
null deque = count deque == 0

-- | A deque with the element added at the front.
cons :: a -> Deque a -> Deque a
cons value (Deque tree) = Deque (FT.cons (Elem value) tree)

-- | A deque with the element added at the back.
snoc :: Deque a -> a -> Deque a
snoc (Deque tree) value = Deque (FT.snoc tree (Elem value))

-- | The concatenation of two deques, sharing both operands' unchanged structure.
append :: Deque a -> Deque a -> Deque a
append (Deque left) (Deque right) = Deque (FT.append left right)

-- | The leftmost element together with the rest, or the empty view.
viewL :: Deque a -> Maybe (a, Deque a)
viewL (Deque tree) =
  case FT.viewL tree of
    EmptyL -> Nothing
    Elem value :< rest -> Just (value, Deque rest)

-- | The rightmost element together with the rest, or the empty view.
viewR :: Deque a -> Maybe (Deque a, a)
viewR (Deque tree) =
  case FT.viewR tree of
    EmptyR -> Nothing
    rest :> Elem value -> Just (Deque rest, value)

-- | The first element.
first :: Deque a -> Maybe a
first deque = fst <$> viewL deque

-- | The last element.
last :: Deque a -> Maybe a
last deque = snd <$> viewR deque

-- | The element at the given rank.
index :: Int -> Deque a -> Maybe a
index position (Deque tree)
  | position < 0 || position >= measureCount (FT.measureTree tree) = Nothing
  | otherwise =
      case FT.split (\measureValue -> measureCount measureValue > position) tree of
        Just (_, Elem value, _) -> Just value
        Nothing -> Nothing

-- | The element at the given rank.
setAt :: Int -> a -> Deque a -> Maybe (Deque a)
setAt position value deque = updateAt position (const value) deque

-- | A deque with the element at the position replaced.
updateAt :: Int -> (a -> a) -> Deque a -> Maybe (Deque a)
updateAt position updater deque@(Deque tree)
  | position < 0 || position >= count deque = Nothing
  | otherwise =
      case FT.split (\measureValue -> measureCount measureValue > position) tree of
        Just (left, Elem old, right) -> Just (Deque (FT.append (FT.snoc left (Elem (updater old))) right))
        Nothing -> Nothing

-- | A deque with the element inserted at the position.
insertAt :: Int -> a -> Deque a -> Maybe (Deque a)
insertAt position value deque
  | position < 0 || position > count deque = Nothing
  | otherwise =
      case splitAt position deque of
        Just (left, right) -> Just (append (snoc left value) right)
        Nothing -> Nothing

-- | A deque without the element at the position.
deleteAt :: Int -> Deque a -> Maybe (Deque a)
deleteAt position deque@(Deque tree)
  | position < 0 || position >= count deque = Nothing
  | otherwise =
      case FT.split (\measureValue -> measureCount measureValue > position) tree of
        Just (left, _, right) -> Just (Deque (FT.append left right))
        Nothing -> Nothing

-- | Splits into the elements before the position and those from it onward.
splitAt :: Int -> Deque a -> Maybe (Deque a, Deque a)
splitAt position deque@(Deque tree)
  | position < 0 || position > total = Nothing
  | position == 0 = Just (empty, deque)
  | position == total = Just (deque, empty)
  | otherwise =
      case FT.split (\measureValue -> measureCount measureValue > position) tree of
        Just (left, value, right) -> Just (Deque left, Deque (FT.cons value right))
        Nothing -> Nothing
  where
    total = count deque

-- | The elements in the given range.
slice :: Int -> Int -> Deque a -> Maybe (Deque a)
slice position lengthValue deque
  | lengthValue < 0 = Nothing
  | otherwise =
      case splitAt position deque of
        Just (_, suffix) -> fst <$> splitAt lengthValue suffix
        Nothing -> Nothing

-- | A deque without the elements in the range.
removeRange :: Int -> Int -> Deque a -> Maybe (Deque a)
removeRange position lengthValue deque
  | lengthValue < 0 = Nothing
  | otherwise =
      case splitAt position deque of
        Just (prefix, suffix) ->
          case splitAt lengthValue suffix of
            Just (_, tailValue) -> Just (append prefix tailValue)
            Nothing -> Nothing
        Nothing -> Nothing

-- | A cursor before the first element.
cursor :: Deque a -> Cursor a
cursor deque = Cursor deque 0

-- | The element at the given rank.
cursorAt :: Int -> Deque a -> Maybe (Cursor a)
cursorAt position deque
  | position < 0 || position > count deque = Nothing
  | otherwise = Just (Cursor deque position)

-- | The cursor's gap position.
cursorPosition :: Cursor a -> Int
cursorPosition (Cursor _ position) = position

-- | Number of elements in the deque version the cursor is positioned in.
cursorCount :: Cursor a -> Int
cursorCount (Cursor deque _) = count deque

-- | Whether the gap precedes the first element.
cursorIsAtStart :: Cursor a -> Bool
cursorIsAtStart value = cursorPosition value == 0

-- | Whether the gap follows the last element.
cursorIsAtEnd :: Cursor a -> Bool
cursorIsAtEnd value = cursorPosition value == cursorCount value

-- | The element immediately before the gap, or `Nothing` at the start.
cursorPeekPrevious :: Cursor a -> Maybe a
cursorPeekPrevious (Cursor deque position) = index (position - 1) deque

-- | The element immediately after the gap, or `Nothing` at the end.
cursorPeekNext :: Cursor a -> Maybe a
cursorPeekNext (Cursor deque position) = index position deque

-- | A cursor one position earlier. The receiver is unchanged.
cursorMovePrevious :: Cursor a -> Maybe (Cursor a)
cursorMovePrevious (Cursor deque position) = cursorAt (position - 1) deque

-- | A cursor one position later. The receiver is unchanged.
cursorMoveNext :: Cursor a -> Maybe (Cursor a)
cursorMoveNext (Cursor deque position)
  | position == count deque = Nothing
  | otherwise = cursorAt (position + 1) deque

-- | A cursor at the given position within the same deque version.
cursorSeek :: Int -> Cursor a -> Maybe (Cursor a)
cursorSeek position (Cursor deque _) = cursorAt position deque

-- | A deque containing the given element.
cursorInsert :: a -> Cursor a -> Cursor a
cursorInsert value (Cursor deque position) =
  Cursor (expectCursorEdit "insert" (insertAt position value deque)) (position + 1)

-- | Inserts a list's elements at the gap, producing a new version the returned cursor is positioned
-- in.
cursorInsertList :: [a] -> Cursor a -> Cursor a
cursorInsertList [] value = value
cursorInsertList values (Cursor deque position) =
  case splitAt position deque of
    Just (left, right) -> Cursor (append (append left (fromList values)) right) (position + length values)
    Nothing -> error "Durable7.FingerTree.Deque.cursorInsertList: invalid cursor"

-- | Removes the element before the gap, producing a new version the returned cursor is positioned
-- in.
cursorDeletePrevious :: Cursor a -> Maybe (Cursor a)
cursorDeletePrevious (Cursor deque position)
  | position == 0 = Nothing
  | otherwise = Just (Cursor (expectCursorEdit "delete previous" (deleteAt (position - 1) deque)) (position - 1))

-- | Removes the element after the gap, producing a new version the returned cursor is positioned
-- in.
cursorDeleteNext :: Cursor a -> Maybe (Cursor a)
cursorDeleteNext (Cursor deque position)
  | position == count deque = Nothing
  | otherwise = Just (Cursor (expectCursorEdit "delete next" (deleteAt position deque)) position)

-- | Replaces the element after the gap, producing a new version the returned cursor is positioned
-- in.
cursorReplaceNext :: a -> Cursor a -> Maybe (Cursor a)
cursorReplaceNext value (Cursor deque position)
  | position == count deque = Nothing
  | otherwise = Just (Cursor (expectCursorEdit "replace next" (setAt position value deque)) position)

-- | The deque version this cursor is positioned in.
cursorSnapshot :: Cursor a -> Deque a
cursorSnapshot (Cursor deque _) = deque

expectCursorEdit :: String -> Maybe a -> a
expectCursorEdit _ (Just value) = value
expectCursorEdit operation Nothing = error ("Durable7.FingerTree.Deque cursor " ++ operation ++ " failed")

-- | The first position in an already-sorted list not less than the probe.
sortedLowerBound :: Ord a => a -> Deque a -> Int
sortedLowerBound = sortedLowerBoundBy compare

-- | The first position in an already-sorted list not less than the probe, under the given
-- comparison.
sortedLowerBoundBy :: (a -> a -> Ordering) -> a -> Deque a -> Int
sortedLowerBoundBy comparison value deque = fst (sortedBoundBy (/= LT) comparison value deque)

-- | The first position in an already-sorted list greater than the probe.
sortedUpperBound :: Ord a => a -> Deque a -> Int
sortedUpperBound = sortedUpperBoundBy compare

-- | The first position in an already-sorted list greater than the probe, under the given
-- comparison.
sortedUpperBoundBy :: (a -> a -> Ordering) -> a -> Deque a -> Int
sortedUpperBoundBy comparison value deque = fst (sortedBoundBy (== GT) comparison value deque)

-- | Binary-searches an already-sorted list.
sortedBinarySearch :: Ord a => a -> Deque a -> SearchResult
sortedBinarySearch = sortedBinarySearchBy compare

-- | Binary-searches an already-sorted list under the given comparison.
sortedBinarySearchBy :: (a -> a -> Ordering) -> a -> Deque a -> SearchResult
sortedBinarySearchBy comparison value deque =
  case candidate of
    Just stored
      | comparison stored value == EQ -> Found lower
    _ -> Missing lower
  where
    (lower, candidate) = sortedBoundBy (/= LT) comparison value deque

-- | Inserts into an already-sorted list, keeping it sorted.
insertSorted :: Ord a => a -> Deque a -> Deque a
insertSorted = insertSortedBy compare

-- | Inserts into an already-sorted list under the given comparison.
insertSortedBy :: (a -> a -> Ordering) -> a -> Deque a -> Deque a
insertSortedBy comparison value deque =
  case insertAt (sortedUpperBoundBy comparison value deque) value deque of
    Just result -> result
    Nothing -> deque

-- | Removes every match from an already-sorted list.
removeAllSorted :: Ord a => a -> Deque a -> Deque a
removeAllSorted = removeAllSortedBy compare

-- | Removes every match from an already-sorted list under the given comparison.
removeAllSortedBy :: (a -> a -> Ordering) -> a -> Deque a -> Deque a
removeAllSortedBy comparison value deque =
  case removeRange lower (upper - lower) deque of
    Just result -> result
    Nothing -> deque
  where
    lower = sortedLowerBoundBy comparison value deque
    upper = sortedUpperBoundBy comparison value deque

sortedBoundBy :: (Ordering -> Bool) -> (a -> a -> Ordering) -> a -> Deque a -> (Int, Maybe a)
sortedBoundBy accepts comparison value deque@(Deque tree) =
  case FT.locate predicate tree of
    Just (before, Elem candidate) -> (measureCount before, Just candidate)
    Nothing -> (count deque, Nothing)
  where
    predicate (DequeMeasure _ candidate) = maybe False (\stored -> accepts (comparison stored value)) candidate

measureCount :: DequeMeasure a -> Int
measureCount (DequeMeasure value _) = value

-- | Adds two element counts, refusing to publish a wrapped count.  A wrapped
-- count is worse than a failure here: it produces a negative 'count', so a
-- cursor built from it is never at its end and every bound check is nonsense.
checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | left < 0 || right < 0 =
      error "Durable7.FingerTree.Deque: internal negative count"
  | right > maxBound - left =
      error "Durable7.FingerTree.Deque: length overflow"
  | otherwise = left + right
