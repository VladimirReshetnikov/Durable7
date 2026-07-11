module Data.Structures.Hamt
  ( HashMap.HashMap
  , HashSet.HashSet
  , HashMap.HashPolicy(..)
  , HashMap.defaultPolicy
  , module Data.Structures.Hamt.Hashable
  , module Data.Structures.Hamt.Patricia
  ) where

-- Integer-specialized Patricia collections live in
-- Data.Structures.Hamt.Patricia to avoid colliding with the hash-map names.

import Data.Structures.Hamt.Hashable
import Data.Structures.Hamt.Patricia
import qualified Data.Structures.Hamt.HashMap as HashMap
import qualified Data.Structures.Hamt.HashSet as HashSet
