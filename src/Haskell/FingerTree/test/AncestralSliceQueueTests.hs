-- | Tests for the ancestral slice queue: the interval algebra against list
-- models, the anchored-empty rule, retained branching histories, structure
-- sharing, and the ancestor-query discipline every bound depends on.
module AncestralSliceQueueTests (run) where

import Control.Monad (forM_, when)
import qualified Data.List as List
import Data.Maybe (listToMaybe)

import qualified Durable7.FingerTree.AncestralSliceQueue as Queue
import qualified Durable7.FingerTree.IncrementalAncestor as Ancestor

-- | Runs this module's test cases, failing with a diagnostic on the first
-- mismatch.
run :: IO ()
run = do
  testEmptyAndEndpointContracts
  testEmptyBoundaryRanges
  testEveryRangeAndAnchoredEmpties
  testTakeDropAndSplitAtBoundaries
  testSquareAndOddBlockBoundaries
  testEnumerationStability
  testQueryDiscipline
  testFactoriesAndIndependence
  testEndpointRemovalHistories
  testRetainedBranches
  testRandomizedBranchingHistory
  testOutOfRangeRejections
  testStructuralValidation
  testSharingIdentities

testEmptyAndEndpointContracts :: IO ()
testEmptyAndEndpointContracts = do
  let emptyQueue = Queue.empty :: Queue.AncestralSliceQueue (Maybe String)
  assertState "empty" [] emptyQueue
  assertEqual "empty index 0" Nothing (Queue.index 0 emptyQueue)
  assertEqual "empty index -1" Nothing (Queue.index (-1) emptyQueue)
  assertEqual "empty removeFirst" NothingResult (tagMaybe (Queue.removeFirst emptyQueue))
  assertEqual "empty removeLast" NothingResult (tagMaybe (Queue.removeLast emptyQueue))
  assertEqual "empty tryRemoveFirst" NothingResult (tagMaybe (Queue.tryRemoveFirst emptyQueue))
  assertEqual "empty tryRemoveLast" NothingResult (tagMaybe (Queue.tryRemoveLast emptyQueue))

  -- A stored Nothing is a present value; absence is reported by the outer Maybe,
  -- exactly as a stored null is a present value in the reference contract.
  let singleton = Queue.addLast emptyQueue Nothing
  assertState "singleton" [Nothing] singleton
  assertEqual "singleton first" (Just Nothing) (Queue.first singleton)
  assertEqual "singleton last" (Just Nothing) (Queue.last singleton)

  let pair = Queue.addLast singleton (Just "tail")
  assertState "pair" [Nothing, Just "tail"] pair
  assertState "singleton retained" [Nothing] singleton

  (removedFirst, afterFirst) <- expectJust "pair tryRemoveFirst" (Queue.tryRemoveFirst pair)
  assertEqual "pair removed front" Nothing removedFirst
  assertState "pair after front removal" [Just "tail"] afterFirst
  (removedLast, afterLast) <- expectJust "pair tryRemoveLast" (Queue.tryRemoveLast pair)
  assertEqual "pair removed back" (Just "tail") removedLast
  assertState "pair after back removal" [Nothing] afterLast
  assertState "pair retained" [Nothing, Just "tail"] pair

testEmptyBoundaryRanges :: IO ()
testEmptyBoundaryRanges = do
  let emptyQueue = Queue.empty :: Queue.AncestralSliceQueue Int
  prefix <- expectJust "empty take" (Queue.take 0 emptyQueue)
  suffix <- expectJust "empty drop" (Queue.drop 0 emptyQueue)
  ranged <- expectJust "empty slice" (Queue.slice 0 0 emptyQueue)
  assertState "empty take" [] prefix
  assertState "empty drop" [] suffix
  assertState "empty slice" [] ranged
  (left, right) <- expectJust "empty split" (Queue.splitAt 0 emptyQueue)
  assertState "empty split left" [] left
  assertState "empty split right" [] right
  assertState "empty split left append" [1] (Queue.addLast left 1)
  assertState "empty split right append" [2] (Queue.addLast right 2)

  -- A singleton emptied from either end appends to exactly one visible value,
  -- but the two results keep different anchors.
  let singleton = Queue.addLast emptyQueue 7
  drainedFront <- expectJust "singleton removeFirst" (Queue.removeFirst singleton)
  drainedBack <- expectJust "singleton removeLast" (Queue.removeLast singleton)
  assertState "front-drained singleton" [] drainedFront
  assertState "back-drained singleton" [] drainedBack
  assertState "front-drained append" [8] (Queue.addLast drainedFront 8)
  assertState "back-drained append" [9] (Queue.addLast drainedBack 9)
  assertState "singleton retained" [7] singleton
  shares <- Queue.sharesRootWith drainedFront drainedBack
  assertBool "drained singletons keep different anchors" (not shares)
  assertEqual "front-drained anchor depth" 0 (tailDepthOf drainedFront)
  assertEqual "front-drained low depth" 1 (Queue.lowDepth drainedFront)
  assertEqual "back-drained anchor depth" (-1) (tailDepthOf drainedBack)
  assertEqual "back-drained low depth" 0 (Queue.lowDepth drainedBack)

