module Data.Structures.FingerTree.Deque
  ( Deque
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
import Data.Structures.FingerTree.Measured (ViewL(..), ViewR(..))
import Data.Structures.FingerTree.Measures (Elem(..), Size(..))

newtype Deque a = Deque (FT.FingerTree Size (Elem a))
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
count (Deque tree) = getSize (FT.measureTree tree)

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
  | position < 0 || position >= getSize (FT.measureTree tree) = Nothing
  | otherwise =
      case FT.split (\(Size seen) -> seen > position) tree of
        Just (_, Elem value, _) -> Just value
        Nothing -> Nothing

setAt :: Int -> a -> Deque a -> Maybe (Deque a)
setAt position value deque = updateAt position (const value) deque

updateAt :: Int -> (a -> a) -> Deque a -> Maybe (Deque a)
updateAt position updater deque@(Deque tree)
  | position < 0 || position >= count deque = Nothing
  | otherwise =
      case FT.split (\(Size seen) -> seen > position) tree of
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
      case FT.split (\(Size seen) -> seen > position) tree of
        Just (left, _, right) -> Just (Deque (FT.append left right))
        Nothing -> Nothing

splitAt :: Int -> Deque a -> Maybe (Deque a, Deque a)
splitAt position deque@(Deque tree)
  | position < 0 || position > total = Nothing
  | position == 0 = Just (empty, deque)
  | position == total = Just (deque, empty)
  | otherwise =
      case FT.split (\(Size seen) -> seen > position) tree of
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

sortedLowerBound :: Ord a => a -> Deque a -> Int
sortedLowerBound = sortedLowerBoundBy compare

sortedLowerBoundBy :: (a -> a -> Ordering) -> a -> Deque a -> Int
sortedLowerBoundBy comparison value deque = go 0 (count deque)
  where
    go low high
      | low >= high = low
      | otherwise =
          let mid = low + (high - low) `div` 2
           in case index mid deque of
                Just candidate
                  | comparison candidate value == LT -> go (mid + 1) high
                  | otherwise -> go low mid
                Nothing -> low

sortedUpperBound :: Ord a => a -> Deque a -> Int
sortedUpperBound = sortedUpperBoundBy compare

sortedUpperBoundBy :: (a -> a -> Ordering) -> a -> Deque a -> Int
sortedUpperBoundBy comparison value deque = go 0 (count deque)
  where
    go low high
      | low >= high = low
      | otherwise =
          let mid = low + (high - low) `div` 2
           in case index mid deque of
                Just candidate
                  | comparison candidate value == GT -> go low mid
                  | otherwise -> go (mid + 1) high
                Nothing -> low

sortedBinarySearch :: Ord a => a -> Deque a -> SearchResult
sortedBinarySearch = sortedBinarySearchBy compare

sortedBinarySearchBy :: (a -> a -> Ordering) -> a -> Deque a -> SearchResult
sortedBinarySearchBy comparison value deque =
  case index lower deque of
    Just candidate
      | comparison candidate value == EQ -> Found lower
    _ -> Missing lower
  where
    lower = sortedLowerBoundBy comparison value deque

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
