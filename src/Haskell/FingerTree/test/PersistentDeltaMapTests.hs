{-# LANGUAGE NumericUnderscores #-}

-- | Tests for the checkpoint-differential persistent sorted map.
module PersistentDeltaMapTests (run) where

import Control.Exception (ErrorCall, displayException, evaluate, try)
import Control.Monad (forM_)
import Data.Bits (shiftR)
import Data.Char (toLower)
import Data.IORef (IORef, modifyIORef', newIORef, readIORef, writeIORef)
import qualified Data.List as List
import qualified Data.Map.Strict as Map
import Data.Word (Word64)
import System.IO.Unsafe (unsafePerformIO)

import qualified Durable7.FingerTree.PersistentDeltaMap as DeltaMap

type IntMap = DeltaMap.PersistentDeltaMap Int String

type ModelMap = Map.Map Int Int

type IntChange = DeltaMap.PersistentMapChange Int Int

-- | Runs this module's test cases, reporting each result.
run :: IO ()
run = do
  testPointEdits
  testEndpointPresence
  testCoalescingAndCancellation
  testRetainedKeyOrder
  testCheckpointAndRollback
  testRetainedBranches
  testKeyRepresentatives
  testLastEquivalentEntryWins
  testValueEquivalencePolicy
  testChangeRangeRestriction
  testCallbackBudgets
  testSetItems
  testStructuralValidation
  testPolicyFailures
  testRandomizedModel

-- Point edits classify additions, updates, and removals with exact endpoints.
testPointEdits :: IO ()
testPointEdits = do
  let source = DeltaMap.fromListWith textPolicy [(1, "one"), (2, "two")]
      changed = DeltaMap.remove 2 (DeltaMap.setItem 1 "ONE" (DeltaMap.setItem 0 "zero" source))
      observed = DeltaMap.changes changed
  assertEqual "change count" 3 (DeltaMap.changeCount changed)
  assertBool "has changes" (DeltaMap.hasChanges changed)
  assertEqual "current contents" [(0, "zero"), (1, "ONE")] (DeltaMap.toList changed)
  assertEqual "change keys" [0, 1, 2] (map DeltaMap.changeKey observed)
  assertEqual "change kinds"
    [DeltaMap.Added, DeltaMap.Updated, DeltaMap.Removed]
    (map DeltaMap.changeKind observed)
  assertEqual "before endpoints"
    [Nothing, Just "one", Just "two"]
    (map DeltaMap.changeBefore observed)
  assertEqual "after endpoints"
    [Just "zero", Just "ONE", Nothing]
    (map DeltaMap.changeAfter observed)
  forM_ observed $ \change ->
    assertEqual "change lookup" (Just change) (DeltaMap.lookupChange (DeltaMap.changeKey change) changed)
  assertEqual "absent change lookup" Nothing (DeltaMap.lookupChange 99 changed)

  assertBool "source stays clean" (not (DeltaMap.hasChanges source))
  assertEqual "source has no changes" [] (DeltaMap.changes source)
  assertEqual "source contents" [(1, "one"), (2, "two")] (DeltaMap.toList source)
  assertEqual "source count" 2 (DeltaMap.count source)
  assertBool "source is nonempty" (not (DeltaMap.null source))
  assertEqual "least entry" (Just (0, "zero")) (DeltaMap.minEntry changed)
  assertEqual "greatest entry" (Just (1, "ONE")) (DeltaMap.maxEntry changed)
  assertEqual "rank select" (Just (1, "ONE")) (DeltaMap.entryAt 1 changed)
  assertEqual "key rank" (Just 0) (DeltaMap.indexOfKey 0 changed)
  assertEqual "absent key rank" Nothing (DeltaMap.indexOfKey 2 changed)
  assertEqual "floor entry" (Just (1, "ONE")) (DeltaMap.floorEntry 5 changed)
  assertEqual "ceiling entry" (Just (0, "zero")) (DeltaMap.ceilingEntry 0 changed)
  assertEqual "lower entry" (Just (0, "zero")) (DeltaMap.lowerEntry 1 changed)
  assertEqual "higher entry" (Just (1, "ONE")) (DeltaMap.higherEntry 0 changed)
  assertEqual "checkpoint contents" [(1, "one"), (2, "two")] (DeltaMap.checkpointToList changed)
  assertEqual "checkpoint count" 2 (DeltaMap.checkpointCount changed)
  assertEqual "checkpoint lookup" (Just "two") (DeltaMap.checkpointLookup 2 changed)
  assertValid "point edits" changed

-- An absent endpoint stays distinct from a present value that is itself Nothing.
testEndpointPresence :: IO ()
testEndpointPresence = do
  let source = DeltaMap.fromListWith optionalPolicy [("present", Nothing)]
      changed = DeltaMap.setItem "added" Nothing (DeltaMap.remove "present" source)
      observed = DeltaMap.changes changed
  assertEqual "presence change keys" ["added", "present"] (map DeltaMap.changeKey observed)
  assertEqual "presence change kinds"
    [DeltaMap.Added, DeltaMap.Removed]
    (map DeltaMap.changeKind observed)
  assertEqual "absent before, present-Nothing after"
    [(Nothing, Just Nothing), (Just Nothing, Nothing)]
    (map (\change -> (DeltaMap.changeBefore change, DeltaMap.changeAfter change)) observed)
  assertValid "presence" changed

  absurd <- try (evaluate (DeltaMap.changeKind emptyEndpointChange))
  assertErrorPrefix "doubly absent change"
    "Durable7.FingerTree.PersistentDeltaMap: a change is absent at both endpoints"
    absurd

emptyEndpointChange :: IntChange
emptyEndpointChange = DeltaMap.PersistentMapChange 1 Nothing Nothing

-- Repeated writes coalesce and a return to the checkpoint state cancels the record.
testCoalescingAndCancellation :: IO ()
testCoalescingAndCancellation = do
  let source = DeltaMap.fromListWith textPolicy [(1, "one")]
  assertShares "value no-op shares every root" source (DeltaMap.setItem 1 "one" source)
  assertShares "absent removal shares every root" source (DeltaMap.remove 99 source)

  let first = DeltaMap.setItem 1 "two" source
      second = DeltaMap.setItem 1 "three" first
  assertEqual "coalesced record"
    [DeltaMap.PersistentMapChange 1 (Just "one") (Just "three")]
    (DeltaMap.changes second)
  assertEqual "coalesced record kind" [DeltaMap.Updated] (map DeltaMap.changeKind (DeltaMap.changes second))
  assertEqual "coalesced change count" 1 (DeltaMap.changeCount second)

  let restored = DeltaMap.setItem 1 "one" second
  assertBool "restoration is clean" (not (DeltaMap.hasChanges restored))
  assertEqual "restoration has no changes" [] (DeltaMap.changes restored)
  assertSharesRoot "restoration snaps to the checkpoint root" source restored
  assertCanonicallyClean "restoration is canonically clean" restored

  let addedThenRemoved = DeltaMap.remove 2 (DeltaMap.setItem 2 "second" (DeltaMap.setItem 2 "first" source))
  assertBool "addition cancels on removal" (not (DeltaMap.hasChanges addedThenRemoved))
  assertSharesRoot "cancelled addition snaps back" source addedThenRemoved

  let removedThenRestored = DeltaMap.setItem 1 "one" (DeltaMap.remove 1 source)
  assertBool "removal cancels on restoration" (not (DeltaMap.hasChanges removedThenRestored))
  assertSharesRoot "cancelled removal snaps back" source removedThenRestored

  assertBool "first version retained" (DeltaMap.hasChanges first)
  assertEqual "first version endpoint" [Just "two"] (map DeltaMap.changeAfter (DeltaMap.changes first))
  assertEqual "second version endpoint" [Just "three"] (map DeltaMap.changeAfter (DeltaMap.changes second))
  assertValid "coalescing" second

-- Change enumeration follows the retained key order rather than the natural one.
testRetainedKeyOrder :: IO ()
testRetainedKeyOrder = do
  let source = DeltaMap.fromListWith descendingPolicy [(1, "one"), (3, "three"), (5, "five")]
      changed = DeltaMap.remove 1
        (DeltaMap.setItem 3 "THREE" (DeltaMap.setItem 4 "four" (DeltaMap.setItem 6 "six" source)))
  assertEqual "descending contents"
    [(6, "six"), (5, "five"), (4, "four"), (3, "THREE")]
    (DeltaMap.toList changed)
  assertEqual "descending change keys" [6, 4, 3, 1] (map DeltaMap.changeKey (DeltaMap.changes changed))
  assertEqual "descending change kinds"
    [DeltaMap.Added, DeltaMap.Added, DeltaMap.Updated, DeltaMap.Removed]
    (map DeltaMap.changeKind (DeltaMap.changes changed))
  assertValid "descending order" changed

-- Checkpoint and rollback are root swaps that return the receiver when already clean.
testCheckpointAndRollback :: IO ()
testCheckpointAndRollback = do
  let source = DeltaMap.fromListWith textPolicy [(1, "one"), (2, "two")]
      dirty = DeltaMap.setItem 3 "three" (DeltaMap.setItem 1 "ONE" source)
      committed = DeltaMap.checkpoint dirty
      returned = DeltaMap.rollback dirty

  assertSharesRoot "checkpoint keeps the current root" dirty committed
  assertCanonicallyClean "checkpoint is canonically clean" committed
  assertBool "checkpoint has no changes" (not (DeltaMap.hasChanges committed))
  assertShares "clean checkpoint is identity" committed (DeltaMap.checkpoint committed)
  assertShares "clean rollback is identity" committed (DeltaMap.rollback committed)

  assertCanonicallyClean "rollback is canonically clean" returned
  assertSharesCheckpoint "rollback restores the checkpoint root" returned dirty
  assertBool "rollback has no changes" (not (DeltaMap.hasChanges returned))
  assertShares "clean rollback of a rollback is identity" returned (DeltaMap.rollback returned)
  assertShares "clean checkpoint of a rollback is identity" returned (DeltaMap.checkpoint returned)

  assertBool "dirty version retained" (DeltaMap.hasChanges dirty)
  assertEqual "dirty change count" 2 (DeltaMap.changeCount dirty)
  assertSharesCheckpoint "dirty measures against the source" source dirty
  assertEqual "rollback contents" [(1, "one"), (2, "two")] (DeltaMap.toList returned)
  assertEqual "checkpoint contents"
    [(1, "ONE"), (2, "two"), (3, "three")]
    (DeltaMap.toList committed)
  assertEqual "checkpoint baseline"
    [(1, "ONE"), (2, "two"), (3, "three")]
    (DeltaMap.checkpointToList committed)
  assertValid "checkpoint" committed
  assertValid "rollback" returned

-- Retained branches evolve independently across checkpoints.
testRetainedBranches :: IO ()
testRetainedBranches = do
  let root = DeltaMap.fromListWith textPolicy [(1, "one"), (2, "two")]
      left = DeltaMap.setItem 1 "left" root
      right = DeltaMap.setItem 3 "right" root
  assertSharesCheckpoint "left branches from the root" root left
  assertSharesCheckpoint "right branches from the root" root right
  assertEqual "left change keys" [1] (map DeltaMap.changeKey (DeltaMap.changes left))
  assertEqual "right change keys" [3] (map DeltaMap.changeKey (DeltaMap.changes right))
  assertBool "root stays clean" (not (DeltaMap.hasChanges root))

  let committed = DeltaMap.checkpoint left
      branchA = DeltaMap.remove 2 committed
      branchB = DeltaMap.setItem 1 "branch-b" committed
  assertSharesRoot "checkpoint keeps the branch root" left committed
  assertSharesCheckpoint "branch A measures against the checkpoint" committed branchA
  assertSharesCheckpoint "branch B measures against the checkpoint" committed branchB
  assertEqual "branch A change keys" [2] (map DeltaMap.changeKey (DeltaMap.changes branchA))
  assertEqual "branch B change keys" [1] (map DeltaMap.changeKey (DeltaMap.changes branchB))

  assertEqual "committed contents" [(1, "left"), (2, "two")] (DeltaMap.toList committed)
  assertEqual "branch A contents" [(1, "left")] (DeltaMap.toList branchA)
  assertEqual "branch B contents" [(1, "branch-b"), (2, "two")] (DeltaMap.toList branchB)
  assertEqual "root contents" [(1, "one"), (2, "two")] (DeltaMap.toList root)
  assertEqual "right contents" [(1, "one"), (2, "two"), (3, "right")] (DeltaMap.toList right)
  forM_ [committed, branchA, branchB, root, right] (assertValid "branch")

-- Key representatives survive updates and delete/re-add round trips.
testKeyRepresentatives :: IO ()
testKeyRepresentatives = do
  let source = DeltaMap.fromListWith caseInsensitiveKeyPolicy [("Alpha", 1)]
      updated = DeltaMap.setItem "ALPHA" 2 source
  assertEqual "update keeps the baseline representative" [("Alpha", 2)] (DeltaMap.toList updated)
  assertEqual "record keeps the baseline representative"
    ["Alpha"]
    (map DeltaMap.changeKey (DeltaMap.changes updated))
  assertEqual "class lookup" (Just 2) (DeltaMap.lookup "alpha" updated)
  assertEqual "class entry lookup" (Just ("Alpha", 2)) (DeltaMap.lookupEntry "alpha" updated)

  let roundTripped = DeltaMap.setItem "ALPHA" 1 (DeltaMap.remove "ALPHA" updated)
  assertBool "round trip cancels" (not (DeltaMap.hasChanges roundTripped))
  assertSharesRoot "round trip snaps back" source roundTripped
  assertEqual "round trip keeps the representative" [("Alpha", 1)] (DeltaMap.toList roundTripped)

  let besideOther = DeltaMap.remove "ALPHA" (DeltaMap.setItem "Omega" 9 source)
      restored = DeltaMap.setItem "ALPHA" 1 besideOther
  assertEqual "unrelated change survives cancellation"
    ["Omega"]
    (map DeltaMap.changeKey (DeltaMap.changes restored))
  assertEqual "restored representative" (Just ("Alpha", 1)) (DeltaMap.lookupEntry "alpha" restored)
  assertValid "restored beside another change" restored

  let added = DeltaMap.setItem "BETA" 4 (DeltaMap.setItem "Beta" 3 source)
  assertEqual "addition episode keeps its first representative"
    (Just ("Beta", 4))
    (DeltaMap.lookupEntry "beta" added)
  assertEqual "added record keeps the first representative"
    ["Beta"]
    (map DeltaMap.changeKey (DeltaMap.changes added))
  assertEqual "added class value" (Just 4) (DeltaMap.lookup "BETA" added)

  let cancelled = DeltaMap.remove "BETA" (DeltaMap.setItem "Beta" 1 (DeltaMap.emptyWith caseInsensitiveKeyPolicy))
      newEpisode = DeltaMap.setItem "BETA" 2 cancelled
  assertBool "cancelled addition is clean" (not (DeltaMap.hasChanges cancelled))
  assertEqual "a new episode takes a new representative" [("BETA", 2)] (DeltaMap.toList newEpisode)
  assertEqual "a new episode records the new representative"
    ["BETA"]
    (map DeltaMap.changeKey (DeltaMap.changes newEpisode))
  assertValid "new representative episode" newEpisode

-- Construction keeps the last equivalent entry's representative and value.
testLastEquivalentEntryWins :: IO ()
testLastEquivalentEntryWins = do
  let built = DeltaMap.fromListWith caseInsensitiveKeyPolicy
        [("Alpha", 1), ("Beta", 2), ("ALPHA", 3)]
  assertEqual "class count" 2 (DeltaMap.count built)
  assertEqual "last value wins" (Just 3) (DeltaMap.lookup "Alpha" built)
  assertEqual "last representative wins" (Just ("ALPHA", 3)) (DeltaMap.lookupEntry "alpha" built)
  assertBool "construction is clean" (not (DeltaMap.hasChanges built))
  assertCanonicallyClean "construction is canonically clean" built
  assertValid "construction" built

-- The retained value equality decides no-ops and cancellation.
testValueEquivalencePolicy :: IO ()
testValueEquivalencePolicy = do
  let source = DeltaMap.fromListWith caseInsensitiveValuePolicy [(1, "Alpha")]
  assertShares "equivalent write is a no-op" source (DeltaMap.setItem 1 "ALPHA" source)

  let dirty = DeltaMap.setItem 1 "Beta" source
  assertShares "equivalent rewrite is a no-op" dirty (DeltaMap.setItem 1 "BETA" dirty)
  assertEqual "recorded endpoints"
    [(Just "Alpha", Just "Beta")]
    (map (\change -> (DeltaMap.changeBefore change, DeltaMap.changeAfter change)) (DeltaMap.changes dirty))

  let restored = DeltaMap.setItem 1 "alpha" dirty
  assertBool "equivalent restoration cancels" (not (DeltaMap.hasChanges restored))
  assertSharesRoot "equivalent restoration snaps back" source restored
  assertEqual "the checkpoint representative survives" (Just "Alpha") (DeltaMap.lookup 1 restored)

  let added = DeltaMap.setItem 2 "Value" source
  assertShares "equivalent write to an addition is a no-op" added (DeltaMap.setItem 2 "VALUE" added)
  let cancelledAddition = DeltaMap.remove 2 added
  assertBool "cancelled addition is clean" (not (DeltaMap.hasChanges cancelledAddition))
  assertSharesRoot "cancelled addition snaps back" source cancelledAddition

  let pair = DeltaMap.fromListWith caseInsensitiveValuePolicy [(1, "Alpha"), (2, "Other")]
      bothDirty = DeltaMap.setItem 2 "Changed" (DeltaMap.setItem 1 "Beta" pair)
      oneCancelled = DeltaMap.setItem 1 "alpha" bothDirty
  assertEqual "cancelled key keeps the written value" (Just "alpha") (DeltaMap.lookup 1 oneCancelled)
  assertEqual "only the other key still changed" [2] (map DeltaMap.changeKey (DeltaMap.changes oneCancelled))
  assertValid "partial cancellation" oneCancelled

  let redirtied = DeltaMap.setItem 1 "Gamma" oneCancelled
  assertEqual "a re-dirtied key captures the current baseline"
    [Just "alpha", Just "Other"]
    (map DeltaMap.changeBefore (DeltaMap.changes redirtied))
  assertBool "the recaptured baseline is equivalent to the checkpoint value"
    (DeltaMap.policyValuesEqual caseInsensitiveValuePolicy "Alpha" "alpha")
  assertEqual "a re-dirtied key records the new value"
    [Just "Gamma", Just "Changed"]
    (map DeltaMap.changeAfter (DeltaMap.changes redirtied))
  assertValid "re-dirtied" redirtied

-- Range-restricted change enumeration is inclusive, ordered, and empty when inverted.
testChangeRangeRestriction :: IO ()
testChangeRangeRestriction = do
  let clean = DeltaMap.fromListWith textPolicy [(key, show key) | key <- [0 .. 9]]
  assertEqual "clean full range" [] (map DeltaMap.changeKey (DeltaMap.changesInRange minBound maxBound clean))
  assertEqual "clean inverted range" [] (map DeltaMap.changeKey (DeltaMap.changesInRange 9 0 clean))

  let changed = DeltaMap.setItem 20 "twenty"
        (DeltaMap.setItem 7 "SEVEN"
          (DeltaMap.remove 4
            (DeltaMap.setItem 2 "TWO" (DeltaMap.setItem (-5) "minus-five" clean))))
  assertEqual "all change keys" [-5, 2, 4, 7, 20] (map DeltaMap.changeKey (DeltaMap.changes changed))
  assertEqual "interior window" [2, 4, 7] (rangeKeys 2 7 changed)
  assertEqual "half-covering window" [2, 4] (rangeKeys 1 5 changed)
  assertEqual "singleton window" [4] (rangeKeys 4 4 changed)
  assertEqual "unbounded low window" [-5] (rangeKeys minBound 0 changed)
  assertEqual "unbounded high window" [20] (rangeKeys 8 maxBound changed)
  assertEqual "whole range matches full enumeration"
    (DeltaMap.changes changed)
    (DeltaMap.changesInRange minBound maxBound changed)
  assertEqual "gap window" [] (rangeKeys 8 19 changed)
  assertEqual "window above the last change" [] (rangeKeys 21 100 changed)
  assertEqual "window below the first change" [] (rangeKeys (-100) (-6) changed)
  assertEqual "inverted window" [] (rangeKeys 7 2 changed)
  assertEqual "inverted entry range" [] (DeltaMap.entriesInRange 7 2 changed)
  assertEqual "entry range" [(2, "TWO"), (3, "3")] (DeltaMap.entriesInRange 2 3 changed)

  assertEqual "window endpoints"
    [(Just "2", Just "TWO"), (Just "4", Nothing)]
    (map (\change -> (DeltaMap.changeBefore change, DeltaMap.changeAfter change))
      (DeltaMap.changesInRange 2 4 changed))
  assertEqual "window kinds"
    [DeltaMap.Updated, DeltaMap.Removed]
    (map DeltaMap.changeKind (DeltaMap.changesInRange 2 4 changed))
  assertEqual "window enumeration repeats"
    (DeltaMap.changesInRange 2 4 changed)
    (DeltaMap.changesInRange 2 4 changed)

  let reversed = DeltaMap.setItem 8 "EIGHT"
        (DeltaMap.setItem 4 "FOUR"
          (DeltaMap.setItem 1 "ONE"
            (DeltaMap.fromListWith descendingPolicy [(key, show key) | key <- [0 .. 9]])))
  assertEqual "descending change keys" [8, 4, 1] (map DeltaMap.changeKey (DeltaMap.changes reversed))
  assertEqual "descending window" [8, 4] (rangeKeys 8 4 reversed)
  assertEqual "descending inverted window" [] (rangeKeys 4 8 reversed)
  assertValid "range restriction" changed
  assertValid "descending range restriction" reversed

rangeKeys :: Int -> Int -> IntMap -> [Int]
rangeKeys low high deltaMap = map DeltaMap.changeKey (DeltaMap.changesInRange low high deltaMap)

-- Enumeration invokes no callbacks, and a window seeks rather than scanning every change.
testCallbackBudgets :: IO ()
testCallbackBudgets = do
  keyCalls <- newIORef 0
  valueCalls <- newIORef 0
  let countingPolicy = DeltaMap.DeltaMapPolicy (countingCompare keyCalls) (countingEqual valueCalls)

  small <- pure (threeChangeMap countingPolicy 128)
  assertEqual "small change count" 3 (DeltaMap.changeCount small)
  writeIORef keyCalls 0
  writeIORef valueCalls 0
  assertEqual "small change keys" [-1, 64, 127] (map DeltaMap.changeKey (DeltaMap.changes small))
  smallKeyCalls <- readIORef keyCalls
  smallValueCalls <- readIORef valueCalls
  assertEqual "small enumeration compares no keys" 0 smallKeyCalls
  assertEqual "small enumeration compares no values" 0 smallValueCalls

  large <- pure (threeChangeMap countingPolicy 16_384)
  assertEqual "large change count" 3 (DeltaMap.changeCount large)
  writeIORef keyCalls 0
  writeIORef valueCalls 0
  assertEqual "large change keys" [-1, 8_192, 16_383] (map DeltaMap.changeKey (DeltaMap.changes large))
  largeKeyCalls <- readIORef keyCalls
  largeValueCalls <- readIORef valueCalls
  assertEqual "large enumeration compares no keys" 0 largeKeyCalls
  assertEqual "large enumeration compares no values" 0 largeValueCalls

  writeIORef keyCalls 0
  writeIORef valueCalls 0
  committed <- evaluate (DeltaMap.checkpoint large)
  assertEqual "checkpoint change count" 0 (DeltaMap.changeCount committed)
  returned <- evaluate (DeltaMap.rollback large)
  assertEqual "rollback change count" 0 (DeltaMap.changeCount returned)
  swapKeyCalls <- readIORef keyCalls
  swapValueCalls <- readIORef valueCalls
  assertEqual "root swaps compare no keys" 0 swapKeyCalls
  assertEqual "root swaps compare no values" 0 swapValueCalls

  -- The instrumentation itself must be observable, or the zero counts above prove nothing.
  writeIORef keyCalls 0
  assertBool "instrumented lookup succeeds" (DeltaMap.member 8_192 large)
  probeKeyCalls <- readIORef keyCalls
  assertBool "the key counter observes real work" (probeKeyCalls > 0)

  let windowed = DeltaMap.setItems
        [(key, negate key - 1) | key <- [0, 2 .. 4_094]]
        (DeltaMap.fromListWith countingPolicy [(key, key) | key <- [0 .. 4_095]])
  assertEqual "windowed change count" 2_048 (DeltaMap.changeCount windowed)
  writeIORef keyCalls 0
  writeIORef valueCalls 0
  assertEqual "window contents"
    [1_000, 1_002, 1_004, 1_006]
    (map DeltaMap.changeKey (DeltaMap.changesInRange 1_000 1_007 windowed))
  windowKeyCalls <- readIORef keyCalls
  windowValueCalls <- readIORef valueCalls
  assertEqual "a window compares no values" 0 windowValueCalls
  assertBool "a window does compare keys" (windowKeyCalls > 0)
  -- Two ordered boundary descents over a 2,048-record index, not one descent plus a successor step
  -- per yielded change: the former costs about 2*log2 2048 = 22 comparisons and the latter about 61,
  -- so this ceiling discriminates between them rather than merely ruling out a full scan.
  assertBool
    ("a window restricts rather than stepping; used " ++ show windowKeyCalls ++ " key comparisons")
    (windowKeyCalls < 40)

threeChangeMap :: DeltaMap.DeltaMapPolicy Int Int -> Int -> DeltaMap.PersistentDeltaMap Int Int
threeChangeMap countingPolicy size =
  DeltaMap.remove (size - 1)
    (DeltaMap.setItem (size `div` 2) (negate size)
      (DeltaMap.setItem (-1) (-1)
        (DeltaMap.fromListWith countingPolicy [(key, key) | key <- [0 .. size - 1]])))

-- A batch is exactly the left fold of the single-entry write.
testSetItems :: IO ()
testSetItems = do
  let source = DeltaMap.fromListWith textPolicy [(1, "one"), (2, "two"), (3, "three")]
      batch =
        [ (4, "four")
        , (2, "TWO")
        , (2, "two-again")
        , (1, "one")
        , (4, "FOUR")
        , (3, "three")
        ]
      folded = foldl (\stored (key, value) -> DeltaMap.setItem key value stored) source batch
      bulk = DeltaMap.setItems batch source
  assertEqual "batch contents match the fold" (DeltaMap.toList folded) (DeltaMap.toList bulk)
  assertEqual "batch changes match the fold" (DeltaMap.changes folded) (DeltaMap.changes bulk)
  assertEqual "batch change count matches the fold" (DeltaMap.changeCount folded) (DeltaMap.changeCount bulk)
  assertEqual "last write wins for a new key" (Just "FOUR") (DeltaMap.lookup 4 bulk)
  assertEqual "last write wins for an existing key" (Just "two-again") (DeltaMap.lookup 2 bulk)
  assertEqual "batch change keys" [2, 4] (map DeltaMap.changeKey (DeltaMap.changes bulk))
  assertValid "batch" bulk

  assertShares "an empty batch is a no-op" source (DeltaMap.setItems [] source)
  assertShares "an all-no-op batch is a no-op" source
    (DeltaMap.setItems [(1, "one"), (3, "three")] source)

  let cancelled = DeltaMap.remove 5 (DeltaMap.setItems [(2, "TWO"), (5, "five"), (2, "two")] source)
  assertBool "a fully cancelling batch is clean" (not (DeltaMap.hasChanges cancelled))
  assertSharesRoot "a fully cancelling batch snaps back" source cancelled
  assertCanonicallyClean "a fully cancelling batch is canonically clean" cancelled

  let representatives = DeltaMap.fromListWith caseInsensitiveKeyPolicy [("Alpha", 1)]
      batched = DeltaMap.setItems [("ALPHA", 2), ("ALPHA", 3)] representatives
  assertEqual "a batch keeps the baseline representative" [("Alpha", 3)] (DeltaMap.toList batched)
  assertEqual "a batch records the baseline representative"
    ["Alpha"]
    (map DeltaMap.changeKey (DeltaMap.changes batched))
  let batchAdded = DeltaMap.setItems [("Beta", 5), ("BETA", 6)] representatives
  assertEqual "a batch keeps the first addition representative"
    (Just ("Beta", 6))
    (DeltaMap.lookupEntry "beta" batchAdded)
  assertValid "batched representatives" batchAdded

-- Structural validation recomputes the representation and reports exact statistics.
testStructuralValidation :: IO ()
testStructuralValidation = do
  let source = DeltaMap.fromListWith textPolicy [(1, "one"), (2, "two")]
      changed = DeltaMap.remove 2 (DeltaMap.setItem 1 "ONE" (DeltaMap.setItem 0 "zero" source))
  statistics <- expectRight "validation" (DeltaMap.validateStructure changed)
  assertEqual "validated current count" 2 (DeltaMap.validationCount statistics)
  assertEqual "validated checkpoint count" 2 (DeltaMap.validationCheckpointCount statistics)
  assertEqual "validated change count" 3 (DeltaMap.validationChangeCount statistics)
  assertEqual "validated additions" 1 (DeltaMap.validationAddedCount statistics)
  assertEqual "validated removals" 1 (DeltaMap.validationRemovedCount statistics)
  assertEqual "validated updates" 1 (DeltaMap.validationUpdatedCount statistics)

  emptyStatistics <- expectRight "empty validation"
    (DeltaMap.validateStructure (DeltaMap.emptyWith textPolicy))
  assertEqual "empty validated counts"
    (0, 0, 0)
    ( DeltaMap.validationCount emptyStatistics
    , DeltaMap.validationCheckpointCount emptyStatistics
    , DeltaMap.validationChangeCount emptyStatistics
    )

-- A failing policy leaves the receiver and every retained branch unchanged.
testPolicyFailures :: IO ()
testPolicyFailures = do
  let hostileValues = DeltaMap.DeltaMapPolicy compare (\_ _ -> error "value policy failed")
      valueSource = DeltaMap.fromListWith hostileValues [(1 :: Int, "one")]
  valueResult <- try (evaluate (DeltaMap.count (DeltaMap.setItem 1 "two" valueSource)))
  assertErrorPrefix "value policy failure" "value policy failed" valueResult
  assertEqual "the receiver survives a value policy failure"
    [(1, "one")]
    (DeltaMap.toList valueSource)

  let hostileKeys = DeltaMap.DeltaMapPolicy (\_ _ -> error "key policy failed") (==)
      keySource = DeltaMap.fromListWith hostileKeys [(1 :: Int, "one")]
  keyResult <- try (evaluate (DeltaMap.count (DeltaMap.setItem 2 "two" keySource)))
  assertErrorPrefix "key policy failure" "key policy failed" keyResult
  removeResult <- try (evaluate (DeltaMap.count (DeltaMap.remove 1 keySource)))
  assertErrorPrefix "key policy failure during removal" "key policy failed" removeResult
  assertEqual "the receiver survives a key policy failure"
    [(1, "one")]
    (DeltaMap.toList keySource)
  assertEqual "the receiver's change index survives" [] (DeltaMap.changes keySource)

-- A randomized history is checked against independent checkpoint and current models.
testRandomizedModel :: IO ()
testRandomizedModel = do
  let initial = [(key, key * 10) | key <- [-8, -6 .. 8]]
      start = DeltaMap.fromListWith modelPolicy initial
      startModel = Map.fromList initial
  assertMatchesModel "seed" start startModel startModel
  retained <- walk 0 0x5eed start startModel startModel []
  forM_ retained $ \(index, deltaMap, checkpointModel, currentModel) ->
    assertMatchesModel ("retained " ++ show index) deltaMap checkpointModel currentModel
  assertMatchesModel "seed retained" start startModel startModel
  where
    steps = 3_000 :: Int

    walk step state deltaMap checkpointModel currentModel retained
      | step >= steps = pure retained
      | otherwise = do
          let (choice, state1) = randomBelow state 100
              (offset, state2) = randomBelow state1 51
              key = offset - 25
              (magnitude, state3) = randomBelow state2 15
              value = magnitude - 7
          (nextMap, nextCheckpoint, nextCurrent) <-
            if choice < 50
              then do
                let successor = DeltaMap.setItem key value deltaMap
                assertNoOpShares "set no-op" (Map.lookup key currentModel == Just value) deltaMap successor
                pure (successor, checkpointModel, Map.insert key value currentModel)
              else if choice < 75
                then do
                  let successor = DeltaMap.remove key deltaMap
                  assertNoOpShares "absent removal" (not (Map.member key currentModel)) deltaMap successor
                  pure (successor, checkpointModel, Map.delete key currentModel)
                else if choice < 86
                  then do
                    let successor = DeltaMap.checkpoint deltaMap
                    assertNoOpShares "clean checkpoint" (checkpointModel == currentModel) deltaMap successor
                    pure (successor, currentModel, currentModel)
                  else if choice < 97
                    then do
                      let successor = DeltaMap.rollback deltaMap
                      assertNoOpShares "clean rollback" (checkpointModel == currentModel) deltaMap successor
                      pure (successor, checkpointModel, checkpointModel)
                    else do
                      assertEqual "model lookup"
                        (Map.lookup key currentModel)
                        (DeltaMap.lookup key deltaMap)
                      assertEqual "model membership"
                        (Map.member key currentModel)
                        (DeltaMap.member key deltaMap)
                      pure (deltaMap, checkpointModel, currentModel)
          assertMatchesModel ("step " ++ show step) nextMap nextCheckpoint nextCurrent
          let nextRetained
                | step `mod` 250 == 0 = (step, nextMap, nextCheckpoint, nextCurrent) : retained
                | otherwise = retained
          walk (step + 1) state3 nextMap nextCheckpoint nextCurrent nextRetained

assertMatchesModel :: String
                   -> DeltaMap.PersistentDeltaMap Int Int
                   -> ModelMap
                   -> ModelMap
                   -> IO ()
assertMatchesModel label deltaMap checkpointModel currentModel = do
  statistics <- expectRight (label ++ " validation") (DeltaMap.validateStructure deltaMap)
  assertEqual (label ++ " contents") (Map.toAscList currentModel) (DeltaMap.toList deltaMap)
  assertEqual (label ++ " checkpoint contents")
    (Map.toAscList checkpointModel)
    (DeltaMap.checkpointToList deltaMap)
  assertEqual (label ++ " count") (Map.size currentModel) (DeltaMap.count deltaMap)
  assertEqual (label ++ " emptiness") (Map.null currentModel) (DeltaMap.null deltaMap)
  assertEqual (label ++ " change count") (length expected) (DeltaMap.changeCount deltaMap)
  assertEqual (label ++ " statistics count") (Map.size currentModel) (DeltaMap.validationCount statistics)
  assertEqual (label ++ " statistics change count")
    (length expected)
    (DeltaMap.validationChangeCount statistics)
  assertEqual (label ++ " statistics kinds")
    (countKind DeltaMap.Added, countKind DeltaMap.Removed, countKind DeltaMap.Updated)
    ( DeltaMap.validationAddedCount statistics
    , DeltaMap.validationRemovedCount statistics
    , DeltaMap.validationUpdatedCount statistics
    )
  assertEqual (label ++ " change presence") (not (List.null expected)) (DeltaMap.hasChanges deltaMap)
  assertEqual (label ++ " changes") expected (DeltaMap.changes deltaMap)
  assertEqual (label ++ " windowed changes")
    (filter inWindow expected)
    (DeltaMap.changesInRange (-5) 5 deltaMap)
  assertEqual (label ++ " inverted window") [] (DeltaMap.changesInRange 5 (-5) deltaMap)
  forM_ expected $ \change ->
    assertEqual (label ++ " change lookup")
      (Just change)
      (DeltaMap.lookupChange (DeltaMap.changeKey change) deltaMap)
  assertEqual (label ++ " absent change lookup") Nothing (DeltaMap.lookupChange 10_000 deltaMap)
  clean <- DeltaMap.sharesCheckpointWith deltaMap deltaMap
  assertEqual (label ++ " canonical cleanliness") (List.null expected) clean
  where
    expected = expectedChanges checkpointModel currentModel
    countKind kind = length (filter ((== kind) . DeltaMap.changeKind) expected)
    inWindow change = DeltaMap.changeKey change >= -5 && DeltaMap.changeKey change <= 5

expectedChanges :: ModelMap -> ModelMap -> [IntChange]
expectedChanges checkpointModel currentModel =
  [ DeltaMap.PersistentMapChange key before after
  | key <- Map.keys (Map.union checkpointModel currentModel)
  , let before = Map.lookup key checkpointModel
  , let after = Map.lookup key currentModel
  , before /= after
  ]

textPolicy :: DeltaMap.DeltaMapPolicy Int String
textPolicy = DeltaMap.naturalPolicy

modelPolicy :: DeltaMap.DeltaMapPolicy Int Int
modelPolicy = DeltaMap.naturalPolicy

optionalPolicy :: DeltaMap.DeltaMapPolicy String (Maybe String)
optionalPolicy = DeltaMap.naturalPolicy

descendingPolicy :: DeltaMap.DeltaMapPolicy Int String
descendingPolicy = DeltaMap.DeltaMapPolicy (\left right -> compare right left) (==)

caseInsensitiveKeyPolicy :: DeltaMap.DeltaMapPolicy String Int
caseInsensitiveKeyPolicy =
  DeltaMap.DeltaMapPolicy (\left right -> compare (lowerCase left) (lowerCase right)) (==)

caseInsensitiveValuePolicy :: DeltaMap.DeltaMapPolicy Int String
caseInsensitiveValuePolicy =
  DeltaMap.DeltaMapPolicy compare (\left right -> lowerCase left == lowerCase right)

lowerCase :: String -> String
lowerCase = map toLower

randomBelow :: Word64 -> Int -> (Int, Word64)
randomBelow state bound = (fromIntegral (next `shiftR` 33 `mod` fromIntegral bound), next)
  where
    next = state * 6_364_136_223_846_793_005 + 1_442_695_040_888_963_407

{-# NOINLINE countingCompare #-}
countingCompare :: Ord a => IORef Int -> a -> a -> Ordering
countingCompare calls left right = unsafePerformIO $ do
  modifyIORef' calls (+ 1)
  pure (compare left right)

{-# NOINLINE countingEqual #-}
countingEqual :: Eq a => IORef Int -> a -> a -> Bool
countingEqual calls left right = unsafePerformIO $ do
  modifyIORef' calls (+ 1)
  pure (left == right)

assertValid :: String -> DeltaMap.PersistentDeltaMap key value -> IO ()
assertValid label deltaMap =
  case DeltaMap.validateStructure deltaMap of
    Right _ -> pure ()
    Left message -> failWith (label ++ ": structural validation failed: " ++ message)

assertShares :: String
             -> DeltaMap.PersistentDeltaMap key value
             -> DeltaMap.PersistentDeltaMap key value
             -> IO ()
assertShares label left right = do
  shares <- DeltaMap.sharesStorageWith left right
  assertBool (label ++ ": expected every root to be shared") shares

assertSharesRoot :: String
                 -> DeltaMap.PersistentDeltaMap key value
                 -> DeltaMap.PersistentDeltaMap key value
                 -> IO ()
assertSharesRoot label left right = do
  shares <- DeltaMap.sharesRootWith left right
  assertBool (label ++ ": expected a shared current root") shares

assertSharesCheckpoint :: String
                       -> DeltaMap.PersistentDeltaMap key value
                       -> DeltaMap.PersistentDeltaMap key value
                       -> IO ()
assertSharesCheckpoint label left right = do
  shares <- DeltaMap.sharesCheckpointWith left right
  assertBool (label ++ ": expected the right version to measure against the left") shares

assertCanonicallyClean :: String -> DeltaMap.PersistentDeltaMap key value -> IO ()
assertCanonicallyClean label deltaMap = do
  shares <- DeltaMap.sharesCheckpointWith deltaMap deltaMap
  assertBool (label ++ ": expected the current root to be the checkpoint root") shares
  assertBool (label ++ ": expected no changes") (not (DeltaMap.hasChanges deltaMap))

assertNoOpShares :: String
                 -> Bool
                 -> DeltaMap.PersistentDeltaMap key value
                 -> DeltaMap.PersistentDeltaMap key value
                 -> IO ()
assertNoOpShares label expected receiver successor
  | expected = assertShares (label ++ " shares every root") receiver successor
  | otherwise = pure ()

assertErrorPrefix :: String -> String -> Either ErrorCall a -> IO ()
assertErrorPrefix label expected result =
  case result of
    Left exception ->
      assertBool
        (label ++ ": wrong error, got " ++ displayException exception)
        (expected `List.isPrefixOf` displayException exception)
    Right _ -> failWith (label ++ ": expected an error")

expectRight :: Show e => String -> Either e a -> IO a
expectRight _ (Right value) = pure value
expectRight label (Left errorValue) =
  failWith (label ++ ": expected Right, got " ++ show errorValue)

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = failWith (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool _ True = pure ()
assertBool label False = failWith (label ++ ": expected True")

failWith :: String -> IO a
failWith message = error ("PersistentDeltaMapTests: " ++ message)