testEveryRangeAndAnchoredEmpties :: IO ()
testEveryRangeAndAnchoredEmpties = do
  let lengthValue = 65 :: Int
      values = [0 .. lengthValue - 1]
      source = Queue.fromList values
  forM_ [0 .. lengthValue] $ \start ->
    forM_ [0 .. lengthValue - start] $ \amount -> do
      let label = "slice " ++ show start ++ " " ++ show amount
      ranged <- expectJust label (Queue.slice start amount source)
      assertState label (take amount (drop start values)) ranged
      when (amount == 0) $ do
        -- Appending to an empty slice continues at that exact anchor and never
        -- resurrects the discarded prefix.
        let continued = Queue.addLast (Queue.addLast ranged (negate start)) (10000 + start)
        assertState (label ++ " anchored append") [negate start, 10000 + start] continued
        assertState (label ++ " anchor retained") [] ranged
        assertEqual (label ++ " anchor depth") (start - 1) (tailDepthOf ranged)
        assertEqual (label ++ " anchor low depth") start (Queue.lowDepth ranged)
  whole <- expectJust "whole slice" (Queue.slice 0 lengthValue source)
  prefix <- expectJust "whole take" (Queue.take lengthValue source)
  suffix <- expectJust "zero drop" (Queue.drop 0 source)
  assertReceiver "whole slice" source whole
  assertReceiver "whole take" source prefix
  assertReceiver "zero drop" source suffix
  assertState "source retained" values source

testTakeDropAndSplitAtBoundaries :: IO ()
testTakeDropAndSplitAtBoundaries = do
  let lengthValue = 257 :: Int
      values = [0 .. lengthValue - 1]
      source = Queue.fromList values
  forM_ [0 .. lengthValue] $ \position -> do
    prefix <- expectJust ("take " ++ show position) (Queue.take position source)
    suffix <- expectJust ("drop " ++ show position) (Queue.drop position source)
    assertState ("take " ++ show position) (take position values) prefix
    assertState ("drop " ++ show position) (drop position values) suffix
    (left, right) <- expectJust ("splitAt " ++ show position) (Queue.splitAt position source)
    assertContents ("splitAt left " ++ show position) (take position values) left
    assertContents ("splitAt right " ++ show position) (drop position values) right

    -- Both results stay real ancestry slices and start independent branches.
    let marker = negate position - 1
    assertContents ("prefix branch " ++ show position)
      (take position values ++ [marker])
      (Queue.addLast prefix marker)
    assertContents ("suffix branch " ++ show position)
      (drop position values ++ [marker])
      (Queue.addLast suffix marker)
  assertState "source retained" values source

