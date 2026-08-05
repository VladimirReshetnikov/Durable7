{-# LANGUAGE BangPatterns #-}

-- | An immutable, fully persistent queue whose every version is one contiguous
-- interval of an append-only ancestry path.
--
-- A value is the constant-sized triple @(tail, low depth, count)@ naming an
-- interval of the unique root-to-tail path through the incremental
-- level-ancestor forest of "Durable7.FingerTree.IncrementalAncestor". The
-- visible values are the labels of the ancestors of the tail at the absolute
-- depths @low, low + 1, ..., nodeDepth tail@, so
--
-- > count == nodeDepth tail - low + 1
--
-- holds for every reachable value. Appending adds one leaf below the tail;
-- removing and slicing move only the two interval boundaries. Because the
-- forest is append-only and its nodes are immutable, a retained handle keeps
-- denoting the same sequence forever, and adding below an /old/ handle starts a
-- sibling branch. That is what makes the structure fully persistent for its
-- restricted algebra: any historical version can be extended, not just the
-- newest. It is not confluently persistent, because unrelated versions cannot
-- be merged.
--
-- == The anchored-empty rule
--
-- An empty value is not \"nothing\". It retains the node immediately /before/
-- its window as an anchor, so @low == nodeDepth tail + 1@ holds whenever 'null'
-- does. Appending to any empty slice therefore yields exactly the new value and
-- never resurrects the discarded prefix. A queue drained from the front and one
-- drained from the back consequently keep /different/ anchors while denoting
-- the same empty sequence; there is no canonical empty node and 'empty' is only
-- the one anchored at the unlabelled bottom node.
--
-- == Bounds
--
-- Let @M@ be the number of leaves ever added on the queried branch, @U@ the
-- forest's leaf-add cost and @Q@ its level-ancestor query cost. Then 'addLast'
-- costs @U@; 'first', 'index', 'take', 'slice', 'tryRemoveFirst', and a
-- nontrivial 'splitAt' cost @Q@; and 'count', 'null', 'last', 'removeFirst',
-- 'removeLast', 'tryRemoveLast', and 'drop' are @O(1)@. The shipped forest has
-- @U = O(1)@ worst case and @Q = O(log M)@ worst case. Boundary-specialized
-- calls make no query at all: @take (count q)@, @drop 0@, @slice 0 (count q)@,
-- any suffix @slice@, a split at @count q@, and an index at the last visible
-- position all reuse a retained or parent-linked node. A split at @0@ of a
-- /non-empty/ queue is not query-free, because the anchored-empty rule makes the
-- empty prefix retain the node one level above the window, which only an
-- ancestor query can name; on an empty queue the two boundaries coincide and
-- both results are the receiver.
-- 'toList' walks parent links in @Theta(count)@ time.
--
-- == Divergence from the managed and native ports
--
-- Those ports own a mutable append-only arena of integer handles behind a lock,
-- injected through a @Create(arena)@ factory and observed through arena
-- statistics. Here a node /is/ its own handle, so a queue value retains nothing
-- but ordinary immutable heap structure. There is no arena object, no backend
-- injection, no arena statistics, and no way to present a node to the wrong
-- forest, so the two managed factories collapse into the single 'empty' and
-- leaf addition improves from @O(1)@ amortized to @O(1)@ worst case. 'tailNode'
-- and 'lowDepth' expose the retained interval boundaries in place of the arena
-- inspection those ports offer.
--
-- == Deliberate limits
--
-- The algebra intentionally excludes prepending, point replacement, arbitrary
-- middle edits, and concatenation of unrelated histories; omitting them is
-- exactly why the indexed and slicing bounds are possible. Ancestry space is
-- charged to all successful historical appends, not to the visible length of
-- one handle, so dropping a prefix reclaims nothing while any descendant of the
-- discarded nodes is retained. Two handles may enumerate equal values while
-- having different tails or anchors, so this module promises no @O(1)@ sequence
-- equality, hashing, or canonical empty identity, and provides none.
module Durable7.FingerTree.AncestralSliceQueue
  ( AncestralSliceQueue
  , ValidationStatistics(..)
  , empty
  , fromList
  , toList
  , count
  , null
  , first
  , last
  , index
  , addLast
  , removeFirst
  , removeLast
  , tryRemoveFirst
  , tryRemoveLast
  , take
  , drop
  , slice
  , splitAt
  , tailNode
  , lowDepth
  , validateStructure
  , sharesRootWith
  ) where

import Prelude hiding (drop, last, null, splitAt, take)

import Durable7.FingerTree.IncrementalAncestor (Node)
import qualified Durable7.FingerTree.IncrementalAncestor as Ancestor

-- | One immutable ancestry interval: the retained tail node, the absolute
-- ancestry depth of the first visible value, and a redundant count cache.
--
-- The cached boundaries are strict; the payload values stay lazy, since they
-- are held by the forest nodes rather than by this handle.
data AncestralSliceQueue a = AncestralSliceQueue !(Node a) !Int !Int

-- | Independently recomputed facts about one queue value, returned by
-- 'validateStructure'.
data ValidationStatistics = ValidationStatistics
  { validationCount :: !Int
    -- ^ Visible values, recounted by walking the window's parent links.
  , validationLowDepth :: !Int
    -- ^ The absolute ancestry depth of the first visible value.
  , validationAnchorDepth :: !Int
    -- ^ The depth of the node immediately before the window, which an empty
    -- value retains as its anchor. Always one less than 'validationLowDepth'.
  , validationTailDepth :: !Int
    -- ^ The absolute ancestry depth of the retained tail node.
  , validationPathLength :: !Int
    -- ^ Labelled nodes from the tail down to the unlabelled bottom node,
    -- including the ancestry below the window that this value cannot see.
  , validationJumpLinkCount :: !Int
    -- ^ Nodes on that root path whose jump link skips more than one level.
  }
  deriving (Eq, Show)

-- | The empty queue anchored at the unlabelled bottom node. @O(1)@.
--
-- This is the only empty value with that anchor; empties produced by draining
-- or by slicing keep the node immediately before their own window instead.
empty :: AncestralSliceQueue a
empty = AncestralSliceQueue Ancestor.bottom 0 0

-- | Appends a list's values in order, starting from 'empty'. @Theta(k)@ for a
-- list of @k@ values, publishing exactly one forest node per value.
fromList :: [a] -> AncestralSliceQueue a
fromList = go empty
  where
    go !result [] = result
    go !result (value : values) = go (addLast result value) values

-- | The visible values in front-to-back order. @Theta(count)@ time and
-- @Theta(count)@ transient list cells.
--
-- The path is captured by following parent links from the retained tail rather
-- than by repeating @count@ ancestor queries. Later appends create sibling
-- branches and so can never enter a path this function already walked.
toList :: AncestralSliceQueue a -> [a]
toList (AncestralSliceQueue node _ visible) = walk visible node []
  where
    walk remaining current suffix
      | remaining <= 0 = suffix
      | remaining == 1 = expectValue current : suffix
      | otherwise = walk (remaining - 1) (expectParent current) (expectValue current : suffix)

-- | The number of visible values, read from the cache. @O(1)@.
count :: AncestralSliceQueue a -> Int
count (AncestralSliceQueue _ _ visible) = visible

-- | Whether this handle denotes an empty ancestry interval. @O(1)@.
null :: AncestralSliceQueue a -> Bool
null queue = count queue == 0

-- | The first visible value, or 'Nothing' when empty.
--
-- Costs one ancestor query: the front is selected jointly by the tail and the
-- low boundary, so it is not an @O(1)@ endpoint read. A singleton reuses its
-- retained tail and makes no query.
first :: AncestralSliceQueue a -> Maybe a
first = index 0

-- | The last visible value, or 'Nothing' when empty. @O(1)@, since the tail is
-- retained by the handle itself.
last :: AncestralSliceQueue a -> Maybe a
last (AncestralSliceQueue node _ visible)
  | visible == 0 = Nothing
  | otherwise = Just (expectValue node)

-- | The visible value at a zero-based position, or 'Nothing' when the position
-- is outside the window.
--
-- Costs one ancestor query; the last visible position reuses the retained tail
-- and makes no query.
index :: Int -> AncestralSliceQueue a -> Maybe a
index position (AncestralSliceQueue node low visible)
  | position < 0 || position >= visible = Nothing
  | position == visible - 1 = Just (expectValue node)
  | otherwise = Just (expectValue (expectAncestor node (checkedAdd low position)))

-- | The queue with one value appended below this handle's tail or empty anchor.
--
-- @O(1)@ worst case, publishing exactly one forest node. The receiver and every
-- other retained version are unaffected; appending below an old handle starts a
-- sibling branch. Raises an error when the visible count cannot grow further.
addLast :: AncestralSliceQueue a -> a -> AncestralSliceQueue a
addLast (AncestralSliceQueue node low visible) value
  | visible == maxBound =
      error "Durable7.FingerTree.AncestralSliceQueue: visible count overflow"
  | otherwise = AncestralSliceQueue (Ancestor.addLeaf node value) low (visible + 1)

-- | The queue without its first visible value, or 'Nothing' when empty. @O(1)@.
--
-- Only the low boundary moves, so no ancestor query is made. Emptying a
-- singleton this way leaves the old tail as the result's anchor.
removeFirst :: AncestralSliceQueue a -> Maybe (AncestralSliceQueue a)
removeFirst (AncestralSliceQueue node low visible)
  | visible == 0 = Nothing
  | otherwise = Just (AncestralSliceQueue node (checkedAdd low 1) (visible - 1))

-- | The queue without its last visible value, or 'Nothing' when empty. @O(1)@.
--
-- The new tail is the old tail's parent, which is one immediate link. Emptying
-- a singleton this way anchors the result immediately before the window.
removeLast :: AncestralSliceQueue a -> Maybe (AncestralSliceQueue a)
removeLast (AncestralSliceQueue node low visible)
  | visible == 0 = Nothing
  | otherwise = Just (AncestralSliceQueue (expectParent node) low (visible - 1))

-- | The first visible value together with the queue that remains without it, or
-- 'Nothing' when empty.
--
-- Costs one ancestor query, because it also reports the front value. This is
-- the reference contract's @TryRemoveFirst@; the receiver stays valid either
-- way.
tryRemoveFirst :: AncestralSliceQueue a -> Maybe (a, AncestralSliceQueue a)
tryRemoveFirst queue = do
  value <- first queue
  rest <- removeFirst queue
  pure (value, rest)

-- | The last visible value together with the queue that remains without it, or
-- 'Nothing' when empty. @O(1)@.
tryRemoveLast :: AncestralSliceQueue a -> Maybe (a, AncestralSliceQueue a)
tryRemoveLast queue = do
  value <- last queue
  rest <- removeLast queue
  pure (value, rest)

-- | The first @amount@ visible values, or 'Nothing' when @amount@ is negative
-- or exceeds the length.
--
-- Costs one ancestor query. @take (count queue) queue@ returns the receiver
-- itself and makes no query. The result is a real ancestry slice and stays
-- appendable; @take 0@ is anchored immediately before the window.
take :: Int -> AncestralSliceQueue a -> Maybe (AncestralSliceQueue a)
take amount queue@(AncestralSliceQueue _ _ visible)
  | amount < 0 || amount > visible = Nothing
  | amount == visible = Just queue
  | otherwise = slice 0 amount queue

-- | The queue after omitting its first @amount@ visible values, or 'Nothing'
-- when @amount@ is negative or exceeds the length. @O(1)@.
--
-- Only the low boundary moves, so no ancestor query is made. @drop 0@ returns
-- the receiver itself, and @drop (count queue) queue@ keeps the old tail as the
-- empty result's anchor.
drop :: Int -> AncestralSliceQueue a -> Maybe (AncestralSliceQueue a)
drop amount queue@(AncestralSliceQueue node low visible)
  | amount < 0 || amount > visible = Nothing
  | amount == 0 = Just queue
  | otherwise = Just (AncestralSliceQueue node (checkedAdd low amount) (visible - amount))

-- | One contiguous, appendable ancestry slice, or 'Nothing' for an invalid
-- range.
--
-- Costs one ancestor query in general. @slice 0 (count queue) queue@ returns
-- the receiver itself, and any range ending at the current tail — the whole
-- range and every suffix — reuses that tail, so neither makes a query. A
-- zero-length range selects the node immediately before its start as an anchor,
-- which is the unlabelled bottom node when the start is the very front of a
-- queue grown from 'empty'.
slice :: Int -> Int -> AncestralSliceQueue a -> Maybe (AncestralSliceQueue a)
slice position amount queue@(AncestralSliceQueue node low visible)
  | position < 0 || amount < 0 = Nothing
  | position > visible = Nothing
  | amount > visible - position = Nothing
  | position == 0 && amount == visible = Just queue
  | otherwise = Just (AncestralSliceQueue selected windowLow amount)
  where
    windowLow = checkedAdd low position
    targetDepth = checkedAdd windowLow amount - 1
    currentTailDepth = checkedAdd low visible - 1
    selected
      | targetDepth == currentTailDepth = node
      | otherwise = expectAncestor node targetDepth

-- | The queue split at a zero-based boundary, or 'Nothing' when the boundary is
-- negative or exceeds the length.
--
-- Costs one ancestor query: the prefix performs the one ancestor-seeking query
-- and the suffix only moves the low boundary. Both results are real ancestry
-- slices and can independently start new branches. Only a split at
-- @count queue@ makes no query; on a non-empty queue a split at @0@ still costs
-- one, because the anchored-empty rule requires the empty prefix to retain the
-- node one level above the window. On an empty queue the two boundaries
-- coincide, so both results are the receiver and no query happens.
splitAt :: Int
        -> AncestralSliceQueue a
        -> Maybe (AncestralSliceQueue a, AncestralSliceQueue a)
splitAt position queue = do
  left <- take position queue
  right <- drop position queue
  pure (left, right)

-- | The retained ancestry node: the last visible value's node when the queue is
-- non-empty, and the anchor immediately before the window when it is empty.
-- @O(1)@.
--
-- This replaces the arena-handle inspection the managed and native ports offer.
-- Nodes are immutable, so exposing one cannot disturb any version; it lets a
-- caller show that an operation shared structure instead of re-deriving it.
tailNode :: AncestralSliceQueue a -> Node a
tailNode (AncestralSliceQueue node _ _) = node

-- | The absolute ancestry depth of the first visible value. @O(1)@.
--
-- Equals @'Ancestor.nodeDepth' ('tailNode' queue) + 1@ exactly when the queue is
-- empty, which is the anchored-empty rule stated as an equation.
lowDepth :: AncestralSliceQueue a -> Int
lowDepth (AncestralSliceQueue _ low _) = low

-- | Audits one queue value against the interval invariant and the forest
-- invariants below it.
--
-- Checks that the count and low depth are non-negative, that the tail's depth
-- closes a window of exactly @count@ values starting at the low depth, that the
-- whole root path satisfies the Myers jump-link invariants, and that walking
-- parent links from the tail visits exactly @count@ labelled nodes before
-- reaching the anchor depth. Returns a description of the first violation, or
-- independently recomputed statistics. @O(d)@ for a tail at depth @d@.
validateStructure :: AncestralSliceQueue a -> Either String ValidationStatistics
validateStructure (AncestralSliceQueue node low visible)
  | visible < 0 =
      Left ("A visible count of " ++ show visible ++ " is negative.")
  | low < 0 =
      Left ("A low depth of " ++ show low ++ " is negative.")
  | toInteger tailDepth /= toInteger low + toInteger visible - 1 =
      Left ("A tail at depth " ++ show tailDepth ++ " does not close a window of "
              ++ show visible ++ " values starting at depth " ++ show low ++ ".")
  | otherwise = do
      pathStatistics <- Ancestor.validateStructure node
      walked <- walkWindow 0 node
      if walked /= visible
        then Left ("A window walk visited " ++ show walked ++ " labelled nodes instead of "
                     ++ show visible ++ ".")
        else Right ValidationStatistics
          { validationCount = walked
          , validationLowDepth = low
          , validationAnchorDepth = low - 1
          , validationTailDepth = tailDepth
          , validationPathLength = Ancestor.ancestorPathLength pathStatistics
          , validationJumpLinkCount = Ancestor.ancestorJumpLinkCount pathStatistics
          }
  where
    tailDepth = Ancestor.nodeDepth node

    walkWindow !visited current
      | Ancestor.nodeDepth current == low - 1 = Right visited
      | Ancestor.isBottom current =
          Left "A visible window extends past the unlabelled bottom node."
      | otherwise =
          case Ancestor.nodeParent current of
            Nothing -> Left "A labelled window node has no parent link."
            Just parent -> walkWindow (visited + 1) parent

-- | Whether two queues retain the same ancestry node rather than equal copies.
--
-- The retained node is what a queue shares or rebuilds, so this is the
-- diagnostic that distinguishes an operation which reused structure from one
-- which re-derived it. Two values sharing a node, a low depth, and a count are
-- indistinguishable, since those three fields are the entire representation.
sharesRootWith :: AncestralSliceQueue a -> AncestralSliceQueue a -> IO Bool
sharesRootWith left right = Ancestor.sharesNodeWith (tailNode left) (tailNode right)

expectValue :: Node a -> a
expectValue node =
  case Ancestor.nodeValue node of
    Just value -> value
    Nothing ->
      error "Durable7.FingerTree.AncestralSliceQueue: a visible position resolved to the bottom node"

expectParent :: Node a -> Node a
expectParent node =
  case Ancestor.nodeParent node of
    Just parent -> parent
    Nothing ->
      error "Durable7.FingerTree.AncestralSliceQueue: a visible tail has no parent link"

expectAncestor :: Node a -> Int -> Node a
expectAncestor node depth =
  case Ancestor.ancestorAtDepth node depth of
    Just ancestor -> ancestor
    Nothing ->
      error "Durable7.FingerTree.AncestralSliceQueue: a window depth left the retained path"

-- | Adds two ancestry depths, refusing to publish a wrapped one.  A wrapped
-- depth is worse than a failure here: it names a node on no path at all, so
-- every window bound derived from it is nonsense.
checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | left < 0 || right < 0 =
      error "Durable7.FingerTree.AncestralSliceQueue: internal negative depth"
  | right > maxBound - left =
      error "Durable7.FingerTree.AncestralSliceQueue: ancestry-depth overflow"
  | otherwise = left + right
