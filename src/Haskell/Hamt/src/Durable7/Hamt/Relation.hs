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

data Relation a b = Relation !(Multimap.HashMultimap a b) !(Multimap.HashMultimap b a)

empty :: (Eq a, Hashable a, Eq b, Hashable b) => Relation a b
empty = emptyWith HashMap.defaultPolicy HashMap.defaultPolicy

emptyWith :: HashPolicy a -> HashPolicy b -> Relation a b
emptyWith left right = Relation (Multimap.emptyWith left right) (Multimap.emptyWith right left)

size :: Relation a b -> Int
size (Relation forward _) = Multimap.size forward

leftCount :: Relation a b -> Int
leftCount (Relation forward _) = Multimap.keyCount forward

rightCount :: Relation a b -> Int
rightCount (Relation _ reverseMap) = Multimap.keyCount reverseMap

null :: Relation a b -> Bool
null relation = size relation == 0

member :: a -> b -> Relation a b -> Bool
member left right (Relation forward _) = Multimap.member left right forward

lookupRights :: a -> Relation a b -> Maybe (HashSet b)
lookupRights left (Relation forward _) = Multimap.lookupValues left forward

lookupLefts :: b -> Relation a b -> Maybe (HashSet a)
lookupLefts right (Relation _ reverseMap) = Multimap.lookupValues right reverseMap

insert :: a -> b -> Relation a b -> Relation a b
insert left right relation@(Relation forward reverseMap)
  | Multimap.member left right forward = relation
  | otherwise = Relation (Multimap.insert left right forward) (Multimap.insert right left reverseMap)

delete :: a -> b -> Relation a b -> Relation a b
delete left right relation@(Relation forward reverseMap)
  | not (Multimap.member left right forward) = relation
  | otherwise = Relation (Multimap.delete left right forward) (Multimap.delete right left reverseMap)

deleteLeft :: a -> Relation a b -> Relation a b
deleteLeft left relation =
  case lookupRights left relation of
    Nothing -> relation
    Just rights -> List.foldl' (\current right -> delete left right current) relation (HashSet.toList rights)

deleteRight :: b -> Relation a b -> Relation a b
deleteRight right relation =
  case lookupLefts right relation of
    Nothing -> relation
    Just lefts -> List.foldl' (\current left -> delete left right current) relation (HashSet.toList lefts)

clear :: Relation a b -> Relation a b
clear (Relation forward reverseMap) = Relation (Multimap.clear forward) (Multimap.clear reverseMap)

inverse :: Relation a b -> Relation b a
inverse (Relation forward reverseMap) = Relation reverseMap forward

toList :: Relation a b -> [(a, b)]
toList (Relation forward _) = Multimap.toList forward

validStructure :: Relation a b -> Bool
validStructure relation@(Relation forward reverseMap) =
  Multimap.validStructure forward
    && Multimap.validStructure reverseMap
    && Multimap.size forward == Multimap.size reverseMap
    && all (\(left, right) -> Multimap.member right left reverseMap) (toList relation)
    && all (\(right, left) -> Multimap.member left right forward) (Multimap.toList reverseMap)
