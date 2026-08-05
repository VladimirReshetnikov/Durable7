-- | Tests for the bilateral ancestral deque, including the level-ancestor
-- query ceilings and the structure-sharing identities its contract promises.
module BilateralAncestralDequeTests (run) where

import Control.Monad (forM, forM_, when)
import Data.Bits (complement, shiftL, shiftR, xor, (.&.), (.|.))
import qualified Data.List as List
import Data.Maybe (listToMaybe)
import Data.Word (Word64)

import qualified Durable7.FingerTree.BilateralAncestralDeque as Deque

-- | Runs this module's test cases, reporting each result.
run :: IO ()
run = do
  testEmptyAndStoredAbsence
  testEndpointOperations
  testReverse
  testSliceAndSplit
  testSmallConstructionWords
  testRandomizedBranches
  testQueryCeilings
  testInvalidInputs
  testValidationStatistics
  testSharing

testEmptyAndStoredAbsence :: IO ()
testEmptyAndStoredAbsence = do
  let emptyDeque = Deque.empty :: Deque.BilateralAncestralDeque Int
  assertDeque "empty" [] emptyDeque
  assertNothing "empty removeFirst" (Deque.removeFirst emptyDeque)
  assertNothing "empty removeLast" (Deque.removeLast emptyDeque)
  assertNothing "empty tryRemoveFirst" (Deque.tryRemoveFirst emptyDeque)
  assertNothing "empty tryRemoveLast" (Deque.tryRemoveLast emptyDeque)
  assertNothing "empty oversized prefix" (Deque.take 1 emptyDeque)
  assertNothing "empty oversized suffix" (Deque.drop 1 emptyDeque)
  assertNothing "empty oversized boundary" (Deque.splitAt 1 emptyDeque)
  assertNothing "empty oversized slice count" (Deque.slice 0 1 emptyDeque)
  assertNothing "empty oversized slice index" (Deque.slice 1 0 emptyDeque)

  emptyClear <- pure (Deque.clear emptyDeque)
  emptyReverse <- pure (Deque.reverse emptyDeque)
  emptyPrefix <- expectJust "empty prefix" (Deque.take 0 emptyDeque)
  emptySuffix <- expectJust "empty suffix" (Deque.drop 0 emptyDeque)
  emptySlice <- expectJust "empty slice" (Deque.slice 0 0 emptyDeque)
  (emptyLeft, emptyRight) <- expectJust "empty split" (Deque.splitAt 0 emptyDeque)
  forM_
    [ ("clear", emptyClear)
    , ("reverse", emptyReverse)
    , ("prefix", emptyPrefix)
    , ("suffix", emptySuffix)
    , ("slice", emptySlice)
    , ("split prefix", emptyLeft)
    , ("split suffix", emptyRight)
    ]
    $ \(label, produced) -> do
        assertDeque ("empty " ++ label) [] produced
        assertShares ("empty " ++ label ++ " retains the handle") emptyDeque produced

  -- A stored absent value stays distinguishable from an absent result: the C#
  -- baseline needs [MaybeNullWhen] only because null is a legal element.
  let stored =
        Deque.addLast (Just "tail") (Deque.addFirst Nothing Deque.empty)
          :: Deque.BilateralAncestralDeque (Maybe String)
  assertDeque "stored absence" [Nothing, Just "tail"] stored
  assertEqual "stored absence first" (Just Nothing) (Deque.first stored)
  assertEqual "stored absence index" (Just Nothing) (Deque.index 0 stored)
  assertEqual "stored absence past end" Nothing (Deque.index 2 stored)
  (storedValue, storedRest) <- expectJust "stored absence pop" (Deque.tryRemoveFirst stored)
  assertEqual "stored absence popped value" Nothing storedValue
  assertDeque "stored absence remainder" [Just "tail"] storedRest

  assertDeque "fromList" [1, 2, 3] (Deque.fromList [1, 2, 3 :: Int])
  assertDeque "fromList empty" ([] :: [Int]) (Deque.fromList [])
  assertDeque "fromList round trip" [1, 2, 3]
    (Deque.fromList (Deque.toList (Deque.fromList [1, 2, 3 :: Int])))