testSquareAndOddBlockBoundaries :: IO ()
testSquareAndOddBlockBoundaries = do
  -- The squares are the seams induced by conceptual odd-sized blocks 1, 3,
  -- 5, ..., even though this port's jump-link forest encodes them differently.
  let maximumRoot = 128 :: Int
      lengthValue = maximumRoot * maximumRoot + 1
      values = [0 .. lengthValue - 1]
      source = Queue.fromList values
  forM_ [0 .. maximumRoot] $ \root -> do
    let square = root * root
        start = max 0 (square - 1)
        amount = min 3 (lengthValue - start)
    assertEqual ("square index " ++ show square) (Just square) (Queue.index square source)
    seamSlice <- expectJust ("seam slice " ++ show square) (Queue.slice start amount source)
    assertState ("seam slice " ++ show square) [start .. start + amount - 1] seamSlice
    anchored <- expectJust ("empty at square " ++ show square) (Queue.slice square 0 source)
    assertState ("empty at square " ++ show square) [] anchored
    assertState ("append at square " ++ show square)
      [negate square] (Queue.addLast anchored (negate square))
    when (square > 0) $ do
      beforeSeam <- expectJust ("prefix before seam " ++ show square) (Queue.take (square - 1) source)
      assertContents ("branch before seam " ++ show square)
        ([0 .. square - 2] ++ [negate square - 1])
        (Queue.addLast beforeSeam (negate square - 1))
      atSeam <- expectJust ("prefix at seam " ++ show square) (Queue.take square source)
      assertContents ("branch at seam " ++ show square)
        ([0 .. square - 1] ++ [negate square - 2])
        (Queue.addLast atSeam (negate square - 2))
    when (root < maximumRoot) $ do
      let nextSquare = (root + 1) * (root + 1)
      assertEqual ("odd block width " ++ show square) (2 * root + 1) (nextSquare - square)
      block <- expectJust ("odd block " ++ show square)
        (Queue.slice square (nextSquare - square) source)
      assertContents ("odd block " ++ show square) [square .. nextSquare - 1] block
  assertContents "source retained" values source

testEnumerationStability :: IO ()
testEnumerationStability = do
  let values = [0 .. 99] :: [Int]
      source = Queue.fromList values
      expected = take 40 (drop 20 values)
  ranged <- expectJust "interior slice" (Queue.slice 20 40 source)
  let captured = Queue.toList ranged
      branch = Queue.addLast (Queue.addLast ranged (-1)) (-2)
  assertEqual "repeatable enumeration" expected (Queue.toList ranged)
  assertEqual "repeatable enumeration again" expected (Queue.toList ranged)
  assertEqual "branch enumeration" (expected ++ [-1, -2]) (Queue.toList branch)
  -- The captured path predates the branch and can never observe it.
  assertEqual "captured enumeration" expected captured
  assertState "slice retained" expected ranged
  assertState "source retained" values source

testQueryDiscipline :: IO ()
testQueryDiscipline = do
  let source = Queue.fromList [0 .. 15 :: Int]
  assertEqual "count" 16 (Queue.count source)
  assertBool "not empty" (not (Queue.null source))
  assertEqual "tail depth" 15 (tailDepthOf source)
  assertEqual "low depth" 0 (Queue.lowDepth source)

  -- The back endpoint and the last visible index read the retained tail, so the
  -- level-ancestor query they would otherwise issue follows no links at all.
  assertEqual "back endpoint" (Just 15) (Queue.last source)
  assertEqual "last index" (Just 15) (Queue.index 15 source)
  assertEqual "last index reads the tail"
    (Ancestor.nodeValue (Queue.tailNode source)) (Queue.index 15 source)
  assertEqual "tail-depth hops" (Just 0) (hopsToDepth source (tailDepthOf source))

  -- The front and every interior index must ascend the path instead.
  assertEqual "front endpoint" (Just 0) (Queue.first source)
  frontHops <- expectJust "front hops" (hopsToDepth source (Queue.lowDepth source))
  assertBool "the front follows ancestry links" (frontHops > 0)
  assertEqual "interior index" (Just 7) (Queue.index 7 source)
  interiorHops <- expectJust "interior hops" (hopsToDepth source 7)
  assertBool "an interior index follows ancestry links" (interiorHops > 0)

  -- A singleton's front is its own tail, so it makes no query.
  let singleton = Queue.fromList [42 :: Int]
  assertEqual "singleton front" (Just 42) (Queue.first singleton)
  assertEqual "singleton front hops" (Just 0)
    (hopsToDepth singleton (Queue.lowDepth singleton))

  removedFirst <- expectJust "removeFirst" (Queue.removeFirst source)
  assertQueryFree "removeFirst" source removedFirst
  removedLast <- expectJust "removeLast" (Queue.removeLast source)
  assertUsesParentLink "removeLast" source removedLast
  dropped <- expectJust "drop 7" (Queue.drop 7 source)
  assertQueryFree "drop 7" source dropped
  assertEqual "drop 7 count" 9 (Queue.count dropped)
  taken <- expectJust "take 7" (Queue.take 7 source)
  assertQueried "take 7" source taken
  assertEqual "take 7 count" 7 (Queue.count taken)
  interior <- expectJust "slice 3 5" (Queue.slice 3 5 source)
  assertQueried "slice 3 5" source interior
  suffix <- expectJust "slice 3 13" (Queue.slice 3 13 source)
  assertQueryFree "slice 3 13" source suffix

  wholeTake <- expectJust "take 16" (Queue.take 16 source)
  zeroDrop <- expectJust "drop 0" (Queue.drop 0 source)
  wholeSlice <- expectJust "slice 0 16" (Queue.slice 0 16 source)
  assertReceiver "take 16" source wholeTake
  assertReceiver "drop 0" source zeroDrop
  assertReceiver "slice 0 16" source wholeSlice

  (splitLeft, splitRight) <- expectJust "splitAt 7" (Queue.splitAt 7 source)
  assertQueried "splitAt 7 left" source splitLeft
  assertQueryFree "splitAt 7 right" source splitRight
  assertEqual "splitAt 7 left count" 7 (Queue.count splitLeft)
  assertEqual "splitAt 7 right count" 9 (Queue.count splitRight)

  -- A split at zero is not query-free: the anchored-empty rule makes the empty
  -- prefix retain the node one level above the window, and only an ancestor
  -- query can name that node from the retained tail.
  (zeroLeft, zeroRight) <- expectJust "splitAt 0" (Queue.splitAt 0 source)
  assertQueried "splitAt 0 left" source zeroLeft
  assertReceiver "splitAt 0 right" source zeroRight
  assertEqual "splitAt 0 anchor depth" (-1) (tailDepthOf zeroLeft)

  -- A split at the length reuses the source tail for both sides.
  (fullLeft, fullRight) <- expectJust "splitAt 16" (Queue.splitAt 16 source)
  assertReceiver "splitAt 16 left" source fullLeft
  assertQueryFree "splitAt 16 right" source fullRight
  assertEqual "splitAt 16 right count" 0 (Queue.count fullRight)
  assertEqual "splitAt 16 right low depth" 16 (Queue.lowDepth fullRight)

  let appended = Queue.addLast source 16
  assertExtendsTail "addLast" source appended
  assertState "addLast contents" ([0 .. 15] ++ [16]) appended
  assertState "source retained" [0 .. 15] source

