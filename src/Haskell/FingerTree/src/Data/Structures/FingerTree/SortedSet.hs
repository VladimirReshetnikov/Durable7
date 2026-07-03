module Data.Structures.FingerTree.SortedSet
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

import qualified Data.Set as Set

newtype SortedSet a = SortedSet (Set.Set a)
  deriving (Eq, Ord, Read, Show)

empty :: SortedSet a
empty = SortedSet Set.empty

singleton :: a -> SortedSet a
singleton value = SortedSet (Set.singleton value)

fromList :: Ord a => [a] -> SortedSet a
fromList values = SortedSet (Set.fromList values)

toList :: SortedSet a -> [a]
toList (SortedSet values) = Set.toAscList values

count :: SortedSet a -> Int
count (SortedSet values) = Set.size values

null :: SortedSet a -> Bool
null (SortedSet values) = Set.null values

member :: Ord a => a -> SortedSet a -> Bool
member value (SortedSet values) = Set.member value values

insert :: Ord a => a -> SortedSet a -> SortedSet a
insert value (SortedSet values) = SortedSet (Set.insert value values)

delete :: Ord a => a -> SortedSet a -> SortedSet a
delete value (SortedSet values) = SortedSet (Set.delete value values)

minValue :: SortedSet a -> Maybe a
minValue (SortedSet values) =
  case Set.lookupMin values of
    Just value -> Just value
    Nothing -> Nothing

maxValue :: SortedSet a -> Maybe a
maxValue (SortedSet values) =
  case Set.lookupMax values of
    Just value -> Just value
    Nothing -> Nothing

floor :: Ord a => a -> SortedSet a -> Maybe a
floor value (SortedSet values) = Set.lookupLE value values

ceiling :: Ord a => a -> SortedSet a -> Maybe a
ceiling value (SortedSet values) = Set.lookupGE value values

lower :: Ord a => a -> SortedSet a -> Maybe a
lower value (SortedSet values) = Set.lookupLT value values

higher :: Ord a => a -> SortedSet a -> Maybe a
higher value (SortedSet values) = Set.lookupGT value values

index :: Int -> SortedSet a -> Maybe a
index position (SortedSet values)
  | position < 0 || position >= Set.size values = Nothing
  | otherwise = Just (Set.elemAt position values)

indexOf :: Ord a => a -> SortedSet a -> Maybe Int
indexOf value (SortedSet values) = Set.lookupIndex value values

slice :: Ord a => Int -> Int -> SortedSet a -> Maybe (SortedSet a)
slice position lengthValue setValue
  | not (isValidRange position lengthValue (count setValue)) = Nothing
  | otherwise = Just (fromList (take lengthValue (drop position (toList setValue))))

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position

union :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
union (SortedSet left) (SortedSet right) = SortedSet (Set.union left right)

intersection :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
intersection (SortedSet left) (SortedSet right) = SortedSet (Set.intersection left right)

difference :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
difference (SortedSet left) (SortedSet right) = SortedSet (Set.difference left right)

symmetricDifference :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
symmetricDifference left right = difference (union left right) (intersection left right)

isSubsetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isSubsetOf (SortedSet left) (SortedSet right) = Set.isSubsetOf left right

isProperSubsetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isProperSubsetOf (SortedSet left) (SortedSet right) = Set.isProperSubsetOf left right

isSupersetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isSupersetOf left right = isSubsetOf right left

isProperSupersetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isProperSupersetOf left right = isProperSubsetOf right left

overlaps :: Ord a => SortedSet a -> SortedSet a -> Bool
overlaps (SortedSet left) (SortedSet right) = not (Set.null (Set.intersection left right))

setEquals :: Ord a => SortedSet a -> SortedSet a -> Bool
setEquals left right = count left == count right && isSubsetOf left right