testEndpointOperations :: IO ()
testEndpointOperations = do
  let root = Deque.empty :: Deque.BilateralAncestralDeque Int
      one = Deque.addLast 10 root
      two = Deque.addLast 20 one
      three = Deque.addFirst 5 two
      four = Deque.addFirst 1 three
      five = Deque.addLast 30 four
  assertDeque "root" [] root
  assertDeque "one" [10] one
  assertDeque "two" [10, 20] two
  assertDeque "three" [5, 10, 20] three
  assertDeque "four" [1, 5, 10, 20] four
  assertDeque "five" [1, 5, 10, 20, 30] five

  withoutFirst <- expectJust "five removeFirst" (Deque.removeFirst five)
  withoutLast <- expectJust "five removeLast" (Deque.removeLast five)
  assertDeque "five without first" [5, 10, 20, 30] withoutFirst
  assertDeque "five without last" [1, 5, 10, 20] withoutLast
  assertDeque "five retained" [1, 5, 10, 20, 30] five

  let rightOnly = buildDeque [] [1, 2, 3]
  rightAfterOne <- expectJust "right-only removeFirst" (Deque.removeFirst rightOnly)
  rightAfterTwo <- expectJust "right-only removeFirst twice" (Deque.removeFirst rightAfterOne)
  rightAfterThree <- expectJust "right-only removeFirst thrice" (Deque.removeFirst rightAfterTwo)
  assertDeque "right-only after one" [2, 3] rightAfterOne
  assertDeque "right-only after two" [3] rightAfterTwo
  assertDeque "right-only after three" [] rightAfterThree
  assertDeque "right-only retained" [1, 2, 3] rightOnly

  let leftOnly = buildDeque [3, 2, 1] []
  leftAfterOne <- expectJust "left-only removeLast" (Deque.removeLast leftOnly)
  leftAfterTwo <- expectJust "left-only removeLast twice" (Deque.removeLast leftAfterOne)
  leftAfterThree <- expectJust "left-only removeLast thrice" (Deque.removeLast leftAfterTwo)
  assertDeque "left-only after one" [1, 2] leftAfterOne
  assertDeque "left-only after two" [1] leftAfterTwo
  assertDeque "left-only after three" [] leftAfterThree
  assertDeque "left-only retained" [1, 2, 3] leftOnly

  (firstValue, firstRest) <- expectJust "five tryRemoveFirst" (Deque.tryRemoveFirst five)
  assertEqual "five tryRemoveFirst value" 1 firstValue
  assertDeque "five tryRemoveFirst remainder" [5, 10, 20, 30] firstRest
  (lastValue, lastRest) <- expectJust "five tryRemoveLast" (Deque.tryRemoveLast five)
  assertEqual "five tryRemoveLast value" 30 lastValue
  assertDeque "five tryRemoveLast remainder" [1, 5, 10, 20] lastRest

  let leftBranch = Deque.addFirst (-1) two
      rightBranch = Deque.addLast 99 two
  assertDeque "left branch" [-1, 10, 20] leftBranch
  assertDeque "right branch" [10, 20, 99] rightBranch
  assertDeque "branch source retained" [10, 20] two

  let cleared = Deque.clear five
  assertDeque "cleared" [] cleared
  assertDeque "cleared regrown" [-7, 8] (Deque.addLast 8 (Deque.addFirst (-7) cleared))
  assertDeque "five retained after clear" [1, 5, 10, 20, 30] five

