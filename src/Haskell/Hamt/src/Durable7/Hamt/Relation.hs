-- | A persistent many-to-many relation, indexed from both sides.
--
-- Both directions are maintained as exact inverses, so inverting the relation reuses both
-- existing indexes rather than rebuilding. Every operation returns a new version and leaves its
-- inputs valid, sharing unchanged structure, so an edit copies a path rather than the whole
-- collection.
module Durable7.Hamt.Relation
  ( Relation
  , empty
  , emptyWith
  , size
  , leftCount
  , rightCount
  , null
  , member
  , lookupRights
  , lookupLefts
  , insert
  , delete
  , deleteLeft
  , deleteRight
  , clear
  , inverse
  , toList
  , validStructure
  ) where

import Prelude hiding (null)

import qualified Data.List as List
import Durable7.Hamt.Hashable (Hashable)
import Durable7.Hamt.HashMap (HashPolicy)
import qualified Durable7.Hamt.HashMap as HashMap
import Durable7.Hamt.HashSet (HashSet)
import qualified Durable7.Hamt.HashSet as HashSet
import qualified Durable7.Hamt.HashMultimap as Multimap

-- | A persistent many-to-many relation indexed from both sides, so inverting it reuses both
-- existing indexes rather than rebuilding.
data Relation a b = Relation !(Multimap.HashMultimap a b) !(Multimap.HashMultimap b a)

-- | The empty relation.
empty :: (Eq a, Hashable a, Eq b, Hashable b) => Relation a b
empty = emptyWith HashMap.defaultPolicy HashMap.defaultPolicy

-- | The empty relation under the given policy, which it retains.
emptyWith :: HashPolicy a -> HashPolicy b -> Relation a b
emptyWith left right = Relation (Multimap.emptyWith left right) (Multimap.emptyWith right left)

-- | Number of pairs in the relation.
size :: Relation a b -> Int
size (Relation forward _) = Multimap.size forward

-- | How many distinct left values are present.
leftCount :: Relation a b -> Int
leftCount (Relation forward _) = Multimap.keyCount forward

-- | How many distinct right values are present.
rightCount :: Relation a b -> Int
rightCount (Relation _ reverseMap) = Multimap.keyCount reverseMap

-- | Whether the relation holds no pairs.
null :: Relation a b -> Bool
null relation = size relation == 0

-- | Whether the pair is present.
member :: a -> b -> Relation a b -> Bool
member left right (Relation forward _) = Multimap.member left right forward

-- | The right values related to the given left value.
lookupRights :: a -> Relation a b -> Maybe (HashSet b)
lookupRights left (Relation forward _) = Multimap.lookupValues left forward

-- | The left values related to the given right value. Both directions are indexed, so neither is a
-- scan.
lookupLefts :: b -> Relation a b -> Maybe (HashSet a)
lookupLefts right (Relation _ reverseMap) = Multimap.lookupValues right reverseMap

-- | A relation containing the given pair.
insert :: a -> b -> Relation a b -> Relation a b
insert left right relation@(Relation forward reverseMap)
  | Multimap.member left right forward = relation
  | otherwise = Relation (Multimap.insert left right forward) (Multimap.insert right left reverseMap)

-- | A relation without that pair.
delete :: a -> b -> Relation a b -> Relation a b
delete left right relation@(Relation forward reverseMap)
  | not (Multimap.member left right forward) = relation
  | otherwise = Relation (Multimap.delete left right forward) (Multimap.delete right left reverseMap)

-- | A relation without any pair holding that left value.
deleteLeft :: a -> Relation a b -> Relation a b
deleteLeft left relation =
  case lookupRights left relation of
    Nothing -> relation
    Just rights -> List.foldl' (\current right -> delete left right current) relation (HashSet.toList rights)

-- | A relation without any pair holding that right value.
deleteRight :: b -> Relation a b -> Relation a b
deleteRight right relation =
  case lookupLefts right relation of
    Nothing -> relation
    Just lefts -> List.foldl' (\current left -> delete left right current) relation (HashSet.toList lefts)

-- | The empty relation, retaining the same policies.
clear :: Relation a b -> Relation a b
clear (Relation forward reverseMap) = Relation (Multimap.clear forward) (Multimap.clear reverseMap)

-- | The relation with the two sides exchanged, reusing both existing indexes.
inverse :: Relation a b -> Relation b a
inverse (Relation forward reverseMap) = Relation reverseMap forward

-- | The pairs, in the relation's own order.
toList :: Relation a b -> [(a, b)]
toList (Relation forward _) = Multimap.toList forward

-- | Whether the relation's structural invariants hold. For tests and diagnostics.
validStructure :: Relation a b -> Bool
validStructure relation@(Relation forward reverseMap) =
  Multimap.validStructure forward
    && Multimap.validStructure reverseMap
    && Multimap.size forward == Multimap.size reverseMap
    && all (\(left, right) -> Multimap.member right left reverseMap) (toList relation)
    && all (\(right, left) -> Multimap.member left right forward) (Multimap.toList reverseMap)
