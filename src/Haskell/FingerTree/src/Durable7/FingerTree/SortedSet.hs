{-# LANGUAGE MultiParamTypeClasses #-}

-- | A persistent sorted set with rank and select.
--
-- Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
-- so an edit copies a path rather than the whole collection.
--
-- The representation is the workspace's own lazy measured finger tree of elements in ascending
-- order under a (count, last key) measure, exactly as 'Durable7.FingerTree.SortedBag' is built.
-- Ordered searches are measure-directed descents, extremes are digit reads at the root, rank
-- windows are two comparison-free structural splits by the cached counts, and the set algebra
-- adopts whole runs of one operand between boundary splits instead of visiting their elements.
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
import qualified Durable7.FingerTree.Measured as Measured

-- | The measure combines positional weight with the last key in each prefix.
-- Key predicates are monotone because elements are stored in ascending order.
data SetMeasure a = SetMeasure !Int !(Maybe a)
  deriving (Eq, Ord, Read, Show)

instance Semigroup (SetMeasure a) where
  SetMeasure leftCount leftKey <> SetMeasure rightCount rightKey =
    SetMeasure
      (leftCount + rightCount)
      (case rightKey of
        Just _ -> rightKey
        Nothing -> leftKey)

instance Monoid (SetMeasure a) where
  mempty = SetMeasure 0 Nothing

-- | One stored element. The wrapper exists to carry the 'Measured.Measured' instance.
newtype SetElement a = SetElement a
  deriving (Eq, Ord, Read, Show)

instance Measured.Measured (SetMeasure a) (SetElement a) where
  measure (SetElement value) = SetMeasure 1 (Just value)

type SetTree a = Measured.FingerTree (SetMeasure a) (SetElement a)

-- | A persistent sorted set with rank and select, keeping the first representative of each
-- equivalence class.
newtype SortedSet a = SortedSet (SetTree a)
  deriving (Read, Show)

instance Eq a => Eq (SortedSet a) where
  left == right = toList left == toList right

instance Ord a => Ord (SortedSet a) where
  compare left right = compare (toList left) (toList right)

-- | The empty set. O(1).
empty :: SortedSet a
empty = SortedSet Measured.empty

-- | A set holding one element. O(1).
singleton :: a -> SortedSet a
singleton value = SortedSet (Measured.singleton (SetElement value))

-- | A set holding a list's elements. @O(n log n)@ by repeated insertion, so the first supplied
-- instance of each equivalence class is the one retained.
fromList :: Ord a => [a] -> SortedSet a
fromList = List.foldl' (flip insert) empty

-- | The elements, in the set's own order. Lazy; Theta(n) fully consumed.
toList :: SortedSet a -> [a]
toList (SortedSet values) = map unElement (Measured.toList values)

-- | Number of elements in the set. O(1): the count is cached in the root measure.
count :: SortedSet a -> Int
count (SortedSet values) = measureCount (Measured.measureTree values)

-- | Whether the set holds no elements. O(1).
null :: SortedSet a -> Bool
null (SortedSet values) = Measured.null values

-- | Whether the element is present. O(log n): one measure-directed descent.
member :: Ord a => a -> SortedSet a -> Bool
member value (SortedSet values) =
  case locateAtLeast value values of
    Just (_, stored) -> sameClass stored value
    Nothing -> False

-- | First stored instance wins: adding a comparer-equal element returns the receiver itself,
-- matching the C# reference (which never replaces a stored equal element). O(log n): one measured
-- split and a bounded-depth rejoin.
insert :: Ord a => a -> SortedSet a -> SortedSet a
insert value set@(SortedSet values) =
  case splitAtLeast value values of
    Nothing -> SortedSet (Measured.snoc values (SetElement value))
    Just (left, stored, right)
      | sameClass stored value -> set
      | otherwise ->
          SortedSet
            (Measured.append
              (Measured.snoc left (SetElement value))
              (Measured.cons stored right))

-- | A set without that element; the receiver itself when it is absent. O(log n).
delete :: Ord a => a -> SortedSet a -> SortedSet a
delete value set@(SortedSet values) =
  case splitAtLeast value values of
    Just (left, stored, right)
      | sameClass stored value -> SortedSet (Measured.append left right)
    _ -> set

-- | The smallest element. O(1): a digit read at the tree's root, never a descent. Forcing a
-- suspended spine left by earlier edits is charged to the edits that suspended it.
minValue :: SortedSet a -> Maybe a
minValue (SortedSet values) = unElement <$> Measured.head values

-- | The largest element. O(1): a digit read at the tree's root, never a descent. Forcing a
-- suspended spine left by earlier edits is charged to the edits that suspended it.
maxValue :: SortedSet a -> Maybe a
maxValue (SortedSet values) = unElement <$> Measured.last values

-- | The largest element not greater than the probe. O(log n).
floor :: Ord a => a -> SortedSet a -> Maybe a
floor value (SortedSet values) =
  case splitAtLeast value values of
    Nothing -> unElement <$> Measured.last values
    Just (left, stored, _)
      | sameClass stored value -> Just (unElement stored)
      | otherwise -> unElement <$> Measured.last left

-- | The smallest element not less than the probe. O(log n).
ceiling :: Ord a => a -> SortedSet a -> Maybe a
ceiling value (SortedSet values) = unElement . snd <$> locateAtLeast value values

-- | The largest element strictly less than the probe. O(log n).
lower :: Ord a => a -> SortedSet a -> Maybe a
lower value (SortedSet values) =
  case splitAtLeast value values of
    Nothing -> unElement <$> Measured.last values
    Just (left, _, _) -> unElement <$> Measured.last left

-- | The smallest element strictly greater than the probe. O(log n).
higher :: Ord a => a -> SortedSet a -> Maybe a
higher value (SortedSet values) = unElement . snd <$> locateAbove value values

-- | The element at the given rank. O(log n): one descent directed by the cached counts, so no
-- element is compared on the way.
index :: Int -> SortedSet a -> Maybe a
index position set@(SortedSet values)
  | position < 0 || position >= count set = Nothing
  | otherwise =
      unElement . snd <$> Measured.locate (\measureValue -> measureCount measureValue > position) values

-- | The rank of the given element. O(log n): the descent reads the accumulated count of the
-- prefix it skips.
indexOf :: Ord a => a -> SortedSet a -> Maybe Int
indexOf value (SortedSet values) =
  case locateAtLeast value values of
    Just (before, stored)
      | sameClass stored value -> Just (measureCount before)
    _ -> Nothing

-- | How many elements order before the probe. O(log n).
countLessThan :: Ord a => a -> SortedSet a -> Int
countLessThan value (SortedSet values) =
  measureBeforeCount (locateAtLeast value values) (Measured.measureTree values)

-- | How many elements order at or before the probe. O(log n).
countAtMost :: Ord a => a -> SortedSet a -> Int
countAtMost value (SortedSet values) =
  measureBeforeCount (locateAbove value values) (Measured.measureTree values)

-- | The elements in the given rank range, sharing the source's untouched structure. O(log n):
-- two structural splits directed by the cached counts, independent of the window's position and
-- comparing no elements at all.
slice :: Ord a => Int -> Int -> SortedSet a -> Maybe (SortedSet a)
slice position lengthValue set@(SortedSet values)
  | not (isValidRange position lengthValue (count set)) = Nothing
  | otherwise =
      let (_, afterPosition) = splitAtRank position values
          (selected, _) = splitAtRank lengthValue afterPosition
       in Just (SortedSet selected)

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position

-- | The elements of both sets, preferring this set's representative when both hold an equivalent
-- element. @O(m log(n\/m + 1))@ for operand sizes @m <= n@: runs of either operand that fall
-- between the other's elements are adopted whole by boundary splits, never entered element by
-- element, so two interleaved operands cost O(n + m) and two disjoint ranges cost O(log n).
union :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
union leftSet@(SortedSet left) (SortedSet right)
  | Measured.null right = leftSet
  | otherwise = SortedSet (unionTrees left right)

-- | The elements present in both sets, taken from this set. @O(m log(n\/m + 1))@, as 'union'.
intersection :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
intersection (SortedSet left) (SortedSet right) = SortedSet (intersectionTrees left right)

-- | This set's elements that are absent from the other. @O(m log(n\/m + 1))@, as 'union'.
difference :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
difference (SortedSet left) (SortedSet right) = SortedSet (differenceTrees left right)

-- | The elements present in exactly one of the two sets. @O(m log(n\/m + 1))@: three run-adopting
-- merges of the same class.
symmetricDifference :: Ord a => SortedSet a -> SortedSet a -> SortedSet a
symmetricDifference left right = difference (union left right) (intersection left right)

-- | Whether every element of this set also occurs in the other. @O(m log(n\/m + 1))@ with an O(1)
-- count gate; the walk stops at the first missing element.
isSubsetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isSubsetOf leftSet@(SortedSet left) rightSet@(SortedSet right)
  | count leftSet > count rightSet = False
  | otherwise = subsetTrees left right

-- | Whether this set is a subset of the other and the other holds an element it lacks.
-- @O(m log(n\/m + 1))@.
isProperSubsetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isProperSubsetOf left right = count left < count right && isSubsetOf left right

-- | Whether every element of the other occurs in this set. @O(m log(n\/m + 1))@.
isSupersetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isSupersetOf left right = isSubsetOf right left

-- | Whether this set is a superset of the other and holds an element the other lacks.
-- @O(m log(n\/m + 1))@.
isProperSupersetOf :: Ord a => SortedSet a -> SortedSet a -> Bool
isProperSupersetOf left right = isProperSubsetOf right left

-- | Whether the two sets share at least one element. @O(m log(n\/m + 1))@ worst case; the walk
-- stops at the first equivalent pair.
overlaps :: Ord a => SortedSet a -> SortedSet a -> Bool
overlaps (SortedSet left) (SortedSet right) = overlapsTrees left right

-- | Whether both sets hold the same elements. @O(m log(n\/m + 1))@ with an O(1) count gate.
setEquals :: Ord a => SortedSet a -> SortedSet a -> Bool
setEquals left right = count left == count right && isSubsetOf left right

unElement :: SetElement a -> a
unElement (SetElement value) = value

measureCount :: SetMeasure a -> Int
measureCount (SetMeasure total _) = total

measureLastKey :: SetMeasure a -> Maybe a
measureLastKey (SetMeasure _ key) = key

-- Ordered containers decide identity by comparison, never by (==), so a coarse Ord instance
-- keeps working even when Eq is finer.
sameClass :: Ord a => SetElement a -> a -> Bool
sameClass (SetElement stored) value = compare stored value == EQ

locateAtLeast :: Ord a => a -> SetTree a -> Maybe (SetMeasure a, SetElement a)
locateAtLeast value = Measured.locate (maybe False (>= value) . measureLastKey)

locateAbove :: Ord a => a -> SetTree a -> Maybe (SetMeasure a, SetElement a)
locateAbove value = Measured.locate (maybe False (> value) . measureLastKey)

splitAtLeast :: Ord a => a -> SetTree a -> Maybe (SetTree a, SetElement a, SetTree a)
splitAtLeast value = Measured.split (maybe False (>= value) . measureLastKey)

measureBeforeCount :: Maybe (SetMeasure a, SetElement a) -> SetMeasure a -> Int
measureBeforeCount located whole =
  case located of
    Just (before, _) -> measureCount before
    Nothing -> measureCount whole

-- Splits into the elements strictly below the probe and the rest, sharing structure with the
-- receiver on both sides.
splitBelow :: Ord a => a -> SetTree a -> (SetTree a, SetTree a)
splitBelow value values =
  case splitAtLeast value values of
    Nothing -> (values, Measured.empty)
    Just (below, stored, rest) -> (below, Measured.cons stored rest)

splitAtRank :: Int -> SetTree a -> (SetTree a, SetTree a)
splitAtRank position values
  | position <= 0 = (Measured.empty, values)
  | position >= measureCount (Measured.measureTree values) = (values, Measured.empty)
  | otherwise =
      case Measured.split (\measureValue -> measureCount measureValue > position) values of
        Nothing -> (values, Measured.empty)
        Just (left, stored, right) -> (left, Measured.cons stored right)

-- The three merges share one shape: compare the two front elements, adopt the leading run of one
-- operand up to the other's front by a boundary split, and recurse on what remains. Each adopted
-- run costs one split at its length's logarithm, so the total is O(m log(n/m + 1)) rather than an
-- element-by-element Theta(m log(n + m)).
unionTrees :: Ord a => SetTree a -> SetTree a -> SetTree a
unionTrees left right =
  case Measured.viewL left of
    Measured.EmptyL -> right
    leftFirst Measured.:< _ ->
      case Measured.viewL right of
        Measured.EmptyL -> left
        rightFirst Measured.:< rightRest ->
          case compareElements leftFirst rightFirst of
            LT ->
              let (run, leftRemaining) = splitBelow (unElement rightFirst) left
               in Measured.append run (unionTrees leftRemaining right)
            EQ -> unionTrees left rightRest
            GT ->
              let (run, rightRemaining) = splitBelow (unElement leftFirst) right
               in Measured.append run (unionTrees left rightRemaining)

intersectionTrees :: Ord a => SetTree a -> SetTree a -> SetTree a
intersectionTrees left right =
  case Measured.viewL left of
    Measured.EmptyL -> Measured.empty
    leftFirst Measured.:< leftRest ->
      case Measured.viewL right of
        Measured.EmptyL -> Measured.empty
        rightFirst Measured.:< rightRest ->
          case compareElements leftFirst rightFirst of
            LT ->
              let (_, leftRemaining) = splitBelow (unElement rightFirst) left
               in intersectionTrees leftRemaining right
            EQ -> Measured.cons leftFirst (intersectionTrees leftRest rightRest)
            GT ->
              let (_, rightRemaining) = splitBelow (unElement leftFirst) right
               in intersectionTrees left rightRemaining

differenceTrees :: Ord a => SetTree a -> SetTree a -> SetTree a
differenceTrees left right =
  case Measured.viewL left of
    Measured.EmptyL -> Measured.empty
    leftFirst Measured.:< leftRest ->
      case Measured.viewL right of
        Measured.EmptyL -> left
        rightFirst Measured.:< rightRest ->
          case compareElements leftFirst rightFirst of
            LT ->
              let (run, leftRemaining) = splitBelow (unElement rightFirst) left
               in Measured.append run (differenceTrees leftRemaining right)
            EQ -> differenceTrees leftRest rightRest
            GT ->
              let (_, rightRemaining) = splitBelow (unElement leftFirst) right
               in differenceTrees left rightRemaining

subsetTrees :: Ord a => SetTree a -> SetTree a -> Bool
subsetTrees left right =
  case Measured.viewL left of
    Measured.EmptyL -> True
    leftFirst Measured.:< leftRest ->
      case splitAtLeast (unElement leftFirst) right of
        Nothing -> False
        Just (_, stored, rightRest)
          | sameClass stored (unElement leftFirst) -> subsetTrees leftRest rightRest
          | otherwise -> False

overlapsTrees :: Ord a => SetTree a -> SetTree a -> Bool
overlapsTrees left right =
  case Measured.viewL left of
    Measured.EmptyL -> False
    leftFirst Measured.:< _ ->
      case Measured.viewL right of
        Measured.EmptyL -> False
        rightFirst Measured.:< _ ->
          case compareElements leftFirst rightFirst of
            LT ->
              let (_, leftRemaining) = splitBelow (unElement rightFirst) left
               in overlapsTrees leftRemaining right
            EQ -> True
            GT ->
              let (_, rightRemaining) = splitBelow (unElement leftFirst) right
               in overlapsTrees left rightRemaining

compareElements :: Ord a => SetElement a -> SetElement a -> Ordering
compareElements (SetElement left) (SetElement right) = compare left right
