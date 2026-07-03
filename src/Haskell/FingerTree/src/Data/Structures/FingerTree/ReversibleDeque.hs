module Data.Structures.FingerTree.ReversibleDeque
  ( ReversibleDeque
  , empty
  , singleton
  , fromList
  , toList
  , count
  , null
  , cons
  , snoc
  , append
  , reverse
  , viewL
  , viewR
  , first
  , last
  , index
  ) where

import Prelude hiding (last, null, reverse)

import qualified Data.List as List
import qualified Data.Structures.FingerTree.Deque as Deque

data ReversibleDeque a = ReversibleDeque !Bool !(Deque.Deque a)
  deriving (Eq, Ord, Read, Show)

empty :: ReversibleDeque a
empty = ReversibleDeque False Deque.empty

singleton :: a -> ReversibleDeque a
singleton value = ReversibleDeque False (Deque.singleton value)

fromList :: [a] -> ReversibleDeque a
fromList values = ReversibleDeque False (Deque.fromList values)

toList :: ReversibleDeque a -> [a]
toList (ReversibleDeque reversed values)
  | reversed = List.reverse (Deque.toList values)
  | otherwise = Deque.toList values

count :: ReversibleDeque a -> Int
count (ReversibleDeque _ values) = Deque.count values

null :: ReversibleDeque a -> Bool
null deque = count deque == 0

cons :: a -> ReversibleDeque a -> ReversibleDeque a
cons value (ReversibleDeque reversed values)
  | reversed = ReversibleDeque reversed (Deque.snoc values value)
  | otherwise = ReversibleDeque reversed (Deque.cons value values)

snoc :: ReversibleDeque a -> a -> ReversibleDeque a
snoc (ReversibleDeque reversed values) value
  | reversed = ReversibleDeque reversed (Deque.cons value values)
  | otherwise = ReversibleDeque reversed (Deque.snoc values value)

append :: ReversibleDeque a -> ReversibleDeque a -> ReversibleDeque a
append (ReversibleDeque False left) (ReversibleDeque False right) = ReversibleDeque False (Deque.append left right)
append left right = fromList (toList left ++ toList right)

reverse :: ReversibleDeque a -> ReversibleDeque a
reverse (ReversibleDeque reversed values) = ReversibleDeque (not reversed) values

viewL :: ReversibleDeque a -> Maybe (a, ReversibleDeque a)
viewL (ReversibleDeque reversed values)
  | reversed =
      case Deque.viewR values of
        Just (rest, value) -> Just (value, ReversibleDeque reversed rest)
        Nothing -> Nothing
  | otherwise =
      case Deque.viewL values of
        Just (value, rest) -> Just (value, ReversibleDeque reversed rest)
        Nothing -> Nothing

viewR :: ReversibleDeque a -> Maybe (ReversibleDeque a, a)
viewR (ReversibleDeque reversed values)
  | reversed =
      case Deque.viewL values of
        Just (value, rest) -> Just (ReversibleDeque reversed rest, value)
        Nothing -> Nothing
  | otherwise =
      case Deque.viewR values of
        Just (rest, value) -> Just (ReversibleDeque reversed rest, value)
        Nothing -> Nothing

first :: ReversibleDeque a -> Maybe a
first deque = fst <$> viewL deque

last :: ReversibleDeque a -> Maybe a
last deque = snd <$> viewR deque

index :: Int -> ReversibleDeque a -> Maybe a
index position deque@(ReversibleDeque reversed values)
  | position < 0 || position >= count deque = Nothing
  | reversed = Deque.index (count deque - position - 1) values
  | otherwise = Deque.index position values
