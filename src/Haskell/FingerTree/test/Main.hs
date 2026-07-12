{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MultiParamTypeClasses #-}
{-# LANGUAGE NumericUnderscores #-}

module Main (main) where

import Prelude hiding (lines, null, reverse, splitAt)

import Control.Concurrent (forkIO, newEmptyMVar, putMVar, takeMVar)
import Control.Exception (SomeException, evaluate, try)
import Control.Monad (forM_, replicateM, when)
import Data.IORef (IORef, modifyIORef', newIORef, readIORef, writeIORef)
import qualified Data.List as List
import qualified Data.Map.Strict as Map
import Data.Monoid (Sum(..))
import Data.Word (Word32)
import System.IO.Unsafe (unsafePerformIO)
import System.Mem.StableName (eqStableName, makeStableName)

import qualified Data.Structures.FingerTree.Deque as Deque
import qualified Data.Structures.FingerTree.BrodalOkasakiHeap as BrodalOkasakiHeap
import qualified Data.Structures.FingerTree.IntervalTree as IntervalTree
import qualified Data.Structures.FingerTree.Measured as FT
import Data.Structures.FingerTree.Measured (ViewL(..), ViewR(..))
import Data.Structures.FingerTree.Measures (Elem(..), Size(..))
import qualified Data.Structures.FingerTree.MeasuredRope as MeasuredRope
import qualified Data.Structures.FingerTree.PriorityQueue as PriorityQueue
import qualified Data.Structures.FingerTree.PrioritySearchQueue as PrioritySearchQueue
import qualified Data.Structures.FingerTree.ReversibleDeque as ReversibleDeque
import qualified Data.Structures.FingerTree.Rope as Rope
import qualified Data.Structures.FingerTree.Rope.Text as RopeText
import qualified Data.Structures.FingerTree.RrbVector as RrbVector
import qualified Data.Structures.FingerTree.SortedBag as SortedBag
import qualified Data.Structures.FingerTree.SortedMap as SortedMap
import qualified Data.Structures.FingerTree.SortedSet as SortedSet
import qualified CanonicalSortedSetTests

main :: IO ()
main = do
  testMeasuredTree
  testMeasureAccumulationOrder
  testLargeFromList
  testDeque
  testDequeSortedBounds
  testReversibleDeque
  testSortedCollections
  testSortedBagRanks
  testPriorityQueue
  testBrodalOkasakiHeap
  testPrioritySearchQueue
  CanonicalSortedSetTests.run
  testIntervalTree
  testRrbVector
  testRopes
  testTextRope
  testConcurrentReads
  putStrLn "tools-data-structures-fingertree tests passed"

testMeasuredTree :: IO ()
testMeasuredTree = do
  let tree :: FT.FingerTree Size (Elem Int)
      tree = FT.fromList (map Elem [1 :: Int .. 10])
  assertEqual "measured toList" [1 .. 10] (map getElem (FT.toList tree))
  assertEqual "measured head" (Just (Elem 1)) (FT.head tree)
  assertEqual "measured last" (Just (Elem 10)) (FT.last tree)
  case FT.split (\(Size seen) -> seen > 4) tree of
    Just (left, Elem value, right) -> do
      assertEqual "split left" [1 .. 4] (map getElem (FT.toList left))
      assertEqual "split value" 5 value
      assertEqual "split right" [6 .. 10] (map getElem (FT.toList right))
    Nothing -> fail "measured split missed"
  let appended :: FT.FingerTree Size (Elem Int)
      appended = FT.append (FT.fromList (map Elem [1 :: Int .. 3])) (FT.fromList (map Elem [4 :: Int .. 6]))
  assertEqual "measured append" [1 .. 6] (map getElem (FT.toList appended))
  case FT.viewL tree of
    Elem 1 :< _ -> pure ()
    _ -> fail "viewL did not expose first element"
  case FT.viewR tree of
    _ :> Elem 10 -> pure ()
    _ -> fail "viewR did not expose last element"

-- Locks the incremental cached-measure order in cons/snoc under a
-- non-commutative monoid: a cons combines the new element's measure on the
-- LEFT of the cached total, a snoc on the RIGHT, so the accumulated trace
-- must read exactly in sequence order through digit-overflow cascades.
testMeasureAccumulationOrder :: IO ()
testMeasureAccumulationOrder = do
  let values = [1 :: Int .. 40]
      consed, snoced, mixed :: FT.FingerTree Trace Traced
      consed = foldr FT.cons FT.empty (map Traced values)
      snoced = List.foldl' FT.snoc FT.empty (map Traced values)
      mixed = FT.cons (Traced 0) (FT.snoc snoced (Traced 41))
  assertEqual "cons keeps non-commutative measure order" (Trace values) (FT.measureTree consed)
  assertEqual "snoc keeps non-commutative measure order" (Trace values) (FT.measureTree snoced)
  assertEqual "mixed endpoint measure order" (Trace ([0] ++ values ++ [41])) (FT.measureTree mixed)
  assertEqual "traced toList round-trip" (map Traced values) (FT.toList consed)

testLargeFromList :: IO ()
testLargeFromList = do
  let upper = 200000 :: Int
      deque = Deque.fromList [0 .. upper - 1]
      tree :: FT.FingerTree Size (Elem Int)
      tree = FT.fromList (map Elem [0 .. upper - 1])
  assertEqual "large deque fromList count" upper (Deque.count deque)
  assertEqual "large deque fromList tail" (Just (upper - 1)) (Deque.last deque)
  assertEqual "large measured fromList count" (Size upper) (FT.measureTree tree)
  assertEqual "large measured fromList tail" (Just (Elem (upper - 1))) (FT.last tree)

testDeque :: IO ()
testDeque = do
  let deque = Deque.fromList [1 :: Int .. 5]
  assertEqual "deque count" 5 (Deque.count deque)
  assertEqual "deque endpoints" (Just 1, Just 5) (Deque.first deque, Deque.last deque)
  assertEqual "deque index" (Just 3) (Deque.index 2 deque)
  assertEqual "deque cons/snoc" [0 .. 6] (Deque.toList (Deque.snoc (Deque.cons 0 deque) 6))
  assertEqual "deque setAt" (Just [1, 2, 99, 4, 5]) (Deque.toList <$> Deque.setAt 2 99 deque)
  assertEqual "deque insertAt" (Just [1, 2, 9, 3, 4, 5]) (Deque.toList <$> Deque.insertAt 2 9 deque)
  assertEqual "deque deleteAt" (Just [1, 2, 4, 5]) (Deque.toList <$> Deque.deleteAt 2 deque)
  assertEqual "deque slice" (Just [2, 3, 4]) (Deque.toList <$> Deque.slice 1 3 deque)
  assertEqual "deque overflowing slice rejected" Nothing (Deque.slice 1 maxBound deque)
  assertEqual "deque lower bound" 1 (Deque.sortedLowerBound 2 (Deque.fromList [1 :: Int, 2, 2, 4]))
  assertEqual "deque upper bound" 3 (Deque.sortedUpperBound 2 (Deque.fromList [1 :: Int, 2, 2, 4]))
  assertEqual "deque binary search" (Deque.Found 1) (Deque.sortedBinarySearch 2 (Deque.fromList [1 :: Int, 2, 2, 4]))
  assertEqual "deque remove sorted" [1, 4] (Deque.toList (Deque.removeAllSorted 2 (Deque.fromList [1 :: Int, 2, 2, 4])))

testDequeSortedBounds :: IO ()
testDequeSortedBounds = do
  let upper = 65_536
      deque = Deque.fromList [0 :: Int .. upper - 1]
  calls <- newIORef 0
  assertEqual "deque measured lower bound" 53_217 (Deque.sortedLowerBoundBy (countingCompare calls) 53_217 deque)
  lowerCalls <- readIORef calls
  assertBool "deque lower bound logarithmic comparisons" (lowerCalls < 128)

  writeIORef calls 0
  assertEqual "deque measured upper bound" 53_218 (Deque.sortedUpperBoundBy (countingCompare calls) 53_217 deque)
  upperCalls <- readIORef calls
  assertBool "deque upper bound logarithmic comparisons" (upperCalls < 128)

  writeIORef calls 0
  assertEqual "deque measured binary search" (Deque.Found 53_217) (Deque.sortedBinarySearchBy (countingCompare calls) 53_217 deque)
  binaryCalls <- readIORef calls
  assertBool "deque binary search logarithmic comparisons" (binaryCalls < 128)

testReversibleDeque :: IO ()
testReversibleDeque = do
  let deque = ReversibleDeque.fromList [1 :: Int, 2, 3]
      reversed = ReversibleDeque.reverse deque
  assertEqual "reverse view" [3, 2, 1] (ReversibleDeque.toList reversed)
  assertEqual "reverse cons" [0, 3, 2, 1] (ReversibleDeque.toList (ReversibleDeque.cons 0 reversed))
  assertEqual "reverse snoc" [3, 2, 1, 4] (ReversibleDeque.toList (ReversibleDeque.snoc reversed 4))
  assertEqual "double reverse" [1, 2, 3] (ReversibleDeque.toList (ReversibleDeque.reverse reversed))
  let leftValues = [1 :: Int .. 6]
      rightValues = [10 :: Int .. 15]
      left = ReversibleDeque.fromList leftValues
      right = ReversibleDeque.fromList rightValues
      reversedLeftValues = List.reverse leftValues
      reversedRightValues = List.reverse rightValues
  assertEqual "append forward forward" (leftValues ++ rightValues) (ReversibleDeque.toList (ReversibleDeque.append left right))
  assertEqual "append reverse forward" (reversedLeftValues ++ rightValues) (ReversibleDeque.toList (ReversibleDeque.append (ReversibleDeque.reverse left) right))
  assertEqual "append forward reverse" (leftValues ++ reversedRightValues) (ReversibleDeque.toList (ReversibleDeque.append left (ReversibleDeque.reverse right)))
  assertEqual "append reverse reverse" (reversedLeftValues ++ reversedRightValues) (ReversibleDeque.toList (ReversibleDeque.append (ReversibleDeque.reverse left) (ReversibleDeque.reverse right)))
  let largeLeft = [1 :: Int .. 1024]
      largeRight = [2001 :: Int .. 3024]
      largeJoined = ReversibleDeque.append
        (ReversibleDeque.reverse (ReversibleDeque.fromList largeLeft))
        (ReversibleDeque.reverse (ReversibleDeque.fromList largeRight))
      largeExpected = List.reverse largeLeft ++ List.reverse largeRight
  assertEqual "large mixed append count" (length largeExpected) (ReversibleDeque.count largeJoined)
  assertEqual "large mixed append first" (Just 1024) (ReversibleDeque.first largeJoined)
  assertEqual "large mixed append last" (Just 2001) (ReversibleDeque.last largeJoined)
  assertEqual "large mixed append boundary index" (Just 3024) (ReversibleDeque.index 1024 largeJoined)
  assertEqual "large mixed append round-trip" largeExpected (ReversibleDeque.toList largeJoined)

testSortedCollections :: IO ()
testSortedCollections = do
  let bag = SortedBag.fromList [3 :: Int, 1, 2, 2, 1]
  assertEqual "bag toList" [1, 1, 2, 2, 3] (SortedBag.toList bag)
  assertEqual "bag countOf" 2 (SortedBag.countOf 2 bag)
  assertEqual "bag index" (Just 2) (SortedBag.index 2 bag)
  assertEqual "bag slice" (Just [1, 2, 2]) (SortedBag.toList <$> SortedBag.slice 1 3 bag)
  assertEqual "bag overflowing slice rejected" Nothing (SortedBag.slice 1 maxBound bag)
  let set = SortedSet.fromList [3 :: Int, 1, 2]
  assertEqual "set floor" (Just 2) (SortedSet.floor 2 set)
  assertEqual "set higher" (Just 3) (SortedSet.higher 2 set)
  assertEqual "set index" (Just 2) (SortedSet.index 1 set)
  assertEqual "set overflowing slice rejected" Nothing (SortedSet.slice 1 maxBound set)
  assertBool "set algebra" (SortedSet.setEquals (SortedSet.union set (SortedSet.singleton 4)) (SortedSet.fromList [1 :: Int, 2, 3, 4]))
  let dict = SortedMap.fromList [(2 :: Int, "b"), (1, "a"), (2, "bb")]
  assertEqual "map last wins" (Just "bb") (SortedMap.lookup 2 dict)
  assertEqual "map entryAt" (Just (2, "bb")) (SortedMap.index 1 dict)
  assertEqual "map floor" (Just (2, "bb")) (SortedMap.floorEntry 2 dict)
  assertEqual "map overflowing slice rejected" Nothing (SortedMap.slice 1 maxBound dict)
  -- Comparer-equal-but-distinct elements: the set keeps the first stored
  -- instance, and the bag retains every instance (new after existing).
  let firstWins = SortedSet.insert (Keyed 1 "new") (SortedSet.fromList [Keyed (1 :: Int) "old"])
  assertEqual "set first instance wins" ["old"] (map keyedLabel (SortedSet.toList firstWins))
  let instanceBag = SortedBag.insert (Keyed 1 "b") (SortedBag.singleton (Keyed (1 :: Int) "a"))
  assertEqual "bag retains instances in order" ["a", "b"] (map keyedLabel (SortedBag.toList instanceBag))
  assertEqual
    "bag toCounts keeps first stored representative"
    [("a", 2)]
    (map (\(value, total) -> (keyedLabel value, total)) (SortedBag.toCounts instanceBag))
  let remaining = SortedBag.deleteOne (Keyed 1 "a") instanceBag
  assertEqual
    "bag deleteOne removes the first instance"
    ["b"]
    (map keyedLabel (SortedBag.toList remaining))
  -- After the first instance is removed, the bucket key must be re-keyed to
  -- the surviving instance: the toCounts representative is always a value
  -- the bag still contains.
  assertEqual
    "bag deleteOne re-keys the toCounts representative"
    [("b", 1)]
    (map (\(value, total) -> (keyedLabel value, total)) (SortedBag.toCounts remaining))
  assertEqual
    "bag rank slice re-keys the right partial bucket"
    (Just [("b", 1)])
    (fmap
      (map (\(value, total) -> (keyedLabel value, total)) . SortedBag.toCounts)
      (SortedBag.slice 1 1 instanceBag))

testSortedBagRanks :: IO ()
testSortedBagRanks = do
  let distinct = SortedBag.fromList [0 :: Int .. 19_999]
  assertEqual "bag logarithmic distinct index" (Just 17_531) (SortedBag.index 17_531 distinct)
  assertEqual "bag logarithmic count less" 12_345 (SortedBag.countLessThan 12_345 distinct)
  assertEqual "bag logarithmic count at most" 12_346 (SortedBag.countAtMost 12_345 distinct)
  assertEqual
    "bag measured slice across buckets"
    (Just [9_995 :: Int .. 10_014])
    (SortedBag.toList <$> SortedBag.slice 9_995 20 distinct)

  let equalRun = SortedBag.fromList (replicate 100_000 (7 :: Int))
  assertEqual "bag equal-run count" 100_000 (SortedBag.count equalRun)
  assertEqual "bag equal-run distinct count" 1 (SortedBag.distinctCount equalRun)
  assertEqual "bag equal-run final rank" (Just 7) (SortedBag.index 99_999 equalRun)
  assertEqual
    "bag measured slice within one bucket"
    (Just (replicate 64 (7 :: Int)))
    (SortedBag.toList <$> SortedBag.slice 50_000 64 equalRun)

testPriorityQueue :: IO ()
testPriorityQueue = do
  let queue = PriorityQueue.fromList [(2 :: Int, "b"), (1, "a"), (1, "c"), (3, "d")]
  assertEqual "priority count" 4 (PriorityQueue.count queue)
  assertEqual "priority peek" (Just ("a", 1)) (PriorityQueue.peek queue)
  case PriorityQueue.dequeue queue of
    Just (("a", 1), rest) -> assertEqual "priority stable next" (Just ("c", 1)) (PriorityQueue.peek rest)
    other -> fail ("unexpected priority dequeue: " ++ show other)
  let melded = PriorityQueue.meld (PriorityQueue.fromList [(5 :: Int, "x")]) (PriorityQueue.fromList [(0, "y")])
  assertEqual "priority meld" (Just ("y", 0)) (PriorityQueue.peek melded)

testBrodalOkasakiHeap :: IO ()
testBrodalOkasakiHeap = do
  let source = [7 :: Int, 1, 9, 3, 1, 8, 2]
      heap = BrodalOkasakiHeap.fromList source
  assertEqual "Brodal count" (length source) (BrodalOkasakiHeap.count heap)
  assertEqual "Brodal minimum" (Just 1) (BrodalOkasakiHeap.minimum heap)
  assertEqual "Brodal sorted drain" (List.sort source) (drainBrodal heap)
  assertBrodalValid "Brodal basic structure" heap

  let left = BrodalOkasakiHeap.fromList [0 :: Int .. 255]
      right = BrodalOkasakiHeap.fromList [256 :: Int .. 511]
      melded = BrodalOkasakiHeap.meld left right
      inserted = BrodalOkasakiHeap.insert (-1) left
  assertEqual "Brodal retained left" [0 :: Int .. 255] (drainBrodal left)
  assertEqual "Brodal retained right" [256 :: Int .. 511] (drainBrodal right)
  assertEqual "Brodal meld" [0 :: Int .. 511] (drainBrodal melded)
  assertEqual "Brodal inserted minimum" (Just (-1)) (BrodalOkasakiHeap.minimum inserted)
  assertBrodalValid "Brodal meld structure" melded
  assertBrodalValid "Brodal inserted structure" inserted

  let selfMelded = BrodalOkasakiHeap.meld left left
  assertEqual "Brodal self-meld count" 512 (BrodalOkasakiHeap.count selfMelded)
  assertEqual "Brodal self-meld values" (concatMap (replicate 2) [0 :: Int .. 255]) (drainBrodal selfMelded)
  assertBrodalValid "Brodal self-meld structure" selfMelded

  let randomValues = take 20_000 (brodalRandomValues 0x6a09_e667)
      randomHeap = BrodalOkasakiHeap.fromList randomValues
  assertEqual "Brodal randomized drain" (List.sort randomValues) (drainBrodal randomHeap)
  case BrodalOkasakiHeap.validateStructure randomHeap of
    Nothing -> fail "Brodal randomized validation failed"
    Just statistics -> do
      assertEqual "Brodal randomized validated count" 20_000 (BrodalOkasakiHeap.brodalStatisticsCount statistics)
      assertBool "Brodal randomized logarithmic rank" (BrodalOkasakiHeap.brodalStatisticsMaximumRank statistics <= 32)
      assertBool "Brodal randomized bounded root forest" (BrodalOkasakiHeap.brodalStatisticsRootForestLength statistics <= 32)

drainBrodal :: Ord a => BrodalOkasakiHeap.BrodalOkasakiHeap a -> [a]
drainBrodal heap = case BrodalOkasakiHeap.minView heap of
  Nothing -> []
  Just (value, remaining) -> value : drainBrodal remaining

brodalRandomValues :: Word32 -> [Int]
brodalRandomValues seed =
  let next = nextRrbRandom seed
  in (fromIntegral (next `mod` 100_003) - 50_001) : brodalRandomValues next

assertBrodalValid :: Ord a => String -> BrodalOkasakiHeap.BrodalOkasakiHeap a -> IO ()
assertBrodalValid label heap = case BrodalOkasakiHeap.validateStructure heap of
  Nothing -> fail (label ++ ": invalid Brodal-Okasaki representation")
  Just statistics -> assertEqual
    (label ++ " count")
    (BrodalOkasakiHeap.count heap)
    (BrodalOkasakiHeap.brodalStatisticsCount statistics)

testPrioritySearchQueue :: IO ()
testPrioritySearchQueue = do
  let queue = PrioritySearchQueue.fromList
        [ (4 :: Int, 2 :: Int, "four")
        , (1, 1, "one")
        , (7, 1, "seven")
        , (4, 0, "FOUR")
        ]
  assertEqual "psq last-wins count" 3 (PrioritySearchQueue.count queue)
  assertEqual
    "psq key order"
    [ PrioritySearchQueue.PrioritySearchEntry 1 1 "one"
    , PrioritySearchQueue.PrioritySearchEntry 4 0 "FOUR"
    , PrioritySearchQueue.PrioritySearchEntry 7 1 "seven"
    ]
    (PrioritySearchQueue.toAscList queue)
  assertEqual
    "psq O(1) minimum"
    (Just (PrioritySearchQueue.PrioritySearchEntry 4 0 "FOUR"))
    (PrioritySearchQueue.minimumEntry queue)
  assertEqual
    "psq range and threshold"
    (Just [PrioritySearchQueue.PrioritySearchEntry 4 0 "FOUR"])
    (PrioritySearchQueue.enumerateAtMost 2 7 0 queue)
  assertEqual "psq inverted range" Nothing (PrioritySearchQueue.enumerateAtMost 8 2 10 queue)
  assertEqual "psq duplicate insertion" Nothing (PrioritySearchQueue.insertNew 4 9 "duplicate" queue)
  case PrioritySearchQueue.insertNew 2 3 "two" queue of
    Nothing -> fail "psq unique insertion failed"
    Just inserted -> do
      assertEqual "psq inserted lookup"
        (Just (PrioritySearchQueue.PrioritySearchEntry 2 3 "two"))
        (PrioritySearchQueue.lookup 2 inserted)
      assertPsqValid "psq inserted structure" inserted
  case PrioritySearchQueue.minView queue of
    Nothing -> fail "psq minView missed"
    Just (removed, remaining) -> do
      assertEqual "psq minView entry" (PrioritySearchQueue.PrioritySearchEntry 4 0 "FOUR") removed
      assertEqual "psq minView keys" [1, 7] (map PrioritySearchQueue.entryKey (PrioritySearchQueue.toAscList remaining))
      assertPsqValid "psq minView structure" remaining

  let ascending = List.foldl'
        (\current key -> PrioritySearchQueue.setItem key (key `mod` 17) (negate key) current)
        PrioritySearchQueue.empty
        [0 :: Int .. 4_095]
  assertPsqValid "psq ascending AVL structure" ascending
  case PrioritySearchQueue.validateStructure ascending of
    Nothing -> fail "psq ascending validation failed"
    Just statistics -> do
      assertEqual "psq ascending count" 4_096 (PrioritySearchQueue.psqStatisticsCount statistics)
      assertBool "psq logarithmic AVL height" (PrioritySearchQueue.psqStatisticsHeight statistics <= 16)
      assertBool "psq AVL balance" (PrioritySearchQueue.psqStatisticsMaximumAbsoluteBalance statistics <= 1)
  assertEqual
    "psq sparse bounded query"
    (map (\key -> PrioritySearchQueue.PrioritySearchEntry key (key `mod` 17) (negate key))
      [key | key <- [512 :: Int .. 1_024], key `mod` 17 <= 3])
    (maybe [] id (PrioritySearchQueue.enumerateAtMost 512 1_024 3 ascending))

  runPsqRandomized 0 0x51a7_2026 PrioritySearchQueue.empty Map.empty []

runPsqRandomized
  :: Int
  -> Word32
  -> PrioritySearchQueue.PrioritySearchQueue Int Int Int
  -> Map.Map Int (Int, Int)
  -> [(PrioritySearchQueue.PrioritySearchQueue Int Int Int, [PrioritySearchQueue.PrioritySearchEntry Int Int Int])]
  -> IO ()
runPsqRandomized !step !random queue model snapshots
  | step == 10_000 = do
      assertPsqModel "psq randomized final" queue model
      forM_ snapshots $ \(snapshot, expected) -> do
        assertEqual "psq retained snapshot" expected (PrioritySearchQueue.toAscList snapshot)
        assertPsqValid "psq retained structure" snapshot
  | otherwise = do
      let next = nextRrbRandom random
          key = boundedPsq next 1_537 - 768
          priority = boundedPsq (nextRrbRandom next) 64
          operation = boundedPsq (nextRrbRandom (nextRrbRandom next)) 8
          value = step
          (updated, expected) = case operation of
            0 -> (PrioritySearchQueue.delete key queue, Map.delete key model)
            1 -> case PrioritySearchQueue.insertNew key priority value queue of
              Nothing -> (queue, model)
              Just inserted -> (inserted, Map.insert key (priority, value) model)
            2
              | Map.null model ->
                  (PrioritySearchQueue.setItem key priority value queue, Map.insert key (priority, value) model)
              | otherwise -> case PrioritySearchQueue.minView queue of
                  Nothing -> (queue, model)
                  Just (entry, remaining) ->
                    (remaining, Map.delete (PrioritySearchQueue.entryKey entry) model)
            _ -> (PrioritySearchQueue.setItem key priority value queue, Map.insert key (priority, value) model)
      when (step `mod` 127 == 0) $ assertPsqModel "psq randomized checkpoint" updated expected
      let retained = if step `mod` 701 == 0
            then (updated, psqModelEntries expected) : snapshots
            else snapshots
      runPsqRandomized (step + 1) next updated expected retained

boundedPsq :: Word32 -> Int -> Int
boundedPsq value bound = fromIntegral (value `mod` fromIntegral bound)

psqModelEntries :: Map.Map Int (Int, Int) -> [PrioritySearchQueue.PrioritySearchEntry Int Int Int]
psqModelEntries model =
  [ PrioritySearchQueue.PrioritySearchEntry key priority value
  | (key, (priority, value)) <- Map.toAscList model
  ]

assertPsqModel
  :: String
  -> PrioritySearchQueue.PrioritySearchQueue Int Int Int
  -> Map.Map Int (Int, Int)
  -> IO ()
assertPsqModel label queue model = do
  assertEqual (label ++ " entries") (psqModelEntries model) (PrioritySearchQueue.toAscList queue)
  assertEqual (label ++ " count") (Map.size model) (PrioritySearchQueue.count queue)
  let expectedMinimum = case Map.toAscList model of
        [] -> Nothing
        values -> Just (List.minimumBy comparePriority
          [PrioritySearchQueue.PrioritySearchEntry key priority value | (key, (priority, value)) <- values])
  assertEqual (label ++ " minimum") expectedMinimum (PrioritySearchQueue.minimumEntry queue)
  assertPsqValid (label ++ " structure") queue
  where
    comparePriority left right = compare
      (PrioritySearchQueue.entryPriority left, PrioritySearchQueue.entryKey left)
      (PrioritySearchQueue.entryPriority right, PrioritySearchQueue.entryKey right)

assertPsqValid :: (Ord k, Ord p, Eq v) => String -> PrioritySearchQueue.PrioritySearchQueue k p v -> IO ()
assertPsqValid label queue = case PrioritySearchQueue.validateStructure queue of
  Nothing -> fail (label ++ ": invalid priority-search queue")
  Just statistics -> do
    assertEqual (label ++ " validated count") (PrioritySearchQueue.count queue) (PrioritySearchQueue.psqStatisticsCount statistics)
    assertEqual (label ++ " validated height") (PrioritySearchQueue.height queue) (PrioritySearchQueue.psqStatisticsHeight statistics)
    assertBool (label ++ " balance") (PrioritySearchQueue.psqStatisticsMaximumAbsoluteBalance statistics <= 1)

testIntervalTree :: IO ()
testIntervalTree = do
  let a = mustInterval (1 :: Int) 3
      b = mustInterval 5 7
      c = mustInterval 2 6
      tree = IntervalTree.fromList [a, b]
  assertEqual "interval overlap" (Just a) (IntervalTree.findOverlap c tree)
  assertEqual "interval containing" (Just b) (IntervalTree.findContaining 6 tree)
  assertEqual "interval delete" [b] (IntervalTree.toList (IntervalTree.delete a tree))
  let d = mustInterval 8 9
      coalesced = IntervalTree.coalesce (IntervalTree.fromList [a, c, d])
  assertEqual "interval coalesce" [IntervalTree.Interval 1 6, d] (IntervalTree.toList coalesced)
  -- New equal-low intervals precede existing ones (C# reference tie order).
  let ties = IntervalTree.fromList [mustInterval (1 :: Int) 3, mustInterval 1 5, mustInterval 1 4, mustInterval 0 9]
  assertEqual
    "interval equal-low tie order"
    [IntervalTree.Interval 0 9, IntervalTree.Interval 1 4, IntervalTree.Interval 1 5, IntervalTree.Interval 1 3]
    (IntervalTree.toList ties)
  assertEqual
    "interval delete within equal-low run"
    [IntervalTree.Interval 0 9, IntervalTree.Interval 1 4, IntervalTree.Interval 1 3]
    (IntervalTree.toList (IntervalTree.delete (IntervalTree.Interval 1 5) ties))
  let pointIntervals = [IntervalTree.Interval value value | value <- [0 :: Int .. 1000]]
      spanning = IntervalTree.Interval 500 2000
      augmented = IntervalTree.fromList (pointIntervals ++ [spanning])
      probe = IntervalTree.Interval 1500 1500
  assertEqual "max-high search skips non-overlapping prefix" (Just spanning) (IntervalTree.findOverlap probe augmented)
  assertEqual "max-high count skips non-overlapping prefix" 1 (IntervalTree.countOverlaps probe augmented)
  let rangeProbe = IntervalTree.Interval 450 550
      expectedOverlaps = filter (\interval -> IntervalTree.low interval <= 550 && IntervalTree.high interval >= 450) (IntervalTree.toList augmented)
  assertEqual "augmented overlap enumeration" expectedOverlaps (IntervalTree.findOverlaps rangeProbe augmented)
  where
    mustInterval low high =
      case IntervalTree.mkInterval low high of
        Just interval -> interval
        Nothing -> error "testIntervalTree: invalid interval literal"

testRrbVector :: IO ()
testRrbVector = do
  forM_ [0, 1, 31, 32, 33, 1_023, 1_024, 1_025, 100_000] $ \valueCount -> do
    let vector = RrbVector.fromList [0 :: Int .. valueCount - 1]
    assertEqual "rrb construction count" valueCount (RrbVector.count vector)
    assertEqual "rrb construction contents" [0 .. valueCount - 1] (RrbVector.toList vector)
    case RrbVector.validateStructure vector of
      Nothing -> fail "rrb packed construction failed structural validation"
      Just statistics -> do
        assertEqual "rrb statistics count" valueCount (RrbVector.statisticsCount statistics)
        assertEqual "rrb packed construction has no relaxed branches" 0 (RrbVector.statisticsRelaxedBranchCount statistics)
    forM_ [0, max 0 (valueCount `div` 2), max 0 (valueCount - 1)] $ \position ->
      when (valueCount /= 0) (assertEqual "rrb indexed lookup" (Just position) (RrbVector.index position vector))

  forM_ [(1, 100_000), (100_000, 1), (31, 33), (1_023, 1_025), (50_000, 50_000)] $ \(leftCount, rightCount) -> do
    let left = RrbVector.fromList [0 :: Int .. leftCount - 1]
        right = RrbVector.fromList [leftCount .. leftCount + rightCount - 1]
        combined = RrbVector.append left right
    assertEqual "rrb unequal-height concat" [0 .. leftCount + rightCount - 1] (RrbVector.toList combined)
    assertBool "rrb concat validates" (isJustValidation combined)

  let base = RrbVector.fromList [0 :: Int .. 9_999]
  forM_ [0, 1, 31, 32, 33, 999, 1_024, 5_000, 9_999, 10_000] $ \boundary ->
    case RrbVector.splitAt boundary base of
      Nothing -> fail "rrb valid split boundary was rejected"
      Just (left, right) -> do
        assertEqual "rrb split left" [0 .. boundary - 1] (RrbVector.toList left)
        assertEqual "rrb split right" [boundary .. 9_999] (RrbVector.toList right)
        assertEqual "rrb split rejoin" base (RrbVector.append left right)
        assertBool "rrb split left validates" (isJustValidation left)
        assertBool "rrb split right validates" (isJustValidation right)

  case (RrbVector.splitAt 0 base, RrbVector.splitAt (RrbVector.count base) base) of
    (Just (_, atZero), Just (atEnd, _)) -> do
      assertRrbSharesRoot "rrb zero split reuses vector" base atZero
      assertRrbSharesRoot "rrb end split reuses vector" base atEnd
    _ -> fail "rrb boundary split failed"
  assertRrbSharesRoot "rrb append empty reuses left" base (RrbVector.append base RrbVector.empty)
  assertRrbSharesRoot "rrb append empty reuses right" base (RrbVector.append RrbVector.empty base)
  case RrbVector.setAt 2 2 base of
    Just unchanged -> assertRrbSharesRoot "rrb equal set reuses vector" base unchanged
    Nothing -> fail "rrb equal set failed"
  case RrbVector.insertListAt 2 [] base of
    Just unchanged -> assertRrbSharesRoot "rrb empty insert reuses vector" base unchanged
    Nothing -> fail "rrb empty insert failed"
  case RrbVector.removeRange 2 0 base of
    Just unchanged -> assertRrbSharesRoot "rrb zero remove reuses vector" base unchanged
    Nothing -> fail "rrb zero remove failed"

  let packed = RrbVector.fromList [0 :: Int .. 32 * 1_024 - 1]
  case RrbVector.splitAt 1 packed of
    Nothing -> fail "rrb relaxed suffix split failed"
    Just (_, relaxed) ->
      case RrbVector.validateStructure relaxed of
        Nothing -> fail "rrb relaxed suffix failed structural validation"
        Just statistics -> assertBool "rrb suffix contains relaxed branches" (RrbVector.statisticsRelaxedBranchCount statistics > 0)

  runRrbRandomized 0 0x9e37_79b9 RrbVector.empty [] []
  adversarial <- runRrbSplitConcat 0 0x1234_abcd packed
  assertEqual "rrb adversarial history contents" (RrbVector.toList packed) (RrbVector.toList adversarial)
  case RrbVector.validateStructure adversarial of
    Nothing -> fail "rrb adversarial history failed validation"
    Just statistics -> do
      assertBool "rrb adversarial logarithmic height" (RrbVector.statisticsHeight statistics <= minimumRrbHeight (RrbVector.count adversarial) + 1)
      assertBool "rrb adversarial leaf density" (RrbVector.statisticsLeafCount statistics <= (RrbVector.count adversarial + 15) `div` 16)

  let (fragmented, fragmentModel) = List.foldl' appendFragment (RrbVector.empty, []) [0 :: Int .. 2_047]
  assertEqual "rrb uneven fragments" fragmentModel (RrbVector.toList fragmented)
  assertBool "rrb uneven fragments validate" (isJustValidation fragmented)
  case RrbVector.unsnoc (RrbVector.fromList [1 :: Int, 2, 3]) of
    Just (remaining, value) -> do
      assertEqual "rrb unsnoc value" 3 value
      assertEqual "rrb unsnoc remaining" [1, 2] (RrbVector.toList remaining)
    Nothing -> fail "rrb unsnoc failed"

runRrbRandomized :: Int -> Word32 -> RrbVector.RrbVector Int -> [Int] -> [(RrbVector.RrbVector Int, [Int])] -> IO ()
runRrbRandomized !step !random vector model snapshots
  | step == 10_000 = do
      assertEqual "rrb randomized final model" model (RrbVector.toList vector)
      forM_ snapshots $ \(snapshot, expected) -> assertEqual "rrb retained randomized snapshot" expected (RrbVector.toList snapshot)
  | otherwise = do
      let next = nextRrbRandom random
          operation = fromIntegral (next `mod` 5)
      (updated, expected) <- applyRrbOperation operation step next vector model
      when (step `mod` 257 == 0) $ assertBool "rrb randomized structure" (isJustValidation updated)
      let retained = if step `mod` 701 == 0 then (updated, expected) : snapshots else snapshots
      runRrbRandomized (step + 1) next updated expected retained

applyRrbOperation :: Int -> Int -> Word32 -> RrbVector.RrbVector Int -> [Int] -> IO (RrbVector.RrbVector Int, [Int])
applyRrbOperation operation step random vector model =
  case operation of
    0 -> pure (RrbVector.snoc vector step, model ++ [step])
    1 -> pure (RrbVector.cons step vector, step : model)
    2
      | List.null model -> pure (vector, model)
      | otherwise ->
          let position = boundedRrb random (length model)
              value = negate step
          in case RrbVector.setAt position value vector of
               Just updated -> pure (updated, take position model ++ [value] ++ drop (position + 1) model)
               Nothing -> fail "rrb randomized set failed"
    3 ->
      let position = boundedRrb random (length model + 1)
          values = [step, step + 1, step + 2]
      in case RrbVector.insertListAt position values vector of
           Just updated -> pure (updated, take position model ++ values ++ drop position model)
           Nothing -> fail "rrb randomized insert failed"
    _
      | List.null model -> pure (vector, model)
      | otherwise ->
          let position = boundedRrb random (length model)
              amount = boundedRrb (nextRrbRandom random) (length model - position + 1)
          in case RrbVector.removeRange position amount vector of
               Just updated -> pure (updated, take position model ++ drop (position + amount) model)
               Nothing -> fail "rrb randomized remove failed"

runRrbSplitConcat :: Int -> Word32 -> RrbVector.RrbVector Int -> IO (RrbVector.RrbVector Int)
runRrbSplitConcat !operation !random vector
  | operation == 2_000 = pure vector
  | otherwise = do
      let next = nextRrbRandom random
          valueCount = RrbVector.count vector
          boundary = case operation `mod` 7 of
            0 -> 1
            1 -> 31
            2 -> 32
            3 -> 1_023
            4 -> 1_024
            5 -> valueCount - 1
            _ -> 1 + boundedRrb next (valueCount - 1)
      updated <- case RrbVector.splitAt boundary vector of
        Just (left, right) -> pure (RrbVector.append left right)
        Nothing -> fail "rrb adversarial split failed"
      when (operation `mod` 127 == 0) $ assertBool "rrb adversarial intermediate structure" (isJustValidation updated)
      runRrbSplitConcat (operation + 1) next updated

appendFragment :: (RrbVector.RrbVector Int, [Int]) -> Int -> (RrbVector.RrbVector Int, [Int])
appendFragment (vector, model) fragment =
  let fragmentLength = 1 + fragment * 17 `mod` 63
      values = [length model .. length model + fragmentLength - 1]
  in (RrbVector.append vector (RrbVector.fromList values), model ++ values)

nextRrbRandom :: Word32 -> Word32
nextRrbRandom value = value * 1_664_525 + 1_013_904_223

boundedRrb :: Word32 -> Int -> Int
boundedRrb _ bound | bound <= 0 = 0
boundedRrb value bound = fromIntegral (value `mod` fromIntegral bound)

minimumRrbHeight :: Int -> Int
minimumRrbHeight valueCount = go 0 32
  where
    go result capacity
      | capacity >= toInteger valueCount = result
      | otherwise = go (result + 1) (capacity * 32)

isJustValidation :: RrbVector.RrbVector a -> Bool
isJustValidation vector = case RrbVector.validateStructure vector of
  Just _ -> True
  Nothing -> False

assertRrbSharesRoot :: String -> RrbVector.RrbVector a -> RrbVector.RrbVector a -> IO ()
assertRrbSharesRoot label left right = RrbVector.sharesRootWith left right >>= assertBool label

testRopes :: IO ()
testRopes = do
  let rope = Rope.fromList [1 :: Int .. 70]
  assertEqual "rope count" 70 (Rope.count rope)
  assertEqual "rope chunks" [64, 6] (map length (Rope.chunks rope))
  assertEqual "rope index" (Just 65) (Rope.index 64 rope)
  assertEqual "rope insert" (Just [1, 99, 2]) (take 3 . Rope.toList <$> Rope.insertAt 1 99 (Rope.fromList [1 :: Int, 2]))
  assertEqual "rope split" (Just ([1, 2], [3, 4])) (pairToLists <$> Rope.splitAt 2 (Rope.fromList [1 :: Int .. 4]))
  assertEqual "rope overflowing slice rejected" Nothing (Rope.slice 1 maxBound rope)
  assertEqual "rope overflowing removal rejected" Nothing (Rope.removeRange 1 maxBound rope)
  let boundaryRope = Rope.fromChunks [[1 :: Int .. 64], [65 .. 128], [129 .. 192]]
  assertEqual "rope insert at chunk boundary" (Just ([61, 62, 63, 64, 999, 65, 66, 67])) (take 8 . drop 60 . Rope.toList <$> Rope.insertAt 64 999 boundaryRope)
  assertEqual "rope delete at chunk boundary" (Just ([62, 63, 64, 66, 67, 68])) (take 6 . drop 61 . Rope.toList <$> Rope.deleteAt 64 boundaryRope)
  assertEqual "rope boundary set" (Just 999) (Rope.index 127 =<< Rope.setAt 127 999 boundaryRope)
  assertEqual "rope cross-chunk removal" (Just ([1 .. 60] ++ [133 .. 192])) (Rope.toList <$> Rope.removeRange 60 72 boundaryRope)
  assertEqual "rope full boundary split" (Just ([1 .. 64], [65 .. 192])) (pairToLists <$> Rope.splitAt 64 boundaryRope)
  let oldFarChunk = Rope.chunks boundaryRope !! 2
      editedBoundary = maybe (error "boundary set unexpectedly failed") id (Rope.setAt 1 777 boundaryRope)
      newFarChunk = Rope.chunks editedBoundary !! 2
  oldFarChunk' <- evaluate oldFarChunk
  newFarChunk' <- evaluate newFarChunk
  oldName <- makeStableName oldFarChunk'
  newName <- makeStableName newFarChunk'
  assertBool "rope edit retains untouched chunk storage" (oldName `eqStableName` newName)
  let measured = MeasuredRope.fromListWith Sum [1 :: Int, 2, 3]
  assertEqual "measured rope total" (Sum 6) (MeasuredRope.measure measured)
  assertEqual "measured rope prefix" (Just (Sum 3)) (MeasuredRope.prefixMeasure 2 measured)
  assertEqual "measured rope locate" (Just (1, Sum 1, 2)) (MeasuredRope.locateByMeasure (\(Sum value) -> value >= 3) measured)
  assertEqual "measured rope overflowing slice rejected" Nothing (MeasuredRope.toList <$> MeasuredRope.slice 1 maxBound measured)
  let measuredBoundary = MeasuredRope.fromListWith Sum [1 :: Int .. 192]
  assertEqual "measured rope cross-chunk prefix" (Just (Sum (sum [1 :: Int .. 129]))) (MeasuredRope.prefixMeasure 129 measuredBoundary)
  assertEqual "measured rope cross-chunk index" (Just 129) (MeasuredRope.index 128 measuredBoundary)
  let changedMeasured = MeasuredRope.setAt 64 1000 measuredBoundary
  assertEqual "measured rope boundary set measure" (Just (Sum (sum [1 :: Int .. 192] - 65 + 1000))) (MeasuredRope.measure <$> changedMeasured)
  let insertedMeasured = MeasuredRope.insertAt 64 1000 measuredBoundary
  assertEqual "measured rope boundary insert count" (Just 193) (MeasuredRope.count <$> insertedMeasured)
  assertEqual "measured rope boundary insert measure" (Just (Sum (sum [1 :: Int .. 192] + 1000))) (MeasuredRope.measure <$> insertedMeasured)
  assertEqual
    "measured rope guided split across chunks"
    (Just ([1 :: Int .. 99], 100, [101 .. 192]))
    ((\(left, value, right) -> (MeasuredRope.toList left, value, MeasuredRope.toList right)) <$> MeasuredRope.splitByMeasure (\(Sum value) -> value >= sum [1 :: Int .. 100]) measuredBoundary)
  where
    pairToLists (left, right) = (Rope.toList left, Rope.toList right)

testTextRope :: IO ()
testTextRope = do
  let text = RopeText.fromString "a\nbc\n"
  assertEqual "text line count" 3 (RopeText.lineCount text)
  assertEqual "text lines" ["a", "bc", ""] (RopeText.lines text)
  assertEqual "text line of offset" (Just 1) (RopeText.lineOfOffset 3 text)
  assertEqual "text line column" (Just (1, 1)) (RopeText.lineColumnOf 3 text)
  assertEqual "text offset" (Just 4) (RopeText.offsetOf 1 2 text)
  assertEqual "text get trailing line" (Just "") (RopeText.getLine 2 text)
  let longLines = replicate 80 'a' ++ "\n" ++ replicate 90 'b' ++ "\n" ++ replicate 70 'c'
      longText = RopeText.fromString longLines
  assertEqual "text measured line count across chunks" 3 (RopeText.lineCount longText)
  assertEqual "text measured line start across chunks" (Just 81) (RopeText.lineStartOffset 1 longText)
  assertEqual "text measured line-column across chunks" (Just (1, 45)) (RopeText.lineColumnOf 126 longText)
  assertEqual "text measured offset across chunks" (Just 171) (RopeText.offsetOf 1 90 longText)
  assertEqual "text measured line extraction across chunks" (Just (replicate 90 'b')) (RopeText.getLine 1 longText)

testConcurrentReads :: IO ()
testConcurrentReads = do
  let expectedDeque = [0 :: Int .. 511]
      deque = Deque.fromList expectedDeque
      reversible = ReversibleDeque.reverse (ReversibleDeque.fromList expectedDeque)
      expectedReverse = List.reverse expectedDeque
      rope = Rope.fromList expectedDeque
      rrb = RrbVector.fromList expectedDeque
      measuredValues = [1 :: Int .. 128]
      measured = MeasuredRope.fromListWith Sum measuredValues
  runConcurrent "fingertree concurrent reads" 8 $ do
    forM_ [1 :: Int .. 128] $ \_ -> do
      assertEqual "concurrent deque count" 512 (Deque.count deque)
      assertEqual "concurrent deque index" (Just 255) (Deque.index 255 deque)
      assertEqual "concurrent deque contents" expectedDeque (Deque.toList deque)
      assertEqual "concurrent reversible contents" expectedReverse (ReversibleDeque.toList reversible)
      assertEqual "concurrent rope count" 512 (Rope.count rope)
      assertEqual "concurrent rope index" (Just 255) (Rope.index 255 rope)
      assertEqual "concurrent rope contents" expectedDeque (Rope.toList rope)
      assertEqual "concurrent rrb count" 512 (RrbVector.count rrb)
      assertEqual "concurrent rrb index" (Just 255) (RrbVector.index 255 rrb)
      assertEqual "concurrent rrb contents" expectedDeque (RrbVector.toList rrb)
      assertEqual "concurrent measured count" 128 (MeasuredRope.count measured)
      assertEqual "concurrent measured total" (Sum (sum measuredValues)) (MeasuredRope.measure measured)
      assertEqual "concurrent measured index" (Just 64) (MeasuredRope.index 63 measured)

runConcurrent :: String -> Int -> IO () -> IO ()
runConcurrent label workerCount action = do
  boxes <- replicateM workerCount newEmptyMVar
  forM_ boxes $ \box -> do
    _ <- forkIO $ do
      result <- try action :: IO (Either SomeException ())
      putMVar box result
    pure ()
  results <- mapM takeMVar boxes
  forM_ results $ \result ->
    case result of
      Right () -> pure ()
      Left exception -> fail (label ++ ": worker failed: " ++ show exception)

-- A non-commutative measure (list concatenation) that records the order in
-- which element measures were combined into the cached total.
newtype Trace = Trace [Int]
  deriving (Eq, Show)

instance Semigroup Trace where
  Trace left <> Trace right = Trace (left ++ right)

instance Monoid Trace where
  mempty = Trace []

newtype Traced = Traced Int
  deriving (Eq, Show)

instance FT.Measured Trace Traced where
  measure (Traced value) = Trace [value]

-- An element whose ordering ignores the label, standing in for
-- comparer-equal-but-distinct instances.
data Keyed = Keyed Int String
  deriving (Show)

instance Eq Keyed where
  Keyed left _ == Keyed right _ = left == right

instance Ord Keyed where
  compare (Keyed left _) (Keyed right _) = compare left right

keyedLabel :: Keyed -> String
keyedLabel (Keyed _ label) = label

{-# NOINLINE countingCompare #-}
countingCompare :: Ord a => IORef Int -> a -> a -> Ordering
countingCompare calls left right = unsafePerformIO $ do
  modifyIORef' calls (+ 1)
  pure (compare left right)

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")
