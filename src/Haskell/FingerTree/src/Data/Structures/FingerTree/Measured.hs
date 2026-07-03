{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE FlexibleInstances #-}
{-# LANGUAGE MultiParamTypeClasses #-}
{-# LANGUAGE UndecidableInstances #-}

module Data.Structures.FingerTree.Measured
  ( Measured(..)
  , FingerTree
  , Digit(..)
  , Node
  , ViewL(..)
  , ViewR(..)
  , empty
  , singleton
  , null
  , cons
  , snoc
  , append
  , fromList
  , toList
  , viewL
  , viewR
  , head
  , last
  , measureTree
  , split
  , locate
  ) where

import Prelude hiding (head, last, null)

class Monoid v => Measured v a where
  measure :: a -> v

data Digit a
  = One a
  | Two a a
  | Three a a a
  | Four a a a a
  deriving (Eq, Ord, Read, Show)

data Node v a
  = Node2 !v a a
  | Node3 !v a a a
  deriving (Eq, Ord, Read, Show)

data FingerTree v a
  = Empty
  | Single a
  | Deep !v !(Digit a) !(FingerTree v (Node v a)) !(Digit a)
  deriving (Eq, Ord, Read, Show)

data ViewL v a
  = EmptyL
  | a :< FingerTree v a
  deriving (Eq, Ord, Read, Show)

infixr 5 :<

data ViewR v a
  = EmptyR
  | FingerTree v a :> a
  deriving (Eq, Ord, Read, Show)

infixl 5 :>

instance Measured v a => Measured v (Node v a) where
  measure (Node2 value _ _) = value
  measure (Node3 value _ _ _) = value

empty :: FingerTree v a
empty = Empty

singleton :: a -> FingerTree v a
singleton = Single

null :: FingerTree v a -> Bool
null Empty = True
null _ = False

measureTree :: Measured v a => FingerTree v a -> v
measureTree Empty = mempty
measureTree (Single value) = measure value
measureTree (Deep value _ _ _) = value

cons :: Measured v a => a -> FingerTree v a -> FingerTree v a
cons value Empty = Single value
cons value (Single old) = deep (One value) Empty (One old)
cons value (Deep _ (Four a b c d) middle suffix) = deep (Two value a) (cons (node3 b c d) middle) suffix
cons value (Deep _ prefix middle suffix) = deep (consDigit value prefix) middle suffix

snoc :: Measured v a => FingerTree v a -> a -> FingerTree v a
snoc Empty value = Single value
snoc (Single old) value = deep (One old) Empty (One value)
snoc (Deep _ prefix middle (Four a b c d)) value = deep prefix (snoc middle (node3 a b c)) (Two d value)
snoc (Deep _ prefix middle suffix) value = deep prefix middle (snocDigit suffix value)

append :: Measured v a => FingerTree v a -> FingerTree v a -> FingerTree v a
append left right = app3 left [] right

fromList :: Measured v a => [a] -> FingerTree v a
fromList = foldl snoc Empty

toList :: Measured v a => FingerTree v a -> [a]
toList tree =
  case viewL tree of
    EmptyL -> []
    value :< rest -> value : toList rest

viewL :: Measured v a => FingerTree v a -> ViewL v a
viewL Empty = EmptyL
viewL (Single value) = value :< Empty
viewL (Deep _ prefix middle suffix) =
  case prefix of
    One value -> value :< deepL middle suffix
    Two a b -> a :< deep (One b) middle suffix
    Three a b c -> a :< deep (Two b c) middle suffix
    Four a b c d -> a :< deep (Three b c d) middle suffix

viewR :: Measured v a => FingerTree v a -> ViewR v a
viewR Empty = EmptyR
viewR (Single value) = Empty :> value
viewR (Deep _ prefix middle suffix) =
  case suffix of
    One value -> deepR prefix middle :> value
    Two a b -> deep prefix middle (One a) :> b
    Three a b c -> deep prefix middle (Two a b) :> c
    Four a b c d -> deep prefix middle (Three a b c) :> d

head :: Measured v a => FingerTree v a -> Maybe a
head tree =
  case viewL tree of
    EmptyL -> Nothing
    value :< _ -> Just value

last :: Measured v a => FingerTree v a -> Maybe a
last tree =
  case viewR tree of
    EmptyR -> Nothing
    _ :> value -> Just value

split :: Measured v a => (v -> Bool) -> FingerTree v a -> Maybe (FingerTree v a, a, FingerTree v a)
split predicate tree
  | null tree = Nothing
  | not (predicate (measureTree tree)) = Nothing
  | otherwise = Just (splitTree predicate mempty tree)

locate :: Measured v a => (v -> Bool) -> FingerTree v a -> Maybe (v, a)
locate predicate tree =
  case split predicate tree of
    Just (left, value, _) -> Just (measureTree left, value)
    Nothing -> Nothing

deep :: Measured v a => Digit a -> FingerTree v (Node v a) -> Digit a -> FingerTree v a
deep prefix middle suffix = Deep (measureDigit prefix <> measureTree middle <> measureDigit suffix) prefix middle suffix

deepL :: Measured v a => FingerTree v (Node v a) -> Digit a -> FingerTree v a
deepL middle suffix =
  case viewL middle of
    EmptyL -> digitToTree suffix
    node :< middle' -> deep (nodeToDigit node) middle' suffix

deepR :: Measured v a => Digit a -> FingerTree v (Node v a) -> FingerTree v a
deepR prefix middle =
  case viewR middle of
    EmptyR -> digitToTree prefix
    middle' :> node -> deep prefix middle' (nodeToDigit node)

app3 :: Measured v a => FingerTree v a -> [a] -> FingerTree v a -> FingerTree v a
app3 Empty values right = prependList values right
app3 left values Empty = appendList left values
app3 (Single value) values right = cons value (prependList values right)
app3 left values (Single value) = snoc (appendList left values) value
app3 (Deep _ prefix1 middle1 suffix1) values (Deep _ prefix2 middle2 suffix2) =
  deep prefix1 (app3 middle1 (nodes (digitToList suffix1 ++ values ++ digitToList prefix2)) middle2) suffix2

prependList :: Measured v a => [a] -> FingerTree v a -> FingerTree v a
prependList values tree = foldr cons tree values

appendList :: Measured v a => FingerTree v a -> [a] -> FingerTree v a
appendList tree values = foldl snoc tree values

nodes :: Measured v a => [a] -> [Node v a]
nodes values =
  case values of
    [a, b] -> [node2 a b]
    [a, b, c] -> [node3 a b c]
    [a, b, c, d] -> [node2 a b, node2 c d]
    [a, b, c, d, e] -> [node2 a b, node3 c d e]
    [a, b, c, d, e, f] -> [node3 a b c, node3 d e f]
    a : b : c : rest -> node3 a b c : nodes rest
    _ -> error "Data.Structures.FingerTree.Measured.nodes: fewer than two elements"

splitTree :: Measured v a => (v -> Bool) -> v -> FingerTree v a -> (FingerTree v a, a, FingerTree v a)
splitTree _ _ Empty = error "Data.Structures.FingerTree.Measured.splitTree: empty tree"
splitTree _ _ (Single value) = (Empty, value, Empty)
splitTree predicate accumulated (Deep _ prefix middle suffix)
  | predicate prefixMeasure =
      let (before, value, after) = splitDigit predicate accumulated prefix
       in (digitListToTree before, value, treeWithPrefix after middle suffix)
  | predicate middleMeasure =
      let (leftMiddle, node, rightMiddle) = splitTree predicate prefixMeasure middle
          (before, value, after) = splitDigit predicate (prefixMeasure <> measureTree leftMiddle) (nodeToDigit node)
       in (treeWithSuffix prefix leftMiddle before, value, treeWithPrefix after rightMiddle suffix)
  | otherwise =
      let (before, value, after) = splitDigit predicate middleMeasure suffix
       in (treeWithSuffix prefix middle before, value, digitListToTree after)
  where
    prefixMeasure = accumulated <> measureDigit prefix
    middleMeasure = prefixMeasure <> measureTree middle

splitDigit :: Measured v a => (v -> Bool) -> v -> Digit a -> ([a], a, [a])
splitDigit predicate accumulated digit = go accumulated [] (digitToList digit)
  where
    go _ _ [] = error "Data.Structures.FingerTree.Measured.splitDigit: predicate missed digit"
    go !measureBefore reversedBefore (value : rest)
      | predicate measureAfter = (reverse reversedBefore, value, rest)
      | otherwise = go measureAfter (value : reversedBefore) rest
      where
        measureAfter = measureBefore <> measure value

treeWithPrefix :: Measured v a => [a] -> FingerTree v (Node v a) -> Digit a -> FingerTree v a
treeWithPrefix [] middle suffix = deepL middle suffix
treeWithPrefix values middle suffix = deep (listToDigit values) middle suffix

treeWithSuffix :: Measured v a => Digit a -> FingerTree v (Node v a) -> [a] -> FingerTree v a
treeWithSuffix prefix middle [] = deepR prefix middle
treeWithSuffix prefix middle values = deep prefix middle (listToDigit values)

digitToTree :: Measured v a => Digit a -> FingerTree v a
digitToTree (One a) = Single a
digitToTree (Two a b) = deep (One a) Empty (One b)
digitToTree (Three a b c) = deep (Two a b) Empty (One c)
digitToTree (Four a b c d) = deep (Two a b) Empty (Two c d)

digitListToTree :: Measured v a => [a] -> FingerTree v a
digitListToTree [] = Empty
digitListToTree values = digitToTree (listToDigit values)

listToDigit :: [a] -> Digit a
listToDigit [a] = One a
listToDigit [a, b] = Two a b
listToDigit [a, b, c] = Three a b c
listToDigit [a, b, c, d] = Four a b c d
listToDigit _ = error "Data.Structures.FingerTree.Measured.listToDigit: invalid digit length"

digitToList :: Digit a -> [a]
digitToList (One a) = [a]
digitToList (Two a b) = [a, b]
digitToList (Three a b c) = [a, b, c]
digitToList (Four a b c d) = [a, b, c, d]

measureDigit :: Measured v a => Digit a -> v
measureDigit = foldMap measure . digitToList

consDigit :: a -> Digit a -> Digit a
consDigit value (One a) = Two value a
consDigit value (Two a b) = Three value a b
consDigit value (Three a b c) = Four value a b c
consDigit _ (Four _ _ _ _) = error "Data.Structures.FingerTree.Measured.consDigit: full digit"

snocDigit :: Digit a -> a -> Digit a
snocDigit (One a) value = Two a value
snocDigit (Two a b) value = Three a b value
snocDigit (Three a b c) value = Four a b c value
snocDigit (Four _ _ _ _) _ = error "Data.Structures.FingerTree.Measured.snocDigit: full digit"

node2 :: Measured v a => a -> a -> Node v a
node2 a b = Node2 (measure a <> measure b) a b

node3 :: Measured v a => a -> a -> a -> Node v a
node3 a b c = Node3 (measure a <> measure b <> measure c) a b c

nodeToDigit :: Node v a -> Digit a
nodeToDigit (Node2 _ a b) = Two a b
nodeToDigit (Node3 _ a b c) = Three a b c