testFactoriesAndIndependence :: IO ()
testFactoriesAndIndependence = do
  assertState "empty factory" ([] :: [Int]) Queue.empty
  let ranged = Queue.fromList [1, 2, 3 :: Int]
  assertState "fromList" [1, 2, 3] ranged
  assertState "fromList append" [1, 2, 3, 4] (Queue.addLast ranged 4)
  assertState "fromList retained" [1, 2, 3] ranged
  assertState "fromList empty" ([] :: [Int]) (Queue.fromList [])

  -- Every value grown from `empty` starts its own branch, so unrelated queues
  -- share no history: there is no arena to inject and none to contend on.
  let firstQueue = Queue.addLast Queue.empty (1 :: Int)
      secondQueue = Queue.addLast Queue.empty (2 :: Int)
  assertState "independent first" [1] firstQueue
  assertState "independent second" [2] secondQueue
  shares <- Queue.sharesRootWith firstQueue secondQueue
  assertBool "independent branches keep separate tails" (not shares)
  emptiesShare <- Queue.sharesRootWith
    (Queue.empty :: Queue.AncestralSliceQueue Int) Queue.empty
  assertBool "every bottom-anchored empty shares its anchor" emptiesShare

testEndpointRemovalHistories :: IO ()
testEndpointRemovalHistories = do
  let lengthValue = 512 :: Int
      values = [0 .. lengthValue - 1]
      source = Queue.fromList values
  frontVersions <- drainWith "front" Queue.removeFirst lengthValue source
  backVersions <- drainWith "back" Queue.removeLast lengthValue source
  forM_ (zip [0 :: Int ..] frontVersions) $ \(removed, queue) ->
    assertContents ("front drained " ++ show removed) (drop removed values) queue
  forM_ (zip [0 :: Int ..] backVersions) $ \(removed, queue) ->
    assertContents ("back drained " ++ show removed) (take (lengthValue - removed) values) queue

  frontEmpty <- expectJust "front empty" (lastValue frontVersions)
  backEmpty <- expectJust "back empty" (lastValue backVersions)
  assertState "front-drained empty" [] frontEmpty
  assertState "back-drained empty" [] backEmpty
  assertState "front-drained empty append" [-1] (Queue.addLast frontEmpty (-1))
  assertState "back-drained empty append" [-2] (Queue.addLast backEmpty (-2))
  shares <- Queue.sharesRootWith frontEmpty backEmpty
  assertBool "drained empties keep different anchors" (not shares)
  assertEqual "front-drained anchor depth" (lengthValue - 1) (tailDepthOf frontEmpty)
  assertEqual "front-drained low depth" lengthValue (Queue.lowDepth frontEmpty)
  assertEqual "back-drained anchor depth" (-1) (tailDepthOf backEmpty)
  assertEqual "back-drained low depth" 0 (Queue.lowDepth backEmpty)
  assertState "source retained" values source

