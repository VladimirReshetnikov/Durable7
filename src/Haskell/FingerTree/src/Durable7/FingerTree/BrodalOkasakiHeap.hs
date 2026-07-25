{-# LANGUAGE BangPatterns #-}

-- | Brodal and Okasaki's bootstrapped skew-binomial min-heap.  The global
-- minimum is stored at the rank-zero root; its children fuse the primitive
-- skew tree children with the embedded heap forest described by the paper.
module Durable7.FingerTree.BrodalOkasakiHeap
  ( BrodalOkasakiHeap
  , BrodalOkasakiHeapStatistics(..)
  , empty
  , singleton
  , fromList
  , count
  , null
  , minimum
  , insert
  , meld
  , minView
  , deleteMinimum
  , toList
  , validateStructure
  ) where

import Prelude hiding (minimum, null)

import qualified Data.List as List

data Tree a = Tree !Int !a ![Tree a]
  deriving (Eq, Ord, Read, Show)

data BrodalOkasakiHeap a = BrodalOkasakiHeap !Int !(Maybe (Tree a))
  deriving (Eq, Ord, Read, Show)

data BrodalOkasakiHeapStatistics = BrodalOkasakiHeapStatistics
  { brodalStatisticsCount :: !Int
  , brodalStatisticsRootForestLength :: !Int
  , brodalStatisticsMaximumRank :: !Int
  , brodalStatisticsMaximumDepth :: !Int
  }
  deriving (Eq, Ord, Read, Show)

empty :: BrodalOkasakiHeap a
empty = BrodalOkasakiHeap 0 Nothing

singleton :: a -> BrodalOkasakiHeap a
singleton value = BrodalOkasakiHeap 1 (Just (Tree 0 value []))

-- | O(n) construction by repeated worst-case-O(1) insertion.
fromList :: Ord a => [a] -> BrodalOkasakiHeap a
fromList = List.foldl' (flip insert) empty

count :: BrodalOkasakiHeap a -> Int
count (BrodalOkasakiHeap total _) = total

null :: BrodalOkasakiHeap a -> Bool
null (BrodalOkasakiHeap _ root) = case root of
  Nothing -> True
  Just _ -> False

minimum :: BrodalOkasakiHeap a -> Maybe a
minimum (BrodalOkasakiHeap _ Nothing) = Nothing
minimum (BrodalOkasakiHeap _ (Just (Tree _ value _))) = Just value

-- | Worst-case O(1) insertion.
insert :: Ord a => a -> BrodalOkasakiHeap a -> BrodalOkasakiHeap a
insert value (BrodalOkasakiHeap _ Nothing) = singleton value
insert value (BrodalOkasakiHeap total (Just root@(Tree _ rootValue rootChildren))) =
  BrodalOkasakiHeap (checkedAdd total 1) (Just updated)
  where
    updated
      | value <= rootValue = Tree 0 value [root]
      | otherwise = Tree 0 rootValue (skewInsert (Tree 0 value []) rootChildren)

-- | Worst-case O(1) meld.
meld :: Ord a => BrodalOkasakiHeap a -> BrodalOkasakiHeap a -> BrodalOkasakiHeap a
meld (BrodalOkasakiHeap _ Nothing) right = right
meld left (BrodalOkasakiHeap _ Nothing) = left
meld (BrodalOkasakiHeap leftCount (Just leftRoot@(Tree _ leftValue leftChildren)))
     (BrodalOkasakiHeap rightCount (Just rightRoot@(Tree _ rightValue rightChildren))) =
  BrodalOkasakiHeap (checkedAdd leftCount rightCount) (Just root)
  where
    root
      | leftValue <= rightValue = Tree 0 leftValue (skewInsert rightRoot leftChildren)
      | otherwise = Tree 0 rightValue (skewInsert leftRoot rightChildren)

-- | Removes the minimum in O(log n) worst-case time.
minView :: Ord a => BrodalOkasakiHeap a -> Maybe (a, BrodalOkasakiHeap a)
minView (BrodalOkasakiHeap _ Nothing) = Nothing
minView (BrodalOkasakiHeap total (Just (Tree _ value []))) =
  Just (value, if total == 1 then empty else error "invalid Brodal-Okasaki singleton count")
minView (BrodalOkasakiHeap total (Just (Tree _ value children))) =
  let (Tree rank minimumValue minimumChildren, remainder) = getMinimum children
      (zeros, trees, embeddedForest) = splitForest rank [] [] minimumChildren
      merged = skewMeld (skewMeld trees remainder) embeddedForest
      restored = List.foldl' (flip skewInsert) merged (reverse zeros)
      remaining = BrodalOkasakiHeap (checkedSubtract total 1) (Just (Tree 0 minimumValue restored))
  in Just (value, remaining)

deleteMinimum :: Ord a => BrodalOkasakiHeap a -> BrodalOkasakiHeap a
deleteMinimum heap = case minView heap of
  Nothing -> heap
  Just (_, remaining) -> remaining

-- | Structural order is deliberately unspecified and requires no comparisons.
toList :: BrodalOkasakiHeap a -> [a]
toList (BrodalOkasakiHeap _ Nothing) = []
toList (BrodalOkasakiHeap _ (Just root)) = go [root]
  where
    go [] = []
    go (Tree _ value children : pending) = value : go (children ++ pending)

-- | Validates the fused primitive-child/embedded-forest boundaries, skew
-- ranks, heap order, global rank-zero root, and logical cardinality.
validateStructure :: Ord a => BrodalOkasakiHeap a -> Maybe BrodalOkasakiHeapStatistics
validateStructure (BrodalOkasakiHeap total Nothing)
  | total == 0 = Just (BrodalOkasakiHeapStatistics 0 0 0 0)
  | otherwise = Nothing
validateStructure (BrodalOkasakiHeap total (Just root@(Tree rank _ children)))
  | rank /= 0 = Nothing
  | otherwise = do
      rootForestLength <- validateSkewForest children
      validated <- validateTrees [(root, 1)] 0 0 0
      if validatedCount validated /= total
        then Nothing
        else Just (BrodalOkasakiHeapStatistics
          total
          rootForestLength
          (validatedMaximumRank validated)
          (validatedMaximumDepth validated))

data Validated = Validated
  { validatedCount :: !Int
  , validatedMaximumRank :: !Int
  , validatedMaximumDepth :: !Int
  }

validateTrees :: Ord a => [(Tree a, Int)] -> Int -> Int -> Int -> Maybe Validated
validateTrees [] logicalCount maximumRank maximumDepth =
  Just (Validated logicalCount maximumRank maximumDepth)
validateTrees ((tree@(Tree rank value children), depth) : pending) logicalCount maximumRank maximumDepth
  | rank < 0 = Nothing
  | any (\(Tree _ childValue _) -> value > childValue) children = Nothing
  | otherwise = do
      embedded <- validateFusedChildren tree
      _ <- validateSkewForest embedded
      validateTrees
        ([(child, checkedAdd depth 1) | child <- children] ++ pending)
        (checkedAdd logicalCount 1)
        (max maximumRank rank)
        (max maximumDepth depth)

skewInsert :: Ord a => Tree a -> [Tree a] -> [Tree a]
skewInsert tree (first@(Tree firstRank _ _) : second@(Tree secondRank _ _) : rest)
  | firstRank == secondRank = skewLink tree first second : rest
skewInsert tree forest = tree : forest

skewLink :: Ord a => Tree a -> Tree a -> Tree a -> Tree a
skewLink zero@(Tree _ zeroValue zeroChildren)
         first@(Tree firstRank firstValue firstChildren)
         second@(Tree secondRank secondValue secondChildren)
  | firstRank /= secondRank = error "invalid skew-link ranks"
  | firstValue <= zeroValue && firstValue <= secondValue =
      Tree (firstRank + 1) firstValue (zero : second : firstChildren)
  | secondValue <= zeroValue && secondValue <= firstValue =
      Tree (secondRank + 1) secondValue (zero : first : secondChildren)
  | otherwise = Tree (firstRank + 1) zeroValue (first : second : zeroChildren)

link :: Ord a => Tree a -> Tree a -> Tree a
link first@(Tree firstRank firstValue firstChildren)
     second@(Tree secondRank secondValue secondChildren)
  | firstRank /= secondRank = error "invalid binomial-link ranks"
  | firstValue <= secondValue = Tree (firstRank + 1) firstValue (second : firstChildren)
  | otherwise = Tree (secondRank + 1) secondValue (first : secondChildren)

getMinimum :: Ord a => [Tree a] -> (Tree a, [Tree a])
getMinimum [] = error "empty Brodal-Okasaki forest"
getMinimum [tree] = (tree, [])
getMinimum (tree@(Tree _ value _) : rest) =
  let (minimumTree@(Tree _ minimumValue _), remainder) = getMinimum rest
  in if value <= minimumValue
       then (tree, rest)
       else (minimumTree, tree : remainder)

splitForest :: Int -> [Tree a] -> [Tree a] -> [Tree a] -> ([Tree a], [Tree a], [Tree a])
splitForest 0 zeros trees forest = (zeros, trees, forest)
splitForest _ _ _ [] = error "invalid skew-binomial forest invariant"
splitForest 1 zeros trees [first] = (zeros, first : trees, [])
splitForest _ _ _ [_] = error "invalid skew-binomial forest invariant"
splitForest rank zeros trees (first@(Tree firstRank _ _) : second@(Tree secondRank _ _) : rest)
  | rank == 1 && secondRank == 0 = (first : zeros, second : trees, rest)
  | rank == 1 = (zeros, first : trees, second : rest)
  | firstRank == secondRank = (zeros, first : second : trees, rest)
  | firstRank == 0 = splitForest (rank - 1) (first : zeros) (second : trees) rest
  | otherwise = splitForest (rank - 1) zeros (first : trees) (second : rest)

skewMeld :: Ord a => [Tree a] -> [Tree a] -> [Tree a]
skewMeld left right = unionUnique (uniquify left) (uniquify right)

uniquify :: Ord a => [Tree a] -> [Tree a]
uniquify [] = []
uniquify (tree : forest) = insertRanked tree (uniquify forest)

insertRanked :: Ord a => Tree a -> [Tree a] -> [Tree a]
insertRanked tree [] = [tree]
insertRanked tree@(Tree rank _ _) forest@(first@(Tree firstRank _ _) : rest)
  | rank < firstRank = tree : forest
  | rank == firstRank = insertRanked (link tree first) rest
  | otherwise = error "invalid ranked insertion order"

unionUnique :: Ord a => [Tree a] -> [Tree a] -> [Tree a]
unionUnique [] right = right
unionUnique left [] = left
unionUnique left@(first@(Tree leftRank _ _) : leftRest)
            right@(second@(Tree rightRank _ _) : rightRest)
  | leftRank < rightRank = first : unionUnique leftRest right
  | leftRank > rightRank = second : unionUnique left rightRest
  | otherwise = insertRanked (link first second) (unionUnique leftRest rightRest)

-- Decodes the primitive children c from the fused list c ++ f and returns f.
validateFusedChildren :: Tree a -> Maybe [Tree a]
validateFusedChildren (Tree initialRank _ initialForest) = go initialRank initialForest
  where
    go 0 forest = Just forest
    go _ [] = Nothing
    go 1 (Tree firstRank _ _ : rest)
      | firstRank /= 0 = Nothing
      | otherwise = case rest of
          [] -> Just []
          Tree secondRank _ _ : remaining
            | secondRank == 0 -> Just remaining
            | otherwise -> Just rest
    go _ [_] = Nothing
    go rank (Tree firstRank _ _ : second@(Tree secondRank _ _) : rest)
      | firstRank == secondRank =
          if firstRank == rank - 1 then Just rest else Nothing
      | firstRank == 0 =
          if secondRank == rank - 1 then go (rank - 1) rest else Nothing
      | firstRank == rank - 1 = go (rank - 1) (second : rest)
      | otherwise = Nothing

validateSkewForest :: [Tree a] -> Maybe Int
validateSkewForest = go 0 (-1) False
  where
    go !lengthValue _ _ [] = Just lengthValue
    go !lengthValue previousRank duplicate (Tree rank _ _ : rest)
      | rank < 0 || rank < previousRank = Nothing
      | rank == previousRank && (lengthValue /= 1 || duplicate) = Nothing
      | otherwise = go
          (lengthValue + 1)
          rank
          (duplicate || rank == previousRank)
          rest

checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | right > 0 && left > maxBound - right = error "Brodal-Okasaki count overflow"
  | otherwise = left + right

checkedSubtract :: Int -> Int -> Int
checkedSubtract left right
  | left < right = error "Brodal-Okasaki count underflow"
  | otherwise = left - right
