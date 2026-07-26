{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE MagicHash #-}

-- | A persistent multiset: hashed elements with multiplicities.
--
-- Multiplicity is stored per distinct element, so a large count costs no more than a small one.
-- Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
-- so an edit copies a path rather than the whole collection.
module Durable7.Hamt.HashBag
  ( HashBag
  , HashBagError(..)
  , empty
  , emptyWith
  , fromList
  , fromListWith
  , distinctCount
  , totalCount
  , null
  , clear
  , policy
  , member
  , countOf
  , actualValue
  , add
  , addCopies
  , remove
  , removeCopies
  , removeAll
  , union
  , intersection
  , difference
  , sum
  , distinctItems
  , entries
  , toList
  , sharesRootWith
  , validStructure
  ) where

import Prelude hiding (null, sum)

import Control.Monad (foldM)
import Data.Int (Int32, Int64)
import qualified Data.List as List
import GHC.Exts (isTrue#, reallyUnsafePtrEquality#)

import Durable7.Hamt.Hashable (Hashable)
import Durable7.Hamt.HashMap (HashMap, HashPolicy(..))
import qualified Durable7.Hamt.HashMap as HashMap

-- | Checked hash-bag update failure. No operation returning 'Left' publishes
-- a partially constructed bag.
data HashBagError
  = NegativeCopies !Int32
  | MultiplicityOverflow
  | TotalCountOverflow
  deriving (Eq, Show)

-- | Immutable unordered multiset with one positive 'Int32' multiplicity per
-- receiver-policy equivalence class and an expanded 'Int64' total.
data HashBag k = HashBag !(HashMap k Int32) !Int64

-- | The empty bag.
empty :: (Eq k, Hashable k) => HashBag k
empty = emptyWith HashMap.defaultPolicy

-- | The empty bag under the given policy, which it retains.
emptyWith :: HashPolicy k -> HashBag k
emptyWith hashPolicy = HashBag (HashMap.emptyWith hashPolicy) 0

-- | Aggregates occurrences in input order and retains the first equivalent
-- representative. Per-class and total additions are checked.
fromList :: (Eq k, Hashable k) => [k] -> Either HashBagError (HashBag k)
fromList = fromListWith HashMap.defaultPolicy

-- | A bag holding a list's occurrences, under the given policy.
fromListWith :: HashPolicy k -> [k] -> Either HashBagError (HashBag k)
fromListWith hashPolicy = foldM (flip add) (emptyWith hashPolicy)

-- | How many distinct elements are present, ignoring multiplicity.
distinctCount :: HashBag k -> Int
distinctCount (HashBag counts _) = HashMap.size counts

-- | The total multiplicity across all elements.
totalCount :: HashBag k -> Int64
totalCount (HashBag _ total) = total

-- | Whether the bag holds no occurrences.
null :: HashBag k -> Bool
null (HashBag counts _) = HashMap.null counts

-- | The empty bag, retaining the same policies.
clear :: HashBag k -> HashBag k
clear bagValue@(HashBag counts _) = withCounts bagValue (HashMap.clear counts) 0

-- | The policy the bag retains.
policy :: HashBag k -> HashPolicy k
policy (HashBag counts _) = HashMap.policy counts

-- | Whether the occurrence is present.
member :: k -> HashBag k -> Bool
member item (HashBag counts _) = HashMap.member item counts

-- | How many times the occurrence occurs.
countOf :: k -> HashBag k -> Int32
countOf item (HashBag counts _) = maybe 0 id (HashMap.lookup item counts)

-- | Returns the retained representative equivalent to the lookup value.
actualValue :: k -> HashBag k -> Maybe k
actualValue item (HashBag counts _) = HashMap.actualKey item counts

-- | A bag containing the given occurrence.
add :: k -> HashBag k -> Either HashBagError (HashBag k)
add item = addCopies item 1

-- | Adds a nonnegative number of occurrences. Negative and zero requests are
-- decided before hashing; a positive request uses one fallible map-factory
-- descent and retains an existing representative.
addCopies :: k -> Int32 -> HashBag k -> Either HashBagError (HashBag k)
addCopies item copies bagValue@(HashBag counts total)
  | copies < 0 = Left (NegativeCopies copies)
  | copies == 0 = Right bagValue
  | otherwise = do
      nextTotal <- checkedAddTotal total (fromIntegral copies)
      (nextCounts, _) <- HashMap.addOrUpdateEither
        item
        (\_ -> Right copies)
        (\_ stored -> checkedAddMultiplicity stored copies)
        counts
      pure (withCounts bagValue nextCounts nextTotal)

-- | A bag without that occurrence.
remove :: k -> HashBag k -> HashBag k
remove item = removeCopiesPositive item 1

-- | Removes at most the requested multiplicity. Subtraction saturates at zero
-- and deletes a class whose remaining count is zero.
removeCopies :: k -> Int32 -> HashBag k -> Either HashBagError (HashBag k)
removeCopies item copies bagValue
  | copies < 0 = Left (NegativeCopies copies)
  | copies == 0 = Right bagValue
  | otherwise = Right (removeCopiesPositive item copies bagValue)

removeCopiesPositive :: k -> Int32 -> HashBag k -> HashBag k
removeCopiesPositive item copies bagValue@(HashBag counts total) =
  case HashMap.lookup item counts of
    Nothing -> bagValue
    Just stored
      | copies >= stored ->
          case HashMap.tryRemove item counts of
            Just (removedCount, nextCounts) ->
              withCounts bagValue nextCounts (total - fromIntegral removedCount)
            Nothing -> error "Durable7.Hamt.HashBag: located class could not be removed"
      | otherwise ->
          withCounts
            bagValue
            (HashMap.insert item (stored - copies) counts)
            (total - fromIntegral copies)

-- | A bag without any occurrence of the occurrence.
removeAll :: k -> HashBag k -> HashBag k
removeAll item bagValue@(HashBag counts total) =
  case HashMap.tryRemove item counts of
    Nothing -> bagValue
    Just (removedCount, nextCounts) ->
      withCounts bagValue nextCounts (total - fromIntegral removedCount)

-- | Multiset union under the receiver policy: maximum multiplicity per class.
union :: HashBag k -> HashBag k -> Either HashBagError (HashBag k)
union receiver@(HashBag receiverCounts _) argument = do
  normalized <- normalizeArgument receiver argument
  let HashBag argumentCounts _ = normalized
  if sameVersion receiver normalized
    then Right receiver
    else do
      let nextCounts = List.foldl' addMaximum receiverCounts (HashMap.toList argumentCounts)
      nextTotal <- totalFromCounts nextCounts
      pure (withCounts receiver nextCounts nextTotal)
  where
    addMaximum counts (item, argumentCount) =
      fst (HashMap.addOrUpdate
        item
        (const argumentCount)
        (\_ receiverCount -> max receiverCount argumentCount)
        counts)

-- | Multiset intersection under the receiver policy: minimum multiplicity.
intersection :: HashBag k -> HashBag k -> Either HashBagError (HashBag k)
intersection receiver@(HashBag receiverCounts _) argument = do
  normalized <- normalizeArgument receiver argument
  let HashBag argumentCounts _ = normalized
  if sameVersion receiver normalized
    then Right receiver
    else do
      let nextCounts = List.foldl' keepMinimum receiverCounts (HashMap.toList receiverCounts)
          keepMinimum counts (item, receiverCount) =
            case HashMap.lookup item argumentCounts of
              Nothing -> HashMap.delete item counts
              Just argumentCount
                | argumentCount < receiverCount -> HashMap.insert item argumentCount counts
                | otherwise -> counts
      nextTotal <- totalFromCounts nextCounts
      pure (withCounts receiver nextCounts nextTotal)

-- | Saturating receiver-minus-argument multiset subtraction.
difference :: HashBag k -> HashBag k -> Either HashBagError (HashBag k)
difference receiver@(HashBag receiverCounts _) argument = do
  normalized <- normalizeArgument receiver argument
  let HashBag argumentCounts _ = normalized
  if sameVersion receiver normalized
    then Right (clear receiver)
    else do
      let nextCounts = List.foldl' subtractCount receiverCounts (HashMap.toList receiverCounts)
          subtractCount counts (item, receiverCount) =
            case HashMap.lookup item argumentCounts of
              Nothing -> counts
              Just argumentCount
                | argumentCount >= receiverCount -> HashMap.delete item counts
                | otherwise -> HashMap.insert item (receiverCount - argumentCount) counts
      nextTotal <- totalFromCounts nextCounts
      pure (withCounts receiver nextCounts nextTotal)

-- | Checked additive multiset sum under the receiver policy.
sum :: HashBag k -> HashBag k -> Either HashBagError (HashBag k)
sum receiver argument = do
  normalized <- normalizeArgument receiver argument
  foldM addEntry receiver (entries normalized)
  where
    addEntry bagValue (item, copies) = addCopies item copies bagValue

-- | The distinct elements, ignoring multiplicity.
distinctItems :: HashBag k -> [k]
distinctItems (HashBag counts _) = HashMap.keys counts

-- | The entries.
entries :: HashBag k -> [(k, Int32)]
entries (HashBag counts _) = HashMap.toList counts

-- | Expanded occurrence enumeration. Copies of one representative are
-- contiguous and distinct classes follow the underlying CHAMP order.
toList :: HashBag k -> [k]
toList = concatMap expand . entries
  where
    expand (item, copies) = replicate (fromIntegral copies) item

-- | Whether both versions reference the same root node. A representation check used to confirm a
-- no-op avoided copying, not an equality test.
sharesRootWith :: HashBag k -> HashBag k -> Bool
sharesRootWith (HashBag left _) (HashBag right _) = HashMap.sharesRootWith left right

-- | Whether the bag's structural invariants hold. For tests and diagnostics.
validStructure :: HashBag k -> Bool
validStructure (HashBag counts total) =
  HashMap.validStructure counts
    && all ((> 0) . snd) (HashMap.toList counts)
    && totalFromCounts counts == Right total

normalizeArgument :: HashBag k -> HashBag k -> Either HashBagError (HashBag k)
normalizeArgument receiver argument
  | policiesShareFunctions (policy receiver) (policy argument) = Right argument
  | otherwise = foldM addEntry (emptyWith (policy receiver)) (entries argument)
  where
    addEntry bagValue (item, copies) = addCopies item copies bagValue

sameVersion :: HashBag k -> HashBag k -> Bool
sameVersion left right =
  totalCount left == totalCount right
    && policiesShareFunctions (policy left) (policy right)
    && sharesRootWith left right

withCounts :: HashBag k -> HashMap k Int32 -> Int64 -> HashBag k
withCounts bagValue@(HashBag oldCounts oldTotal) nextCounts nextTotal
  | HashMap.sharesRootWith oldCounts nextCounts =
      if oldTotal == nextTotal
        then bagValue
        else error "Durable7.Hamt.HashBag: unchanged root has a different total"
  | otherwise = HashBag nextCounts nextTotal

checkedAddMultiplicity :: Int32 -> Int32 -> Either HashBagError Int32
checkedAddMultiplicity left right
  | right > 0 && left > maxBound - right = Left MultiplicityOverflow
  | otherwise = Right (left + right)

checkedAddTotal :: Int64 -> Int64 -> Either HashBagError Int64
checkedAddTotal left right
  | right > 0 && left > maxBound - right = Left TotalCountOverflow
  | otherwise = Right (left + right)

totalFromCounts :: HashMap k Int32 -> Either HashBagError Int64
totalFromCounts = foldM addCount 0 . HashMap.elems
  where
    addCount total copies = checkedAddTotal total (fromIntegral copies)

policiesShareFunctions :: HashPolicy k -> HashPolicy k -> Bool
policiesShareFunctions left right =
  ptrEq left right
    || (ptrEq (hashKey left) (hashKey right) && ptrEq (equalKeys left) (equalKeys right))

ptrEq :: a -> a -> Bool
ptrEq left right = isTrue# (reallyUnsafePtrEquality# left right)
{-# INLINE ptrEq #-}
