module Durable7.Tungsten.List
  ( PersistentList
  , empty
  , fromList
  , toList
  , count
  , null
  , first
  , last
  , index
  , snoc
  , cons
  , append
  , addRange
  , insertAt
  , insertRange
  , deleteAt
  , removeRange
  , removeFirst
  , removeLast
  , setAt
  , updateAt
  , slice
  , take
  , takeLast
  , drop
  , dropLast
  , splitAt
  , reverse
  , map
  , indexOf
  , member
  ) where

import Prelude hiding (drop, last, map, null, reverse, splitAt, take)

import qualified Data.List as List
import qualified Durable7.FingerTree.Deque as Deque

newtype PersistentList a = PersistentList (Deque.Deque a)
  deriving (Show)

-- Extensional equality: two lists are equal exactly when they contain the
-- same element sequence, regardless of internal tree shape.
instance Eq a => Eq (PersistentList a) where
  left == right = count left == count right && toList left == toList right

instance Ord a => Ord (PersistentList a) where
  compare left right = compare (toList left) (toList right)

empty :: PersistentList a
empty = PersistentList Deque.empty

fromList :: [a] -> PersistentList a
fromList = PersistentList . Deque.fromList

toList :: PersistentList a -> [a]
toList (PersistentList items) = Deque.toList items

count :: PersistentList a -> Int
count (PersistentList items) = Deque.count items

null :: PersistentList a -> Bool
null (PersistentList items) = Deque.null items

first :: PersistentList a -> Maybe a
first (PersistentList items) = Deque.first items

last :: PersistentList a -> Maybe a
last (PersistentList items) = Deque.last items

index :: Int -> PersistentList a -> Maybe a
index position (PersistentList items) = Deque.index position items

snoc :: PersistentList a -> a -> PersistentList a
snoc (PersistentList items) value = PersistentList (Deque.snoc items value)

cons :: a -> PersistentList a -> PersistentList a
cons value (PersistentList items) = PersistentList (Deque.cons value items)

append :: PersistentList a -> PersistentList a -> PersistentList a
append (PersistentList left) (PersistentList right)
  | Deque.null left = PersistentList right
  | Deque.null right = PersistentList left
  | otherwise = PersistentList (Deque.append left right)

addRange :: PersistentList a -> [a] -> PersistentList a
addRange list values = append list (fromList values)

insertAt :: Int -> a -> PersistentList a -> Maybe (PersistentList a)
insertAt position value (PersistentList items) = PersistentList <$> Deque.insertAt position value items

insertRange :: Int -> [a] -> PersistentList a -> Maybe (PersistentList a)
insertRange position values list
  | position < 0 || position > count list = Nothing
  | otherwise =
      case splitAt position list of
        Just (left, right) -> Just (append (addRange left values) right)
        Nothing -> Nothing

deleteAt :: Int -> PersistentList a -> Maybe (PersistentList a)
deleteAt position (PersistentList items) = PersistentList <$> Deque.deleteAt position items

removeRange :: Int -> Int -> PersistentList a -> Maybe (PersistentList a)
removeRange position lengthValue (PersistentList items) = PersistentList <$> Deque.removeRange position lengthValue items

removeFirst :: PersistentList a -> Maybe (PersistentList a)
removeFirst list = deleteAt 0 list

removeLast :: PersistentList a -> Maybe (PersistentList a)
removeLast list = deleteAt (count list - 1) list

setAt :: Int -> a -> PersistentList a -> Maybe (PersistentList a)
setAt position value (PersistentList items) = PersistentList <$> Deque.setAt position value items

updateAt :: Int -> (a -> a) -> PersistentList a -> Maybe (PersistentList a)
updateAt position updater (PersistentList items) = PersistentList <$> Deque.updateAt position updater items

slice :: Int -> Int -> PersistentList a -> Maybe (PersistentList a)
slice position lengthValue (PersistentList items) = PersistentList <$> Deque.slice position lengthValue items

take :: Int -> PersistentList a -> Maybe (PersistentList a)
take lengthValue = slice 0 lengthValue

takeLast :: Int -> PersistentList a -> Maybe (PersistentList a)
takeLast lengthValue list
  | lengthValue < 0 || lengthValue > count list = Nothing
  | otherwise = slice (count list - lengthValue) lengthValue list

drop :: Int -> PersistentList a -> Maybe (PersistentList a)
drop lengthValue list
  | lengthValue < 0 || lengthValue > count list = Nothing
  | otherwise = slice lengthValue (count list - lengthValue) list

dropLast :: Int -> PersistentList a -> Maybe (PersistentList a)
dropLast lengthValue list
  | lengthValue < 0 || lengthValue > count list = Nothing
  | otherwise = slice 0 (count list - lengthValue) list

splitAt :: Int -> PersistentList a -> Maybe (PersistentList a, PersistentList a)
splitAt position (PersistentList items) =
  case Deque.splitAt position items of
    Just (left, right) -> Just (PersistentList left, PersistentList right)
    Nothing -> Nothing

reverse :: PersistentList a -> PersistentList a
reverse list
  | count list <= 1 = list
  | otherwise = fromList (List.reverse (toList list))

map :: (a -> b) -> PersistentList a -> PersistentList b
map mapper = fromList . fmap mapper . toList

indexOf :: (a -> a -> Bool) -> a -> PersistentList a -> Maybe Int
indexOf equal value list = go 0 (toList list)
  where
    go _ [] = Nothing
    go position (candidate : rest)
      | equal candidate value = Just position
      | otherwise = go (position + 1) rest

member :: (a -> a -> Bool) -> a -> PersistentList a -> Bool
member equal value list =
  case indexOf equal value list of
    Just _ -> True
    Nothing -> False