testRetainedBranches :: IO ()
testRetainedBranches = do
  let values = [0 .. 199] :: [Int]
      source = Queue.fromList values
      segment low high = [low .. high - 1]
  middle <- expectJust "interior slice" (Queue.slice 50 100 source)
  afterFirst <- expectJust "middle removeFirst" (Queue.removeFirst middle)
  afterLast <- expectJust "middle removeLast" (Queue.removeLast middle)
  prefix <- expectJust "middle take" (Queue.take 40 middle)
  suffix <- expectJust "middle drop" (Queue.drop 60 middle)
  anchored <- expectJust "middle empty slice" (Queue.slice 30 0 middle)
  assertState "branch append" (segment 50 150 ++ [-1]) (Queue.addLast middle (-1))
  assertState "branch double append" (segment 50 150 ++ [-2, -3])
    (Queue.addLast (Queue.addLast middle (-2)) (-3))
  assertState "branch after front removal" (segment 51 150 ++ [-4]) (Queue.addLast afterFirst (-4))
  assertState "branch after back removal" (segment 50 149 ++ [-5]) (Queue.addLast afterLast (-5))
  assertState "branch from prefix" (segment 50 90 ++ [-6]) (Queue.addLast prefix (-6))
  assertState "branch from suffix" (segment 110 150 ++ [-7]) (Queue.addLast suffix (-7))
  assertState "branch from anchored empty" [-8] (Queue.addLast anchored (-8))
  assertState "middle retained" (segment 50 150) middle
  assertState "source retained" values source

testRandomizedBranchingHistory :: IO ()
testRandomizedBranchingHistory = do
    histories <- walk 0 20260805 [(Queue.empty, []) :: (Queue.AncestralSliceQueue Int, [Int])]
    forM_ (everyNth 97 histories) $ \(queue, model) ->
      assertState "randomized retained" model queue
  where
    stepCount = 1200 :: Int

    walk step seed histories
      | step == stepCount = pure histories
      | otherwise = do
          let (choice, seed1) = randomBelow (length histories) seed
          (source, model) <- expectJust "randomized selection" (pick choice histories)
          let visible = length model
              (operation, seed2) = randomBelow 9 seed1
              (position, seed3) = randomBelow (visible + 1) seed2
              (amount, seed4) = randomBelow (visible - position + 1) seed3
              (side, seed5) = randomBelow 2 seed4
          (result, expected) <- case operation of
            1 | visible /= 0 -> do
                  rest <- expectJust "randomized removeFirst" (Queue.removeFirst source)
                  pure (rest, drop 1 model)
            2 | visible /= 0 -> do
                  rest <- expectJust "randomized removeLast" (Queue.removeLast source)
                  pure (rest, take (visible - 1) model)
            3 -> do
                  taken <- expectJust "randomized take" (Queue.take position source)
                  pure (taken, take position model)
            4 -> do
                  dropped <- expectJust "randomized drop" (Queue.drop position source)
                  pure (dropped, drop position model)
            5 -> do
                  ranged <- expectJust "randomized slice" (Queue.slice position amount source)
                  pure (ranged, take amount (drop position model))
            6 -> do
                  (left, right) <- expectJust "randomized split" (Queue.splitAt position source)
                  pure (if side == 0
                          then (left, take position model)
                          else (right, drop position model))
            7 | visible /= 0 -> do
                  (value, rest) <- expectJust "randomized tryRemoveFirst" (Queue.tryRemoveFirst source)
                  assertEqual "randomized front value" (listToMaybe model) (Just value)
                  pure (rest, drop 1 model)
            8 | visible /= 0 -> do
                  (value, rest) <- expectJust "randomized tryRemoveLast" (Queue.tryRemoveLast source)
                  assertEqual "randomized back value" (lastValue model) (Just value)
                  pure (rest, take (visible - 1) model)
            _ -> pure (Queue.addLast source step, model ++ [step])
          assertState ("randomized source at step " ++ show step) model source
          assertState ("randomized result at step " ++ show step) expected result
          walk (step + 1) seed5 ((result, expected) : histories)

