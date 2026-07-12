{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MagicHash #-}

module Data.Structures.FingerTree.RrbVector
  ( RrbVector
  , RrbStatistics(..)
  , empty
  , singleton
  , fromList
  , toList
  , count
  , null
  , height
  , index
  , setAt
  , cons
  , snoc
  , append
  , splitAt
  , insertListAt
  , removeRange
  , unsnoc
  , validateStructure
  , sharesRootWith
  ) where

import Prelude hiding (null, splitAt)
import qualified Prelude as P
import Data.Array (Array, (!), bounds, elems, listArray)
import Data.Bits (finiteBitSize, shiftL, shiftR)
import Control.Exception (evaluate)
import Data.Maybe (isNothing)
import System.Mem.StableName (eqStableName, makeStableName)
import GHC.Exts (isTrue#, reallyUnsafePtrEquality#)

sameValue :: Eq a => a -> a -> Bool
sameValue left right = isTrue# (reallyUnsafePtrEquality# left right) || left == right
{-# INLINE sameValue #-}

branchFactor :: Int
branchFactor = 32

radixBits :: Int
radixBits = 5

maximumHeight :: Int
maximumHeight = (finiteBitSize (0 :: Int) - 1) `div` radixBits

data RrbVector a = RrbVector !(Maybe (Node a))

data Node a
  = Leaf !(Array Int a)
  | Branch !Int !Int !(Array Int (Node a)) !(Maybe (Array Int Int))

data RrbStatistics = RrbStatistics
  { statisticsCount :: !Int
  , statisticsHeight :: !Int
  , statisticsLeafCount :: !Int
  , statisticsBranchCount :: !Int
  , statisticsRegularBranchCount :: !Int
  , statisticsRelaxedBranchCount :: !Int
  , statisticsMinimumLeafLength :: !Int
  , statisticsMaximumLeafLength :: !Int
  , statisticsMinimumBranchingFactor :: !Int
  , statisticsMaximumBranchingFactor :: !Int
  }
  deriving (Eq, Show)

instance Eq a => Eq (RrbVector a) where
  left == right = toList left == toList right

instance Show a => Show (RrbVector a) where
  showsPrec precedence vector = showsPrec precedence (toList vector)

instance Semigroup (RrbVector a) where
  (<>) = append

instance Monoid (RrbVector a) where
  mempty = empty

empty :: RrbVector a
empty = RrbVector Nothing

singleton :: a -> RrbVector a
singleton value = RrbVector (Just (leafFromList [value]))

fromList :: [a] -> RrbVector a
fromList values =
  case map leafFromList (chunksOf branchFactor values) of
    [] -> empty
    leaves -> RrbVector (Just (buildLevel leaves))

toList :: RrbVector a -> [a]
toList (RrbVector Nothing) = []
toList (RrbVector (Just root)) = nodeToList root

count :: RrbVector a -> Int
count (RrbVector Nothing) = 0
count (RrbVector (Just root)) = nodeCount root

null :: RrbVector a -> Bool
null (RrbVector root) = isNothing root

height :: RrbVector a -> Int
height (RrbVector Nothing) = 0
height (RrbVector (Just root)) = nodeHeight root

index :: Int -> RrbVector a -> Maybe a
index position (RrbVector root)
  | position < 0 = Nothing
  | otherwise = do
      node <- root
      if position >= nodeCount node
        then Nothing
        else Just (indexNode position node)

setAt :: Eq a => Int -> a -> RrbVector a -> Maybe (RrbVector a)
setAt position value vector@(RrbVector root)
  | position < 0 = Nothing
  | otherwise = do
      node <- root
      if position >= nodeCount node
        then Nothing
        else
          let (updated, changed) = setNode position value node
          in Just (if changed then RrbVector (Just updated) else vector)

cons :: a -> RrbVector a -> RrbVector a
cons value vector = append (singleton value) vector

snoc :: RrbVector a -> a -> RrbVector a
snoc vector value = append vector (singleton value)

append :: RrbVector a -> RrbVector a -> RrbVector a
append (RrbVector Nothing) right = right
append left (RrbVector Nothing) = left
append (RrbVector (Just left)) (RrbVector (Just right)) =
  let !_ = checkedAdd (nodeCount left) (nodeCount right)
      roots = concatNodes left right
  in fromRoots roots

splitAt :: Int -> RrbVector a -> Maybe (RrbVector a, RrbVector a)
splitAt position vector@(RrbVector root)
  | position < 0 || position > count vector = Nothing
  | position == 0 = Just (empty, vector)
  | position == count vector = Just (vector, empty)
  | otherwise = do
      node <- root
      let (left, right) = splitNode position node
      pure (fromRoot left, fromRoot right)

insertListAt :: Int -> [a] -> RrbVector a -> Maybe (RrbVector a)
insertListAt position values vector = do
  (left, right) <- splitAt position vector
  let middle = fromList values
  pure (if null middle then vector else append (append left middle) right)

removeRange :: Int -> Int -> RrbVector a -> Maybe (RrbVector a)
removeRange position amount vector
  | position < 0 || amount < 0 = Nothing
  | amount > count vector - position = Nothing
  | amount == 0 = Just vector
  | otherwise = do
      (left, tailVector) <- splitAt position vector
      (_, right) <- splitAt amount tailVector
      pure (append left right)

unsnoc :: RrbVector a -> Maybe (RrbVector a, a)
unsnoc vector
  | null vector = Nothing
  | otherwise = do
      value <- index (count vector - 1) vector
      remaining <- removeRange (count vector - 1) 1 vector
      pure (remaining, value)

validateStructure :: RrbVector a -> Maybe RrbStatistics
validateStructure (RrbVector Nothing) = Just (RrbStatistics 0 0 0 0 0 0 0 0 0 0)
validateStructure (RrbVector (Just root)) = do
  (actualCount, actualHeight, accumulator) <- validateNode True root
  if actualCount /= nodeCount root || actualHeight /= nodeHeight root || actualHeight > maximumHeight
    then Nothing
    else Just (finishStatistics actualCount actualHeight accumulator)

sharesRootWith :: RrbVector a -> RrbVector a -> IO Bool
sharesRootWith (RrbVector Nothing) (RrbVector Nothing) = pure True
sharesRootWith (RrbVector (Just left)) (RrbVector (Just right)) = do
  evaluatedLeft <- evaluate left
  evaluatedRight <- evaluate right
  leftName <- makeStableName evaluatedLeft
  rightName <- makeStableName evaluatedRight
  pure (leftName `eqStableName` rightName)
sharesRootWith _ _ = pure False

indexNode :: Int -> Node a -> a
indexNode position (Leaf values) = values ! position
indexNode position branch@(Branch _ _ children _) =
  let (childIndex, before) = findChild position branch
  in indexNode (position - before) (children ! childIndex)

setNode :: Eq a => Int -> a -> Node a -> (Node a, Bool)
setNode position value leaf@(Leaf values)
  | sameValue (values ! position) value = (leaf, False)
  | otherwise = (Leaf (replaceArray position value values), True)
setNode position value branch@(Branch _ _ children _) =
  let (childIndex, before) = findChild position branch
      (child, changed) = setNode (position - before) value (children ! childIndex)
  in if not changed
       then (branch, False)
       else (mkBranch (replaceArray childIndex child children), True)

concatNodes :: Node a -> Node a -> [Node a]
concatNodes left right
  | nodeHeight left == nodeHeight right = concatSameHeight left right
  | nodeHeight left > nodeHeight right =
      case left of
        Branch _ _ children _ ->
          let childList = elems children
              (prefix, boundaryChild) = splitLast childList
              boundary = concatNodes boundaryChild right
          in partitionNodes (prefix ++ boundary)
        Leaf _ -> error "RRB height invariant violated"
  | otherwise =
      case right of
        Branch _ _ children _ ->
          case elems children of
            [] -> error "RRB branch invariant violated"
            boundaryChild : suffix -> partitionNodes (concatNodes left boundaryChild ++ suffix)
        Leaf _ -> error "RRB height invariant violated"

concatSameHeight :: Node a -> Node a -> [Node a]
concatSameHeight left@(Leaf leftValues) right@(Leaf rightValues)
  | arrayLength leftValues == branchFactor && arrayLength rightValues == branchFactor = [left, right]
  | otherwise =
      let combined = elems leftValues ++ elems rightValues
      in if length combined <= branchFactor
           then [leafFromList combined]
           else
             let middle = length combined `div` 2
             in [leafFromList (take middle combined), leafFromList (drop middle combined)]
concatSameHeight (Branch _ _ leftChildren _) (Branch _ _ rightChildren _) =
  case (splitLast (elems leftChildren), elems rightChildren) of
    ((leftPrefix, leftBoundary), rightBoundary : rightSuffix) ->
      partitionNodes (leftPrefix ++ concatSameHeight leftBoundary rightBoundary ++ rightSuffix)
    _ -> error "RRB branch invariant violated"
concatSameHeight _ _ = error "RRB equal-height node kinds differ"

partitionNodes :: [Node a] -> [Node a]
partitionNodes children
  | length children <= branchFactor = [mkBranch (arrayFromList children)]
  | otherwise =
      let middle = length children `div` 2
      in [mkBranch (arrayFromList (take middle children)), mkBranch (arrayFromList (drop middle children))]

splitNode :: Int -> Node a -> (Maybe (Node a), Maybe (Node a))
splitNode position node
  | position == 0 = (Nothing, Just node)
  | position == nodeCount node = (Just node, Nothing)
splitNode position (Leaf values) =
  (Just (leafFromList (take position (elems values))), Just (leafFromList (drop position (elems values))))
splitNode position branch@(Branch _ _ children _) =
  let (childIndex, before) = findChild position branch
      (childLeft, childRight) = splitNode (position - before) (children ! childIndex)
      childList = elems children
      left = take childIndex childList ++ maybeToList childLeft
      right = maybeToList childRight ++ drop (childIndex + 1) childList
  in (buildSameHeight left, buildSameHeight right)

buildSameHeight :: [Node a] -> Maybe (Node a)
buildSameHeight [] = Nothing
buildSameHeight nodes = Just (mkBranch (arrayFromList nodes))

fromRoots :: [Node a] -> RrbVector a
fromRoots [] = empty
fromRoots [root] = fromRoot (Just root)
fromRoots roots = fromRoot (Just (mkBranch (arrayFromList roots)))

fromRoot :: Maybe (Node a) -> RrbVector a
fromRoot Nothing = empty
fromRoot (Just (Branch _ _ children _))
  | arrayLength children == 1 = fromRoot (Just (children ! 0))
fromRoot root = RrbVector root

buildLevel :: [Node a] -> Node a
buildLevel [] = error "RRB level cannot be empty"
buildLevel [node] = node
buildLevel nodes = buildLevel (map (mkBranch . arrayFromList) (chunksOf branchFactor nodes))

mkBranch :: Array Int (Node a) -> Node a
mkBranch children =
  case elems children of
    [] -> error "RRB branch arity is outside 1..32"
    childList@(firstChild : _)
      | length childList > branchFactor -> error "RRB branch arity is outside 1..32"
      | any ((/= childHeight) . nodeHeight) childList -> error "RRB branch children have unequal heights"
      | otherwise -> Branch total branchHeight children sizes
      where
        childHeight = nodeHeight firstChild
        branchHeight = checkedAdd childHeight 1
        total = checkedSum (map nodeCount childList)
        sizes
          | hasRegularLayout childList branchHeight = Nothing
          | otherwise = Just (arrayFromList (drop 1 (scanl checkedAdd 0 (map nodeCount childList))))

hasRegularLayout :: [Node a] -> Int -> Bool
hasRegularLayout [] _ = False
hasRegularLayout children branchHeight
  | branchHeight < 1 || branchHeight > maximumHeight = False
  | otherwise =
      let capacity = (1 :: Integer) `shiftL` (branchHeight * radixBits)
          (prefix, finalChild) = splitLast children
      in all ((== capacity) . toInteger . nodeCount) prefix
           && toInteger (nodeCount finalChild) <= capacity

findChild :: Int -> Node a -> (Int, Int)
findChild position (Branch _ branchHeight children Nothing) =
  let childIndex = position `shiftR` (branchHeight * radixBits)
      before = childIndex `shiftL` (branchHeight * radixBits)
  in if childIndex < arrayLength children
       then (childIndex, before)
       else error "RRB regular child selection escaped the branch"
findChild position (Branch _ _ _ (Just sizes)) = go 0
  where
    go childIndex
      | position < sizes ! childIndex = (childIndex, if childIndex == 0 then 0 else sizes ! (childIndex - 1))
      | otherwise = go (childIndex + 1)
findChild _ (Leaf _) = error "RRB child selection requires a branch"

nodeCount :: Node a -> Int
nodeCount (Leaf values) = arrayLength values
nodeCount (Branch value _ _ _) = value

nodeHeight :: Node a -> Int
nodeHeight (Leaf _) = 0
nodeHeight (Branch _ value _ _) = value

nodeToList :: Node a -> [a]
nodeToList (Leaf values) = elems values
nodeToList (Branch _ _ children _) = concatMap nodeToList (elems children)

leafFromList :: [a] -> Node a
leafFromList values
  | P.null values || length values > branchFactor = error "RRB leaf length is outside 1..32"
  | otherwise = Leaf (arrayFromList values)

arrayFromList :: [a] -> Array Int a
arrayFromList values = listArray (0, length values - 1) values

arrayLength :: Array Int a -> Int
arrayLength values =
  let (lower, upper) = bounds values
  in upper - lower + 1

replaceArray :: Int -> a -> Array Int a -> Array Int a
replaceArray position value source =
  let (lower, upper) = bounds source
  in listArray (lower, upper) [if current == position then value else source ! current | current <- [lower .. upper]]

chunksOf :: Int -> [a] -> [[a]]
chunksOf _ [] = []
chunksOf amount values =
  let (chunk, rest) = splitAtList amount values
  in chunk : chunksOf amount rest

splitAtList :: Int -> [a] -> ([a], [a])
splitAtList amount = go amount []
  where
    go remaining prefix rest
      | remaining == 0 = (reverse prefix, rest)
    go _ prefix [] = (reverse prefix, [])
    go remaining prefix (value : rest) = go (remaining - 1) (value : prefix) rest

splitLast :: [a] -> ([a], a)
splitLast [] = error "RRB internal list cannot be empty"
splitLast (value : values) = go [] value values
  where
    go prefix finalValue [] = (reverse prefix, finalValue)
    go prefix previous (next : rest) = go (previous : prefix) next rest

maybeToList :: Maybe a -> [a]
maybeToList Nothing = []
maybeToList (Just value) = [value]

checkedAdd :: Int -> Int -> Int
checkedAdd left right =
  let result = toInteger left + toInteger right
  in if result > toInteger (maxBound :: Int)
       then error "RRB vector count exceeds maxBound :: Int"
       else fromInteger result

checkedSum :: [Int] -> Int
checkedSum = foldl' checkedAdd 0

data ValidationAccumulator = ValidationAccumulator
  { accumulatorLeafCount :: !Int
  , accumulatorBranchCount :: !Int
  , accumulatorRegularCount :: !Int
  , accumulatorRelaxedCount :: !Int
  , accumulatorMinimumLeaf :: !Int
  , accumulatorMaximumLeaf :: !Int
  , accumulatorMinimumBranching :: !Int
  , accumulatorMaximumBranching :: !Int
  }

emptyAccumulator :: ValidationAccumulator
emptyAccumulator = ValidationAccumulator 0 0 0 0 maxBound 0 maxBound 0

validateNode :: Bool -> Node a -> Maybe (Int, Int, ValidationAccumulator)
validateNode _ (Leaf values)
  | leafLength < 1 || leafLength > branchFactor = Nothing
  | otherwise = Just (leafLength, 0, addLeaf leafLength emptyAccumulator)
  where
    leafLength = arrayLength values
validateNode isRoot branch@(Branch storedCount storedHeight children sizes)
  | arity < 1 || arity > branchFactor || isRoot && arity == 1 = Nothing
  | otherwise = do
      validated <- traverse (validateNode False) childList
      let counts = [childCount | (childCount, _, _) <- validated]
          heights = [childHeight | (_, childHeight, _) <- validated]
          expectedCountInteger = sum (map toInteger counts)
          combined = foldl' combineAccumulator emptyAccumulator [item | (_, _, item) <- validated]
      case heights of
        [] -> Nothing
        firstHeight : remainingHeights ->
          let expectedHeight = firstHeight + 1
              regular = hasRegularLayout childList expectedHeight
              metadataValid =
                all (== firstHeight) remainingHeights
                  && expectedCountInteger == toInteger storedCount
                  && expectedHeight == storedHeight
                  && validateSizes regular counts sizes
                  && findChildSamples branch
          in if metadataValid
               then Just (storedCount, storedHeight, addBranch arity regular combined)
               else Nothing
  where
    childList = elems children
    arity = length childList

validateSizes :: Bool -> [Int] -> Maybe (Array Int Int) -> Bool
validateSizes True _ Nothing = True
validateSizes False counts (Just sizes) = elems sizes == drop 1 (scanl (+) 0 counts)
validateSizes _ _ _ = False

findChildSamples :: Node a -> Bool
findChildSamples branch@(Branch _ _ children _) = all valid samplePositions
  where
    total = nodeCount branch
    samplePositions = [0, total `div` 2, total - 1]
    valid position =
      let (childIndex, before) = findChild position branch
          local = position - before
      in childIndex >= 0
           && childIndex < arrayLength children
           && local >= 0
           && local < nodeCount (children ! childIndex)
findChildSamples (Leaf _) = False

addLeaf :: Int -> ValidationAccumulator -> ValidationAccumulator
addLeaf lengthValue accumulator = accumulator
  { accumulatorLeafCount = accumulatorLeafCount accumulator + 1
  , accumulatorMinimumLeaf = min (accumulatorMinimumLeaf accumulator) lengthValue
  , accumulatorMaximumLeaf = max (accumulatorMaximumLeaf accumulator) lengthValue
  }

addBranch :: Int -> Bool -> ValidationAccumulator -> ValidationAccumulator
addBranch arity regular accumulator = accumulator
  { accumulatorBranchCount = accumulatorBranchCount accumulator + 1
  , accumulatorRegularCount = accumulatorRegularCount accumulator + if regular then 1 else 0
  , accumulatorRelaxedCount = accumulatorRelaxedCount accumulator + if regular then 0 else 1
  , accumulatorMinimumBranching = min (accumulatorMinimumBranching accumulator) arity
  , accumulatorMaximumBranching = max (accumulatorMaximumBranching accumulator) arity
  }

combineAccumulator :: ValidationAccumulator -> ValidationAccumulator -> ValidationAccumulator
combineAccumulator left right = ValidationAccumulator
  { accumulatorLeafCount = accumulatorLeafCount left + accumulatorLeafCount right
  , accumulatorBranchCount = accumulatorBranchCount left + accumulatorBranchCount right
  , accumulatorRegularCount = accumulatorRegularCount left + accumulatorRegularCount right
  , accumulatorRelaxedCount = accumulatorRelaxedCount left + accumulatorRelaxedCount right
  , accumulatorMinimumLeaf = min (accumulatorMinimumLeaf left) (accumulatorMinimumLeaf right)
  , accumulatorMaximumLeaf = max (accumulatorMaximumLeaf left) (accumulatorMaximumLeaf right)
  , accumulatorMinimumBranching = min (accumulatorMinimumBranching left) (accumulatorMinimumBranching right)
  , accumulatorMaximumBranching = max (accumulatorMaximumBranching left) (accumulatorMaximumBranching right)
  }

finishStatistics :: Int -> Int -> ValidationAccumulator -> RrbStatistics
finishStatistics valueCount valueHeight accumulator = RrbStatistics
  { statisticsCount = valueCount
  , statisticsHeight = valueHeight
  , statisticsLeafCount = accumulatorLeafCount accumulator
  , statisticsBranchCount = accumulatorBranchCount accumulator
  , statisticsRegularBranchCount = accumulatorRegularCount accumulator
  , statisticsRelaxedBranchCount = accumulatorRelaxedCount accumulator
  , statisticsMinimumLeafLength = if accumulatorLeafCount accumulator == 0 then 0 else accumulatorMinimumLeaf accumulator
  , statisticsMaximumLeafLength = accumulatorMaximumLeaf accumulator
  , statisticsMinimumBranchingFactor = if accumulatorBranchCount accumulator == 0 then 0 else accumulatorMinimumBranching accumulator
  , statisticsMaximumBranchingFactor = accumulatorMaximumBranching accumulator
  }