testReverse :: IO ()
testReverse = do
  let deque = buildDeque [3, 2, 1] [4, 5, 6, 7]
      reversed = Deque.reverse deque
      restored = Deque.reverse reversed
  assertDeque "reverse source" [1 .. 7] deque
  assertDeque "reversed" [7, 6, 5, 4, 3, 2, 1] reversed
  assertDeque "restored" [1 .. 7] restored
  assertDeque "reversed grown" [0, 7, 6, 5, 4, 3, 2, 1, 8]
    (Deque.addLast 8 (Deque.addFirst 0 reversed))
  trimmedFront <- expectJust "reversed removeFirst" (Deque.removeFirst reversed)
  trimmed <- expectJust "reversed removeLast" (Deque.removeLast trimmedFront)
  assertDeque "reversed trimmed" [6, 5, 4, 3, 2] trimmed
  assertDeque "reverse source retained" [1 .. 7] deque

  let singleton = Deque.fromList [42 :: Int]
  assertShares "singleton reverse retains the handle" singleton (Deque.reverse singleton)

testSliceAndSplit :: IO ()
testSliceAndSplit = do
  let original = buildDeque [3, 2, 1] [4, 5, 6, 7]
      expected = [1 .. 7] :: [Int]
  forM_ [0 .. length expected] $ \start ->
    forM_ [0 .. length expected - start] $ \amount -> do
      let model = take amount (drop start expected)
          label = show (start, amount)
      sliced <- expectJust ("slice " ++ label) (Deque.slice start amount original)
      assertDeque ("slice " ++ label) model sliced
      assertDeque ("slice grown " ++ label)
        ([-100 - start] ++ model ++ [100 + amount])
        (Deque.addLast (100 + amount) (Deque.addFirst (-100 - start) sliced))
      assertDeque ("slice retained " ++ label) model sliced
      assertDeque ("slice reversed and grown " ++ label)
        ([-1] ++ reverse model ++ [-2])
        (Deque.addLast (-2) (Deque.addFirst (-1) (Deque.reverse sliced)))

  forM_ [0 .. length expected] $ \boundary -> do
    let label = show boundary
        prefixModel = take boundary expected
        suffixModel = drop boundary expected
    (prefix, suffix) <- expectJust ("split " ++ label) (Deque.splitAt boundary original)
    assertDeque ("split prefix " ++ label) prefixModel prefix
    assertDeque ("split suffix " ++ label) suffixModel suffix
    assertDeque ("split prefix grown " ++ label) (prefixModel ++ [88]) (Deque.addLast 88 prefix)
    assertDeque ("split suffix grown " ++ label) (77 : suffixModel) (Deque.addFirst 77 suffix)

  assertDeque "slice source retained" expected original

testSmallConstructionWords :: IO ()
testSmallConstructionWords =
  forM_ [0 .. 8 :: Int] $ \lengthValue ->
    forM_ [0 .. (1 `shiftL` lengthValue) - 1] $ \word -> do
      let (deque, model) = buildWord lengthValue word
          wordLabel = show (lengthValue, word)
      assertDeque ("word " ++ wordLabel) model deque
      forM_ [0 .. lengthValue] $ \start ->
        forM_ [0 .. lengthValue - start] $ \amount -> do
          let sliceModel = take amount (drop start model)
              sliceLabel = wordLabel ++ " " ++ show (start, amount)
          sliced <- expectJust ("word slice " ++ sliceLabel) (Deque.slice start amount deque)
          assertDeque ("word slice " ++ sliceLabel) sliceModel sliced
          assertDeque ("word slice grown " ++ sliceLabel)
            ([-1] ++ sliceModel ++ [-2])
            (Deque.addLast (-2) (Deque.addFirst (-1) sliced))
      forM_ [0 .. lengthValue] $ \boundary -> do
        let boundaryLabel = wordLabel ++ " " ++ show boundary
        (prefix, suffix) <- expectJust ("word split " ++ boundaryLabel) (Deque.splitAt boundary deque)
        prefixOnly <- expectJust ("word prefix " ++ boundaryLabel) (Deque.take boundary deque)
        suffixOnly <- expectJust ("word suffix " ++ boundaryLabel) (Deque.drop boundary deque)
        assertDeque ("word split prefix " ++ boundaryLabel) (take boundary model) prefix
        assertDeque ("word split suffix " ++ boundaryLabel) (drop boundary model) suffix
        assertDeque ("word prefix " ++ boundaryLabel) (take boundary model) prefixOnly
        assertDeque ("word suffix " ++ boundaryLabel) (drop boundary model) suffixOnly

