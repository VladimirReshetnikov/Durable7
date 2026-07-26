{-# LANGUAGE MultiParamTypeClasses #-}

-- | The measures the package ships: counting, elements, products, maxima and minima.
--
-- A measure must be a `Monoid`, because the tree combines cached measures in whatever grouping
-- its shape happens to have; a non-associative combine would make the answer depend on that
-- shape.
module Durable7.FingerTree.Measures
  ( Size(..)
  , Elem(..)
  , MeasurePair(..)
  , Maximum(..)
  , Minimum(..)
  ) where

import Durable7.FingerTree.Measured (Measured(..))

-- | Counts elements, making a measured tree indexable by position.
newtype Size = Size { getSize :: Int }
  deriving (Eq, Ord, Read, Show)

instance Semigroup Size where
  Size left <> Size right = Size (left + right)

instance Monoid Size where
  mempty = Size 0

-- | Wraps a value so it measures as one element.
newtype Elem a = Elem { getElem :: a }
  deriving (Eq, Ord, Read, Show)

instance Measured Size (Elem a) where
  measure _ = Size 1

-- | Two measures carried side by side over the same elements. The product of two monoids is a
-- monoid, so one tree can be searched by either component.
data MeasurePair a b = MeasurePair !a !b
  deriving (Eq, Ord, Read, Show)

instance (Semigroup a, Semigroup b) => Semigroup (MeasurePair a b) where
  MeasurePair a1 b1 <> MeasurePair a2 b2 = MeasurePair (a1 <> a2) (b1 <> b2)

instance (Monoid a, Monoid b) => Monoid (MeasurePair a b) where
  mempty = MeasurePair mempty mempty

-- | Tracks the largest element, making a measured tree a max-priority structure with a constant-
-- time peek. `Nothing` is the identity.
newtype Maximum a = Maximum { getMaximum :: Maybe a }
  deriving (Eq, Ord, Read, Show)

instance Ord a => Semigroup (Maximum a) where
  Maximum Nothing <> right = right
  left <> Maximum Nothing = left
  Maximum (Just left) <> Maximum (Just right) = Maximum (Just (max left right))

instance Ord a => Monoid (Maximum a) where
  mempty = Maximum Nothing

-- | Tracks the smallest element, the mirror image of `Maximum`.
newtype Minimum a = Minimum { getMinimum :: Maybe a }
  deriving (Eq, Ord, Read, Show)

instance Ord a => Semigroup (Minimum a) where
  Minimum Nothing <> right = right
  left <> Minimum Nothing = left
  Minimum (Just left) <> Minimum (Just right) = Minimum (Just (min left right))

instance Ord a => Monoid (Minimum a) where
  mempty = Minimum Nothing
