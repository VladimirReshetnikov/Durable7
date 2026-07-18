module OrderedSearchCursorTests (run) where

import Data.Word (Word64)

import qualified Data.Structures.FingerTree.CanonicalSortedSet as Canonical
import qualified Data.Structures.FingerTree.IntervalMap as IntervalMap
import qualified Data.Structures.FingerTree.IntervalTree as IntervalTree
import qualified Data.Structures.FingerTree.OrderedSearchCursor as Cursor
import qualified Data.Structures.FingerTree.PersistentChunkedBitSet as BitSet
import qualified Data.Structures.FingerTree.PrioritySearchQueue as PrioritySearch
import qualified Data.Structures.FingerTree.SortedBag as SortedBag
import qualified Data.Structures.FingerTree.SortedMap as SortedMap
import qualified Data.Structures.FingerTree.SortedSet as SortedSet

data Ranked = Ranked !Int !String
  deriving (Show)

instance Eq Ranked where
  Ranked left _ == Ranked right _ = left == right

instance Ord Ranked where
  compare (Ranked left _) (Ranked right _) = compare left right

rankedLabel :: Ranked -> String
rankedLabel (Ranked _ label) = label

run :: IO ()
run = do
  testSortedCursors
  testCanonicalAndPrioritySearchCursors
  testIntervalCursors
  testIntervalMapAndBitSetCursors

testSortedCursors :: IO ()
testSortedCursors = do
  let bag = SortedBag.fromList
        [ Ranked 1 "low"
        , Ranked 2 "first"
        , Ranked 2 "second"
        , Ranked 2 "third"
        ]
  selected <- expectJust "bag selected cursor" (Cursor.sortedBagCursorAt 2 bag)
  assertEqual "bag selected duplicate" (Just "second") (rankedLabel <$> Cursor.sortedBagPeekNext selected)
  edited <- expectJust "bag exact deletion" (Cursor.sortedBagDeleteNext selected)
  assertEqual "bag exact duplicate occurrence"
    ["low", "first", "third"]
    (map rankedLabel (SortedBag.toList (Cursor.sortedBagSnapshot edited)))
  assertEqual "bag source retained" 4 (SortedBag.count bag)

  let nullable = SortedBag.fromList [Nothing, Just (2 :: Int)]
  nullableCursor <- expectJust "bag Maybe cursor" (Cursor.sortedBagCursorAt 1 nullable)
  assertEqual "bag stored Nothing remains present" (Just Nothing) (Cursor.sortedBagPeekPrevious nullableCursor)

  let set = SortedSet.fromList [1 :: Int, 3, 5]
      gap = Cursor.sortedSetLowerBoundCursor 4 set
      insertedSet = Cursor.sortedSetAdd 4 gap
  assertEqual "set lower-bound position" 2 (Cursor.sortedSetCursorPosition gap)
  assertEqual "set lower-bound neighbors" (Just 3, Just 5)
    (Cursor.sortedSetPeekPrevious gap, Cursor.sortedSetPeekNext gap)
  assertEqual "set cursor insertion" [1, 3, 4, 5]
    (SortedSet.toList (Cursor.sortedSetSnapshot insertedSet))

  let mapValue = SortedMap.fromList [(1 :: Int, "one"), (3, "three"), (5, "five")]
      found = Cursor.findSortedMapCursor 3 mapValue
  assertBool "map exact cursor" (Cursor.cursorFound found)
  changed <- expectJust "map set next" (Cursor.sortedMapSetNextValue "THREE" (Cursor.searchCursor found))
  insertedMap <- expectJust "map strict insert" (Cursor.sortedMapInsert 4 "four" changed)
  assertEqual "map cursor edits"
    [(1, "one"), (3, "THREE"), (4, "four"), (5, "five")]
    (SortedMap.toList (Cursor.sortedMapSnapshot insertedMap))

testCanonicalAndPrioritySearchCursors :: IO ()
testCanonicalAndPrioritySearchCursors = do
  policy <- Canonical.newSeededPolicy fromIntegral 0x5a17
  set <- expectRight "canonical construction" (Canonical.fromList policy [1 :: Int, 3, 5])
  inserted <- expectRight "canonical cursor insertion" (Cursor.canonicalAdd 4 (Cursor.canonicalLowerBoundCursor 4 set))
  assertEqual "canonical insertion position" 3 (Cursor.canonicalCursorPosition inserted)
  assertEqual "canonical cursor edit" [1, 3, 4, 5]
    (Canonical.toAscList (Cursor.canonicalSnapshot inserted))
  assertEqual "canonical retained policy seed" (Just (0x5a17 :: Word64))
    (Canonical.policySeed (Canonical.rankPolicy (Cursor.canonicalSnapshot inserted)))

  maybePolicy <- Canonical.newSeededPolicy maybeHash 71
  maybeSet <- expectRight "canonical Maybe construction" (Canonical.fromList maybePolicy [Nothing, Just (2 :: Int)])
  let maybeFound = Cursor.findCanonicalCursor Nothing maybeSet
  assertBool "canonical Nothing search" (Cursor.cursorFound maybeFound)
  assertEqual "canonical stored Nothing remains present" (Just Nothing)
    (Cursor.canonicalPeekNext (Cursor.searchCursor maybeFound))

  let queue = PrioritySearch.setItem 5 20 "five"
        (PrioritySearch.setItem 3 10 "three"
          (PrioritySearch.setItem (1 :: Int) (30 :: Int) "one" PrioritySearch.empty))
      minimumCursor = Cursor.prioritySearchMinimumCursor queue
  assertEqual "priority minimum cursor" (Just 3)
    (PrioritySearch.entryKey <$> Cursor.prioritySearchPeekNext minimumCursor)
  changed <- expectJust "priority set next" (Cursor.prioritySearchSetNext 40 "THREE" minimumCursor)
  assertEqual "priority winner moved" (Just 5)
    (PrioritySearch.entryKey <$> PrioritySearch.minimumEntry (Cursor.prioritySearchSnapshot changed))
  let insertedResult = Cursor.prioritySearchTryInsert 4 5 "four" changed
  assertBool "priority cursor insertion" (Cursor.cursorAdded insertedResult)
  assertEqual "priority winner after insert" (Just 4)
    (PrioritySearch.entryKey <$> PrioritySearch.minimumEntry
      (Cursor.prioritySearchSnapshot (Cursor.insertionCursor insertedResult)))
  where
    maybeHash Nothing = 0
    maybeHash (Just value) = fromIntegral value

