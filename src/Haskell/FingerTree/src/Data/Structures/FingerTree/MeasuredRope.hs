{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MultiParamTypeClasses #-}

module Data.Structures.FingerTree.MeasuredRope
  ( MeasuredRope
  , emptyWith
  , singletonWith
  , fromListWith
  , toList
  , count
  , null
  , measure
  , prefixMeasure
  , append
  , index
  , setAt
  , insertAt
  , deleteAt
  , splitAt
  , slice
  , locateByMeasure
  , splitByMeasure
  ) where

import Prelude hiding (null, splitAt)

import qualified Prelude as P
import qualified Data.List as List
import qualified Data.Structures.FingerTree.Measured as FT
import qualified Data.Structures.FingerTree.Rope as Rope

data RopeMeasure v = RopeMeasure !Int !v

instance Semigroup v => Semigroup (RopeMeasure v) where
  RopeMeasure leftCount left <> RopeMeasure rightCount right =
    RopeMeasure (leftCount + rightCount) (left <> right)

instance Monoid v => Monoid (RopeMeasure v) where
  mempty = RopeMeasure 0 mempty

data MeasuredChunk v a = MeasuredChunk !Int !v [a]

instance Monoid v => FT.Measured (RopeMeasure v) (MeasuredChunk v a) where
  measure (MeasuredChunk lengthValue value _) = RopeMeasure lengthValue value

-- The function is retained for editing boundary chunks. Cached chunk
-- measures let splits, concatenation, prefix queries, and measure-guided
-- location reuse the persistent finger-tree spine.
data MeasuredRope v a = MeasuredRope (a -> v) !(FT.FingerTree (RopeMeasure v) (MeasuredChunk v a))

emptyWith :: Monoid v => (a -> v) -> MeasuredRope v a
emptyWith elementMeasure = MeasuredRope elementMeasure FT.empty

singletonWith :: Monoid v => (a -> v) -> a -> MeasuredRope v a
singletonWith elementMeasure value =
  MeasuredRope elementMeasure (FT.singleton (measuredChunk elementMeasure [value]))

fromListWith :: Monoid v => (a -> v) -> [a] -> MeasuredRope v a
fromListWith elementMeasure values =
  MeasuredRope elementMeasure (FT.fromList (map (measuredChunk elementMeasure) (chunkify values)))

toList :: Monoid v => MeasuredRope v a -> [a]
toList (MeasuredRope _ tree) = concatMap measuredChunkItems (FT.toList tree)

count :: Monoid v => MeasuredRope v a -> Int
count (MeasuredRope _ tree) = measureCount (FT.measureTree tree)

null :: Monoid v => MeasuredRope v a -> Bool
null (MeasuredRope _ tree) = FT.null tree

measure :: Monoid v => MeasuredRope v a -> v
measure (MeasuredRope _ tree) = measureValue (FT.measureTree tree)

prefixMeasure :: Monoid v => Int -> MeasuredRope v a -> Maybe v
prefixMeasure lengthValue measured@(MeasuredRope elementMeasure tree)
  | lengthValue < 0 || lengthValue > count measured = Nothing
  | lengthValue == count measured = Just (measure measured)
  | otherwise = do
      (left, MeasuredChunk _ _ values, _) <- FT.split (past lengthValue) tree
      let local = lengthValue - treeCount left
      pure (treeValue left <> foldMap elementMeasure (take local values))

-- Both operands must have extensionally identical element-measure functions.
-- Haskell functions have no decidable equality, so this is the same policy
-- precondition that callers already observe when constructing related ropes.
append :: Monoid v => MeasuredRope v a -> MeasuredRope v a -> MeasuredRope v a
append left@(MeasuredRope elementMeasure leftTree) right@(MeasuredRope _ rightTree)
  | null left = right
  | null right = left
  | otherwise = MeasuredRope elementMeasure (FT.append leftTree rightTree)

index :: Monoid v => Int -> MeasuredRope v a -> Maybe a
index position measured@(MeasuredRope _ tree)
  | position < 0 || position >= count measured = Nothing
  | otherwise = do
      (left, MeasuredChunk _ _ values, _) <- FT.split (past position) tree
      listIndex (position - treeCount left) values

setAt :: Monoid v => Int -> a -> MeasuredRope v a -> Maybe (MeasuredRope v a)
setAt position value measured@(MeasuredRope elementMeasure tree)
  | position < 0 || position >= count measured = Nothing
  | otherwise = do
      (left, MeasuredChunk _ _ values, right) <- FT.split (past position) tree
      let local = position - treeCount left
          updated = measuredChunk elementMeasure (replaceAt local value values)
      pure (MeasuredRope elementMeasure (joinWithChunks left [updated] right))

insertAt :: Monoid v => Int -> a -> MeasuredRope v a -> Maybe (MeasuredRope v a)
insertAt position value measured@(MeasuredRope elementMeasure tree)
  | position < 0 || position > count measured = Nothing
  | position == count measured =
      Just (MeasuredRope elementMeasure (appendValue elementMeasure tree value))
  | otherwise = do
      (left, MeasuredChunk _ _ values, right) <- FT.split (past position) tree
      let local = position - treeCount left
          (before, after) = P.splitAt local values
          replacements = measuredChunks elementMeasure (before ++ value : after)
      pure (MeasuredRope elementMeasure (joinWithChunks left replacements right))

deleteAt :: Monoid v => Int -> MeasuredRope v a -> Maybe (MeasuredRope v a)
deleteAt position measured@(MeasuredRope elementMeasure tree)
  | position < 0 || position >= count measured = Nothing
  | otherwise = do
      (left, MeasuredChunk _ _ values, right) <- FT.split (past position) tree
      let local = position - treeCount left
          (before, after) = P.splitAt local values
          replacements = measuredChunks elementMeasure (before ++ drop 1 after)
      pure (MeasuredRope elementMeasure (joinWithChunks left replacements right))

splitAt :: Monoid v => Int -> MeasuredRope v a -> Maybe (MeasuredRope v a, MeasuredRope v a)
splitAt position measured@(MeasuredRope elementMeasure tree)
  | position < 0 || position > count measured = Nothing
  | position == 0 = Just (emptyWith elementMeasure, measured)
  | position == count measured = Just (measured, emptyWith elementMeasure)
  | otherwise = do
      (left, MeasuredChunk _ _ values, right) <- FT.split (past position) tree
      let local = position - treeCount left
          (before, after) = P.splitAt local values
          leftTree = appendChunks left (measuredChunks elementMeasure before)
          rightTree = prependChunks (measuredChunks elementMeasure after) right
      pure (MeasuredRope elementMeasure leftTree, MeasuredRope elementMeasure rightTree)

slice :: Monoid v => Int -> Int -> MeasuredRope v a -> Maybe (MeasuredRope v a)
slice position lengthValue measured
  | not (isValidRange position lengthValue (count measured)) = Nothing
  | otherwise = do
      (_, suffix) <- splitAt position measured
      fst <$> splitAt lengthValue suffix

locateByMeasure :: Monoid v => (v -> Bool) -> MeasuredRope v a -> Maybe (Int, v, a)
locateByMeasure predicate (MeasuredRope elementMeasure tree) = do
  (left, MeasuredChunk _ _ values, _) <- FT.split (predicate . measureValue) tree
  let start = treeCount left
      beforeChunk = treeValue left
  locateInChunk elementMeasure predicate start beforeChunk values

splitByMeasure :: Monoid v => (v -> Bool) -> MeasuredRope v a -> Maybe (MeasuredRope v a, a, MeasuredRope v a)
splitByMeasure predicate measured = do
  (position, _, value) <- locateByMeasure predicate measured
  (left, suffix) <- splitAt position measured
  (_, right) <- splitAt 1 suffix
  pure (left, value, right)

measureCount :: RopeMeasure v -> Int
measureCount (RopeMeasure total _) = total

measureValue :: RopeMeasure v -> v
measureValue (RopeMeasure _ value) = value

treeCount :: Monoid v => FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> Int
treeCount = measureCount . FT.measureTree

treeValue :: Monoid v => FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> v
treeValue = measureValue . FT.measureTree

past :: Int -> RopeMeasure v -> Bool
past position (RopeMeasure seen _) = seen > position

measuredChunk :: Monoid v => (a -> v) -> [a] -> MeasuredChunk v a
measuredChunk elementMeasure values = MeasuredChunk (length values) (foldMap elementMeasure values) values

measuredChunkItems :: MeasuredChunk v a -> [a]
measuredChunkItems (MeasuredChunk _ _ values) = values

measuredChunks :: Monoid v => (a -> v) -> [a] -> [MeasuredChunk v a]
measuredChunks elementMeasure = map (measuredChunk elementMeasure) . chunkify

appendValue :: Monoid v => (a -> v) -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> a -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a)
appendValue elementMeasure tree value =
  case FT.viewR tree of
    FT.EmptyR -> FT.singleton (measuredChunk elementMeasure [value])
    rest FT.:> chunk@(MeasuredChunk lengthValue _ values)
      | lengthValue < Rope.maxChunkSize -> FT.snoc rest (measuredChunk elementMeasure (values ++ [value]))
      | otherwise -> FT.snoc (FT.snoc rest chunk) (measuredChunk elementMeasure [value])