testRandomizedBranches :: IO ()
testRandomizedBranches =
  forM_ [0, 1, 17, 91, 8675309 :: Int] $ \seed -> do
    versions <- walk seed 0 (newRandom seed) [(Deque.empty, [])]
    forM_ (zip [0 :: Int ..] versions) $ \(position, (retained, model)) ->
      when (position `mod` 23 == 0) $
        assertDeque ("randomized retained " ++ show (seed, position)) model retained
    let (root, rootModel) = List.last versions
    assertDeque ("randomized root " ++ show seed) rootModel root
  where
    stepLimit = 240 :: Int

    walk seed step generator versions
      | step == stepLimit = pure versions
      | otherwise = do
          let (sourceIndex, afterSource) = nextBelow (length versions) generator
              (source, sourceModel) = versions !! sourceIndex
              modelLength = length sourceModel
              value = seed + step * 31
              (choice, afterChoice) = nextBelow 11 afterSource
              label = show (seed, step)
          assertDeque ("randomized source " ++ label) sourceModel source
          (result, model, generator') <-
            case choice of
              0 -> pure (Deque.addFirst value source, value : sourceModel, afterChoice)
              1 -> pure (Deque.addLast value source, sourceModel ++ [value], afterChoice)
              2 | modelLength /= 0 -> do
                    shortened <- expectJust ("randomized removeFirst " ++ label)
                      (Deque.removeFirst source)
                    pure (shortened, drop 1 sourceModel, afterChoice)
              3 | modelLength /= 0 -> do
                    shortened <- expectJust ("randomized removeLast " ++ label)
                      (Deque.removeLast source)
                    pure (shortened, take (modelLength - 1) sourceModel, afterChoice)
              4 -> pure (Deque.reverse source, reverse sourceModel, afterChoice)
              5 -> do
                    let (start, afterStart) = nextBelow (modelLength + 1) afterChoice
                        (amount, afterAmount) = nextBelow (modelLength - start + 1) afterStart
                    sliced <- expectJust ("randomized slice " ++ label)
                      (Deque.slice start amount source)
                    pure (sliced, take amount (drop start sourceModel), afterAmount)
              6 -> do
                    let (boundary, afterBoundary) = nextBelow (modelLength + 1) afterChoice
                        (side, afterSide) = nextBelow 2 afterBoundary
                    (prefix, suffix) <- expectJust ("randomized split " ++ label)
                      (Deque.splitAt boundary source)
                    if side == 0
                      then pure (prefix, take boundary sourceModel, afterSide)
                      else pure (suffix, drop boundary sourceModel, afterSide)
              7 -> do
                    let (amount, afterAmount) = nextBelow (modelLength + 1) afterChoice
                    prefix <- expectJust ("randomized take " ++ label) (Deque.take amount source)
                    pure (prefix, take amount sourceModel, afterAmount)
              8 -> do
                    let (amount, afterAmount) = nextBelow (modelLength + 1) afterChoice
                    suffix <- expectJust ("randomized drop " ++ label) (Deque.drop amount source)
                    pure (suffix, drop amount sourceModel, afterAmount)
              9 -> pure (Deque.clear source, [], afterChoice)
              _ ->
                pure
                  ( Deque.addLast (complement value) (Deque.reverse (Deque.addFirst value source))
                  , reverse (value : sourceModel) ++ [complement value]
                  , afterChoice
                  )
          assertDeque ("randomized result " ++ label) model result
          assertDeque ("randomized source retained " ++ label) sourceModel source
          walk seed (step + 1) generator' ((result, model) : versions)

testQueryCeilings :: IO ()
testQueryCeilings = do
  let deque = buildDeque [4, 3, 2, 1] [5, 6, 7, 8]
      total = Deque.count deque
  assertDeque "ceiling base" [1 .. 8] deque

  -- The four cached logical endpoints are answered with no query at all.
  forM_ [(0, 1), (3, 4), (4, 5), (7, 8)] $ \(position, value) -> do
    (found, cost) <- expectJust ("cached index " ++ show position)
      (Deque.indexCost position deque)
    assertEqual ("cached index value " ++ show position) value found
    assertQueriesAtMost ("cached index " ++ show position) 0 cost

  indexQueries <- forM [0 .. total - 1] $ \position -> do
    (found, cost) <- expectJust ("index " ++ show position) (Deque.indexCost position deque)
    assertEqual ("index value " ++ show position) (position + 1) found
    assertQueriesAtMost ("index " ++ show position) 1 cost
    pure (Deque.queryCostQueries cost)
  -- Exactly the four cached logical endpoints answer for free; every interior
  -- position spends its one query. Pinning the profile keeps the ceilings above
  -- from passing vacuously.
  assertEqual "index query profile" [0, 1, 1, 0, 0, 1, 1, 0] indexQueries

  sliceQueries <- forM [(start, amount) | start <- [0 .. total], amount <- [0 .. total - start]]
    $ \(start, amount) -> do
        let label = show (start, amount)
        (sliced, cost) <- expectJust ("slice " ++ label) (Deque.sliceCost start amount deque)
        assertEqual ("slice contents " ++ label)
          (take amount (drop start [1 .. 8])) (Deque.toList sliced)
        assertQueriesAtMost ("slice " ++ label) 2 cost
        pure (Deque.queryCostQueries cost)
  assertEqual "the slice ceiling of two is reached" 2 (maximum sliceQueries)

  splitQueries <- forM [0 .. total] $ \boundary -> do
    let label = show boundary
    ((prefix, suffix), cost) <- expectJust ("split " ++ label) (Deque.splitAtCost boundary deque)
    assertEqual ("split prefix " ++ label) (take boundary [1 .. 8]) (Deque.toList prefix)
    assertEqual ("split suffix " ++ label) (drop boundary [1 .. 8]) (Deque.toList suffix)
    assertQueriesAtMost ("split " ++ label) 2 cost
    pure (Deque.queryCostQueries cost)
  -- Both endpoint boundaries and the centre boundary are free; the rest reach
  -- but never exceed two across the prefix/suffix pair.
  assertEqual "split query profile" [0, 1, 2, 2, 0, 1, 2, 2, 0] splitQueries

  -- The whole-deque slice and the empty slice both cost nothing.
  (wholeSlice, wholeCost) <- expectJust "whole slice" (Deque.sliceCost 0 total deque)
  assertQueriesAtMost "whole slice" 0 wholeCost
  assertShares "whole slice retains the handle" deque wholeSlice
  (_, emptyCost) <- expectJust "empty slice" (Deque.sliceCost 3 0 deque)
  assertQueriesAtMost "empty slice" 0 emptyCost

  -- Removal on its own nonempty arm makes no query at all.
  (leftOwn, leftCost) <- expectJust "own-arm removeFirst" (Deque.removeFirstCost deque)
  assertQueriesAtMost "own-arm removeFirst" 0 leftCost
  assertDeque "own-arm removeFirst" [2 .. 8] leftOwn
  (rightOwn, rightCost) <- expectJust "own-arm removeLast" (Deque.removeLastCost deque)
  assertQueriesAtMost "own-arm removeLast" 0 rightCost
  assertDeque "own-arm removeLast" [1 .. 7] rightOwn

  -- Removal that crosses the centre makes at most one. The last two steps are
  -- free: they take RemoveBase's count-of-two shortcut and then the
  -- whole-deque empty shortcut, so only the deeper steps select a new cached
  -- base through an ancestor query.
  rightDrain <- drainFirst "right-only" (buildDeque [] [10, 11, 12, 13])
  assertEqual "cross-centre removeFirst profile" [1, 1, 0, 0] rightDrain
  leftDrain <- drainLast "left-only" (buildDeque [13, 12, 11, 10] [])
  assertEqual "cross-centre removeLast profile" [1, 1, 0, 0] leftDrain

testInvalidInputs :: IO ()
testInvalidInputs = do
  let deque = Deque.fromList [1, 2, 3 :: Int]
  assertEqual "index below start" Nothing (Deque.index (-1) deque)
  assertEqual "index past end" Nothing (Deque.index 3 deque)
  assertEqual "index at maxBound" Nothing (Deque.index maxBound deque)
  assertNothing "negative prefix" (Deque.take (-1) deque)
  assertNothing "oversized prefix" (Deque.take 4 deque)
  assertNothing "negative suffix" (Deque.drop (-1) deque)
  assertNothing "oversized suffix" (Deque.drop 4 deque)
  assertNothing "negative boundary" (Deque.splitAt (-1) deque)
  assertNothing "oversized boundary" (Deque.splitAt 4 deque)
  assertNothing "negative slice index" (Deque.slice (-1) 0 deque)
  assertNothing "negative slice count" (Deque.slice 0 (-1) deque)
  assertNothing "slice index past end" (Deque.slice 4 0 deque)
  assertNothing "slice count past end" (Deque.slice 2 2 deque)
  assertNothing "slice index at maxBound" (Deque.slice maxBound 1 deque)
  assertNothing "slice count at maxBound" (Deque.slice 1 maxBound deque)
  assertNothing "instrumented negative slice" (Deque.sliceCost (-1) 0 deque)
  assertNothing "instrumented oversized boundary" (Deque.splitAtCost 4 deque)
  assertNothing "instrumented index past end" (Deque.indexCost 3 deque)
  assertDeque "rejected boundaries leave the deque usable" [1, 2, 3] deque

testValidationStatistics :: IO ()
testValidationStatistics = do
  let deque = buildDeque [2, 1] [3]
  assertEqual "bilateral statistics"
    (Right (Deque.BilateralStatistics 3 2 1))
    (Deque.validateStructure deque)
  assertEqual "empty statistics"
    (Right (Deque.BilateralStatistics 0 0 0))
    (Deque.validateStructure (Deque.empty :: Deque.BilateralAncestralDeque Int))

  rightArmOnly <- expectJust "right-arm slice" (Deque.slice 2 1 deque)
  assertEqual "right-arm statistics"
    (Right (Deque.BilateralStatistics 1 0 1))
    (Deque.validateStructure rightArmOnly)
  leftArmOnly <- expectJust "left-arm slice" (Deque.slice 0 2 deque)
  assertEqual "left-arm statistics"
    (Right (Deque.BilateralStatistics 2 2 0))
    (Deque.validateStructure leftArmOnly)
  assertEqual "reversed statistics"
    (Right (Deque.BilateralStatistics 3 1 2))
    (Deque.validateStructure (Deque.reverse deque))

  -- Every arm shape reachable from an interior slice audits cleanly.
  let wide = buildDeque [4, 3, 2, 1] [5, 6, 7, 8]
  forM_ [0 .. 8] $ \start ->
    forM_ [0 .. 8 - start] $ \amount -> do
      sliced <- expectJust "audited slice" (Deque.slice start amount wide)
      case Deque.validateStructure sliced of
        Left message -> fail ("audited slice " ++ show (start, amount) ++ ": " ++ message)
        Right statistics ->
          assertEqual ("audited slice count " ++ show (start, amount))
            amount (Deque.bilateralCount statistics)

testSharing :: IO ()
testSharing = do
  let deque = buildDeque [3, 2, 1] [4, 5, 6, 7]
      total = Deque.count deque
  wholeSlice <- expectJust "whole slice" (Deque.slice 0 total deque)
  wholePrefix <- expectJust "whole prefix" (Deque.take total deque)
  zeroSuffix <- expectJust "zero suffix" (Deque.drop 0 deque)
  assertShares "whole slice retains the handle" deque wholeSlice
  assertShares "whole prefix retains the handle" deque wholePrefix
  assertShares "zero suffix retains the handle" deque zeroSuffix

  (startPrefix, startSuffix) <- expectJust "start split" (Deque.splitAt 0 deque)
  assertShares "start split retains the handle as its suffix" deque startSuffix
  assertDeque "start split prefix" [] startPrefix
  (endPrefix, endSuffix) <- expectJust "end split" (Deque.splitAt total deque)
  assertShares "end split retains the handle as its prefix" deque endPrefix
  assertDeque "end split suffix" [] endSuffix

  let emptyDeque = Deque.empty :: Deque.BilateralAncestralDeque Int
  assertShares "empty clear retains the handle" emptyDeque (Deque.clear emptyDeque)
  drained <- expectJust "drained handle" (Deque.removeFirst (Deque.fromList [42 :: Int]))
  assertDeque "drained handle" [] drained
  assertShares "clear on a drained handle retains it" drained (Deque.clear drained)

  reversedShares <- Deque.sharesRootWith deque (Deque.reverse deque)
  assertBool "reversing more than one value produces a new handle" (not reversedShares)
  clearedShares <- Deque.sharesRootWith deque (Deque.clear deque)
  assertBool "clearing a nonempty handle produces a new handle" (not clearedShares)
  interiorShares <- do
    interior <- expectJust "interior slice" (Deque.slice 1 3 deque)
    Deque.sharesRootWith deque interior
  assertBool "an interior slice produces a new handle" (not interiorShares)
  -- Equal contents and equal arm shapes are not sharing: dropping the back
  -- value and publishing it again produces a distinct right tail. Comparing
  -- two syntactically identical constructions instead would only observe the
  -- compiler's common-subexpression elimination.
  republishedShares <- do
    shortened <- expectJust "shortened handle" (Deque.removeLast deque)
    let regrown = Deque.addLast 7 shortened
    assertDeque "republished endpoint" [1 .. 7] regrown
    Deque.sharesRootWith deque regrown
  assertBool "a republished endpoint does not share" (not republishedShares)

-- | Removes every value from the front, checking the one-query ceiling at each
-- step, and reports the query count each step spent.
drainFirst :: String -> Deque.BilateralAncestralDeque Int -> IO [Int]
drainFirst label deque
  | Deque.null deque = pure []
  | otherwise = do
      (rest, cost) <- expectJust (label ++ " removeFirst") (Deque.removeFirstCost deque)
      assertQueriesAtMost (label ++ " removeFirst") 1 cost
      assertDeque (label ++ " removeFirst") (drop 1 (Deque.toList deque)) rest
      remaining <- drainFirst label rest
      pure (Deque.queryCostQueries cost : remaining)

-- | Removes every value from the back, checking the one-query ceiling at each
-- step, and reports the query count each step spent.
drainLast :: String -> Deque.BilateralAncestralDeque Int -> IO [Int]
drainLast label deque
  | Deque.null deque = pure []
  | otherwise = do
      (rest, cost) <- expectJust (label ++ " removeLast") (Deque.removeLastCost deque)
      assertQueriesAtMost (label ++ " removeLast") 1 cost
      let model = Deque.toList deque
      assertDeque (label ++ " removeLast") (take (length model - 1) model) rest
      remaining <- drainLast label rest
      pure (Deque.queryCostQueries cost : remaining)

buildDeque :: [Int] -> [Int] -> Deque.BilateralAncestralDeque Int
buildDeque fronts backs =
  foldl (flip Deque.addLast) (foldl (flip Deque.addFirst) Deque.empty fronts) backs

buildWord :: Int -> Int -> (Deque.BilateralAncestralDeque Int, [Int])
buildWord lengthValue word = go 0 Deque.empty []
  where
    go position deque model
      | position == lengthValue = (deque, model)
      | word .&. (1 `shiftL` position) == 0 =
          go (position + 1) (Deque.addFirst position deque) (position : model)
      | otherwise = go (position + 1) (Deque.addLast position deque) (model ++ [position])

-- | A deterministic xorshift generator; the tests must not depend on a
-- generator package.
newtype Random = Random Word64

newRandom :: Int -> Random
newRandom seed = Random (fromIntegral seed .|. 1)

nextBelow :: Int -> Random -> (Int, Random)
nextBelow boundValue (Random state) =
  (fromIntegral (shuffled `mod` fromIntegral boundValue), Random shuffled)
  where
    stepOne = state `xor` (state `shiftL` 13)
    stepTwo = stepOne `xor` (stepOne `shiftR` 7)
    shuffled = stepTwo `xor` (stepTwo `shiftL` 17)

assertDeque :: (Eq a, Show a) => String -> [a] -> Deque.BilateralAncestralDeque a -> IO ()
assertDeque label expected deque = do
  assertEqual (label ++ " contents") expected (Deque.toList deque)
  assertEqual (label ++ " count") (length expected) (Deque.count deque)
  assertEqual (label ++ " emptiness") (null expected) (Deque.null deque)
  forM_ (zip [0 :: Int ..] expected) $ \(position, value) ->
    assertEqual (label ++ " index " ++ show position) (Just value) (Deque.index position deque)
  assertEqual (label ++ " index past end") Nothing (Deque.index (length expected) deque)
  assertEqual (label ++ " index below start") Nothing (Deque.index (-1) deque)
  assertEqual (label ++ " first") (listToMaybe expected) (Deque.first deque)
  assertEqual (label ++ " last") (listToMaybe (reverse expected)) (Deque.last deque)
  case Deque.validateStructure deque of
    Left message -> fail (label ++ ": structural validation failed: " ++ message)
    Right statistics -> do
      assertEqual (label ++ " validated count") (length expected)
        (Deque.bilateralCount statistics)
      assertEqual (label ++ " validated arm counts") (length expected)
        (Deque.bilateralLeftCount statistics + Deque.bilateralRightCount statistics)

assertQueriesAtMost :: String -> Int -> Deque.QueryCost -> IO ()
assertQueriesAtMost label maximumQueries cost = do
  assertBool
    (label ++ ": made " ++ show (Deque.queryCostQueries cost)
      ++ " level-ancestor queries but at most " ++ show maximumQueries ++ " are allowed")
    (Deque.queryCostQueries cost <= maximumQueries)
  assertBool (label ++ ": reported a negative query count") (Deque.queryCostQueries cost >= 0)
  assertBool (label ++ ": reported a negative hop count") (Deque.queryCostHops cost >= 0)
  assertBool (label ++ ": reported ancestry hops without issuing a query")
    (Deque.queryCostQueries cost /= 0 || Deque.queryCostHops cost == 0)

assertShares
  :: String
  -> Deque.BilateralAncestralDeque a
  -> Deque.BilateralAncestralDeque a
  -> IO ()
assertShares label left right = do
  shares <- Deque.sharesRootWith left right
  assertBool (label ++ ": expected the same retained handle") shares

assertNothing :: String -> Maybe a -> IO ()
assertNothing _ Nothing = pure ()
assertNothing label (Just _) = fail (label ++ ": expected Nothing")

expectJust :: String -> Maybe a -> IO a
expectJust _ (Just value) = pure value
expectJust label Nothing = fail (label ++ ": expected Just")

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")
