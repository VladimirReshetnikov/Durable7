{-# LANGUAGE BangPatterns #-}

-- | A persistent indexed sequence with algebraic lazy range updates.
--
-- The representation is an implicit-key AVL tree.  Every node caches its
-- height, element count, and ordered logical measure.  A pending tag has
-- already transformed the node value and cached measure, but not its child
-- roots.  Structural descent pushes that tag immutably; read-only descent
-- instead carries inherited tags without rebuilding the tree.
module Data.Structures.FingerTree.RangeUpdateSequence
  ( RangeUpdateAlgebra(..)
  , RangeUpdateSequence
  , Cursor
  , ValidationStatistics(..)
  , emptyWith
  , singletonWith
  , fromListWith
  , toList
  , count
  , null
  , measure
  , index
  , cons
  , snoc
  , insertAt
  , setAt
  , deleteAt
  , append
  , splitAt
  , slice
  , applyRange
  , measureRange
  , validateStructure
  , sharesRootWith
  , cursor
  , cursorAt
  , cursorPosition
  , cursorCount
  , cursorIsAtStart
  , cursorIsAtEnd
  , cursorMeasureBefore
  , cursorMeasureAfter
  , cursorPeekPrevious
  , cursorPeekNext
  , cursorMovePrevious
  , cursorMoveNext
  , cursorSeek
  , cursorInsert
  , cursorDeletePrevious
  , cursorDeleteNext
  , cursorReplaceNext
  , cursorMeasurePrevious
  , cursorMeasureNext
  , cursorApplyPrevious
  , cursorApplyNext
  , cursorSnapshot
  ) where

import Prelude hiding (null, sequence, splitAt)

import Control.Exception (evaluate)
import Data.Maybe (isNothing)
import System.Mem.StableName (eqStableName, makeStableName)

-- | Ordered measurement and a monoid of lazy tags acting on both elements
-- and cached measures.
--
-- @algebraCompose newer older@ denotes applying @older@ first and @newer@
-- second.  Besides the measure-monoid laws, tags must form a monoid, their
-- element and measure actions must agree on singletons, and the measure
-- action must distribute over ordered combination.  Every tag accepted by
-- 'algebraIsIdentity' must have full identity behavior, even when it is
-- value-distinct from 'algebraIdentityTag'.
data RangeUpdateAlgebra element measureValue tag = RangeUpdateAlgebra
  { algebraEmptyMeasure :: measureValue
  , algebraCombine :: measureValue -> measureValue -> measureValue
  , algebraMeasureElement :: element -> measureValue
  , algebraIdentityTag :: tag
  , algebraIsIdentity :: tag -> Bool
  , algebraCompose :: tag -> tag -> tag
  , algebraApplyElement :: tag -> element -> element
  , algebraApplyMeasure :: tag -> measureValue -> Int -> measureValue
  }

-- | An immutable range-update sequence.  The policy record is retained so
-- empty results remain usable without a global/static policy facility.
data RangeUpdateSequence element measureValue tag =
  RangeUpdateSequence
    !(RangeUpdateAlgebra element measureValue tag)
    !(Maybe (Node element measureValue tag))

-- | Immutable root-plus-position cursor over a lazy range-update snapshot.
data Cursor element measureValue tag = Cursor
  !(RangeUpdateSequence element measureValue tag)
  !Int

data Node element measureValue tag = Node
  { nodeValue :: element
  , nodeLeft :: !(Maybe (Node element measureValue tag))
  , nodeRight :: !(Maybe (Node element measureValue tag))
  , nodeHeight :: !Int
  , nodeCount :: !Int
  , nodeMeasure :: !measureValue
  , nodePendingTag :: !(Maybe tag)
  }

-- | Independently recomputed structural metadata returned by
-- 'validateStructure'.
data ValidationStatistics = ValidationStatistics
  { validationCount :: !Int
  , validationHeight :: !Int
  , validationNodeCount :: !Int
  , validationPendingTagCount :: !Int
  }
  deriving (Eq, Show)

-- | Constructs an empty sequence using the supplied algebra.
emptyWith :: RangeUpdateAlgebra element measureValue tag
          -> RangeUpdateSequence element measureValue tag
emptyWith algebra = RangeUpdateSequence algebra Nothing

-- | Constructs a singleton sequence.
singletonWith :: RangeUpdateAlgebra element measureValue tag
              -> element
              -> RangeUpdateSequence element measureValue tag
singletonWith algebra value =
  RangeUpdateSequence algebra (Just (makeNode algebra value Nothing Nothing))

-- | Constructs a deterministically balanced sequence in O(n) time.
fromListWith :: RangeUpdateAlgebra element measureValue tag
             -> [element]
             -> RangeUpdateSequence element measureValue tag
fromListWith algebra values =
  let !lengthValue = checkedLength values
      (root, remainder) = buildBalanced algebra lengthValue values
  in case remainder of
       [] -> RangeUpdateSequence algebra root
       _ -> error "Data.Structures.FingerTree.RangeUpdateSequence: internal build remainder"

-- | Enumerates logical elements in sequence order.  Pending tags are carried
-- through the traversal and no persistent nodes are allocated.
toList :: RangeUpdateSequence element measureValue tag -> [element]
toList (RangeUpdateSequence algebra root) =
  enumerate algebra root Nothing []

-- | Returns the cached element count in O(1).
count :: RangeUpdateSequence element measureValue tag -> Int
count (RangeUpdateSequence _ root) = countOf root

-- | Returns whether the sequence is empty.
null :: RangeUpdateSequence element measureValue tag -> Bool
null (RangeUpdateSequence _ root) = isNothing root

-- | Returns the ordered logical measure in O(1).
measure :: RangeUpdateSequence element measureValue tag -> measureValue
measure (RangeUpdateSequence algebra Nothing) = algebraEmptyMeasure algebra
measure (RangeUpdateSequence _ (Just root)) = nodeMeasure root

-- | Looks up a zero-based logical element in O(log n).
index :: Int -> RangeUpdateSequence element measureValue tag -> Maybe element
index position (RangeUpdateSequence algebra root)
  | position < 0 || position >= countOf root = Nothing
  | otherwise = Just (getElement algebra (expectNode root) position Nothing)

-- | Inserts an element at the beginning.
cons :: element
     -> RangeUpdateSequence element measureValue tag
     -> RangeUpdateSequence element measureValue tag
cons value sequence =
  case insertAt 0 value sequence of
    Just result -> result
    Nothing -> error "Data.Structures.FingerTree.RangeUpdateSequence: internal cons boundary"

-- | Inserts an element at the end.
snoc :: RangeUpdateSequence element measureValue tag
     -> element
     -> RangeUpdateSequence element measureValue tag
snoc sequence value =
  case insertAt (count sequence) value sequence of
    Just result -> result
    Nothing -> error "Data.Structures.FingerTree.RangeUpdateSequence: internal snoc boundary"

-- | Inserts an element at a boundary in @0 .. count@.  An invalid boundary
-- returns 'Nothing'; count overflow raises the family-specific pure error
-- before invoking algebra callbacks.
insertAt :: Int
         -> element
         -> RangeUpdateSequence element measureValue tag
         -> Maybe (RangeUpdateSequence element measureValue tag)
insertAt position value (RangeUpdateSequence algebra root)
  | position < 0 || position > countOf root = Nothing
  | otherwise =
      checkedAdd (countOf root) 1 `seq`
        Just (RangeUpdateSequence algebra (Just (insertCore algebra root position value)))

-- | Replaces one logical element.  Earlier range tags do not transform the
-- supplied replacement.
setAt :: Int
      -> element
      -> RangeUpdateSequence element measureValue tag
      -> Maybe (RangeUpdateSequence element measureValue tag)
setAt position value (RangeUpdateSequence algebra root)
  | position < 0 || position >= countOf root = Nothing
  | otherwise =
      Just (RangeUpdateSequence algebra (Just (setCore algebra (expectNode root) position value)))

-- | Removes one logical element.
deleteAt :: Int
         -> RangeUpdateSequence element measureValue tag
         -> Maybe (RangeUpdateSequence element measureValue tag)
deleteAt position (RangeUpdateSequence algebra root)
  | position < 0 || position >= countOf root = Nothing
  | otherwise =
      Just (RangeUpdateSequence algebra (deleteCore algebra (expectNode root) position))

-- | Concatenates two sequences.  Both operands must have extensionally
-- identical algebra records; Haskell functions do not have decidable
-- equality, so this is a caller precondition just as it is for MeasuredRope.
append :: RangeUpdateSequence element measureValue tag
       -> RangeUpdateSequence element measureValue tag
       -> RangeUpdateSequence element measureValue tag
append left@(RangeUpdateSequence algebra leftRoot)
       right@(RangeUpdateSequence _ rightRoot) =
  checkedAdd (countOf leftRoot) (countOf rightRoot) `seq`
    case (leftRoot, rightRoot) of
      (Nothing, _) -> right
      (_, Nothing) -> left
      (Just leftNode, Just rightNode) ->
        let (pivot, remainder) = extractMinimum algebra rightNode
        in RangeUpdateSequence algebra (Just (join algebra (Just leftNode) pivot remainder))

-- | Splits at a boundary in @0 .. count@.  Endpoint splits retain the exact
-- nonempty operand rather than pushing a pending root tag.
splitAt :: Int
        -> RangeUpdateSequence element measureValue tag
        -> Maybe
             ( RangeUpdateSequence element measureValue tag
             , RangeUpdateSequence element measureValue tag
             )
splitAt position sequence@(RangeUpdateSequence algebra root)
  | position < 0 || position > countOf root = Nothing
  | position == 0 = Just (emptyWith algebra, sequence)
  | position == countOf root = Just (sequence, emptyWith algebra)
  | otherwise =
      let (left, right) = splitCore algebra root position
      in Just (RangeUpdateSequence algebra left, RangeUpdateSequence algebra right)

-- | Returns a contiguous subsequence.  Invalid ranges return 'Nothing'.
slice :: Int
      -> Int
      -> RangeUpdateSequence element measureValue tag
      -> Maybe (RangeUpdateSequence element measureValue tag)
slice position amount sequence@(RangeUpdateSequence algebra root)
  | not (validRange position amount (countOf root)) = Nothing
  | amount == 0 = Just (emptyWith algebra)
  | amount == countOf root = Just sequence
  | otherwise =
      let (_, tailRoot) = splitCore algebra root position
          (middle, _) = splitCore algebra tailRoot amount
      in Just (RangeUpdateSequence algebra middle)

-- | Applies a tag after all updates already represented by the sequence.
-- Empty ranges do not call the identity predicate.  Empty ranges and
-- recognized identity tags retain the exact input value.  A nonidentity
-- whole-sequence update replaces only the root node.
applyRange :: Int
           -> Int
           -> tag
           -> RangeUpdateSequence element measureValue tag
           -> Maybe (RangeUpdateSequence element measureValue tag)
applyRange position amount tag sequence@(RangeUpdateSequence algebra root)
  | not (validRange position amount (countOf root)) = Nothing
  | amount == 0 = Just sequence
  | algebraIsIdentity algebra tag = Just sequence
  | amount == countOf root =
      Just (RangeUpdateSequence algebra (Just (applySubtree algebra (expectNode root) tag)))
  | otherwise =
      let (left, tailRoot) = splitCore algebra root position
          (middle, right) = splitCore algebra tailRoot amount
          changed = Just (applySubtree algebra (expectNode middle) tag)
          rebuilt = joinConcatenated algebra (joinConcatenated algebra left changed) right
      in Just (RangeUpdateSequence algebra rebuilt)

-- | Measures a contiguous logical range in O(log n), consuming cached
-- measures for fully covered subtrees without constructing split trees.
measureRange :: Int
             -> Int
             -> RangeUpdateSequence element measureValue tag
             -> Maybe measureValue
measureRange position amount (RangeUpdateSequence algebra root)
  | not (validRange position amount (countOf root)) = Nothing
  | amount == 0 = Just (algebraEmptyMeasure algebra)
  | amount == countOf root = Just (nodeMeasure (expectNode root))
  | otherwise =
      Just (measureRangeCore algebra (expectNode root) position amount Nothing)

-- | Recomputes AVL metadata and logical cached measures independently.
-- Returns 'Nothing' if any invariant fails.  The check may invoke algebra
-- callbacks and therefore has no production-operation complexity claim.
validateStructure :: Eq measureValue
                  => RangeUpdateSequence element measureValue tag
                  -> Maybe ValidationStatistics
validateStructure (RangeUpdateSequence algebra Nothing) =
  algebraEmptyMeasure algebra `seq` Just (ValidationStatistics 0 0 0 0)
validateStructure (RangeUpdateSequence algebra (Just root)) = do
  (_, statistics) <- validateNode algebra root
  pure statistics

-- | Test-facing root-identity diagnostic.  It forces only both facade roots.
sharesRootWith :: RangeUpdateSequence element measureValue tag
               -> RangeUpdateSequence element measureValue tag
               -> IO Bool
sharesRootWith (RangeUpdateSequence _ Nothing) (RangeUpdateSequence _ Nothing) = pure True
sharesRootWith (RangeUpdateSequence _ (Just left)) (RangeUpdateSequence _ (Just right)) = do
  leftValue <- evaluate left
  rightValue <- evaluate right
  leftName <- makeStableName leftValue
  rightName <- makeStableName rightValue
  pure (leftName `eqStableName` rightName)
sharesRootWith _ _ = pure False

cursor :: RangeUpdateSequence element measureValue tag -> Cursor element measureValue tag
cursor sequence = Cursor sequence 0

cursorAt :: Int
         -> RangeUpdateSequence element measureValue tag
         -> Maybe (Cursor element measureValue tag)
cursorAt position sequence
  | position < 0 || position > count sequence = Nothing
  | otherwise = Just (Cursor sequence position)

cursorPosition :: Cursor element measureValue tag -> Int
cursorPosition (Cursor _ position) = position

cursorCount :: Cursor element measureValue tag -> Int
cursorCount (Cursor sequence _) = count sequence

cursorIsAtStart :: Cursor element measureValue tag -> Bool
cursorIsAtStart value = cursorPosition value == 0

cursorIsAtEnd :: Cursor element measureValue tag -> Bool
cursorIsAtEnd value = cursorPosition value == cursorCount value

cursorMeasureBefore :: Cursor element measureValue tag -> measureValue
cursorMeasureBefore (Cursor sequence position) =
  expectCursorEdit "measure before" (measureRange 0 position sequence)

cursorMeasureAfter :: Cursor element measureValue tag -> measureValue
cursorMeasureAfter (Cursor sequence position) =
  expectCursorEdit "measure after" (measureRange position (count sequence - position) sequence)

cursorPeekPrevious :: Cursor element measureValue tag -> Maybe element
cursorPeekPrevious (Cursor sequence position) = index (position - 1) sequence

cursorPeekNext :: Cursor element measureValue tag -> Maybe element
cursorPeekNext (Cursor sequence position) = index position sequence

cursorMovePrevious :: Cursor element measureValue tag -> Maybe (Cursor element measureValue tag)
cursorMovePrevious (Cursor sequence position) = cursorAt (position - 1) sequence

cursorMoveNext :: Cursor element measureValue tag -> Maybe (Cursor element measureValue tag)
cursorMoveNext (Cursor sequence position)
  | position == count sequence = Nothing
  | otherwise = cursorAt (position + 1) sequence

cursorSeek :: Int
           -> Cursor element measureValue tag
           -> Maybe (Cursor element measureValue tag)
cursorSeek position (Cursor sequence _) = cursorAt position sequence

cursorInsert :: element
             -> Cursor element measureValue tag
             -> Cursor element measureValue tag
cursorInsert value (Cursor sequence position) =
  Cursor (expectCursorEdit "insert" (insertAt position value sequence)) (checkedAdd position 1)

cursorDeletePrevious :: Cursor element measureValue tag
                     -> Maybe (Cursor element measureValue tag)
cursorDeletePrevious (Cursor sequence position)
  | position == 0 = Nothing
  | otherwise = Just (Cursor (expectCursorEdit "delete previous" (deleteAt (position - 1) sequence)) (position - 1))

cursorDeleteNext :: Cursor element measureValue tag
                 -> Maybe (Cursor element measureValue tag)
cursorDeleteNext (Cursor sequence position)
  | position == count sequence = Nothing
  | otherwise = Just (Cursor (expectCursorEdit "delete next" (deleteAt position sequence)) position)

cursorReplaceNext :: element
                  -> Cursor element measureValue tag
                  -> Maybe (Cursor element measureValue tag)
cursorReplaceNext value (Cursor sequence position)
  | position == count sequence = Nothing
  | otherwise = Just (Cursor (expectCursorEdit "replace next" (setAt position value sequence)) position)

cursorMeasurePrevious :: Int
                      -> Cursor element measureValue tag
                      -> Maybe measureValue
cursorMeasurePrevious amount (Cursor sequence position)
  | amount < 0 || amount > position = Nothing
  | otherwise = measureRange (position - amount) amount sequence

cursorMeasureNext :: Int
                  -> Cursor element measureValue tag
                  -> Maybe measureValue
cursorMeasureNext amount (Cursor sequence position)
  | amount < 0 || amount > count sequence - position = Nothing
  | otherwise = measureRange position amount sequence

cursorApplyPrevious :: Int
                    -> tag
                    -> Cursor element measureValue tag
                    -> Maybe (Cursor element measureValue tag)
cursorApplyPrevious amount tag (Cursor sequence position)
  | amount < 0 || amount > position = Nothing
  | otherwise = Cursor <$> applyRange (position - amount) amount tag sequence <*> pure position

cursorApplyNext :: Int
                -> tag
                -> Cursor element measureValue tag
                -> Maybe (Cursor element measureValue tag)
cursorApplyNext amount tag (Cursor sequence position)
  | amount < 0 || amount > count sequence - position = Nothing
  | otherwise = Cursor <$> applyRange position amount tag sequence <*> pure position

cursorSnapshot :: Cursor element measureValue tag
               -> RangeUpdateSequence element measureValue tag
cursorSnapshot (Cursor sequence _) = sequence

expectCursorEdit :: String -> Maybe a -> a
expectCursorEdit _ (Just value) = value
expectCursorEdit operation Nothing = error ("Data.Structures.FingerTree.RangeUpdateSequence cursor " ++ operation ++ " failed")

buildBalanced :: RangeUpdateAlgebra element measureValue tag
              -> Int
              -> [element]
              -> (Maybe (Node element measureValue tag), [element])
buildBalanced _ 0 values = (Nothing, values)
buildBalanced algebra lengthValue values =
  let leftLength = lengthValue `div` 2
      rightLength = lengthValue - leftLength - 1
      (left, afterLeft) = buildBalanced algebra leftLength values
  in case afterLeft of
       [] -> error "Data.Structures.FingerTree.RangeUpdateSequence: truncated build input"
       value : afterValue ->
         let (right, remainder) = buildBalanced algebra rightLength afterValue
         in (Just (makeNode algebra value left right), remainder)

enumerate :: RangeUpdateAlgebra element measureValue tag
          -> Maybe (Node element measureValue tag)
          -> Maybe tag
          -> [element]
          -> [element]
enumerate _ Nothing _ suffix = suffix
enumerate algebra (Just node) inherited suffix =
  let childTag = childInheritance algebra node inherited
      value = applyInheritedToElement algebra inherited (nodeValue node)
      rightValues = enumerate algebra (nodeRight node) childTag suffix
  in enumerate algebra (nodeLeft node) childTag (value : rightValues)

getElement :: RangeUpdateAlgebra element measureValue tag
           -> Node element measureValue tag
           -> Int
           -> Maybe tag
           -> element
getElement algebra node position inherited =
  let leftCount = countOf (nodeLeft node)
  in if position == leftCount
       then applyInheritedToElement algebra inherited (nodeValue node)
       else
         let childTag = childInheritance algebra node inherited
         in if position < leftCount
              then getElement algebra (expectNode (nodeLeft node)) position childTag
              else getElement
                     algebra
                     (expectNode (nodeRight node))
                     (position - leftCount - 1)
                     childTag

insertCore :: RangeUpdateAlgebra element measureValue tag
           -> Maybe (Node element measureValue tag)
           -> Int
           -> element
           -> Node element measureValue tag
insertCore algebra Nothing _ value = makeNode algebra value Nothing Nothing
insertCore algebra (Just original) position value =
  let node = push algebra original
      leftCount = countOf (nodeLeft node)
  in if position <= leftCount
       then balance algebra
              (makeNode algebra
                (nodeValue node)
                (Just (insertCore algebra (nodeLeft node) position value))
                (nodeRight node))
       else balance algebra
              (makeNode algebra
                (nodeValue node)
                (nodeLeft node)
                (Just (insertCore algebra
                  (nodeRight node)
                  (position - leftCount - 1)
                  value)))

setCore :: RangeUpdateAlgebra element measureValue tag
        -> Node element measureValue tag
        -> Int
        -> element
        -> Node element measureValue tag
setCore algebra original position value =
  let node = push algebra original
      leftCount = countOf (nodeLeft node)
  in if position < leftCount
       then makeNode algebra
              (nodeValue node)
              (Just (setCore algebra (expectNode (nodeLeft node)) position value))
              (nodeRight node)
       else if position > leftCount
         then makeNode algebra
                (nodeValue node)
                (nodeLeft node)
                (Just (setCore algebra
                  (expectNode (nodeRight node))
                  (position - leftCount - 1)
                  value))
         else makeNode algebra value (nodeLeft node) (nodeRight node)

deleteCore :: RangeUpdateAlgebra element measureValue tag
           -> Node element measureValue tag
           -> Int
           -> Maybe (Node element measureValue tag)
deleteCore algebra original position =
  let node = push algebra original
      leftCount = countOf (nodeLeft node)
  in if position < leftCount
       then Just (balance algebra
              (makeNode algebra
                (nodeValue node)
                (deleteCore algebra (expectNode (nodeLeft node)) position)
                (nodeRight node)))
       else if position > leftCount
         then Just (balance algebra
                (makeNode algebra
                  (nodeValue node)
                  (nodeLeft node)
                  (deleteCore algebra
                    (expectNode (nodeRight node))
                    (position - leftCount - 1))))
         else case (nodeLeft node, nodeRight node) of
                (Nothing, right) -> right
                (left, Nothing) -> left
                (left, Just right) ->
                  let (successor, remainder) = extractMinimum algebra right
                  in Just (balance algebra (makeNode algebra successor left remainder))

extractMinimum :: RangeUpdateAlgebra element measureValue tag
               -> Node element measureValue tag
               -> (element, Maybe (Node element measureValue tag))
extractMinimum algebra original =
  let node = push algebra original
  in case nodeLeft node of
       Nothing -> (nodeValue node, nodeRight node)
       Just left ->
         let (minimumValue, remainder) = extractMinimum algebra left
             rebuilt = balance algebra
               (makeNode algebra (nodeValue node) remainder (nodeRight node))
         in (minimumValue, Just rebuilt)

splitCore :: RangeUpdateAlgebra element measureValue tag
          -> Maybe (Node element measureValue tag)
          -> Int
          -> (Maybe (Node element measureValue tag), Maybe (Node element measureValue tag))
splitCore _ Nothing _ = (Nothing, Nothing)
splitCore _ root 0 = (Nothing, root)
splitCore _ root position | position == countOf root = (root, Nothing)
splitCore algebra (Just original) position =
  let node = push algebra original
      leftCount = countOf (nodeLeft node)
  in if position <= leftCount
       then
         let (left, middle) = splitCore algebra (nodeLeft node) position
             right = join algebra middle (nodeValue node) (nodeRight node)
         in (left, Just right)
       else
         let (middle, right) =
               splitCore algebra (nodeRight node) (position - leftCount - 1)
             left = join algebra (nodeLeft node) (nodeValue node) middle
         in (Just left, right)

join :: RangeUpdateAlgebra element measureValue tag
     -> Maybe (Node element measureValue tag)
     -> element
     -> Maybe (Node element measureValue tag)
     -> Node element measureValue tag
join algebra left pivot right
  | leftHeight <= rightHeight + 1 && rightHeight <= leftHeight + 1 =
      makeNode algebra pivot left right
  | leftHeight > rightHeight =
      let leftNode = push algebra (expectNode left)
          boundary = join algebra (nodeRight leftNode) pivot right
      in balance algebra
           (makeNode algebra (nodeValue leftNode) (nodeLeft leftNode) (Just boundary))
  | otherwise =
      let rightNode = push algebra (expectNode right)
          leading = join algebra left pivot (nodeLeft rightNode)
      in balance algebra
           (makeNode algebra (nodeValue rightNode) (Just leading) (nodeRight rightNode))
  where
    leftHeight = heightOf left
    rightHeight = heightOf right

joinConcatenated :: RangeUpdateAlgebra element measureValue tag
                 -> Maybe (Node element measureValue tag)
                 -> Maybe (Node element measureValue tag)
                 -> Maybe (Node element measureValue tag)
joinConcatenated _ Nothing right = right
joinConcatenated _ left Nothing = left
joinConcatenated algebra left (Just right) =
  let (pivot, remainder) = extractMinimum algebra right
  in Just (join algebra left pivot remainder)

balance :: RangeUpdateAlgebra element measureValue tag
        -> Node element measureValue tag
        -> Node element measureValue tag
balance algebra original
  | factor > 1 =
      let left = expectNode (nodeLeft original)
          prepared =
            if heightOf (nodeLeft left) < heightOf (nodeRight left)
              then
                let node = push algebra original
                    rotated = rotateLeft algebra (expectNode (nodeLeft node))
                in makeNode algebra (nodeValue node) (Just rotated) (nodeRight node)
              else original
      in rotateRight algebra prepared
  | factor < -1 =
      let right = expectNode (nodeRight original)
          prepared =
            if heightOf (nodeRight right) < heightOf (nodeLeft right)
              then
                let node = push algebra original
                    rotated = rotateRight algebra (expectNode (nodeRight node))
                in makeNode algebra (nodeValue node) (nodeLeft node) (Just rotated)
              else original
      in rotateLeft algebra prepared
  | otherwise = original
  where
    factor = heightOf (nodeLeft original) - heightOf (nodeRight original)

rotateLeft :: RangeUpdateAlgebra element measureValue tag
           -> Node element measureValue tag
           -> Node element measureValue tag
rotateLeft algebra original =
  let node = push algebra original
      pivot = push algebra (expectNode (nodeRight node))
      lower = makeNode algebra (nodeValue node) (nodeLeft node) (nodeLeft pivot)
  in makeNode algebra (nodeValue pivot) (Just lower) (nodeRight pivot)

rotateRight :: RangeUpdateAlgebra element measureValue tag
            -> Node element measureValue tag
            -> Node element measureValue tag
rotateRight algebra original =
  let node = push algebra original
      pivot = push algebra (expectNode (nodeLeft node))
      lower = makeNode algebra (nodeValue node) (nodeRight pivot) (nodeRight node)
  in makeNode algebra (nodeValue pivot) (nodeLeft pivot) (Just lower)

applySubtree :: RangeUpdateAlgebra element measureValue tag
             -> Node element measureValue tag
             -> tag
             -> Node element measureValue tag
applySubtree algebra node newerTag =
  let !value = algebraApplyElement algebra newerTag (nodeValue node)
      !measureValue = algebraApplyMeasure algebra newerTag (nodeMeasure node) (nodeCount node)
      pending =
        case nodePendingTag node of
          Nothing -> Just newerTag
          Just olderTag ->
            let !composed = algebraCompose algebra newerTag olderTag
            in if algebraIsIdentity algebra composed then Nothing else Just composed
  in pending `seq`
       rawNode value (nodeLeft node) (nodeRight node)
         (nodeHeight node) (nodeCount node) measureValue pending

push :: RangeUpdateAlgebra element measureValue tag
     -> Node element measureValue tag
     -> Node element measureValue tag
push _ node@Node { nodePendingTag = Nothing } = node
push algebra node@Node { nodePendingTag = Just tag } =
  let left = fmap (\child -> applySubtree algebra child tag) (nodeLeft node)
      right = fmap (\child -> applySubtree algebra child tag) (nodeRight node)
  in rawNode (nodeValue node) left right
       (nodeHeight node) (nodeCount node) (nodeMeasure node) Nothing

measureRangeCore :: RangeUpdateAlgebra element measureValue tag
                 -> Node element measureValue tag
                 -> Int
                 -> Int
                 -> Maybe tag
                 -> measureValue
measureRangeCore algebra node position amount inherited
  | position == 0 && amount == nodeCount node =
      applyInheritedToMeasure algebra inherited (nodeMeasure node) (nodeCount node)
  | otherwise =
      combineNonEmpty algebra (leftPart ++ valuePart ++ rightPart)
  where
    end = position + amount
    leftCount = countOf (nodeLeft node)
    childTag = childInheritance algebra node inherited

    leftPart
      | position < leftCount =
          let childAmount = min amount (leftCount - position)
          in [measureRangeCore algebra
                (expectNode (nodeLeft node)) position childAmount childTag]
      | otherwise = []

    valuePart
      | position <= leftCount && end > leftCount =
          let own = algebraMeasureElement algebra (nodeValue node)
          in [applyInheritedToMeasure algebra inherited own 1]
      | otherwise = []

    rightStart = leftCount + 1
    rightPart
      | end > rightStart =
          let localStart = max 0 (position - rightStart)
              localEnd = end - rightStart
          in [measureRangeCore algebra
                (expectNode (nodeRight node))
                localStart
                (localEnd - localStart)
                childTag]
      | otherwise = []

childInheritance :: RangeUpdateAlgebra element measureValue tag
                 -> Node element measureValue tag
                 -> Maybe tag
                 -> Maybe tag
childInheritance algebra node inherited =
  case (inherited, nodePendingTag node) of
    (Nothing, Nothing) -> Nothing
    (Just tag, Nothing) -> Just tag
    (Nothing, Just tag) -> Just tag
    (Just newer, Just older) ->
      let !composed = algebraCompose algebra newer older
      in if algebraIsIdentity algebra composed then Nothing else Just composed

applyInheritedToElement :: RangeUpdateAlgebra element measureValue tag
                        -> Maybe tag
                        -> element
                        -> element
applyInheritedToElement _ Nothing value = value
applyInheritedToElement algebra (Just tag) value = algebraApplyElement algebra tag value

applyInheritedToMeasure :: RangeUpdateAlgebra element measureValue tag
                        -> Maybe tag
                        -> measureValue
                        -> Int
                        -> measureValue
applyInheritedToMeasure _ Nothing value _ = value
applyInheritedToMeasure algebra (Just tag) value amount =
  algebraApplyMeasure algebra tag value amount

combineNonEmpty :: RangeUpdateAlgebra element measureValue tag
                -> [measureValue]
                -> measureValue
combineNonEmpty _ [] =
  error "Data.Structures.FingerTree.RangeUpdateSequence: internal empty measure query"
combineNonEmpty algebra (value : values) =
  foldlStrict (algebraCombine algebra) value values

makeNode :: RangeUpdateAlgebra element measureValue tag
         -> element
         -> Maybe (Node element measureValue tag)
         -> Maybe (Node element measureValue tag)
         -> Node element measureValue tag
makeNode algebra value left right =
  let !heightValue = 1 + max (heightOf left) (heightOf right)
      !countValue = checkedAdd (checkedAdd (countOf left) 1) (countOf right)
      !ownMeasure = algebraMeasureElement algebra value
      !leftCombined =
        case left of
          Nothing -> ownMeasure
          Just leftNode -> algebraCombine algebra (nodeMeasure leftNode) ownMeasure
      !measureValue =
        case right of
          Nothing -> leftCombined
          Just rightNode -> algebraCombine algebra leftCombined (nodeMeasure rightNode)
  in countValue `seq` heightValue `seq` measureValue `seq`
       rawNode value left right heightValue countValue measureValue Nothing

rawNode :: element
        -> Maybe (Node element measureValue tag)
        -> Maybe (Node element measureValue tag)
        -> Int
        -> Int
        -> measureValue
        -> Maybe tag
        -> Node element measureValue tag
rawNode = Node

validateNode :: Eq measureValue
             => RangeUpdateAlgebra element measureValue tag
             -> Node element measureValue tag
             -> Maybe (measureValue, ValidationStatistics)
validateNode algebra node = do
  (leftMeasure, leftStats) <- validateChild (nodeLeft node)
  (rightMeasure, rightStats) <- validateChild (nodeRight node)
  let leftCount = validationCount leftStats
      rightCount = validationCount rightStats
      expectedCountInteger = toInteger leftCount + 1 + toInteger rightCount
      expectedHeight = 1 + max (validationHeight leftStats) (validationHeight rightStats)
      balanced = abs (validationHeight leftStats - validationHeight rightStats) <= 1
  if expectedCountInteger > toInteger (maxBound :: Int)
      || nodeCount node /= fromInteger expectedCountInteger
      || nodeHeight node /= expectedHeight
      || not balanced
    then Nothing
    else do
      let pendingIsCanonical =
            case nodePendingTag node of
              Nothing -> True
              Just tag -> not (algebraIsIdentity algebra tag)
          transformedLeft = applyPending (nodePendingTag node) leftMeasure leftCount
          transformedRight = applyPending (nodePendingTag node) rightMeasure rightCount
          ownMeasure = algebraMeasureElement algebra (nodeValue node)
          withLeft =
            if leftCount == 0
              then ownMeasure
              else algebraCombine algebra transformedLeft ownMeasure
          expectedMeasure =
            if rightCount == 0
              then withLeft
              else algebraCombine algebra withLeft transformedRight
          statistics = ValidationStatistics
            { validationCount = nodeCount node
            , validationHeight = nodeHeight node
            , validationNodeCount =
                1 + validationNodeCount leftStats + validationNodeCount rightStats
            , validationPendingTagCount =
                maybe 0 (const 1) (nodePendingTag node)
                  + validationPendingTagCount leftStats
                  + validationPendingTagCount rightStats
            }
      if pendingIsCanonical && expectedMeasure == nodeMeasure node
        then Just (nodeMeasure node, statistics)
        else Nothing
  where
    validateChild Nothing =
      Just
        ( algebraEmptyMeasure algebra
        , ValidationStatistics 0 0 0 0
        )
    validateChild (Just child) = validateNode algebra child
    applyPending Nothing cached _ = cached
    applyPending (Just tag) cached childCount =
      algebraApplyMeasure algebra tag cached childCount

validRange :: Int -> Int -> Int -> Bool
validRange position amount lengthValue =
  position >= 0
    && amount >= 0
    && position <= lengthValue
    && amount <= lengthValue - position

heightOf :: Maybe (Node element measureValue tag) -> Int
heightOf Nothing = 0
heightOf (Just node) = nodeHeight node

countOf :: Maybe (Node element measureValue tag) -> Int
countOf Nothing = 0
countOf (Just node) = nodeCount node

expectNode :: Maybe (Node element measureValue tag) -> Node element measureValue tag
expectNode (Just node) = node
expectNode Nothing =
  error "Data.Structures.FingerTree.RangeUpdateSequence: internal missing node"

checkedLength :: [a] -> Int
checkedLength = go 0
  where
    go !result [] = result
    go !result (_ : values)
      | result == maxBound = lengthOverflow
      | otherwise = go (result + 1) values

checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | left < 0 || right < 0 =
      error "Data.Structures.FingerTree.RangeUpdateSequence: internal negative count"
  | right > maxBound - left = lengthOverflow
  | otherwise = left + right

lengthOverflow :: a
lengthOverflow = error "Data.Structures.FingerTree.RangeUpdateSequence: length overflow"

foldlStrict :: (a -> b -> a) -> a -> [b] -> a
foldlStrict _ !result [] = result
foldlStrict operation !result (value : values) =
  foldlStrict operation (operation result value) values
