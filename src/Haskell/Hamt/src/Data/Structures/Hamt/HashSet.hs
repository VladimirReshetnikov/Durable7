module Data.Structures.Hamt.HashSet
  ( HashSet
  , empty
  , emptyWith
  , singleton
  , singletonWith
  , fromList
  , fromListWith
  , size
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
union left right = List.foldl' (flip insert) left (toList right)

-- Membership in every binary relation below is judged under the receiver's
-- (left operand's) policy, matching the C# reference, which materializes its
-- probe set with this.Comparer; the argument's own policy never decides.
probeWithReceiverPolicy :: HashSet a -> HashSet a -> HashSet a
probeWithReceiverPolicy receiver argument =
  List.foldl' (flip insert) (clear receiver) (toList argument)

intersection :: HashSet a -> HashSet a -> HashSet a
intersection left right = List.foldl' addIfMember (clear left) (toList left)
  where
    probe = probeWithReceiverPolicy left right
    addIfMember result value
      | member value probe = insert value result
      | otherwise = result

difference :: HashSet a -> HashSet a -> HashSet a
difference left right = List.foldl' (flip delete) left (toList right)

-- Toggles the argument's elements on the receiver (matching the C#
-- reference) instead of materializing union/intersection intermediates,
-- so untouched subtrees stay structurally shared. The argument is first
-- deduplicated under the receiver's policy: elements that are duplicates
-- to the receiver must toggle once, not cancel out pairwise.
symmetricDifference :: HashSet a -> HashSet a -> HashSet a
symmetricDifference left right =
  List.foldl' toggle left (toList (probeWithReceiverPolicy left right))
  where
    toggle result value
      | member value result = delete value result
      | otherwise = insert value result

isSubsetOf :: HashSet a -> HashSet a -> Bool
isSubsetOf left right = all (`member` probeWithReceiverPolicy left right) (toList left)

-- The strictness comparisons below use the probe's size, not the argument's
-- raw size: the C# reference counts the probe set materialized under the
-- receiver's comparer, so argument elements that collapse under the
-- receiver's policy count once.
isProperSubsetOf :: HashSet a -> HashSet a -> Bool
isProperSubsetOf left right =
  size left < size probe && all (`member` probe) (toList left)
  where
    probe = probeWithReceiverPolicy left right

isSupersetOf :: HashSet a -> HashSet a -> Bool
isSupersetOf left right = all (`member` left) (toList right)

isProperSupersetOf :: HashSet a -> HashSet a -> Bool
isProperSupersetOf left right =
  size left > size (probeWithReceiverPolicy left right) && isSupersetOf left right

overlaps :: HashSet a -> HashSet a -> Bool
overlaps left right = any (`member` left) (toList right)

setEquals :: HashSet a -> HashSet a -> Bool
setEquals left right =
  size left == probeSize && all (`member` probe) (toList left)
  where
    probe = probeWithReceiverPolicy left right
    probeSize = size probe

toList :: HashSet a -> [a]
toList (HashSet values) = HashMap.keys values
