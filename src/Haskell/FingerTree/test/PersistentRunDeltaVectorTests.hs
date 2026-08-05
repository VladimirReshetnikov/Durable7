{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE NumericUnderscores #-}

-- | Tests for the checkpointed run-delta vector: run-index maximality under every local edit,
-- comparison-free structural splices, exact checkpoint-representative restoration, the equality
-- policy's reflexivity obligation, and a randomized check against a retained list model.
module PersistentRunDeltaVectorTests (run) where

import Control.Exception (ErrorCall, evaluate, try)
import Control.Monad (forM_)
import Data.Char (toLower)
import Data.IORef (IORef, modifyIORef', newIORef, readIORef, writeIORef)
import qualified Data.List as List
import Data.Maybe (isJust)
import Data.Word (Word32)
import System.IO.Unsafe (unsafePerformIO)

import qualified Durable7.FingerTree.PersistentRunDeltaVector as RunDelta

-- | Runs this module's test cases.
run :: IO ()
run = do
  testEmptyAndFactories
  testPolicyDefinesNoOpsAndCancellation
  testReferenceIdentityRestoration
  testPointEditsMaintainMaximalRuns
  testAcceptAndRevertRunsAreComparisonFree
  testRunAddressingByContainedPosition
  testClusteredDeltaUsesTwoDescriptors
  testReflexiveIeeePolicy
  testPolicyFailureLeavesTheSourceUsable
  testRandomizedRetainedVersions

testEmptyAndFactories :: IO ()
testEmptyAndFactories = do
  let emptyVector = RunDelta.emptyWith naturalIntEquality
  assertBool "empty is null" (RunDelta.null emptyVector)
  assertEqual "empty count" 0 (RunDelta.count emptyVector)
  assertBool "empty has no changes" (not (RunDelta.hasChanges emptyVector))
  assertEqual "empty dirty count" 0 (RunDelta.dirtyCount emptyVector)
  assertEqual "empty dirty run count" 0 (RunDelta.dirtyRunCount emptyVector)
  assertEqual "empty values" [] (RunDelta.toList emptyVector)
  assertEqual "empty runs" [] (RunDelta.dirtyRuns emptyVector)
  assertEqual "empty index" Nothing (RunDelta.index 0 emptyVector)
  assertEqual "empty checkpoint value" Nothing (RunDelta.checkpointValue 0 emptyVector)
  assertEqual "empty dirtiness" Nothing (RunDelta.isDirty 0 emptyVector)
  assertEqual "empty containing run" Nothing (RunDelta.dirtyRunContaining 0 emptyVector)
  assertEqual "empty run rank" Nothing (RunDelta.dirtyRunAt 0 emptyVector)
  assertEqual "empty set" NothingResult (tagMaybe (RunDelta.setAt 0 1 emptyVector))
  assertEqual "empty reset" NothingResult (tagMaybe (RunDelta.resetAt 0 emptyVector))
  assertEqual "empty accept by rank" NothingResult
    (tagMaybe (RunDelta.acceptDirtyRunAt 0 emptyVector))
  assertEqual "empty revert by rank" NothingResult
    (tagMaybe (RunDelta.revertDirtyRunAt 0 emptyVector))
  assertEqual "empty accept by position" NothingResult
    (tagMaybe (RunDelta.acceptDirtyRunContaining 0 emptyVector))
  assertEqual "empty revert by position" NothingResult
    (tagMaybe (RunDelta.revertDirtyRunContaining 0 emptyVector))
  assertSharesAllRoots "empty checkpoint returns the receiver" emptyVector
    (RunDelta.checkpoint emptyVector)
  assertSharesAllRoots "empty rollback returns the receiver" emptyVector
    (RunDelta.rollback emptyVector)
  _ <- assertValid "empty vector" emptyVector

  let source = naturalVector [0 .. 3]
  assertEqual "built count" 4 (RunDelta.count source)
  assertEqual "built values" [0, 1, 2, 3] (RunDelta.toList source)
  assertBool "built vector is clean" (not (RunDelta.hasChanges source))
  assertEqual "built index" (Just 2) (RunDelta.index 2 source)
  assertEqual "built checkpoint value" (Just 2) (RunDelta.checkpointValue 2 source)
  assertEqual "built dirtiness" (Just False) (RunDelta.isDirty 2 source)
  assertEqual "built containing run" Nothing (RunDelta.dirtyRunContaining 2 source)
  assertEqual "clean vector shares the checkpoint value class" True
    (RunDelta.policyEquals (RunDelta.valueEquality source) 2 2)

  -- Positions outside the vector are Nothing on every position-addressed operation.
  forM_ [-1, 4] $ \position -> do
    assertEqual "out-of-range index" Nothing (RunDelta.index position source)
    assertEqual "out-of-range checkpoint value" Nothing
      (RunDelta.checkpointValue position source)
    assertEqual "out-of-range dirtiness" Nothing (RunDelta.isDirty position source)
    assertEqual "out-of-range containing run" Nothing
      (RunDelta.dirtyRunContaining position source)
    assertEqual "out-of-range set" NothingResult (tagMaybe (RunDelta.setAt position 9 source))
    assertEqual "out-of-range reset" NothingResult (tagMaybe (RunDelta.resetAt position source))
    assertEqual "out-of-range accept by position" NothingResult
      (tagMaybe (RunDelta.acceptDirtyRunContaining position source))
    assertEqual "out-of-range revert by position" NothingResult
      (tagMaybe (RunDelta.revertDirtyRunContaining position source))
  assertEqual "negative rank" Nothing (RunDelta.dirtyRunAt (-1) source)

  -- A clean position is a no-op returning an equal value and sharing every root.
  cleanSet <- expectJust "clean-position write" (RunDelta.setAt 2 2 source)
  cleanReset <- expectJust "clean-position reset" (RunDelta.resetAt 2 source)
  forM_ [cleanSet, cleanReset] $ \unchanged -> do
    assertSharesAllRoots "clean-position no-op shares every root" source unchanged
    assertEqual "clean-position no-op value" (Just 2) (RunDelta.index 2 unchanged)
    assertEqual "clean-position no-op dirty count" 0 (RunDelta.dirtyCount unchanged)
  assertSharesAllRoots "clean accept by position returns the receiver" source
    (expectPure "clean accept" (RunDelta.acceptDirtyRunContaining 1 source))
  assertSharesAllRoots "clean revert by position returns the receiver" source
    (expectPure "clean revert" (RunDelta.revertDirtyRunContaining 1 source))

  let descriptor = RunDelta.DirtyRun 3 2
  assertEqual "run end" 5 (RunDelta.dirtyRunEndExclusive descriptor)
  assertBool "run contains its start" (RunDelta.dirtyRunContains 3 descriptor)
  assertBool "run contains its interior" (RunDelta.dirtyRunContains 4 descriptor)
  assertBool "run excludes its end" (not (RunDelta.dirtyRunContains 5 descriptor))
  assertBool "run excludes before its start" (not (RunDelta.dirtyRunContains 2 descriptor))

testPolicyDefinesNoOpsAndCancellation :: IO ()
testPolicyDefinesNoOpsAndCancellation = do
  let policy = RunDelta.EqualityPolicy (\left right -> map toLower left == map toLower right)
      source = RunDelta.fromListWith policy ["Alpha", "Beta"]
  noOp <- expectJust "policy-equal write" (RunDelta.setAt 0 "ALPHA" source)
  assertBool "policy-equal write is a semantic no-op" (not (RunDelta.hasChanges noOp))
  assertEqual "policy-equal write keeps the stored value" (Just "Alpha") (RunDelta.index 0 noOp)
  assertEqual "policy-equal write leaves the dirty count" 0 (RunDelta.dirtyCount noOp)
  assertEqual "policy-equal write leaves the run count" 0 (RunDelta.dirtyRunCount noOp)
  assertSharesAllRoots "policy-equal write shares every root" source noOp
  assertSharesRepresentative "policy-equal write keeps the current representative" 0 source noOp

  dirty <- expectJust "policy-distinct write" (RunDelta.setAt 0 "Gamma" source)
  assertEqual "written value" (Just "Gamma") (RunDelta.index 0 dirty)
  assertEqual "checkpoint value" (Just "Alpha") (RunDelta.checkpointValue 0 dirty)
  assertEqual "one singleton run" [RunDelta.DirtyRun 0 1] (RunDelta.dirtyRuns dirty)
  -- The identity witnesses discriminate: a dirty position holds a different representative, and an
  -- edited version does not share the source's current root.
  dirtyShares <- RunDelta.sharesCheckpointRepresentativeAt 0 dirty
  assertBool "a dirty position holds a distinct representative" (not dirtyShares)
  editedShares <- RunDelta.sharesCurrentRootWith dirty source
  assertBool "an edited version replaces the current root" (not editedShares)
  _ <- assertValid "policy-distinct write" dirty

  cancelled <- expectJust "cancelling write" (RunDelta.setAt 0 "alpha" dirty)
  assertBool "cancellation clears every change" (not (RunDelta.hasChanges cancelled))
  assertEqual "cancellation restores the checkpoint value" (Just "Alpha")
    (RunDelta.index 0 cancelled)
  assertCheckpointRepresentative
    "cancellation restores the exact checkpoint representative" 0 cancelled
  -- Compared against the clean source rather than against @checkpoint cancelled@: checkpoint
  -- returns its receiver when the version is already clean, so that comparison would hold whatever
  -- the implementation did.  The source's current root /is/ the checkpoint root, so this
  -- discriminates.
  assertSharesAllRoots "cancellation canonicalizes onto the checkpoint" source cancelled
  sharesCheckpoint <- RunDelta.sharesCheckpointRootWith cancelled source
  assertBool "cancellation retains the original checkpoint root" sharesCheckpoint
  _ <- assertValid "cancelled version" cancelled

  -- Every retained intermediate version is untouched.
  assertEqual "retained dirty version" (Just "Gamma") (RunDelta.index 0 dirty)
  assertEqual "retained source" ["Alpha", "Beta"] (RunDelta.toList source)

testReferenceIdentityRestoration :: IO ()
testReferenceIdentityRestoration = do
  -- 'IORef' equality is reference identity, which is the Haskell analogue of the C# baseline's
  -- reference comparer: two cells holding equal contents are still distinct representatives.
  checkpointRepresentative <- newIORef (7 :: Int)
  replacementRepresentative <- newIORef (7 :: Int)
  checkpointContent <- readIORef checkpointRepresentative
  replacementContent <- readIORef replacementRepresentative
  assertEqual "representatives hold equal contents" checkpointContent replacementContent
  assertBool "representatives are distinct references"
    (checkpointRepresentative /= replacementRepresentative)

  let policy = RunDelta.EqualityPolicy ((==) :: IORef Int -> IORef Int -> Bool)
      source = RunDelta.fromListWith policy [checkpointRepresentative]
  sameReferenceWrite <- expectJust "same-reference write"
    (RunDelta.setAt 0 checkpointRepresentative source)
  assertBool "same-reference write is a no-op" (not (RunDelta.hasChanges sameReferenceWrite))
  assertSharesAllRoots "same-reference write shares every root" source sameReferenceWrite

  dirty <- expectJust "distinct-reference write"
    (RunDelta.setAt 0 replacementRepresentative source)
  assertEqual "distinct reference is dirty" (Just True) (RunDelta.isDirty 0 dirty)
  assertBool "current holds the replacement reference"
    (RunDelta.index 0 dirty == Just replacementRepresentative)
  assertBool "checkpoint holds the original reference"
    (RunDelta.checkpointValue 0 dirty == Just checkpointRepresentative)
  _ <- assertValid "distinct-reference write" dirty

  reset <- expectJust "reference reset" (RunDelta.resetAt 0 dirty)
  assertBool "reset clears the change" (not (RunDelta.hasChanges reset))
  assertBool "reset restores the exact reference, not an equal value"
    (RunDelta.index 0 reset == Just checkpointRepresentative)
  assertCheckpointRepresentative "reset restores the exact heap representative" 0 reset
  assertSharesAllRoots "reset canonicalizes onto the checkpoint root" source reset
  () <$ assertValid "reference reset" reset

testPointEditsMaintainMaximalRuns :: IO ()
testPointEditsMaintainMaximalRuns = do
  -- A fresh dirty position with a run only on its left extends that run.
  leftMerged <- applyEdits (naturalVector (replicate 12 0)) [(4, 40), (5, 50)]
  assertRuns "left merge" leftMerged [RunDelta.DirtyRun 4 2]
  -- A fresh dirty position with a run only on its right absorbs that run.
  rightMerged <- applyEdits (naturalVector (replicate 12 0)) [(5, 50), (4, 40)]
  assertRuns "right merge" rightMerged [RunDelta.DirtyRun 4 2]

  let cleanBase = naturalVector (replicate 12 (0 :: Int))
  step0 <- applyEdits cleanBase [(2, 20), (4, 40)]
  assertRuns "two singleton runs" step0 [RunDelta.DirtyRun 2 1, RunDelta.DirtyRun 4 1]
  assertEqual "rank past the end" Nothing (RunDelta.dirtyRunAt 2 step0)
  assertEqual "negative rank" Nothing (RunDelta.dirtyRunAt (-1) step0)

  -- A new position between two singleton runs merges both.
  step1 <- applyEdits step0 [(3, 30)]
  assertRuns "both-sided merge" step1 [RunDelta.DirtyRun 2 3]

  -- A detached position, then the gap joining it to the run on its left.
  step2 <- applyEdits step1 [(6, 60), (5, 50)]
  assertRuns "gap closure" step2 [RunDelta.DirtyRun 2 5]

  -- Interior clearing splits one run four ways: left remainder plus right remainder.
  step3 <- applyResets step2 [4]
  assertRuns "interior split" step3 [RunDelta.DirtyRun 2 2, RunDelta.DirtyRun 5 2]
  -- Left-edge clearing shrinks from the start.
  step4 <- applyResets step3 [2]
  assertRuns "left-edge shrink" step4 [RunDelta.DirtyRun 3 1, RunDelta.DirtyRun 5 2]
  -- Right-edge clearing shrinks from the end.
  step5 <- applyResets step4 [6]
  assertRuns "right-edge shrink" step5 [RunDelta.DirtyRun 3 1, RunDelta.DirtyRun 5 1]

  -- Clearing the last dirty position canonicalizes the current root onto the checkpoint root.
  step6 <- applyResets step5 [3, 5]
  assertBool "cleared version has no changes" (not (RunDelta.hasChanges step6))
  assertEqual "cleared version has no runs" [] (RunDelta.dirtyRuns step6)
  assertEqual "cleared version values" (replicate 12 0) (RunDelta.toList step6)
  -- Against the clean ancestor, not against @rollback step6@/@checkpoint step6@: both return the
  -- receiver once the version is clean, so those comparisons cannot fail.
  assertSharesAllRoots "cleared version canonicalizes onto its checkpoint" cleanBase step6
  forM_ [0 .. 11] $ \position ->
    assertCheckpointRepresentative "every cleared position shares its representative" position step6
  _ <- assertValid "cleared version" step6

  -- Resetting an already clean position returns the receiver.
  unchanged <- expectJust "clean reset" (RunDelta.resetAt 3 step6)
  assertSharesAllRoots "clean reset returns the receiver" step6 unchanged

  -- Checkpoint and rollback are O(1) root swaps returning the receiver when already clean.
  assertSharesAllRoots "clean checkpoint returns the receiver" step6 (RunDelta.checkpoint step6)
  assertSharesAllRoots "clean rollback returns the receiver" step6 (RunDelta.rollback step6)
  wholeEdited <- applyEdits step6 [(1, 11), (7, 77)]
  let accepted = RunDelta.checkpoint wholeEdited
      reverted = RunDelta.rollback wholeEdited
  assertBool "whole checkpoint clears every change" (not (RunDelta.hasChanges accepted))
  assertEqual "whole checkpoint keeps the current values" (RunDelta.toList wholeEdited)
    (RunDelta.toList accepted)
  acceptedShares <- RunDelta.sharesCurrentRootWith accepted wholeEdited
  assertBool "whole checkpoint swaps in the current root" acceptedShares
  assertBool "whole rollback clears every change" (not (RunDelta.hasChanges reverted))
  assertEqual "whole rollback restores the checkpoint values" (replicate 12 0)
    (RunDelta.toList reverted)
  revertedShares <- RunDelta.sharesCheckpointRootWith reverted wholeEdited
  assertBool "whole rollback swaps in the checkpoint root" revertedShares
  _ <- assertValid "whole checkpoint" accepted
  _ <- assertValid "whole rollback" reverted
  assertRuns "retained edited version" wholeEdited
    [RunDelta.DirtyRun 1 1, RunDelta.DirtyRun 7 1]

testAcceptAndRevertRunsAreComparisonFree :: IO ()
testAcceptAndRevertRunsAreComparisonFree = do
  counter <- newIORef 0
  let policy = countingEquality counter
      original = RunDelta.fromListWith policy [0 .. 19]
  edited <- applyEdits original [(position, 1000 + position) | position <- [2 .. 5] ++ [9 .. 13] ++ [17]]
  let expected =
        [RunDelta.DirtyRun 2 4, RunDelta.DirtyRun 9 5, RunDelta.DirtyRun 17 1]
  forceVector edited
  -- The counting policy really is consulted by point edits, so a later zero delta around a splice
  -- is evidence about the splice rather than about an idle counter.
  editCalls <- readIORef counter
  assertBool "point edits consult the value policy" (editCalls > 0)
  assertRuns "clustered edits" edited expected
  assertEqual "clustered dirty count" 10 (RunDelta.dirtyCount edited)

  acceptedMiddle <-
    spliceWithoutComparisons counter "accepting a run" (RunDelta.acceptDirtyRunAt 1 edited)
  assertRuns "accepted middle run" acceptedMiddle
    [RunDelta.DirtyRun 2 4, RunDelta.DirtyRun 17 1]
  forM_ [9 .. 13] $ \position -> do
    assertEqual "accepted current value" (Just (1000 + position)) (RunDelta.index position acceptedMiddle)
    assertEqual "accepted checkpoint value" (Just (1000 + position))
      (RunDelta.checkpointValue position acceptedMiddle)

  revertedFirst <-
    spliceWithoutComparisons counter "reverting a run" (RunDelta.revertDirtyRunAt 0 acceptedMiddle)
  assertRuns "reverted first run" revertedFirst [RunDelta.DirtyRun 17 1]
  forM_ [2 .. 5] $ \position ->
    assertEqual "reverted current value" (Just position) (RunDelta.index position revertedFirst)

  clean <-
    spliceWithoutComparisons counter "accepting the last run" (RunDelta.acceptDirtyRunAt 0 revertedFirst)
  assertBool "the last accepted run leaves a clean version" (not (RunDelta.hasChanges clean))
  assertEqual "the last accepted run is in the checkpoint" (Just 1017)
    (RunDelta.checkpointValue 17 clean)
  -- Accepting folds the current root into the checkpoint, so there is no earlier clean version with
  -- these contents to compare against, and @checkpoint clean@ would just return the receiver.  The
  -- discriminating consequence is per position: every cell must be the very cell the current root
  -- holds, which a rebuilt-but-equal checkpoint would fail.
  forM_ [0 .. 19] $ \position ->
    assertCheckpointRepresentative
      "every position of the accepted version shares its representative" position clean

  -- Every retained source version is unchanged.
  assertRuns "retained edited version" edited expected
  assertBool "retained original is clean" (not (RunDelta.hasChanges original))
  assertEqual "retained original values" [0 .. 19] (RunDelta.toList original)
  forM_ [clean, revertedFirst, acceptedMiddle] $ \version ->
    () <$ assertValid "retained spliced version" version

  assertEqual "accept past the last rank" NothingResult
    (tagMaybe (RunDelta.acceptDirtyRunAt 3 edited))
  assertEqual "revert past the last rank" NothingResult
    (tagMaybe (RunDelta.revertDirtyRunAt 3 edited))
  assertEqual "accept at a negative rank" NothingResult
    (tagMaybe (RunDelta.acceptDirtyRunAt (-1) edited))
  assertEqual "revert at a negative rank" NothingResult
    (tagMaybe (RunDelta.revertDirtyRunAt (-1) edited))

testRunAddressingByContainedPosition :: IO ()
testRunAddressingByContainedPosition = do
  counter <- newIORef 0
  let policy = countingEquality counter
      original = RunDelta.fromListWith policy [0 .. 19]
  edited <- applyEdits original [(position, 1000 + position) | position <- [2 .. 5] ++ [9 .. 13] ++ [17]]
  let expected =
        [RunDelta.DirtyRun 2 4, RunDelta.DirtyRun 9 5, RunDelta.DirtyRun 17 1]
  assertRuns "position-addressed source" edited expected

  assertEqual "accept outside the vector" NothingResult
    (tagMaybe (RunDelta.acceptDirtyRunContaining 20 edited))
  assertEqual "revert outside the vector" NothingResult
    (tagMaybe (RunDelta.revertDirtyRunContaining 20 edited))

  -- A clean position is vacuous: the receiver comes back, sharing every root, with no comparison.
  forM_ [0, 1, 6, 8, 14, 19] $ \position -> do
    assertEqual "clean position" (Just False) (RunDelta.isDirty position edited)
    before <- readIORef counter
    let acceptedClean = expectPure "clean accept" (RunDelta.acceptDirtyRunContaining position edited)
        revertedClean = expectPure "clean revert" (RunDelta.revertDirtyRunContaining position edited)
    forM_ [acceptedClean, revertedClean] $ \unchanged -> do
      forceVector unchanged
      assertSharesAllRoots "clean position shares every root" edited unchanged
      assertEqual "clean position keeps the dirty count" (RunDelta.dirtyCount edited)
        (RunDelta.dirtyCount unchanged)
      assertEqual "clean position keeps the runs" expected (RunDelta.dirtyRuns unchanged)
    after <- readIORef counter
    assertEqual "a clean position consults no comparer" before after

  -- Every position inside a run acts exactly as the rank-addressed operation on that run.
  forM_ (zip [0 ..] expected) $ \(rank, descriptor) -> do
    acceptedByRank <- expectJust "rank accept" (RunDelta.acceptDirtyRunAt rank edited)
    revertedByRank <- expectJust "rank revert" (RunDelta.revertDirtyRunAt rank edited)
    forM_ [RunDelta.dirtyRunStart descriptor .. RunDelta.dirtyRunEndExclusive descriptor - 1] $
      \position -> do
        assertEqual "containing run" (Just descriptor) (RunDelta.dirtyRunContaining position edited)
        acceptedByPosition <-
          spliceWithoutComparisons counter "accepting by contained position"
            (RunDelta.acceptDirtyRunContaining position edited)
        revertedByPosition <-
          spliceWithoutComparisons counter "reverting by contained position"
            (RunDelta.revertDirtyRunContaining position edited)
        assertSameObservableState "accept by position" acceptedByRank acceptedByPosition
        assertSameObservableState "revert by position" revertedByRank revertedByPosition

  -- A vector with exactly one run collapses to the whole-version operations.
  single <- applyEdits original [(4, 400), (5, 500)]
  assertRuns "single run" single [RunDelta.DirtyRun 4 2]
  acceptedSingle <- expectJust "single accept" (RunDelta.acceptDirtyRunContaining 5 single)
  assertBool "single accept clears the version" (not (RunDelta.hasChanges acceptedSingle))
  assertEqual "single accept promotes the current value" (Just 400)
    (RunDelta.checkpointValue 4 acceptedSingle)
  revertedSingle <- expectJust "single revert" (RunDelta.revertDirtyRunContaining 5 single)
  assertBool "single revert clears the version" (not (RunDelta.hasChanges revertedSingle))
  assertEqual "single revert restores position four" (Just 4) (RunDelta.index 4 revertedSingle)
  assertEqual "single revert restores position five" (Just 5) (RunDelta.index 5 revertedSingle)
  _ <- assertValid "single accept" acceptedSingle
  _ <- assertValid "single revert" revertedSingle
  assertRuns "retained single-run version" single [RunDelta.DirtyRun 4 2]

testClusteredDeltaUsesTwoDescriptors :: IO ()
testClusteredDeltaUsesTwoDescriptors = do
  let total = 8_192
      source = naturalVector (replicate total 0)
      clustered =
        List.foldl'
          (\acc position -> expectPure "clustered write" (RunDelta.setAt position 1 acc))
          source
          [0 .. total - 3]
  edited <- expectJust "isolated write" (RunDelta.setAt (total - 1) 1 clustered)
  assertEqual "thousands of dirty positions" (total - 1) (RunDelta.dirtyCount edited)
  assertEqual "two run descriptors" 2 (RunDelta.dirtyRunCount edited)
  assertEqual "run descriptors"
    [RunDelta.DirtyRun 0 (total - 2), RunDelta.DirtyRun (total - 1) 1]
    (RunDelta.dirtyRuns edited)

  accepted <- expectJust "clustered accept" (RunDelta.acceptDirtyRunAt 0 edited)
  assertEqual "accepted clustered runs" [RunDelta.DirtyRun (total - 1) 1]
    (RunDelta.dirtyRuns accepted)
  assertEqual "accepted clustered dirty count" 1 (RunDelta.dirtyCount accepted)
  assertEqual "accepted clustered checkpoint head" (Just 1) (RunDelta.checkpointValue 0 accepted)
  assertEqual "accepted clustered checkpoint tail" (Just 0)
    (RunDelta.checkpointValue (total - 1) accepted)
  assertEqual "retained clustered source" (replicate total 0) (RunDelta.toList source)
  statistics <- assertValid "clustered edited version" edited
  assertEqual "clustered longest run" (total - 2)
    (RunDelta.statisticsMaximumRunLength statistics)
  _ <- assertValid "clustered accepted version" accepted
  pure ()

testReflexiveIeeePolicy :: IO ()
testReflexiveIeeePolicy = do
  let notANumber = 0 / 0 :: Double
      source = RunDelta.fromListWith RunDelta.reflexiveIeeeEquality [notANumber, 1.0, 0.0]
  rewritten <- expectJust "NaN rewrite" (RunDelta.setAt 0 notANumber source)
  assertBool "a NaN rewrite is a no-op" (not (RunDelta.hasChanges rewritten))
  assertEqual "a NaN rewrite leaves no dirty position" 0 (RunDelta.dirtyCount rewritten)
  assertEqual "a NaN rewrite leaves no run" 0 (RunDelta.dirtyRunCount rewritten)
  assertSharesAllRoots "a NaN rewrite shares every root" source rewritten
  assertBool "a NaN rewrite keeps NaN" (maybe False isNaN (RunDelta.index 0 rewritten))
  _ <- assertValid "NaN rewrite" rewritten

  -- Signed zeros are one equality class, matching the C# baseline's default comparer.
  signedZero <- expectJust "negative zero write" (RunDelta.setAt 2 (-0.0) source)
  assertBool "signed zeros are one class" (not (RunDelta.hasChanges signedZero))
  assertSharesAllRoots "a signed-zero rewrite shares every root" source signedZero

  dirty <- expectJust "float write" (RunDelta.setAt 0 2.0 source)
  assertEqual "a genuine float change is recorded" [RunDelta.DirtyRun 0 1] (RunDelta.dirtyRuns dirty)
  cancelled <- expectJust "NaN cancellation" (RunDelta.setAt 0 notANumber dirty)
  assertBool "returning NaN cancels the change" (not (RunDelta.hasChanges cancelled))
  assertBool "cancellation restores NaN" (maybe False isNaN (RunDelta.index 0 cancelled))
  assertCheckpointRepresentative "float cancellation restores the representative" 0 cancelled
  _ <- assertValid "float cancellation" cancelled

  -- The caller's obligation, made visible: a non-reflexive relation records phantom changes.
  let naive = RunDelta.EqualityPolicy ((==) :: Double -> Double -> Bool)
      naiveSource = RunDelta.fromListWith naive [notANumber]
  phantom <- expectJust "non-reflexive rewrite" (RunDelta.setAt 0 notANumber naiveSource)
  assertEqual "a non-reflexive relation records a phantom change" 1 (RunDelta.dirtyCount phantom)
  repeated <- expectJust "repeated non-reflexive rewrite" (RunDelta.setAt 0 notANumber phantom)
  assertEqual "the phantom run cannot be written away" 1 (RunDelta.dirtyCount repeated)

testPolicyFailureLeavesTheSourceUsable :: IO ()
testPolicyFailureLeavesTheSourceUsable = do
  state <- newIORef (0, 0)
  let policy = RunDelta.EqualityPolicy (\left right -> unsafePerformIO (failingEquals state left right))
      source = RunDelta.fromListWith policy [1, 2, 3 :: Int]
  writeIORef state (0, 2)
  outcome <- try (evaluate (tagMaybe (RunDelta.setAt 1 99 source)))
  case outcome :: Either ErrorCall MaybeResult of
    Left _ -> pure ()
    Right _ -> failure "a failing value policy did not propagate"

  writeIORef state (0, 0)
  assertEqual "the source survives a failing policy" [1, 2, 3] (RunDelta.toList source)
  assertBool "the source stays clean" (not (RunDelta.hasChanges source))
  _ <- assertValid "source after a policy failure" source
  edited <- expectJust "write after a policy failure" (RunDelta.setAt 1 99 source)
  assertEqual "the retried write lands" [1, 99, 3] (RunDelta.toList edited)
  assertEqual "the retried write records one run" [RunDelta.DirtyRun 1 1] (RunDelta.dirtyRuns edited)
  assertEqual "the source is still retained" [1, 2, 3] (RunDelta.toList source)

testRandomizedRetainedVersions :: IO ()
testRandomizedRetainedVersions = do
  let modelLength = 24
      initial = [position `mod` 5 | position <- [0 .. modelLength - 1]]
      start = ModelVersion (naturalVector initial) initial initial
  retained <- walk 0 0x5a17_2026 [start]
  forM_ (zip [0 :: Int ..] retained) $ \(rank, version) ->
    assertMatchesModel
      ("final retained version " ++ show rank)
      (modelVector version)
      (modelCurrent version)
      (modelCheckpoint version)
  where
    walk :: Int -> Word32 -> [ModelVersion] -> IO [ModelVersion]
    walk step random versions
      | step == 400 = pure versions
      | otherwise = do
          let firstDraw = nextRandom random
              secondDraw = nextRandom firstDraw
              thirdDraw = nextRandom secondDraw
              fourthDraw = nextRandom thirdDraw
              fifthDraw = nextRandom fourthDraw
              parent = versions !! boundedRandom firstDraw (length versions)
              modelLength = length (modelCurrent parent)
              position = boundedRandom thirdDraw modelLength
              value = boundedRandom fourthDraw 9 - 4
              runs = expectedRuns (modelCurrent parent) (modelCheckpoint parent)
              choice =
                case boundedRandom secondDraw 6 of
                  5 | List.null runs -> 0
                  other -> other
          updated <-
            case choice of
              2 -> do
                next <- expectJust "model reset" (RunDelta.resetAt position (modelVector parent))
                pure
                  (ModelVersion
                    next
                    (replaceAt position (modelCheckpoint parent !! position) (modelCurrent parent))
                    (modelCheckpoint parent))
              3 ->
                pure
                  (ModelVersion
                    (RunDelta.checkpoint (modelVector parent))
                    (modelCurrent parent)
                    (modelCurrent parent))
              4 ->
                pure
                  (ModelVersion
                    (RunDelta.rollback (modelVector parent))
                    (modelCheckpoint parent)
                    (modelCheckpoint parent))
              5 -> do
                let rank = boundedRandom fifthDraw (length runs)
                    descriptor = runs !! rank
                    contained =
                      RunDelta.dirtyRunStart descriptor
                        + boundedRandom thirdDraw (RunDelta.dirtyRunLength descriptor)
                    byPosition = even (boundedRandom fourthDraw 2)
                    accepting = even (boundedRandom secondDraw 2)
                next <-
                  expectJust "model run splice" $
                    case (accepting, byPosition) of
                      (True, True) ->
                        RunDelta.acceptDirtyRunContaining contained (modelVector parent)
                      (True, False) -> RunDelta.acceptDirtyRunAt rank (modelVector parent)
                      (False, True) ->
                        RunDelta.revertDirtyRunContaining contained (modelVector parent)
                      (False, False) -> RunDelta.revertDirtyRunAt rank (modelVector parent)
                pure $
                  if accepting
                    then
                      ModelVersion
                        next
                        (modelCurrent parent)
                        (spliceRange descriptor (modelCurrent parent) (modelCheckpoint parent))
                    else
                      ModelVersion
                        next
                        (spliceRange descriptor (modelCheckpoint parent) (modelCurrent parent))
                        (modelCheckpoint parent)
              _ -> do
                next <- expectJust "model write" (RunDelta.setAt position value (modelVector parent))
                pure
                  (ModelVersion
                    next
                    (replaceAt position value (modelCurrent parent))
                    (modelCheckpoint parent))
          assertMatchesModel
            ("step " ++ show step)
            (modelVector updated)
            (modelCurrent updated)
            (modelCheckpoint updated)
          assertMatchesModel
            ("retained parent at step " ++ show step)
            (modelVector parent)
            (modelCurrent parent)
            (modelCheckpoint parent)
          let extended = updated : versions
              trimmed =
                if length extended > 12
                  then take 11 extended ++ [last extended]
                  else extended
          walk (step + 1) fifthDraw trimmed

data ModelVersion = ModelVersion
  { modelVector :: RunDelta.PersistentRunDeltaVector Int
  , modelCurrent :: [Int]
  , modelCheckpoint :: [Int]
  }

data MaybeResult = NothingResult | JustResult
  deriving (Eq, Show)

tagMaybe :: Maybe a -> MaybeResult
tagMaybe Nothing = NothingResult
tagMaybe (Just _) = JustResult

naturalIntEquality :: RunDelta.EqualityPolicy Int
naturalIntEquality = RunDelta.naturalEquality

naturalVector :: [Int] -> RunDelta.PersistentRunDeltaVector Int
naturalVector = RunDelta.fromListWith naturalIntEquality

-- | Counts every policy call so run splices can be shown to make none.
countingEquality :: IORef Int -> RunDelta.EqualityPolicy Int
countingEquality counter =
  RunDelta.EqualityPolicy (\left right -> unsafePerformIO (countedEquals counter left right))

countedEquals :: IORef Int -> Int -> Int -> IO Bool
countedEquals counter left right = do
  modifyIORef' counter (+ 1)
  pure (left == right)
{-# NOINLINE countedEquals #-}

-- | Fails on one armed call, standing in for the C# baseline's throwing comparer.
failingEquals :: IORef (Int, Int) -> Int -> Int -> IO Bool
failingEquals state left right = do
  (calls, armed) <- readIORef state
  let attempt = calls + 1
  writeIORef state (attempt, armed)
  if armed /= 0 && attempt == armed
    then error "PersistentRunDeltaVectorTests: value policy failure"
    else pure (left == right)
{-# NOINLINE failingEquals #-}

applyEdits
  :: RunDelta.PersistentRunDeltaVector Int
  -> [(Int, Int)]
  -> IO (RunDelta.PersistentRunDeltaVector Int)
applyEdits vector [] = pure vector
applyEdits vector ((position, value) : rest) = do
  next <- expectJust ("write at " ++ show position) (RunDelta.setAt position value vector)
  applyEdits next rest

applyResets
  :: RunDelta.PersistentRunDeltaVector Int
  -> [Int]
  -> IO (RunDelta.PersistentRunDeltaVector Int)
applyResets vector [] = pure vector
applyResets vector (position : rest) = do
  next <- expectJust ("reset at " ++ show position) (RunDelta.resetAt position vector)
  applyResets next rest

-- | Forces every stored representative in both roots without consulting the value policy, so a
-- comparison count taken around a splice measures the splice rather than an unforced thunk.
forceVector :: RunDelta.PersistentRunDeltaVector Int -> IO ()
forceVector vector = do
  _ <- evaluate (sum (RunDelta.toList vector))
  _ <-
    evaluate
      (sum
        [ value
        | position <- [0 .. RunDelta.count vector - 1]
        , Just value <- [RunDelta.checkpointValue position vector]
        ])
  _ <- evaluate (length (RunDelta.dirtyRuns vector))
  _ <- evaluate (RunDelta.dirtyCount vector)
  pure ()

spliceWithoutComparisons
  :: IORef Int
  -> String
  -> Maybe (RunDelta.PersistentRunDeltaVector Int)
  -> IO (RunDelta.PersistentRunDeltaVector Int)
spliceWithoutComparisons counter label candidate = do
  before <- readIORef counter
  spliced <- expectJust label candidate
  forceVector spliced
  after <- readIORef counter
  assertEqual (label ++ " consults no comparer") before after
  pure spliced

expectedRuns :: [Int] -> [Int] -> [RunDelta.DirtyRun]
expectedRuns current checkpointModel = go 0 (zipWith (/=) current checkpointModel)
  where
    go _ [] = []
    go !position (False : rest) = go (position + 1) rest
    go !position flags =
      let amount = length (takeWhile id flags)
      in RunDelta.DirtyRun position amount : go (position + amount) (drop amount flags)

replaceAt :: Int -> Int -> [Int] -> [Int]
replaceAt position value values =
  take position values ++ value : drop (position + 1) values

spliceRange :: RunDelta.DirtyRun -> [Int] -> [Int] -> [Int]
spliceRange descriptor source destination =
  take start destination
    ++ take amount (drop start source)
    ++ drop (start + amount) destination
  where
    start = RunDelta.dirtyRunStart descriptor
    amount = RunDelta.dirtyRunLength descriptor

assertRuns
  :: String -> RunDelta.PersistentRunDeltaVector Int -> [RunDelta.DirtyRun] -> IO ()
assertRuns label vector expected = do
  assertEqual (label ++ " dirty count")
    (sum (map RunDelta.dirtyRunLength expected))
    (RunDelta.dirtyCount vector)
  assertEqual (label ++ " run count") (length expected) (RunDelta.dirtyRunCount vector)
  assertEqual (label ++ " runs") expected (RunDelta.dirtyRuns vector)
  assertEqual (label ++ " past-end rank") Nothing (RunDelta.dirtyRunAt (length expected) vector)
  forM_ (zip [0 ..] expected) $ \(rank, descriptor) -> do
    assertEqual (label ++ " run by rank") (Just descriptor) (RunDelta.dirtyRunAt rank vector)
    forM_ [RunDelta.dirtyRunStart descriptor .. RunDelta.dirtyRunEndExclusive descriptor - 1] $
      \position -> do
        assertEqual (label ++ " dirty position") (Just True) (RunDelta.isDirty position vector)
        assertEqual (label ++ " containing run") (Just descriptor)
          (RunDelta.dirtyRunContaining position vector)
  () <$ assertValid label vector

assertMatchesModel
  :: String -> RunDelta.PersistentRunDeltaVector Int -> [Int] -> [Int] -> IO ()
assertMatchesModel label vector current checkpointModel = do
  assertEqual (label ++ " count") (length current) (RunDelta.count vector)
  assertEqual (label ++ " values") current (RunDelta.toList vector)
  let runs = expectedRuns current checkpointModel
  forM_ (zip3 [0 ..] current checkpointModel) $ \(position, value, checkpointItem) -> do
    assertEqual (label ++ " index") (Just value) (RunDelta.index position vector)
    assertEqual (label ++ " checkpoint value") (Just checkpointItem)
      (RunDelta.checkpointValue position vector)
    let containing = List.find (RunDelta.dirtyRunContains position) runs
    assertEqual (label ++ " dirtiness") (Just (isJust containing)) (RunDelta.isDirty position vector)
    assertEqual (label ++ " containing run") containing (RunDelta.dirtyRunContaining position vector)
  assertEqual (label ++ " dirty count") (sum (map RunDelta.dirtyRunLength runs))
    (RunDelta.dirtyCount vector)
  assertEqual (label ++ " run count") (length runs) (RunDelta.dirtyRunCount vector)
  assertEqual (label ++ " runs") runs (RunDelta.dirtyRuns vector)
  forM_ (zip [0 ..] runs) $ \(rank, descriptor) ->
    assertEqual (label ++ " run by rank") (Just descriptor) (RunDelta.dirtyRunAt rank vector)
  assertEqual (label ++ " past-end rank") Nothing (RunDelta.dirtyRunAt (length runs) vector)
  () <$ assertValid label vector

assertSameObservableState
  :: String
  -> RunDelta.PersistentRunDeltaVector Int
  -> RunDelta.PersistentRunDeltaVector Int
  -> IO ()
assertSameObservableState label expected actual = do
  assertEqual (label ++ " count") (RunDelta.count expected) (RunDelta.count actual)
  assertEqual (label ++ " values") (RunDelta.toList expected) (RunDelta.toList actual)
  assertEqual (label ++ " dirty count") (RunDelta.dirtyCount expected) (RunDelta.dirtyCount actual)
  assertEqual (label ++ " run count") (RunDelta.dirtyRunCount expected)
    (RunDelta.dirtyRunCount actual)
  assertEqual (label ++ " runs") (RunDelta.dirtyRuns expected) (RunDelta.dirtyRuns actual)
  forM_ [0 .. RunDelta.count expected - 1] $ \position -> do
    assertEqual (label ++ " checkpoint value") (RunDelta.checkpointValue position expected)
      (RunDelta.checkpointValue position actual)
    assertEqual (label ++ " dirtiness") (RunDelta.isDirty position expected)
      (RunDelta.isDirty position actual)
  () <$ assertValid label actual

assertValid
  :: String -> RunDelta.PersistentRunDeltaVector a -> IO RunDelta.RunDeltaStatistics
assertValid label vector =
  case RunDelta.validateStructure vector of
    Left message -> failure (label ++ ": structural validation failed: " ++ message)
    Right statistics -> do
      assertEqual (label ++ " validated count") (RunDelta.count vector)
        (RunDelta.statisticsCount statistics)
      assertEqual (label ++ " validated dirty count") (RunDelta.dirtyCount vector)
        (RunDelta.statisticsDirtyCount statistics)
      assertEqual (label ++ " validated run count") (RunDelta.dirtyRunCount vector)
        (RunDelta.statisticsDirtyRunCount statistics)
      assertEqual (label ++ " validated clean representatives")
        (RunDelta.count vector - RunDelta.dirtyCount vector)
        (RunDelta.statisticsCleanRepresentativeCount statistics)
      pure statistics

assertSharesAllRoots
  :: String
  -> RunDelta.PersistentRunDeltaVector a
  -> RunDelta.PersistentRunDeltaVector a
  -> IO ()
assertSharesAllRoots label left right = do
  sharesCurrent <- RunDelta.sharesCurrentRootWith left right
  sharesCheckpoint <- RunDelta.sharesCheckpointRootWith left right
  assertBool (label ++ ": current root") sharesCurrent
  assertBool (label ++ ": checkpoint root") sharesCheckpoint

assertSharesRepresentative
  :: String
  -> Int
  -> RunDelta.PersistentRunDeltaVector a
  -> RunDelta.PersistentRunDeltaVector a
  -> IO ()
assertSharesRepresentative label position left right =
  RunDelta.sharesCurrentRepresentativeWith position left right >>= assertBool label

assertCheckpointRepresentative
  :: String -> Int -> RunDelta.PersistentRunDeltaVector a -> IO ()
assertCheckpointRepresentative label position vector =
  RunDelta.sharesCheckpointRepresentativeAt position vector
    >>= assertBool (label ++ " at " ++ show position)

nextRandom :: Word32 -> Word32
nextRandom value = value * 1_664_525 + 1_013_904_223

boundedRandom :: Word32 -> Int -> Int
boundedRandom _ bound | bound <= 0 = 0
boundedRandom value bound = fromIntegral (value `mod` fromIntegral bound)

expectJust :: String -> Maybe a -> IO a
expectJust _ (Just value) = pure value
expectJust label Nothing = failure (label ++ ": expected Just")

expectPure :: String -> Maybe a -> a
expectPure _ (Just value) = value
expectPure label Nothing =
  error ("PersistentRunDeltaVectorTests: " ++ label ++ ": expected Just")

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = failure (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = failure (label ++ ": expected true")

failure :: String -> IO a
failure message = error ("PersistentRunDeltaVectorTests: " ++ message)
