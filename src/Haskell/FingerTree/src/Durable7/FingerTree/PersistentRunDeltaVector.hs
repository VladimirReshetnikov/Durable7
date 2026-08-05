{-# LANGUAGE BangPatterns #-}

-- | A fixed-length persistent vector carrying a checkpoint and an exact maximal-run index of the
-- positions whose current values differ from that checkpoint.
--
-- Two logical views of one fixed-length sequence are held as immutable RRB vectors -- the current
-- values and the checkpoint -- beside a persistent ordered map holding one @start -> endExclusive@
-- record for every /maximal/ run of differing positions. Dirtiness is relative to a retained
-- 'EqualityPolicy' rather than to write history: writing a policy-equal value is a semantic no-op,
-- and returning a position to its checkpoint equality class cancels that position's delta and
-- restores the exact checkpoint representative.
--
-- Let @n@ be the fixed length, @k@ the number of dirty positions, and @r@ the number of maximal
-- dirty runs, so @0 <= r <= k <= n@. Against a bare pair of unindexed roots the one strict result
-- is that exact dirty-run descriptor discovery is output-optimal @Theta(r)@ rather than worst-case
-- @Theta(n)@; the gap is unbounded, because one contiguous changed block gives @k = n@ and
-- @r = 1@. Emitting the changed payload /values/ is still @Omega(k)@ and is not claimed to be
-- faster. The run index is an application of the Discrete Interval Encoding Tree idea, not a new
-- interval algorithm.
--
-- Shipped bounds:
--
-- * build @n@ values -- @Theta(n)@;
-- * read a current or checkpoint value -- @O(log n)@;
-- * persistent point edit -- @O(log n)@;
-- * fork by retaining a version, whole checkpoint, whole rollback -- @O(1)@;
-- * enumerate current values -- @Theta(n)@;
-- * dirty membership, select a run by rank, locate the run containing a position -- @O(log (r+2))@;
-- * exact dirty-run descriptors -- @Theta(r)@;
-- * accept or revert one indexed run -- @O(log n)@, independent of that run's length, and with no
--   equality-policy call at all.
--
-- Accepting or reverting a run splices structurally, by four 'Durable7.FingerTree.RrbVector.splitAt'
-- boundaries and two 'Durable7.FingerTree.RrbVector.append' spine joins, never by walking that
-- run's positions; that is what makes it independent of the run's length. Length is fixed at
-- construction: there is deliberately no insertion, deletion, concatenation, or merge of unrelated
-- branches, because checkpoint correspondence would need a position-transformation policy this
-- abstract data type does not define. There is one checkpoint per version.
--
-- == Reference identity
--
-- The cleanliness invariant is /a clean position reuses its exact checkpoint representative/, not
-- merely an equal value: the retained policy may be coarser or finer than any natural equality, so
-- it cannot decide representative identity. The C# baseline uses a private cell class with
-- @ReferenceEquals@, Rust uses @Arc::ptr_eq@, and C uses cell addresses. Here each payload is boxed
-- in an internal cell stamped with a globally unique token drawn from an atomic counter -- the same
-- device @Data.Unique@ uses -- so \"the same representative\" is a decidable, pure test. Cancelling
-- a write physically stores the checkpoint's own cell into the current root, so the restored value
-- is the identical heap object and not a reconstructed equal. 'validateStructure' audits that
-- sharing purely through the tokens; 'sharesCheckpointRepresentativeAt' and
-- 'sharesCurrentRepresentativeWith' confirm the same fact independently through
-- 'System.Mem.StableName.StableName', so the token discipline is never taken on trust.
--
-- Root-level canonicalization -- that a clean version's current root /is/ its checkpoint root
-- rather than an equal tree -- is a physical-identity property no pure function can observe;
-- 'sharesCurrentRootWith' and 'sharesCheckpointRootWith' expose it for tests, while
-- 'validateStructure' checks the per-position consequence.
--
-- Every version is immutable and every producing operation leaves its receiver usable, so any
-- retained version can be branched again. Both policy calls of an edit happen before the successor
-- is built, so a diverging policy publishes no partial version. Concurrent reads and independent
-- branch derivation are safe when the retained policy is safe for concurrent calls and stays one
-- stable equivalence relation for as long as any version retaining it is alive.
module Durable7.FingerTree.PersistentRunDeltaVector
  ( -- * Equality policy
    EqualityPolicy(..)
  , naturalEquality
  , reflexiveIeeeEquality
    -- * Run descriptors
  , DirtyRun(..)
  , dirtyRunEndExclusive
  , dirtyRunContains
    -- * Construction
  , PersistentRunDeltaVector
  , emptyWith
  , fromListWith
    -- * Queries
  , toList
  , count
  , null
  , hasChanges
  , dirtyCount
  , dirtyRunCount
  , valueEquality
  , index
  , checkpointValue
  , isDirty
  , dirtyRunContaining
  , dirtyRunAt
  , dirtyRuns
    -- * Editing
  , setAt
  , resetAt
  , checkpoint
  , rollback
  , acceptDirtyRunAt
  , revertDirtyRunAt
  , acceptDirtyRunContaining
  , revertDirtyRunContaining
    -- * Diagnostics
  , RunDeltaStatistics(..)
  , validateStructure
  , sharesCurrentRootWith
  , sharesCheckpointRootWith
  , sharesCurrentRepresentativeWith
  , sharesCheckpointRepresentativeAt
  ) where

import Prelude hiding (null)

import Control.Exception (evaluate)
import Data.IORef (IORef, atomicModifyIORef', newIORef)
import System.IO.Unsafe (unsafePerformIO)
import System.Mem.StableName (eqStableName, makeStableName)

import qualified Durable7.FingerTree.RrbVector as Rrb
import qualified Durable7.FingerTree.SortedMap as SortedMap

-- | Value equality as a retained record of functions rather than a class constraint.
--
-- The rule is load-bearing rather than incidental: it decides which writes are semantic no-ops and
-- when a recorded change cancels against its checkpoint, so it determines a version's observable
-- change set. It is supplied once at construction and carried by every derived version.
--
-- The caller must supply a true equivalence relation -- reflexive, symmetric, and transitive --
-- that stays stable for as long as any version retaining it is alive, and must not mutate state the
-- relation observes in a value a version still holds. Reflexivity is the trap inherited from the
-- baselines: .NET's default comparer treats @NaN@ as equal to itself, while a naive @(==)@ on
-- 'Double' does not, and a non-reflexive relation silently corrupts run accounting by recording a
-- rewrite of an identical value as a change and leaving a phantom run that no invariant check can
-- detect. Use 'reflexiveIeeeEquality' for floating payloads.
newtype EqualityPolicy a = EqualityPolicy
  { policyEquals :: a -> a -> Bool
  }

-- | The natural equivalence of an 'Eq' payload. O(1) plus the cost of one @(==)@.
--
-- Sound only when the instance really is reflexive, which rules out raw 'Float' and 'Double'.
naturalEquality :: Eq a => EqualityPolicy a
naturalEquality = EqualityPolicy (==)

-- | The canonical reflexive IEEE-754 equivalence for binary floating-point payloads. O(1).
--
-- Adds the reflexivity @(==)@ lacks and resolves signed zeros the way .NET's default comparer does:
-- every @NaN@ is equivalent to every other @NaN@, and @-0.0@ stays equivalent to @+0.0@. This is
-- the documented exit for float payloads, mirroring Rust's @EqualityPolicy::reflexive_ieee@.
reflexiveIeeeEquality :: RealFloat a => EqualityPolicy a
reflexiveIeeeEquality =
  EqualityPolicy (\left right -> left == right || (isNaN left && isNaN right))

-- | One maximal half-open run of checkpoint-relative changes, covering
-- @dirtyRunStart .. dirtyRunStart + dirtyRunLength@ and never empty.
--
-- The runs published by one version are ordered, non-overlapping, non-adjacent, and maximal.
data DirtyRun = DirtyRun
  { dirtyRunStart :: !Int
    -- ^ The zero-based first changed position.
  , dirtyRunLength :: !Int
    -- ^ The number of consecutive changed positions.
  }
  deriving (Eq, Ord, Show)

-- | The exclusive end position of a run. O(1).
dirtyRunEndExclusive :: DirtyRun -> Int
dirtyRunEndExclusive run = dirtyRunStart run + dirtyRunLength run

-- | Whether a zero-based position lies inside a run. O(1).
dirtyRunContains :: Int -> DirtyRun -> Bool
dirtyRunContains position run =
  position >= dirtyRunStart run && position < dirtyRunEndExclusive run

-- | One element slot, identified by token rather than by value.
--
-- The token is what makes \"the same representative\" decidable without asking the payload's
-- equality, and it is what turns 'Durable7.FingerTree.RrbVector.setAt'\'s equal-replacement short
-- circuit into an identity test, which is exactly the test wanted there.
data Cell a = Cell !Int a

-- | Identity, not value: two cells are equal exactly when they are the same representative.
instance Eq (Cell a) where
  Cell left _ == Cell right _ = left == right

cellValue :: Cell a -> a
cellValue (Cell _ value) = value

cellCounter :: IORef Int
cellCounter = unsafePerformIO (newIORef 0)
{-# NOINLINE cellCounter #-}

newCell :: a -> Cell a
newCell value =
  unsafePerformIO
    (do
      token <- atomicModifyIORef' cellCounter (\next -> (next + 1, next))
      pure (Cell token value))
{-# NOINLINE newCell #-}

-- | A fixed-length persistent vector with a checkpoint and an exact maximal-run change index.
--
-- See the module documentation for the representation, the shipped bounds, and the deliberate
-- limits of this abstract data type.
data PersistentRunDeltaVector a = PersistentRunDeltaVector
  { vectorEquality :: !(EqualityPolicy a)
  , vectorCurrent :: !(Rrb.RrbVector (Cell a))
  , vectorCheckpoint :: !(Rrb.RrbVector (Cell a))
  , vectorRuns :: !(SortedMap.SortedMap Int Int)
  , vectorDirtyCount :: !Int
  }

-- | Independently recomputed metadata returned by 'validateStructure'.
data RunDeltaStatistics = RunDeltaStatistics
  { statisticsCount :: !Int
    -- ^ The fixed number of positions.
  , statisticsDirtyCount :: !Int
    -- ^ The number of differing positions, recounted from the run index.
  , statisticsDirtyRunCount :: !Int
    -- ^ The number of maximal dirty runs.
  , statisticsCleanRepresentativeCount :: !Int
    -- ^ The number of clean positions confirmed to reuse their exact checkpoint representative.
  , statisticsMaximumRunLength :: !Int
    -- ^ The longest recomputed run, or zero when the version is clean.
  }
  deriving (Eq, Show)

-- | The clean empty vector retaining the supplied equality policy. O(1).
emptyWith :: EqualityPolicy a -> PersistentRunDeltaVector a
emptyWith policy = cleanVector policy Rrb.empty

-- | A clean fixed-length vector built from one pass over a list, retaining the supplied policy.
--
-- @Theta(n)@. Both roots start as the same root, so the vector begins with no dirty runs.
fromListWith :: EqualityPolicy a -> [a] -> PersistentRunDeltaVector a
fromListWith policy values = cleanVector policy (Rrb.fromList (map newCell values))

-- | The current values, in positional order. @Theta(n)@.
toList :: PersistentRunDeltaVector a -> [a]
toList vector = map cellValue (Rrb.toList (vectorCurrent vector))

-- | The fixed number of positions. O(1).
count :: PersistentRunDeltaVector a -> Int
count vector = Rrb.count (vectorCurrent vector)

-- | Whether the vector has no positions. O(1).
null :: PersistentRunDeltaVector a -> Bool
null vector = Rrb.null (vectorCurrent vector)

-- | Whether at least one current value differs from its checkpoint value. O(1).
hasChanges :: PersistentRunDeltaVector a -> Bool
hasChanges vector = vectorDirtyCount vector /= 0

-- | The number of positions that differ from the checkpoint. O(1).
dirtyCount :: PersistentRunDeltaVector a -> Int
dirtyCount = vectorDirtyCount

-- | The number of maximal dirty runs. O(1).
dirtyRunCount :: PersistentRunDeltaVector a -> Int
dirtyRunCount vector = SortedMap.count (vectorRuns vector)

-- | The retained policy defining checkpoint-relative value equality. O(1).
valueEquality :: PersistentRunDeltaVector a -> EqualityPolicy a
valueEquality = vectorEquality

-- | The current value at a zero-based position, or 'Nothing' outside the vector. @O(log n)@.
index :: Int -> PersistentRunDeltaVector a -> Maybe a
index position vector = cellValue <$> Rrb.index position (vectorCurrent vector)

-- | The checkpoint value at a zero-based position, or 'Nothing' outside the vector. @O(log n)@.
checkpointValue :: Int -> PersistentRunDeltaVector a -> Maybe a
checkpointValue position vector = cellValue <$> Rrb.index position (vectorCheckpoint vector)

-- | Whether a position differs from its checkpoint value, or 'Nothing' outside the vector.
--
-- @O(log (r + 2))@ over @r@ maximal runs. Use this to tell a clean position from one out of range.
isDirty :: Int -> PersistentRunDeltaVector a -> Maybe Bool
isDirty position vector
  | position < 0 || position >= count vector = Nothing
  | otherwise =
      Just
        (case findDirtyRun position vector of
          Just _ -> True
          Nothing -> False)

-- | The maximal dirty run containing a position, or 'Nothing' when it is clean or out of range.
--
-- @O(log (r + 2))@. Runs are addressable this way as well as by rank.
dirtyRunContaining :: Int -> PersistentRunDeltaVector a -> Maybe DirtyRun
dirtyRunContaining position vector
  | position < 0 || position >= count vector = Nothing
  | otherwise = findDirtyRun position vector

-- | The maximal dirty run at a zero-based ascending-start rank, or 'Nothing' outside the index.
--
-- @O(log (r + 2))@.
dirtyRunAt :: Int -> PersistentRunDeltaVector a -> Maybe DirtyRun
dirtyRunAt rank vector = entryToRun <$> SortedMap.index rank (vectorRuns vector)

-- | The maximal dirty runs, in ascending position order.
--
-- Output-optimal @Theta(r)@: the cost tracks the number of runs, not how many positions they cover.
dirtyRuns :: PersistentRunDeltaVector a -> [DirtyRun]
dirtyRuns vector = map entryToRun (SortedMap.toList (vectorRuns vector))

-- | A version with the current value at a position replaced, or 'Nothing' outside the vector.
--
-- A policy-equal replacement is a semantic no-op returning the receiver, which therefore shares
-- every root and changes no dirty or run accounting. A replacement landing back in the checkpoint's
-- equality class cancels that position's delta and restores the exact checkpoint representative;
-- when the last dirty position clears, the current root snaps back onto the checkpoint root.
-- Otherwise the position becomes or stays dirty and at most two neighbouring run records change.
--
-- @O(log n)@. Both policy calls happen before the successor is built.
setAt :: Int -> a -> PersistentRunDeltaVector a -> Maybe (PersistentRunDeltaVector a)
setAt position value vector =
  case Rrb.index position (vectorCurrent vector) of
    Nothing -> Nothing
    Just currentCell
      | policyEquals policy (cellValue currentCell) value -> Just vector
      | otherwise ->
          let checkpointCell =
                expectCell (Rrb.index position (vectorCheckpoint vector))
              !remainsDirty = not (policyEquals policy (cellValue checkpointCell) value)
              replacement = if remainsDirty then newCell value else checkpointCell
          in Just (replaceCell position replacement remainsDirty vector)
  where
    policy = vectorEquality vector

-- | A version whose current value at a position is reset to its checkpoint value, or 'Nothing'
-- outside the vector.
--
-- An already clean position is a no-op returning the receiver, which shares every root. A dirty
-- position is restored to the checkpoint's exact representative rather than to a fresh equal value,
-- so this needs no reflexivity from the policy to do the right thing. @O(log n)@, one policy call.
resetAt :: Int -> PersistentRunDeltaVector a -> Maybe (PersistentRunDeltaVector a)
resetAt position vector =
  case Rrb.index position (vectorCheckpoint vector) of
    Nothing -> Nothing
    Just checkpointCell ->
      let currentCell = expectCell (Rrb.index position (vectorCurrent vector))
      in if policyEquals (vectorEquality vector) (cellValue currentCell) (cellValue checkpointCell)
           then Just vector
           else Just (replaceCell position checkpointCell False vector)

-- | Accepts every current value as a new checkpoint. O(1) root swap.
--
-- An already clean version returns the receiver.
checkpoint :: PersistentRunDeltaVector a -> PersistentRunDeltaVector a
checkpoint vector
  | not (hasChanges vector) = vector
  | otherwise = cleanVector (vectorEquality vector) (vectorCurrent vector)

-- | Reverts every current value to the checkpoint. O(1) root swap.
--
-- An already clean version returns the receiver.
rollback :: PersistentRunDeltaVector a -> PersistentRunDeltaVector a
rollback vector
  | not (hasChanges vector) = vector
  | otherwise = cleanVector (vectorEquality vector) (vectorCheckpoint vector)

-- | Accepts the maximal dirty run at a rank into the checkpoint, or 'Nothing' outside the index.
--
-- Only the selected run becomes clean; every other run is untouched. Implemented by structural RRB
-- splitting and concatenation, so it costs @O(log n)@ /independently of that run's length/ and
-- makes no equality-policy call at all.
acceptDirtyRunAt :: Int -> PersistentRunDeltaVector a -> Maybe (PersistentRunDeltaVector a)
acceptDirtyRunAt rank vector = acceptRun vector <$> dirtyRunAt rank vector

-- | Reverts the maximal dirty run at a rank to the checkpoint, or 'Nothing' outside the index.
--
-- Only the selected run becomes clean; every other run is untouched. @O(log n)@ independently of
-- that run's length, with no equality-policy call at all.
revertDirtyRunAt :: Int -> PersistentRunDeltaVector a -> Maybe (PersistentRunDeltaVector a)
revertDirtyRunAt rank vector = revertRun vector <$> dirtyRunAt rank vector

-- | Accepts the maximal dirty run /containing a position/, or 'Nothing' outside the vector.
--
-- The argument is a zero-based position, not a run rank, so a descriptor obtained from
-- 'dirtyRunContaining' can be acted on without rediscovering its rank. A clean position is vacuous
-- rather than an error: the receiver comes back, sharing every root and consulting no policy.
-- Locating the run is @O(log (r + 2))@ and the splice is @O(log n)@, independent of its length.
acceptDirtyRunContaining :: Int -> PersistentRunDeltaVector a -> Maybe (PersistentRunDeltaVector a)
acceptDirtyRunContaining position vector
  | position < 0 || position >= count vector = Nothing
  | otherwise = Just (maybe vector (acceptRun vector) (findDirtyRun position vector))

-- | Reverts the maximal dirty run /containing a position/, or 'Nothing' outside the vector.
--
-- The argument is a zero-based position, not a run rank. A clean position is vacuous rather than an
-- error: the receiver comes back, sharing every root and consulting no policy. Locating the run is
-- @O(log (r + 2))@ and the splice is @O(log n)@, independent of its length.
revertDirtyRunContaining :: Int -> PersistentRunDeltaVector a -> Maybe (PersistentRunDeltaVector a)
revertDirtyRunContaining position vector
  | position < 0 || position >= count vector = Nothing
  | otherwise = Just (maybe vector (revertRun vector) (findDirtyRun position vector))

-- | Audits both RRB roots, run ordering, non-adjacency, maximality, the dirty-count sum, and exact
-- representative sharing at every clean position, reporting the first violation.
--
-- The run index is checked against a run encoding recomputed from scratch out of the two roots, so
-- ordering, non-adjacency, and maximality are all confirmed against an independent witness rather
-- than against the maintained index's own shape. @Theta(n)@ policy calls: an auditor, not a
-- hot-path operation. Root-level canonicalization is a physical-identity property that no pure
-- function can observe; see 'sharesCurrentRootWith'.
validateStructure :: PersistentRunDeltaVector a -> Either String RunDeltaStatistics
validateStructure vector = do
  check
    (Rrb.count currentRoot == Rrb.count checkpointRoot)
    "the current and checkpoint roots have different lengths"
  check
    (storedDirty >= 0 && storedDirty <= total)
    "the dirty cardinality is outside the position range"
  check (validRrb currentRoot) "the current RRB root is structurally invalid"
  check (validRrb checkpointRoot) "the checkpoint RRB root is structurally invalid"
  checkRunShape total entries
  check
    (countedDirty == storedDirty)
    "dirty run lengths do not sum to the dirty cardinality"
  check
    (indexedRuns == observedRuns)
    "the run index is not the exact maximal encoding of the differing positions"
  check
    (cleanShared == cleanTotal)
    "a clean position does not reuse its exact checkpoint representative"
  pure
    RunDeltaStatistics
      { statisticsCount = total
      , statisticsDirtyCount = countedDirty
      , statisticsDirtyRunCount = length entries
      , statisticsCleanRepresentativeCount = cleanShared
      , statisticsMaximumRunLength = maximum (0 : map dirtyRunLength observedRuns)
      }
  where
    currentRoot = vectorCurrent vector
    checkpointRoot = vectorCheckpoint vector
    total = Rrb.count currentRoot
    storedDirty = vectorDirtyCount vector
    entries = SortedMap.toList (vectorRuns vector)
    indexedRuns = map entryToRun entries
    countedDirty = sum (map (\(start, endExclusive) -> endExclusive - start) entries)
    pairs = zip (Rrb.toList currentRoot) (Rrb.toList checkpointRoot)
    differs (left, right) =
      not (policyEquals (vectorEquality vector) (cellValue left) (cellValue right))
    flags = map differs pairs
    observedRuns = maximalRuns flags
    cleanTotal = length (filter not flags)
    cleanShared =
      length [() | ((left, right), False) <- zip pairs flags, left == right]

-- | Whether two versions are backed by the same current root. A representation test used to
-- confirm a no-op avoided copying, not an equality test.
sharesCurrentRootWith
  :: PersistentRunDeltaVector a -> PersistentRunDeltaVector a -> IO Bool
sharesCurrentRootWith left right =
  Rrb.sharesRootWith (vectorCurrent left) (vectorCurrent right)

-- | Whether two versions are backed by the same checkpoint root. A representation test, not an
-- equality test.
sharesCheckpointRootWith
  :: PersistentRunDeltaVector a -> PersistentRunDeltaVector a -> IO Bool
sharesCheckpointRootWith left right =
  Rrb.sharesRootWith (vectorCheckpoint left) (vectorCheckpoint right)

-- | Whether two versions hold the identical current representative at a position, by
-- 'System.Mem.StableName.StableName' rather than by value. Out-of-range positions report 'False'.
sharesCurrentRepresentativeWith
  :: Int -> PersistentRunDeltaVector a -> PersistentRunDeltaVector a -> IO Bool
sharesCurrentRepresentativeWith position left right =
  sameObject (Rrb.index position (vectorCurrent left)) (Rrb.index position (vectorCurrent right))

-- | Whether one version's current representative at a position is the identical heap object as its
-- checkpoint representative there, by 'System.Mem.StableName.StableName' rather than by value.
--
-- This is the physical-identity witness behind \"a clean position reuses its exact checkpoint
-- representative\". Out-of-range positions report 'False'.
sharesCheckpointRepresentativeAt :: Int -> PersistentRunDeltaVector a -> IO Bool
sharesCheckpointRepresentativeAt position vector =
  sameObject
    (Rrb.index position (vectorCurrent vector))
    (Rrb.index position (vectorCheckpoint vector))

cleanVector :: EqualityPolicy a -> Rrb.RrbVector (Cell a) -> PersistentRunDeltaVector a
cleanVector policy root = PersistentRunDeltaVector policy root root SortedMap.empty 0

entryToRun :: (Int, Int) -> DirtyRun
entryToRun (start, endExclusive) = DirtyRun start (endExclusive - start)

findDirtyRun :: Int -> PersistentRunDeltaVector a -> Maybe DirtyRun
findDirtyRun position vector =
  case SortedMap.floorEntry position (vectorRuns vector) of
    Just entry@(_, endExclusive) | endExclusive > position -> Just (entryToRun entry)
    _ -> Nothing

replaceCell
  :: Int -> Cell a -> Bool -> PersistentRunDeltaVector a -> PersistentRunDeltaVector a
replaceCell position replacement remainsDirty vector =
  PersistentRunDeltaVector
    (vectorEquality vector)
    nextCurrent
    (vectorCheckpoint vector)
    nextRuns
    nextDirtyCount
  where
    wasDirty =
      case findDirtyRun position vector of
        Just _ -> True
        Nothing -> False
    nextRuns
      | not wasDirty && remainsDirty = addDirtyPosition position (vectorRuns vector)
      | wasDirty && not remainsDirty = removeDirtyPosition position (vectorRuns vector)
      | otherwise = vectorRuns vector
    nextDirtyCount
      | not wasDirty && remainsDirty = vectorDirtyCount vector + 1
      | wasDirty && not remainsDirty = vectorDirtyCount vector - 1
      | otherwise = vectorDirtyCount vector
    spliced =
      case Rrb.setAt position replacement (vectorCurrent vector) of
        Just updated -> updated
        Nothing ->
          error
            "Durable7.FingerTree.PersistentRunDeltaVector: internal validated position rejected"
    nextCurrent
      | SortedMap.null nextRuns = vectorCheckpoint vector
      | otherwise = spliced

-- Records a position as dirty, merging the runs on both sides, extending one side, or inserting a
-- fresh singleton run, so the encoding stays ordered, non-overlapping, non-adjacent, and maximal.
addDirtyPosition :: Int -> SortedMap.SortedMap Int Int -> SortedMap.SortedMap Int Int
addDirtyPosition position runs =
  case (leftStart, rightEntry) of
    (Just start, Just (rightStart, rightEnd)) ->
      SortedMap.insert start rightEnd (SortedMap.delete rightStart runs)
    (Just start, Nothing) -> SortedMap.insert start (position + 1) runs
    (Nothing, Just (rightStart, rightEnd)) ->
      SortedMap.insert position rightEnd (SortedMap.delete rightStart runs)
    (Nothing, Nothing) -> SortedMap.insert position (position + 1) runs
  where
    leftStart =
      case SortedMap.floorEntry position runs of
        Just (start, endExclusive) | endExclusive == position -> Just start
        _ -> Nothing
    rightEntry =
      case SortedMap.ceilingEntry position runs of
        Just (start, endExclusive) | start == position + 1 -> Just (start, endExclusive)
        _ -> Nothing

-- Clears a position, deleting, shrinking at either edge, or splitting its unique containing run.
removeDirtyPosition :: Int -> SortedMap.SortedMap Int Int -> SortedMap.SortedMap Int Int
removeDirtyPosition position runs =
  case SortedMap.floorEntry position runs of
    Just (start, endExclusive)
      | endExclusive > position ->
          case (start == position, endExclusive == position + 1) of
            (True, True) -> SortedMap.delete start runs
            (True, False) ->
              SortedMap.insert (position + 1) endExclusive (SortedMap.delete start runs)
            (False, True) -> SortedMap.insert start position runs
            (False, False) ->
              SortedMap.insert (position + 1) endExclusive (SortedMap.insert start position runs)
    _ ->
      error
        "Durable7.FingerTree.PersistentRunDeltaVector: cleared position absent from the run index"

acceptRun :: PersistentRunDeltaVector a -> DirtyRun -> PersistentRunDeltaVector a
acceptRun vector run
  | dirtyRunCount vector == 1 = checkpoint vector
  | otherwise =
      PersistentRunDeltaVector
        (vectorEquality vector)
        (vectorCurrent vector)
        (replaceRange (vectorCheckpoint vector) (vectorCurrent vector) run)
        (SortedMap.delete (dirtyRunStart run) (vectorRuns vector))
        (vectorDirtyCount vector - dirtyRunLength run)

revertRun :: PersistentRunDeltaVector a -> DirtyRun -> PersistentRunDeltaVector a
revertRun vector run
  | dirtyRunCount vector == 1 = rollback vector
  | otherwise =
      PersistentRunDeltaVector
        (vectorEquality vector)
        (replaceRange (vectorCurrent vector) (vectorCheckpoint vector) run)
        (vectorCheckpoint vector)
        (SortedMap.delete (dirtyRunStart run) (vectorRuns vector))
        (vectorDirtyCount vector - dirtyRunLength run)

-- Splices one run's slice of the source over the destination, sharing every untouched subtree.
-- Four splits and two boundary-spine joins, so O(log n) regardless of the run's length.
replaceRange
  :: Rrb.RrbVector (Cell a) -> Rrb.RrbVector (Cell a) -> DirtyRun -> Rrb.RrbVector (Cell a)
replaceRange destination source run
  | start == 0 && amount == Rrb.count destination = source
  | otherwise = Rrb.append (Rrb.append left middle) right
  where
    start = dirtyRunStart run
    amount = dirtyRunLength run
    (left, destinationTail) = expectSplit (Rrb.splitAt start destination)
    (_, right) = expectSplit (Rrb.splitAt amount destinationTail)
    (_, sourceTail) = expectSplit (Rrb.splitAt start source)
    (middle, _) = expectSplit (Rrb.splitAt amount sourceTail)

maximalRuns :: [Bool] -> [DirtyRun]
maximalRuns = go 0
  where
    go _ [] = []
    go !position (False : rest) = go (position + 1) rest
    go !position values =
      let amount = length (takeWhile id values)
      in DirtyRun position amount : go (position + amount) (drop amount values)

checkRunShape :: Int -> [(Int, Int)] -> Either String ()
checkRunShape total = go (-1)
  where
    go _ [] = Right ()
    go previousEnd ((start, endExclusive) : rest)
      | start < 0 || start >= endExclusive || endExclusive > total || start <= previousEnd =
          Left "dirty runs are empty, out of range, overlapping, adjacent, or unordered"
      | otherwise = go endExclusive rest

check :: Bool -> String -> Either String ()
check True _ = Right ()
check False message = Left message

validRrb :: Rrb.RrbVector a -> Bool
validRrb root =
  case Rrb.validateStructure root of
    Just _ -> True
    Nothing -> False

sameObject :: Maybe (Cell a) -> Maybe (Cell a) -> IO Bool
sameObject (Just left) (Just right) = do
  leftValue <- evaluate left
  rightValue <- evaluate right
  leftName <- makeStableName leftValue
  rightName <- makeStableName rightValue
  pure (leftName `eqStableName` rightName)
sameObject _ _ = pure False

expectCell :: Maybe (Cell a) -> Cell a
expectCell (Just value) = value
expectCell Nothing =
  error "Durable7.FingerTree.PersistentRunDeltaVector: internal root length disagreement"

expectSplit :: Maybe (Rrb.RrbVector a, Rrb.RrbVector a) -> (Rrb.RrbVector a, Rrb.RrbVector a)
expectSplit (Just value) = value
expectSplit Nothing =
  error "Durable7.FingerTree.PersistentRunDeltaVector: internal run boundary rejected"
