-- | A persistent sorted set with rank and select.
--
-- Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
-- so an edit copies a path rather than the whole collection.
module Durable7.FingerTree.SortedSet
  ( SortedSet
  , empty
  , singleton
  , fromList
  , toList
  , count
  , null
  , member
  , insert
  , delete
  , minValue
  , maxValue
  , floor
  , ceiling
  , lower
  , higher
  , index
  , indexOf
  , countLessThan
  , countAtMost
  , slice
  , union
  , intersection
  , difference
  , symmetricDifference
  , isSubsetOf
  , isProperSubsetOf
  , isSupersetOf
  , isProperSupersetOf
  , overlaps
  , setEquals
  ) where

import Prelude hiding (ceiling, floor, null)

import qualified Data.List as List
import qualified Data.Set as Set

-- | A persistent sorted set with rank and select, keeping the first representative of each
-- equivalence class.
newtype SortedSet a = SortedSet (Set.Set a)
  deriving (Eq, Ord, Read, Show)

-- | The empty set.
empty :: SortedSet a
empty = SortedSet Set.empty

-- | A set holding one element.
singleton :: a -> SortedSet a
singleton value = SortedSet (Set.singleton value)

-- | A set holding a list's elements, built in bulk rather than by repeated insertion.
fromList :: Ord a => [a] -> SortedSet a
fromList = List.foldl' (flip insert) empty

-- | The elements, in the set's own order.
toList :: SortedSet a -> [a]
toList (SortedSet values) = Set.toAscList values

-- | Number of elements in the set.
count :: SortedSet a -> Int
count (SortedSet values) = Set.size values

-- | Whether the set holds no elements.
null :: SortedSet a -> Bool
null (SortedSet values) = Set.null values

-- | Whether the element is present.
member :: Ord a => a -> SortedSet a -> Bool
member value (SortedSet values) = Set.member value values

-- | First stored instance wins: adding a comparer-equal element leaves the set
-- unchanged, matching the C# reference (Set.insert would replace the stored
-- element with the supplied equal one).
insert :: Ord a => a -> SortedSet a -> SortedSet a
insert value set@(SortedSet values)
  | Set.member value values = set
  | otherwise = SortedSet (Set.insert value values)

-- | A set without that element.
delete :: Ord a => a -> SortedSet a -> SortedSet a
delete value (SortedSet values) = SortedSet (Set.delete value values)

-- | The smallest element.
minValue :: SortedSet a -> Maybe a
minValue (SortedSet values) =
  case Set.lookupMin values of
    Just value -> Just value
    Nothing -> Nothing

-- | The largest element.
maxValue :: SortedSet a -> Maybe a
maxValue (SortedSet values) =
  case Set.lookupMax values of
    Just value -> Just value
    Nothing -> Nothing

-- | The largest element not greater than the probe.
floor :: Ord a => a -> SortedSet a -> Maybe a
floor value (SortedSet values) = Set.lookupLE value values

-- | The smallest element not less than the probe.
ceiling :: Ord a => a -> SortedSet a -> Maybe a
ceiling value (SortedSet values) = Set.lookupGE value values

-- | The largest element strictly less than the probe.
lower :: Ord a => a -> SortedSet a -> Maybe a
lower value (SortedSet values) = Set.lookupLT value values

-- | The smallest element strictly greater than the probe.
higher :: Ord a => a -> SortedSet a -> Maybe a
higher value (SortedSet values) = Set.lookupGT value values

-- | The element at the given rank.
index :: Int -> SortedSet a -> Maybe a
index position (SortedSet values)
  | position < 0 || position >= Set.size values = Nothing
  | otherwise = Just (Set.elemAt position values)

-- | The rank of the given element.
indexOf :: Ord a => a -> SortedSet a -> Maybe Int
indexOf value (SortedSet values) = Set.lookupIndex value values

-- | How many elements order before the probe.
countLessThan :: Ord a => a -> SortedSet a -> Int
countLessThan value (SortedSet values) = Set.size lowerValues
  where
    (lowerValues, _) = Set.split value values

-- | How many elements order at or before the probe.
countAtMost :: Ord a => a -> SortedSet a -> Int
countAtMost value setValue = countLessThan value setValue + if member value setValue then 1 else 0

-- | The elements in the given range.
slice :: Ord a => Int -> Int -> SortedSet a -> Maybe (SortedSet a)
slice position lengthValue setValue
  | not (isValidRange position lengthValue (count setValue)) = Nothing
  | otherwise = Just (fromList (take lengthValue (drop position (toList setValue))))

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position

-- | The elements of both sets. Subtrees the operands already share are adopted whole rather than
-- re-entered.
union :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
union (SortedSet left) (SortedSet right) = SortedSet (Set.union left right)

-- | The elements present in both sets.
intersection :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
intersection (SortedSet left) (SortedSet right) = SortedSet (Set.intersection left right)

-- | This set's elements that are absent from the other.
difference :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
difference (SortedSet left) (SortedSet right) = SortedSet (Set.difference left right)

-- | The elements present in exactly one of the two sets.
symmetricDifference :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
symmetricDifference left right = difference (union left right) (intersection left right)

-- | Whether every element of this set also occurs in the other.
isSubsetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isSubsetOf (SortedSet left) (SortedSet right) = Set.isSubsetOf left right

-- | Whether this set is a subset of the other and the other holds an element it lacks.
isProperSubsetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isProperSubsetOf (SortedSet left) (SortedSet right) = Set.isProperSubsetOf left right

-- | Whether every element of the other occurs in this set.
isSupersetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isSupersetOf left right = isSubsetOf right left

-- | Whether this set is a superset of the other and holds an element the other lacks.
isProperSupersetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isProperSupersetOf left right = isProperSubsetOf right left

-- | Whether the two sets share at least one element.
overlaps :: Ord a => SortedSet a -> SortedSet a -> Bool
overlaps (SortedSet left) (SortedSet right) = not (Set.null (Set.intersection left right))

-- | Whether both sets hold the same elements.
setEquals :: Ord a => SortedSet a -> SortedSet a -> Bool
setEquals left right = count left == count right && isSubsetOf left right