testOutOfRangeRejections :: IO ()
testOutOfRangeRejections = do
  let source = Queue.fromList [10, 20, 30 :: Int]
  assertEqual "index -1" Nothing (Queue.index (-1) source)
  assertEqual "index 3" Nothing (Queue.index 3 source)
  assertEqual "index maxBound" Nothing (Queue.index maxBound source)
  assertEqual "slice -1 0" NothingResult (tagMaybe (Queue.slice (-1) 0 source))
  assertEqual "slice 0 -1" NothingResult (tagMaybe (Queue.slice 0 (-1) source))
  assertEqual "slice 4 0" NothingResult (tagMaybe (Queue.slice 4 0 source))
  assertEqual "slice 2 2" NothingResult (tagMaybe (Queue.slice 2 2 source))
  assertEqual "slice 2 maxBound" NothingResult (tagMaybe (Queue.slice 2 maxBound source))
  assertEqual "take -1" NothingResult (tagMaybe (Queue.take (-1) source))
  assertEqual "take 4" NothingResult (tagMaybe (Queue.take 4 source))
  assertEqual "drop -1" NothingResult (tagMaybe (Queue.drop (-1) source))
  assertEqual "drop 4" NothingResult (tagMaybe (Queue.drop 4 source))
  assertEqual "splitAt -1" NothingResult (tagMaybe (Queue.splitAt (-1) source))
  assertEqual "splitAt 4" NothingResult (tagMaybe (Queue.splitAt 4 source))
  assertState "source retained" [10, 20, 30] source

testStructuralValidation :: IO ()
testStructuralValidation = do
  emptyStatistics <- expectRight "empty validation"
    (Queue.validateStructure (Queue.empty :: Queue.AncestralSliceQueue Int))
  assertEqual "empty statistics"
    (Queue.ValidationStatistics 0 0 (-1) (-1) 0 0)
    emptyStatistics

  let source = Queue.fromList [0 .. 9 :: Int]
  sourceStatistics <- expectRight "source validation" (Queue.validateStructure source)
  assertEqual "source validated count" 10 (Queue.validationCount sourceStatistics)
  assertEqual "source low depth" 0 (Queue.validationLowDepth sourceStatistics)
  assertEqual "source anchor depth" (-1) (Queue.validationAnchorDepth sourceStatistics)
  assertEqual "source tail depth" 9 (Queue.validationTailDepth sourceStatistics)
  assertEqual "source path length" 10 (Queue.validationPathLength sourceStatistics)
  assertBool "source jump links counted" (Queue.validationJumpLinkCount sourceStatistics >= 0)

  ranged <- expectJust "interior slice" (Queue.slice 3 4 source)
  rangedStatistics <- expectRight "slice validation" (Queue.validateStructure ranged)
  assertEqual "slice validated count" 4 (Queue.validationCount rangedStatistics)
  assertEqual "slice low depth" 3 (Queue.validationLowDepth rangedStatistics)
  assertEqual "slice anchor depth" 2 (Queue.validationAnchorDepth rangedStatistics)
  assertEqual "slice tail depth" 6 (Queue.validationTailDepth rangedStatistics)
  -- The path below the window is retained even though this value cannot see it.
  assertEqual "slice path length" 7 (Queue.validationPathLength rangedStatistics)

  drained <- expectJust "front-drained" (Queue.drop 10 source)
  drainedStatistics <- expectRight "front-drained validation" (Queue.validateStructure drained)
  assertEqual "front-drained validated count" 0 (Queue.validationCount drainedStatistics)
  assertEqual "front-drained low depth" 10 (Queue.validationLowDepth drainedStatistics)
  assertEqual "front-drained anchor depth" 9 (Queue.validationAnchorDepth drainedStatistics)
  assertEqual "front-drained tail depth" 9 (Queue.validationTailDepth drainedStatistics)
  assertEqual "front-drained path length" 10 (Queue.validationPathLength drainedStatistics)

  -- Every version reachable through the slicing algebra validates.
  forM_ [0 .. 10] $ \start ->
    forM_ [0 .. 10 - start] $ \amount -> do
      slice <- expectJust ("validated slice " ++ show start) (Queue.slice start amount source)
      statistics <- expectRight ("validated slice " ++ show start) (Queue.validateStructure slice)
      assertEqual ("validated slice count " ++ show start) amount
        (Queue.validationCount statistics)
      assertEqual ("validated slice low depth " ++ show start) start
        (Queue.validationLowDepth statistics)
      assertEqual ("validated slice tail depth " ++ show start) (start + amount - 1)
        (Queue.validationTailDepth statistics)

