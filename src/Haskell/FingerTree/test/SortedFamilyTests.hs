{-# LANGUAGE NumericUnderscores #-}

-- | Tests for the finger-tree sorted set and sorted map, including the complexity probes that pin
-- the bounds the census requires: comparison-free digit-read extremes, position-independent
-- structural rank slices, logarithmic point writes, run-adopting set algebra, and the two-split
-- key-range budget the persistent delta map's callback accounting depends on.
module SortedFamilyTests (run) where

import Control.Exception (evaluate)
import Control.Monad (forM_, unless)
import Data.IORef (IORef, modifyIORef', newIORef, readIORef, writeIORef)
import Data.Int (Int64)
import qualified Data.List as List
import qualified Data.Map.Strict as Map
import qualified Data.Set as Set
import Data.Word (Word64)
import System.IO.Unsafe (unsafePerformIO)
import System.Mem (getAllocationCounter)

import qualified Durable7.FingerTree.SortedMap as SortedMap
import qualified Durable7.FingerTree.SortedSet as SortedSet

-- | Runs this module's test cases, reporting each result.
run :: IO ()
run = do
  testSetBehavior
  testSetAlgebraSemantics
  testMapBehavior
  testRandomizedSetModel
  testRandomizedMapModel
  testExtremesAreDigitReads
  testSliceIsStructural
  testWriteLogScaling
  testSetAlgebraAdoptsRuns
  testKeyRangeBudget

-- Point queries, neighbors, ranks, and slices agree with the ordered contract.
testSetBehavior :: IO ()
testSetBehavior = do
  let set = SortedSet.fromList [5 :: Int, 1, 9, 3, 7]
  assertEqual "set toList" [1, 3, 5, 7, 9] (SortedSet.toList set)
  assertEqual "set count" 5 (SortedSet.count set)
  assertBool "set nonempty" (not (SortedSet.null set))
  assertBool "set empty" (SortedSet.null (SortedSet.empty :: SortedSet.SortedSet Int))
  assertEqual "set member hit" True (SortedSet.member 7 set)
  assertEqual "set member miss" False (SortedSet.member 4 set)
  assertEqual "set min" (Just 1) (SortedSet.minValue set)
  assertEqual "set max" (Just 9) (SortedSet.maxValue set)
  assertEqual "empty min" (Nothing :: Maybe Int) (SortedSet.minValue SortedSet.empty)
  assertEqual "empty max" (Nothing :: Maybe Int) (SortedSet.maxValue SortedSet.empty)
  assertEqual "set floor hit" (Just 5) (SortedSet.floor 5 set)
  assertEqual "set floor gap" (Just 5) (SortedSet.floor 6 set)
  assertEqual "set floor below" Nothing (SortedSet.floor 0 set)
  assertEqual "set ceiling hit" (Just 5) (SortedSet.ceiling 5 set)
  assertEqual "set ceiling gap" (Just 7) (SortedSet.ceiling 6 set)
  assertEqual "set ceiling above" Nothing (SortedSet.ceiling 10 set)
  assertEqual "set lower" (Just 3) (SortedSet.lower 5 set)
  assertEqual "set lower below" Nothing (SortedSet.lower 1 set)
  assertEqual "set higher" (Just 7) (SortedSet.higher 5 set)
  assertEqual "set higher above" Nothing (SortedSet.higher 9 set)
  assertEqual "set index" (Just 7) (SortedSet.index 3 set)
  assertEqual "set index out of range" Nothing (SortedSet.index 5 set)
  assertEqual "set indexOf" (Just 3) (SortedSet.indexOf 7 set)
  assertEqual "set indexOf miss" Nothing (SortedSet.indexOf 6 set)
  assertEqual "set countLessThan" 2 (SortedSet.countLessThan 5 set)
  assertEqual "set countAtMost" 3 (SortedSet.countAtMost 5 set)
  assertEqual "set slice" (Just [3, 5, 7]) (SortedSet.toList <$> SortedSet.slice 1 3 set)
  assertEqual "set empty slice" (Just []) (SortedSet.toList <$> SortedSet.slice 2 0 set)
  assertEqual "set invalid slice" Nothing (SortedSet.slice 3 3 set)
  assertEqual "set delete" [1, 3, 7, 9] (SortedSet.toList (SortedSet.delete 5 set))
  assertEqual "set delete miss" [1, 3, 5, 7, 9] (SortedSet.toList (SortedSet.delete 4 set))
  assertEqual "set duplicate insert" [1, 3, 5, 7, 9] (SortedSet.toList (SortedSet.insert 3 set))
  -- The first stored instance of an equivalence class is the retained representative.
  let firstWins = SortedSet.insert (Labeled 1 "new") (SortedSet.singleton (Labeled 1 "old"))
  assertEqual "set first instance wins" ["old"] (map labelOf (SortedSet.toList firstWins))

-- The set algebra matches the reference semantics, including the left operand's representative
-- winning wherever both sets hold an equivalent element.
testSetAlgebraSemantics :: IO ()
testSetAlgebraSemantics = do
  let left = SortedSet.fromList [1 :: Int, 3, 5, 7]
      right = SortedSet.fromList [3, 4, 7, 10]
  assertEqual "union" [1, 3, 4, 5, 7, 10] (SortedSet.toList (SortedSet.union left right))
  assertEqual "intersection" [3, 7] (SortedSet.toList (SortedSet.intersection left right))
  assertEqual "difference" [1, 5] (SortedSet.toList (SortedSet.difference left right))
  assertEqual "symmetric difference" [1, 4, 5, 10]
    (SortedSet.toList (SortedSet.symmetricDifference left right))
  assertBool "overlaps" (SortedSet.overlaps left right)
  assertBool "no overlap"
    (not (SortedSet.overlaps left (SortedSet.fromList [2 :: Int, 6])))
  assertBool "subset" (SortedSet.isSubsetOf (SortedSet.fromList [3 :: Int, 7]) left)
  assertBool "not subset" (not (SortedSet.isSubsetOf right left))
  assertBool "proper subset" (SortedSet.isProperSubsetOf (SortedSet.fromList [3 :: Int, 7]) left)
  assertBool "not proper subset" (not (SortedSet.isProperSubsetOf left left))
  assertBool "superset" (SortedSet.isSupersetOf left (SortedSet.fromList [1 :: Int, 5]))
  assertBool "proper superset" (SortedSet.isProperSupersetOf left (SortedSet.fromList [1 :: Int, 5]))
  assertBool "set equals" (SortedSet.setEquals left (SortedSet.fromList [7 :: Int, 5, 3, 1]))
  assertBool "set not equals" (not (SortedSet.setEquals left right))
  assertBool "empty union identity"
    (SortedSet.setEquals left (SortedSet.union left SortedSet.empty))
  assertBool "empty union identity flipped"
    (SortedSet.setEquals left (SortedSet.union SortedSet.empty left))
  let unionBias = SortedSet.union (SortedSet.singleton (Labeled 1 "L")) (SortedSet.singleton (Labeled 1 "R"))
  assertEqual "union keeps the left representative" ["L"] (map labelOf (SortedSet.toList unionBias))
  let intersectionBias =
        SortedSet.intersection
          (SortedSet.fromList [Labeled 1 "L1", Labeled 2 "L2"])
          (SortedSet.singleton (Labeled 2 "R2"))
  assertEqual "intersection takes from the left set" ["L2"]
    (map labelOf (SortedSet.toList intersectionBias))

-- Map point writes, neighbor queries, ranks, windows, and representative rules.
testMapBehavior :: IO ()
testMapBehavior = do
  let mapValue = SortedMap.fromList [(5 :: Int, "five"), (1, "one"), (9, "nine"), (3, "three")]
  assertEqual "map toList" [(1, "one"), (3, "three"), (5, "five"), (9, "nine")]
    (SortedMap.toList mapValue)
  assertEqual "map keys" [1, 3, 5, 9] (SortedMap.keys mapValue)
  assertEqual "map elems" ["one", "three", "five", "nine"] (SortedMap.elems mapValue)
  assertEqual "map count" 4 (SortedMap.count mapValue)
  assertBool "map empty" (SortedMap.null (SortedMap.empty :: SortedMap.SortedMap Int Int))
  assertEqual "map lookup hit" (Just "three") (SortedMap.lookup 3 mapValue)
  assertEqual "map lookup miss" Nothing (SortedMap.lookup 4 mapValue)
  assertEqual "map findWithDefault" "none" (SortedMap.findWithDefault "none" 4 mapValue)
  assertEqual "map replace" (Just "THREE") (SortedMap.lookup 3 (SortedMap.insert 3 "THREE" mapValue))
  assertEqual "map insertNew rejects" Nothing (SortedMap.insertNew 3 "again" mapValue)
  assertEqual "map insertNew accepts" (Just (Just "four"))
    (SortedMap.lookup 4 <$> SortedMap.insertNew 4 "four" mapValue)
  assertEqual "map delete" [1, 5, 9] (SortedMap.keys (SortedMap.delete 3 mapValue))
  assertEqual "map delete miss" 4 (SortedMap.count (SortedMap.delete 4 mapValue))
  assertEqual "map min" (Just (1, "one")) (SortedMap.minEntry mapValue)
  assertEqual "map max" (Just (9, "nine")) (SortedMap.maxEntry mapValue)
  assertEqual "map floor gap" (Just (3, "three")) (SortedMap.floorEntry 4 mapValue)
  assertEqual "map floor below" Nothing (SortedMap.floorEntry 0 mapValue)
  assertEqual "map ceiling gap" (Just (5, "five")) (SortedMap.ceilingEntry 4 mapValue)
  assertEqual "map ceiling above" Nothing (SortedMap.ceilingEntry 10 mapValue)
  assertEqual "map lower" (Just (1, "one")) (SortedMap.lowerEntry 3 mapValue)
  assertEqual "map higher" (Just (5, "five")) (SortedMap.higherEntry 3 mapValue)
  assertEqual "map index" (Just (5, "five")) (SortedMap.index 2 mapValue)
  assertEqual "map index out of range" Nothing (SortedMap.index 4 mapValue)
  assertEqual "map indexOfKey" (Just 2) (SortedMap.indexOfKey 5 mapValue)
  assertEqual "map indexOfKey miss" Nothing (SortedMap.indexOfKey 4 mapValue)
  assertEqual "map countKeysLessThan" 2 (SortedMap.countKeysLessThan 5 mapValue)
  assertEqual "map countKeysAtMost" 3 (SortedMap.countKeysAtMost 5 mapValue)
  assertEqual "map slice" (Just [(3, "three"), (5, "five")])
    (SortedMap.toList <$> SortedMap.slice 1 2 mapValue)
  assertEqual "map invalid slice" Nothing (SortedMap.slice 2 3 mapValue)
  assertEqual "map key range" [(3, "three"), (5, "five")]
    (SortedMap.toList (SortedMap.keyRange 2 5 mapValue))
  assertEqual "map key range inverted" [] (SortedMap.toList (SortedMap.keyRange 5 2 mapValue))
  assertEqual "map key range empty window" [] (SortedMap.toList (SortedMap.keyRange 6 8 mapValue))
  assertEqual "map key range spanning" 4 (SortedMap.count (SortedMap.keyRange (-5) 50 mapValue))
  -- The supplied entry replaces both the stored key representative and the value, so the last
  -- equivalent entry wins during construction — the rule the delta map's representative
  -- accounting builds on.
  let represented = SortedMap.fromList [(Labeled 1 "first", 'a'), (Labeled 1 "second", 'b')]
  assertEqual "map last representative wins" ["second"]
    (map labelOf (SortedMap.keys represented))
  -- Range probes select boundaries and are never stored.
  let ranged = SortedMap.keyRange (Labeled 1 "probe") (Labeled 1 "probe") represented
  assertEqual "map key range retains representatives" ["second"]
    (map labelOf (SortedMap.keys ranged))

-- A randomized history checked against Data.Set, with retained snapshots proving persistence and
-- periodic algebra checks against the model.
testRandomizedSetModel :: IO ()
testRandomizedSetModel = do
  walk 0 0x5eed_f00d SortedSet.empty Set.empty []
  where
    steps = 2_000 :: Int

    walk step state set model retained
      | step >= steps = do
          assertMatches "final" set model
          forM_ retained $ \(index, snapshot, snapshotModel) ->
            assertMatches ("retained " ++ show index) snapshot snapshotModel
      | otherwise = do
          let (choice, state1) = randomBelow state 100
              (offset, state2) = randomBelow state1 201
              value = offset - 100
          (nextSet, nextModel) <-
            if choice < 45
              then pure (SortedSet.insert value set, Set.insert value model)
              else if choice < 70
                then pure (SortedSet.delete value set, Set.delete value model)
                else if choice < 85
                  then do
                    assertEqual "model member" (Set.member value model) (SortedSet.member value set)
                    assertEqual "model floor" (Set.lookupLE value model) (SortedSet.floor value set)
                    assertEqual "model ceiling" (Set.lookupGE value model) (SortedSet.ceiling value set)
                    assertEqual "model lower" (Set.lookupLT value model) (SortedSet.lower value set)
                    assertEqual "model higher" (Set.lookupGT value model) (SortedSet.higher value set)
                    assertEqual "model countLessThan"
                      (Set.size (fst (Set.split value model)))
                      (SortedSet.countLessThan value set)
                    pure (set, model)
                  else do
                    let position = offset `mod` (Set.size model + 1)
                        expected = Just (take 3 (drop position (Set.toAscList model)))
                        lengthValue = min 3 (Set.size model - position)
                    assertEqual "model index"
                      (List.lookup position (zip [0 ..] (Set.toAscList model)))
                      (SortedSet.index position set)
                    assertEqual "model slice"
                      ((take lengthValue <$> expected))
                      (SortedSet.toList <$> SortedSet.slice position lengthValue set)
                    pure (set, model)
          if step `mod` 200 == 0
            then assertMatches ("step " ++ show step) nextSet nextModel
            else pure ()
          let nextRetained
                | step `mod` 250 == 0 = (step, nextSet, nextModel) : retained
                | otherwise = retained
          forM_ [snapshot | (index, snapshot, _) <- nextRetained, index + 500 == step] $ \snapshot -> do
            let snapshotModel = Set.fromList (SortedSet.toList snapshot)
            assertEqual "model union"
              (Set.toAscList (Set.union nextModel snapshotModel))
              (SortedSet.toList (SortedSet.union nextSet snapshot))
            assertEqual "model intersection"
              (Set.toAscList (Set.intersection nextModel snapshotModel))
              (SortedSet.toList (SortedSet.intersection nextSet snapshot))
            assertEqual "model difference"
              (Set.toAscList (Set.difference nextModel snapshotModel))
              (SortedSet.toList (SortedSet.difference nextSet snapshot))
            assertEqual "model symmetric difference"
              (Set.toAscList
                (Set.difference (Set.union nextModel snapshotModel) (Set.intersection nextModel snapshotModel)))
              (SortedSet.toList (SortedSet.symmetricDifference nextSet snapshot))
            assertEqual "model subset"
              (Set.isSubsetOf snapshotModel nextModel)
              (SortedSet.isSubsetOf snapshot nextSet)
            assertEqual "model overlap"
              (not (Set.null (Set.intersection nextModel snapshotModel)))
              (SortedSet.overlaps nextSet snapshot)
          walk (step + 1) state2 nextSet nextModel nextRetained

    assertMatches label set model = do
      assertEqual (label ++ " contents") (Set.toAscList model) (SortedSet.toList set)
      assertEqual (label ++ " count") (Set.size model) (SortedSet.count set)
      assertEqual (label ++ " emptiness") (Set.null model) (SortedSet.null set)
      assertEqual (label ++ " min") (Set.lookupMin model) (SortedSet.minValue set)
      assertEqual (label ++ " max") (Set.lookupMax model) (SortedSet.maxValue set)

-- A randomized history checked against Data.Map.Strict, with retained snapshots.
testRandomizedMapModel :: IO ()
testRandomizedMapModel = do
  walk 0 0xfab1e_5eed SortedMap.empty Map.empty []
  where
    steps = 2_000 :: Int

    walk :: Int -> Word64 -> SortedMap.SortedMap Int Int -> Map.Map Int Int
         -> [(Int, SortedMap.SortedMap Int Int, Map.Map Int Int)] -> IO ()
    walk step state mapValue model retained
      | step >= steps = do
          assertMatches "final" mapValue model
          forM_ retained $ \(index, snapshot, snapshotModel) ->
            assertMatches ("retained " ++ show index) snapshot snapshotModel
      | otherwise = do
          let (choice, state1) = randomBelow state 100
              (offset, state2) = randomBelow state1 201
              key = offset - 100
          (nextMap, nextModel) <-
            if choice < 45
              then pure (SortedMap.insert key step mapValue, Map.insert key step model)
              else if choice < 70
                then pure (SortedMap.delete key mapValue, Map.delete key model)
                else if choice < 85
                  then do
                    assertEqual "map model member" (Map.member key model) (SortedMap.member key mapValue)
                    assertEqual "map model lookup" (Map.lookup key model) (SortedMap.lookup key mapValue)
                    assertEqual "map model floor" (Map.lookupLE key model) (SortedMap.floorEntry key mapValue)
                    assertEqual "map model ceiling" (Map.lookupGE key model) (SortedMap.ceilingEntry key mapValue)
                    assertEqual "map model lower" (Map.lookupLT key model) (SortedMap.lowerEntry key mapValue)
                    assertEqual "map model higher" (Map.lookupGT key model) (SortedMap.higherEntry key mapValue)
                    assertEqual "map model rank"
                      (Map.lookupIndex key model)
                      (SortedMap.indexOfKey key mapValue)
                    pure (mapValue, model)
                  else do
                    let (highOffset, _) = randomBelow state2 201
                        high = highOffset - 100
                    assertEqual "map model key range"
                      (Map.toAscList (Map.filterWithKey (\stored _ -> stored >= key && stored <= high) model))
                      (SortedMap.toList (SortedMap.keyRange key high mapValue))
                    assertEqual "map model counts"
                      (Map.size (fst (Map.split key model)))
                      (SortedMap.countKeysLessThan key mapValue)
                    pure (mapValue, model)
          if step `mod` 200 == 0
            then assertMatches ("step " ++ show step) nextMap nextModel
            else pure ()
          let nextRetained
                | step `mod` 250 == 0 = (step, nextMap, nextModel) : retained
                | otherwise = retained
          walk (step + 1) state2 nextMap nextModel nextRetained

    assertMatches label mapValue model = do
      assertEqual (label ++ " contents") (Map.toAscList model) (SortedMap.toList mapValue)
      assertEqual (label ++ " count") (Map.size model) (SortedMap.count mapValue)
      assertEqual (label ++ " min") (Map.lookupMin model) (SortedMap.minEntry mapValue)
      assertEqual (label ++ " max") (Map.lookupMax model) (SortedMap.maxEntry mapValue)

-- Extremes are digit reads: zero comparisons regardless of size, with a positive control proving
-- the instrumentation observes real seeks.
testExtremesAreDigitReads :: IO ()
testExtremesAreDigitReads = do
  let set = SortedSet.fromList (map Counted [0 .. 4_095])
  _ <- evaluate (length (SortedSet.toList set))
  (minimumValue, minimumCalls) <- countComparisons (evaluate (SortedSet.minValue set))
  (maximumValue, maximumCalls) <- countComparisons (evaluate (SortedSet.maxValue set))
  assertEqual "set minimum value" (Just (Counted 0)) minimumValue
  assertEqual "set maximum value" (Just (Counted 4_095)) maximumValue
  assertEqual "set minimum compares nothing" 0 minimumCalls
  assertEqual "set maximum compares nothing" 0 maximumCalls

  let mapValue = SortedMap.fromList [(Counted key, key) | key <- [0 .. 4_095]]
  _ <- evaluate (length (SortedMap.toList mapValue))
  (minimumEntry, minimumEntryCalls) <- countComparisons (evaluate (SortedMap.minEntry mapValue))
  (maximumEntry, maximumEntryCalls) <- countComparisons (evaluate (SortedMap.maxEntry mapValue))
  assertEqual "map minimum entry" (Just (Counted 0, 0)) minimumEntry
  assertEqual "map maximum entry" (Just (Counted 4_095, 4_095)) maximumEntry
  assertEqual "map minimum compares nothing" 0 minimumEntryCalls
  assertEqual "map maximum compares nothing" 0 maximumEntryCalls

  -- Negative control: a keyed seek over the same trees must be observed by the same counter, or
  -- the zeroes above prove nothing.
  (_, memberCalls) <- countComparisons (evaluate (SortedSet.member (Counted 2_048) set))
  assertBool "the comparison counter observes a real seek" (memberCalls > 0)
  (_, lookupCalls) <- countComparisons (evaluate (SortedMap.lookup (Counted 2_048) mapValue))
  assertBool "the comparison counter observes a real map seek" (lookupCalls > 0)

-- A rank slice is two count-directed structural splits: it compares no elements at any position,
-- and its allocation does not track the window's position.
testSliceIsStructural :: IO ()
testSliceIsStructural = do
  let size = 32_768
      window = 64
      source = SortedSet.fromList (map Counted [0 .. size - 1])
  _ <- evaluate (length (SortedSet.toList source))
  -- Warm both split paths once so the measured passes do purely structural work on a forced
  -- spine.  The measured positions are read back through an IORef so the compiler cannot share
  -- the measured slices with these warming ones, which would report vacuous zero allocation.
  _ <- evaluate (sliceTotal 10 window source)
  _ <- evaluate (sliceTotal 30_000 window source)
  positions <- newIORef (10, 30_000)
  (nearPosition, farPosition) <- readIORef positions
  (nearTotal, nearCalls, nearAllocation) <-
    countComparisonsAndAllocation (evaluate (sliceTotal nearPosition window source))
  (farTotal, farCalls, farAllocation) <-
    countComparisonsAndAllocation (evaluate (sliceTotal farPosition window source))
  assertEqual "near slice contents" (windowTotal nearPosition window) nearTotal
  assertEqual "far slice contents" (windowTotal farPosition window) farTotal
  assertEqual "near slice compares nothing" 0 nearCalls
  assertEqual "far slice compares nothing" 0 farCalls
  assertBool
    ("slice allocation is position-independent; near "
      ++ show nearAllocation ++ " bytes, far " ++ show farAllocation ++ " bytes")
    (nearAllocation < 200_000 && farAllocation < 200_000)
  where
    sliceTotal position window source =
      maybe (-1) (sum . map countedValue . SortedSet.toList) (SortedSet.slice position window source)
    windowTotal position window = sum [position .. position + window - 1]

-- Point writes are one measured descent: the comparison count grows by the depth difference, not
-- with the collection's size.
testWriteLogScaling :: IO ()
testWriteLogScaling = do
  let smallSize = 1_024
      largeSize = 32_768
      small = SortedSet.fromList (map (Counted . (* 2)) [0 .. smallSize - 1])
      large = SortedSet.fromList (map (Counted . (* 2)) [0 .. largeSize - 1])
  _ <- evaluate (length (SortedSet.toList small))
  _ <- evaluate (length (SortedSet.toList large))
  (_, smallCalls) <- countComparisons
    (evaluate (length (SortedSet.toList (SortedSet.insert (Counted smallSize) small))))
  (_, largeCalls) <- countComparisons
    (evaluate (length (SortedSet.toList (SortedSet.insert (Counted largeSize) large))))
  assertBool
    ("insert comparisons stay logarithmic; " ++ show smallCalls ++ " at n=" ++ show smallSize
      ++ ", " ++ show largeCalls ++ " at n=" ++ show largeSize)
    (smallCalls > 0 && smallCalls < 90 && largeCalls < 140 && largeCalls - smallCalls < 60)
  (_, deleteCalls) <- countComparisons
    (evaluate (length (SortedSet.toList (SortedSet.delete (Counted largeSize) large))))
  assertBool
    ("delete comparisons stay logarithmic; used " ++ show deleteCalls)
    (deleteCalls > 0 && deleteCalls < 140)

-- The set algebra adopts whole runs between boundary splits: disjoint ranges merge in a constant
-- number of comparisons, and fully interleaved operands stay within a linear comparison budget —
-- the O(m log(n/m + 1)) shape, never an element-by-element Theta(m log(n + m)).
testSetAlgebraAdoptsRuns :: IO ()
testSetAlgebraAdoptsRuns = do
  let low = SortedSet.fromList (map Counted [0 .. 4_095])
      high = SortedSet.fromList (map Counted [10_000 .. 14_095])
  _ <- evaluate (length (SortedSet.toList low))
  _ <- evaluate (length (SortedSet.toList high))
  (unionCount, unionCalls) <- countComparisons
    (evaluate (SortedSet.count (SortedSet.union low high)))
  assertEqual "disjoint union count" 8_192 unionCount
  assertBool
    ("a disjoint union adopts both operands whole; used " ++ show unionCalls ++ " comparisons")
    (unionCalls < 12)
  (differenceCount, differenceCalls) <- countComparisons
    (evaluate (SortedSet.count (SortedSet.difference low high)))
  assertEqual "disjoint difference count" 4_096 differenceCount
  assertBool
    ("a disjoint difference adopts its receiver whole; used " ++ show differenceCalls ++ " comparisons")
    (differenceCalls < 12)
  (intersectionCount, intersectionCalls) <- countComparisons
    (evaluate (SortedSet.count (SortedSet.intersection low high)))
  assertEqual "disjoint intersection count" 0 intersectionCount
  assertBool
    ("a disjoint intersection dismisses both operands; used " ++ show intersectionCalls ++ " comparisons")
    (intersectionCalls < 12)

  forM_ [1_024, 4_096] $ \half -> do
    let evens = SortedSet.fromList (map (Counted . (* 2)) [0 .. half - 1])
        odds = SortedSet.fromList (map (\value -> Counted (2 * value + 1)) [0 .. half - 1])
    _ <- evaluate (length (SortedSet.toList evens))
    _ <- evaluate (length (SortedSet.toList odds))
    (interleavedCount, interleavedCalls) <- countComparisons
      (evaluate (SortedSet.count (SortedSet.union evens odds)))
    assertEqual "interleaved union count" (2 * half) interleavedCount
    assertBool
      ("an interleaved union stays within a linear comparison budget; used "
        ++ show interleavedCalls ++ " comparisons for " ++ show (2 * half) ++ " elements")
      (interleavedCalls < 8 * 2 * half)

  let sparse = SortedSet.fromList [Counted (2_048 * slot + 1) | slot <- [0 .. 31]]
      dense = SortedSet.fromList (map (Counted . (* 2)) [0 .. 32_767])
  _ <- evaluate (length (SortedSet.toList sparse))
  _ <- evaluate (length (SortedSet.toList dense))
  (sparseCount, sparseCalls) <- countComparisons
    (evaluate (SortedSet.count (SortedSet.union dense sparse)))
  assertEqual "sparse union count" 32_800 sparseCount
  assertBool
    ("a sparse union pays per adopted run, not per skipped element; used "
      ++ show sparseCalls ++ " comparisons for 32 insertions into 32,768")
    (sparseCalls < 2_000)

-- The delta map's callback budget depends on this exact shape: an eight-key window over 2,048
-- entries restricted by two boundary splits, comfortably under the 40-comparison ceiling its
-- 'changesInRange' test pins.
testKeyRangeBudget :: IO ()
testKeyRangeBudget = do
  let entries = SortedMap.fromList [(Counted key, key) | key <- [0, 2 .. 4_094]]
  _ <- evaluate (length (SortedMap.toList entries))
  (windowKeys, windowCalls) <- countComparisons
    (evaluate (sum (SortedMap.elems (SortedMap.keyRange (Counted 1_000) (Counted 1_007) entries))))
  assertEqual "key range window contents" (1_000 + 1_002 + 1_004 + 1_006) windowKeys
  assertBool
    ("a key window restricts with two boundary splits; used " ++ show windowCalls ++ " comparisons")
    (windowCalls < 40)
  unless (windowCalls > 0) (failWith "key range instrumentation observed nothing")

-- An element type whose identity carries a label the comparison ignores, for observing which
-- operand's representative survives.
data Labeled = Labeled Int String
  deriving (Show)

instance Eq Labeled where
  Labeled left _ == Labeled right _ = left == right

instance Ord Labeled where
  compare (Labeled left _) (Labeled right _) = compare left right

labelOf :: Labeled -> String
labelOf (Labeled _ value) = value

-- A counting element: every comparison bumps a global counter, in the same
-- unsafePerformIO/NOINLINE style the delta-map budget tests use.
newtype Counted = Counted Int
  deriving (Show)

countedValue :: Counted -> Int
countedValue (Counted value) = value

instance Eq Counted where
  left == right = compare left right == EQ

instance Ord Counted where
  {-# NOINLINE compare #-}
  compare (Counted left) (Counted right) = unsafePerformIO $ do
    modifyIORef' comparisonCounter (+ 1)
    pure (compare left right)

{-# NOINLINE comparisonCounter #-}
comparisonCounter :: IORef Int
comparisonCounter = unsafePerformIO (newIORef 0)

countComparisons :: IO a -> IO (a, Int)
countComparisons action = do
  writeIORef comparisonCounter 0
  result <- action
  calls <- readIORef comparisonCounter
  pure (result, calls)

countComparisonsAndAllocation :: IO a -> IO (a, Int, Int64)
countComparisonsAndAllocation action = do
  writeIORef comparisonCounter 0
  before <- getAllocationCounter
  result <- action
  after <- getAllocationCounter
  calls <- readIORef comparisonCounter
  pure (result, calls, before - after)

randomBelow :: Word64 -> Int -> (Int, Word64)
randomBelow state bound = (fromIntegral (next `div` 8_589_934_592 `mod` fromIntegral bound), next)
  where
    next = state * 6_364_136_223_846_793_005 + 1_442_695_040_888_963_407

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = failWith (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool _ True = pure ()
assertBool label False = failWith (label ++ ": expected True")

failWith :: String -> IO a
failWith message = error ("SortedFamilyTests: " ++ message)