appendChunks :: Monoid v => FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> [MeasuredChunk v a] -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a)
appendChunks = List.foldl' FT.snoc

prependChunks :: Monoid v => [MeasuredChunk v a] -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a)
prependChunks values tree = foldr FT.cons tree values

joinWithChunks :: Monoid v => FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> [MeasuredChunk v a] -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a) -> FT.FingerTree (RopeMeasure v) (MeasuredChunk v a)
joinWithChunks left middle right = FT.append (appendChunks left middle) right

locateInChunk :: Monoid v => (a -> v) -> (v -> Bool) -> Int -> v -> [a] -> Maybe (Int, v, a)
locateInChunk elementMeasure predicate = go
  where
    go _ _ [] = Nothing
    go !position !before (value : rest)
      | predicate after = Just (position, before, value)
      | otherwise = go (position + 1) after rest
      where
        after = before <> elementMeasure value

chunkify :: [a] -> [[a]]
chunkify [] = []
chunkify values =
  let (chunk, rest) = P.splitAt Rope.maxChunkSize values
   in chunk : chunkify rest

replaceAt :: Int -> a -> [a] -> [a]
replaceAt 0 value (_ : rest) = value : rest
replaceAt position value (x : rest) = x : replaceAt (position - 1) value rest
replaceAt _ _ [] = []

listIndex :: Int -> [a] -> Maybe a
listIndex 0 (value : _) = Just value
listIndex position (_ : rest)
  | position > 0 = listIndex (position - 1) rest
listIndex _ _ = Nothing

isValidRange :: Int -> Int -> Int -> Bool
isValidRange position lengthValue total =
  position >= 0 && lengthValue >= 0 && position <= total && lengthValue <= total - position