testSharingIdentities :: IO ()
testSharingIdentities = do
  let lengthValue = 32 :: Int
      source = Queue.fromList [0 .. lengthValue - 1]
  wholeTake <- expectJust "whole take" (Queue.take lengthValue source)
  zeroDrop <- expectJust "zero drop" (Queue.drop 0 source)
  wholeSlice <- expectJust "whole slice" (Queue.slice 0 lengthValue source)
  (fullLeft, fullRight) <- expectJust "boundary split" (Queue.splitAt lengthValue source)
  assertReceiver "whole take" source wholeTake
  assertReceiver "zero drop" source zeroDrop
  assertReceiver "whole slice" source wholeSlice
  assertReceiver "boundary split left" source fullLeft
  assertQueryFree "boundary split right" source fullRight

  -- Every suffix keeps the source tail; every proper prefix must re-derive one.
  forM_ [0 .. lengthValue - 1] $ \start -> do
    suffix <- expectJust ("suffix " ++ show start) (Queue.slice start (lengthValue - start) source)
    assertQueryFree ("suffix " ++ show start) source suffix
  forM_ [0 .. lengthValue - 1] $ \amount -> do
    prefix <- expectJust ("prefix " ++ show amount) (Queue.take amount source)
    assertQueried ("prefix " ++ show amount) source prefix

-- Assertions and helpers.

data MaybeResult = NothingResult | JustResult
  deriving (Eq, Show)

tagMaybe :: Maybe a -> MaybeResult
tagMaybe Nothing = NothingResult
tagMaybe (Just _) = JustResult

-- | Checks every read the reference @AssertState@ helper checks, against one
-- expected list model.
assertState :: (Eq a, Show a) => String -> [a] -> Queue.AncestralSliceQueue a -> IO ()
assertState label expected queue = do
  assertContents label expected queue
  forM_ (zip [0 :: Int ..] expected) $ \(position, value) ->
    assertEqual (label ++ " index " ++ show position) (Just value) (Queue.index position queue)
  assertEqual (label ++ " index past the end") Nothing (Queue.index (length expected) queue)
  assertEqual (label ++ " negative index") Nothing (Queue.index (-1) queue)
  when (null expected) $ do
    assertEqual (label ++ " empty removeFirst") NothingResult (tagMaybe (Queue.removeFirst queue))
    assertEqual (label ++ " empty removeLast") NothingResult (tagMaybe (Queue.removeLast queue))
    assertEqual (label ++ " empty tryRemoveFirst")
      NothingResult (tagMaybe (Queue.tryRemoveFirst queue))
    assertEqual (label ++ " empty tryRemoveLast")
      NothingResult (tagMaybe (Queue.tryRemoveLast queue))
    -- The anchored-empty rule, stated as an equation on the retained boundaries.
    assertEqual (label ++ " anchored empty") (Queue.lowDepth queue - 1) (tailDepthOf queue)

assertContents :: (Eq a, Show a) => String -> [a] -> Queue.AncestralSliceQueue a -> IO ()
assertContents label expected queue = do
  assertEqual (label ++ " count") (length expected) (Queue.count queue)
  assertEqual (label ++ " null") (null expected) (Queue.null queue)
  assertEqual (label ++ " contents") expected (Queue.toList queue)
  assertEqual (label ++ " first") (listToMaybe expected) (Queue.first queue)
  assertEqual (label ++ " last") (lastValue expected) (Queue.last queue)

-- | Checks that an operation returned a value indistinguishable from its
-- receiver: the same retained node and the same two interval boundaries, which
-- together are the whole representation.
assertReceiver :: String
               -> Queue.AncestralSliceQueue a
               -> Queue.AncestralSliceQueue a
               -> IO ()
assertReceiver label source result = do
  shares <- Queue.sharesRootWith source result
  assertBool (label ++ " retains the receiver's node") shares
  assertEqual (label ++ " retains the receiver's count") (Queue.count source) (Queue.count result)
  assertEqual (label ++ " retains the receiver's low depth")
    (Queue.lowDepth source) (Queue.lowDepth result)

