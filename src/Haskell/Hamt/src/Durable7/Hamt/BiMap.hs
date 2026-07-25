{-# LANGUAGE BangPatterns #-}

module Durable7.Hamt.BiMap
  ( BiMap
  , BiMapConflict(..)
  , empty
  , emptyWith
  , fromList
  , fromListWith
  , size
  , null
  , keyPolicy
  , valuePolicy
  , memberKey
  , memberValue
  , lookup
  , lookupKey
  , actualKey
  , actualValue
  , add
  , tryAdd
  , set
  , deleteKey
  , tryRemoveKey
  , deleteValue
  , tryRemoveValue
  , clear
  , inverse
  , toList
  , keys
  , elems
  , sharesRootsWith
  , validStructure
  ) where

import Prelude hiding (lookup, null)

import qualified Data.List as List

import Durable7.Hamt.Hashable (Hashable)
import Durable7.Hamt.HashMap (HashMap, HashPolicy(..))
import qualified Durable7.Hamt.HashMap as HashMap

data BiMapConflict = KeyConflict | ValueConflict
  deriving (Eq, Show)

-- Strict fields ensure both immutable successor maps reach weak-head normal form before a facade is
-- observable. Neither source map can be mutated, so an exception cannot publish half a bijection.
data BiMap k v = BiMap !(HashMap k v) !(HashMap v k)

empty :: (Eq k, Hashable k, Eq v, Hashable v) => BiMap k v
empty = emptyWith HashMap.defaultPolicy HashMap.defaultPolicy

emptyWith :: HashPolicy k -> HashPolicy v -> BiMap k v
emptyWith keysPolicy valuesPolicy =
  BiMap (HashMap.emptyWith keysPolicy) (HashMap.emptyWith valuesPolicy)

fromList :: (Eq k, Hashable k, Eq v, Hashable v) => [(k, v)] -> Either BiMapConflict (BiMap k v)
fromList = fromListWith HashMap.defaultPolicy HashMap.defaultPolicy

fromListWith :: HashPolicy k -> HashPolicy v -> [(k, v)] -> Either BiMapConflict (BiMap k v)
fromListWith keysPolicy valuesPolicy =
  List.foldl' addPair (Right (emptyWith keysPolicy valuesPolicy))
  where
    addPair (Left conflict) _ = Left conflict
    addPair (Right mapValue) (key, value) = add key value mapValue

size :: BiMap k v -> Int
size (BiMap forward _) = HashMap.size forward

null :: BiMap k v -> Bool
null mapValue = size mapValue == 0

keyPolicy :: BiMap k v -> HashPolicy k
keyPolicy (BiMap forward _) = HashMap.policy forward

valuePolicy :: BiMap k v -> HashPolicy v
valuePolicy (BiMap _ backward) = HashMap.policy backward

memberKey :: k -> BiMap k v -> Bool
memberKey key (BiMap forward _) = HashMap.member key forward

memberValue :: v -> BiMap k v -> Bool
memberValue value (BiMap _ backward) = HashMap.member value backward

lookup :: k -> BiMap k v -> Maybe v
lookup key (BiMap forward _) = HashMap.lookup key forward

lookupKey :: v -> BiMap k v -> Maybe k
lookupKey value (BiMap _ backward) = HashMap.lookup value backward

actualKey :: k -> BiMap k v -> Maybe k
actualKey key (BiMap forward _) = HashMap.actualKey key forward

actualValue :: v -> BiMap k v -> Maybe v
actualValue value (BiMap _ backward) = HashMap.actualKey value backward

add :: k -> v -> BiMap k v -> Either BiMapConflict (BiMap k v)
add key value source =
  case tryAdd key value source of
    (_, Just conflict) -> Left conflict
    (result, Nothing) -> Right result

-- Key conflict has precedence when both domain classes are represented.
tryAdd :: k -> v -> BiMap k v -> (BiMap k v, Maybe BiMapConflict)
tryAdd key value source@(BiMap forward backward)
  | HashMap.member key forward = (source, Just KeyConflict)
  | HashMap.member value backward = (source, Just ValueConflict)
  | otherwise =
      ( BiMap (HashMap.insert key value forward) (HashMap.insert value key backward)
      , Nothing
      )

-- Adds or replaces one key's value but never displaces a different key.
set :: k -> v -> BiMap k v -> Either BiMapConflict (BiMap k v)
set key value source@(BiMap forward backward) =
  case HashMap.lookup key forward of
    Nothing ->
      if HashMap.member value backward
        then Left ValueConflict
        else Right (BiMap (HashMap.insert key value forward) (HashMap.insert value key backward))
    Just oldValue
      | equalKeys (HashMap.policy backward) oldValue value -> Right source
      | HashMap.member value backward -> Left ValueConflict
      | otherwise ->
          case HashMap.actualKey key forward of
            Nothing -> error "persistent bimap invariant failure"
            Just storedKey ->
              Right
                (BiMap
                  (HashMap.insert storedKey value (HashMap.delete storedKey forward))
                  (HashMap.insert value storedKey (HashMap.delete oldValue backward)))

deleteKey :: k -> BiMap k v -> BiMap k v
deleteKey key source = fst (tryRemoveKey key source)

tryRemoveKey :: k -> BiMap k v -> (BiMap k v, Maybe v)
tryRemoveKey key source@(BiMap forward backward) =
  case (HashMap.actualKey key forward, HashMap.lookup key forward) of
    (Just storedKey, Just storedValue) ->
      ( BiMap (HashMap.delete storedKey forward) (HashMap.delete storedValue backward)
      , Just storedValue
      )
    _ -> (source, Nothing)

deleteValue :: v -> BiMap k v -> BiMap k v
deleteValue value source = fst (tryRemoveValue value source)

tryRemoveValue :: v -> BiMap k v -> (BiMap k v, Maybe k)
tryRemoveValue value source@(BiMap forward backward) =
  case (HashMap.actualKey value backward, HashMap.lookup value backward) of
    (Just storedValue, Just storedKey) ->
      ( BiMap (HashMap.delete storedKey forward) (HashMap.delete storedValue backward)
      , Just storedKey
      )
    _ -> (source, Nothing)

clear :: BiMap k v -> BiMap k v
clear source@(BiMap forward backward)
  | HashMap.null forward = source
  | otherwise = BiMap (HashMap.clear forward) (HashMap.clear backward)

-- O(1): swaps two immutable map facades without enumerating entries.
inverse :: BiMap k v -> BiMap v k
inverse (BiMap forward backward) = BiMap backward forward

toList :: BiMap k v -> [(k, v)]
toList (BiMap forward _) = HashMap.toList forward

keys :: BiMap k v -> [k]
keys (BiMap forward _) = HashMap.keys forward

elems :: BiMap k v -> [v]
elems (BiMap forward _) = HashMap.elems forward

sharesRootsWith :: BiMap k v -> BiMap k v -> Bool
sharesRootsWith (BiMap leftForward leftBackward) (BiMap rightForward rightBackward) =
  HashMap.sharesRootWith leftForward rightForward
    && HashMap.sharesRootWith leftBackward rightBackward

validStructure :: BiMap k v -> Bool
validStructure (BiMap forward backward) =
  HashMap.size forward == HashMap.size backward
    && HashMap.validStructure forward
    && HashMap.validStructure backward
    && all forwardEntryValid (HashMap.toList forward)
    && all backwardEntryValid (HashMap.toList backward)
  where
    forwardEntryValid (key, value) =
      case HashMap.lookup value backward of
        Just inverseKey -> equalKeys (HashMap.policy forward) key inverseKey
        Nothing -> False
    backwardEntryValid (value, key) =
      case HashMap.lookup key forward of
        Just forwardValue -> equalKeys (HashMap.policy backward) value forwardValue
        Nothing -> False
