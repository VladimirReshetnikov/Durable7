module Data.Structures.Hamt.HashSet
  ( HashSet
  , empty
  , emptyWith
  , singleton
  , singletonWith
  , fromList
  , fromListWith
  , size
  , policy
  , sharesRootWith
  , null
  , clear
  , member
  , actualValue
  , insert
  , insertNew
  , delete
  , tryRemove
  , union
  , intersection
  , difference
  , symmetricDifference
  , isSubsetOf
  , isProperSubsetOf
  , isSupersetOf
  , isProperSupersetOf
  , overlaps
  , setEquals
  , toList
  ) where

import Prelude hiding (null)

import qualified Data.List as List
import qualified Data.Structures.Hamt.HashMap as HashMap
import Data.Structures.Hamt.HashMap (HashPolicy)
import Data.Structures.Hamt.Hashable (Hashable)

newtype HashSet a = HashSet (HashMap.HashMap a ())

empty :: (Eq a, Hashable a) => HashSet a
empty = HashSet HashMap.empty

emptyWith :: HashPolicy a -> HashSet a
emptyWith hashPolicy = HashSet (HashMap.emptyWith hashPolicy)

singleton :: (Eq a, Hashable a) => a -> HashSet a
singleton value = insert value empty

singletonWith :: HashPolicy a -> a -> HashSet a
singletonWith hashPolicy value = insert value (emptyWith hashPolicy)

fromList :: (Eq a, Hashable a) => [a] -> HashSet a
fromList = List.foldl' (flip insert) empty

fromListWith :: HashPolicy a -> [a] -> HashSet a
fromListWith hashPolicy = List.foldl' (flip insert) (emptyWith hashPolicy)

size :: HashSet a -> Int
size (HashSet values) = HashMap.size values

policy :: HashSet a -> HashPolicy a
policy (HashSet values) = HashMap.policy values

-- | Reports whether two sets retain the exact same immutable map root.
sharesRootWith :: HashSet a -> HashSet a -> Bool
sharesRootWith (HashSet left) (HashSet right) = HashMap.sharesRootWith left right

null :: HashSet a -> Bool
null setValue = size setValue == 0

clear :: HashSet a -> HashSet a
clear (HashSet values) = HashSet (HashMap.clear values)

member :: a -> HashSet a -> Bool
member value (HashSet values) = HashMap.member value values

actualValue :: a -> HashSet a -> Maybe a
actualValue value (HashSet values) = HashMap.actualKey value values

-- Adding an already-present element is a no-op returning the receiver, so the
-- first stored instance wins and the root stays shared (matching the C# set's
-- Add; a plain map insert would rebuild the spine for the unit value).
insert :: a -> HashSet a -> HashSet a
insert value setValue@(HashSet values)
  | HashMap.member value values = setValue
  | otherwise = HashSet (HashMap.insert value () values)

insertNew :: a -> HashSet a -> Maybe (HashSet a)
insertNew value (HashSet values) = HashSet <$> HashMap.insertNew value () values

delete :: a -> HashSet a -> HashSet a
delete value (HashSet values) = HashSet (HashMap.delete value values)

tryRemove :: a -> HashSet a -> Maybe (HashSet a)
tryRemove value (HashSet values) =
  case HashMap.tryRemove value values of
    Just (_, rest) -> Just (HashSet rest)
    Nothing -> Nothing

union :: HashSet a -> HashSet a -> HashSet a
union (HashSet left) (HashSet right) = HashSet (HashMap.union left right)

intersection :: HashSet a -> HashSet a -> HashSet a
intersection (HashSet left) (HashSet right) = HashSet (HashMap.intersection left right)

difference :: HashSet a -> HashSet a -> HashSet a
difference (HashSet left) (HashSet right) = HashSet (HashMap.difference left right)

-- Structural map combination deduplicates the argument under the receiver's
-- policy before toggling when the policy objects are not pointer-identical.
symmetricDifference :: HashSet a -> HashSet a -> HashSet a
symmetricDifference (HashSet left) (HashSet right) =
  HashSet (HashMap.symmetricDifference left right)

isSubsetOf :: HashSet a -> HashSet a -> Bool
isSubsetOf left right = size (intersection left right) == size left

-- Strictness uses the structurally normalized union/intersection sizes, so
-- argument elements that collapse under the receiver's policy count once.
isProperSubsetOf :: HashSet a -> HashSet a -> Bool
isProperSubsetOf left right =
  isSubsetOf left right && size (union left right) > size left

isSupersetOf :: HashSet a -> HashSet a -> Bool
isSupersetOf left right = size (union left right) == size left

isProperSupersetOf :: HashSet a -> HashSet a -> Bool
isProperSupersetOf left right =
  isSupersetOf left right && size (intersection left right) < size left

overlaps :: HashSet a -> HashSet a -> Bool
overlaps left right = not (null (intersection left right))

setEquals :: HashSet a -> HashSet a -> Bool
setEquals left right =
  size (intersection left right) == size left && size (union left right) == size left

toList :: HashSet a -> [a]
toList (HashSet values) = HashMap.keys values