-- | Checks that an operation reused the source's retained tail, so the ancestor
-- query it would otherwise have issued follows no links at all.
assertQueryFree :: String
                -> Queue.AncestralSliceQueue a
                -> Queue.AncestralSliceQueue a
                -> IO ()
assertQueryFree label source result = do
  hops <- ancestorHops label source result
  assertEqual (label ++ " ancestor hops") 0 hops
  shares <- Queue.sharesRootWith source result
  assertBool (label ++ " retains the source tail") shares

-- | Checks that an operation had to ascend the ancestry path, and that the node
-- it selected is exactly the level-ancestor answer at the result's depth.
assertQueried :: String
              -> Queue.AncestralSliceQueue a
              -> Queue.AncestralSliceQueue a
              -> IO ()
assertQueried label source result = do
  hops <- ancestorHops label source result
  assertBool (label ++ " follows ancestry links") (hops > 0)
  shares <- Queue.sharesRootWith source result
  assertBool (label ++ " selects a different tail") (not shares)

-- | Checks that an operation moved by exactly one parent link.
assertUsesParentLink :: String
                     -> Queue.AncestralSliceQueue a
                     -> Queue.AncestralSliceQueue a
                     -> IO ()
assertUsesParentLink label source result = do
  parent <- expectJust (label ++ " source parent") (Ancestor.nodeParent (Queue.tailNode source))
  same <- Ancestor.sharesNodeWith parent (Queue.tailNode result)
  assertBool (label ++ " retains the source tail's parent") same

-- | Checks that an operation published one leaf directly below the source tail.
assertExtendsTail :: String
                  -> Queue.AncestralSliceQueue a
                  -> Queue.AncestralSliceQueue a
                  -> IO ()
assertExtendsTail label source result = do
  parent <- expectJust (label ++ " result parent") (Ancestor.nodeParent (Queue.tailNode result))
  same <- Ancestor.sharesNodeWith parent (Queue.tailNode source)
  assertBool (label ++ " adds a leaf below the source tail") same

ancestorHops :: String
             -> Queue.AncestralSliceQueue a
             -> Queue.AncestralSliceQueue a
             -> IO Int
ancestorHops label source result =
  case Ancestor.ancestorAtDepthHops (Queue.tailNode source) (tailDepthOf result) of
    Nothing -> fail (label ++ ": the result tail depth is off the source path")
    Just (node, hops) -> do
      same <- Ancestor.sharesNodeWith node (Queue.tailNode result)
      assertBool (label ++ ": the result tail is the source path's node at that depth") same
      pure hops

hopsToDepth :: Queue.AncestralSliceQueue a -> Int -> Maybe Int
hopsToDepth queue depth = snd <$> Ancestor.ancestorAtDepthHops (Queue.tailNode queue) depth

tailDepthOf :: Queue.AncestralSliceQueue a -> Int
tailDepthOf queue = Ancestor.nodeDepth (Queue.tailNode queue)

drainWith :: String
          -> (Queue.AncestralSliceQueue a -> Maybe (Queue.AncestralSliceQueue a))
          -> Int
          -> Queue.AncestralSliceQueue a
          -> IO [Queue.AncestralSliceQueue a]
drainWith label step amount start = go amount start
  where
    go remaining current
      | remaining <= 0 = pure [current]
      | otherwise = do
          next <- expectJust (label ++ " drain step") (step current)
          rest <- go (remaining - 1) next
          pure (current : rest)

randomBelow :: Int -> Int -> (Int, Int)
randomBelow bound seed =
  let next = (seed * 1103515245 + 12345) `mod` 2147483648
  in (next `mod` bound, next)

pick :: Int -> [a] -> Maybe a
pick position values = listToMaybe (drop position values)

everyNth :: Int -> [a] -> [a]
everyNth stride values =
  map snd (filter (\(position, _) -> position `mod` stride == 0) (zip [0 :: Int ..] values))

lastValue :: [a] -> Maybe a
lastValue = List.foldl' (\_ value -> Just value) Nothing

expectJust :: String -> Maybe a -> IO a
expectJust _ (Just value) = pure value
expectJust label Nothing = fail (label ++ ": expected Just")

expectRight :: String -> Either String a -> IO a
expectRight _ (Right value) = pure value
expectRight label (Left message) = fail (label ++ ": expected Right, got " ++ message)

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")
