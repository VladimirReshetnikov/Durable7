{-# LANGUAGE MultiParamTypeClasses #-}

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

import qualified Data.Foldable as Foldable
import qualified Data.List as List
import qualified Data.Sequence as Seq
import qualified Data.Structures.FingerTree.Measured as Measured

-- The measure combines positional weight with the last key in each prefix.
-- Key predicates are monotone because buckets are stored in ascending order.
data BagMeasure a = BagMeasure !Int !Int !(Maybe a)
  deriving (Eq, Ord, Read, Show)

instance Semigroup (BagMeasure a) where
  BagMeasure leftCount leftDistinct leftKey <> BagMeasure rightCount rightDistinct rightKey =
    BagMeasure
      (leftCount + rightCount)
      (leftDistinct + rightDistinct)
      (case rightKey of
        Just _ -> rightKey
        Nothing -> leftKey)

instance Monoid (BagMeasure a) where
  mempty = BagMeasure 0 0 Nothing

-- Buckets retain every comparer-equal instance in insertion order. Sequence
-- gives logarithmic rank access even when every bag element has one key.
data Bucket a = Bucket !a !(Seq.Seq a)
  deriving (Eq, Ord, Read, Show)

instance Measured.Measured (BagMeasure a) (Bucket a) where
  measure (Bucket key values) = BagMeasure (Seq.length values) 1 (Just key)

newtype SortedBag a = SortedBag (Measured.FingerTree (BagMeasure a) (Bucket a))
  deriving (Read, Show)

instance Eq a => Eq (SortedBag a) where
  left == right = toList left == toList right

instance Ord a => Ord (SortedBag a) where
  compare left right = compare (toList left) (toList right)

empty :: SortedBag a
empty = SortedBag Measured.empty

singleton :: a -> SortedBag a
singleton value = SortedBag (Measured.singleton (singletonBucket value))

fromList :: Ord a => [a] -> SortedBag a
fromList = List.foldl' (flip insert) empty

toList :: SortedBag a -> [a]
toList (SortedBag buckets) = concatMap bucketValues (Measured.toList buckets)

toCounts :: SortedBag a -> [(a, Int)]
toCounts (SortedBag buckets) = map summarize (Measured.toList buckets)
  where
    summarize (Bucket key values) = (key, Seq.length values)

count :: SortedBag a -> Int
count (SortedBag buckets) = measureCount (Measured.measureTree buckets)

null :: SortedBag a -> Bool
null (SortedBag buckets) = Measured.null buckets

distinctCount :: SortedBag a -> Int
distinctCount (SortedBag buckets) = measureDistinct (Measured.measureTree buckets)

member :: Ord a => a -> SortedBag a -> Bool
member value bag = countOf value bag > 0

countOf :: Ord a => a -> SortedBag a -> Int
countOf value (SortedBag buckets) =
  case locateAtLeast value buckets of
    Just (_, bucket)
      | bucketKey bucket == value -> bucketLength bucket
    _ -> 0

-- The first stored equal instance remains the bucket key and toCounts
-- representative. New instances append after existing ones.
insert :: Ord a => a -> SortedBag a -> SortedBag a
insert value (SortedBag buckets) =
  case splitAtLeast value buckets of
    Nothing -> SortedBag (Measured.snoc buckets (singletonBucket value))
    Just (left, bucket, right)
      | bucketKey bucket == value ->
          SortedBag (joinWithBucket left (appendBucket value bucket) right)
      | otherwise ->
          SortedBag
            (Measured.append
              (Measured.snoc left (singletonBucket value))
              (Measured.cons bucket right))

deleteOne :: Ord a => a -> SortedBag a -> SortedBag a
deleteOne value bag@(SortedBag buckets) =
  case splitAtLeast value buckets of
    Just (left, bucket, right)
      | bucketKey bucket == value ->
          case Seq.viewl (bucketSequence bucket) of
            Seq.EmptyL -> bag
            _ Seq.:< rest
              | Seq.null rest -> SortedBag (Measured.append left right)
              | otherwise -> SortedBag (joinWithBucket left (Bucket (bucketKey bucket) rest) right)
    _ -> bag

deleteAll :: Ord a => a -> SortedBag a -> SortedBag a
deleteAll value bag@(SortedBag buckets) =
  case splitAtLeast value buckets of
    Just (left, bucket, right)
      | bucketKey bucket == value -> SortedBag (Measured.append left right)
    _ -> bag

minValue :: SortedBag a -> Maybe a
minValue (SortedBag buckets) = firstBucketValue =<< Measured.head buckets

maxValue :: SortedBag a -> Maybe a
maxValue (SortedBag buckets) = lastBucketValue =<< Measured.last buckets

countLessThan :: Ord a => a -> SortedBag a -> Int
countLessThan value (SortedBag buckets) =
  measureBeforeCount (locateAtLeast value buckets) (Measured.measureTree buckets)

countAtMost :: Ord a => a -> SortedBag a -> Int
countAtMost value (SortedBag buckets) =
  measureBeforeCount (locateAbove value buckets) (Measured.measureTree buckets)

index :: Int -> SortedBag a -> Maybe a
index position bag@(SortedBag buckets)
  | position < 0 || position >= count bag = Nothing
  | otherwise = do
      (BagMeasure before _ _, bucket) <-
        Measured.locate (\measureValue -> measureCount measureValue > position) buckets
      Seq.lookup (position - before) (bucketSequence bucket)

slice :: Ord a => Int -> Int -> SortedBag a -> Maybe (SortedBag a)
slice position lengthValue bag@(SortedBag buckets)
  | not (isValidRange position lengthValue (count bag)) = Nothing
  | otherwise =
      let (_, afterPosition) = splitAtRank position buckets
          (selected, _) = splitAtRank lengthValue afterPosition
       in Just (SortedBag selected)

singletonBucket :: a -> Bucket a
singletonBucket value = Bucket value (Seq.singleton value)

bucketKey :: Bucket a -> a
bucketKey (Bucket key _) = key

bucketSequence :: Bucket a -> Seq.Seq a
bucketSequence (Bucket _ values) = values

bucketValues :: Bucket a -> [a]
bucketValues = Foldable.toList . bucketSequence

bucketLength :: Bucket a -> Int
bucketLength = Seq.length . bucketSequence

appendBucket :: a -> Bucket a -> Bucket a
appendBucket value (Bucket key values) = Bucket key (values Seq.|> value)

firstBucketValue :: Bucket a -> Maybe a
firstBucketValue = Seq.lookup 0 . bucketSequence

lastBucketValue :: Bucket a -> Maybe a
lastBucketValue bucket = Seq.lookup (bucketLength bucket - 1) (bucketSequence bucket)

measureCount :: BagMeasure a -> Int
measureCount (BagMeasure total _ _) = total

measureDistinct :: BagMeasure a -> Int
measureDistinct (BagMeasure _ total _) = total

measureLastKey :: BagMeasure a -> Maybe a
measureLastKey (BagMeasure _ _ key) = key

locateAtLeast :: Ord a => a -> Measured.FingerTree (BagMeasure a) (Bucket a) -> Maybe (BagMeasure a, Bucket a)
locateAtLeast value = Measured.locate (maybe False (>= value) . measureLastKey)

locateAbove :: Ord a => a -> Measured.FingerTree (BagMeasure a) (Bucket a) -> Maybe (BagMeasure a, Bucket a)
locateAbove value = Measured.locate (maybe False (> value) . measureLastKey)

splitAtLeast
  :: Ord a
  => a
  -> Measured.FingerTree (BagMeasure a) (Bucket a)
  -> Maybe
      ( Measured.FingerTree (BagMeasure a) (Bucket a)
      , Bucket a
      , Measured.FingerTree (BagMeasure a) (Bucket a)
      )
splitAtLeast value = Measured.split (maybe False (>= value) . measureLastKey)

joinWithBucket
  :: Measured.FingerTree (BagMeasure a) (Bucket a)
  -> Bucket a
  -> Measured.FingerTree (BagMeasure a) (Bucket a)
  -> Measured.FingerTree (BagMeasure a) (Bucket a)
joinWithBucket left bucket right = Measured.append (Measured.snoc left bucket) right

measureBeforeCount :: Maybe (BagMeasure a, Bucket a) -> BagMeasure a -> Int
measureBeforeCount located whole =
  case located of
    Just (before, _) -> measureCount before
    Nothing -> measureCount whole

splitAtRank
  :: Int
  -> Measured.FingerTree (BagMeasure a) (Bucket a)
  -> ( Measured.FingerTree (BagMeasure a) (Bucket a)
     , Measured.FingerTree (BagMeasure a) (Bucket a)
     )
splitAtRank position buckets
  | position <= 0 = (Measured.empty, buckets)
  | position >= measureCount (Measured.measureTree buckets) = (buckets, Measured.empty)
  | otherwise =
      case Measured.split (\measureValue -> measureCount measureValue > position) buckets of
        Nothing -> (buckets, Measured.empty)
        Just (left, bucket, right) ->
          let offset = position - measureCount (Measured.measureTree left)
              (leftValues, rightValues) = Seq.splitAt offset (bucketSequence bucket)
              leftTree =
                if Seq.null leftValues
                  then left
                  else Measured.snoc left (Bucket (bucketKey bucket) leftValues)
              rightTree =
                if Seq.null rightValues
                  then right
                  else Measured.cons (Bucket (bucketKey bucket) rightValues) right
           in (leftTree, rightTree)

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position
