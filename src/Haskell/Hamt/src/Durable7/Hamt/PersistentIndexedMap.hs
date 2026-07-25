module Durable7.Hamt.PersistentIndexedMap
  ( PersistentIndexedMap
  , empty
  , emptyWith
  , fromList
  , fromListWith
  , size
  , null
  , keyPolicy
  , indexPolicy
  , member
  , lookup
  , actualKey
  , lookupIndexKey
  , memberIndex
  , lookupKeysByIndex
  , insert
  , set
  , delete
  , clear
  , toList
  , validStructure
  ) where

import Prelude hiding (lookup, null)

import qualified Data.List as List
import Durable7.Hamt.Hashable (Hashable)
import Durable7.Hamt.HashMap (HashMap, HashPolicy(..))
import qualified Durable7.Hamt.HashMap as HashMap
import Durable7.Hamt.HashMultimap (HashMultimap)
import qualified Durable7.Hamt.HashMultimap as HashMultimap
import Durable7.Hamt.HashSet (HashSet)

data IndexedEntry v i = IndexedEntry v i

data PersistentIndexedMap k v i = PersistentIndexedMap
  !(HashMap k (IndexedEntry v i))
  !(HashMultimap i k)
  !(k -> v -> i)
  !(v -> v -> Bool)

empty :: (Eq k, Hashable k, Eq v, Eq i, Hashable i) => (k -> v -> i) -> PersistentIndexedMap k v i
empty selector = emptyWith selector HashMap.defaultPolicy (==) HashMap.defaultPolicy

emptyWith :: (k -> v -> i) -> HashPolicy k -> (v -> v -> Bool) -> HashPolicy i -> PersistentIndexedMap k v i
emptyWith selector keys equalValues indexes =
  PersistentIndexedMap (HashMap.emptyWith keys) (HashMultimap.emptyWith indexes keys) selector equalValues

fromList :: (Eq k, Hashable k, Eq v, Eq i, Hashable i) => (k -> v -> i) -> [(k, v)] -> PersistentIndexedMap k v i
fromList selector = List.foldl' (\current (key, value) -> set key value current) (empty selector)

fromListWith :: (k -> v -> i) -> HashPolicy k -> (v -> v -> Bool) -> HashPolicy i -> [(k, v)] -> PersistentIndexedMap k v i
fromListWith selector keys equalValues indexes =
  List.foldl' (\current (key, value) -> set key value current) (emptyWith selector keys equalValues indexes)

size :: PersistentIndexedMap k v i -> Int
size (PersistentIndexedMap primary _ _ _) = HashMap.size primary

null :: PersistentIndexedMap k v i -> Bool
null values = size values == 0

keyPolicy :: PersistentIndexedMap k v i -> HashPolicy k
keyPolicy (PersistentIndexedMap primary _ _ _) = HashMap.policy primary

indexPolicy :: PersistentIndexedMap k v i -> HashPolicy i
indexPolicy (PersistentIndexedMap _ secondary _ _) = HashMultimap.keyPolicy secondary

member :: k -> PersistentIndexedMap k v i -> Bool
member key (PersistentIndexedMap primary _ _ _) = HashMap.member key primary

lookup :: k -> PersistentIndexedMap k v i -> Maybe v
lookup key (PersistentIndexedMap primary _ _ _) = valueOf <$> HashMap.lookup key primary

actualKey :: k -> PersistentIndexedMap k v i -> Maybe k
actualKey key (PersistentIndexedMap primary _ _ _) = HashMap.actualKey key primary

lookupIndexKey :: k -> PersistentIndexedMap k v i -> Maybe i
lookupIndexKey key (PersistentIndexedMap primary _ _ _) = indexOf <$> HashMap.lookup key primary

memberIndex :: i -> PersistentIndexedMap k v i -> Bool
memberIndex indexKey (PersistentIndexedMap _ secondary _ _) = HashMultimap.memberKey indexKey secondary

lookupKeysByIndex :: i -> PersistentIndexedMap k v i -> Maybe (HashSet k)
lookupKeysByIndex indexKey (PersistentIndexedMap _ secondary _ _) = HashMultimap.lookupValues indexKey secondary

