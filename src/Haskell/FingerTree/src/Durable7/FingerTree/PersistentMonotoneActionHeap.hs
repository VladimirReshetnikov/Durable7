{-# LANGUAGE BangPatterns #-}

-- | A persistent Brodal-Okasaki heap whose priorities carry lazily composable monotone actions.
--
-- The representation is the action-tagged sibling of
-- "Durable7.FingerTree.BrodalOkasakiHeap": a bootstrapped skew-binomial forest whose global
-- minimum sits at a rank-zero root, with one immutable /pending action/ added to every tree node
-- and every forest spine cell.  A tree tag applies to that tree and all of its descendants; a
-- forest tag applies uniformly to every tree in that spine suffix.
--
-- The point of the tags is 'transformAll', which applies one monotone action to every priority
-- /currently/ in a version by composing a single tag at the root.  That costs O(1) worst-case time
-- and allocates O(1) new structure.  Its semantics are deliberately temporal: entries inserted
-- later, and entries arriving through a later meld, are not retroactively transformed.  Two heaps
-- transformed independently still meld in O(1) worst-case time, including when the actions have no
-- inverse (a clamp has none).
--
-- The ordinary heap bounds are unchanged: 'insert', 'minimum', and 'meld' are O(1) worst-case;
-- 'minView' and 'deleteMinimum' are O(log n) worst-case; 'toList' and 'validateStructure' are
-- Theta(n); retaining a version as a fork is O(1).  Those bounds count the policy calls and
-- priority comparisons they perform, so a policy whose 'actionIsIdentity', 'actionCompose', or
-- 'actionApply' is not O(1) over fixed-size actions invalidates them.
--
-- Ordering and the action family both come from a retained 'MonotoneActionPolicy' /value/ rather
-- than from a class constraint, so an empty or fully drained result stays usable.  The policy is
-- trusted: violating its identity, associative-composition, or monotonicity contract invalidates
-- the semantic and complexity guarantees.  Every update is persistent and leaves all source
-- versions valid, sharing unchanged structure.
module Durable7.FingerTree.PersistentMonotoneActionHeap
  ( MonotoneActionPolicy(..)
  , OrderClamp
  , orderClampPolicy
  , naturalClampPolicy
  , clampIdentity
  , clampAtLeast
  , clampAtMost
  , clampBetween
  , clampConstant
  , clampIsIdentity
  , clampIsConstant
  , clampConstantValue
  , clampLowerBound
  , clampUpperBound
  , PersistentMonotoneActionHeap
  , MonotoneHeapEntry(..)
  , MonotoneActionHeapStatistics(..)
  , emptyWith
  , fromListWith
  , actionPolicy
  , count
  , null
  , minimum
  , insert
  , transformAll
  , meld
  , minView
  , deleteMinimum
  , toList
  , validateStructure
  , sharesRootWith
  ) where

import Prelude hiding (minimum, null)

import Control.Exception (evaluate)
import qualified Data.List as List
import Data.Maybe (isJust)
import System.Mem.StableName (eqStableName, makeStableName)

-- | A constant-time-composable family of monotone priority actions, bundled with the priority
-- order the family is monotone for.
--
-- @'actionCompose' outer inner@ denotes @outer (inner p)@, so the /newer/ action is always the
-- outer one.  Composition must be associative, 'actionIdentity' must be a two-sided unit that
-- 'actionIsIdentity' accepts, and every action must be monotone for 'priorityCompare': if
-- @p <= q@ then @'actionApply' a p <= 'actionApply' a q@.  Each of 'actionIsIdentity',
-- 'actionCompose', and 'actionApply' must run in O(1) time over fixed-size actions, and
-- 'priorityCompare' must be a stable total preorder over every retained priority; the heap's
-- advertised bounds are stated in terms of those calls.
--
-- Composition must also preserve the family's observable priority semantics, including any
-- representative distinctions callers care about when 'priorityCompare' treats distinct values as
-- equal.
data MonotoneActionPolicy priority action = MonotoneActionPolicy
  { actionIdentity :: action
    -- ^ The semantic identity action.
  , actionIsIdentity :: action -> Bool
    -- ^ Whether an action is the semantic identity, in O(1) time.
  , actionCompose :: action -> action -> action
    -- ^ @'actionCompose' outer inner@ is @outer (inner p)@, in O(1) time.
  , actionApply :: action -> priority -> priority
    -- ^ Applies one action to one priority, in O(1) time.
  , priorityCompare :: priority -> priority -> Ordering
    -- ^ The total preorder every action of this family is monotone for.
  }

-- | A lower bound, an upper bound, both bounds, an exact constant, or the identity.
--
-- A missing bound denotes negative or positive infinity.  Values are built by 'clampAtLeast',
-- 'clampAtMost', 'clampBetween', 'clampConstant', and 'clampIdentity' so that a two-sided clamp is
-- validated with the same comparison used for application and composition.  Composition always
-- yields either one constant-sized clamp or an explicit constant function, so composing and
-- applying are O(1).
--
-- The explicit constant case preserves exact results even when the comparison treats distinct
-- priority values as equal: an inclusive @[k, k]@ clamp passes an input equivalent to @k@ through
-- unchanged, whereas the constant function returns the exact @k@ representative it stores.
data OrderClamp priority =
  OrderClamp !(Maybe priority) !(Maybe priority) !(Maybe priority)
  deriving (Eq, Ord, Show)

-- | The clamp family @x -> clamp x lower upper@ over a caller-supplied comparison, in O(1).
--
-- The resulting policy retains @compareValues@ as its 'priorityCompare', which is the comparison
-- the family is monotone for.  'clampBetween' must be given that same comparison.
orderClampPolicy :: (priority -> priority -> Ordering)
                 -> MonotoneActionPolicy priority (OrderClamp priority)
orderClampPolicy compareValues = MonotoneActionPolicy
  { actionIdentity = clampIdentity
  , actionIsIdentity = clampIsIdentity
  , actionCompose = composeClamp compareValues
  , actionApply = applyClamp compareValues
  , priorityCompare = compareValues
  }

-- | The clamp family over the canonical 'Ord' comparison, in O(1).
naturalClampPolicy :: Ord priority => MonotoneActionPolicy priority (OrderClamp priority)
naturalClampPolicy = orderClampPolicy compare

-- | The identity clamp, in O(1).
clampIdentity :: OrderClamp priority
clampIdentity = OrderClamp Nothing Nothing Nothing

-- | A floor action @x -> max x lowerBound@, in O(1).
clampAtLeast :: priority -> OrderClamp priority
clampAtLeast lowerBound = OrderClamp Nothing (Just lowerBound) Nothing

-- | A cap action @x -> min x upperBound@, in O(1).
clampAtMost :: priority -> OrderClamp priority
clampAtMost upperBound = OrderClamp Nothing Nothing (Just upperBound)

-- | An exact constant action, in O(1).
--
-- Unlike an inclusive @[k, k]@ clamp this returns the stored representative for every input, which
-- matters when the comparison is coarser than value identity.
clampConstant :: priority -> OrderClamp priority
clampConstant value = OrderClamp (Just value) Nothing Nothing

-- | A two-sided clamp action, in O(1), or 'Nothing' when the lower bound compares greater than the
-- upper bound.
--
-- @compareValues@ must be the comparison retained by the policy this action will be used with.
clampBetween :: (priority -> priority -> Ordering)
             -> priority
             -> priority
             -> Maybe (OrderClamp priority)
clampBetween compareValues lowerBound upperBound
  | compareValues lowerBound upperBound == GT = Nothing
  | otherwise = Just (OrderClamp Nothing (Just lowerBound) (Just upperBound))

-- | Whether a clamp is the identity, in O(1).
clampIsIdentity :: OrderClamp priority -> Bool
clampIsIdentity (OrderClamp Nothing Nothing Nothing) = True
clampIsIdentity _ = False

-- | Whether a clamp is an exact constant function, in O(1).
clampIsConstant :: OrderClamp priority -> Bool
clampIsConstant (OrderClamp constantValue _ _) = isJust constantValue

-- | The constant result of a clamp, or 'Nothing' when it is not constant, in O(1).
clampConstantValue :: OrderClamp priority -> Maybe priority
clampConstantValue (OrderClamp constantValue _ _) = constantValue

-- | The lower bound of a clamp, or 'Nothing' when it has none, in O(1).
clampLowerBound :: OrderClamp priority -> Maybe priority
clampLowerBound (OrderClamp _ lowerBound _) = lowerBound

-- | The upper bound of a clamp, or 'Nothing' when it has none, in O(1).
clampUpperBound :: OrderClamp priority -> Maybe priority
clampUpperBound (OrderClamp _ _ upperBound) = upperBound

-- Composition keeps the clamp family closed.  The identity short-circuits, an outer constant wins
-- outright, an inner constant is pushed through the outer action, disjoint bounds collapse to a
-- constant carrying the outer (newer) boundary, and overlapping bounds intersect while keeping the
-- inner (older) representative whenever two boundaries compare equal.
composeClamp :: (priority -> priority -> Ordering)
             -> OrderClamp priority
             -> OrderClamp priority
             -> OrderClamp priority
composeClamp compareValues outer inner
  | clampIsIdentity outer = inner
  | clampIsIdentity inner = outer
  | isJust outerConstant = outer
  | Just value <- innerConstant =
      OrderClamp (Just (applyClamp compareValues outer value)) Nothing Nothing
  | disjointBelow = OrderClamp outerLower Nothing Nothing
  | disjointAbove = OrderClamp outerUpper Nothing Nothing
  | otherwise = OrderClamp Nothing mergedLower mergedUpper
  where
    OrderClamp outerConstant outerLower outerUpper = outer
    OrderClamp innerConstant innerLower innerUpper = inner
    disjointBelow = case (innerUpper, outerLower) of
      (Just ceiling', Just floor') -> compareValues ceiling' floor' == LT
      _ -> False
    disjointAbove = case (innerLower, outerUpper) of
      (Just floor', Just ceiling') -> compareValues floor' ceiling' == GT
      _ -> False
    -- The inner bound is the older one, so it wins whenever the two compare equal.
    mergedLower = case (innerLower, outerLower) of
      (Nothing, newer) -> newer
      (Just older, Nothing) -> Just older
      (Just older, Just newer) ->
        Just (if compareValues older newer /= LT then older else newer)
    mergedUpper = case (innerUpper, outerUpper) of
      (Nothing, newer) -> newer
      (Just older, Nothing) -> Just older
      (Just older, Just newer) ->
        Just (if compareValues older newer /= GT then older else newer)

-- Application tests the constant first, then the lower bound, then the upper bound.
applyClamp :: (priority -> priority -> Ordering)
           -> OrderClamp priority
           -> priority
           -> priority
applyClamp compareValues (OrderClamp constantValue lowerBound upperBound) value =
  case constantValue of
    Just result -> result
    Nothing -> case lowerBound of
      Just bound | compareValues value bound == LT -> bound
      _ -> case upperBound of
        Just bound | compareValues value bound == GT -> bound
        _ -> value

-- | A payload paired with its current logical priority, with every pending action already applied.
data MonotoneHeapEntry element priority = MonotoneHeapEntry
  { entryElement :: element
    -- ^ The payload.
  , entryPriority :: !priority
    -- ^ The logical priority.
  }
  deriving (Eq, Ord, Read, Show)

-- | Shape measurements independently recomputed by 'validateStructure'.
data MonotoneActionHeapStatistics = MonotoneActionHeapStatistics
  { monotoneStatisticsCount :: !Int
    -- ^ The logical entry count.
  , monotoneStatisticsRootForestLength :: !Int
    -- ^ The number of skew-binomial trees directly below the global root.
  , monotoneStatisticsMaximumRank :: !Int
    -- ^ The largest skew-binomial rank encountered.
  , monotoneStatisticsMaximumDepth :: !Int
    -- ^ The deepest global-root-to-tree path, counting the root as depth one.
  , monotoneStatisticsTaggedComponentCount :: !Int
    -- ^ The number of pending tree and forest tag observations made during the audit.  This is not
    -- a distinct-node count, because exposing one uniform forest tag produces several tagged
    -- logical views of the same retained cells.
  }
  deriving (Eq, Ord, Read, Show)

-- One skew-binomial tree carrying a pending action for itself and all of its descendants.  The
-- logical priority is @'actionApply' policy action priority@ and the logical children are
-- @tagForest policy action children@.
data Tree element priority action = Tree
  { treeRank :: !Int
  , treeElement :: element
  , treePriority :: !priority
  , treeChildren :: !(Forest element priority action)
  , treeAction :: !action
  }

-- One forest spine cell carrying a pending action for its whole suffix.  The logical head is
-- @tagTree policy action rawHead@ and the logical tail is @tagForest policy action rawTail@.
data Forest element priority action
  = ForestNil
  | ForestCons !action !(Tree element priority action) !(Forest element priority action)

-- | An immutable action-tagged Brodal-Okasaki min-heap.  The policy record is retained so that
-- empty and drained results remain usable without a global policy facility.
data PersistentMonotoneActionHeap element priority action =
  PersistentMonotoneActionHeap
    !(MonotoneActionPolicy priority action)
    !Int
    !(Maybe (Tree element priority action))

-- | An empty heap retaining an action policy, in O(1).
emptyWith :: MonotoneActionPolicy priority action
          -> PersistentMonotoneActionHeap element priority action
emptyWith policy = PersistentMonotoneActionHeap policy 0 Nothing

-- | O(n) construction from payload/priority pairs by repeated worst-case-O(1) insertion.
fromListWith :: MonotoneActionPolicy priority action
             -> [(element, priority)]
             -> PersistentMonotoneActionHeap element priority action
fromListWith policy =
  List.foldl' (\heap (element, priority) -> insert element priority heap) (emptyWith policy)

-- | The retained action policy, in O(1).
actionPolicy :: PersistentMonotoneActionHeap element priority action
             -> MonotoneActionPolicy priority action
actionPolicy (PersistentMonotoneActionHeap policy _ _) = policy

-- | The number of entries, in O(1).
count :: PersistentMonotoneActionHeap element priority action -> Int
count (PersistentMonotoneActionHeap _ total _) = total

-- | Whether the heap holds no entries, in O(1).
null :: PersistentMonotoneActionHeap element priority action -> Bool
null (PersistentMonotoneActionHeap _ _ root) = case root of
  Nothing -> True
  Just _ -> False

-- | A minimum entry in O(1) worst-case time, or 'Nothing' when the heap is empty.
--
-- Ties are resolved in favour of the entry the representation currently holds at the root, which
-- for a chain of insertions is the most recently inserted one.
minimum :: PersistentMonotoneActionHeap element priority action
        -> Maybe (MonotoneHeapEntry element priority)
minimum (PersistentMonotoneActionHeap policy _ root) = fmap (entryOf policy) root

-- | Inserts an entry in O(1) worst-case time.
--
-- The new entry joins untransformed: actions applied by an earlier 'transformAll' never reach it.
-- A tie against the current root is resolved in favour of the new entry.
insert :: element
       -> priority
       -> PersistentMonotoneActionHeap element priority action
       -> PersistentMonotoneActionHeap element priority action
insert element priority (PersistentMonotoneActionHeap policy total root) =
  case root of
    Nothing -> PersistentMonotoneActionHeap policy 1 (Just singleton)
    Just current ->
      PersistentMonotoneActionHeap policy (checkedAdd total 1) (Just (updated current))
  where
    singleton = Tree 0 element priority ForestNil (actionIdentity policy)
    updated current
      | lessOrEqual policy singleton current =
          Tree 0 element priority (cons policy current ForestNil) (actionIdentity policy)
      | otherwise =
          -- Exposing the old root first is what stops its pending action from leaking onto a child
          -- that did not exist when that action was applied.
          let exposed = expose policy current
          in Tree
               0
               (treeElement exposed)
               (treePriority exposed)
               (skewInsert policy singleton (treeChildren exposed))
               (actionIdentity policy)

-- | Applies one action to every priority present in this version in O(1) worst-case time,
-- allocating O(1) new structure.
--
-- Later insertions and later melds are not retroactively transformed.  An empty heap or an
-- identity action returns the receiver itself, so the result shares its root.
transformAll :: action
             -> PersistentMonotoneActionHeap element priority action
             -> PersistentMonotoneActionHeap element priority action
transformAll action heap@(PersistentMonotoneActionHeap policy total root) =
  case root of
    Nothing -> heap
    Just current
      | actionIsIdentity policy action -> heap
      | otherwise ->
          PersistentMonotoneActionHeap policy total (Just (tagTree policy action current))

-- | Melds two heaps in O(1) worst-case time.
--
-- Each operand keeps its own action history: neither heap's pending actions reach the other's
-- entries, even for actions that have no inverse.  Melding a heap with itself duplicates every
-- logical occurrence.
--
-- Unlike the managed ports, which reject operands whose retained comparer and policy objects
-- differ, a record of functions has no identity to compare, so this cannot be checked.  Melding
-- heaps built with semantically different policies is a caller error: melding two nonempty heaps
-- silently adopts the left operand's policy, and the right operand's entries are then ordered and
-- transformed by rules they were not built under.  When either operand is empty the other is
-- returned verbatim, so that operand's own policy survives — which makes the surviving policy
-- depend on emptiness, one more reason the operands must agree.
meld :: PersistentMonotoneActionHeap element priority action
     -> PersistentMonotoneActionHeap element priority action
     -> PersistentMonotoneActionHeap element priority action
meld left@(PersistentMonotoneActionHeap policy leftCount leftRoot)
     right@(PersistentMonotoneActionHeap _ rightCount rightRoot) =
  case (leftRoot, rightRoot) of
    (Nothing, _) -> right
    (_, Nothing) -> left
    (Just leftTree, Just rightTree) ->
      let leftWins = lessOrEqual policy leftTree rightTree
          winner = expose policy (if leftWins then leftTree else rightTree)
          loser = if leftWins then rightTree else leftTree
          root = Tree
            0
            (treeElement winner)
            (treePriority winner)
            (skewInsert policy loser (treeChildren winner))
            (actionIdentity policy)
      in PersistentMonotoneActionHeap policy (checkedAdd leftCount rightCount) (Just root)

-- | Removes a minimum entry in O(log n) worst-case time, or 'Nothing' when the heap is empty.
minView :: PersistentMonotoneActionHeap element priority action
        -> Maybe ( MonotoneHeapEntry element priority
                 , PersistentMonotoneActionHeap element priority action )
minView (PersistentMonotoneActionHeap policy total root) =
  case root of
    Nothing -> Nothing
    Just current ->
      let entry = entryOf policy current
          exposed = expose policy current
      in case treeChildren exposed of
           ForestNil
             | total == 1 -> Just (entry, PersistentMonotoneActionHeap policy 0 Nothing)
             | otherwise ->
                 error "Durable7.FingerTree.PersistentMonotoneActionHeap: invalid singleton count"
           children ->
             let (taggedMinimum, remainder) = getMinimum policy children
                 smallest = expose policy taggedMinimum
                 (zeros, trees, embedded) =
                   splitForest policy (treeRank smallest) ForestNil ForestNil
                     (treeChildren smallest)
                 merged = skewMeld policy (skewMeld policy trees remainder) embedded
                 restored = List.foldl'
                   (\forest zero -> skewInsert policy zero forest)
                   merged
                   (reverse (forestToList policy zeros))
                 promoted = Tree
                   0
                   (treeElement smallest)
                   (treePriority smallest)
                   restored
                   (actionIdentity policy)
             in Just
                  ( entry
                  , PersistentMonotoneActionHeap policy (checkedSubtract total 1) (Just promoted) )

-- | The heap without a minimum entry, in O(log n) worst-case time.  An empty heap is returned
-- unchanged, exactly as "Durable7.FingerTree.BrodalOkasakiHeap" does.
deleteMinimum :: PersistentMonotoneActionHeap element priority action
              -> PersistentMonotoneActionHeap element priority action
deleteMinimum heap = maybe heap snd (minView heap)

-- | Every entry in Theta(n), in a deliberately unspecified structural order.
toList :: PersistentMonotoneActionHeap element priority action
       -> [MonotoneHeapEntry element priority]
toList (PersistentMonotoneActionHeap policy _ root) = case root of
  Nothing -> []
  Just current -> walk [current]
  where
    walk [] = []
    walk (tagged : pending) =
      let tree = expose policy tagged
      in MonotoneHeapEntry (treeElement tree) (treePriority tree)
           : walk (forestToList policy (treeChildren tree) ++ pending)

-- | Independently audits the representation in Theta(n) and returns its shape measurements, or a
-- diagnostic naming the first invariant that failed.
--
-- The audit checks the rank-zero global root, the fused primitive-child/embedded-forest encoding of
-- every tree, the skew-forest rank rules (nondecreasing, with only the first two ranks allowed to
-- be equal), logical heap order through every composed pending action, and reconciliation of the
-- cached count with the tree graph.  It calls the retained policy, so it carries no
-- production-operation complexity claim beyond being linear in the number of entries.
validateStructure :: PersistentMonotoneActionHeap element priority action
                  -> Either String MonotoneActionHeapStatistics
validateStructure (PersistentMonotoneActionHeap _ total Nothing)
  | total /= 0 = Left "an empty action heap has a nonzero count"
  | otherwise = Right (MonotoneActionHeapStatistics 0 0 0 0 0)
validateStructure (PersistentMonotoneActionHeap policy total (Just root))
  | treeRank root /= 0 = Left "the global heap root must have rank zero"
  | otherwise = do
      rootForestLength <- validateSkewForest policy (treeChildren (expose policy root))
      audit <- validateTrees policy [(root, 1)] (Audit 0 0 0 0)
      if auditCount audit /= total
        then Left "the action heap's logical count is invalid"
        else Right (MonotoneActionHeapStatistics
          total
          rootForestLength
          (auditMaximumRank audit)
          (auditMaximumDepth audit)
          (auditTaggedComponents audit))

-- | Whether two versions retain the very same root node, in O(1).
--
-- This is the test-facing analogue of comparing retained root object identity in the managed
-- ports: it is what proves a no-op transform or meld reused its source instead of rebuilding it.
-- It forces only the two roots.
sharesRootWith :: PersistentMonotoneActionHeap element priority action
               -> PersistentMonotoneActionHeap element priority action
               -> IO Bool
sharesRootWith (PersistentMonotoneActionHeap _ _ Nothing)
               (PersistentMonotoneActionHeap _ _ Nothing) = pure True
sharesRootWith (PersistentMonotoneActionHeap _ _ (Just left))
               (PersistentMonotoneActionHeap _ _ (Just right)) = do
  leftValue <- evaluate left
  rightValue <- evaluate right
  leftName <- makeStableName leftValue
  rightName <- makeStableName rightValue
  pure (leftName `eqStableName` rightName)
sharesRootWith _ _ = pure False

-- Running totals threaded through the structural audit.
data Audit = Audit
  { auditCount :: !Int
  , auditMaximumRank :: !Int
  , auditMaximumDepth :: !Int
  , auditTaggedComponents :: !Int
  }

validateTrees :: MonotoneActionPolicy priority action
              -> [(Tree element priority action, Int)]
              -> Audit
              -> Either String Audit
validateTrees _ [] audit = Right audit
validateTrees policy ((tagged, depth) : pending) audit
  | treeRank tagged < 0 = Left "an action-heap tree has a negative rank"
  | otherwise = do
      let observed
            | actionIsIdentity policy (treeAction tagged) = auditTaggedComponents audit
            | otherwise = auditTaggedComponents audit + 1
          tree = expose policy tagged
      embedded <- validateFusedChildren policy tree
      _ <- validateSkewForest policy embedded
      (children, taggedAfter) <- validateChildren policy tree (treeChildren tree) observed
      validateTrees
        policy
        ([(child, checkedAdd depth 1) | child <- children] ++ pending)
        (Audit
          (checkedAdd (auditCount audit) 1)
          (max (auditMaximumRank audit) (treeRank tree))
          (max (auditMaximumDepth audit) depth)
          taggedAfter)

validateChildren :: MonotoneActionPolicy priority action
                 -> Tree element priority action
                 -> Forest element priority action
                 -> Int
                 -> Either String ([Tree element priority action], Int)
validateChildren _ _ ForestNil observed = Right ([], observed)
validateChildren policy parent forest@(ForestCons action _ _) observed =
  let nextObserved
        | actionIsIdentity policy action = observed
        | otherwise = observed + 1
      child = forestHead policy forest
  in if not (lessOrEqual policy parent child)
       then Left "an action-heap child outranks its parent"
       else do
         (rest, finalObserved) <-
           validateChildren policy parent (forestTail policy forest) nextObserved
         Right (child : rest, finalObserved)

-- Decodes the primitive children of an exposed rank-r tree from the fused child list and returns
-- the embedded skew-binomial forest that follows them.
validateFusedChildren :: MonotoneActionPolicy priority action
                      -> Tree element priority action
                      -> Either String (Forest element priority action)
validateFusedChildren policy tree = go (treeRank tree) (treeChildren tree)
  where
    go rank forest
      | rank <= 0 = Right forest
      | otherwise = case forest of
          ForestNil -> Left "a ranked action-heap tree is missing children"
          ForestCons _ _ _ ->
            let firstTree = forestHead policy forest
                tailForest = forestTail policy forest
            in if rank == 1
                 then rankOne firstTree tailForest
                 else case tailForest of
                   ForestNil -> Left "a ranked action-heap tree has incomplete children"
                   ForestCons _ _ _ -> ranked rank firstTree tailForest
    rankOne firstTree tailForest
      | treeRank firstTree /= 0 = Left "a rank-one tree must begin with rank zero"
      | otherwise = case tailForest of
          ForestNil -> Right ForestNil
          ForestCons _ _ _
            | treeRank (forestHead policy tailForest) == 0 ->
                Right (forestTail policy tailForest)
            | otherwise -> Right tailForest
    ranked rank firstTree tailForest
      | treeRank firstTree == treeRank secondTree =
          if treeRank firstTree /= rank - 1
            then Left "a skew-linked tree has invalid child ranks"
            else Right (forestTail policy tailForest)
      | treeRank firstTree == 0 =
          if treeRank secondTree /= rank - 1
            then Left "a skew-linked tree has an invalid ranked child"
            else go (rank - 1) (forestTail policy tailForest)
      | treeRank firstTree /= rank - 1 = Left "a linked tree has an invalid ranked child"
      | otherwise = go (rank - 1) tailForest
      where
        secondTree = forestHead policy tailForest

-- Checks that a skew-binomial forest has nondecreasing ranks and at most one leading equal pair.
-- Ranks are tag-invariant, so this reads raw heads while still walking the tag-composing spine.
validateSkewForest :: MonotoneActionPolicy priority action
                   -> Forest element priority action
                   -> Either String Int
validateSkewForest policy = go 0 (-1) False
  where
    go !lengthValue _ _ ForestNil = Right lengthValue
    go !lengthValue previousRank duplicate forest@(ForestCons _ rawHead _)
      | rank < 0 = Left "an action-heap forest contains a negative rank"
      | rank < previousRank = Left "action-heap forest ranks are not nondecreasing"
      | rank == previousRank && (lengthValue /= 1 || duplicate) =
          Left "only the first two forest ranks may be equal"
      | otherwise = go
          (lengthValue + 1)
          rank
          (duplicate || rank == previousRank)
          (forestTail policy forest)
      where
        rank = treeRank rawHead

entryOf :: MonotoneActionPolicy priority action
        -> Tree element priority action
        -> MonotoneHeapEntry element priority
entryOf policy tree =
  MonotoneHeapEntry (treeElement tree) (logicalPriority policy tree)

logicalPriority :: MonotoneActionPolicy priority action
                -> Tree element priority action
                -> priority
logicalPriority policy tree = actionApply policy (treeAction tree) (treePriority tree)

lessOrEqual :: MonotoneActionPolicy priority action
            -> Tree element priority action
            -> Tree element priority action
            -> Bool
lessOrEqual policy left right =
  priorityCompare policy (logicalPriority policy left) (logicalPriority policy right) /= GT

-- Pushes a newer action onto one tree, composing it with that tree's pending action.
tagTree :: MonotoneActionPolicy priority action
        -> action
        -> Tree element priority action
        -> Tree element priority action
tagTree policy outer tree
  | actionIsIdentity policy outer = tree
  | otherwise = tree { treeAction = actionCompose policy outer (treeAction tree) }

-- Pushes a newer action onto one forest suffix, composing it with that suffix's pending action.
tagForest :: MonotoneActionPolicy priority action
          -> action
          -> Forest element priority action
          -> Forest element priority action
tagForest _ _ ForestNil = ForestNil
tagForest policy outer forest@(ForestCons action rawHead rawTail)
  | actionIsIdentity policy outer = forest
  | otherwise = ForestCons (actionCompose policy outer action) rawHead rawTail

-- Materializes one tree's pending action into its own priority and its child forest.
expose :: MonotoneActionPolicy priority action
       -> Tree element priority action
       -> Tree element priority action
expose policy tree
  | actionIsIdentity policy action = tree
  | otherwise = Tree
      (treeRank tree)
      (treeElement tree)
      (actionApply policy action (treePriority tree))
      (tagForest policy action (treeChildren tree))
      (actionIdentity policy)
  where
    action = treeAction tree

-- Builds an identity-tagged spine cell, so an attached tree keeps only its own action history.
cons :: MonotoneActionPolicy priority action
     -> Tree element priority action
     -> Forest element priority action
     -> Forest element priority action
cons policy headTree tailForest = ForestCons (actionIdentity policy) headTree tailForest

forestHead :: MonotoneActionPolicy priority action
           -> Forest element priority action
           -> Tree element priority action
forestHead _ ForestNil =
  error "Durable7.FingerTree.PersistentMonotoneActionHeap: head of an empty forest"
forestHead policy (ForestCons action rawHead _) = tagTree policy action rawHead

forestTail :: MonotoneActionPolicy priority action
           -> Forest element priority action
           -> Forest element priority action
forestTail _ ForestNil =
  error "Durable7.FingerTree.PersistentMonotoneActionHeap: tail of an empty forest"
forestTail policy (ForestCons action _ rawTail) = tagForest policy action rawTail

forestToList :: MonotoneActionPolicy priority action
             -> Forest element priority action
             -> [Tree element priority action]
forestToList _ ForestNil = []
forestToList policy forest@(ForestCons _ _ _) =
  forestHead policy forest : forestToList policy (forestTail policy forest)

-- Ranks are tag-invariant, so the raw tail's rank decides the link before any tagged tail cell is
-- materialized; only the linking branch pays for the tagged tail.
skewInsert :: MonotoneActionPolicy priority action
           -> Tree element priority action
           -> Forest element priority action
           -> Forest element priority action
skewInsert policy tree forest = case forest of
  ForestCons action rawHead rawTail@(ForestCons _ tailRawHead _)
    | treeRank rawHead == treeRank tailRawHead ->
        let tagged = tagForest policy action rawTail
        in cons
             policy
             (skewLink policy tree (tagTree policy action rawHead) (forestHead policy tagged))
             (forestTail policy tagged)
  _ -> cons policy tree forest

-- Only the logical winner is exposed; the losers attach through identity-tagged spine cells so
-- they keep their own action histories.
skewLink :: MonotoneActionPolicy priority action
         -> Tree element priority action
         -> Tree element priority action
         -> Tree element priority action
         -> Tree element priority action
skewLink policy zero firstTree secondTree
  | treeRank firstTree /= treeRank secondTree =
      error "Durable7.FingerTree.PersistentMonotoneActionHeap: invalid skew-link ranks"
  | lessOrEqual policy firstTree zero && lessOrEqual policy firstTree secondTree =
      let winner = expose policy firstTree
      in Tree
           (treeRank firstTree + 1)
           (treeElement winner)
           (treePriority winner)
           (cons policy zero (cons policy secondTree (treeChildren winner)))
           (actionIdentity policy)
  | lessOrEqual policy secondTree zero && lessOrEqual policy secondTree firstTree =
      let winner = expose policy secondTree
      in Tree
           (treeRank secondTree + 1)
           (treeElement winner)
           (treePriority winner)
           (cons policy zero (cons policy firstTree (treeChildren winner)))
           (actionIdentity policy)
  | otherwise =
      let winner = expose policy zero
      in Tree
           (treeRank firstTree + 1)
           (treeElement winner)
           (treePriority winner)
           (cons policy firstTree (cons policy secondTree (treeChildren winner)))
           (actionIdentity policy)

link :: MonotoneActionPolicy priority action
     -> Tree element priority action
     -> Tree element priority action
     -> Tree element priority action
link policy firstTree secondTree
  | treeRank firstTree /= treeRank secondTree =
      error "Durable7.FingerTree.PersistentMonotoneActionHeap: invalid binomial-link ranks"
  | otherwise =
      let firstWins = lessOrEqual policy firstTree secondTree
          winner = expose policy (if firstWins then firstTree else secondTree)
          loser = if firstWins then secondTree else firstTree
      in Tree
           (treeRank winner + 1)
           (treeElement winner)
           (treePriority winner)
           (cons policy loser (treeChildren winner))
           (actionIdentity policy)

-- Lifts a minimum-priority tree out of a forest, favouring the earliest spine occurrence on ties.
getMinimum :: MonotoneActionPolicy priority action
           -> Forest element priority action
           -> (Tree element priority action, Forest element priority action)
getMinimum _ ForestNil =
  error "Durable7.FingerTree.PersistentMonotoneActionHeap: minimum of an empty forest"
getMinimum policy forest@(ForestCons _ _ _) =
  let headTree = forestHead policy forest
      tailForest = forestTail policy forest
  in case tailForest of
       ForestNil -> (headTree, ForestNil)
       ForestCons _ _ _ ->
         let (smallest, remainder) = getMinimum policy tailForest
         in if lessOrEqual policy headTree smallest
              then (headTree, tailForest)
              else (smallest, cons policy headTree remainder)

-- Decomposes a rank-r tree's fused child list into the rank-zero prefix trees, the primitive
-- children, and the embedded skew-binomial forest.
splitForest :: MonotoneActionPolicy priority action
            -> Int
            -> Forest element priority action
            -> Forest element priority action
            -> Forest element priority action
            -> ( Forest element priority action
               , Forest element priority action
               , Forest element priority action )
splitForest policy rank zeros trees forest
  | rank == 0 = (zeros, trees, forest)
  | otherwise = case forest of
      ForestNil ->
        error "Durable7.FingerTree.PersistentMonotoneActionHeap: invalid skew-binomial forest"
      ForestCons _ _ _ ->
        let firstTree = forestHead policy forest
            tailForest = forestTail policy forest
        in case tailForest of
             ForestNil
               | rank == 1 -> (zeros, cons policy firstTree trees, ForestNil)
               | otherwise ->
                   error
                     "Durable7.FingerTree.PersistentMonotoneActionHeap: invalid skew-binomial forest"
             ForestCons _ _ _ ->
               let secondTree = forestHead policy tailForest
                   restForest = forestTail policy tailForest
               in if rank == 1
                    then
                      -- The rank-zero ambiguity is resolved in favour of the structural prefix.
                      if treeRank secondTree == 0
                        then
                          ( cons policy firstTree zeros
                          , cons policy secondTree trees
                          , restForest )
                        else
                          ( zeros
                          , cons policy firstTree trees
                          , cons policy secondTree restForest )
                    else if treeRank firstTree == treeRank secondTree
                      then
                        ( zeros
                        , cons policy firstTree (cons policy secondTree trees)
                        , restForest )
                      else if treeRank firstTree == 0
                        then splitForest
                               policy
                               (rank - 1)
                               (cons policy firstTree zeros)
                               (cons policy secondTree trees)
                               restForest
                        else splitForest
                               policy
                               (rank - 1)
                               zeros
                               (cons policy firstTree trees)
                               (cons policy secondTree restForest)

skewMeld :: MonotoneActionPolicy priority action
         -> Forest element priority action
         -> Forest element priority action
         -> Forest element priority action
skewMeld policy left right =
  unionUnique policy (uniquify policy left) (uniquify policy right)

uniquify :: MonotoneActionPolicy priority action
         -> Forest element priority action
         -> Forest element priority action
uniquify _ ForestNil = ForestNil
uniquify policy forest@(ForestCons _ _ _) =
  insertRanked policy (forestHead policy forest) (uniquify policy (forestTail policy forest))

insertRanked :: MonotoneActionPolicy priority action
             -> Tree element priority action
             -> Forest element priority action
             -> Forest element priority action
insertRanked policy tree ForestNil = cons policy tree ForestNil
insertRanked policy tree forest@(ForestCons _ _ _) =
  let headTree = forestHead policy forest
  in if treeRank tree < treeRank headTree
       then cons policy tree forest
       else insertRanked policy (link policy tree headTree) (forestTail policy forest)

unionUnique :: MonotoneActionPolicy priority action
            -> Forest element priority action
            -> Forest element priority action
            -> Forest element priority action
unionUnique _ ForestNil right = right
unionUnique _ left ForestNil = left
unionUnique policy left@(ForestCons _ _ _) right@(ForestCons _ _ _) =
  let leftHead = forestHead policy left
      rightHead = forestHead policy right
  in case compare (treeRank leftHead) (treeRank rightHead) of
       LT -> cons policy leftHead (unionUnique policy (forestTail policy left) right)
       GT -> cons policy rightHead (unionUnique policy left (forestTail policy right))
       EQ -> insertRanked
               policy
               (link policy leftHead rightHead)
               (unionUnique policy (forestTail policy left) (forestTail policy right))

checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | right > 0 && left > maxBound - right =
      error "Durable7.FingerTree.PersistentMonotoneActionHeap: count overflow"
  | otherwise = left + right

checkedSubtract :: Int -> Int -> Int
checkedSubtract left right
  | left < right =
      error "Durable7.FingerTree.PersistentMonotoneActionHeap: count underflow"
  | otherwise = left - right
