{-# LANGUAGE FlexibleInstances #-}

module Durable7.Hamt.Hashable
  ( Hashable(..)
  , combineHash
  ) where

import Data.Bits (shiftL, shiftR, xor)
import Data.Char (ord)

class Hashable a where
  hashWithSalt :: Int -> a -> Int

  hash :: a -> Int
  hash = hashWithSalt 0

combineHash :: Int -> Int -> Int
combineHash salt value = mix (salt `xor` (value + golden + (salt `shiftL` 6) + (salt `shiftR` 2)))
  where
    golden = 0x9e3779b9 :: Int

mix :: Int -> Int
mix x0 = x3
  where
    x1 = (x0 `xor` (x0 `shiftR` 16)) * 0x45d9f3b
    x2 = (x1 `xor` (x1 `shiftR` 16)) * 0x45d9f3b
    x3 = x2 `xor` (x2 `shiftR` 16)

instance Hashable () where
  hashWithSalt salt () = combineHash salt 0

instance Hashable Bool where
  hashWithSalt salt value = combineHash salt (if value then 1 else 0)

instance Hashable Char where
  hashWithSalt salt value = combineHash salt (ord value)

instance Hashable Int where
  hashWithSalt salt value = combineHash salt value

instance Hashable Integer where
  hashWithSalt salt value = combineHash salt (fromInteger value)

instance Hashable Ordering where
  hashWithSalt salt LT = combineHash salt 1
  hashWithSalt salt EQ = combineHash salt 2
  hashWithSalt salt GT = combineHash salt 3

instance Hashable a => Hashable (Maybe a) where
  hashWithSalt salt Nothing = combineHash salt 0x42108421
  hashWithSalt salt (Just value) = hashWithSalt (combineHash salt 0x42108422) value

instance (Hashable a, Hashable b) => Hashable (Either a b) where
  hashWithSalt salt (Left value) = hashWithSalt (combineHash salt 0x51f15e) value
  hashWithSalt salt (Right value) = hashWithSalt (combineHash salt 0x61919a) value

instance Hashable a => Hashable [a] where
  hashWithSalt salt values = foldl' hashWithSalt (combineHash salt 0x345678) values

instance (Hashable a, Hashable b) => Hashable (a, b) where
  hashWithSalt salt (a, b) = hashWithSalt (hashWithSalt (combineHash salt 2) a) b

instance (Hashable a, Hashable b, Hashable c) => Hashable (a, b, c) where
  hashWithSalt salt (a, b, c) = hashWithSalt (hashWithSalt (hashWithSalt (combineHash salt 3) a) b) c
