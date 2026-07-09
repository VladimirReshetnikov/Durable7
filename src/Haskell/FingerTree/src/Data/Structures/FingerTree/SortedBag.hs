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

import qualified Data.List as List
import qualified Data.Map.Strict as Map

-- Buckets keep every stored instance in insertion order (new equal elements
-- are appended after existing ones), matching the C# reference, which retains
-- comparer-equal-but-distinct instances instead of collapsing them to counts.
data SortedBag a = SortedBag !Int !(Map.Map a [a])
  deriving (Eq, Ord, Read, Show)

empty :: SortedBag a
empty = SortedBag 0 Map.empty

singleton :: a -> SortedBag a
singleton value = SortedBag 1 (Map.singleton value [value])

fromList :: Ord a => [a] -> SortedBag a
fromList = List.foldl' (flip insert) empty

toList :: SortedBag a -> [a]
toList (SortedBag _ values) = concatMap snd (Map.toAscList values)

toCounts :: SortedBag a -> [(a, Int)]
toCounts (SortedBag _ values) = map summarize (Map.toAscList values)
  where
    summarize (value, bucket) = (value, length bucket)

count :: SortedBag a -> Int
count (SortedBag total _) = total

null :: SortedBag a -> Bool
null bag = count bag == 0

distinctCount :: SortedBag a -> Int
distinctCount (SortedBag _ values) = Map.size values

member :: Ord a => a -> SortedBag a -> Bool
member value bag = countOf value bag > 0

countOf :: Ord a => a -> SortedBag a -> Int
countOf value (SortedBag _ values) = maybe 0 length (Map.lookup value values)

insert :: Ord a => a -> SortedBag a -> SortedBag a
insert value (SortedBag total values) =
  SortedBag (total + 1) (Map.insertWith (flip (++)) value [value] values)

deleteOne :: Ord a => a -> SortedBag a -> SortedBag a
deleteOne value bag@(SortedBag total values) =
  case Map.lookup value values of
    Nothing -> bag
    Just bucket
      | length bucket <= 1 -> SortedBag (total - 1) (Map.delete value values)
      -- Removes the first stored equal instance (C# TryRemove semantics);
      -- Map.adjust keeps the originally stored map key.
      | otherwise -> SortedBag (total - 1) (Map.adjust (List.drop 1) value values)

deleteAll :: Ord a => a -> SortedBag a -> SortedBag a
deleteAll value bag@(SortedBag total values) =
  case Map.lookup value values of
    Nothing -> bag
    Just bucket -> SortedBag (total - length bucket) (Map.delete value values)

minValue :: SortedBag a -> Maybe a
minValue (SortedBag _ values) = firstInstance <$> Map.lookupMin values
  where
    firstInstance (key, bucket) = case bucket of
      instanceValue : _ -> instanceValue
      [] -> key

maxValue :: SortedBag a -> Maybe a
maxValue (SortedBag _ values) = lastInstance <$> Map.lookupMax values
  where
    lastInstance (key, bucket) = case bucket of
      [] -> key
      _ -> List.last bucket

countLessThan :: Ord a => a -> SortedBag a -> Int
countLessThan value (SortedBag _ values) = sum (map length (Map.elems less))
  where
    (less, _) = Map.split value values

countAtMost :: Ord a => a -> SortedBag a -> Int
countAtMost value (SortedBag _ values) = sum (map length (Map.elems less)) + maybe 0 length equal
  where
    (less, equal, _) = Map.splitLookup value values

index :: Int -> SortedBag a -> Maybe a
index position bag@(SortedBag _ values)
  | position < 0 || position >= count bag = Nothing
  | otherwise = go position (Map.toAscList values)
  where
    go _ [] = Nothing
    go remaining ((_, bucket) : rest)
      | remaining < length bucket = Just (bucket List.!! remaining)
      | otherwise = go (remaining - length bucket) rest

slice :: Ord a => Int -> Int -> SortedBag a -> Maybe (SortedBag a)
slice position lengthValue bag
  | not (isValidRange position lengthValue (count bag)) = Nothing
  | otherwise = Just (fromList (take lengthValue (drop position (toList bag))))

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position
