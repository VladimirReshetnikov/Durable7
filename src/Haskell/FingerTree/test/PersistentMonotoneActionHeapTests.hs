{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE NumericUnderscores #-}

-- | Tests for the action-tagged persistent Brodal-Okasaki heap: the clamp algebra's laws and
-- collapse cases, the temporal semantics of a whole-heap transform, tie-breaking, structural
-- validation of deeply tagged shapes, and a randomized retained-branch model check.
module PersistentMonotoneActionHeapTests (run) where

import Control.Exception (ErrorCall, evaluate, try)
import Control.Monad (forM, forM_)
import Data.Bits (shiftL, shiftR, xor, (.|.))
import Data.IORef (IORef, modifyIORef', newIORef, readIORef, writeIORef)
import qualified Data.List as List
import Data.Word (Word64)
import System.IO.Unsafe (unsafePerformIO)

import qualified Durable7.FingerTree.PersistentMonotoneActionHeap as Heap

type Clamp = Heap.OrderClamp Int

type ClampPolicy = Heap.MonotoneActionPolicy Int Clamp

type IntHeap = Heap.PersistentMonotoneActionHeap Int Int Clamp

type StringHeap = Heap.PersistentMonotoneActionHeap String Int Clamp

-- | Runs this module's test cases.
run :: IO ()
run = do
  testClampAlgebra
  testClampRepresentatives
  testCompositionDirection
  testTemporalInsert
  testMeldIndependence
  testPersistentBranches
  testTieBreaking
  testTransformCostAndSharing
  testValidateDeepTaggedShapes
  testCollapsedPriorities
  testFailureAtomicity
  testEmptyAndBoundaries
  testRandomizedModel
  testDeepDeleteAndMeldShapes

clampPolicy :: ClampPolicy
clampPolicy = Heap.naturalClampPolicy

applyClamp :: Clamp -> Int -> Int
applyClamp = Heap.actionApply clampPolicy

composeClamp :: Clamp -> Clamp -> Clamp
composeClamp = Heap.actionCompose clampPolicy

between :: Int -> Int -> Clamp
between lowerBound upperBound =
  case Heap.clampBetween compare lowerBound upperBound of
    Just action -> action
    Nothing -> error ("PersistentMonotoneActionHeapTests: unordered clamp bounds "
      ++ show (lowerBound, upperBound))

emptyIntHeap :: IntHeap
emptyIntHeap = Heap.emptyWith clampPolicy

emptyStringHeap :: StringHeap
emptyStringHeap = Heap.emptyWith clampPolicy

testClampAlgebra :: IO ()
testClampAlgebra = do
  let identity = Heap.clampIdentity :: Clamp
      actions =
        [ identity
        , Heap.clampAtLeast (-3)
        , Heap.clampAtLeast 8
        , Heap.clampAtMost (-4)
        , Heap.clampAtMost 11
        , between (-6) 9
        , between 5 5
        , Heap.clampConstant (-9)
        ]
      samples = [-20 .. 20] ++ [minBound, maxBound] :: [Int]
  assertBool "identity clamp recognized" (Heap.actionIsIdentity clampPolicy identity)
  assertBool "floor clamp is not the identity"
    (not (Heap.actionIsIdentity clampPolicy (Heap.clampAtLeast 0)))
  forM_ actions $ \action -> do
    assertClampEquivalent "left unit" action (composeClamp identity action) samples
    assertClampEquivalent "right unit" action (composeClamp action identity) samples

  -- @compose outer inner@ is @outer (inner p)@, so the newer action is the outer one.  The clamp
  -- algebra pins the direction: a cap applied after a floor collapses to the cap's boundary.
  let floorThenCap = composeClamp (Heap.clampAtMost 5) (Heap.clampAtLeast 10)
      capThenFloor = composeClamp (Heap.clampAtLeast 10) (Heap.clampAtMost 5)
  assertEqual "floor then cap collapses" (Heap.clampConstant 5) floorThenCap
  assertEqual "cap then floor collapses" (Heap.clampConstant 10) capThenFloor
  assertBool "floor then cap is constant" (Heap.clampIsConstant floorThenCap)
  assertBool "cap then floor is constant" (Heap.clampIsConstant capThenFloor)
  assertEqual "floor then cap constant" (Just 5) (Heap.clampConstantValue floorThenCap)
  assertEqual "cap then floor constant" (Just 10) (Heap.clampConstantValue capThenFloor)
  forM_ samples $ \value -> do
    assertEqual "floor then cap application" 5 (applyClamp floorThenCap value)
    assertEqual "cap then floor application" 10 (applyClamp capThenFloor value)

  forM_ actions $ \inner ->
    forM_ actions $ \outer -> do
      let composed = composeClamp outer inner
      forM_ samples $ \value ->
        assertEqual "composition matches sequential application"
          (applyClamp outer (applyClamp inner value))
          (applyClamp composed value)

  forM_ actions $ \oldest ->
    forM_ actions $ \middle ->
      forM_ actions $ \newest ->
        assertClampEquivalent "composition associativity"
          (composeClamp newest (composeClamp middle oldest))
          (composeClamp (composeClamp newest middle) oldest)
          samples

  forM_ actions $ \action ->
    forM_ [(lower, upper) | lower <- [-12 .. 12 :: Int], upper <- [lower .. 12]] $ \(lower, upper) ->
      assertBool "clamp actions are monotone for the retained order"
        (applyClamp action lower <= applyClamp action upper)

  -- The named collapse cases.
  assertEqual "identity short-circuits as the outer action"
    (Heap.clampAtLeast 4) (composeClamp identity (Heap.clampAtLeast 4))
  assertEqual "identity short-circuits as the inner action"
    (Heap.clampAtMost 4) (composeClamp (Heap.clampAtMost 4) identity)
  assertEqual "an outer constant wins outright"
    (Heap.clampConstant 3) (composeClamp (Heap.clampConstant 3) (Heap.clampAtLeast 100))
  assertEqual "an inner constant is pushed through the outer action"
    (Heap.clampConstant 5) (composeClamp (Heap.clampAtMost 5) (Heap.clampConstant 100))
  assertEqual "disjoint bounds collapse to the outer lower boundary"
    (Heap.clampConstant 10) (composeClamp (Heap.clampAtLeast 10) (Heap.clampAtMost 5))
  assertEqual "disjoint bounds collapse to the outer upper boundary"
    (Heap.clampConstant 5) (composeClamp (Heap.clampAtMost 5) (Heap.clampAtLeast 10))
  assertEqual "overlapping bounds intersect"
    (between 2 6) (composeClamp (between 2 8) (between (-1) 6))
  assertEqual "a constant applies before either bound"
    (Just 7) (Heap.clampConstantValue (composeClamp (Heap.clampConstant 7) (between 0 3)))

data Labeled = Labeled Int String
  deriving (Eq, Show)

labeledCompare :: Labeled -> Labeled -> Ordering
labeledCompare (Labeled left _) (Labeled right _) = compare left right

-- A comparison coarser than value identity is the Haskell stand-in for the managed suite's
-- reference-identity assertions: the label names which exact representative survived.
testClampRepresentatives :: IO ()
testClampRepresentatives = do
  let policy = Heap.orderClampPolicy labeledCompare
      composeLabeled = Heap.actionCompose policy
      applyLabeled = Heap.actionApply policy
      olderLower = Labeled 10 "older lower"
      newerUpper = Labeled 5 "newer upper"
      floorThenCap = composeLabeled (Heap.clampAtMost newerUpper) (Heap.clampAtLeast olderLower)
  assertBool "disjoint bounds collapse" (Heap.clampIsConstant floorThenCap)
  assertEqual "collapse keeps the newer boundary"
    (Just newerUpper) (Heap.clampConstantValue floorThenCap)
  forM_ [Labeled 5 "comparison-equal input", Labeled (-100) "below", Labeled 100 "above"] $ \probe ->
    assertEqual "a constant returns its own representative"
      newerUpper (applyLabeled floorThenCap probe)

  let olderUpper = Labeled 5 "older upper"
      newerLower = Labeled 10 "newer lower"
      capThenFloor = composeLabeled (Heap.clampAtLeast newerLower) (Heap.clampAtMost olderUpper)
  assertBool "reverse disjoint bounds collapse" (Heap.clampIsConstant capThenFloor)
  assertEqual "reverse collapse keeps the newer boundary"
    (Just newerLower) (Heap.clampConstantValue capThenFloor)
  forM_ [Labeled 10 "comparison-equal input", Labeled (-100) "below", Labeled 100 "above"] $ \probe ->
    assertEqual "the reverse constant returns its own representative"
      newerLower (applyLabeled capThenFloor probe)

  -- Equal order classes overlap rather than collapse: each side keeps its own exact
  -- representative and an input inside the shared class survives unchanged.
  let olderCap = Labeled 7 "older cap"
      newerFloor = Labeled 7 "newer floor"
      inside = Labeled 7 "inside representative"
      touching = composeLabeled (Heap.clampAtLeast newerFloor) (Heap.clampAtMost olderCap)
  assertBool "touching bounds do not collapse" (not (Heap.clampIsConstant touching))
  assertEqual "touching lower bound" (Just newerFloor) (Heap.clampLowerBound touching)
  assertEqual "touching upper bound" (Just olderCap) (Heap.clampUpperBound touching)
  assertEqual "below the touching class" newerFloor (applyLabeled touching (Labeled 6 "below"))
  assertEqual "inside the touching class" inside (applyLabeled touching inside)
  assertEqual "above the touching class" olderCap (applyLabeled touching (Labeled 8 "above"))

  -- On equal boundaries an intersection keeps the inner, older representative.
  let olderFloor = Labeled 3 "older floor"
      competingFloor = Labeled 3 "newer floor"
      olderCeiling = Labeled 9 "older ceiling"
      competingCeiling = Labeled 9 "newer ceiling"
      lowerTie = composeLabeled (Heap.clampAtLeast competingFloor) (Heap.clampAtLeast olderFloor)
      upperTie = composeLabeled (Heap.clampAtMost competingCeiling) (Heap.clampAtMost olderCeiling)
  assertEqual "an equal lower boundary keeps the older representative"
    (Just olderFloor) (Heap.clampLowerBound lowerTie)
  assertEqual "an equal upper boundary keeps the older representative"
    (Just olderCeiling) (Heap.clampUpperBound upperTie)

-- The composition direction has to hold at every pushdown site, not just at the root tag, so this
-- drives exposes and forest tagging through delete-min before checking again.
testCompositionDirection :: IO ()
testCompositionDirection = do
  let priorities = [index * 3 - 20 | index <- [0 .. 63 :: Int]]
      base = Heap.fromListWith clampPolicy (zip [0 ..] priorities) :: IntHeap
      floorThenCap = Heap.transformAll (Heap.clampAtMost 5) (Heap.transformAll (Heap.clampAtLeast 10) base)
      capThenFloor = Heap.transformAll (Heap.clampAtLeast 10) (Heap.transformAll (Heap.clampAtMost 5) base)
  assertBool "a cap after a floor collapses to the cap"
    (all ((== 5) . Heap.entryPriority) (Heap.toList floorThenCap))
  assertBool "a floor after a cap collapses to the floor"
    (all ((== 10) . Heap.entryPriority) (Heap.toList capThenFloor))
  validate "floor then cap" floorThenCap
  validate "cap then floor" capThenFloor

  let floored = Heap.transformAll (Heap.clampAtLeast 8) base
      drilled = Heap.transformAll (Heap.clampAtMost 100)
        (List.foldl' (\heap _ -> Heap.deleteMinimum heap) floored [1 .. 20 :: Int])
      expected = List.sort (map (min 100) (drop 20 (List.sort (map (max 8) priorities))))
  validate "drilled" drilled
  assertEqual "composition direction survives every pushdown"
    expected (List.sort (map Heap.entryPriority (Heap.toList drilled)))

testTemporalInsert :: IO ()
testTemporalInsert = do
  let source = Heap.insert "high" 20 (Heap.insert "middle" 5 (Heap.insert "low" (-10) emptyStringHeap))
      transformed = Heap.transformAll (Heap.clampAtLeast 10) source
      inserted = Heap.insert "future" 0 transformed
  assertEntries "temporal source" [("high", 20), ("low", -10), ("middle", 5)] source
  assertEntries "temporal transformed" [("high", 20), ("low", 10), ("middle", 10)] transformed
  assertEntries "temporal inserted"
    [("future", 0), ("high", 20), ("low", 10), ("middle", 10)] inserted
  assertEqual "an untransformed insertion becomes the minimum"
    (Just ("future", 0)) (fmap entryPair (Heap.minimum inserted))

  let deleted = Heap.deleteMinimum inserted
  assertEntries "temporal deleted" [("high", 20), ("low", 10), ("middle", 10)] deleted
  assertEntries "temporal source retained after deletion"
    [("high", 20), ("low", 10), ("middle", 10)] transformed

  -- The opposite root-selection case: the transformed root wins, so it is exposed before the
  -- future child is skew-inserted.  That is what stops the old cap from leaking onto the child.
  let bounded = Heap.transformAll (between 0 10)
        (Heap.insert "old-high" 100 (Heap.insert "old-low" (-10) emptyStringHeap))
      laterHigh = Heap.insert "future-high" 50 bounded
  assertEntries "bounded" [("old-low", 0), ("old-high", 10)] bounded
  assertEntries "later high" [("old-low", 0), ("old-high", 10), ("future-high", 50)] laterHigh

  let transformedAgain = Heap.transformAll (Heap.clampAtLeast 5) laterHigh
      newest = Heap.insert "newest-low" (-20) transformedAgain
  assertEntries "transformed again"
    [("old-low", 5), ("old-high", 10), ("future-high", 50)] transformedAgain
  assertEntries "newest"
    [("old-low", 5), ("old-high", 10), ("future-high", 50), ("newest-low", -20)] newest

testMeldIndependence :: IO ()
testMeldIndependence = do
  let rawLeft = Heap.insert "L1" 3 (Heap.insert "L0" (-20) emptyStringHeap)
      rawRight = Heap.insert "R1" 30 (Heap.insert "R0" (-4) emptyStringHeap)
      left = Heap.transformAll (Heap.clampAtLeast 10) rawLeft
      right = Heap.transformAll (Heap.clampAtMost (-10)) rawRight
      melded = Heap.meld left right
  assertEntries "meld raw left" [("L0", -20), ("L1", 3)] rawLeft
  assertEntries "meld raw right" [("R0", -4), ("R1", 30)] rawRight
  assertEntries "meld left" [("L0", 10), ("L1", 10)] left
  assertEntries "meld right" [("R0", -10), ("R1", -10)] right
  assertEntries "melded" [("L0", 10), ("L1", 10), ("R0", -10), ("R1", -10)] melded
  assertEqual "melded minimum" (Just (-10)) (fmap Heap.entryPriority (Heap.minimum melded))

  let sizeCount = 257 :: Int
      leftAction = between (-30) 20
      rightAction = between 40 70
      leftPriorities = [((index * 37) `mod` 401) - 200 | index <- [0 .. sizeCount - 1]]
      rightPriorities = [((index * 53) `mod` 401) - 200 | index <- [0 .. sizeCount - 1]]
      manyLeft = Heap.transformAll leftAction
        (Heap.fromListWith clampPolicy (zip [0 ..] leftPriorities) :: IntHeap)
      manyRight = Heap.transformAll rightAction
        (Heap.fromListWith clampPolicy (zip [sizeCount ..] rightPriorities) :: IntHeap)
      manyMelded = Heap.meld manyLeft manyRight
      expectedPriorities = List.sort
        (map (applyClamp leftAction) leftPriorities ++ map (applyClamp rightAction) rightPriorities)
  validate "many left" manyLeft
  validate "many right" manyRight
  validate "many melded" manyMelded
  drained <- drain "many melded" manyMelded
  assertEqual "melded drain contents" expectedPriorities (map snd drained)
  assertBool "melded drain is nondecreasing" (List.sort (map snd drained) == map snd drained)

testPersistentBranches :: IO ()
testPersistentBranches = do
  let original = Heap.insert "c" 9 (Heap.insert "b" 4 (Heap.insert "a" 1 emptyStringHeap))
      floored = Heap.transformAll (Heap.clampAtLeast 5) original
      withFuture = Heap.insert "future" 0 floored
      deleted = Heap.deleteMinimum withFuture
      capped = Heap.transformAll (Heap.clampAtMost 3) original
      melded = Heap.meld deleted capped
  Heap.sharesRootWith original original
    >>= assertBool "a retained version shares its own root"
  Heap.sharesRootWith original floored
    >>= (assertBool "a nonidentity transform replaces the root" . not)
  Heap.sharesRootWith original (Heap.transformAll Heap.clampIdentity original)
    >>= assertBool "an identity transform shares the root"
  assertEntries "branch original" [("a", 1), ("b", 4), ("c", 9)] original
  assertEntries "branch floored" [("a", 5), ("b", 5), ("c", 9)] floored
  assertEntries "branch with future"
    [("a", 5), ("b", 5), ("c", 9), ("future", 0)] withFuture
  assertEntries "branch deleted" [("a", 5), ("b", 5), ("c", 9)] deleted
  assertEntries "branch capped" [("a", 1), ("b", 3), ("c", 3)] capped
  assertEntries "branch melded"
    [("a", 1), ("a", 5), ("b", 3), ("b", 5), ("c", 3), ("c", 9)] melded

testTieBreaking :: IO ()
testTieBreaking = do
  -- An insertion that ties the current root takes the root, so the newest entry is reported.
  let tied = List.foldl' (\heap name -> Heap.insert name 5 heap) emptyStringHeap ["a", "b", "c"]
  assertEqual "an insert tie favours the new entry"
    (Just ("c", 5)) (fmap entryPair (Heap.minimum tied))
  drainedTies <- drain "insert ties" tied
  assertEqual "tied insertions drain newest first"
    [("c", 5), ("b", 5), ("a", 5)] drainedTies

  -- Lifting a minimum out of a spine favours the earliest occurrence.
  let spine = Heap.insert "c" 9 (Heap.insert "b" 9 (Heap.insert "a" 0 emptyStringHeap))
  assertEqual "spine minimum" (Just ("a", 0)) (fmap entryPair (Heap.minimum spine))
  drainedSpine <- drain "spine ties" spine
  assertEqual "tied spine entries drain in spine order"
    [("a", 0), ("c", 9), ("b", 9)] drainedSpine

  -- Melding a heap with itself duplicates every logical occurrence.
  let base = Heap.fromListWith clampPolicy [(1, 4), (2, 1), (3, 7)] :: IntHeap
      doubled = Heap.meld base base
  assertEntries "self meld" [(1, 4), (1, 4), (2, 1), (2, 1), (3, 7), (3, 7)] doubled
  assertEqual "self meld count" 6 (Heap.count doubled)
  assertEntries "self meld source" [(1, 4), (2, 1), (3, 7)] base

data CountingPolicy = CountingPolicy
  { countingIdentityTests :: IORef Int
  , countingCompositions :: IORef Int
  , countingApplications :: IORef Int
  , countingComparisons :: IORef Int
  }

{-# NOINLINE countCall #-}
countCall :: IORef Int -> a -> a
countCall counter value = unsafePerformIO (modifyIORef' counter (+ 1) >> pure value)

newCountingPolicy :: IO (CountingPolicy, ClampPolicy)
newCountingPolicy = do
  identityTests <- newIORef 0
  compositions <- newIORef 0
  applications <- newIORef 0
  comparisons <- newIORef 0
  let counters = CountingPolicy identityTests compositions applications comparisons
      counted = Heap.MonotoneActionPolicy
        { Heap.actionIdentity = Heap.actionIdentity clampPolicy
        , Heap.actionIsIdentity = \action ->
            countCall identityTests (Heap.actionIsIdentity clampPolicy action)
        , Heap.actionCompose = \outer inner ->
            countCall compositions (Heap.actionCompose clampPolicy outer inner)
        , Heap.actionApply = \action value ->
            countCall applications (Heap.actionApply clampPolicy action value)
        , Heap.priorityCompare = \left right ->
            countCall comparisons (Heap.priorityCompare clampPolicy left right)
        }
  pure (counters, counted)

countersOf :: CountingPolicy -> [IORef Int]
countersOf counters =
  [ countingIdentityTests counters
  , countingCompositions counters
  , countingApplications counters
  , countingComparisons counters
  ]

resetCounts :: CountingPolicy -> IO ()
resetCounts counters = forM_ (countersOf counters) (\counter -> writeIORef counter 0)

readCounts :: CountingPolicy -> IO (Int, Int, Int, Int)
readCounts counters = do
  identityTests <- readIORef (countingIdentityTests counters)
  compositions <- readIORef (countingCompositions counters)
  applications <- readIORef (countingApplications counters)
  comparisons <- readIORef (countingComparisons counters)
  pure (identityTests, compositions, applications, comparisons)

-- The whole-heap transform must cost a fixed number of policy calls and allocate one tree node,
-- whatever the heap holds.  Counting policy calls is the portable half of that claim; sharing the
-- receiver's root on a no-op is the other half.
testTransformCostAndSharing :: IO ()
testTransformCostAndSharing = do
  (counters, policy) <- newCountingPolicy
  let action = Heap.clampAtLeast 17
  observations <- forM [1, 1024, 8192 :: Int] $ \size -> do
    let heap = List.foldl' (\acc index -> Heap.insert index index acc)
          (Heap.emptyWith policy :: IntHeap) [0 .. size - 1]
    forceRoot heap
    resetCounts counters
    -- Reading the counters is only meaningful once the successor's root has been forced, so the
    -- root-identity comparison doubles as the demand that runs the transform's policy calls.
    let transformed = Heap.transformAll action heap
    shares <- Heap.sharesRootWith heap transformed
    counts <- readCounts counters
    let (identityTests, compositions, applications, comparisons) = counts
    assertBool ("transform replaces the root at " ++ show size) (not shares)
    assertEqual ("transform composes once at " ++ show size) 1 compositions
    assertEqual ("transform applies nothing at " ++ show size) 0 applications
    assertEqual ("transform compares nothing at " ++ show size) 0 comparisons
    assertBool ("transform tests identity at most twice at " ++ show size)
      (identityTests >= 1 && identityTests <= 2)
    assertEqual ("transform preserves the count at " ++ show size)
      (Heap.count heap) (Heap.count transformed)
    validate ("transformed " ++ show size) transformed
    pure counts
  case observations of
    [] -> failWith "no transform observations were collected"
    reference : _ -> assertBool "transform cost does not grow with the heap"
      (all (== reference) observations)

  let nonempty = Heap.insert 2 2 (Heap.insert 1 1 (Heap.emptyWith policy :: IntHeap))
  forceRoot nonempty
  resetCounts counters
  let unchanged = Heap.transformAll Heap.clampIdentity nonempty
  identityShares <- Heap.sharesRootWith nonempty unchanged
  (_, identityCompositions, identityApplications, identityComparisons) <- readCounts counters
  assertEqual "an identity transform composes nothing" 0 identityCompositions
  assertEqual "an identity transform applies nothing" 0 identityApplications
  assertEqual "an identity transform compares nothing" 0 identityComparisons
  assertBool "an identity transform shares the receiver's root" identityShares

  let emptyHeap = Heap.emptyWith policy :: IntHeap
  resetCounts counters
  let stillEmpty = Heap.transformAll action emptyHeap
  emptyIsEmpty <- evaluate (Heap.null stillEmpty)
  emptyCounts <- readCounts counters
  assertEqual "an empty transform makes no policy calls at all" (0, 0, 0, 0) emptyCounts
  assertBool "an empty transform stays empty" emptyIsEmpty

-- Forces the retained root.  Every structural field of a tree or spine cell except the payload is
-- strict, so forcing the root to weak head normal form forces the whole representation.
forceRoot :: Heap.PersistentMonotoneActionHeap element priority action -> IO ()
forceRoot heap = do
  _ <- Heap.sharesRootWith heap heap
  pure ()

-- Pending tags land on tree roots and on forest spines; this builds a shape carrying both and
-- then audits the fused primitive-child encoding underneath them.
testValidateDeepTaggedShapes :: IO ()
testValidateDeepTaggedShapes = do
  let baseLeft = List.foldl' (\heap index -> Heap.insert index ((index `mod` 137) - 68) heap)
        emptyIntHeap [0 .. 2047 :: Int]
      baseRight = List.foldl' (\heap index -> Heap.insert (index + 2048) (90 - (index `mod` 181)) heap)
        emptyIntHeap [0 .. 2047 :: Int]
      left = Heap.transformAll (Heap.clampAtMost 30)
        (Heap.transformAll (Heap.clampAtLeast (-20)) baseLeft)
      right = Heap.transformAll (Heap.clampAtLeast (-35))
        (Heap.transformAll (Heap.clampAtMost 25) baseRight)
      melded = Heap.transformAll (between (-12) 18) (Heap.meld left right)
      deleteStep heap index =
        let removed = Heap.deleteMinimum heap
        in if index `mod` 8 == 0
             then Heap.transformAll (Heap.clampAtLeast (-10 + (index `div` 8))) removed
             else removed
      stepped = List.foldl' deleteStep melded [0 .. 95 :: Int]
      final = Heap.transformAll (between (-9) 11) (Heap.insert (-1) (-100) stepped)
  statistics <- expectRight "deep tagged validation" (Heap.validateStructure final)
  assertEqual "deep tagged validated count"
    (Heap.count final) (Heap.monotoneStatisticsCount statistics)
  assertEqual "deep tagged expected count" 4001 (Heap.monotoneStatisticsCount statistics)
  assertBool "deep tagged root forest is populated"
    (Heap.monotoneStatisticsRootForestLength statistics > 0)
  assertBool "deep tagged maximum rank" (Heap.monotoneStatisticsMaximumRank statistics > 0)
  assertBool "deep tagged maximum depth" (Heap.monotoneStatisticsMaximumDepth statistics > 1)
  assertBool "deep tagged pending components"
    (Heap.monotoneStatisticsTaggedComponentCount statistics > 0)
  assertEqual "deep tagged enumeration length" (Heap.count final) (length (Heap.toList final))
  assertBool "deep tagged priorities are bounded"
    (all (\entry -> Heap.entryPriority entry >= -9 && Heap.entryPriority entry <= 11)
      (Heap.toList final))
  leftStatistics <- expectRight "deep tagged left" (Heap.validateStructure left)
  rightStatistics <- expectRight "deep tagged right" (Heap.validateStructure right)
  assertEqual "deep tagged left count" 2048 (Heap.monotoneStatisticsCount leftStatistics)
  assertEqual "deep tagged right count" 2048 (Heap.monotoneStatisticsCount rightStatistics)

testCollapsedPriorities :: IO ()
testCollapsedPriorities = do
  let payloads = ["payload-" ++ padded index | index <- [0 .. 2047 :: Int]]
      source = List.foldl'
        (\heap (index, payload) -> Heap.insert payload ((index `mod` 257) - 128) heap)
        emptyStringHeap
        (zip [0 ..] payloads)
      collapsed = Heap.transformAll (Heap.clampConstant 7) source
  validate "collapse source" source
  validate "collapsed" collapsed
  drained <- drain "collapsed" collapsed
  assertEqual "collapsed length" (length payloads) (length drained)
  assertBool "every collapsed priority is the constant" (all ((== 7) . snd) drained)
  assertEqual "collapse preserves every payload"
    (List.sort payloads) (List.sort (map fst drained))
  assertEqual "collapse leaves the source count" (length payloads) (Heap.count source)
  assertBool "collapse leaves the source untransformed"
    (any ((/= 7) . Heap.entryPriority) (Heap.toList source))

padded :: Int -> String
padded value = replicate (4 - length text) '0' ++ text
  where
    text = show value

testFailureAtomicity :: IO ()
testFailureAtomicity = do
  assertEqual "a reversed two-sided clamp is rejected"
    Nothing (Heap.clampBetween compare (2 :: Int) 1)
  assertBool "an ordered two-sided clamp is accepted"
    (Heap.clampBetween compare (1 :: Int) 2 /= Nothing)
  assertEqual "an equal-bound two-sided clamp is accepted"
    (Just (between 3 3)) (Heap.clampBetween compare (3 :: Int) 3)

  -- A policy that fails part-way through composition abandons its successor: an update returns a
  -- new version by value, so a failed one publishes nothing and every source stays usable.
  let hostile = clampPolicy { Heap.actionCompose = \_ _ -> error "injected composition failure" }
      source = Heap.fromListWith hostile [(10, 10), (20, 20), (30, -5)] :: IntHeap
      sourceEntries = entries source
  outcome <- try (forceRoot (Heap.transformAll (Heap.clampAtLeast 5) source))
  case outcome :: Either ErrorCall () of
    Right _ -> failWith "a failing composition must not publish a successor"
    Left _ -> pure ()
  assertEqual "a failed transform leaves its source unchanged" sourceEntries (entries source)
  assertEqual "a failed transform leaves the source minimum"
    (Just (30, -5)) (fmap entryPair (Heap.minimum source))
  validate "atomicity source" source

testEmptyAndBoundaries :: IO ()
testEmptyAndBoundaries = do
  assertBool "an empty heap is empty" (Heap.null emptyIntHeap)
  assertEqual "an empty heap has no entries" 0 (Heap.count emptyIntHeap)
  assertEqual "an empty heap has no minimum"
    Nothing (fmap entryPair (Heap.minimum emptyIntHeap))
  assertBool "an empty heap has no minimum view" (viewIsEmpty (Heap.minView emptyIntHeap))
  assertEqual "an empty heap enumerates nothing" 0 (length (Heap.toList emptyIntHeap))
  assertEqual "an empty heap audits to zero statistics"
    (Right (Heap.MonotoneActionHeapStatistics 0 0 0 0 0)) (Heap.validateStructure emptyIntHeap)
  Heap.sharesRootWith emptyIntHeap (Heap.deleteMinimum emptyIntHeap)
    >>= assertBool "deleting from an empty heap is a no-op"
  Heap.sharesRootWith emptyIntHeap (Heap.transformAll (Heap.clampAtLeast 3) emptyIntHeap)
    >>= assertBool "transforming an empty heap is a no-op"
  Heap.sharesRootWith emptyIntHeap (Heap.meld emptyIntHeap emptyIntHeap)
    >>= assertBool "melding two empty heaps is a no-op"

  let singleton = Heap.insert 7 maxBound emptyIntHeap
  assertEqual "singleton minimum" (Just (7, maxBound)) (fmap entryPair (Heap.minimum singleton))
  (removed, afterRemoval) <- expectJust "singleton minimum view" (Heap.minView singleton)
  assertEqual "singleton removed entry" (7, maxBound) (entryPair removed)
  assertBool "singleton remainder is empty" (Heap.null afterRemoval)
  assertEqual "singleton count" 1 (Heap.count singleton)
  validate "singleton" singleton
  validate "singleton remainder" afterRemoval

  let identity = Heap.clampIdentity :: Clamp
      floorAction = Heap.clampAtLeast (minBound :: Int)
      capAction = Heap.clampAtMost (maxBound :: Int)
      constantAction = Heap.clampConstant (42 :: Int)
  assertBool "the identity clamp is not constant" (not (Heap.clampIsConstant identity))
  assertEqual "the identity clamp has no constant" Nothing (Heap.clampConstantValue identity)
  assertEqual "the identity clamp has no lower bound" Nothing (Heap.clampLowerBound identity)
  assertEqual "the identity clamp has no upper bound" Nothing (Heap.clampUpperBound identity)
  assertEqual "a floor exposes its lower bound" (Just minBound) (Heap.clampLowerBound floorAction)
  assertEqual "a floor has no upper bound" Nothing (Heap.clampUpperBound floorAction)
  assertBool "a floor is not constant" (not (Heap.clampIsConstant floorAction))
  assertEqual "a cap exposes its upper bound" (Just maxBound) (Heap.clampUpperBound capAction)
  assertEqual "a cap has no lower bound" Nothing (Heap.clampLowerBound capAction)
  assertBool "a constant is constant" (Heap.clampIsConstant constantAction)
  assertEqual "a constant exposes its value" (Just 42) (Heap.clampConstantValue constantAction)
  assertEqual "a constant has no lower bound" Nothing (Heap.clampLowerBound constantAction)
  assertEqual "a constant has no upper bound" Nothing (Heap.clampUpperBound constantAction)

  let extremes = Heap.insert 2 maxBound (Heap.insert 1 minBound emptyIntHeap)
      bounded = Heap.transformAll (between (-10) 10) extremes
  assertEntries "clamped extremes" [(1, -10), (2, 10)] bounded
  Heap.sharesRootWith extremes (Heap.transformAll identity extremes)
    >>= assertBool "an identity transform shares the root"
  Heap.sharesRootWith extremes (Heap.meld extremes emptyIntHeap)
    >>= assertBool "melding an empty right operand shares the root"
  Heap.sharesRootWith extremes (Heap.meld emptyIntHeap extremes)
    >>= assertBool "melding an empty left operand shares the root"
  validate "extremes" extremes

newtype Rng = Rng Word64

newRng :: Word64 -> Rng
newRng seed = Rng (seed .|. 1)

nextWord :: Rng -> (Word64, Rng)
nextWord (Rng state) =
  let shifted = state `xor` (state `shiftL` 13)
      folded = shifted `xor` (shifted `shiftR` 7)
      mixed = folded `xor` (folded `shiftL` 17)
  in (mixed, Rng mixed)

nextBelow :: Int -> Rng -> (Int, Rng)
nextBelow bound rng =
  let (value, next) = nextWord rng
  in (fromIntegral (value `mod` fromIntegral bound), next)

nextBetween :: Int -> Int -> Rng -> (Int, Rng)
nextBetween low high rng =
  let (offset, next) = nextBelow (high - low + 1) rng
  in (low + offset, next)

randomClamp :: Rng -> (Clamp, Rng)
randomClamp rng =
  let (firstValue, afterFirst) = nextBetween (-100) 100 rng
      (secondValue, afterSecond) = nextBetween (-100) 100 afterFirst
      (choice, afterChoice) = nextBelow 6 afterSecond
      action = case choice of
        0 -> Heap.clampIdentity
        1 -> Heap.clampAtLeast firstValue
        2 -> Heap.clampAtMost firstValue
        3 -> between (min firstValue secondValue) (max firstValue secondValue)
        4 -> between firstValue firstValue
        _ -> Heap.clampConstant firstValue
  in (action, afterChoice)

data ModelVersion = ModelVersion
  { versionHeap :: IntHeap
  , versionModel :: [(Int, Int)]
  , versionLength :: !Int
  }

data ModelState = ModelState
  { stateVersions :: [ModelVersion]
  , stateVersionCount :: !Int
  , stateRng :: !Rng
  , stateNextPayload :: !Int
  , stateInserts :: !Int
  , stateDeletes :: !Int
  , stateMelds :: !Int
  , stateTransforms :: !Int
  }

data Operation = OperationInsert | OperationDelete | OperationTransform | OperationMeld

-- Thousands of operations issued from randomly chosen retained branches, checked against an
-- immutable list model.  Every source version has to keep reporting its own contents afterwards.
testRandomizedModel :: IO ()
testRandomizedModel = do
  let initial = ModelState
        { stateVersions = [ModelVersion emptyIntHeap [] 0]
        , stateVersionCount = 1
        , stateRng = newRng 0x5A17_2026
        , stateNextPayload = 0
        , stateInserts = 0
        , stateDeletes = 0
        , stateMelds = 0
        , stateTransforms = 0
        }
  final <- walkModel 0 3000 initial
  assertBool ("insert coverage: " ++ show (stateInserts final)) (stateInserts final > 300)
  assertBool ("delete coverage: " ++ show (stateDeletes final)) (stateDeletes final > 150)
  assertBool ("meld coverage: " ++ show (stateMelds final)) (stateMelds final > 100)
  assertBool ("transform coverage: " ++ show (stateTransforms final)) (stateTransforms final > 300)
  let versions = stateVersions final
  forM_ (zip [0 :: Int ..] versions) $ \(index, version) ->
    if index `mod` 97 == 0
      then assertMatchesModel ("sampled version " ++ show index) version
      else pure ()
  case versions of
    [] -> failWith "the randomized model retained no versions"
    latest : _ -> assertMatchesModel "final version" latest

walkModel :: Int -> Int -> ModelState -> IO ModelState
walkModel step limit state
  | step >= limit = pure state
  | otherwise = do
      next <- modelStep step state
      walkModel (step + 1) limit next

modelStep :: Int -> ModelState -> IO ModelState
modelStep step state = do
  let versions = stateVersions state
      (sourceIndex, afterSource) = nextBelow (stateVersionCount state) (stateRng state)
      source = versions !! sourceIndex
      sourceLength = versionLength source
      (operation, afterOperation) = nextBelow 100 afterSource
  (result, afterStep, kind) <-
    if sourceLength == 0 || (sourceLength < 384 && operation < 30)
      then do
        let (priority, afterPriority) = nextBetween (-100) 100 afterOperation
            element = stateNextPayload state
            heap = Heap.insert element priority (versionHeap source)
        pure
          ( ModelVersion heap ((element, priority) : versionModel source) (sourceLength + 1)
          , afterPriority
          , OperationInsert )
      else if operation < 50
        then do
          (entry, remainder) <-
            expectJust "delete-min from a nonempty version" (Heap.minView (versionHeap source))
          assertEqual "delete-min returns the model minimum"
            (minimum (map snd (versionModel source))) (Heap.entryPriority entry)
          model <- removeOne (entryPair entry) (versionModel source)
          pure (ModelVersion remainder model (sourceLength - 1), afterOperation, OperationDelete)
        else if operation < 75
          then pure (transformVersion source afterOperation)
          else case findPartner 24 versions (stateVersionCount state) sourceLength afterOperation of
            (Nothing, afterAttempts) -> pure (transformVersion source afterAttempts)
            (Just partner, afterAttempts) -> do
              let heap = Heap.meld (versionHeap source) (versionHeap partner)
              pure
                ( ModelVersion
                    heap
                    (versionModel source ++ versionModel partner)
                    (sourceLength + versionLength partner)
                , afterAttempts
                , OperationMeld )
  let bumped = case kind of
        OperationInsert -> state
          { stateNextPayload = stateNextPayload state + 1
          , stateInserts = stateInserts state + 1 }
        OperationDelete -> state { stateDeletes = stateDeletes state + 1 }
        OperationTransform -> state { stateTransforms = stateTransforms state + 1 }
        OperationMeld -> state { stateMelds = stateMelds state + 1 }
  -- Every produced version is forced immediately, so no retained branch is silently skipped by
  -- laziness, and every source must still report its own count after the operation.
  forceRoot (versionHeap result)
  assertEqual ("step " ++ show step ++ " count")
    (versionLength result) (Heap.count (versionHeap result))
  assertEqual ("step " ++ show step ++ " source count")
    (versionLength source) (Heap.count (versionHeap source))
  if step `mod` 64 == 0
    then do
      assertMatchesModel ("step " ++ show step) result
      assertMatchesModel ("step " ++ show step ++ " source") source
      Heap.sharesRootWith (versionHeap source) (versionHeap (versions !! sourceIndex))
        >>= assertBool ("step " ++ show step ++ ": the source version kept its root")
    else pure ()
  pure bumped
    { stateVersions = result : versions
    , stateVersionCount = stateVersionCount bumped + 1
    , stateRng = afterStep
    }

transformVersion :: ModelVersion -> Rng -> (ModelVersion, Rng, Operation)
transformVersion source rng =
  let (action, afterAction) = randomClamp rng
      heap = Heap.transformAll action (versionHeap source)
      model = [(element, applyClamp action priority) | (element, priority) <- versionModel source]
  in (ModelVersion heap model (versionLength source), afterAction, OperationTransform)

findPartner :: Int -> [ModelVersion] -> Int -> Int -> Rng -> (Maybe ModelVersion, Rng)
findPartner attempts versions versionCount sourceLength rng
  | attempts <= 0 = (Nothing, rng)
  | otherwise =
      let (candidateIndex, next) = nextBelow versionCount rng
          candidate = versions !! candidateIndex
      in if sourceLength + versionLength candidate <= 384
           then (Just candidate, next)
           else findPartner (attempts - 1) versions versionCount sourceLength next

removeOne :: (Int, Int) -> [(Int, Int)] -> IO [(Int, Int)]
removeOne target model = case break (== target) model of
  (_, []) -> failWith "delete-min returned an entry absent from the model"
  (before, _ : after) -> pure (before ++ after)

assertMatchesModel :: String -> ModelVersion -> IO ()
assertMatchesModel label version = do
  statistics <- expectRight (label ++ " validation") (Heap.validateStructure (versionHeap version))
  assertEqual (label ++ " validated count")
    (versionLength version) (Heap.monotoneStatisticsCount statistics)
  assertEqual (label ++ " count") (versionLength version) (Heap.count (versionHeap version))
  assertEqual (label ++ " contents")
    (List.sort (versionModel version)) (entries (versionHeap version))
  if null (versionModel version)
    then do
      assertBool (label ++ " is empty") (Heap.null (versionHeap version))
      assertEqual (label ++ " has no minimum")
        Nothing (fmap Heap.entryPriority (Heap.minimum (versionHeap version)))
    else do
      assertBool (label ++ " is nonempty") (not (Heap.null (versionHeap version)))
      assertEqual (label ++ " minimum")
        (Just (minimum (map snd (versionModel version))))
        (fmap Heap.entryPriority (Heap.minimum (versionHeap version)))

-- A narrow window of recent versions with heavy delete and meld traffic reaches split-forest
-- decompositions that a broad, shallow walk never produces -- in particular a rank-one tree whose
-- embedded heap forest begins above rank zero, which is the branch of the rank-one/rank-zero
-- ambiguity that resolves in favour of the embedded forest.
testDeepDeleteAndMeldShapes :: IO ()
testDeepDeleteAndMeldShapes = do
  window <- walkWindow 0 400 (newRng 0x0D15_EA5E) [(emptyIntHeap, [])]
  forM_ (zip [0 :: Int ..] window) $ \(index, (heap, model)) ->
    assertEntries ("window version " ++ show index) model heap
  forM_ (zip [0 :: Int ..] (take 6 window)) $ \(index, (heap, model)) -> do
    drained <- drain ("window drain " ++ show index) heap
    assertEqual ("window drain " ++ show index ++ " contents")
      (List.sort model) (List.sort drained)
    assertBool ("window drain " ++ show index ++ " is nondecreasing")
      (List.sort (map snd drained) == map snd drained)

walkWindow :: Int
           -> Int
           -> Rng
           -> [(IntHeap, [(Int, Int)])]
           -> IO [(IntHeap, [(Int, Int)])]
walkWindow step limit rng window
  | step >= limit = pure window
  | otherwise = do
      let windowSize = length window
          (sourceIndex, afterSource) = nextBelow windowSize rng
          (heap, model) = window !! sourceIndex
          (operation, afterOperation) = nextBelow 100 afterSource
          (value, afterValue) = nextBetween (-200) 200 afterOperation
          (partnerIndex, afterPartner) = nextBelow windowSize afterValue
          (partnerHeap, partnerModel) = window !! partnerIndex
      produced <-
        if null model || operation < 40
          then pure (Heap.insert step value heap, (step, value) : model)
          else if operation < 70
            then do
              (entry, remainder) <- expectJust "window delete-min" (Heap.minView heap)
              assertEqual ("window step " ++ show step ++ " delete-min priority")
                (minimum (map snd model)) (Heap.entryPriority entry)
              rest <- removeOne (entryPair entry) model
              pure (remainder, rest)
            else if length model + length partnerModel > 512
              then pure (Heap.insert step value heap, (step, value) : model)
              else pure (Heap.meld heap partnerHeap, model ++ partnerModel)
      validate ("window step " ++ show step) (fst produced)
      assertEqual ("window step " ++ show step ++ " count")
        (length (snd produced)) (Heap.count (fst produced))
      walkWindow (step + 1) limit afterPartner (take 40 (produced : window))

entryPair :: Heap.MonotoneHeapEntry element priority -> (element, priority)
entryPair entry = (Heap.entryElement entry, Heap.entryPriority entry)

entries :: Ord element
        => Heap.PersistentMonotoneActionHeap element Int action
        -> [(element, Int)]
entries heap = List.sort (map entryPair (Heap.toList heap))

assertEntries :: (Ord element, Show element)
              => String
              -> [(element, Int)]
              -> Heap.PersistentMonotoneActionHeap element Int action
              -> IO ()
assertEntries label expected heap = do
  assertEqual (label ++ " contents") (List.sort expected) (entries heap)
  assertEqual (label ++ " count") (length expected) (Heap.count heap)
  validate label heap

validate :: String -> Heap.PersistentMonotoneActionHeap element priority action -> IO ()
validate label heap = do
  statistics <- expectRight (label ++ " structural validation") (Heap.validateStructure heap)
  assertEqual (label ++ " validated count")
    (Heap.count heap) (Heap.monotoneStatisticsCount statistics)

drain :: String
      -> Heap.PersistentMonotoneActionHeap element Int action
      -> IO [(element, Int)]
drain label heap = go (0 :: Int) heap []
  where
    go !index current acc = case Heap.minView current of
      Nothing -> do
        assertBool (label ++ " drained to empty") (Heap.null current)
        pure (reverse acc)
      Just (entry, remainder) -> do
        if index `mod` 256 == 0
          then validate (label ++ " drain remainder") remainder
          else pure ()
        go (index + 1) remainder (entryPair entry : acc)

assertClampEquivalent :: String -> Clamp -> Clamp -> [Int] -> IO ()
assertClampEquivalent label expected actual samples =
  forM_ samples $ \value ->
    assertEqual (label ++ " at " ++ show value)
      (applyClamp expected value) (applyClamp actual value)

viewIsEmpty :: Maybe (Heap.MonotoneHeapEntry element priority, heap) -> Bool
viewIsEmpty view = case view of
  Nothing -> True
  Just _ -> False

expectJust :: String -> Maybe a -> IO a
expectJust label view = case view of
  Just value -> pure value
  Nothing -> failWith (label ++ ": expected Just")

expectRight :: String -> Either String a -> IO a
expectRight label result = case result of
  Right value -> pure value
  Left message -> failWith (label ++ ": " ++ message)

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = failWith (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = failWith (label ++ ": expected true")

failWith :: String -> IO a
failWith message = error ("PersistentMonotoneActionHeapTests: " ++ message)
