-- | A persistent hash set backed by the same CHAMP trie, keeping the first representative of each
-- equivalence class.
--
-- Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
-- so an edit copies a path rather than the whole collection.
module Durable7.Hamt.HashSet
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
import qualified Durable7.Hamt.HashMap as HashMap
import Durable7.Hamt.HashMap (HashPolicy)
import Durable7.Hamt.Hashable (Hashable)

-- | A persistent hash set backed by the same CHAMP trie, retaining the first representative of each
-- equivalence class.
newtype HashSet a = HashSet (HashMap.HashMap a ())

-- | The empty set.
empty :: (Eq a, Hashable a) => HashSet a
empty = HashSet HashMap.empty

-- | The empty set under the given policy, which it retains.
emptyWith :: HashPolicy a -> HashSet a
emptyWith hashPolicy = HashSet (HashMap.emptyWith hashPolicy)

-- | A set holding one element.
singleton :: (Eq a, Hashable a) => a -> HashSet a
singleton value = insert value empty

-- | A set holding one element, under the given policy.
singletonWith :: HashPolicy a -> a -> HashSet a
singletonWith hashPolicy value = insert value (emptyWith hashPolicy)

-- | A set holding a list's elements, built in bulk rather than by repeated insertion.
fromList :: (Eq a, Hashable a) => [a] -> HashSet a
fromList = List.foldl' (flip insert) empty

-- | A set holding a list's elements, under the given policy.
fromListWith :: HashPolicy a -> [a] -> HashSet a
fromListWith hashPolicy = List.foldl' (flip insert) (emptyWith hashPolicy)

-- | Number of elements in the set.
size :: HashSet a -> Int
size (HashSet values) = HashMap.size values

-- | The policy the set retains.
policy :: HashSet a -> HashPolicy a
policy (HashSet values) = HashMap.policy values

-- | Reports whether two sets retain the exact same immutable map root.
sharesRootWith :: HashSet a -> HashSet a -> Bool
sharesRootWith (HashSet left) (HashSet right) = HashMap.sharesRootWith left right

-- | Whether the set holds no elements.
null :: HashSet a -> Bool
null setValue = size setValue == 0

-- | The empty set, retaining the same policies.
clear :: HashSet a -> HashSet a
clear (HashSet values) = HashSet (HashMap.clear values)

-- | Whether the element is present.
member :: a -> HashSet a -> Bool
member value (HashSet values) = HashMap.member value values

-- | The stored value representative.
actualValue :: a -> HashSet a -> Maybe a
actualValue value (HashSet values) = HashMap.actualKey value values

-- | Adding an already-present element is a no-op returning the receiver, so the
-- first stored instance wins and the root stays shared (matching the C# set's
-- Add; a plain map insert would rebuild the spine for the unit value).
insert :: a -> HashSet a -> HashSet a
insert value setValue@(HashSet values)
  | HashMap.member value values = setValue
  | otherwise = HashSet (HashMap.insert value () values)

-- | A set containing the given element, unchanged when an equivalent one is present.
insertNew :: a -> HashSet a -> Maybe (HashSet a)
insertNew value (HashSet values) = HashSet <$> HashMap.insertNew value () values

-- | A set without that element.
delete :: a -> HashSet a -> HashSet a
delete value (HashSet values) = HashSet (HashMap.delete value values)

-- | A set without that element, or `Nothing` when it was absent.
tryRemove :: a -> HashSet a -> Maybe (HashSet a)
tryRemove value (HashSet values) =
  case HashMap.tryRemove value values of
    Just (_, rest) -> Just (HashSet rest)
    Nothing -> Nothing

-- | The elements of both sets. Subtrees the operands already share are adopted whole rather than
-- re-entered.
union :: HashSet a -> HashSet a -> HashSet a
union (HashSet left) (HashSet right) = HashSet (HashMap.union left right)

-- | The elements present in both sets.
intersection :: HashSet a -> HashSet a -> HashSet a
intersection (HashSet left) (HashSet right) = HashSet (HashMap.intersection left right)

-- | This set's elements that are absent from the other.
difference :: HashSet a -> HashSet a -> HashSet a
difference (HashSet left) (HashSet right) = HashSet (HashMap.difference left right)

-- | Structural map combination deduplicates the argument under the receiver's
-- policy before toggling when the policy objects are not pointer-identical.
symmetricDifference :: HashSet a -> HashSet a -> HashSet a
symmetricDifference (HashSet left) (HashSet right) =
  HashSet (HashMap.symmetricDifference left right)

-- | Whether every element of this set also occurs in the other.
isSubsetOf :: HashSet a -> HashSet a -> Bool
isSubsetOf left right = size (intersection left right) == size left

-- | Strictness uses the structurally normalized union/intersection sizes, so
-- argument elements that collapse under the receiver's policy count once.
isProperSubsetOf :: HashSet a -> HashSet a -> Bool
isProperSubsetOf left right =
  isSubsetOf left right && size (union left right) > size left

-- | Whether every element of the other occurs in this set.
isSupersetOf :: HashSet a -> HashSet a -> Bool
isSupersetOf left right = size (union left right) == size left

-- | Whether this set is a superset of the other and holds an element the other lacks.
isProperSupersetOf :: HashSet a -> HashSet a -> Bool
isProperSupersetOf left right =
  isSupersetOf left right && size (intersection left right) < size left

-- | Whether the two sets share at least one element.
overlaps :: HashSet a -> HashSet a -> Bool
overlaps left right = not (null (intersection left right))

-- | Whether both sets hold the same elements.
setEquals :: HashSet a -> HashSet a -> Bool
setEquals left right =
  size (intersection left right) == size left && size (union left right) == size left

-- | The elements, in the set's own order.
toList :: HashSet a -> [a]
toList (HashSet values) = HashMap.keys values
