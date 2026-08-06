{-# LANGUAGE BangPatterns #-}

-- | A persistent sorted map with an explicit checkpoint and a coalesced ordered index of the
-- exact net changes from that checkpoint.
--
-- A version stores three immutable roots: the checkpoint state, the current state, and a change
-- index holding exactly one @(before, after)@ record for every key class on which the two states
-- differ.  The first effective write to a class captures its checkpoint-relative @before@; later
-- writes replace only @after@; returning a class to its checkpoint state removes its record
-- entirely.  When the change index becomes empty the current root snaps back to the exact
-- checkpoint root, so a wholly cancelled epoch is storage-clean as well as extensionally clean and
-- 'checkpoint' and 'rollback' are O(1) identity operations.
--
-- Let @N = max(2, |dom(checkpoint) union dom(current)|)@, counting key-policy equivalence classes
-- rather than concrete keys, and let @k@ be the number of net-changed classes.  Point lookup,
-- point update and removal, rank, and neighbor queries are O(log N).  An effective point edit costs
-- @O(log N + log(k + 1)) = O(log N)@ because it touches the current state and at most one change
-- record.  'checkpoint' and 'rollback' are O(1), and 'lookupChange' is O(log(k + 1)).  Fully
-- consuming 'changes' is Theta(k + 1), which is output-optimal and is the point of the design: it
-- avoids the Theta(k log(N\/k + 1)) adversarial divergent-path walk that a reference-pruned
-- comparison between two path-copied balanced trees must perform.  A live version occupies
-- O(N + k) nodes; retaining every successor adds O(log N) nodes per changed point update, the same
-- asymptotic persistence cost as an ordinary path-copied ordered map.
--
-- Key identity and enumeration order come from the retained 'DeltaMapPolicy'; two keys name the
-- same class exactly when 'policyCompareKeys' reports 'EQ'.  Semantic no-ops and change
-- cancellation come from 'policyValuesEqual'.  \"Exact\" therefore means exact /extensionally/
-- under those two callbacks, not by identity of the retained key or value representatives.  A
-- baseline key representative is retained across point updates and delete\/re-add round trips; a
-- newly added class keeps its first representative while its net-added record is active, and a
-- later addition after complete cancellation begins a new representative episode.  The key
-- callback must define a stable total preorder and the value callback a stable equivalence
-- relation.
--
-- Because a record of functions has no identity, a policy cannot be compared at run time.  Every
-- version reachable from one root carries that root's policy, and each edit propagates it, so a
-- single lineage is always internally consistent.  Mixing lineages is a caller obligation: the
-- two-argument diagnostics 'sharesRootWith', 'sharesCheckpointWith', and 'sharesStorageWith' do
-- not verify that their operands were built with extensionally identical policies, and comparing
-- versions whose policies disagree is meaningless rather than detected.  This mirrors the
-- precondition 'Durable7.FingerTree.RangeUpdateSequence.append' places on its algebra records.
--
-- Enumeration is lazy.  'changes', 'changesInRange', 'entriesInRange', and 'toList' produce their
-- elements on demand and invoke no key or value callback beyond the ones documented per function,
-- so abandoning an enumeration after one element costs only what that element cost.  Policy
-- failures cannot publish a partial successor: every operation is pure and the receiver, together
-- with every retained branch, is unaffected by a callback that diverges or raises.
--
-- The mutation surface is deliberately point-only.  An eager bulk clear would produce Theta(N)
-- removal records and cannot simultaneously retain an ordinary map's O(1) clear bound, so callers
-- start a fresh epoch with 'emptyWith' when they do not need an enumerable delta, and remove
-- entries explicitly when they do.  'setItems' is not an exception: it is exactly the left fold of
-- 'setItem', a convenience and an allocation saving that claims no better bound than the
-- @O(m log N)@ sequence of point writes it replaces.
module Durable7.FingerTree.PersistentDeltaMap
  ( DeltaMapPolicy(..)
  , PersistentDeltaMap
  , PersistentMapChange(..)
  , PersistentMapChangeKind(..)
  , ValidationStatistics(..)
  , naturalPolicy
  , emptyWith
  , fromListWith
  , policy
  , count
  , null
  , member
  , lookup
  , lookupEntry
  , toList
  , keys
  , elems
  , minEntry
  , maxEntry
  , floorEntry
  , ceilingEntry
  , lowerEntry
  , higherEntry
  , entryAt
  , indexOfKey
  , entriesInRange
  , checkpointCount
  , checkpointLookup
  , checkpointToList
  , changeCount
  , hasChanges
  , changeKind
  , lookupChange
  , changes
  , changesInRange
  , setItem
  , setItems
  , remove
  , checkpoint
  , rollback
  , validateStructure
  , sharesRootWith
  , sharesCheckpointWith
  , sharesStorageWith
  ) where

import Prelude hiding (lookup, null)

import Control.Exception (evaluate)
import qualified Data.List as List
import System.Mem.StableName (eqStableName, makeStableName)

import Durable7.FingerTree.SortedMap (SortedMap)
import qualified Durable7.FingerTree.SortedMap as SortedMap

-- | Key order and value equality retained by one map version.
--
-- The house convention is a record of functions rather than a type class, so one key type may be
-- ordered differently by different versions and one value type may define different no-ops.
-- 'policyCompareKeys' must be a stable total preorder and 'policyValuesEqual' a stable reflexive,
-- symmetric, transitive equivalence; in particular a comparison that is not reflexive, such as
-- IEEE equality over @NaN@, would record a change that never cancels.
data DeltaMapPolicy key value = DeltaMapPolicy
  { policyCompareKeys :: key -> key -> Ordering
    -- ^ Decides key classes and ascending enumeration order.
  , policyValuesEqual :: value -> value -> Bool
    -- ^ Decides which writes are semantic no-ops and when a recorded change cancels.
  }

-- | A stored key paired with the retained key order that decides its class.
--
-- The shared 'SortedMap' substrate takes its order from 'Ord', while this structure takes it from
-- a retained policy value.  Wrapping every stored key with the policy's comparison bridges the two
-- without copying the map: the wrapper's 'Ord' delegates to the callback, so one map instance is
-- ordered by exactly one comparison.  The instance is lawful only among keys wrapped by the same
-- policy, which is why the wrapper never escapes this module.
data OrderedKey key = OrderedKey
  { orderedKeyValue :: key
  , orderedKeyCompare :: key -> key -> Ordering
  }

instance Eq (OrderedKey key) where
  left == right = compare left right == EQ

instance Ord (OrderedKey key) where
  compare left right = orderedKeyCompare left (orderedKeyValue left) (orderedKeyValue right)

-- | One @(before, after)@ endpoint pair stored in the change index.
--
-- Presence is carried by 'Maybe', so an absent endpoint stays distinct from a present value that
-- happens to be a default, an empty container, or 'Nothing' itself.
data DeltaRecord value = DeltaRecord
  { recordBefore :: !(Maybe value)
  , recordAfter :: !(Maybe value)
  }

-- | An immutable, fully persistent sorted map with an explicit checkpoint and a coalesced ordered
-- index of the exact net changes from that checkpoint.
--
-- Every operation returns a new version and leaves the receiver, and every other retained version,
-- valid and unchanged.  A semantic no-op returns the receiver itself, so it shares all three
-- roots.
data PersistentDeltaMap key value = PersistentDeltaMap
  { deltaPolicy :: !(DeltaMapPolicy key value)
  , deltaCurrent :: !(SortedMap (OrderedKey key) value)
  , deltaCheckpoint :: !(SortedMap (OrderedKey key) value)
  , deltaChanges :: !(SortedMap (OrderedKey key) (DeltaRecord value))
  }

-- | Classifies one net key change relative to a version's checkpoint.
data PersistentMapChangeKind
  = Added
    -- ^ The class is absent at the checkpoint and present in the current map.
  | Removed
    -- ^ The class is present at the checkpoint and absent from the current map.
  | Updated
    -- ^ The class is present at both endpoints with non-equivalent values.
  deriving (Eq, Ord, Show)

-- | One exact net change between a version's checkpoint and its current state.
data PersistentMapChange key value = PersistentMapChange
  { changeKey :: key
    -- ^ The checkpoint representative when the class was present at the checkpoint; otherwise the
    -- current addition episode's first representative.
  , changeBefore :: !(Maybe value)
    -- ^ The checkpoint endpoint, or 'Nothing' when the class was absent at the checkpoint.
  , changeAfter :: !(Maybe value)
    -- ^ The current endpoint, or 'Nothing' when the class is absent from the current map.
  }
  deriving (Eq, Show)

-- | Independently recomputed representation statistics returned by 'validateStructure'.
data ValidationStatistics = ValidationStatistics
  { validationCount :: !Int
    -- ^ Number of current entries.
  , validationCheckpointCount :: !Int
    -- ^ Number of checkpoint entries.
  , validationChangeCount :: !Int
    -- ^ Number of exact net-changed key classes.
  , validationAddedCount :: !Int
    -- ^ Number of records absent at the checkpoint and present now.
  , validationRemovedCount :: !Int
    -- ^ Number of records present at the checkpoint and absent now.
  , validationUpdatedCount :: !Int
    -- ^ Number of records present at both endpoints with non-equivalent values.
  }
  deriving (Eq, Show)

-- | The policy built from a key type's natural order and a value type's natural equality. O(1).
--
-- This is the analogue of the baseline's default comparer pair.  It uses type classes only to
-- /build/ a policy value; the collection itself never takes a class constraint, so a caller may
-- always supply a different comparison instead.
naturalPolicy :: (Ord key, Eq value) => DeltaMapPolicy key value
naturalPolicy = DeltaMapPolicy compare (==)

-- | The empty map whose current state is also its checkpoint. O(1).
--
-- This is also the O(1) way to start an unrelated clean epoch.  It is not a clear that retains the
-- old checkpoint, which the point-only mutation surface deliberately omits.
emptyWith :: DeltaMapPolicy key value -> PersistentDeltaMap key value
emptyWith mapPolicy = PersistentDeltaMap mapPolicy state state SortedMap.empty
  where
    state = SortedMap.empty

-- | A clean checkpoint holding the given entries, with the last equivalent key winning.
-- @O(m log m)@ for @m@ supplied entries.
--
-- The last entry of a key class supplies both the retained representative and the value, matching
-- the baseline's range constructor.  The result has no changes, so it shares its current and
-- checkpoint roots.
fromListWith :: DeltaMapPolicy key value -> [(key, value)] -> PersistentDeltaMap key value
fromListWith mapPolicy entries = PersistentDeltaMap mapPolicy state state SortedMap.empty
  where
    state = foldlStrict insertEntry SortedMap.empty entries
    insertEntry stored (key, value) = SortedMap.insert (wrapKey mapPolicy key) value stored

-- | The retained key-order and value-equality policy. O(1).
policy :: PersistentDeltaMap key value -> DeltaMapPolicy key value
policy = deltaPolicy

-- | The number of current entries. O(1).
count :: PersistentDeltaMap key value -> Int
count = SortedMap.count . deltaCurrent

-- | Whether the current map holds no entries. O(1).
null :: PersistentDeltaMap key value -> Bool
null = SortedMap.null . deltaCurrent

-- | Whether a current entry exists for the key's class. O(log N).
member :: key -> PersistentDeltaMap key value -> Bool
member key deltaMap = SortedMap.member (probeKey key deltaMap) (deltaCurrent deltaMap)

-- | The current value stored for the key's class, or 'Nothing' when absent. O(log N).
lookup :: key -> PersistentDeltaMap key value -> Maybe value
lookup key deltaMap = SortedMap.lookup (probeKey key deltaMap) (deltaCurrent deltaMap)

-- | The retained current representative and value for the key's class. O(log N).
--
-- The returned key is the stored representative, which need not be the probe even though the two
-- name the same class.
lookupEntry :: key -> PersistentDeltaMap key value -> Maybe (key, value)
lookupEntry key deltaMap =
  fmap unwrapEntry (lookupStoredEntry (deltaCurrent deltaMap) (probeKey key deltaMap))

-- | The current entries in ascending key order. Theta(1) per element, Theta(n + 1) fully consumed.
toList :: PersistentDeltaMap key value -> [(key, value)]
toList = map unwrapEntry . SortedMap.toList . deltaCurrent

-- | The current keys in ascending order. Theta(1) per element.
keys :: PersistentDeltaMap key value -> [key]
keys = map fst . toList

-- | The current values in ascending key order. Theta(1) per element.
elems :: PersistentDeltaMap key value -> [value]
elems = map snd . toList

-- | The least current entry by key, or 'Nothing' when empty. O(1): the finger-tree 'SortedMap'
-- substrate reads its extremes from the root digits without a descent, matching the baseline's
-- cached extreme.
minEntry :: PersistentDeltaMap key value -> Maybe (key, value)
minEntry = fmap unwrapEntry . SortedMap.minEntry . deltaCurrent

-- | The greatest current entry by key, or 'Nothing' when empty. O(1), as 'minEntry'.
maxEntry :: PersistentDeltaMap key value -> Maybe (key, value)
maxEntry = fmap unwrapEntry . SortedMap.maxEntry . deltaCurrent

-- | The greatest current entry whose key is at most the probe. O(log N).
floorEntry :: key -> PersistentDeltaMap key value -> Maybe (key, value)
floorEntry key deltaMap =
  fmap unwrapEntry (SortedMap.floorEntry (probeKey key deltaMap) (deltaCurrent deltaMap))

-- | The least current entry whose key is at least the probe. O(log N).
ceilingEntry :: key -> PersistentDeltaMap key value -> Maybe (key, value)
ceilingEntry key deltaMap =
  fmap unwrapEntry (SortedMap.ceilingEntry (probeKey key deltaMap) (deltaCurrent deltaMap))

-- | The greatest current entry whose key is strictly below the probe. O(log N).
lowerEntry :: key -> PersistentDeltaMap key value -> Maybe (key, value)
lowerEntry key deltaMap =
  fmap unwrapEntry (SortedMap.lowerEntry (probeKey key deltaMap) (deltaCurrent deltaMap))

-- | The least current entry whose key is strictly above the probe. O(log N).
higherEntry :: key -> PersistentDeltaMap key value -> Maybe (key, value)
higherEntry key deltaMap =
  fmap unwrapEntry (SortedMap.higherEntry (probeKey key deltaMap) (deltaCurrent deltaMap))

-- | The current entry at a zero-based key rank, or 'Nothing' when out of range. O(log N).
entryAt :: Int -> PersistentDeltaMap key value -> Maybe (key, value)
entryAt position deltaMap = fmap unwrapEntry (SortedMap.index position (deltaCurrent deltaMap))

-- | The zero-based rank of the key's class in the current map, or 'Nothing' when absent.
-- O(log N).
indexOfKey :: key -> PersistentDeltaMap key value -> Maybe Int
indexOfKey key deltaMap = SortedMap.indexOfKey (probeKey key deltaMap) (deltaCurrent deltaMap)

-- | The current entries whose keys lie in the inclusive range @[low, high]@, in ascending key
-- order.  An inverted range yields no entries.
--
-- This is an ordered range restriction plus a lazy ascending walk, never a filter over the whole
-- map, so the cost is output-sensitive: O(log N) to restrict to the two boundaries and Theta(1)
-- amortized per yielded entry.
entriesInRange :: key -> key -> PersistentDeltaMap key value -> [(key, value)]
entriesInRange low high deltaMap =
  map unwrapEntry (storedRange (deltaPolicy deltaMap) (deltaCurrent deltaMap) low high)

-- | The number of checkpoint entries. O(1).
checkpointCount :: PersistentDeltaMap key value -> Int
checkpointCount = SortedMap.count . deltaCheckpoint

-- | The checkpoint value stored for the key's class, or 'Nothing' when absent. O(log N).
checkpointLookup :: key -> PersistentDeltaMap key value -> Maybe value
checkpointLookup key deltaMap =
  SortedMap.lookup (probeKey key deltaMap) (deltaCheckpoint deltaMap)

-- | The checkpoint entries in ascending key order. Theta(1) per element.
checkpointToList :: PersistentDeltaMap key value -> [(key, value)]
checkpointToList = map unwrapEntry . SortedMap.toList . deltaCheckpoint

-- | The number of exact net-changed key classes. O(1).
changeCount :: PersistentDeltaMap key value -> Int
changeCount = SortedMap.count . deltaChanges

-- | Whether the current map differs semantically from its checkpoint. O(1).
hasChanges :: PersistentDeltaMap key value -> Bool
hasChanges = not . SortedMap.null . deltaChanges

-- | The classification implied by a change's two endpoints. O(1).
--
-- A record absent at both endpoints is not a net change and is never produced by this module;
-- asking for its kind raises the family's invariant error.
changeKind :: PersistentMapChange key value -> PersistentMapChangeKind
changeKind change =
  case (changeBefore change, changeAfter change) of
    (Just _, Just _) -> Updated
    (Just _, Nothing) -> Removed
    (Nothing, Just _) -> Added
    (Nothing, Nothing) ->
      error "Durable7.FingerTree.PersistentDeltaMap: a change is absent at both endpoints"

-- | One exact checkpoint-relative net change, or 'Nothing' when the class is unchanged.
-- O(log(k + 1)).
--
-- This searches the change index rather than the current state, so its cost tracks the number of
-- changes and not the size of the map.
lookupChange :: key -> PersistentDeltaMap key value -> Maybe (PersistentMapChange key value)
lookupChange key deltaMap =
  fmap storedChange (lookupStoredEntry (deltaChanges deltaMap) (probeKey key deltaMap))

-- | The exact net changes, once each, in ascending key order.  Theta(k + 1) when fully consumed.
--
-- The list is produced lazily at Theta(1) per element and invokes no key or value callback at all,
-- which is what makes full consumption output-optimal.  Unlike the eager Rust port, and like the
-- lazy baseline, abandoning the enumeration after one element costs only that element.
changes :: PersistentDeltaMap key value -> [PersistentMapChange key value]
changes = map storedChange . SortedMap.toList . deltaChanges

-- | The exact net changes whose keys lie in the inclusive range @[low, high]@, once each, in the
-- same ascending key order 'changes' uses.  An inverted range yields no changes.
--
-- The change index is itself an ordered map under the retained key policy, so this restricts to the
-- in-range sub-map and walks it, never filtering over all @k@ records: the cost does not depend on
-- how many changes fall outside the range.  Restricting to both boundaries is O(log(k + 1)) and
-- each yielded change is Theta(1) amortized, matching the managed and Rust baselines.  No value
-- callback runs.
--
-- \"Inverted\" is decided by the retained policy, so under a descending key order the low endpoint
-- is the numerically greater key.
changesInRange :: key -> key -> PersistentDeltaMap key value -> [PersistentMapChange key value]
changesInRange low high deltaMap =
  map storedChange (storedRange (deltaPolicy deltaMap) (deltaChanges deltaMap) low high)

-- | Adds or replaces one current entry and coalesces its checkpoint-relative change. O(log N).
--
-- A write whose value the retained policy considers equal to the current value is a semantic
-- no-op: the receiver itself is returned, sharing all three roots, and no change is recorded.  A
-- write that returns a class to its checkpoint state removes that class's record, and a write that
-- empties the change index snaps the current root back to the checkpoint root.
setItem :: key -> value -> PersistentDeltaMap key value -> PersistentDeltaMap key value
setItem key value deltaMap =
  case currentEntry of
    Just (_, currentValue) | policyValuesEqual mapPolicy currentValue value -> deltaMap
    _ -> successor deltaMap nextCurrent nextChanges
  where
    mapPolicy = deltaPolicy deltaMap
    probe = wrapKey mapPolicy key
    currentEntry = lookupStoredEntry (deltaCurrent deltaMap) probe
    changeEntry = lookupStoredEntry (deltaChanges deltaMap) probe
    -- A class present at the checkpoint keeps its baseline representative; a still-active addition
    -- episode keeps its first representative; only a genuinely new class takes the supplied key.
    representative =
      case currentEntry of
        Just (storedKey, _) -> storedKey
        Nothing ->
          case changeEntry of
            Just (storedKey, _) -> storedKey
            Nothing -> probe
    -- The first effective write captures the checkpoint endpoint; later writes preserve it.
    before =
      case changeEntry of
        Just (_, record) -> recordBefore record
        Nothing -> fmap snd currentEntry
    after = Just value
    nextCurrent = SortedMap.insert representative value (deltaCurrent deltaMap)
    nextChanges
      | equivalentEndpoints mapPolicy before after =
          SortedMap.delete representative (deltaChanges deltaMap)
      | otherwise =
          SortedMap.insert representative (DeltaRecord before after) (deltaChanges deltaMap)

-- | Applies the entries in order, exactly as repeated 'setItem' would. @O(m log N)@ for @m@
-- entries.
--
-- This is a convenience and an allocation saving over repeated single assignment, not a different
-- algorithm: it /is/ the left fold of 'setItem', so every coalescing, cancellation,
-- representative-retention, and checkpoint-snap rule holds verbatim and no stronger bound is
-- claimed.  A later entry for the same class overwrites an earlier one.  An empty list, or one
-- whose every entry is a semantic no-op, returns the receiver itself.
setItems :: [(key, value)] -> PersistentDeltaMap key value -> PersistentDeltaMap key value
setItems entries deltaMap = foldlStrict applyEntry deltaMap entries
  where
    applyEntry stored (key, value) = setItem key value stored

-- | Removes one current entry and coalesces its checkpoint-relative change. O(log N).
--
-- Removing an absent key is a no-op that returns the receiver itself.  Removing a class that was
-- added since the checkpoint cancels its record instead of recording a removal, and a removal that
-- empties the change index snaps the current root back to the checkpoint root.
remove :: key -> PersistentDeltaMap key value -> PersistentDeltaMap key value
remove key deltaMap =
  case lookupStoredEntry (deltaCurrent deltaMap) probe of
    Nothing -> deltaMap
    Just (currentKey, currentValue) ->
      let changeEntry = lookupStoredEntry (deltaChanges deltaMap) probe
          representative =
            case changeEntry of
              Just (storedKey, _) -> storedKey
              Nothing -> currentKey
          before =
            case changeEntry of
              Just (_, record) -> recordBefore record
              Nothing -> Just currentValue
          nextCurrent = SortedMap.delete currentKey (deltaCurrent deltaMap)
          nextChanges
            | equivalentEndpoints mapPolicy before Nothing =
                SortedMap.delete representative (deltaChanges deltaMap)
            | otherwise =
                SortedMap.insert representative (DeltaRecord before Nothing) (deltaChanges deltaMap)
      in successor deltaMap nextCurrent nextChanges
  where
    mapPolicy = deltaPolicy deltaMap
    probe = wrapKey mapPolicy key

-- | Makes the current snapshot the new checkpoint and resets the change index. O(1).
--
-- An already-clean version returns the receiver itself, and no key or value callback runs either
-- way: this is a root swap, not a comparison.
checkpoint :: PersistentDeltaMap key value -> PersistentDeltaMap key value
checkpoint deltaMap
  | SortedMap.null (deltaChanges deltaMap) = deltaMap
  | otherwise = PersistentDeltaMap (deltaPolicy deltaMap) state state SortedMap.empty
  where
    state = deltaCurrent deltaMap

-- | Returns to this version's checkpoint and resets the change index. O(1).
--
-- An already-clean version returns the receiver itself, and no key or value callback runs either
-- way.  Retained successors of the receiver are unaffected.
rollback :: PersistentDeltaMap key value -> PersistentDeltaMap key value
rollback deltaMap
  | SortedMap.null (deltaChanges deltaMap) = deltaMap
  | otherwise = PersistentDeltaMap (deltaPolicy deltaMap) state state SortedMap.empty
  where
    state = deltaCheckpoint deltaMap

-- | Walks the whole representation and returns recomputed statistics, or the first invariant
-- failure.
--
-- This is diagnostic validation, not part of any operation's cost: it is @O((N + k) log N)@ and it
-- does invoke key and value callbacks.  It checks that every root is strictly ascending under the
-- retained order, that every record has non-equivalent endpoints matching the two states, and that
-- the change cardinality is exact.  The storage half of the canonicalization rule — that a version
-- without changes reuses its checkpoint root rather than an equal copy — is not decidable purely
-- and is checked instead by 'sharesCheckpointWith'; what is checked here is that a version without
-- changes is extensionally equal to its checkpoint.
validateStructure :: PersistentDeltaMap key value -> Either String ValidationStatistics
validateStructure deltaMap = do
  checkAscending "the current state is not strictly ascending by key" comparer currentKeys
  checkAscending "the checkpoint state is not strictly ascending by key" comparer checkpointKeys
  checkAscending "the change index is not strictly ascending by key" comparer changeKeys
  (added, removed, updated) <- foldlEither classify (0, 0, 0) changeEntries
  requireRecords "a checkpoint difference has no recorded change" checkpointDifferences
  requireRecords "a current addition has no recorded change" currentAdditions
  let expected = List.length checkpointDifferences + List.length currentAdditions
  if expected /= SortedMap.count (deltaChanges deltaMap)
    then Left "the recorded change cardinality is not exact"
    else
      if List.null changeEntries && not extensionallyClean
        then Left "a version without changes differs from its checkpoint"
        else
          Right ValidationStatistics
            { validationCount = SortedMap.count (deltaCurrent deltaMap)
            , validationCheckpointCount = SortedMap.count (deltaCheckpoint deltaMap)
            , validationChangeCount = SortedMap.count (deltaChanges deltaMap)
            , validationAddedCount = added
            , validationRemovedCount = removed
            , validationUpdatedCount = updated
            }
  where
    mapPolicy = deltaPolicy deltaMap
    comparer = policyCompareKeys mapPolicy
    currentEntries = SortedMap.toList (deltaCurrent deltaMap)
    checkpointEntries = SortedMap.toList (deltaCheckpoint deltaMap)
    changeEntries = SortedMap.toList (deltaChanges deltaMap)
    currentKeys = map (orderedKeyValue . fst) currentEntries
    checkpointKeys = map (orderedKeyValue . fst) checkpointEntries
    changeKeys = map (orderedKeyValue . fst) changeEntries

    classify (added, removed, updated) (storedKey, record) = do
      let before = recordBefore record
          after = recordAfter record
      requireThat
        "a recorded change has equivalent endpoints"
        (not (equivalentEndpoints mapPolicy before after))
      requireThat
        "a change's before endpoint differs from the checkpoint state"
        (equivalentEndpoints mapPolicy before (SortedMap.lookup storedKey (deltaCheckpoint deltaMap)))
      requireThat
        "a change's after endpoint differs from the current state"
        (equivalentEndpoints mapPolicy after (SortedMap.lookup storedKey (deltaCurrent deltaMap)))
      case (before, after) of
        (Nothing, Just _) -> Right (added + 1, removed, updated)
        (Just _, Nothing) -> Right (added, removed + 1, updated)
        (Just _, Just _) -> Right (added, removed, updated + 1)
        (Nothing, Nothing) -> Left "a recorded change is absent at both endpoints"

    checkpointDifferences =
      [ storedKey
      | (storedKey, value) <- checkpointEntries
      , not (equivalentEndpoints mapPolicy (Just value) (SortedMap.lookup storedKey (deltaCurrent deltaMap)))
      ]
    currentAdditions =
      [ storedKey
      | (storedKey, _) <- currentEntries
      , not (SortedMap.member storedKey (deltaCheckpoint deltaMap))
      ]
    extensionallyClean =
      SortedMap.count (deltaCurrent deltaMap) == SortedMap.count (deltaCheckpoint deltaMap)
        && List.null checkpointDifferences

    requireRecords message probes =
      requireThat
        message
        (all (\probe -> SortedMap.member probe (deltaChanges deltaMap)) probes)

-- | Whether both versions are backed by the same current root, so neither can observe a change
-- made through the other. O(1).
--
-- A representation test rather than an equality test, answered by comparing stable names of the
-- two already-forced roots.  It does not verify that the operands retain extensionally identical
-- policies.
sharesRootWith :: PersistentDeltaMap key value -> PersistentDeltaMap key value -> IO Bool
sharesRootWith left right = sameObject (deltaCurrent left) (deltaCurrent right)

-- | Whether the left version's current root is the right version's checkpoint root. O(1).
--
-- The asymmetry is deliberate: it answers both \"is this version clean in storage as well as
-- extensionally\", by passing the same version twice, and \"was that version the checkpoint this
-- one measures against\".
sharesCheckpointWith :: PersistentDeltaMap key value -> PersistentDeltaMap key value -> IO Bool
sharesCheckpointWith left right = sameObject (deltaCurrent left) (deltaCheckpoint right)

-- | Whether both versions are backed by the same three roots. O(1).
--
-- This is the storage test a semantic no-op must satisfy: the result must share the receiver's
-- current state, checkpoint, and change index alike.
sharesStorageWith :: PersistentDeltaMap key value -> PersistentDeltaMap key value -> IO Bool
sharesStorageWith left right = do
  sameCurrent <- sameObject (deltaCurrent left) (deltaCurrent right)
  sameCheckpoint <- sameObject (deltaCheckpoint left) (deltaCheckpoint right)
  sameChanges <- sameObject (deltaChanges left) (deltaChanges right)
  pure (sameCurrent && sameCheckpoint && sameChanges)

sameObject :: a -> a -> IO Bool
sameObject left right = do
  leftValue <- evaluate left
  rightValue <- evaluate right
  leftName <- makeStableName leftValue
  rightName <- makeStableName rightValue
  pure (leftName `eqStableName` rightName)

-- A wholly cancelled epoch snaps back to the exact checkpoint root, so clean versions are
-- canonical and later checkpoint, rollback, and change queries stay O(1).
successor :: PersistentDeltaMap key value
          -> SortedMap (OrderedKey key) value
          -> SortedMap (OrderedKey key) (DeltaRecord value)
          -> PersistentDeltaMap key value
successor deltaMap nextCurrent nextChanges =
  PersistentDeltaMap (deltaPolicy deltaMap) canonicalCurrent (deltaCheckpoint deltaMap) nextChanges
  where
    canonicalCurrent
      | SortedMap.null nextChanges = deltaCheckpoint deltaMap
      | otherwise = nextCurrent

storedRange :: DeltaMapPolicy key value
            -> SortedMap (OrderedKey key) payload
            -> key
            -> key
            -> [(OrderedKey key, payload)]
-- The restriction is two ordered boundary descents on the retained key policy; enumerating the
-- result is then an ordinary lazy ascending walk, so an abandoned enumeration costs only the
-- boundaries and the elements actually demanded.
storedRange mapPolicy entries low high =
  SortedMap.toList
    (SortedMap.keyRange (wrapKey mapPolicy low) (wrapKey mapPolicy high) entries)

lookupStoredEntry :: SortedMap (OrderedKey key) payload
                  -> OrderedKey key
                  -> Maybe (OrderedKey key, payload)
lookupStoredEntry entries probe =
  case SortedMap.ceilingEntry probe entries of
    Just entry | fst entry == probe -> Just entry
    _ -> Nothing

storedChange :: (OrderedKey key, DeltaRecord value) -> PersistentMapChange key value
storedChange (storedKey, record) =
  PersistentMapChange (orderedKeyValue storedKey) (recordBefore record) (recordAfter record)

equivalentEndpoints :: DeltaMapPolicy key value -> Maybe value -> Maybe value -> Bool
equivalentEndpoints _ Nothing Nothing = True
equivalentEndpoints mapPolicy (Just left) (Just right) = policyValuesEqual mapPolicy left right
equivalentEndpoints _ _ _ = False

wrapKey :: DeltaMapPolicy key value -> key -> OrderedKey key
wrapKey mapPolicy key = OrderedKey key (policyCompareKeys mapPolicy)

probeKey :: key -> PersistentDeltaMap key value -> OrderedKey key
probeKey key deltaMap = wrapKey (deltaPolicy deltaMap) key

unwrapEntry :: (OrderedKey key, payload) -> (key, payload)
unwrapEntry (storedKey, payload) = (orderedKeyValue storedKey, payload)

checkAscending :: String -> (key -> key -> Ordering) -> [key] -> Either String ()
checkAscending message comparer = go
  where
    go (left : rest@(right : _))
      | comparer left right /= LT = Left message
      | otherwise = go rest
    go _ = Right ()

requireThat :: String -> Bool -> Either String ()
requireThat _ True = Right ()
requireThat message False = Left message

foldlEither :: (a -> b -> Either String a) -> a -> [b] -> Either String a
foldlEither _ !result [] = Right result
foldlEither operation !result (value : values) = do
  next <- operation result value
  foldlEither operation next values

foldlStrict :: (a -> b -> a) -> a -> [b] -> a
foldlStrict _ !result [] = result
foldlStrict operation !result (value : values) =
  foldlStrict operation (operation result value) values
