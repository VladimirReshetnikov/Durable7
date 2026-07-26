{-# LANGUAGE DeriveDataTypeable #-}

-- | One-way, single-owner editing sessions for the persistent CHAMP map and set.
--
-- The sessions preserve the C# transient lifecycle and observable collection
-- semantics, but deliberately use the existing persistent path-copying edits.
-- Adoption and publication are O(1); point edits have the same complexity and
-- allocation behavior as the corresponding persistent operation.  The API
-- makes no owner-token or performance-win claim.
-- | Mutable sessions over the persistent hash collections, for building a version in bulk.
--
-- A session is seeded from a collection and leaves it untouched; its edits are visible only
-- through the session until it is persisted, and a persisted session is spent.
module Durable7.Hamt.Transient
  ( TransientException(..)
  , MapTransient
  , newMapTransient
  , newMapTransientWith
  , mapToTransient
  , mapTransientSize
  , mapTransientPolicy
  , mapTransientMember
  , mapTransientLookup
  , mapTransientActualKey
  , mapTransientToList
  , mapTransientAdd
  , mapTransientTryAdd
  , mapTransientPut
  , mapTransientDelete
  , mapTransientClear
  , persistMap
  , SetTransient
  , newSetTransient
  , newSetTransientWith
  , setToTransient
  , setTransientSize
  , setTransientPolicy
  , setTransientMember
  , setTransientActualValue
  , setTransientToList
  , setTransientAdd
  , setTransientDelete
  , setTransientClear
  , setTransientIsSubsetOf
  , setTransientIsProperSubsetOf
  , setTransientIsSupersetOf
  , setTransientIsProperSupersetOf
  , setTransientOverlaps
  , setTransientEquals
  , persistSet
  ) where

import Control.Exception (Exception, evaluate, mask, mask_, throwIO)
import Data.IORef (IORef, newIORef, readIORef, writeIORef)
import Data.Typeable (Typeable)

import Durable7.Hamt.Hashable (Hashable)
import qualified Durable7.Hamt.HashMap as HashMap
import qualified Durable7.Hamt.HashSet as HashSet

-- | Deterministic lifecycle failures raised by transient operations.
data TransientException
  = TransientConsumed
  | TransientDuplicateKey
  deriving (Eq, Show, Typeable)

instance Exception TransientException

-- | An unsynchronized one-way editing session over a persistent hash map.
newtype MapTransient k v = MapTransient (IORef (Maybe (HashMap.HashMap k v)))

-- | Creates an empty session using the default hash policy.
newMapTransient :: (Eq k, Hashable k) => IO (MapTransient k v)
newMapTransient = mapToTransient HashMap.empty

-- | Creates an empty session using the supplied hash policy object.
newMapTransientWith :: HashMap.HashPolicy k -> IO (MapTransient k v)
newMapTransientWith hashPolicy = mapToTransient (HashMap.emptyWith hashPolicy)

-- | Adopts a persistent source in O(1).  The source remains immutable.
mapToTransient :: HashMap.HashMap k v -> IO (MapTransient k v)
mapToTransient source = MapTransient <$> newIORef (Just source)

-- | Number of entries in the session.
mapTransientSize :: MapTransient k v -> IO Int
mapTransientSize session = HashMap.size <$> readMap session

-- | The policy the session retains.
mapTransientPolicy :: MapTransient k v -> IO (HashMap.HashPolicy k)
mapTransientPolicy session = HashMap.policy <$> readMap session

-- | Whether the entry is present.
mapTransientMember :: k -> MapTransient k v -> IO Bool
mapTransientMember key session = HashMap.member key <$> readMap session

-- | The value stored for the key, or `Nothing` when absent.
mapTransientLookup :: k -> MapTransient k v -> IO (Maybe v)
mapTransientLookup key session = HashMap.lookup key <$> readMap session

-- | The stored key representative, which need not be the key passed in.
mapTransientActualKey :: k -> MapTransient k v -> IO (Maybe k)
mapTransientActualKey key session = HashMap.actualKey key <$> readMap session

-- | The entries, in the session's own order.
mapTransientToList :: MapTransient k v -> IO [(k, v)]
mapTransientToList session = HashMap.toList <$> readMap session

-- | Adds a new key or raises 'TransientDuplicateKey'.
mapTransientAdd :: k -> v -> MapTransient k v -> IO ()
mapTransientAdd key value session = do
  added <- mapTransientTryAdd key value session
  if added then pure () else throwIO TransientDuplicateKey

-- | Adds a key when absent.  A duplicate is an identity-preserving no-op.
mapTransientTryAdd :: k -> v -> MapTransient k v -> IO Bool
mapTransientTryAdd key value session = editMap session $ \current ->
  case HashMap.insertNew key value current of
    Nothing -> pure (False, Nothing)
    Just next -> do
      prepared <- evaluate next
      pure (True, Just prepared)

-- | Sets a value.  Equal values preserve the current persistent root.
mapTransientPut :: Eq v => k -> v -> MapTransient k v -> IO Bool
mapTransientPut key value session = editMap session $ \current ->
  case HashMap.lookup key current of
    Just oldValue | oldValue == value -> pure (False, Nothing)
    _ -> do
      prepared <- evaluate (HashMap.insert key value current)
      pure (True, Just prepared)

-- | Removes a key when present.  A miss is an identity-preserving no-op.
mapTransientDelete :: k -> MapTransient k v -> IO Bool
mapTransientDelete key session = editMap session $ \current ->
  case HashMap.tryRemove key current of
    Nothing -> pure (False, Nothing)
    Just (_, next) -> do
      prepared <- evaluate next
      pure (True, Just prepared)

-- | Clears the session in O(1).  Clearing an empty session is a no-op.
mapTransientClear :: MapTransient k v -> IO Bool
mapTransientClear session = editMap session $ \current ->
  if HashMap.null current
    then pure (False, Nothing)
    else do
      prepared <- evaluate (HashMap.clear current)
      pure (True, Just prepared)

-- | Publishes the current map in O(1) and consumes the session.
persistMap :: MapTransient k v -> IO (HashMap.HashMap k v)
persistMap (MapTransient reference) = mask_ $ do
  current <- readActive reference
  writeIORef reference Nothing
  pure current

-- | An unsynchronized one-way editing session over a persistent hash set.
newtype SetTransient a = SetTransient (IORef (Maybe (HashSet.HashSet a)))

-- | Opens an empty mutable session.
newSetTransient :: (Eq a, Hashable a) => IO (SetTransient a)
newSetTransient = setToTransient HashSet.empty

-- | Opens an empty mutable session under the given policy.
newSetTransientWith :: HashMap.HashPolicy a -> IO (SetTransient a)
newSetTransientWith hashPolicy = setToTransient (HashSet.emptyWith hashPolicy)

-- | Opens a mutable session seeded from this set. The set itself is unaffected; the session's edits
-- are visible only through it.
setToTransient :: HashSet.HashSet a -> IO (SetTransient a)
setToTransient source = SetTransient <$> newIORef (Just source)

-- | Number of elements in the session.
setTransientSize :: SetTransient a -> IO Int
setTransientSize session = HashSet.size <$> readSet session

-- | The policy the session retains.
setTransientPolicy :: SetTransient a -> IO (HashMap.HashPolicy a)
setTransientPolicy session = HashSet.policy <$> readSet session

-- | Whether the element is present.
setTransientMember :: a -> SetTransient a -> IO Bool
setTransientMember value session = HashSet.member value <$> readSet session

-- | The stored value representative.
setTransientActualValue :: a -> SetTransient a -> IO (Maybe a)
setTransientActualValue value session = HashSet.actualValue value <$> readSet session

-- | The elements, in the session's own order.
setTransientToList :: SetTransient a -> IO [a]
setTransientToList session = HashSet.toList <$> readSet session

-- | A session containing the given element.
setTransientAdd :: a -> SetTransient a -> IO Bool
setTransientAdd value session = editSet session $ \current ->
  case HashSet.insertNew value current of
    Nothing -> pure (False, Nothing)
    Just next -> do
      prepared <- evaluate next
      pure (True, Just prepared)

-- | A session without that element.
setTransientDelete :: a -> SetTransient a -> IO Bool
setTransientDelete value session = editSet session $ \current ->
  case HashSet.tryRemove value current of
    Nothing -> pure (False, Nothing)
    Just next -> do
      prepared <- evaluate next
      pure (True, Just prepared)

-- | The empty session, retaining the same policies.
setTransientClear :: SetTransient a -> IO Bool
setTransientClear session = editSet session $ \current ->
  if HashSet.null current
    then pure (False, Nothing)
    else do
      prepared <- evaluate (HashSet.clear current)
      pure (True, Just prepared)

-- | Whether every element of this session also occurs in the other.
setTransientIsSubsetOf :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientIsSubsetOf session other = (`HashSet.isSubsetOf` other) <$> readSet session

-- | Whether this session is a subset of the other and the other holds an element it lacks.
setTransientIsProperSubsetOf :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientIsProperSubsetOf session other = (`HashSet.isProperSubsetOf` other) <$> readSet session

-- | Whether every element of the other occurs in this session.
setTransientIsSupersetOf :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientIsSupersetOf session other = (`HashSet.isSupersetOf` other) <$> readSet session

-- | Whether this session is a superset of the other and holds an element the other lacks.
setTransientIsProperSupersetOf :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientIsProperSupersetOf session other = (`HashSet.isProperSupersetOf` other) <$> readSet session

-- | Whether the two sessions share at least one element.
setTransientOverlaps :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientOverlaps session other = (`HashSet.overlaps` other) <$> readSet session

-- | Whether both sessions hold the same elements.
setTransientEquals :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientEquals session other = (`HashSet.setEquals` other) <$> readSet session

-- | Closes the session and produces the set holding its accumulated edits.
persistSet :: SetTransient a -> IO (HashSet.HashSet a)
persistSet (SetTransient reference) = mask_ $ do
  current <- readActive reference
  writeIORef reference Nothing
  pure current

readMap :: MapTransient k v -> IO (HashMap.HashMap k v)
readMap (MapTransient reference) = readActive reference

readSet :: SetTransient a -> IO (HashSet.HashSet a)
readSet (SetTransient reference) = readActive reference

readActive :: IORef (Maybe a) -> IO a
readActive reference = do
  state <- readIORef reference
  case state of
    Just value -> pure value
    Nothing -> throwIO TransientConsumed

editMap
  :: MapTransient k v
  -> (HashMap.HashMap k v -> IO (result, Maybe (HashMap.HashMap k v)))
  -> IO result
editMap (MapTransient reference) prepare = mask $ \restore -> do
  current <- readActive reference
  (result, replacement) <- restore (prepare current)
  case replacement of
    Nothing -> pure result
    Just next -> writeIORef reference (Just next) >> pure result

editSet
  :: SetTransient a
  -> (HashSet.HashSet a -> IO (result, Maybe (HashSet.HashSet a)))
  -> IO result
editSet (SetTransient reference) prepare = mask $ \restore -> do
  current <- readActive reference
  (result, replacement) <- restore (prepare current)
  case replacement of
    Nothing -> pure result
    Just next -> writeIORef reference (Just next) >> pure result
