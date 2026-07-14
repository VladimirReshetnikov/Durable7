{-# LANGUAGE DeriveDataTypeable #-}

-- | One-way, single-owner editing sessions for the persistent CHAMP map and set.
--
-- The sessions preserve the C# transient lifecycle and observable collection
-- semantics, but deliberately use the existing persistent path-copying edits.
-- Adoption and publication are O(1); point edits have the same complexity and
-- allocation behavior as the corresponding persistent operation.  The API
-- makes no owner-token or performance-win claim.
module Data.Structures.Hamt.Transient
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

import Data.Structures.Hamt.Hashable (Hashable)
import qualified Data.Structures.Hamt.HashMap as HashMap
import qualified Data.Structures.Hamt.HashSet as HashSet

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

mapTransientSize :: MapTransient k v -> IO Int
mapTransientSize session = HashMap.size <$> readMap session

mapTransientPolicy :: MapTransient k v -> IO (HashMap.HashPolicy k)
mapTransientPolicy session = HashMap.policy <$> readMap session

mapTransientMember :: k -> MapTransient k v -> IO Bool
mapTransientMember key session = HashMap.member key <$> readMap session

mapTransientLookup :: k -> MapTransient k v -> IO (Maybe v)
mapTransientLookup key session = HashMap.lookup key <$> readMap session

mapTransientActualKey :: k -> MapTransient k v -> IO (Maybe k)
mapTransientActualKey key session = HashMap.actualKey key <$> readMap session

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

newSetTransient :: (Eq a, Hashable a) => IO (SetTransient a)
newSetTransient = setToTransient HashSet.empty

newSetTransientWith :: HashMap.HashPolicy a -> IO (SetTransient a)
newSetTransientWith hashPolicy = setToTransient (HashSet.emptyWith hashPolicy)

setToTransient :: HashSet.HashSet a -> IO (SetTransient a)
setToTransient source = SetTransient <$> newIORef (Just source)

setTransientSize :: SetTransient a -> IO Int
setTransientSize session = HashSet.size <$> readSet session

setTransientPolicy :: SetTransient a -> IO (HashMap.HashPolicy a)
setTransientPolicy session = HashSet.policy <$> readSet session

setTransientMember :: a -> SetTransient a -> IO Bool
setTransientMember value session = HashSet.member value <$> readSet session

setTransientActualValue :: a -> SetTransient a -> IO (Maybe a)
setTransientActualValue value session = HashSet.actualValue value <$> readSet session

setTransientToList :: SetTransient a -> IO [a]
setTransientToList session = HashSet.toList <$> readSet session

setTransientAdd :: a -> SetTransient a -> IO Bool
setTransientAdd value session = editSet session $ \current ->
  case HashSet.insertNew value current of
    Nothing -> pure (False, Nothing)
    Just next -> do
      prepared <- evaluate next
      pure (True, Just prepared)

setTransientDelete :: a -> SetTransient a -> IO Bool
setTransientDelete value session = editSet session $ \current ->
  case HashSet.tryRemove value current of
    Nothing -> pure (False, Nothing)
    Just next -> do
      prepared <- evaluate next
      pure (True, Just prepared)

setTransientClear :: SetTransient a -> IO Bool
setTransientClear session = editSet session $ \current ->
  if HashSet.null current
    then pure (False, Nothing)
    else do
      prepared <- evaluate (HashSet.clear current)
      pure (True, Just prepared)

setTransientIsSubsetOf :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientIsSubsetOf session other = (`HashSet.isSubsetOf` other) <$> readSet session

setTransientIsProperSubsetOf :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientIsProperSubsetOf session other = (`HashSet.isProperSubsetOf` other) <$> readSet session

setTransientIsSupersetOf :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientIsSupersetOf session other = (`HashSet.isSupersetOf` other) <$> readSet session

setTransientIsProperSupersetOf :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientIsProperSupersetOf session other = (`HashSet.isProperSupersetOf` other) <$> readSet session

setTransientOverlaps :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientOverlaps session other = (`HashSet.overlaps` other) <$> readSet session

setTransientEquals :: SetTransient a -> HashSet.HashSet a -> IO Bool
setTransientEquals session other = (`HashSet.setEquals` other) <$> readSet session

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
