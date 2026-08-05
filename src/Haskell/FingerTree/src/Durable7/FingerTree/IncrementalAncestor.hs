{-# LANGUAGE BangPatterns #-}

-- | The append-only incremental level-ancestor machinery shared by the
-- ancestry-backed collections.
--
-- An incremental level-ancestor forest grows one leaf at a time and answers
-- \"which ancestor of this node sits at absolute depth @d@?\". A leaf may be
-- added below any node that already exists, not only below the most recently
-- added one, so old handles grow into independent branches.
--
-- Each node keeps one parent link and one coalesced jump link, the two-link
-- applicative-stack scheme of Myers. Adding a leaf does @O(1)@ link work in
-- the worst case, the primitive reads 'nodeDepth', 'nodeParent', and
-- 'nodeValue' are @O(1)@, and 'ancestorAtDepth' follows @O(log M)@
-- parent\/jump links after @M@ additions on the queried branch.
--
-- The managed and native ports of this seam own a mutable append-only arena
-- of integer handles behind a lock. Here the forest is ordinary immutable
-- heap structure: a node /is/ its own handle, so there is no arena object, no
-- lock, no handle-recycling question, and no way to present a node to the
-- wrong forest. Nodes are shared rather than copied, so retaining an old node
-- retains only its own root path.
module Durable7.FingerTree.IncrementalAncestor
  ( Node
  , bottom
  , isBottom
  , addLeaf
  , nodeDepth
  , nodeParent
  , nodeValue
  , ancestorAtDepth
  , ancestorAtDepthHops
  , AncestorStatistics(..)
  , validateStructure
  , sharesNodeWith
  ) where

import Control.Exception (evaluate)
import System.Mem.StableName (eqStableName, makeStableName)

-- | One immutable node of an incremental level-ancestor forest.
--
-- The distinguished bottom node sits at depth @-1@ and carries no value;
-- every labelled node has a depth of zero or more.
data Node a
  = Bottom
  | Labelled a !(Node a) !Int !(Node a) !Int

-- | The distinguished unlabelled node at depth @-1@.
bottom :: Node a
bottom = Bottom

-- | Whether a node is the unlabelled bottom node. @O(1)@.
isBottom :: Node a -> Bool
isBottom Bottom = True
isBottom Labelled{} = False

-- | A node's immutable absolute depth; the bottom node has depth @-1@. @O(1)@.
nodeDepth :: Node a -> Int
nodeDepth Bottom = -1
nodeDepth (Labelled _ _ d _ _) = d

-- | A labelled node's parent, or 'Nothing' for the bottom node, which has
-- none. A node at depth zero has the bottom node as its parent. @O(1)@.
nodeParent :: Node a -> Maybe (Node a)
nodeParent Bottom = Nothing
nodeParent (Labelled _ p _ _ _) = Just p

-- | A labelled node's value, or 'Nothing' for the bottom node, which carries
-- none. @O(1)@.
nodeValue :: Node a -> Maybe a
nodeValue Bottom = Nothing
nodeValue (Labelled v _ _ _ _) = Just v

-- | Adds a labelled leaf below an existing node, returning the new leaf.
--
-- @O(1)@ worst case: one node allocation and one jump-link decision. The
-- supplied node is unchanged and remains valid, so adding below an old node
-- creates a sibling branch rather than extending the previous leaf.
addLeaf :: Node a -> a -> Node a
addLeaf p v = Labelled v p (nodeDepth p + 1) skip skipDistance
  where
    (skip, skipDistance) = case p of
      Bottom -> (Bottom, 1)
      Labelled _ _ _ parentSkip parentDistance -> case parentSkip of
        Bottom -> (p, 1)
        Labelled _ _ _ skippedSkip skippedDistance
          | skippedDistance <= parentDistance ->
              (skippedSkip, 1 + parentDistance + skippedDistance)
          | otherwise -> (p, 1)

-- | The unique ancestor of a node at an absolute depth.
--
-- Returns 'Nothing' when the depth is below @-1@ or above the node's own
-- depth. Follows @O(log M)@ parent\/jump links after @M@ additions on this
-- branch.
ancestorAtDepth :: Node a -> Int -> Maybe (Node a)
ancestorAtDepth node depth = fst <$> ancestorAtDepthHops node depth

-- | 'ancestorAtDepth' paired with the number of parent\/jump links the query
-- followed.
--
-- The managed ports retain hop counters on the mutable arena and expose them
-- through an arena statistics snapshot. A pure forest has nowhere to retain
-- them, so the count is returned by the query that produced it.
ancestorAtDepthHops :: Node a -> Int -> Maybe (Node a, Int)
ancestorAtDepthHops node depth
  | depth < -1 || depth > nodeDepth node = Nothing
  | otherwise = Just (go node 0)
  where
    go current !hops
      | nodeDepth current <= depth = (current, hops)
      | otherwise = go (step current) (hops + 1)
      where
        step Bottom = Bottom
        step (Labelled _ p d skip skipDistance)
          | skipDistance > 0 && skipDistance <= d - depth = skip
          | otherwise = p

-- | Independently recomputed facts about one node's root path, returned by
-- 'validateStructure'.
data AncestorStatistics = AncestorStatistics
  { ancestorDepth :: !Int
    -- ^ The node's own depth.
  , ancestorPathLength :: !Int
    -- ^ Labelled nodes from this node down to the bottom node.
  , ancestorJumpLinkCount :: !Int
    -- ^ Path nodes whose jump link skips more than one level.
  , ancestorMaximumJumpDistance :: !Int
    -- ^ The greatest jump distance on the path.
  }
  deriving (Eq, Show)

-- | Audits a node's whole root path against the Myers invariants.
--
-- Checks that depths decrease by exactly one per parent link, that the bottom
-- node terminates the path at depth @-1@, and that every jump link lands
-- exactly its recorded distance above its owner. Returns a description of the
-- first violation, or independently recomputed path statistics. @O(d)@ for a
-- node at depth @d@.
validateStructure :: Node a -> Either String AncestorStatistics
validateStructure = go 0 0 0
  where
    go !path !jumps !maximumDistance Bottom =
      Right AncestorStatistics
        { ancestorDepth = -1
        , ancestorPathLength = path
        , ancestorJumpLinkCount = jumps
        , ancestorMaximumJumpDistance = maximumDistance
        }
    go !path !jumps !maximumDistance node@(Labelled _ p d skip skipDistance)
      | d < 0 = Left ("A labelled node has negative depth " ++ show d ++ ".")
      | nodeDepth p /= d - 1 =
          Left ("A parent link at depth " ++ show d ++ " does not descend exactly one level.")
      | skipDistance < 1 =
          Left ("A jump link at depth " ++ show d ++ " has non-positive distance.")
      | nodeDepth skip /= d - skipDistance =
          Left ("A jump link at depth " ++ show d ++ " does not match its recorded distance.")
      | otherwise = case go (path + 1) jumps' maximumDistance' p of
          Left message -> Left message
          Right statistics -> Right statistics { ancestorDepth = nodeDepth node }
      where
        jumps' = if skipDistance > 1 then jumps + 1 else jumps
        maximumDistance' = max maximumDistance skipDistance

-- | Whether two nodes are the same retained node rather than equal copies.
--
-- Used by the tests to show that an operation shared structure instead of
-- rebuilding it.
sharesNodeWith :: Node a -> Node a -> IO Bool
sharesNodeWith Bottom Bottom = pure True
sharesNodeWith left right = do
  leftValue <- evaluate left
  rightValue <- evaluate right
  leftName <- makeStableName leftValue
  rightName <- makeStableName rightValue
  pure (leftName `eqStableName` rightName)
