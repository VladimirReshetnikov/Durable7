{-# LANGUAGE BangPatterns #-}

-- | An immutable deque held as two oppositely oriented intervals of an
-- append-only ancestry forest.
--
-- A handle stores a reversed /left/ interval @(l-anchor, l-tail]@ and a
-- forward /right/ interval @(r-anchor, r-tail]@; its logical value is the
-- reversal of the left interval followed by the right interval. A front
-- insertion adds one leaf below the left tail, a back insertion one below the
-- right tail, and 'reverse' only exchanges the two intervals. Every contiguous
-- slice meets each interval at most once, so a slice is again a bilateral
-- handle rather than a rebuilt tree.
--
-- Let @Q@ be the level-ancestor query cost of
-- "Durable7.FingerTree.IncrementalAncestor", which is @O(log M)@ after @M@
-- additions on the queried branch. End insertion publishes one leaf in @O(1)@
-- worst case and makes no query. End removal makes no query on its own
-- nonempty arm and at most one @Q@ when it crosses the centre. 'index' costs
-- at most one @Q@, and the four cached logical endpoints (visible index zero,
-- the end of the left arm, the start of the right arm, and the last visible
-- index) cost none. 'slice' and 'splitAt' cost at most two @Q@ each, however
-- many values they select, and 'validateStructure' at most four. 'first',
-- 'last', 'count', 'null', 'clear', and 'reverse' are @O(1)@ with no query.
-- Enumeration through 'toList' is @Theta(count)@, makes no query, and may
-- retain @O(count)@ temporary values because the forward-oriented right
-- interval is buffered to keep it query-free.
--
-- The algebra is deliberately restricted: add or remove at either end, read an
-- end or an indexed value, reverse, take\/drop\/slice\/split, and enumerate
-- front to back. Arbitrary concatenation of unrelated versions, point
-- replacement, and middle editing are intentionally absent.
--
-- The managed and native ports of this collection inject a mutable
-- integer-handle arena and read query counters back off it. Here the forest is
-- ordinary immutable heap structure and a node /is/ its own handle, so there is
-- no arena object, no backend-injecting factory, and no arena statistics:
-- 'empty' is the only constructor, leaf addition is @O(1)@ worst case rather
-- than amortized, and the query cost an auditor wants is returned by the
-- operation that paid for it -- see 'indexCost', 'sliceCost', 'splitAtCost',
-- 'removeFirstCost', and 'removeLastCost'.
module Durable7.FingerTree.BilateralAncestralDeque
  ( BilateralAncestralDeque
  , BilateralStatistics(..)
  , QueryCost(..)
  , empty
  , fromList
  , toList
  , count
  , null
  , first
  , last
  , index
  , addFirst
  , addLast
  , removeFirst
  , removeLast
  , tryRemoveFirst
  , tryRemoveLast
  , take
  , drop
  , slice
  , splitAt
  , reverse
  , clear
  , indexCost
  , removeFirstCost
  , removeLastCost
  , sliceCost
  , splitAtCost
  , validateStructure
  , sharesRootWith
  ) where

import Prelude hiding (drop, last, null, reverse, splitAt, take)

import Durable7.FingerTree.IncrementalAncestor
  ( Node
  , addLeaf
  , ancestorAtDepth
  , ancestorAtDepthHops
  , bottom
  , nodeDepth
  , nodeParent
  , nodeValue
  , sharesNodeWith
  )

-- | One oriented ancestry interval @(anchor, tail]@.
--
-- @base@ is redundant but load-bearing: it caches the first node after
-- @anchor@, which keeps both endpoint reads query-free even when the opposite
-- arm is empty. For the reversed left arm the logical first value sits at
-- @tail@ and the logical last at @base@; for the forward right arm those roles
-- are exchanged.
data Segment a = Segment
  { segmentAnchor :: !(Node a)
  , segmentBase :: !(Node a)
  , segmentTail :: !(Node a)
  , segmentCount :: !Int
  }

-- | A persistent deque held as two oppositely oriented ancestry intervals.
--
-- Every operation returns a new handle and leaves its input valid; nothing is
-- copied, so an old handle keeps seeing exactly the values it saw before.
data BilateralAncestralDeque a = BilateralAncestralDeque
  { dequeLeft :: !(Segment a)
  , dequeRight :: !(Segment a)
  , dequeCount :: !Int
  }

-- | Independently recomputed representation facts returned by
-- 'validateStructure'.
data BilateralStatistics = BilateralStatistics
  { bilateralCount :: !Int
    -- ^ The number of visible values, which equals the sum of the arm counts.
  , bilateralLeftCount :: !Int
    -- ^ The number of values held by the reversed left interval.
  , bilateralRightCount :: !Int
    -- ^ The number of values held by the forward right interval.
  }
  deriving (Eq, Show)

-- | The level-ancestor work one operation performed.
--
-- The managed and native ports retain these counters on the mutable arena and
-- read them back through an arena statistics snapshot. A pure forest has
-- nowhere to retain them, so an operation that can query returns its own cost
-- alongside its result.
data QueryCost = QueryCost
  { queryCostQueries :: !Int
    -- ^ Level-ancestor queries issued. Zero whenever cached endpoints answered.
  , queryCostHops :: !Int
    -- ^ Parent\/jump links followed across all of those queries.
  }
  deriving (Eq, Show)

-- | The empty deque. @O(1)@.
--
-- Unlike the managed and native ports there is no arena to belong to, so one
-- shared empty handle serves every element type and every deque family.
empty :: BilateralAncestralDeque a
empty = BilateralAncestralDeque emptyArm emptyArm 0
  where
    emptyArm = emptySegmentAt bottom

-- | Builds a deque by appending the values in list order. @Theta(n)@ leaf
-- additions and no queries.
fromList :: [a] -> BilateralAncestralDeque a
fromList = go empty
  where
    go !deque [] = deque
    go !deque (value : values) = go (addLast value deque) values

-- | The visible values in logical order.
--
-- @Theta(count)@ with no level-ancestor query. The forward-oriented right
-- interval is reversed through a temporary list, so the result can retain
-- @O(count)@ values before the first right-arm value is produced.
toList :: BilateralAncestralDeque a -> [a]
toList deque = leftValues ++ reverseOnto rightValues []
  where
    leftValues = valuesFromTail (segmentTail (dequeLeft deque)) (segmentCount (dequeLeft deque))
    rightValues = valuesFromTail (segmentTail (dequeRight deque)) (segmentCount (dequeRight deque))

-- | The number of visible values. @O(1)@.
count :: BilateralAncestralDeque a -> Int
count = dequeCount

-- | Whether this handle denotes an empty deque. @O(1)@.
null :: BilateralAncestralDeque a -> Bool
null deque = dequeCount deque == 0

-- | The first visible value, or 'Nothing' when the deque is empty.
--
-- Reads one cached endpoint, so it is @O(1)@ with no query. The C# member
-- throws @InvalidOperationException@ on an empty deque.
first :: BilateralAncestralDeque a -> Maybe a
first deque
  | dequeCount deque == 0 = Nothing
  | segmentCount (dequeLeft deque) /= 0 = Just (expectValue (segmentTail (dequeLeft deque)))
  | otherwise = Just (expectValue (segmentBase (dequeRight deque)))

-- | The last visible value, or 'Nothing' when the deque is empty.
--
-- Reads one cached endpoint, so it is @O(1)@ with no query. The C# member
-- throws @InvalidOperationException@ on an empty deque.
last :: BilateralAncestralDeque a -> Maybe a
last deque
  | dequeCount deque == 0 = Nothing
  | segmentCount (dequeRight deque) /= 0 = Just (expectValue (segmentTail (dequeRight deque)))
  | otherwise = Just (expectValue (segmentBase (dequeLeft deque)))

-- | The value at a zero-based visible index, or 'Nothing' when the index is
-- outside the deque.
--
-- Costs at most one level-ancestor query; the four cached logical endpoints
-- are answered with none.
index :: Int -> BilateralAncestralDeque a -> Maybe a
index position deque = fst <$> indexCost position deque

-- | 'index' paired with the level-ancestor work it performed.
indexCost :: Int -> BilateralAncestralDeque a -> Maybe (a, QueryCost)
indexCost position deque
  | position < 0 || position >= dequeCount deque = Nothing
  | position < leftTotal =
      if position == 0
        then cached (segmentTail leftSegment)
        else
          if position == leftTotal - 1
            then cached (segmentBase leftSegment)
            else
              queried
                (segmentTail leftSegment)
                (checkedSub (nodeDepth (segmentTail leftSegment)) position)
  | rightPosition == 0 = cached (segmentBase rightSegment)
  | rightPosition == segmentCount rightSegment - 1 = cached (segmentTail rightSegment)
  | otherwise =
      queried
        (segmentTail rightSegment)
        (checkedAdd (checkedAdd (nodeDepth (segmentAnchor rightSegment)) rightPosition) 1)
  where
    leftSegment = dequeLeft deque
    rightSegment = dequeRight deque
    leftTotal = segmentCount leftSegment
    rightPosition = position - leftTotal
    cached node = Just (expectValue node, noQueries)
    queried node depth =
      let (found, cost) = queryAncestor node depth
      in Just (expectValue found, cost)

-- | A deque with one value added at the front; the receiver is unchanged.
--
-- Publishes exactly one leaf in @O(1)@ worst case and makes no query.
addFirst :: a -> BilateralAncestralDeque a -> BilateralAncestralDeque a
addFirst value deque =
  BilateralAncestralDeque
    (addToTail (dequeLeft deque) value)
    (dequeRight deque)
    (checkedIncrement (dequeCount deque))

-- | A deque with one value added at the back; the receiver is unchanged.
--
-- Publishes exactly one leaf in @O(1)@ worst case and makes no query.
addLast :: a -> BilateralAncestralDeque a -> BilateralAncestralDeque a
addLast value deque =
  BilateralAncestralDeque
    (dequeLeft deque)
    (addToTail (dequeRight deque) value)
    (checkedIncrement (dequeCount deque))

-- | The deque without its first value, or 'Nothing' when it is empty.
--
-- Makes no query when the left arm is nonempty and at most one when the
-- removal crosses the centre into the right arm.
removeFirst :: BilateralAncestralDeque a -> Maybe (BilateralAncestralDeque a)
removeFirst deque = fst <$> removeFirstCost deque

-- | 'removeFirst' paired with the level-ancestor work it performed.
removeFirstCost :: BilateralAncestralDeque a -> Maybe (BilateralAncestralDeque a, QueryCost)
removeFirstCost deque
  | dequeCount deque == 0 = Nothing
  | dequeCount deque == 1 = Just (empty, noQueries)
  | segmentCount (dequeLeft deque) /= 0 =
      Just
        ( BilateralAncestralDeque
            (removeTail (dequeLeft deque))
            (dequeRight deque)
            (dequeCount deque - 1)
        , noQueries
        )
  | otherwise =
      let (shortened, cost) = removeBase (dequeRight deque)
      in Just
           ( BilateralAncestralDeque (dequeLeft deque) shortened (dequeCount deque - 1)
           , cost
           )

-- | The deque without its last value, or 'Nothing' when it is empty.
--
-- Makes no query when the right arm is nonempty and at most one when the
-- removal crosses the centre into the left arm.
removeLast :: BilateralAncestralDeque a -> Maybe (BilateralAncestralDeque a)
removeLast deque = fst <$> removeLastCost deque

-- | 'removeLast' paired with the level-ancestor work it performed.
removeLastCost :: BilateralAncestralDeque a -> Maybe (BilateralAncestralDeque a, QueryCost)
removeLastCost deque
  | dequeCount deque == 0 = Nothing
  | dequeCount deque == 1 = Just (empty, noQueries)
  | segmentCount (dequeRight deque) /= 0 =
      Just
        ( BilateralAncestralDeque
            (dequeLeft deque)
            (removeTail (dequeRight deque))
            (dequeCount deque - 1)
        , noQueries
        )
  | otherwise =
      let (shortened, cost) = removeBase (dequeLeft deque)
      in Just
           ( BilateralAncestralDeque shortened (dequeRight deque) (dequeCount deque - 1)
           , cost
           )

-- | The first value together with the deque that remains, or 'Nothing' when
-- the deque is empty. Ports the C# @TryRemoveFirst@ out-parameter pair.
tryRemoveFirst :: BilateralAncestralDeque a -> Maybe (a, BilateralAncestralDeque a)
tryRemoveFirst deque = (,) <$> first deque <*> removeFirst deque

-- | The last value together with the deque that remains, or 'Nothing' when the
-- deque is empty. Ports the C# @TryRemoveLast@ out-parameter pair.
tryRemoveLast :: BilateralAncestralDeque a -> Maybe (a, BilateralAncestralDeque a)
tryRemoveLast deque = (,) <$> last deque <*> removeLast deque

-- | The first @amount@ values, or 'Nothing' when @amount@ is outside zero
-- through 'count'.
--
-- A prefix is a slice, so it costs at most two level-ancestor queries. The
-- whole-deque prefix retains the receiver itself.
take :: Int -> BilateralAncestralDeque a -> Maybe (BilateralAncestralDeque a)
take amount deque = fst <$> takeCost amount deque

-- | The deque after omitting its first @amount@ values, or 'Nothing' when
-- @amount@ is outside zero through 'count'.
--
-- A suffix is a slice, so it costs at most two level-ancestor queries. Omitting
-- nothing retains the receiver itself.
drop :: Int -> BilateralAncestralDeque a -> Maybe (BilateralAncestralDeque a)
drop amount deque = fst <$> dropCost amount deque

-- | One contiguous slice, or 'Nothing' when the index\/count pair is outside
-- this deque.
--
-- Every contiguous slice meets each interval at most once, so the result is
-- another bilateral handle and the operation costs at most /two/
-- level-ancestor queries however many values it selects. An empty slice and
-- the whole-deque slice cost none, and the whole-deque slice retains the
-- receiver itself.
slice :: Int -> Int -> BilateralAncestralDeque a -> Maybe (BilateralAncestralDeque a)
slice position amount deque = fst <$> sliceCost position amount deque

-- | 'slice' paired with the level-ancestor work it performed.
sliceCost
  :: Int
  -> Int
  -> BilateralAncestralDeque a
  -> Maybe (BilateralAncestralDeque a, QueryCost)
sliceCost position amount deque
  | position < 0 || amount < 0 = Nothing
  | position > total = Nothing
  | amount > total - position = Nothing
  | position == 0 && amount == total = Just (deque, noQueries)
  | amount == 0 = Just (empty, noQueries)
  | otherwise =
      Just
        ( BilateralAncestralDeque slicedLeft slicedRight amount
        , addCost leftCost rightCost
        )
  where
    total = dequeCount deque
    endPosition = position + amount
    leftSegment = dequeLeft deque
    rightSegment = dequeRight deque
    leftTotal = segmentCount leftSegment
    leftStart = min position leftTotal
    leftStop = min endPosition leftTotal
    rightStart = max 0 (position - leftTotal)
    rightStop = max 0 (endPosition - leftTotal)
    (slicedLeft, leftCost) = sliceLeft leftSegment leftStart (leftStop - leftStart)
    (slicedRight, rightCost) = sliceRight rightSegment rightStart (rightStop - rightStart)

-- | The prefix before a zero-based boundary and the suffix from it onward, or
-- 'Nothing' when the boundary is outside zero through 'count'.
--
-- The two results together enumerate the original value. Although the split is
-- expressed as a prefix plus a suffix, the pair costs at most /two/
-- level-ancestor queries in total, and an endpoint boundary retains the
-- receiver itself on the nonempty side.
splitAt
  :: Int
  -> BilateralAncestralDeque a
  -> Maybe (BilateralAncestralDeque a, BilateralAncestralDeque a)
splitAt position deque = fst <$> splitAtCost position deque

-- | 'splitAt' paired with the level-ancestor work the pair performed.
splitAtCost
  :: Int
  -> BilateralAncestralDeque a
  -> Maybe ((BilateralAncestralDeque a, BilateralAncestralDeque a), QueryCost)
splitAtCost position deque
  | position < 0 || position > dequeCount deque = Nothing
  | otherwise =
      case (takeCost position deque, dropCost position deque) of
        (Just (prefix, prefixCost), Just (suffix, suffixCost)) ->
          Just ((prefix, suffix), addCost prefixCost suffixCost)
        _ -> error "Durable7.FingerTree.BilateralAncestralDeque: internal split boundary"

-- | The logical reversal, by exchanging the two oriented intervals. @O(1)@
-- with no query; a deque of at most one value retains the receiver itself.
reverse :: BilateralAncestralDeque a -> BilateralAncestralDeque a
reverse deque
  | dequeCount deque <= 1 = deque
  | otherwise =
      BilateralAncestralDeque (dequeRight deque) (dequeLeft deque) (dequeCount deque)

-- | An empty, independently appendable handle. @O(1)@ with no query; an
-- already-empty deque retains the receiver itself.
clear :: BilateralAncestralDeque a -> BilateralAncestralDeque a
clear deque
  | dequeCount deque == 0 = deque
  | otherwise = empty

-- | Audits the cached interval endpoints and counts of one handle.
--
-- Checks the visible count against the two arm counts, each arm's count
-- against its endpoint depths, and each nonempty arm's anchor and cached base
-- against its tail's ancestry path. Confirming the ancestry path spends at
-- most two level-ancestor queries per nonempty arm, so the audit has a ceiling
-- of four queries and is not @O(1)@ work; an empty handle spends none.
--
-- Ports the C# internal @ValidateInvariants@ audit. That audit compares arena
-- handles for equality, which two distinct nodes of a pure forest cannot be
-- compared for without reference equality, so the endpoint-identity checks
-- become depth-agreement and query-domain checks here. A handle produced
-- through this module's API cannot hold an endpoint from the wrong branch in
-- the first place, because a node is its own handle.
validateStructure :: BilateralAncestralDeque a -> Either String BilateralStatistics
validateStructure deque
  | leftTotal < 0 || rightTotal < 0 =
      Left "An interval count is negative."
  | leftTotal > maxBound - rightTotal =
      Left "The interval counts overflow the visible count."
  | dequeCount deque /= leftTotal + rightTotal =
      Left "The total count disagrees with the interval counts."
  | otherwise = do
      validateSegment (dequeLeft deque)
      validateSegment (dequeRight deque)
      Right
        BilateralStatistics
          { bilateralCount = dequeCount deque
          , bilateralLeftCount = leftTotal
          , bilateralRightCount = rightTotal
          }
  where
    leftTotal = segmentCount (dequeLeft deque)
    rightTotal = segmentCount (dequeRight deque)

-- | Whether two handles retain the same interval endpoints rather than equal
-- copies of them.
--
-- Used by the tests to show that an operation documented as retaining its
-- receiver -- 'clear' on an empty deque, 'reverse' of at most one value, the
-- whole-deque slice or prefix, the empty suffix, and an endpoint 'splitAt' --
-- shared structure instead of rebuilding it.
--
-- Like @RangeUpdateSequence.sharesRootWith@ this compares the retained /roots/
-- through 'System.Mem.StableName.StableName' rather than the outer handle: a
-- handle is a small record of strict fields that the optimizer is free to
-- unbox and rebuild around unchanged endpoints, so only the endpoints
-- themselves witness sharing. Two empty handles therefore share, exactly as
-- two empty range-update sequences do.
sharesRootWith :: BilateralAncestralDeque a -> BilateralAncestralDeque a -> IO Bool
sharesRootWith left right
  | dequeCount left /= dequeCount right = pure False
  | otherwise = do
      sharedLeft <- sharesSegmentWith (dequeLeft left) (dequeLeft right)
      if sharedLeft
        then sharesSegmentWith (dequeRight left) (dequeRight right)
        else pure False

sharesSegmentWith :: Segment a -> Segment a -> IO Bool
sharesSegmentWith left right
  | segmentCount left /= segmentCount right = pure False
  | otherwise = do
      sharedAnchor <- sharesNodeWith (segmentAnchor left) (segmentAnchor right)
      sharedBase <- sharesNodeWith (segmentBase left) (segmentBase right)
      sharedTail <- sharesNodeWith (segmentTail left) (segmentTail right)
      pure (sharedAnchor && sharedBase && sharedTail)

takeCost :: Int -> BilateralAncestralDeque a -> Maybe (BilateralAncestralDeque a, QueryCost)
takeCost amount deque
  | amount < 0 || amount > dequeCount deque = Nothing
  | amount == dequeCount deque = Just (deque, noQueries)
  | otherwise = sliceCost 0 amount deque

dropCost :: Int -> BilateralAncestralDeque a -> Maybe (BilateralAncestralDeque a, QueryCost)
dropCost amount deque
  | amount < 0 || amount > dequeCount deque = Nothing
  | amount == 0 = Just (deque, noQueries)
  | otherwise = sliceCost amount (dequeCount deque - amount) deque

addToTail :: Segment a -> a -> Segment a
addToTail segment value =
  Segment (segmentAnchor segment) newBase newTail (segmentCount segment + 1)
  where
    newTail = addLeaf (segmentTail segment) value
    newBase = if segmentCount segment == 0 then newTail else segmentBase segment

removeTail :: Segment a -> Segment a
removeTail segment
  | segmentCount segment == 0 =
      error "Durable7.FingerTree.BilateralAncestralDeque: internal removal from an empty interval"
  | segmentCount segment == 1 = emptySegmentAt newTail
  | otherwise =
      Segment (segmentAnchor segment) (segmentBase segment) newTail (segmentCount segment - 1)
  where
    newTail = expectParent (segmentTail segment)

removeBase :: Segment a -> (Segment a, QueryCost)
removeBase segment
  | segmentCount segment == 0 =
      error "Durable7.FingerTree.BilateralAncestralDeque: internal removal from an empty interval"
  | segmentCount segment == 1 = (emptySegmentAt (segmentTail segment), noQueries)
  | segmentCount segment == 2 =
      ( Segment (segmentBase segment) (segmentTail segment) (segmentTail segment) 1
      , noQueries
      )
  | otherwise =
      ( Segment (segmentBase segment) newBase (segmentTail segment) (segmentCount segment - 1)
      , cost
      )
  where
    baseDepth =
      checkedAdd (checkedSub (nodeDepth (segmentTail segment)) (segmentCount segment)) 2
    (newBase, cost) = queryAncestor (segmentTail segment) baseDepth

sliceLeft :: Segment a -> Int -> Int -> (Segment a, QueryCost)
sliceLeft segment startIndex amount
  | amount == 0 = (emptySegmentAt bottom, noQueries)
  | startIndex == 0 && amount == segmentCount segment = (segment, noQueries)
  | otherwise = (Segment newAnchor newBase newTail amount, addCost tailCost baseCost)
  where
    tailDepth = nodeDepth (segmentTail segment)
    baseDepth = checkedAdd (checkedSub tailDepth (segmentCount segment)) 1
    slicedTailDepth = checkedSub tailDepth startIndex
    slicedBaseDepth = checkedAdd (checkedSub slicedTailDepth amount) 1
    (newTail, tailCost)
      | slicedTailDepth == tailDepth = (segmentTail segment, noQueries)
      | otherwise = queryAncestor (segmentTail segment) slicedTailDepth
    (newBase, baseCost)
      | slicedBaseDepth == slicedTailDepth = (newTail, noQueries)
      | slicedBaseDepth == baseDepth = (segmentBase segment, noQueries)
      | otherwise = queryAncestor (segmentTail segment) slicedBaseDepth
    newAnchor
      | slicedBaseDepth == baseDepth = segmentAnchor segment
      | otherwise = expectParent newBase

sliceRight :: Segment a -> Int -> Int -> (Segment a, QueryCost)
sliceRight segment startIndex amount
  | amount == 0 = (emptySegmentAt bottom, noQueries)
  | startIndex == 0 && amount == segmentCount segment = (segment, noQueries)
  | otherwise = (Segment newAnchor newBase newTail amount, addCost baseCost tailCost)
  where
    anchorDepth = nodeDepth (segmentAnchor segment)
    slicedBaseDepth = checkedAdd (checkedAdd anchorDepth startIndex) 1
    slicedTailDepth = checkedSub (checkedAdd slicedBaseDepth amount) 1
    endIndex = startIndex + amount
    (newBase, baseCost)
      | startIndex == 0 = (segmentBase segment, noQueries)
      | otherwise = queryAncestor (segmentTail segment) slicedBaseDepth
    (newTail, tailCost)
      | slicedTailDepth == slicedBaseDepth = (newBase, noQueries)
      | endIndex == segmentCount segment = (segmentTail segment, noQueries)
      | otherwise = queryAncestor (segmentTail segment) slicedTailDepth
    newAnchor
      | startIndex == 0 = segmentAnchor segment
      | otherwise = expectParent newBase

validateSegment :: Segment a -> Either String ()
validateSegment segment
  | segmentCount segment == 0 =
      if nodeDepth (segmentAnchor segment) == nodeDepth (segmentBase segment)
        && nodeDepth (segmentBase segment) == nodeDepth (segmentTail segment)
        then Right ()
        else Left "An empty interval does not have one shared endpoint."
  | checkedSub tailDepth anchorDepth /= segmentCount segment =
      Left "An interval count disagrees with its endpoint depths."
  | nodeDepth (segmentBase segment) /= anchorDepth + 1
      || fmap nodeDepth (nodeParent (segmentBase segment)) /= Just anchorDepth =
      Left "The cached base is not the first node after the anchor."
  | fmap nodeDepth (ancestorAtDepth (segmentTail segment) anchorDepth) /= Just anchorDepth =
      Left "The interval anchor is not an ancestor of its tail."
  | fmap nodeDepth (ancestorAtDepth (segmentTail segment) (anchorDepth + 1))
      /= Just (anchorDepth + 1) =
      Left "The cached base is not on the tail's ancestry path."
  | otherwise = Right ()
  where
    anchorDepth = nodeDepth (segmentAnchor segment)
    tailDepth = nodeDepth (segmentTail segment)

emptySegmentAt :: Node a -> Segment a
emptySegmentAt node = Segment node node node 0

valuesFromTail :: Node a -> Int -> [a]
valuesFromTail node amount
  | amount <= 0 = []
  | otherwise = expectValue node : valuesFromTail (expectParent node) (amount - 1)

reverseOnto :: [a] -> [a] -> [a]
reverseOnto [] suffix = suffix
reverseOnto (value : values) suffix = reverseOnto values (value : suffix)

queryAncestor :: Node a -> Int -> (Node a, QueryCost)
queryAncestor node depth =
  case ancestorAtDepthHops node depth of
    Just (found, hops) -> (found, QueryCost 1 hops)
    Nothing ->
      error "Durable7.FingerTree.BilateralAncestralDeque: internal ancestry query outside the interval"

noQueries :: QueryCost
noQueries = QueryCost 0 0

addCost :: QueryCost -> QueryCost -> QueryCost
addCost (QueryCost leftQueries leftHops) (QueryCost rightQueries rightHops) =
  QueryCost (leftQueries + rightQueries) (leftHops + rightHops)

expectValue :: Node a -> a
expectValue node =
  case nodeValue node of
    Just value -> value
    Nothing ->
      error "Durable7.FingerTree.BilateralAncestralDeque: internal unlabelled interval endpoint"

expectParent :: Node a -> Node a
expectParent node =
  case nodeParent node of
    Just parent -> parent
    Nothing ->
      error "Durable7.FingerTree.BilateralAncestralDeque: internal interval endpoint without a parent"

checkedIncrement :: Int -> Int
checkedIncrement value
  | value == maxBound =
      error "Durable7.FingerTree.BilateralAncestralDeque: the visible count exceeds the Int domain"
  | otherwise = value + 1

checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | right > 0 && left > maxBound - right = depthOverflow
  | right < 0 && left < minBound - right = depthOverflow
  | otherwise = left + right

checkedSub :: Int -> Int -> Int
checkedSub left right
  | right < 0 && left > maxBound + right = depthOverflow
  | right > 0 && left < minBound + right = depthOverflow
  | otherwise = left - right

depthOverflow :: a
depthOverflow =
  error "Durable7.FingerTree.BilateralAncestralDeque: the ancestry depth exceeds the Int domain"
