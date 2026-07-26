-- | A persistent, invertible record of changes between two map versions.
--
-- Each change records both the state it expects and the state it produces. That is what lets a
-- patch be inverted, composed with another, and applied against a target that may have drifted,
-- rather than only replayed forward onto its original source.
module Durable7.Hamt.PersistentMapPatch
  ( MapPatchEntry(..)
  , PersistentMapPatch
  , empty
  , emptyWith
  , fromList
  , fromListWith
  , between
  , size
  , null
  , policy
  , lookup
  , insert
  , delete
  , apply
  , invert
  , compose
  , toList
  , validStructure
  ) where

import Prelude hiding (lookup, null)

import Control.Monad (foldM)
import qualified Data.List as List
import Durable7.Hamt.Hashable (Hashable)
import Durable7.Hamt.HashMap (HashMap, HashPolicy, MapDifference(..))
import qualified Durable7.Hamt.HashMap as HashMap

-- | One strict, presence-safe map change. 'Nothing' denotes absence; a
-- present Haskell value remains distinguishable even when @v@ is itself a
-- 'Maybe'.
data MapPatchEntry k v = MapPatchEntry
  { entryKey :: k
  , beforeValue :: Maybe v
  , afterValue :: Maybe v
  }
  deriving (Eq, Show)

-- | A persistent, invertible record of changes between two map versions. Each change records both
-- the state it expects and the state it produces, which is what lets a patch be inverted,
-- composed, and applied against a target that may have drifted.
data PersistentMapPatch k v = PersistentMapPatch !(HashMap k (Maybe v, Maybe v))

-- | The empty patch.
empty :: (Eq k, Hashable k) => PersistentMapPatch k v
empty = emptyWith HashMap.defaultPolicy

-- | The empty patch under the given policy, which it retains.
emptyWith :: HashPolicy k -> PersistentMapPatch k v
emptyWith keys = PersistentMapPatch (HashMap.emptyWith keys)

-- | A patch holding a list's changes, built in bulk rather than by repeated insertion.
fromList :: (Eq k, Hashable k, Eq v) => [MapPatchEntry k v] -> Maybe (PersistentMapPatch k v)
fromList = fromListWith HashMap.defaultPolicy

-- | A patch holding a list's changes, under the given policy.
fromListWith :: Eq v => HashPolicy k -> [MapPatchEntry k v] -> Maybe (PersistentMapPatch k v)
fromListWith keys = foldM (flip insert) (emptyWith keys)

-- | Computes the strict patch between maps whose key policies are compatible.
between :: Eq v => HashMap k v -> HashMap k v -> PersistentMapPatch k v
between source target =
  PersistentMapPatch (List.foldl' addDifference (HashMap.emptyWith (HashMap.policy source)) (HashMap.diff source target))
  where
    addDifference changes difference = case difference of
      EntryAdded key value -> HashMap.insert key (Nothing, Just value) changes
      EntryRemoved key value -> HashMap.insert key (Just value, Nothing) changes
      EntryChanged key oldValue newValue -> HashMap.insert key (Just oldValue, Just newValue) changes

-- | Number of changes in the patch.
size :: PersistentMapPatch k v -> Int
size (PersistentMapPatch changes) = HashMap.size changes

-- | Whether the patch holds no changes.
null :: PersistentMapPatch k v -> Bool
null patch = size patch == 0

-- | The policy the patch retains.
policy :: PersistentMapPatch k v -> HashPolicy k
policy (PersistentMapPatch changes) = HashMap.policy changes

-- | The value stored for the key, or `Nothing` when absent.
lookup :: k -> PersistentMapPatch k v -> Maybe (MapPatchEntry k v)
lookup key (PersistentMapPatch changes) = do
  actual <- HashMap.actualKey key changes
  (before, after) <- HashMap.lookup key changes
  pure (MapPatchEntry actual before after)

-- | Adds a non-conflicting changed key. Semantic no-ops are ignored; an
-- equivalent changed key is rejected with 'Nothing'.
insert :: Eq v => MapPatchEntry k v -> PersistentMapPatch k v -> Maybe (PersistentMapPatch k v)
insert entry patch@(PersistentMapPatch changes)
  | sameState (beforeValue entry) (afterValue entry) = Just patch
  | HashMap.member (entryKey entry) changes = Nothing
  | otherwise = Just (PersistentMapPatch (HashMap.insert (entryKey entry) states changes))
  where
    states = (beforeValue entry, afterValue entry)

-- | A patch without that change.
delete :: k -> PersistentMapPatch k v -> PersistentMapPatch k v
delete key patch@(PersistentMapPatch changes)
  | not (HashMap.member key changes) = patch
  | otherwise = PersistentMapPatch (HashMap.delete key changes)

-- | Validates every expectation before publishing any edit.
apply :: Eq v => PersistentMapPatch k v -> HashMap k v -> Either k (HashMap k v)
apply patch source = case List.find conflicts (toList patch) of
  Just entry -> Left (entryKey entry)
  Nothing -> Right (List.foldl' applyEntry source (toList patch))
  where
    conflicts entry = HashMap.lookup (entryKey entry) source /= beforeValue entry
    applyEntry current entry = case afterValue entry of
      Nothing -> HashMap.delete (entryKey entry) current
      Just value -> HashMap.insert (entryKey entry) value current

-- | The patch undoing this one. Each change records both its prior and its resulting state, which
-- is what makes inversion possible.
invert :: PersistentMapPatch k v -> PersistentMapPatch k v
invert patch =
  PersistentMapPatch
    (HashMap.fromListWith (policy patch)
      [(entryKey entry, (afterValue entry, beforeValue entry)) | entry <- toList patch])

-- | Composes this patch with a following patch. Overlapping keys must have
-- equal intermediate states; otherwise the conflicting key is returned.
compose :: Eq v => PersistentMapPatch k v -> PersistentMapPatch k v -> Either k (PersistentMapPatch k v)
compose first next = foldM composeEntry first (toList next)
  where
    composeEntry current entry =
      case lookup (entryKey entry) current of
        Nothing ->
          case insert entry current of
            Just result -> Right result
            Nothing -> Left (entryKey entry)
        Just prior
          | not (sameState (afterValue prior) (beforeValue entry)) -> Left (entryKey entry)
          | sameState (beforeValue prior) (afterValue entry) -> Right (delete (entryKey prior) current)
          | otherwise ->
              let PersistentMapPatch changes = current
               in Right (PersistentMapPatch (HashMap.insert (entryKey prior) (beforeValue prior, afterValue entry) changes))

-- | The changes, in the patch's own order.
toList :: PersistentMapPatch k v -> [MapPatchEntry k v]
toList (PersistentMapPatch changes) =
  [MapPatchEntry key before after | (key, (before, after)) <- HashMap.toList changes]

-- | Whether the patch's structural invariants hold. For tests and diagnostics.
validStructure :: Eq v => PersistentMapPatch k v -> Bool
validStructure patch@(PersistentMapPatch changes) =
  HashMap.validStructure changes
    && all (\entry -> not (sameState (beforeValue entry) (afterValue entry))) (toList patch)

sameState :: Eq v => Maybe v -> Maybe v -> Bool
sameState = (==)