testIntervalCursors :: IO ()
testIntervalCursors = do
  let first = IntervalTree.Interval (Ranked 1 "first-low") (Ranked 5 "first-high")
      second = IntervalTree.Interval (Ranked 1 "second-low") (Ranked 5 "second-high")
      late = IntervalTree.Interval (Ranked 7 "late-low") (Ranked 9 "late-high")
      tree = IntervalTree.fromList [first, second, late]
  firstCursor <- expectJust "first interval cursor" (Cursor.intervalTreeCursorAt 0 tree)
  assertEqual "equal-low insertion order" (Just "second-low")
    (rankedLabel . IntervalTree.low <$> Cursor.intervalTreePeekNext firstCursor)
  selected <- expectJust "selected interval cursor" (Cursor.intervalTreeCursorAt 1 tree)
  edited <- expectJust "selected interval deletion" (Cursor.intervalTreeDeleteNext selected)
  assertEqual "remaining equal interval" (Just "second-low")
    (rankedLabel . IntervalTree.low <$> Cursor.intervalTreePeekPrevious edited)
  assertEqual "interval successor" (Just "late-low")
    (rankedLabel . IntervalTree.low <$> Cursor.intervalTreePeekNext edited)

  let probe = IntervalTree.Interval (Ranked 4 "probe-low") (Ranked 8 "probe-high")
      firstHit = Cursor.findOverlapCursor probe tree
      secondHit = Cursor.intervalTreeSeekNextOverlap probe (Cursor.searchCursor firstHit)
  assertBool "first interval overlap" (Cursor.cursorFound firstHit)
  assertEqual "first interval overlap rank" 0
    (Cursor.intervalTreeCursorPosition (Cursor.searchCursor firstHit))
  assertBool "continued interval overlap" (Cursor.cursorFound secondHit)
  assertEqual "continued interval overlap rank" 1
    (Cursor.intervalTreeCursorPosition (Cursor.searchCursor secondHit))

testIntervalMapAndBitSetCursors :: IO ()
testIntervalMapAndBitSetCursors = do
  let one = IntervalTree.Interval (1 :: Int) 3
      five = IntervalTree.Interval 5 8
      ten = IntervalTree.Interval 10 12
      mapValue = IntervalMap.fromList [(one, "one"), (five, "five"), (ten, "ten")]
      probe = IntervalTree.Interval 2 11
      firstHit = Cursor.findIntervalMapOverlapCursor probe mapValue
      secondHit = Cursor.intervalMapSeekNextOverlap probe (Cursor.searchCursor firstHit)
  assertEqual "first interval-map overlap" (Just "one")
    (snd <$> Cursor.intervalMapPeekNext (Cursor.searchCursor firstHit))
  assertEqual "continued interval-map overlap" (Just "five")
    (snd <$> Cursor.intervalMapPeekNext (Cursor.searchCursor secondHit))
  changed <- expectJust "interval-map set next" (Cursor.intervalMapSetNextValue "FIVE" (Cursor.searchCursor secondHit))
  assertEqual "interval-map cursor payload update" (Just "FIVE") (snd <$> Cursor.intervalMapPeekNext changed)
  assertEqual "interval-map source retained" (Just "five") (IntervalMap.lookup five mapValue)

  let bits = BitSet.fromList [1, 64, 130]
      gap = Cursor.chunkedBitSetCursorAtOrAfter 65 bits
      inserted = Cursor.chunkedBitSetInsert 100 gap
  assertEqual "bit-set cursor rank" 2 (Cursor.chunkedBitSetCursorPosition gap)
  assertEqual "bit-set cursor neighbors" (Just 64, Just 130)
    (Cursor.chunkedBitSetPeekPrevious gap, Cursor.chunkedBitSetPeekNext gap)
  assertEqual "bit-set cursor insertion" [1, 64, 100, 130]
    (BitSet.toList (Cursor.chunkedBitSetSnapshot inserted))
  deleted <- expectJust "bit-set cursor deletion" (Cursor.chunkedBitSetDeletePrevious inserted)
  assertEqual "bit-set cursor deletion result" [1, 64, 130]
    (BitSet.toList (Cursor.chunkedBitSetSnapshot deleted))
  assertEqual "bit-set source retained" [1, 64, 130] (BitSet.toList bits)

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool _ True = pure ()
assertBool label False = fail (label ++ ": expected True")

expectJust :: String -> Maybe a -> IO a
expectJust _ (Just value) = pure value
expectJust label Nothing = fail (label ++ ": expected Just")

expectRight :: Show e => String -> Either e a -> IO a
expectRight _ (Right value) = pure value
expectRight label (Left errorValue) = fail (label ++ ": expected Right, got " ++ show errorValue)
