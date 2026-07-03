{-# LANGUAGE MultiParamTypeClasses #-}

module Data.Structures.FingerTree.Measures
  ( Size(..)
  , Elem(..)
  , MeasurePair(..)
  , Maximum(..)
  , Minimum(..)
  ) where

import Data.Structures.FingerTree.Measured (Measured(..))

newtype Size = Size { getSize :: Int }
  deriving (Eq, Ord, Read, Show)

instance Semigroup Size where
  Size left <> Size right = Size (left + right)

instance Monoid Size where
  mempty = Size 0

newtype Elem a = Elem { getElem :: a }
  deriving (Eq, Ord, Read, Show)

instance Measured Size (Elem a) where
  measure _ = Size 1

data MeasurePair a b = MeasurePair !a !b
  deriving (Eq, Ord, Read, Show)

instance (Semigroup a, Semigroup b) => Semigroup (MeasurePair a b) where
  MeasurePair a1 b1 <> MeasurePair a2 b2 = MeasurePair (a1 <> a2) (b1 <> b2)

instance (Monoid a, Monoid b) => Monoid (MeasurePair a b) where
  mempty = MeasurePair mempty mempty

newtype Maximum a = Maximum { getMaximum :: Maybe a }
  deriving (Eq, Ord, Read, Show)

instance Ord a => Semigroup (Maximum a) where
  Maximum Nothing <> right = right
  left <> Maximum Nothing = left
  Maximum (Just left) <> Maximum (Just right) = Maximum (Just (max left right))

instance Ord a => Monoid (Maximum a) where
  mempty = Maximum Nothing

newtype Minimum a = Minimum { getMinimum :: Maybe a }
  deriving (Eq, Ord, Read, Show)

instance Ord a => Semigroup (Minimum a) where
  Minimum Nothing <> right = right
  left <> Minimum Nothing = left
  Minimum (Just left) <> Minimum (Just right) = Minimum (Just (min left right))

instance Ord a => Monoid (Minimum a) where
  mempty = Minimum Nothing
