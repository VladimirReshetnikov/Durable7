module Main (main) where

import Prelude hiding (lines, null, reverse, splitAt)

import Control.Concurrent (forkIO, newEmptyMVar, putMVar, takeMVar)
import Control.Exception (SomeException, try)
import Control.Monad (forM_, replicateM)
import qualified Data.List as List
import Data.Monoid (Sum(..))

import qualified Data.Structures.FingerTree.Deque as Deque
import qualified Data.Structures.FingerTree.IntervalTree as IntervalTree
import qualified Data.Structures.FingerTree.Measured as FT
import Data.Structures.FingerTree.Measured (ViewL(..), ViewR(..))
import Data.Structures.FingerTree.Measures (Elem(..), Size(..))
import qualified Data.Structures.FingerTree.MeasuredRope as MeasuredRope
import qualified Data.Structures.FingerTree.PriorityQueue as PriorityQueue
import qualified Data.Structures.FingerTree.ReversibleDeque as ReversibleDeque
import qualified Data.Structures.FingerTree.Rope as Rope
import qualified Data.Structures.FingerTree.Rope.Text as RopeText
import qualified Data.Structures.FingerTree.SortedBag as SortedBag
import qualified Data.Structures.FingerTree.SortedMap as SortedMap
import qualified Data.Structures.FingerTree.SortedSet as SortedSet

main :: IO ()
main = do
  testMeasuredTree
  testDeque
  testReversibleDeque
  testSortedCollections
  testPriorityQueue
  testIntervalTree
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
  where
    mustInterval low high =
      case IntervalTree.mkInterval low high of
        Just interval -> interval
        Nothing -> error "testIntervalTree: invalid interval literal"

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
  let measured = MeasuredRope.fromListWith Sum [1 :: Int, 2, 3]
  assertEqual "measured rope total" (Sum 6) (MeasuredRope.measure measured)
  assertEqual "measured rope prefix" (Just (Sum 3)) (MeasuredRope.prefixMeasure 2 measured)
  assertEqual "measured rope locate" (Just (1, Sum 1, 2)) (MeasuredRope.locateByMeasure (\(Sum value) -> value >= 3) measured)
  assertEqual "measured rope overflowing slice rejected" Nothing (MeasuredRope.toList <$> MeasuredRope.slice 1 maxBound measured)
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

testConcurrentReads :: IO ()
testConcurrentReads = do
  let expectedDeque = [0 :: Int .. 511]
      deque = Deque.fromList expectedDeque
      reversible = ReversibleDeque.reverse (ReversibleDeque.fromList expectedDeque)
      expectedReverse = List.reverse expectedDeque
      rope = Rope.fromList expectedDeque
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

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")