-- | Adds a new primary row, returning 'Nothing' for an equivalent key.
insert :: k -> v -> PersistentIndexedMap k v i -> Maybe (PersistentIndexedMap k v i)
insert key value (PersistentIndexedMap primary secondary selector equalValues)
  | HashMap.member key primary = Nothing
  | otherwise =
      let selectedIndex = selector key value
          nextSecondary = HashMultimap.insert selectedIndex key secondary
          actualIndex = requiredIndex selectedIndex nextSecondary
          nextPrimary = HashMap.insert key (IndexedEntry value actualIndex) primary
       in Just (PersistentIndexedMap nextPrimary nextSecondary selector equalValues)

-- | Sets a row and moves its primary key between secondary groups only when
-- the selected index class changes. First key and index representatives win.
set :: k -> v -> PersistentIndexedMap k v i -> PersistentIndexedMap k v i
set key value values@(PersistentIndexedMap primary secondary selector equalValues) =
  case HashMap.lookup key primary of
    Nothing -> case insert key value values of
      Just result -> result
      Nothing -> values
    Just current
      | equalValues (valueOf current) value -> values
      | otherwise ->
          let storedKey = requiredKey key primary
              selectedIndex = selector storedKey value
              indexesEqual = equalKeys (indexPolicy values) (indexOf current) selectedIndex
           in if indexesEqual
                then PersistentIndexedMap
                  (HashMap.insert storedKey (IndexedEntry value (indexOf current)) primary)
                  secondary selector equalValues
                else
                  let withoutOld = HashMultimap.delete (indexOf current) storedKey secondary
                      nextSecondary = HashMultimap.insert selectedIndex storedKey withoutOld
                      actualIndex = requiredIndex selectedIndex nextSecondary
                   in PersistentIndexedMap
                        (HashMap.insert storedKey (IndexedEntry value actualIndex) primary)
                        nextSecondary selector equalValues

delete :: k -> PersistentIndexedMap k v i -> PersistentIndexedMap k v i
delete key values@(PersistentIndexedMap primary secondary selector equalValues) =
  case HashMap.lookup key primary of
    Nothing -> values
    Just current ->
      let storedKey = requiredKey key primary
       in PersistentIndexedMap
            (HashMap.delete storedKey primary)
            (HashMultimap.delete (indexOf current) storedKey secondary)
            selector equalValues

clear :: PersistentIndexedMap k v i -> PersistentIndexedMap k v i
clear values@(PersistentIndexedMap primary secondary selector equalValues)
  | null values = values
  | otherwise = PersistentIndexedMap (HashMap.clear primary) (HashMultimap.clear secondary) selector equalValues

toList :: PersistentIndexedMap k v i -> [(k, v)]
toList (PersistentIndexedMap primary _ _ _) =
  [(key, valueOf entry) | (key, entry) <- HashMap.toList primary]

validStructure :: PersistentIndexedMap k v i -> Bool
validStructure values@(PersistentIndexedMap primary secondary _ _) =
  HashMap.validStructure primary
    && HashMultimap.validStructure secondary
    && HashMap.size primary == HashMultimap.size secondary
    && all primaryHasIndex (HashMap.toList primary)
    && all indexHasPrimary (HashMultimap.toList secondary)
  where
    primaryHasIndex (key, entry) = HashMultimap.member (indexOf entry) key secondary
    indexHasPrimary (indexKey, key) = case HashMap.lookup key primary of
      Just entry -> equalKeys (indexPolicy values) (indexOf entry) indexKey
      Nothing -> False

valueOf :: IndexedEntry v i -> v
valueOf (IndexedEntry value _) = value

indexOf :: IndexedEntry v i -> i
indexOf (IndexedEntry _ indexKey) = indexKey

requiredKey :: k -> HashMap k v -> k
requiredKey key primary = case HashMap.actualKey key primary of
  Just actual -> actual
  Nothing -> error "PersistentIndexedMap failed to retain a primary key"

requiredIndex :: i -> HashMultimap i k -> i
requiredIndex indexKey secondary = case HashMultimap.actualKey indexKey secondary of
  Just actual -> actual
  Nothing -> error "PersistentIndexedMap failed to retain an index key"
