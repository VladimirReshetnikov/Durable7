{-# LANGUAGE FlexibleInstances #-}
{-# LANGUAGE MultiParamTypeClasses #-}

module Data.Structures.FingerTree.Deque
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

import qualified Data.Structures.FingerTree.Measured as FT
import Data.Structures.FingerTree.Measured (Measured(..), ViewL(..), ViewR(..))
import Data.Structures.FingerTree.Measures (Elem(..))

data DequeMeasure a = DequeMeasure !Int !(Maybe a)
  deriving (Eq, Ord, Read, Show)

instance Semigroup (DequeMeasure a) where
  DequeMeasure leftCount leftLast <> DequeMeasure rightCount rightLast =
    DequeMeasure (checkedAdd leftCount rightCount) (case rightLast of Just _ -> rightLast; Nothing -> leftLast)

instance Monoid (DequeMeasure a) where
  mempty = DequeMeasure 0 Nothing

instance Measured (DequeMeasure a) (Elem a) where
  measure (Elem value) = DequeMeasure 1 (Just value)

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

data SearchResult
  = Found !Int
  | Missing !Int
  deriving (Eq, Ord, Read, Show)

empty :: Deque a
empty = Deque FT.empty

singleton :: a -> Deque a
singleton value = Deque (FT.singleton (Elem value))

fromList :: [a] -> Deque a
fromList values = Deque (FT.fromList (map Elem values))

toList :: Deque a -> [a]
toList (Deque tree) = map getElem (FT.toList tree)

count :: Deque a -> Int
count (Deque tree) = measureCount (FT.measureTree tree)

null :: Deque a -> Bool
null deque = count deque == 0

cons :: a -> Deque a -> Deque a
cons value (Deque tree) = Deque (FT.cons (Elem value) tree)

snoc :: Deque a -> a -> Deque a
snoc (Deque tree) value = Deque (FT.snoc tree (Elem value))

append :: Deque a -> Deque a -> Deque a
append (Deque left) (Deque right) = Deque (FT.append left right)

viewL :: Deque a -> Maybe (a, Deque a)
viewL (Deque tree) =
  case FT.viewL tree of
    EmptyL -> Nothing
    Elem value :< rest -> Just (value, Deque rest)

viewR :: Deque a -> Maybe (Deque a, a)
viewR (Deque tree) =
  case FT.viewR tree of
    EmptyR -> Nothing
    rest :> Elem value -> Just (Deque rest, value)

first :: Deque a -> Maybe a
first deque = fst <$> viewL deque

last :: Deque a -> Maybe a
last deque = snd <$> viewR deque

index :: Int -> Deque a -> Maybe a
index position (Deque tree)
  | position < 0 || position >= measureCount (FT.measureTree tree) = Nothing
  | otherwise =
      case FT.split (\measureValue -> measureCount measureValue > position) tree of
        Just (_, Elem value, _) -> Just value
        Nothing -> Nothing

setAt :: Int -> a -> Deque a -> Maybe (Deque a)
setAt position value deque = updateAt position (const value) deque

updateAt :: Int -> (a -> a) -> Deque a -> Maybe (Deque a)
updateAt position updater deque@(Deque tree)
  | position < 0 || position >= count deque = Nothing
  | otherwise =
      case FT.split (\measureValue -> measureCount measureValue > position) tree of
        Just (left, Elem old, right) -> Just (Deque (FT.append (FT.snoc left (Elem (updater old))) right))
        Nothing -> Nothing

insertAt :: Int -> a -> Deque a -> Maybe (Deque a)
insertAt position value deque
  | position < 0 || position > count deque = Nothing
  | otherwise =
      case splitAt position deque of
        Just (left, right) -> Just (append (snoc left value) right)
        Nothing -> Nothing

deleteAt :: Int -> Deque a -> Maybe (Deque a)
deleteAt position deque@(Deque tree)
  | position < 0 || position >= count deque = Nothing
  | otherwise =
      case FT.split (\measureValue -> measureCount measureValue > position) tree of
        Just (left, _, right) -> Just (Deque (FT.append left right))
        Nothing -> Nothing

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

slice :: Int -> Int -> Deque a -> Maybe (Deque a)
slice position lengthValue deque
  | lengthValue < 0 = Nothing
  | otherwise =
      case splitAt position deque of
        Just (_, suffix) -> fst <$> splitAt lengthValue suffix
        Nothing -> Nothing

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

cursor :: Deque a -> Cursor a
cursor deque = Cursor deque 0

cursorAt :: Int -> Deque a -> Maybe (Cursor a)
cursorAt position deque
  | position < 0 || position > count deque = Nothing
  | otherwise = Just (Cursor deque position)

cursorPosition :: Cursor a -> Int
cursorPosition (Cursor _ position) = position

cursorCount :: Cursor a -> Int
cursorCount (Cursor deque _) = count deque

cursorIsAtStart :: Cursor a -> Bool
cursorIsAtStart value = cursorPosition value == 0

cursorIsAtEnd :: Cursor a -> Bool
cursorIsAtEnd value = cursorPosition value == cursorCount value

cursorPeekPrevious :: Cursor a -> Maybe a
cursorPeekPrevious (Cursor deque position) = index (position - 1) deque

cursorPeekNext :: Cursor a -> Maybe a
cursorPeekNext (Cursor deque position) = index position deque

cursorMovePrevious :: Cursor a -> Maybe (Cursor a)
cursorMovePrevious (Cursor deque position) = cursorAt (position - 1) deque

cursorMoveNext :: Cursor a -> Maybe (Cursor a)
cursorMoveNext (Cursor deque position)
  | position == count deque = Nothing
  | otherwise = cursorAt (position + 1) deque

cursorSeek :: Int -> Cursor a -> Maybe (Cursor a)
cursorSeek position (Cursor deque _) = cursorAt position deque

cursorInsert :: a -> Cursor a -> Cursor a
cursorInsert value (Cursor deque position) =
  Cursor (expectCursorEdit "insert" (insertAt position value deque)) (position + 1)

cursorInsertList :: [a] -> Cursor a -> Cursor a
cursorInsertList [] value = value
cursorInsertList values (Cursor deque position) =
  case splitAt position deque of
    Just (left, right) -> Cursor (append (append left (fromList values)) right) (position + length values)
    Nothing -> error "Data.Structures.FingerTree.Deque.cursorInsertList: invalid cursor"

cursorDeletePrevious :: Cursor a -> Maybe (Cursor a)
cursorDeletePrevious (Cursor deque position)
  | position == 0 = Nothing
  | otherwise = Just (Cursor (expectCursorEdit "delete previous" (deleteAt (position - 1) deque)) (position - 1))

cursorDeleteNext :: Cursor a -> Maybe (Cursor a)
cursorDeleteNext (Cursor deque position)
  | position == count deque = Nothing
  | otherwise = Just (Cursor (expectCursorEdit "delete next" (deleteAt position deque)) position)

cursorReplaceNext :: a -> Cursor a -> Maybe (Cursor a)
cursorReplaceNext value (Cursor deque position)
  | position == count deque = Nothing
  | otherwise = Just (Cursor (expectCursorEdit "replace next" (setAt position value deque)) position)

cursorSnapshot :: Cursor a -> Deque a
cursorSnapshot (Cursor deque _) = deque

expectCursorEdit :: String -> Maybe a -> a
expectCursorEdit _ (Just value) = value
expectCursorEdit operation Nothing = error ("Data.Structures.FingerTree.Deque cursor " ++ operation ++ " failed")

sortedLowerBound :: Ord a => a -> Deque a -> Int
sortedLowerBound = sortedLowerBoundBy compare

sortedLowerBoundBy :: (a -> a -> Ordering) -> a -> Deque a -> Int
sortedLowerBoundBy comparison value deque = fst (sortedBoundBy (/= LT) comparison value deque)

sortedUpperBound :: Ord a => a -> Deque a -> Int
sortedUpperBound = sortedUpperBoundBy compare

sortedUpperBoundBy :: (a -> a -> Ordering) -> a -> Deque a -> Int
sortedUpperBoundBy comparison value deque = fst (sortedBoundBy (== GT) comparison value deque)

sortedBinarySearch :: Ord a => a -> Deque a -> SearchResult
sortedBinarySearch = sortedBinarySearchBy compare

sortedBinarySearchBy :: (a -> a -> Ordering) -> a -> Deque a -> SearchResult
sortedBinarySearchBy comparison value deque =
  case candidate of
    Just stored
      | comparison stored value == EQ -> Found lower
    _ -> Missing lower
  where
    (lower, candidate) = sortedBoundBy (/= LT) comparison value deque

insertSorted :: Ord a => a -> Deque a -> Deque a
insertSorted = insertSortedBy compare

insertSortedBy :: (a -> a -> Ordering) -> a -> Deque a -> Deque a
insertSortedBy comparison value deque =
  case insertAt (sortedUpperBoundBy comparison value deque) value deque of
    Just result -> result
    Nothing -> deque

removeAllSorted :: Ord a => a -> Deque a -> Deque a
removeAllSorted = removeAllSortedBy compare

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
      error "Data.Structures.FingerTree.Deque: internal negative count"
  | right > maxBound - left =
      error "Data.Structures.FingerTree.Deque: length overflow"
  | otherwise = left + right
