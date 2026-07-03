{-# LANGUAGE BangPatterns #-}

module Data.Structures.FingerTree.MeasuredRope
  ( MeasuredRope
  , emptyWith
  , singletonWith
  , fromListWith
  , toList
  , count
  , null
  , measure
  , prefixMeasure
  , append
  , index
  , setAt
  , insertAt
  , deleteAt
  , splitAt
  , slice
  , locateByMeasure
  , splitByMeasure
  ) where

import Prelude hiding (null, splitAt)

import qualified Prelude as P
import qualified Data.Structures.FingerTree.Rope as Rope

data MeasuredRope v a = MeasuredRope (a -> v) !(Rope.Rope a) !v

emptyWith :: Monoid v => (a -> v) -> MeasuredRope v a
emptyWith elementMeasure = MeasuredRope elementMeasure Rope.empty mempty

singletonWith :: Monoid v => (a -> v) -> a -> MeasuredRope v a
singletonWith elementMeasure value = fromListWith elementMeasure [value]

fromListWith :: Monoid v => (a -> v) -> [a] -> MeasuredRope v a
fromListWith elementMeasure values = MeasuredRope elementMeasure (Rope.fromList values) (foldMap elementMeasure values)

toList :: MeasuredRope v a -> [a]
toList (MeasuredRope _ rope _) = Rope.toList rope

count :: MeasuredRope v a -> Int
count (MeasuredRope _ rope _) = Rope.count rope

null :: MeasuredRope v a -> Bool
null rope = count rope == 0

measure :: MeasuredRope v a -> v
measure (MeasuredRope _ _ total) = total

prefixMeasure :: Monoid v => Int -> MeasuredRope v a -> Maybe v
prefixMeasure lengthValue measured@(MeasuredRope elementMeasure _ _)
  | lengthValue < 0 || lengthValue > count measured = Nothing
  | otherwise = Just (foldMap elementMeasure (take lengthValue (toList measured)))

append :: Monoid v => MeasuredRope v a -> MeasuredRope v a -> MeasuredRope v a
append (MeasuredRope elementMeasure left _) right =
  fromListWith elementMeasure (Rope.toList left ++ toList right)

index :: Int -> MeasuredRope v a -> Maybe a
index position (MeasuredRope _ rope _) = Rope.index position rope

setAt :: Monoid v => Int -> a -> MeasuredRope v a -> Maybe (MeasuredRope v a)
setAt position value (MeasuredRope elementMeasure rope _) =
  rebuild elementMeasure <$> Rope.setAt position value rope
  where
    rebuild f newRope = fromListWith f (Rope.toList newRope)

insertAt :: Monoid v => Int -> a -> MeasuredRope v a -> Maybe (MeasuredRope v a)
insertAt position value (MeasuredRope elementMeasure rope _) =
  rebuild elementMeasure <$> Rope.insertAt position value rope
  where
    rebuild f newRope = fromListWith f (Rope.toList newRope)

deleteAt :: Monoid v => Int -> MeasuredRope v a -> Maybe (MeasuredRope v a)
deleteAt position (MeasuredRope elementMeasure rope _) =
  rebuild elementMeasure <$> Rope.deleteAt position rope
  where
    rebuild f newRope = fromListWith f (Rope.toList newRope)

splitAt :: Monoid v => Int -> MeasuredRope v a -> Maybe (MeasuredRope v a, MeasuredRope v a)
splitAt position measured@(MeasuredRope elementMeasure _ _) =
  case Rope.splitAt position (ropeOf measured) of
    Just (left, right) -> Just (fromListWith elementMeasure (Rope.toList left), fromListWith elementMeasure (Rope.toList right))
    Nothing -> Nothing

slice :: Monoid v => Int -> Int -> MeasuredRope v a -> Maybe (MeasuredRope v a)
slice position lengthValue (MeasuredRope elementMeasure rope _) =
  rebuild elementMeasure <$> Rope.slice position lengthValue rope
  where
    rebuild f newRope = fromListWith f (Rope.toList newRope)

locateByMeasure :: Monoid v => (v -> Bool) -> MeasuredRope v a -> Maybe (Int, v, a)
locateByMeasure predicate (MeasuredRope elementMeasure rope _) = go 0 mempty (Rope.toList rope)
  where
    go _ _ [] = Nothing
    go !position !before (value : rest)
      | predicate after = Just (position, before, value)
      | otherwise = go (position + 1) after rest
      where
        after = before <> elementMeasure value

splitByMeasure :: Monoid v => (v -> Bool) -> MeasuredRope v a -> Maybe (MeasuredRope v a, a, MeasuredRope v a)
splitByMeasure predicate measured@(MeasuredRope elementMeasure _ _) =
  case locateByMeasure predicate measured of
    Nothing -> Nothing
    Just (position, _, _) ->
      case P.splitAt position (toList measured) of
        (before, value : after) -> Just (fromListWith elementMeasure before, value, fromListWith elementMeasure after)
        _ -> Nothing

ropeOf :: MeasuredRope v a -> Rope.Rope a
ropeOf (MeasuredRope _ rope _) = rope
