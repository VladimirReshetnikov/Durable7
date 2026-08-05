{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MultiParamTypeClasses #-}
{-# LANGUAGE NumericUnderscores #-}

-- | Entry point for the durable7-fingertree test suite.
module Main (main) where

import Prelude hiding (lines, null, reverse, splitAt)

import Control.Concurrent (forkIO, newEmptyMVar, putMVar, takeMVar)
import Control.Exception (ErrorCall, SomeException, displayException, evaluate, try)
import Control.Monad (forM_, replicateM, when)
import Data.IORef (IORef, modifyIORef', newIORef, readIORef, writeIORef)
import qualified Data.List as List
import qualified Data.Map.Strict as Map
import Data.Monoid (Sum(..))
import Data.Word (Word32)
import System.IO.Unsafe (unsafePerformIO)
import System.Mem.StableName (eqStableName, makeStableName)

import qualified Durable7.FingerTree.Deque as Deque
import qualified Durable7.FingerTree.BrodalOkasakiHeap as BrodalOkasakiHeap
import qualified Durable7.FingerTree.IntervalTree as IntervalTree
import qualified Durable7.FingerTree.IntervalMap as IntervalMap
import qualified Durable7.FingerTree.Measured as FT
import Durable7.FingerTree.Measured (ViewL(..), ViewR(..))
import Durable7.FingerTree.Measures (Elem(..), Size(..))
import qualified Durable7.FingerTree.MeasuredRope as MeasuredRope
import qualified Durable7.FingerTree.PriorityQueue as PriorityQueue
import qualified Durable7.FingerTree.PrioritySearchQueue as PrioritySearchQueue
import qualified Durable7.FingerTree.PersistentChunkedBitSet as ChunkedBitSet
import qualified Durable7.FingerTree.ReversibleDeque as ReversibleDeque
import qualified Durable7.FingerTree.Rope as Rope
import qualified Durable7.FingerTree.Rope.Text as RopeText
import qualified Durable7.FingerTree.RrbVector as RrbVector
import qualified Durable7.FingerTree.RangeUpdateSequence as RangeUpdate
import qualified Durable7.FingerTree.SortedBag as SortedBag
import qualified Durable7.FingerTree.SortedMap as SortedMap
import qualified Durable7.FingerTree.SortedSet as SortedSet
import qualified AncestralSliceQueueTests
import qualified BilateralAncestralDequeTests
import qualified CanonicalSortedSetTests
import qualified ContextualRankSequenceTests
import qualified PersistentDeltaMapTests
import qualified PersistentMonotoneActionHeapTests
import qualified PersistentRunDeltaVectorTests
import qualified RangeUpdateSequenceTests
import qualified OrderedSearchCursorTests

-- | Entry point running this package's test suite.
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
  testPersistentChunkedBitSet
  CanonicalSortedSetTests.run
  RangeUpdateSequenceTests.run
  testIntervalTree
  testIntervalMap
  testRrbVector
  testSequenceCursors
  OrderedSearchCursorTests.run
  testRopes
  testRopeCursor
  testMeasuredRopeCursor
  testTextRope
  AncestralSliceQueueTests.run
  BilateralAncestralDequeTests.run
  ContextualRankSequenceTests.run
  PersistentDeltaMapTests.run
  PersistentRunDeltaVectorTests.run
  PersistentMonotoneActionHeapTests.run
  testConcurrentReads
  putStrLn "durable7-fingertree tests passed"

testSequenceCursors :: IO ()
testSequenceCursors = do
  let dequeSource = Deque.fromList [Just (1 :: Int), Nothing, Just 3]
  deque0 <- expectJust "deque cursor" (Deque.cursorAt 2 dequeSource)
  assertEqual "deque cursor stored Nothing" (Just Nothing) (Deque.cursorPeekPrevious deque0)
  assertEqual "deque cursor next" (Just (Just 3)) (Deque.cursorPeekNext deque0)
  deque1 <- expectJust "deque cursor delete" (Deque.cursorDeletePrevious (Deque.cursorInsertList [Just 7, Just 8] deque0))
  deque2 <- expectJust "deque cursor replace" (Deque.cursorReplaceNext (Just 9) deque1)
  assertEqual "deque cursor position" 3 (Deque.cursorPosition deque2)
  assertEqual "deque cursor edited" [Just 1, Nothing, Just 7, Just 9] (Deque.toList (Deque.cursorSnapshot deque2))
  assertEqual "deque cursor retained" [Just 1, Nothing, Just 3] (Deque.toList dequeSource)

  let reversibleSource = ReversibleDeque.reverse (ReversibleDeque.fromList [1 :: Int, 2, 3, 4])
  reversible0 <- expectJust "reversible cursor" (ReversibleDeque.cursorAt 1 reversibleSource)
  assertEqual "reversible cursor previous" (Just 4) (ReversibleDeque.cursorPeekPrevious reversible0)
  reversible1 <- expectJust "reversible cursor delete" (ReversibleDeque.cursorDeleteNext (ReversibleDeque.cursorInsert 9 reversible0))
  let reversible2 = ReversibleDeque.cursorReverse reversible1
  assertEqual "reversible cursor edit" [4, 9, 2, 1] (ReversibleDeque.toList (ReversibleDeque.cursorSnapshot reversible1))
  assertEqual "reversible cursor mapped position" 2 (ReversibleDeque.cursorPosition reversible2)
  assertEqual "reversible cursor reverse" [1, 2, 9, 4] (ReversibleDeque.toList (ReversibleDeque.cursorSnapshot reversible2))

  let measuredSource :: FT.FingerTree Size (Elem Int)
      measuredSource = FT.fromList (map Elem [2, 3, 5, 7])
      measured0 = FT.cursorSearchCursor (FT.cursorByMeasure (\(Size total) -> total > 2) measuredSource)
      measured1 = FT.cursorInsert (Elem 11) measured0
  measured2 <- expectJust "measured cursor delete" (FT.cursorDeleteNext measured1)
  measured3 <- expectJust "measured cursor replace" (FT.cursorReplaceNext (Elem 13) measured2)
  assertEqual "measured cursor before" (Size 2) (FT.cursorMeasureBefore measured0)
  assertEqual "measured cursor after" (Size 2) (FT.cursorMeasureAfter measured0)
  assertEqual "measured cursor edited" [2, 3, 11, 13] (map getElem (FT.toList (FT.cursorSnapshot measured3)))
  assertEqual "measured cursor retained" [2, 3, 5, 7] (map getElem (FT.toList measuredSource))

  let vectorSource = RrbVector.fromList [0 :: Int .. 95]
      vector0 = RrbVector.cursorInsertVector (RrbVector.fromList [500, 501, 502])
        (expectPure "RRB cursor" (RrbVector.cursorAt 32 vectorSource))
  assertEqual "RRB cursor position" 35 (RrbVector.cursorPosition vector0)
  assertEqual "RRB cursor splice" [30, 31, 500, 501, 502, 32, 33]
    (take 7 (drop 30 (RrbVector.toList (RrbVector.cursorSnapshot vector0))))
  assertEqual "RRB cursor retained" [0 .. 95] (RrbVector.toList vectorSource)

  let rangeSource = RangeUpdate.fromListWith cursorRangeAlgebra [1 :: Int, 2, 3, 4]
  range0 <- expectJust "Range cursor" (RangeUpdate.cursorAt 2 rangeSource)
  assertEqual "Range cursor measure before" 3 (RangeUpdate.cursorMeasureBefore range0)
  assertEqual "Range cursor measure after" 7 (RangeUpdate.cursorMeasureAfter range0)
  range1 <- expectJust "Range apply previous" (RangeUpdate.cursorApplyPrevious 1 10 range0)
  range2 <- expectJust "Range apply next" (RangeUpdate.cursorApplyNext 2 20 range1)
  range3 <- expectJust "Range replace next" (RangeUpdate.cursorReplaceNext 99 range2)
  assertEqual "Range cursor edited" [1, 12, 99, 24] (RangeUpdate.toList (RangeUpdate.cursorSnapshot range3))
  assertEqual "Range cursor retained" [1, 2, 3, 4] (RangeUpdate.toList rangeSource)

cursorRangeAlgebra :: RangeUpdate.RangeUpdateAlgebra Int Int Int
cursorRangeAlgebra =
  RangeUpdate.RangeUpdateAlgebra
    { RangeUpdate.algebraEmptyMeasure = 0
    , RangeUpdate.algebraCombine = (+)
    , RangeUpdate.algebraMeasureElement = id
    , RangeUpdate.algebraIdentityTag = 0
    , RangeUpdate.algebraIsIdentity = (== 0)
    , RangeUpdate.algebraCompose = (+)
    , RangeUpdate.algebraApplyElement = (+)
    , RangeUpdate.algebraApplyMeasure = \tag total amount -> total + tag * amount
    }

expectPure :: String -> Maybe a -> a
expectPure _ (Just value) = value
expectPure label Nothing = error (label ++ ": expected Just")

testPersistentChunkedBitSet :: IO ()
testPersistentChunkedBitSet = do
  let values0 = ChunkedBitSet.fromList [0, 1, 63, 64, 130, 130]
      snapshot = values0
      values1 = ChunkedBitSet.delete 64 (ChunkedBitSet.insert 65 values0)
      other = ChunkedBitSet.fromList [1, 64, 129]
  assertEqual "chunked bit-set enumeration" [0, 1, 63, 64, 130] (ChunkedBitSet.toList values0)
  assertEqual "chunked bit-set count" 5 (ChunkedBitSet.size values0)
  assertEqual "chunked bit-set chunks" 3 (ChunkedBitSet.chunkCount values0)
  assertEqual "chunked bit-set inclusive rank" 4 (ChunkedBitSet.rank 64 values0)
  assertEqual "chunked bit-set select" (Just 63) (ChunkedBitSet.select 2 values0)
  assertEqual "chunked bit-set edit" [0, 1, 63, 65, 130] (ChunkedBitSet.toList values1)
  assertEqual "chunked bit-set union" [0, 1, 63, 64, 129, 130]
    (ChunkedBitSet.toList (ChunkedBitSet.union values0 other))
  assertEqual "chunked bit-set intersection" [1, 64]
    (ChunkedBitSet.toList (ChunkedBitSet.intersection values0 other))
  assertEqual "chunked bit-set difference" [0, 63, 130]
    (ChunkedBitSet.toList (ChunkedBitSet.difference values0 other))
  let boundary = ChunkedBitSet.insert 2147483647 ChunkedBitSet.empty
  assertEqual "chunked bit-set signed-32 boundary" [2147483647] (ChunkedBitSet.toList boundary)
  assertBool "chunked bit-set rejects membership beyond signed-32 domain"
    (not (ChunkedBitSet.member 2147483648 boundary))
  assertBool "chunked bit-set snapshot preserved" (ChunkedBitSet.member 64 snapshot)
  assertBool "chunked bit-set invariant" (ChunkedBitSet.validStructure values1)

testIntervalMap :: IO ()
testIntervalMap = do
  let a = IntervalTree.Interval (1 :: Int) 5
      b = IntervalTree.Interval 1 3
      c = IntervalTree.Interval 4 9
      map0 = IntervalMap.fromList [(a, "a"), (b, "b"), (c, "c")]
      map1 = IntervalMap.set b "B" map0
      map2 = IntervalMap.delete a map1
      probe = IntervalTree.Interval 2 6
  assertEqual "interval map exact replacement" (Just "B") (IntervalMap.lookup b map1)
  assertEqual "interval map overlap count" 3 (IntervalMap.countOverlaps probe map1)
  assertEqual "interval map overlap values" 3 (length (IntervalMap.findOverlaps probe map1))
  assertEqual "interval map snapshot" (Just "a") (IntervalMap.lookup a map0)
  assertEqual "interval map removal" Nothing (IntervalMap.lookup a map2)
  assertBool "interval map invariant" (IntervalMap.validStructure map2)

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

-- | Locks the incremental cached-measure order in cons/snoc under a
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
  assertEqual "map slice selects a rank window" [(2, "bb")]
    (maybe [] SortedMap.toList (SortedMap.slice 1 1 dict))
  let ranged = SortedMap.fromList [(key, show key) | key <- [0, 10 .. 90 :: Int]]
  assertEqual "map key range is inclusive at both ends" [20, 30, 40]
    (SortedMap.keys (SortedMap.keyRange 20 40 ranged))
  assertEqual "map key range accepts probes between stored keys" [30, 40]
    (SortedMap.keys (SortedMap.keyRange 25 45 ranged))
  assertEqual "map key range yields nothing when inverted" []
    (SortedMap.keys (SortedMap.keyRange 40 20 ranged))
  assertEqual "map key range yields nothing for an empty window" []
    (SortedMap.keys (SortedMap.keyRange 21 29 ranged))
  assertEqual "map key range spanning past both ends keeps everything" 10
    (SortedMap.count (SortedMap.keyRange (-5) 500 ranged))
  assertEqual "map key range of a single stored key" [(30, "30")]
    (SortedMap.toList (SortedMap.keyRange 30 30 ranged))
  -- The probes select boundaries and are never stored, so a comparison coarser than equality still
  -- sees the representatives the map retained.
  let keyedMap = SortedMap.fromList [(Keyed (1 :: Int) "one", 'x'), (Keyed 2 "two", 'y')]
  assertEqual "map key range retains stored key representatives" ["one", "two"]
    (map keyedLabel
      (SortedMap.keys (SortedMap.keyRange (Keyed 1 "probe") (Keyed 2 "probe") keyedMap)))
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

testRopeCursor :: IO ()
testRopeCursor = do
  let emptyRope = Rope.empty :: Rope.Rope Int
      emptyCursor = Rope.cursor emptyRope
  assertEqual "empty cursor position" 0 (Rope.cursorPosition emptyCursor)
  assertEqual "empty cursor count" 0 (Rope.cursorCount emptyCursor)
  assertBool "empty cursor reports null" (Rope.cursorNull emptyCursor)
  assertBool "empty cursor is at start" (Rope.isAtStart emptyCursor)
  assertBool "empty cursor is at end" (Rope.isAtEnd emptyCursor)
  assertNothing "empty cursor has no previous value" (Rope.peekPrevious emptyCursor)
  assertNothing "empty cursor has no next value" (Rope.peekNext emptyCursor)
  assertNothing "empty cursor cannot move previous" (Rope.movePrevious emptyCursor)
  assertNothing "empty cursor cannot move next" (Rope.moveNext emptyCursor)
  assertSameStableName "empty cursor retains exact source rope" emptyRope (Rope.snapshot emptyCursor)

  let source = Rope.fromList [0 :: Int .. 129]
      start = Rope.cursor source
  assertEqual "cursor start position" 0 (Rope.cursorPosition start)
  assertEqual "cursor start count" 130 (Rope.cursorCount start)
  assertBool "nonempty cursor does not report null" (not (Rope.cursorNull start))
  assertBool "cursor starts at first gap" (Rope.isAtStart start)
  assertBool "nonempty start is not end" (not (Rope.isAtEnd start))
  assertNothing "start has no previous value" (Rope.peekPrevious start)
  assertEqual "start next value" (Just 0) (Rope.peekNext start)
  assertNothing "cursor cannot move before start" (Rope.movePrevious start)
  assertSameStableName "cursor retains exact source rope" source (Rope.snapshot start)
  assertNothing "negative cursor gap rejected" (Rope.cursorAt (-1) source)
  assertNothing "past-end cursor gap rejected" (Rope.cursorAt 131 source)
  assertEqual
    "direct range insertion preserves order across a chunk boundary"
    (Just ([60, 61, 62, 63, -2, -3, 64, 65, 66, 67] :: [Int]))
    (take 10 . drop 60 . Rope.toList <$> Rope.insertRangeAt 64 [-2, -3] source)
  assertNothing
    "invalid direct range gap does not force its source"
    (Rope.insertRangeAt (-1) (error "invalid range source was forced") source)
  assertNothing "past-end direct range gap rejected" (Rope.insertRangeAt 131 [1] source)
  unchangedRope <- expectJust "empty rope range insertion" (Rope.insertRangeAt 64 [] source)
  assertSameStableName "empty rope range insertion preserves source identity" source unchangedRope

  middle <- expectJust "middle cursor" (Rope.cursorAt 64 source)
  assertEqual "middle previous value" (Just 63) (Rope.peekPrevious middle)
  assertEqual "middle next value" (Just 64) (Rope.peekNext middle)
  same <- expectJust "same-position cursor seek" (Rope.seek 64 middle)
  assertEqual "same-position seek preserves gap" 64 (Rope.cursorPosition same)
  assertSameStableName "same-position seek preserves source identity" source (Rope.snapshot same)
  moved <- expectJust "cursor move next" (Rope.moveNext middle)
  assertSameStableName "cursor navigation retains exact source rope" source (Rope.snapshot moved)
  assertNothing "negative cursor seek rejected" (Rope.seek (-1) middle)
  assertNothing "past-end cursor seek rejected" (Rope.seek 131 middle)
  let emptyInsertion = Rope.insertRange [] middle
  assertEqual "empty range insertion preserves gap" 64 (Rope.cursorPosition emptyInsertion)
  assertSameStableName "empty range insertion preserves source identity" source (Rope.snapshot emptyInsertion)

  let ranged = Rope.insertRange [-2, -3] middle
  assertEqual "range insertion advances gap" 66 (Rope.cursorPosition ranged)
  assertEqual "range insertion previous value" (Just (-3)) (Rope.peekPrevious ranged)
  assertEqual "range insertion next value" (Just 64) (Rope.peekNext ranged)
  let inserted = Rope.insert (-1) ranged
  assertEqual "single insertion advances gap" 67 (Rope.cursorPosition inserted)
  assertEqual "single insertion previous value" (Just (-1)) (Rope.peekPrevious inserted)
  restored <- expectJust "delete previous" (Rope.deletePrevious inserted)
  assertEqual "backspace restores ranged contents" (Rope.toList (Rope.snapshot ranged)) (Rope.toList (Rope.snapshot restored))
  replaced <- expectJust "replace next" (Rope.replaceNext 99_999 restored)
  assertEqual "replacement keeps gap" (Rope.cursorPosition restored) (Rope.cursorPosition replaced)
  assertEqual "replacement stores supplied value" (Just 99_999) (Rope.peekNext replaced)
  deleted <- expectJust "delete next" (Rope.deleteNext replaced)
  assertEqual "delete next keeps gap" (Rope.cursorPosition replaced) (Rope.cursorPosition deleted)
  assertEqual "delete next exposes successor" (Just 65) (Rope.peekNext deleted)

  end <- expectJust "end cursor" (Rope.cursorAt (Rope.count source) source)
  assertBool "end cursor reports end" (Rope.isAtEnd end)
  assertEqual "end previous value" (Just 129) (Rope.peekPrevious end)
  assertNothing "end has no next value" (Rope.peekNext end)
  assertNothing "cursor cannot move beyond end" (Rope.moveNext end)
  assertNothing "cannot backspace at start" (Rope.deletePrevious start)
  assertNothing "cannot delete next at end" (Rope.deleteNext end)
  assertNothing "cannot replace next at end" (Rope.replaceNext 0 end)

  nullable <- expectJust "nullable cursor" (Rope.cursorAt 1 (Rope.fromList [Nothing, Just (1 :: Int)]))
  assertEqual "peek distinguishes stored Nothing from a boundary" (Just Nothing) (Rope.peekPrevious nullable)
  assertEqual "nullable cursor next value" (Just (Just 1)) (Rope.peekNext nullable)

  let originalRepresentative = CursorRepresentative 7 "original"
      replacementRepresentative = CursorRepresentative 7 "replacement"
      representativeRope = Rope.fromList
        [ CursorRepresentative 6 "left"
        , originalRepresentative
        , CursorRepresentative 8 "right"
        ]
  representativeSource <- expectJust "representative cursor" (Rope.cursorAt 1 representativeRope)
  representativeEdit <- expectJust "representative replacement" (Rope.replaceNext replacementRepresentative representativeSource)
  assertEqual
    "replacement stores supplied representative without an Eq constraint"
    (Just "replacement")
    (cursorRepresentativeLabel <$> Rope.peekNext representativeEdit)
  assertEqual
    "replacement preserves source representative"
    (Just "original")
    (cursorRepresentativeLabel <$> Rope.peekNext representativeSource)

  let retainedRope = Rope.fromList [0 :: Int .. 255]
  retained <- expectJust "retained cursor" (Rope.cursorAt 128 retainedRope)
  let leftBranch = Rope.insert (-10) retained
      rightBranch = Rope.insert (-20) retained
  assertEqual "retained source remains unchanged" (Just 128) (Rope.peekNext retained)
  assertEqual "left branch value" (Just (-10)) (Rope.index 128 (Rope.snapshot leftBranch))
  assertEqual "right branch value" (Just (-20)) (Rope.index 128 (Rope.snapshot rightBranch))
  assertEqual "left branch keeps source contents" [0 :: Int .. 255] (Rope.toList retainedRope)
  assertEqual "right branch keeps source contents" [0 :: Int .. 255] (Rope.toList retainedRope)
  assertSameStableName
    "left cursor branch retains an untouched far chunk"
    (Rope.chunks retainedRope !! 3)
    (Rope.chunks (Rope.snapshot leftBranch) !! 4)
  assertSameStableName
    "right cursor branch retains an untouched far chunk"
    (Rope.chunks retainedRope !! 3)
    (Rope.chunks (Rope.snapshot rightBranch) !! 4)

  runRopeCursorModel 0 0x9e37_79b9 (Rope.cursor (Rope.empty :: Rope.Rope Int)) [] 0
  testRopeGrowthOverflow

runRopeCursorModel :: Int -> Word32 -> Rope.RopeCursor Int -> [Int] -> Int -> IO ()
runRopeCursorModel !step !randomState cursorValue model position
  | step == 750 = assertRopeCursorModel step cursorValue model position
  | otherwise = do
      let next = nextRrbRandom randomState
          operation = fromIntegral (next `mod` 7) :: Int
      (edited, expected, nextPosition) <-
        case operation of
          0 ->
            pure
              ( Rope.insert step cursorValue
              , take position model ++ [step] ++ drop position model
              , position + 1
              )
          1 ->
            let values = [step, negate step]
             in pure
                  ( Rope.insertRange values cursorValue
                  , take position model ++ values ++ drop position model
                  , position + length values
                  )
          2 ->
            case Rope.deletePrevious cursorValue of
              Nothing -> do
                assertEqual (modelLabel "backspace boundary" step) 0 position
                pure (cursorValue, model, position)
              Just result ->
                let previous = position - 1
                 in pure (result, take previous model ++ drop position model, previous)
          3 ->
            case Rope.deleteNext cursorValue of
              Nothing -> do
                assertEqual (modelLabel "delete boundary" step) (length model) position
                pure (cursorValue, model, position)
              Just result -> pure (result, take position model ++ drop (position + 1) model, position)
          4 ->
            case Rope.replaceNext step cursorValue of
              Nothing -> do
                assertEqual (modelLabel "replace boundary" step) (length model) position
                pure (cursorValue, model, position)
              Just result -> pure (result, take position model ++ [step] ++ drop (position + 1) model, position)
          5
            | (next `div` 4096) `mod` 2 == 0 ->
                case Rope.movePrevious cursorValue of
                  Nothing -> do
                    assertEqual (modelLabel "move previous boundary" step) 0 position
                    pure (cursorValue, model, position)
                  Just result -> pure (result, model, position - 1)
            | otherwise ->
                case Rope.moveNext cursorValue of
                  Nothing -> do
                    assertEqual (modelLabel "move next boundary" step) (length model) position
                    pure (cursorValue, model, position)
                  Just result -> pure (result, model, position + 1)
          _ -> do
            let target = fromIntegral ((next `div` 256) `mod` fromIntegral (length model + 2))
            case Rope.seek target cursorValue of
              Nothing -> do
                assertBool (modelLabel "invalid seek" step) (target > length model)
                pure (cursorValue, model, position)
              Just result -> pure (result, model, target)
      assertRopeCursorModel step edited expected nextPosition
      runRopeCursorModel (step + 1) next edited expected nextPosition

assertRopeCursorModel :: Int -> Rope.RopeCursor Int -> [Int] -> Int -> IO ()
assertRopeCursorModel step cursorValue model position = do
  assertEqual (modelLabel "position" step) position (Rope.cursorPosition cursorValue)
  assertEqual (modelLabel "count" step) (length model) (Rope.cursorCount cursorValue)
  assertEqual (modelLabel "start" step) (position == 0) (Rope.isAtStart cursorValue)
  assertEqual (modelLabel "end" step) (position == length model) (Rope.isAtEnd cursorValue)
  assertEqual
    (modelLabel "previous" step)
    (if position == 0 then Nothing else Just (model !! (position - 1)))
    (Rope.peekPrevious cursorValue)
  assertEqual
    (modelLabel "next" step)
    (if position == length model then Nothing else Just (model !! position))
    (Rope.peekNext cursorValue)
  assertEqual (modelLabel "snapshot" step) model (Rope.toList (Rope.snapshot cursorValue))

modelLabel :: String -> Int -> String
modelLabel label step = "rope cursor model " ++ label ++ " at step " ++ show step

testRopeGrowthOverflow :: IO ()
testRopeGrowthOverflow = do
  let seed = Rope.singleton (7 :: Int)
      grow current
        | Rope.count current > maxBound `div` 2 = current
        | otherwise = grow (Rope.append current current)
      power = grow seed
      powerCount = Rope.count power
  assertEqual "shared power rope count" (maxBound `div` 2 + 1) powerCount
  assertEqual "shared power rope first value" (Just 7) (Rope.index 0 power)
  assertEqual "shared power rope last value" (Just 7) (Rope.index (powerCount - 1) power)
  assertLengthOverflow "overflowing rope self-append" (evaluate (Rope.count (Rope.append power power)))

  almostPower <- expectJust "remove from shared power rope" (Rope.deleteAt 0 power)
  let maximumRope = Rope.append power almostPower
      maximumCursor = Rope.cursor maximumRope
  maximumEnd <- expectJust "maximum end cursor" (Rope.cursorAt maxBound maximumRope)
  assertEqual "maximum representable rope count" maxBound (Rope.count maximumRope)
  assertLengthOverflow "overflowing rope cons" (evaluate (Rope.count (Rope.cons 8 maximumRope)))
  assertLengthOverflow "overflowing rope snoc" (evaluate (Rope.count (Rope.snoc maximumRope 8)))
  assertLengthOverflow
    "overflowing rope point insertion"
    (evaluate (maybe (-1) Rope.count (Rope.insertAt 0 8 maximumRope)))
  assertLengthOverflow
    "overflowing rope range insertion"
    (evaluate (maybe (-1) Rope.count (Rope.insertRangeAt 0 [8] maximumRope)))
  assertLengthOverflow "overflowing maximum rope append" (evaluate (Rope.count (Rope.append maximumRope seed)))
  assertLengthOverflow "overflowing maximum rope prefix append" (evaluate (Rope.count (Rope.append seed maximumRope)))
  assertLengthOverflow "overflowing cursor insertion at start" (evaluate (Rope.cursorCount (Rope.insert 8 maximumCursor)))
  assertLengthOverflow
    "overflowing cursor range insertion at start"
    (evaluate (Rope.cursorCount (Rope.insertRange [8] maximumCursor)))
  assertLengthOverflow "overflowing cursor position at end" (evaluate (Rope.cursorCount (Rope.insert 8 maximumEnd)))
  assertLengthOverflow
    "overflowing cursor range position at end"
    (evaluate (Rope.cursorCount (Rope.insertRange [8] maximumEnd)))

  assertEqual "failed growth preserves maximum rope" maxBound (Rope.count maximumRope)
  assertEqual "failed growth preserves maximum front" (Just 7) (Rope.index 0 maximumRope)
  assertEqual "failed growth preserves maximum back" (Just 7) (Rope.index (maxBound - 1) maximumRope)
  assertEqual "failed growth preserves shared power" powerCount (Rope.count power)
  assertEqual "failed growth preserves almost-power" (powerCount - 1) (Rope.count almostPower)
  assertEqual "failed growth preserves seed" [7] (Rope.toList seed)

testMeasuredRopeCursor :: IO ()
testMeasuredRopeCursor = do
  let emptyRope :: MeasuredRope.MeasuredRope (Sum Int) Int
      emptyRope = MeasuredRope.emptyWith (const (Sum 1))
      emptyCursor = MeasuredRope.cursor emptyRope
  assertEqual "empty measured cursor position" 0 (MeasuredRope.cursorPosition emptyCursor)
  assertEqual "empty measured cursor count" 0 (MeasuredRope.cursorCount emptyCursor)
  assertBool "empty measured cursor reports null" (MeasuredRope.cursorNull emptyCursor)
  assertBool "empty measured cursor is at start" (MeasuredRope.isAtStart emptyCursor)
  assertBool "empty measured cursor is at end" (MeasuredRope.isAtEnd emptyCursor)
  assertEqual "empty measured cursor before" (Sum 0) (MeasuredRope.measureBefore emptyCursor)
  assertEqual "empty measured cursor after" (Sum 0) (MeasuredRope.measureAfter emptyCursor)
  assertNothing "empty measured cursor has no previous" (MeasuredRope.peekPrevious emptyCursor)
  assertNothing "empty measured cursor has no next" (MeasuredRope.peekNext emptyCursor)
  assertNothing "empty measured cursor cannot move previous" (MeasuredRope.movePrevious emptyCursor)
  assertNothing "empty measured cursor cannot move next" (MeasuredRope.moveNext emptyCursor)
  assertSameStableName
    "empty measured cursor retains exact source"
    emptyRope
    (MeasuredRope.snapshot emptyCursor)

  let source :: MeasuredRope.MeasuredRope (Sum Int) Int
      source = MeasuredRope.fromListWith (const (Sum 1)) [0 :: Int .. 129]
      start = MeasuredRope.cursor source
  assertEqual "measured cursor start position" 0 (MeasuredRope.cursorPosition start)
  assertEqual "measured cursor start count" 130 (MeasuredRope.cursorCount start)
  assertEqual "measured cursor start before" (Sum 0) (MeasuredRope.measureBefore start)
  assertEqual "measured cursor start after" (Sum 130) (MeasuredRope.measureAfter start)
  assertNothing "negative measured cursor gap rejected" (MeasuredRope.cursorAt (-1) source)
  assertNothing "past-end measured cursor gap rejected" (MeasuredRope.cursorAt 131 source)
  assertNothing
    "invalid measured range gap does not force values"
    (MeasuredRope.insertRangeAt (-1) (error "invalid measured range source was forced") source)
  assertEqual
    "direct measured range insertion preserves order"
    (Just ([60, 61, 62, 63, -2, -3, 64, 65, 66, 67] :: [Int]))
    (take 10 . drop 60 . MeasuredRope.toList <$> MeasuredRope.insertRangeAt 64 [-2, -3] source)
  unchanged <- expectJust "empty measured direct range insertion" (MeasuredRope.insertRangeAt 64 [] source)
  assertSameStableName "empty measured direct range retains source" source unchanged

  middle <- expectJust "middle measured cursor" (MeasuredRope.cursorAt 64 source)
  assertEqual "middle measured previous" (Just 63) (MeasuredRope.peekPrevious middle)
  assertEqual "middle measured next" (Just 64) (MeasuredRope.peekNext middle)
  assertEqual "middle measured before" (Sum 64) (MeasuredRope.measureBefore middle)
  assertEqual "middle measured after" (Sum 66) (MeasuredRope.measureAfter middle)
  same <- expectJust "same measured cursor seek" (MeasuredRope.seek 64 middle)
  assertSameStableName "same measured seek retains source" source (MeasuredRope.snapshot same)
  moved <- expectJust "measured cursor move next" (MeasuredRope.moveNext middle)
  assertSameStableName "measured navigation retains source" source (MeasuredRope.snapshot moved)
  assertNothing "negative measured cursor seek rejected" (MeasuredRope.seek (-1) middle)
  assertNothing "past-end measured cursor seek rejected" (MeasuredRope.seek 131 middle)
  let unchangedCursor = MeasuredRope.insertRange [] middle
  assertEqual "empty measured range preserves gap" 64 (MeasuredRope.cursorPosition unchangedCursor)
  assertSameStableName
    "empty measured range preserves source"
    source
    (MeasuredRope.snapshot unchangedCursor)

  let ranged = MeasuredRope.insertRange [-2, -3] middle
  assertEqual "measured range advances gap" 66 (MeasuredRope.cursorPosition ranged)
  assertEqual "measured range previous" (Just (-3)) (MeasuredRope.peekPrevious ranged)
  assertEqual "measured range next" (Just 64) (MeasuredRope.peekNext ranged)
  assertEqual "measured range before" (Sum 66) (MeasuredRope.measureBefore ranged)
  assertEqual "measured range after" (Sum 66) (MeasuredRope.measureAfter ranged)
  let inserted = MeasuredRope.insert (-1) ranged
  restored <- expectJust "measured delete previous" (MeasuredRope.deletePrevious inserted)
  assertEqual
    "measured backspace restores range"
    (MeasuredRope.toList (MeasuredRope.snapshot ranged))
    (MeasuredRope.toList (MeasuredRope.snapshot restored))
  replaced <- expectJust "measured replace next" (MeasuredRope.replaceNext 99_999 restored)
  assertEqual "measured replacement keeps gap" 66 (MeasuredRope.cursorPosition replaced)
  assertEqual "measured replacement stores value" (Just 99_999) (MeasuredRope.peekNext replaced)
  deleted <- expectJust "measured delete next" (MeasuredRope.deleteNext replaced)
  assertEqual "measured delete next keeps gap" 66 (MeasuredRope.cursorPosition deleted)
  assertEqual "measured delete next exposes successor" (Just 65) (MeasuredRope.peekNext deleted)

  end <- expectJust "measured end cursor" (MeasuredRope.cursorAt 130 source)
  assertBool "measured end reports end" (MeasuredRope.isAtEnd end)
  assertEqual "measured end before" (Sum 130) (MeasuredRope.measureBefore end)
  assertEqual "measured end after" (Sum 0) (MeasuredRope.measureAfter end)
  assertNothing "measured cursor cannot move beyond end" (MeasuredRope.moveNext end)
  assertNothing "measured cannot backspace at start" (MeasuredRope.deletePrevious start)
  assertNothing "measured cannot delete at end" (MeasuredRope.deleteNext end)
  assertNothing "measured cannot replace at end" (MeasuredRope.replaceNext 0 end)

  let nullableRope :: MeasuredRope.MeasuredRope (Sum Int) (Maybe Int)
      nullableRope = MeasuredRope.fromListWith (const (Sum 1)) [Nothing, Just 1]
  nullable <- expectJust
    "nullable measured cursor"
    (MeasuredRope.cursorAt 1 nullableRope)
  assertEqual
    "measured peek distinguishes stored Nothing"
    (Just Nothing)
    (MeasuredRope.peekPrevious nullable)
  assertEqual "nullable measured next" (Just (Just 1)) (MeasuredRope.peekNext nullable)

  let originalRepresentative = CursorRepresentative 7 "original"
      replacementRepresentative = CursorRepresentative 7 "replacement"
      representativeRope :: MeasuredRope.MeasuredRope (Sum Int) CursorRepresentative
      representativeRope = MeasuredRope.fromListWith (const (Sum 1))
        [ CursorRepresentative 6 "left"
        , originalRepresentative
        , CursorRepresentative 8 "right"
        ]
  representativeSource <- expectJust
    "measured representative cursor"
    (MeasuredRope.cursorAt 1 representativeRope)
  representativeEdit <- expectJust
    "measured representative replacement"
    (MeasuredRope.replaceNext replacementRepresentative representativeSource)
  assertEqual
    "measured replacement stores supplied representative without Eq"
    (Just "replacement")
    (cursorRepresentativeLabel <$> MeasuredRope.peekNext representativeEdit)
  assertEqual
    "measured replacement retains source representative"
    (Just "original")
    (cursorRepresentativeLabel <$> MeasuredRope.peekNext representativeSource)

  retained <- expectJust "retained measured cursor" (MeasuredRope.cursorAt 64 source)
  let leftBranch = MeasuredRope.insert (-10) retained
      rightBranch = MeasuredRope.insert (-20) retained
  assertEqual "retained measured source remains unchanged" (Just 64) (MeasuredRope.peekNext retained)
  assertEqual "left measured branch value" (Just (-10)) (MeasuredRope.index 64 (MeasuredRope.snapshot leftBranch))
  assertEqual "right measured branch value" (Just (-20)) (MeasuredRope.index 64 (MeasuredRope.snapshot rightBranch))
  assertSameStableName "retained measured cursor keeps exact source" source (MeasuredRope.snapshot retained)

  testMeasuredCursorOrderedMeasures
  testMeasuredCursorSearch
  runMeasuredCursorModel 0 0x6d2b_79f5 (MeasuredRope.cursor (MeasuredRope.emptyWith (const (Sum 1)))) [] 0
  testMeasuredCursorCallbackFailures
  testMeasuredRopeGrowthOverflow

testMeasuredCursorOrderedMeasures :: IO ()
testMeasuredCursorOrderedMeasures = do
  let model = [0 :: Int .. 299]
      source = MeasuredRope.fromListWith (\value -> Trace [value]) model
  forM_ [0, 1, 15, 16, 17, 63, 64, 65, 127, 128, 255, 256, 299, 300] $ \position -> do
    cursorValue <- expectJust "ordered measured cursor" (MeasuredRope.cursorAt position source)
    assertEqual ("ordered measured prefix at " ++ show position) (Trace (take position model)) (MeasuredRope.measureBefore cursorValue)
    assertEqual ("ordered measured suffix at " ++ show position) (Trace (drop position model)) (MeasuredRope.measureAfter cursorValue)
    assertEqual
      ("ordered measured partition at " ++ show position)
      (MeasuredRope.measure source)
      (MeasuredRope.measureBefore cursorValue <> MeasuredRope.measureAfter cursorValue)

  sourceCursor <- expectJust "ordered measured edit cursor" (MeasuredRope.cursorAt 128 source)
  replaced <- expectJust
    "ordered measured edit replacement"
    (MeasuredRope.replaceNext (-3) =<< MeasuredRope.deletePrevious (MeasuredRope.insert (-2) (MeasuredRope.insert (-1) sourceCursor)))
  let expected = take 128 model ++ [-1, -3] ++ drop 129 model
  assertEqual "ordered measured edited position" 129 (MeasuredRope.cursorPosition replaced)
  assertEqual "ordered measured edited prefix" (Trace (take 129 expected)) (MeasuredRope.measureBefore replaced)
  assertEqual "ordered measured edited suffix" (Trace (drop 129 expected)) (MeasuredRope.measureAfter replaced)
  assertEqual "ordered measured edited snapshot" expected (MeasuredRope.toList (MeasuredRope.snapshot replaced))
  assertEqual "ordered measured source retained" model (MeasuredRope.toList source)

  let orderedSearch = MeasuredRope.cursorByMeasure (\(Trace values) -> length values >= 129) source
      orderedSearchCursor = MeasuredRope.searchCursor orderedSearch
  assertBool "ordered noncommutative measured search found" (MeasuredRope.searchFound orderedSearch)
  assertEqual "ordered noncommutative measured search position" 128 (MeasuredRope.cursorPosition orderedSearchCursor)
  assertEqual "ordered noncommutative measured search prefix" (Trace (take 128 model)) (MeasuredRope.measureBefore orderedSearchCursor)
  assertEqual "ordered noncommutative measured search suffix" (Trace (drop 128 model)) (MeasuredRope.measureAfter orderedSearchCursor)

testMeasuredCursorSearch :: IO ()
testMeasuredCursorSearch = do
  let source = MeasuredRope.fromListWith Sum [2 :: Int, 3, 5, 7]
      cases =
        [ (0, True, 0, 0)
        , (1, True, 0, 0)
        , (2, True, 0, 0)
        , (3, True, 1, 2)
        , (5, True, 1, 2)
        , (6, True, 2, 5)
        , (10, True, 2, 5)
        , (11, True, 3, 10)
        , (17, True, 3, 10)
        , (18, False, 4, 17)
        ]
  receiver <- expectJust "measured search receiver" (MeasuredRope.cursorAt 3 source)
  forM_ cases $ \(threshold, expectedFound, expectedPosition, expectedBefore) -> do
    let results =
          [ ("factory", MeasuredRope.cursorByMeasure (\(Sum value) -> value >= threshold) source)
          , ("seek", MeasuredRope.seekByMeasure (\(Sum value) -> value >= threshold) receiver)
          ]
    forM_ results $ \(label, result) -> do
      let cursorValue = MeasuredRope.searchCursor result
      assertEqual (label ++ " measured search found at " ++ show threshold) expectedFound (MeasuredRope.searchFound result)
      assertEqual (label ++ " measured search position at " ++ show threshold) expectedPosition (MeasuredRope.cursorPosition cursorValue)
      assertEqual (label ++ " measured search before at " ++ show threshold) (Sum expectedBefore) (MeasuredRope.measureBefore cursorValue)
      assertEqual (label ++ " measured search after at " ++ show threshold) (Sum (17 - expectedBefore)) (MeasuredRope.measureAfter cursorValue)
      assertSameStableName (label ++ " measured search retains source") source (MeasuredRope.snapshot cursorValue)

  let same = MeasuredRope.seekByMeasure (\(Sum value) -> value >= 11) receiver
  assertBool "same-gap measured search found" (MeasuredRope.searchFound same)
  assertEqual "same-gap measured search position" 3 (MeasuredRope.cursorPosition (MeasuredRope.searchCursor same))
  assertSameStableName "same-gap measured search retains source" source (MeasuredRope.snapshot (MeasuredRope.searchCursor same))

  let dirty = MeasuredRope.insert 11 receiver
      dirtyHit = MeasuredRope.seekByMeasure (\(Sum value) -> value >= 11) dirty
      dirtyMiss = MeasuredRope.seekByMeasure (\(Sum value) -> value >= 29) dirty
  assertBool "dirty measured search found" (MeasuredRope.searchFound dirtyHit)
  assertEqual "dirty measured search is absolute" 3 (MeasuredRope.cursorPosition (MeasuredRope.searchCursor dirtyHit))
  assertEqual "dirty measured search snapshot" [2, 3, 5, 11, 7] (MeasuredRope.toList (MeasuredRope.snapshot (MeasuredRope.searchCursor dirtyHit)))
  assertBool "dirty measured search miss" (not (MeasuredRope.searchFound dirtyMiss))
  assertEqual "dirty measured miss returns end" 5 (MeasuredRope.cursorPosition (MeasuredRope.searchCursor dirtyMiss))

  let emptySearch = MeasuredRope.cursorByMeasure (const True) (MeasuredRope.emptyWith Sum :: MeasuredRope.MeasuredRope (Sum Int) Int)
  assertBool "empty measured search misses" (not (MeasuredRope.searchFound emptySearch))
  assertBool "empty measured search returns empty end" (MeasuredRope.isAtEnd (MeasuredRope.searchCursor emptySearch))

  predicateFailure <- try
    (evaluate (MeasuredRope.searchFound (MeasuredRope.seekByMeasure (\_ -> error "measured predicate callback") receiver)))
  assertErrorCallPrefix "measured predicate failure propagates" "measured predicate callback" predicateFailure
  assertSameStableName "predicate failure retains measured source" source (MeasuredRope.snapshot receiver)
  assertEqual
    "predicate failure measured retry succeeds"
    1
    (MeasuredRope.cursorPosition (MeasuredRope.searchCursor (MeasuredRope.seekByMeasure (\(Sum value) -> value >= 3) receiver)))

runMeasuredCursorModel :: Int -> Word32 -> MeasuredRope.MeasuredRopeCursor (Sum Int) Int -> [Int] -> Int -> IO ()
runMeasuredCursorModel !step !randomState cursorValue model position
  | step == 750 = assertMeasuredCursorModel step cursorValue model position
  | otherwise = do
      let next = nextRrbRandom randomState
          operation = fromIntegral (next `mod` 7) :: Int
      (edited, expected, nextPosition) <-
        case operation of
          0 -> pure
            ( MeasuredRope.insert step cursorValue
            , take position model ++ [step] ++ drop position model
            , position + 1
            )
          1 ->
            let values = [step, negate step]
             in pure
                  ( MeasuredRope.insertRange values cursorValue
                  , take position model ++ values ++ drop position model
                  , position + length values
                  )
          2 ->
            case MeasuredRope.deletePrevious cursorValue of
              Nothing -> do
                assertEqual (measuredModelLabel "backspace boundary" step) 0 position
                pure (cursorValue, model, position)
              Just result ->
                let previous = position - 1
                 in pure (result, take previous model ++ drop position model, previous)
          3 ->
            case MeasuredRope.deleteNext cursorValue of
              Nothing -> do
                assertEqual (measuredModelLabel "delete boundary" step) (length model) position
                pure (cursorValue, model, position)
              Just result -> pure (result, take position model ++ drop (position + 1) model, position)
          4 ->
            case MeasuredRope.replaceNext step cursorValue of
              Nothing -> do
                assertEqual (measuredModelLabel "replace boundary" step) (length model) position
                pure (cursorValue, model, position)
              Just result -> pure (result, take position model ++ [step] ++ drop (position + 1) model, position)
          5
            | (next `div` 4096) `mod` 2 == 0 ->
                case MeasuredRope.movePrevious cursorValue of
                  Nothing -> do
                    assertEqual (measuredModelLabel "move previous boundary" step) 0 position
                    pure (cursorValue, model, position)
                  Just result -> pure (result, model, position - 1)
            | otherwise ->
                case MeasuredRope.moveNext cursorValue of
                  Nothing -> do
                    assertEqual (measuredModelLabel "move next boundary" step) (length model) position
                    pure (cursorValue, model, position)
                  Just result -> pure (result, model, position + 1)
          _ -> do
            let target = fromIntegral ((next `div` 256) `mod` fromIntegral (length model + 2))
            case MeasuredRope.seek target cursorValue of
              Nothing -> do
                assertBool (measuredModelLabel "invalid seek" step) (target > length model)
                pure (cursorValue, model, position)
              Just result -> pure (result, model, target)
      assertMeasuredCursorModel step edited expected nextPosition
      runMeasuredCursorModel (step + 1) next edited expected nextPosition

assertMeasuredCursorModel :: Int -> MeasuredRope.MeasuredRopeCursor (Sum Int) Int -> [Int] -> Int -> IO ()
assertMeasuredCursorModel step cursorValue model position = do
  assertEqual (measuredModelLabel "position" step) position (MeasuredRope.cursorPosition cursorValue)
  assertEqual (measuredModelLabel "count" step) (length model) (MeasuredRope.cursorCount cursorValue)
  assertEqual (measuredModelLabel "before" step) (Sum position) (MeasuredRope.measureBefore cursorValue)
  assertEqual (measuredModelLabel "after" step) (Sum (length model - position)) (MeasuredRope.measureAfter cursorValue)
  assertEqual
    (measuredModelLabel "previous" step)
    (if position == 0 then Nothing else Just (model !! (position - 1)))
    (MeasuredRope.peekPrevious cursorValue)
  assertEqual
    (measuredModelLabel "next" step)
    (if position == length model then Nothing else Just (model !! position))
    (MeasuredRope.peekNext cursorValue)
  assertEqual (measuredModelLabel "snapshot" step) model (MeasuredRope.toList (MeasuredRope.snapshot cursorValue))

measuredModelLabel :: String -> Int -> String
measuredModelLabel label step = "measured rope cursor model " ++ label ++ " at step " ++ show step

testMeasuredCursorCallbackFailures :: IO ()
testMeasuredCursorCallbackFailures = do
  state <- newMeasureProbeState
  let source = MeasuredRope.fromListWith (probeElementMeasure state) [0 :: Int .. 128]
  _ <- evaluate (probeMeasureTotal (MeasuredRope.measure source))
  cursorValue <- expectJust "probe measured cursor" (MeasuredRope.cursorAt 64 source)
  resetMeasureProbe state

  setMeasureProbeArming state True False
  elementFailure <- try
    (evaluate (probeMeasureTotal (MeasuredRope.measure (MeasuredRope.snapshot (MeasuredRope.insert (-1) cursorValue)))))
  setMeasureProbeArming state False False
  assertErrorCallPrefix "measured element callback failure" "measured element callback" elementFailure
  assertSameStableName "element failure retains measured source" source (MeasuredRope.snapshot cursorValue)
  resetMeasureProbe state
  assertEqual
    "element callback failure retry succeeds"
    130
    (MeasuredRope.cursorCount (MeasuredRope.insert (-1) cursorValue))

  resetMeasureProbe state
  setMeasureProbeArming state False True
  combineFailure <- try
    (evaluate (case MeasuredRope.replaceNext (-1) cursorValue of
      Just edited -> probeMeasureTotal (MeasuredRope.measure (MeasuredRope.snapshot edited))
      Nothing -> error "probe replacement unexpectedly missed"))
  setMeasureProbeArming state False False
  assertErrorCallPrefix "measured combine callback failure" "measured combine callback" combineFailure
  assertSameStableName "combine failure retains measured source" source (MeasuredRope.snapshot cursorValue)
  resetMeasureProbe state
  retried <- expectJust "combine callback retry" (MeasuredRope.replaceNext (-1) cursorValue)
  assertEqual "combine callback retry stores value" (Just (-1)) (MeasuredRope.peekNext retried)

  resetMeasureProbe state
  setMeasureProbeArming state True True
  assertEqual "measured snapshot count is callback-free" 129 (MeasuredRope.count (MeasuredRope.snapshot cursorValue))
  assertSameStableName "measured snapshot itself is callback-free" source (MeasuredRope.snapshot cursorValue)
  elementCalls <- readIORef (probeElementCalls state)
  combineCalls <- readIORef (probeCombineCalls state)
  setMeasureProbeArming state False False
  assertEqual "snapshot element callback count" 0 elementCalls
  assertEqual "snapshot combine callback count" 0 combineCalls

testMeasuredRopeGrowthOverflow :: IO ()
testMeasuredRopeGrowthOverflow = do
  state <- newMeasureProbeState
  let seed = MeasuredRope.singletonWith (probeElementMeasure state) (7 :: Int)
      grow current
        | MeasuredRope.count current > maxBound `div` 2 = current
        | otherwise = grow (MeasuredRope.append current current)
      power = grow seed
      powerCount = MeasuredRope.count power
  _ <- evaluate (probeMeasureTotal (MeasuredRope.measure power))
  assertEqual "measured shared power count" (maxBound `div` 2 + 1) powerCount
  almostPower <- expectJust "remove from measured shared power" (MeasuredRope.deleteAt 0 power)
  let maximumRope = MeasuredRope.append power almostPower
      maximumCursor = MeasuredRope.cursor maximumRope
  maximumEnd <- expectJust "maximum measured end cursor" (MeasuredRope.cursorAt maxBound maximumRope)
  _ <- evaluate (probeMeasureTotal (MeasuredRope.measure maximumRope))
  assertEqual "maximum measured rope count" maxBound (MeasuredRope.count maximumRope)

  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured self-append"
    (evaluate (MeasuredRope.count (MeasuredRope.append power power)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured point insertion"
    (evaluate (maybe (-1) MeasuredRope.count (MeasuredRope.insertAt 0 8 maximumRope)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured end insertion"
    (evaluate (maybe (-1) MeasuredRope.count (MeasuredRope.insertAt maxBound 8 maximumRope)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured range insertion"
    (evaluate (maybe (-1) MeasuredRope.count (MeasuredRope.insertRangeAt 0 [8] maximumRope)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured range preflight does not force values or tail"
    (evaluate (maybe (-1) MeasuredRope.count
      (MeasuredRope.insertRangeAt 0 (error "measured range head forced" : error "measured range tail forced") maximumRope)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured maximum append"
    (evaluate (MeasuredRope.count (MeasuredRope.append maximumRope seed)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured maximum prefix append"
    (evaluate (MeasuredRope.count (MeasuredRope.append seed maximumRope)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured cursor insertion"
    (evaluate (MeasuredRope.cursorCount (MeasuredRope.insert 8 maximumCursor)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured cursor range"
    (evaluate (MeasuredRope.cursorCount (MeasuredRope.insertRange [8] maximumCursor)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured cursor range does not force values or tail"
    (evaluate (MeasuredRope.cursorCount
      (MeasuredRope.insertRange (error "measured cursor range head forced" : error "measured cursor range tail forced") maximumCursor)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured end cursor insertion"
    (evaluate (MeasuredRope.cursorCount (MeasuredRope.insert 8 maximumEnd)))
  assertMeasuredLengthOverflowNoCallbacks state
    "overflowing measured end cursor range"
    (evaluate (MeasuredRope.cursorCount (MeasuredRope.insertRange [8] maximumEnd)))

  assertEqual "measured overflow preserves maximum" maxBound (MeasuredRope.count maximumRope)
  assertEqual "measured overflow preserves front" (Just 7) (MeasuredRope.index 0 maximumRope)
  assertEqual "measured overflow preserves back" (Just 7) (MeasuredRope.index (maxBound - 1) maximumRope)
  assertEqual "measured overflow preserves power" powerCount (MeasuredRope.count power)
  assertEqual "measured overflow preserves seed" [7] (MeasuredRope.toList seed)

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

  let cursorContent = '\x1F603' : 'e' : '\x0301' : '\r' : '\n' : "alpha\n\x03B2"
      cursorText = RopeText.fromString cursorContent
      firstNewlineOffset =
        case List.elemIndex '\n' cursorContent of
          Just value -> value
          Nothing -> error "text cursor fixture has no newline"
  forM_ [0 .. length cursorContent] $ \position -> do
    cursorValue <- expectJust "text cursor at valid gap" (RopeText.cursorAt position cursorText)
    assertEqual
      ("text cursor line-column at " ++ show position)
      (textLineColumnModel position cursorContent)
      (RopeText.lineColumnOfCursor cursorValue)
    assertEqual
      ("text cursor helper parity at " ++ show position)
      (Just (RopeText.lineColumnOfCursor cursorValue))
      (RopeText.lineColumnOf position cursorText)
    assertSameStableName
      ("text cursor retains exact source at " ++ show position)
      cursorText
      (RopeText.snapshot cursorValue)

  let textStart = RopeText.cursor cursorText
      unchangedTextCursor = RopeText.insertRange [] textStart
  assertSameStableName "empty text cursor insertion retains facade" cursorText (RopeText.snapshot unchangedTextCursor)
  assertEqual "Haskell text cursor counts Char elements" (0, 1) (RopeText.lineColumnOfCursor (expectTextCursor 1 cursorText))
  assertEqual
    "text cursor CR remains in the preceding column"
    (0, firstNewlineOffset)
    (RopeText.lineColumnOfCursor (expectTextCursor firstNewlineOffset cursorText))
  assertEqual
    "text cursor moves to column zero after LF"
    (1, 0)
    (RopeText.lineColumnOfCursor (expectTextCursor (firstNewlineOffset + 1) cursorText))

  let firstNewline = RopeText.cursorByMeasure (\(RopeText.NewlineMeasure seen) -> seen >= 1) cursorText
  assertBool "text cursor first newline search found" (RopeText.searchFound firstNewline)
  assertEqual
    "text cursor first newline search position"
    firstNewlineOffset
    (RopeText.cursorPosition (RopeText.searchCursor firstNewline))
  assertSameStableName
    "text cursor search retains exact facade"
    cursorText
    (RopeText.snapshot (RopeText.searchCursor firstNewline))
  let secondNewline = RopeText.seekByMeasure
        (\(RopeText.NewlineMeasure seen) -> seen >= 2)
        (RopeText.searchCursor firstNewline)
  assertBool "text cursor second newline search found" (RopeText.searchFound secondNewline)
  assertEqual
    "text cursor second newline search position"
    (maybe (-1) id (List.elemIndex '\n' (drop (firstNewlineOffset + 1) cursorContent)) + firstNewlineOffset + 1)
    (RopeText.cursorPosition (RopeText.searchCursor secondNewline))

  let insertionPoint = expectTextCursor firstNewlineOffset cursorText
      inserted = RopeText.insert '\n' insertionPoint
      editedText = RopeText.snapshot inserted
      expectedText = take firstNewlineOffset cursorContent ++ "\n" ++ drop firstNewlineOffset cursorContent
  assertEqual "text cursor edited snapshot" expectedText (RopeText.toString editedText)
  assertEqual "text cursor edited lines" (splitTextLines expectedText) (RopeText.lines editedText)
  assertEqual "text cursor edited line count" (length (splitTextLines expectedText)) (RopeText.lineCount editedText)
  assertEqual
    "text cursor edited coordinates"
    (maybe (error "edited text coordinate") id (RopeText.lineColumnOf (RopeText.cursorPosition inserted) editedText))
    (RopeText.lineColumnOfCursor inserted)
  restored <- expectJust "text cursor delete previous" (RopeText.deletePrevious inserted)
  assertEqual "text cursor delete restores source content" cursorContent (RopeText.toString (RopeText.snapshot restored))
  replaced <- expectJust "text cursor replacement" (RopeText.replaceNext 'x' insertionPoint)
  assertEqual
    "text cursor replacement snapshot"
    (take firstNewlineOffset cursorContent ++ "x" ++ drop (firstNewlineOffset + 1) cursorContent)
    (RopeText.toString (RopeText.snapshot replaced))
  assertEqual
    "text cursor replacement updates newline measure"
    (RopeText.lineCount cursorText - 1)
    (RopeText.lineCount (RopeText.snapshot replaced))
  testTextLineCountOverflow

expectTextCursor :: Int -> RopeText.TextRope -> RopeText.TextRopeCursor
expectTextCursor position text =
  case RopeText.cursorAt position text of
    Just value -> value
    Nothing -> error "expected a valid text cursor gap"

textLineColumnModel :: Int -> String -> (Int, Int)
textLineColumnModel position content =
  let before = take position content
      line = length (filter (== '\n') before)
      lineStart =
        case List.elemIndices '\n' before of
          [] -> 0
          offsets -> last offsets + 1
   in (line, position - lineStart)

splitTextLines :: String -> [String]
splitTextLines values =
  case break (== '\n') values of
    (line, []) -> [line]
    (line, _ : rest) -> line : splitTextLines rest

testTextLineCountOverflow :: IO ()
testTextLineCountOverflow = do
  let seed = RopeText.fromString "\n"
      grow current
        | MeasuredRope.count current > maxBound `div` 2 = current
        | otherwise = grow (MeasuredRope.append current current)
      power = grow seed
  almostPower <- expectJust "remove from newline shared power" (MeasuredRope.deleteAt 0 power)
  let maximumText = MeasuredRope.append power almostPower
  assertEqual "maximum newline text count" maxBound (MeasuredRope.count maximumText)
  let startCursor = RopeText.cursor maximumText
      endCursor = expectTextCursor maxBound maximumText
  assertEqual "maximum newline text start coordinate" (0, 0) (RopeText.lineColumnOfCursor startCursor)
  assertEqual "maximum newline text end coordinate" (maxBound, 0) (RopeText.lineColumnOfCursor endCursor)
  assertEqual "maximum newline text first line start" (Just 0) (RopeText.lineStartOffset 0 maximumText)
  assertEqual "maximum newline text final line start" (Just maxBound) (RopeText.lineStartOffset maxBound maximumText)
  assertEqual "maximum newline text first line" (Just "") (RopeText.getLine 0 maximumText)
  assertEqual "maximum newline text final line" (Just "") (RopeText.getLine maxBound maximumText)
  assertEqual "maximum newline text end offset" (Just maxBound) (RopeText.offsetOf maxBound 0 maximumText)
  result <- try (evaluate (RopeText.lineCount maximumText))
  assertErrorCallPrefix
    "maximum newline text line count is checked"
    "Durable7.FingerTree.Rope.Text: line count overflow"
    result

testConcurrentReads :: IO ()
testConcurrentReads = do
  let expectedDeque = [0 :: Int .. 511]
      deque = Deque.fromList expectedDeque
      reversible = ReversibleDeque.reverse (ReversibleDeque.fromList expectedDeque)
      expectedReverse = List.reverse expectedDeque
      rope = Rope.fromList expectedDeque
      ropeCursor = maybe (error "concurrent rope cursor") id (Rope.cursorAt 255 rope)
      rrb = RrbVector.fromList expectedDeque
      measuredValues = [1 :: Int .. 128]
      measured = MeasuredRope.fromListWith Sum measuredValues
      measuredCursor = maybe (error "concurrent measured cursor") id (MeasuredRope.cursorAt 64 measured)
      text = RopeText.fromString "a\nbc"
      textCursor = expectTextCursor 3 text
  runConcurrent "fingertree concurrent reads" 8 $ do
    forM_ [1 :: Int .. 128] $ \_ -> do
      assertEqual "concurrent deque count" 512 (Deque.count deque)
      assertEqual "concurrent deque index" (Just 255) (Deque.index 255 deque)
      assertEqual "concurrent deque contents" expectedDeque (Deque.toList deque)
      assertEqual "concurrent reversible contents" expectedReverse (ReversibleDeque.toList reversible)
      assertEqual "concurrent rope count" 512 (Rope.count rope)
      assertEqual "concurrent rope index" (Just 255) (Rope.index 255 rope)
      assertEqual "concurrent rope contents" expectedDeque (Rope.toList rope)
      assertEqual "concurrent rope cursor position" 255 (Rope.cursorPosition ropeCursor)
      assertEqual "concurrent rope cursor previous" (Just 254) (Rope.peekPrevious ropeCursor)
      assertEqual "concurrent rope cursor next" (Just 255) (Rope.peekNext ropeCursor)
      assertEqual "concurrent rope cursor snapshot" expectedDeque (Rope.toList (Rope.snapshot ropeCursor))
      assertEqual "concurrent rrb count" 512 (RrbVector.count rrb)
      assertEqual "concurrent rrb index" (Just 255) (RrbVector.index 255 rrb)
      assertEqual "concurrent rrb contents" expectedDeque (RrbVector.toList rrb)
      assertEqual "concurrent measured count" 128 (MeasuredRope.count measured)
      assertEqual "concurrent measured total" (Sum (sum measuredValues)) (MeasuredRope.measure measured)
      assertEqual "concurrent measured index" (Just 64) (MeasuredRope.index 63 measured)
      assertEqual "concurrent measured cursor position" 64 (MeasuredRope.cursorPosition measuredCursor)
      assertEqual "concurrent measured cursor previous" (Just 64) (MeasuredRope.peekPrevious measuredCursor)
      assertEqual "concurrent measured cursor next" (Just 65) (MeasuredRope.peekNext measuredCursor)
      assertEqual "concurrent measured cursor before" (Sum (sum [1 :: Int .. 64])) (MeasuredRope.measureBefore measuredCursor)
      assertEqual "concurrent measured cursor after" (Sum (sum [65 :: Int .. 128])) (MeasuredRope.measureAfter measuredCursor)
      assertEqual "concurrent measured cursor snapshot" measuredValues (MeasuredRope.toList (MeasuredRope.snapshot measuredCursor))
      assertEqual "concurrent text cursor position" 3 (RopeText.cursorPosition textCursor)
      assertEqual "concurrent text cursor coordinate" (1, 1) (RopeText.lineColumnOfCursor textCursor)
      assertEqual "concurrent text cursor snapshot" "a\nbc" (RopeText.toString (RopeText.snapshot textCursor))

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

-- | A non-commutative measure (list concatenation) that records the order in
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

-- | An element whose ordering ignores the label, standing in for
-- comparer-equal-but-distinct instances.
data Keyed = Keyed Int String
  deriving (Show)

instance Eq Keyed where
  Keyed left _ == Keyed right _ = left == right

instance Ord Keyed where
  compare (Keyed left _) (Keyed right _) = compare left right

keyedLabel :: Keyed -> String
keyedLabel (Keyed _ label) = label

-- | Deliberately has no Eq instance: cursor replacement must neither require
-- nor consult element equality, and must retain the supplied representative.
data CursorRepresentative = CursorRepresentative Int String
  deriving (Show)

cursorRepresentativeLabel :: CursorRepresentative -> String
cursorRepresentativeLabel (CursorRepresentative _ label) = label

data MeasureProbeState = MeasureProbeState
  { probeElementArmed :: !(IORef Bool)
  , probeCombineArmed :: !(IORef Bool)
  , probeElementCalls :: !(IORef Int)
  , probeCombineCalls :: !(IORef Int)
  }

data ProbeMeasure
  = ProbeIdentity
  | ProbeValue !MeasureProbeState !Int !Int

instance Semigroup ProbeMeasure where
  (<>) = combineProbeMeasure

instance Monoid ProbeMeasure where
  mempty = ProbeIdentity

{-# NOINLINE combineProbeMeasure #-}
combineProbeMeasure :: ProbeMeasure -> ProbeMeasure -> ProbeMeasure
combineProbeMeasure ProbeIdentity right = right
combineProbeMeasure left ProbeIdentity = left
combineProbeMeasure (ProbeValue state left leftTag) (ProbeValue _ right rightTag) = unsafePerformIO $ do
  modifyIORef' (probeCombineCalls state) (+ 1)
  armed <- readIORef (probeCombineArmed state)
  if armed
    then error "measured combine callback"
    else pure (ProbeValue state (left + right) (leftTag + rightTag))

{-# NOINLINE probeElementMeasure #-}
probeElementMeasure :: MeasureProbeState -> Int -> ProbeMeasure
probeElementMeasure state value = unsafePerformIO $ do
  modifyIORef' (probeElementCalls state) (+ 1)
  armed <- readIORef (probeElementArmed state)
  if armed
    then error "measured element callback"
    else pure (ProbeValue state 1 value)

probeMeasureTotal :: ProbeMeasure -> Int
probeMeasureTotal ProbeIdentity = 0
probeMeasureTotal (ProbeValue _ total _) = total

newMeasureProbeState :: IO MeasureProbeState
newMeasureProbeState =
  MeasureProbeState <$> newIORef False <*> newIORef False <*> newIORef 0 <*> newIORef 0

setMeasureProbeArming :: MeasureProbeState -> Bool -> Bool -> IO ()
setMeasureProbeArming state elementArmed combineArmed = do
  writeIORef (probeElementArmed state) elementArmed
  writeIORef (probeCombineArmed state) combineArmed

resetMeasureProbe :: MeasureProbeState -> IO ()
resetMeasureProbe state = do
  setMeasureProbeArming state False False
  writeIORef (probeElementCalls state) 0
  writeIORef (probeCombineCalls state) 0

assertMeasuredLengthOverflowNoCallbacks :: MeasureProbeState -> String -> IO a -> IO ()
assertMeasuredLengthOverflowNoCallbacks state label action = do
  resetMeasureProbe state
  setMeasureProbeArming state True True
  result <- try (action >> pure ())
  setMeasureProbeArming state False False
  elementCalls <- readIORef (probeElementCalls state)
  combineCalls <- readIORef (probeCombineCalls state)
  assertErrorCallPrefix label "Durable7.FingerTree.MeasuredRope: length overflow" result
  assertEqual (label ++ " element callback count") 0 elementCalls
  assertEqual (label ++ " combine callback count") 0 combineCalls

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

assertNothing :: String -> Maybe a -> IO ()
assertNothing _ Nothing = pure ()
assertNothing label (Just _) = fail (label ++ ": expected Nothing")

expectJust :: String -> Maybe a -> IO a
expectJust _ (Just value) = pure value
expectJust label Nothing = fail (label ++ ": expected Just")

assertSameStableName :: String -> a -> a -> IO ()
assertSameStableName label left right = do
  leftValue <- evaluate left
  rightValue <- evaluate right
  leftName <- makeStableName leftValue
  rightName <- makeStableName rightValue
  assertBool label (leftName `eqStableName` rightName)

assertErrorCallPrefix :: String -> String -> Either ErrorCall a -> IO ()
assertErrorCallPrefix label expected result =
  case result of
    Left exception ->
      assertBool
        (label ++ ": expected error prefix " ++ show expected)
        (expected `List.isPrefixOf` displayException exception)
    Right _ -> fail (label ++ ": expected an exception")

assertLengthOverflow :: String -> IO a -> IO ()
assertLengthOverflow label action = do
  result <- try (action >> pure ())
  case (result :: Either ErrorCall ()) of
    Left exception ->
      assertBool
        (label ++ ": expected the rope length-overflow exception")
        ("Durable7.FingerTree.Rope: length overflow" `List.isPrefixOf` displayException exception)
    Right () -> fail (label ++ ": expected an exception")
