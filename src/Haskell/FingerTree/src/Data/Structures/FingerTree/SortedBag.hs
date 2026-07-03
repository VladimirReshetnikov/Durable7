module Data.Structures.FingerTree.SortedBag
  ( SortedBag
  , empty
  , singleton
  , fromList
  , toList
  , toCounts
  , count
  , null
  , distinctCount
  , member
  , countOf
  , insert
  , deleteOne
  , deleteAll
  , minValue
  , maxValue
  , countLessThan
  , countAtMost
  , index
  , slice
  ) where

import Prelude hiding (null)

import qualified Data.Map.Strict as Map

data SortedBag a = SortedBag !Int !(Map.Map a Int)
  deriving (Eq, Ord, Read, Show)

empty :: SortedBag a
empty = SortedBag 0 Map.empty

singleton :: a -> SortedBag a
singleton value = SortedBag 1 (Map.singleton value 1)

fromList :: Ord a => [a] -> SortedBag a
fromList = foldl (flip insert) empty

toList :: SortedBag a -> [a]
toList (SortedBag _ values) = concatMap expand (Map.toAscList values)
  where
    expand (value, copies) = replicate copies value

toCounts :: SortedBag a -> [(a, Int)]
toCounts (SortedBag _ values) = Map.toAscList values

count :: SortedBag a -> Int
count (SortedBag total _) = total

null :: SortedBag a -> Bool
null bag = count bag == 0

distinctCount :: SortedBag a -> Int
distinctCount (SortedBag _ values) = Map.size values

member :: Ord a => a -> SortedBag a -> Bool
member value bag = countOf value bag > 0

countOf :: Ord a => a -> SortedBag a -> Int
countOf value (SortedBag _ values) = Map.findWithDefault 0 value values

insert :: Ord a => a -> SortedBag a -> SortedBag a
insert value (SortedBag total values) = SortedBag (total + 1) (Map.insertWith (+) value 1 values)

deleteOne :: Ord a => a -> SortedBag a -> SortedBag a
deleteOne value bag@(SortedBag total values) =
  case Map.lookup value values of
    Nothing -> bag
    Just copies
      | copies <= 1 -> SortedBag (total - 1) (Map.delete value values)
      | otherwise -> SortedBag (total - 1) (Map.insert value (copies - 1) values)

deleteAll :: Ord a => a -> SortedBag a -> SortedBag a
deleteAll value bag@(SortedBag total values) =
  case Map.lookup value values of
    Nothing -> bag
    Just copies -> SortedBag (total - copies) (Map.delete value values)

minValue :: SortedBag a -> Maybe a
minValue (SortedBag _ values) = fst <$> Map.lookupMin values

maxValue :: SortedBag a -> Maybe a
maxValue (SortedBag _ values) = fst <$> Map.lookupMax values

countLessThan :: Ord a => a -> SortedBag a -> Int
countLessThan value (SortedBag _ values) = sum (Map.elems less)
  where
    (less, _) = Map.split value values

countAtMost :: Ord a => a -> SortedBag a -> Int
countAtMost value (SortedBag _ values) = sum (Map.elems less) + maybe 0 id equal
  where
    (less, equal, _) = Map.splitLookup value values

index :: Int -> SortedBag a -> Maybe a
index position bag
  | position < 0 || position >= count bag = Nothing
  | otherwise = go position (toCounts bag)
  where
    go _ [] = Nothing
    go remaining ((value, copies) : rest)
      | remaining < copies = Just value
      | otherwise = go (remaining - copies) rest

slice :: Ord a => Int -> Int -> SortedBag a -> Maybe (SortedBag a)
slice position lengthValue bag
  | not (isValidRange position lengthValue (count bag)) = Nothing
  | otherwise = Just (fromList (take lengthValue (drop position (toList bag))))

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position
