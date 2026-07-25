{-# LANGUAGE BangPatterns #-}

-- | An immutable policy-canonical sorted set backed by a zip-zip tree.
--
-- A policy derives a deterministic pseudorandom rank from an equivalence-class
-- hash.  Consequently, one coherent policy and one mathematical set determine
-- one tree topology, independently of insertion order.  Policy construction is
-- in 'IO': besides obtaining entropy for random policies, this allocates an
-- opaque identity used to gate canonical algebra honestly in a language where
-- functions and ordinary immutable records do not have observable identity.
module Durable7.FingerTree.CanonicalSortedSet
  ( ZipTreeRankPolicy
  , ZipTreeRank(..)
  , ZipTreePolicyError(..)
  , CanonicalSetError(..)
  , CanonicalInvariantError(..)
  , CanonicalSortedSet
  , CanonicalSortedSetStatistics(..)
  , CanonicalNodeShape(..)
  , newSeededPolicy
  , newSeededPolicyBy
  , newKeyedPolicy
  , newKeyedPolicyBy
  , newRandomPolicy
  , newRandomPolicyBy
  , policySeed
  , rankPolicy
  , rankFor
  , empty
  , singleton
  , fromList
  , toAscList
  , count
  , null
  , height
  , contentHash
  , contains
  , lookupEquivalent
  , insert
  , delete
  , clear
  , union
  , intersection
  , difference
  , setEquals
  , setEqualsList
  , isSubsetOf
  , isProperSubsetOf
  , isSupersetOf
  , isProperSupersetOf
  , overlaps
  , validateStructure
  , shapeForTesting
  , sharesRootWith
  , sharedNodeCount
  , cursorBoundRank
  , cursorIndex
  ) where

import Prelude hiding (null)
import qualified Prelude as P

import Control.Monad (foldM)
import Crypto.Hash (Digest, SHA256, hash)
import Crypto.MAC.HMAC (HMAC, hmac)
import Crypto.Random (getRandomBytes)
import qualified Data.ByteArray as ByteArray
import Data.ByteString (ByteString)
import qualified Data.ByteString as ByteString
import Data.Bits (countLeadingZeros, rotateL, shiftR, xor)
import qualified Data.IntMap.Strict as IntMap
import qualified Data.List as List
import qualified Data.Set as Set
import Data.Unique (Unique, newUnique)
import Data.Word (Word64, Word8)
import System.Mem.StableName (eqStableName, hashStableName, makeStableName)

minimumRankKeyBytes :: Int
minimumRankKeyBytes = 32

rankKeyBytes :: Int
rankKeyBytes = 32

leftDigestSeed :: Word64
leftDigestSeed = 0x243f6a8885a308d3

rightDigestSeed :: Word64
rightDigestSeed = 0x13198a2e03707344

-- | The three words that determine a node's heap priority and digest input.
--
-- 'geometricRank' is the leading-zero count of the first HMAC word.
-- 'secondaryRank' and 'contentWord' are the next two big-endian HMAC words.
-- 'Word64' ordering is unsigned, matching the C# reference implementation.
data ZipTreeRank = ZipTreeRank
  { geometricRank :: !Int
  , secondaryRank :: !Word64
  , contentWord :: !Word64
  }
  deriving (Eq, Show)

-- | Failure to construct a caller-keyed policy.
data ZipTreePolicyError
  = RankKeyTooShort !Int
  deriving (Eq, Show)

-- | A caller contract failure encountered by a persistent set operation.
data CanonicalSetError
  = InconsistentRankHash
  | IncompatibleRankPolicy
  deriving (Eq, Show)

-- | A structural invariant failure found by 'validateStructure'.
data CanonicalInvariantError
  = KeyCrossesLowerBound
  | KeyCrossesUpperBound
  | NonReproducibleRank
  | LeftChildOutranksParent
  | RightChildOutranksParent
  | InvalidCachedMetadata
  | InvalidCachedDigest
  | InvalidRootMetadata
  deriving (Eq, Show)

-- | Statistics returned by an independent structural validation pass.
data CanonicalSortedSetStatistics = CanonicalSortedSetStatistics
  { statisticsCount :: !Int
  , statisticsHeight :: !Int
  , statisticsMaximumGeometricRank :: !Int
  , statisticsPriorityCollisionCount :: !Int
  }
  deriving (Eq, Show)

-- | Pre-order topology diagnostics used by cross-port and canonicality tests.
data CanonicalNodeShape a = CanonicalNodeShape
  { shapeItem :: a
  , shapeLeftCount :: !Int
  , shapeRightCount :: !Int
  }
  deriving (Eq, Show)

-- The constructor stays private so callers cannot forge policy-family identity
-- or inspect retained secret key bytes.
data ZipTreeRankPolicy a = ZipTreeRankPolicy
  { policyCompare :: a -> a -> Ordering
  , policyRankHash :: a -> Word64
  , policyRankKey :: !ByteString
  , policyPublicSeed :: !(Maybe Word64)
  , policyIdentity :: !Unique
  }

data CanonicalSortedSet a = CanonicalSortedSet
  { setPolicy :: !(ZipTreeRankPolicy a)
  , setRoot :: !(Maybe (Node a))
  }

data Node a = Node
  { nodeItem :: !a
  , nodeRank :: !ZipTreeRank
  , nodeLeft :: !(Maybe (Node a))
  , nodeRight :: !(Maybe (Node a))
  , nodeCount :: !Int
  , nodeHeight :: !Int
  , nodeDigest :: !Word64
  }

data OpenNode a = OpenNode !a !ZipTreeRank !(Maybe (Node a))

data PathStep a = PathStep !(Node a) !Bool

data MergeStep a = MergeStep !(Node a) !Bool

data ValidationFrame a = ValidationFrame !(Node a) !(Maybe a) !(Maybe a) !Int

instance Show a => Show (CanonicalSortedSet a) where
  showsPrec precedence set = showsPrec precedence (toAscList set)

-- | Creates a public-seed policy using natural ordering.
--
-- Policy construction is in 'IO' solely to allocate an opaque algebra-family
-- identity.  Ranks, shapes, and digests remain reproducible from the seed and
-- hash function.
newSeededPolicy :: Ord a => (a -> Word64) -> Word64 -> IO (ZipTreeRankPolicy a)
newSeededPolicy = newSeededPolicyBy compare

-- | Creates a public-seed policy using an explicit equivalence/order function.
newSeededPolicyBy
  :: (a -> a -> Ordering)
  -> (a -> Word64)
  -> Word64
  -> IO (ZipTreeRankPolicy a)
newSeededPolicyBy compareItems rankHash seed = do
  identity <- newUnique
  let !key = sha256 (ByteString.append zzt2Prefix (word64BigEndian seed))
  pure (ZipTreeRankPolicy compareItems rankHash key (Just seed) identity)

-- | Creates a caller-keyed policy using natural ordering.
--
-- The full immutable key is copied and retained.  Keys shorter than 32 bytes
-- are rejected.
newKeyedPolicy
  :: Ord a
  => (a -> Word64)
  -> ByteString
  -> IO (Either ZipTreePolicyError (ZipTreeRankPolicy a))
newKeyedPolicy = newKeyedPolicyBy compare

-- | Creates a caller-keyed policy using an explicit equivalence/order function.
newKeyedPolicyBy
  :: (a -> a -> Ordering)
  -> (a -> Word64)
  -> ByteString
  -> IO (Either ZipTreePolicyError (ZipTreeRankPolicy a))
newKeyedPolicyBy compareItems rankHash key
  | ByteString.length key < minimumRankKeyBytes =
      pure (Left (RankKeyTooShort (ByteString.length key)))
  | otherwise = do
      identity <- newUnique
      let !ownedKey = ByteString.copy key
      pure (Right (ZipTreeRankPolicy compareItems rankHash ownedKey Nothing identity))

-- | Creates a fresh hidden-key policy using natural ordering.
newRandomPolicy :: Ord a => (a -> Word64) -> IO (ZipTreeRankPolicy a)
newRandomPolicy = newRandomPolicyBy compare

-- | Creates a fresh hidden-key policy using an explicit order function.
newRandomPolicyBy
  :: (a -> a -> Ordering)
  -> (a -> Word64)
  -> IO (ZipTreeRankPolicy a)
newRandomPolicyBy compareItems rankHash = do
  key <- getRandomBytes rankKeyBytes :: IO ByteString
  identity <- newUnique
  pure (ZipTreeRankPolicy compareItems rankHash key Nothing identity)

-- | Returns the public reproducibility seed, if this is a seeded policy.
policySeed :: ZipTreeRankPolicy a -> Maybe Word64
policySeed = policyPublicSeed

-- | Returns the retained policy of a set version.
rankPolicy :: CanonicalSortedSet a -> ZipTreeRankPolicy a
rankPolicy = setPolicy

-- | Derives the exact HMAC-SHA-256 rank for an item.
rankFor :: ZipTreeRankPolicy a -> a -> ZipTreeRank
rankFor policy item =
  let !source = word64BigEndian (policyRankHash policy item)
      !digest = hmacSha256 (policyRankKey policy) source
      !primary = word64At 0 digest
      !secondary = word64At 8 digest
      !content = word64At 16 digest
  in ZipTreeRank (countLeadingZeros primary) secondary content

-- | Population rank of the lower or upper policy-order bound.
cursorBoundRank :: Bool -> a -> CanonicalSortedSet a -> Int
cursorBoundRank upper item (CanonicalSortedSet policy root) = go 0 root
  where
    go !rank Nothing = rank
    go !rank (Just node) =
      case policyCompare policy (nodeItem node) item of
        LT -> go (rank + maybe 0 nodeCount (nodeLeft node) + 1) (nodeRight node)
        EQ | upper -> go (rank + maybe 0 nodeCount (nodeLeft node) + 1) (nodeRight node)
        _ -> go rank (nodeLeft node)

-- | Item at a zero-based policy-order population rank.
cursorIndex :: Int -> CanonicalSortedSet a -> Maybe a
cursorIndex position (CanonicalSortedSet _ root)
  | position < 0 = Nothing
  | otherwise = go position root
  where
    go _ Nothing = Nothing
    go remaining (Just node)
      | remaining < leftCount = go remaining (nodeLeft node)
      | remaining == leftCount = Just (nodeItem node)
      | otherwise = go (remaining - leftCount - 1) (nodeRight node)
      where
        leftCount = maybe 0 nodeCount (nodeLeft node)

-- | Creates an empty set retaining the supplied policy.
empty :: ZipTreeRankPolicy a -> CanonicalSortedSet a
empty policy = CanonicalSortedSet policy Nothing

-- | Creates a singleton set retaining the supplied policy.
singleton :: ZipTreeRankPolicy a -> a -> CanonicalSortedSet a
singleton policy item =
  CanonicalSortedSet policy (Just (mkNode item (rankFor policy item) Nothing Nothing))

-- | Builds a canonical set in O(n log n) sorting time and O(n) tree-building
-- time.  Stable sorting preserves the first representative of each equivalence
-- class.
fromList
  :: ZipTreeRankPolicy a
  -> [a]
  -> Either CanonicalSetError (CanonicalSortedSet a)
fromList policy items = do
  unique <- uniqueRanked policy (List.sortBy (policyCompare policy) items)
  let !root = buildCanonical (policyCompare policy) unique
  pure (CanonicalSortedSet policy root)

-- | Returns items in policy order.
toAscList :: CanonicalSortedSet a -> [a]
toAscList (CanonicalSortedSet _ root) = List.reverse (walk root [] [])
  where
    walk !cursor !pending !result =
      case cursor of
        Just node -> walk (nodeLeft node) (node : pending) result
        Nothing ->
          case pending of
            [] -> result
            node : rest -> walk (nodeRight node) rest (nodeItem node : result)

-- | Number of represented equivalence classes.
count :: CanonicalSortedSet a -> Int
count (CanonicalSortedSet _ root) = maybe 0 nodeCount root

-- | Whether the set is empty.
null :: CanonicalSortedSet a -> Bool
null (CanonicalSortedSet _ root) =
  case root of
    Nothing -> True
    Just _ -> False

-- | Cached tree height, primarily for diagnostics.
height :: CanonicalSortedSet a -> Int
height (CanonicalSortedSet _ root) = maybe 0 nodeHeight root

-- | Cached non-cryptographic canonical-tree digest, or zero for an empty set.
-- Digest inequality proves inequality only within one coherent policy family.
contentHash :: CanonicalSortedSet a -> Word64
contentHash (CanonicalSortedSet _ root) = maybe 0 nodeDigest root

-- | Whether an equivalent item is present.
contains :: a -> CanonicalSortedSet a -> Bool
contains item set =
  case findNode item set of
    Nothing -> False
    Just _ -> True

-- | Recovers the first stored representative equivalent to the probe.
lookupEquivalent :: a -> CanonicalSortedSet a -> Maybe a
lookupEquivalent item set = nodeItem <$> findNode item set

-- | Adds an item.  An equivalent existing representative and the current root
-- are retained.  An incoherent rank hash is reported instead of silently
-- breaking canonicality.
insert
  :: a
  -> CanonicalSortedSet a
  -> Either CanonicalSetError (CanonicalSortedSet a)
insert item set@(CanonicalSortedSet policy root) =
  let !rank = rankFor policy item
  in case findNode item set of
       Just existing
         | nodeRank existing == rank -> Right set
         | otherwise -> Left InconsistentRankHash
       Nothing ->
         let !newRoot = insertNode (policyCompare policy) item rank root
         in Right (CanonicalSortedSet policy (Just newRoot))

-- | Removes an equivalent item, retaining the current root when absent.
delete :: a -> CanonicalSortedSet a -> CanonicalSortedSet a
delete item set@(CanonicalSortedSet policy root) =
  case removeNode (policyCompare policy) item root of
    Nothing -> set
    Just updated -> CanonicalSortedSet policy updated

-- | Returns an empty set retaining the policy.
clear :: CanonicalSortedSet a -> CanonicalSortedSet a
clear set@(CanonicalSortedSet _ Nothing) = set
clear (CanonicalSortedSet policy _) = empty policy

-- | Canonical union.  Both operands must descend from the same policy value.
union
  :: CanonicalSortedSet a
  -> CanonicalSortedSet a
  -> Either CanonicalSetError (CanonicalSortedSet a)
union left right
  | not (samePolicy left right) = Left IncompatibleRankPolicy
  | otherwise = foldInsert left (toAscList right)

-- | Canonical intersection.  Representatives come from the left operand.
intersection
  :: CanonicalSortedSet a
  -> CanonicalSortedSet a
  -> Either CanonicalSetError (CanonicalSortedSet a)
intersection left right
  | not (samePolicy left right) = Left IncompatibleRankPolicy
  | otherwise = foldInsert (empty (setPolicy left)) retained
  where
    retained = List.filter (`contains` right) (toAscList left)

-- | Canonical left difference.  Both operands must share policy identity.
difference
  :: CanonicalSortedSet a
  -> CanonicalSortedSet a
  -> Either CanonicalSetError (CanonicalSortedSet a)
difference left right
  | not (samePolicy left right) = Left IncompatibleRankPolicy
  | otherwise = Right (List.foldl' (flip delete) left (toAscList right))

-- | Mathematical set equality under the left operand's equivalence policy.
-- Same-policy operands additionally use canonical count/digest rejection and
-- lockstep topology comparison.
setEquals :: CanonicalSortedSet a -> CanonicalSortedSet a -> Bool
setEquals left right
  | samePolicy left right =
      count left == count right
        && contentHash left == contentHash right
        && nodesEquivalent (policyCompare (setPolicy left)) (setRoot left) (setRoot right)
  | otherwise = setEqualsList left (toAscList right)

-- | Mathematical equality against an ordinary list, deduplicated under the
-- set's receiver-defined policy.
setEqualsList :: CanonicalSortedSet a -> [a] -> Bool
setEqualsList set values =
  equivalentLists compareItems (toAscList set) (semanticUnique compareItems values)
  where
    compareItems = policyCompare (setPolicy set)

-- | Receiver-policy subset relation.
isSubsetOf :: CanonicalSortedSet a -> CanonicalSortedSet a -> Bool
isSubsetOf left right =
  orderedSubset compareItems (toAscList left) (semanticUnique compareItems (toAscList right))
  where
    compareItems = policyCompare (setPolicy left)

-- | Receiver-policy proper-subset relation.
isProperSubsetOf :: CanonicalSortedSet a -> CanonicalSortedSet a -> Bool
isProperSubsetOf left right =
  P.length leftValues < P.length rightValues
    && orderedSubset compareItems leftValues rightValues
  where
    compareItems = policyCompare (setPolicy left)
    leftValues = toAscList left
    rightValues = semanticUnique compareItems (toAscList right)

-- | Receiver-policy superset relation.
isSupersetOf :: CanonicalSortedSet a -> CanonicalSortedSet a -> Bool
isSupersetOf left right = orderedSubset compareItems rightValues (toAscList left)
  where
    compareItems = policyCompare (setPolicy left)
    rightValues = semanticUnique compareItems (toAscList right)

-- | Receiver-policy proper-superset relation.
isProperSupersetOf :: CanonicalSortedSet a -> CanonicalSortedSet a -> Bool
isProperSupersetOf left right =
  P.length leftValues > P.length rightValues
    && orderedSubset compareItems rightValues leftValues
  where
    compareItems = policyCompare (setPolicy left)
    leftValues = toAscList left
    rightValues = semanticUnique compareItems (toAscList right)

-- | Whether the mathematical sets overlap under the left policy.
overlaps :: CanonicalSortedSet a -> CanonicalSortedSet a -> Bool
overlaps left right = orderedOverlaps compareItems (toAscList left) rightValues
  where
    compareItems = policyCompare (setPolicy left)
    rightValues = semanticUnique compareItems (toAscList right)

-- | Independently validates ordering, heap rank, rank reproducibility, cached
-- metadata, cached digest, and root statistics.  The traversal is iterative and
-- remains stack-safe for a completely colliding rank function.
validateStructure
  :: CanonicalSortedSet a
  -> Either CanonicalInvariantError CanonicalSortedSetStatistics
validateStructure (CanonicalSortedSet _ Nothing) =
  Right (CanonicalSortedSetStatistics 0 0 0 0)
validateStructure set@(CanonicalSortedSet policy (Just root)) =
  walk [ValidationFrame root Nothing Nothing 1] Set.empty 0 0 0 0
  where
    compareItems = policyCompare policy

    walk !pending !priorities !seenCount !maximumDepth !maximumRank !collisions =
      case pending of
        []
          | seenCount /= count set || maximumDepth /= height set -> Left InvalidRootMetadata
          | otherwise ->
              Right
                (CanonicalSortedSetStatistics
                  seenCount
                  maximumDepth
                  maximumRank
                  collisions)
        ValidationFrame node lower upper depth : rest
          | maybe False (\bound -> compareItems (nodeItem node) bound /= GT) lower -> Left KeyCrossesLowerBound
          | maybe False (\bound -> compareItems (nodeItem node) bound /= LT) upper -> Left KeyCrossesUpperBound
          | nodeRank node /= rankFor policy (nodeItem node) -> Left NonReproducibleRank
          | maybe False (not . higherNode compareItems node) (nodeLeft node) -> Left LeftChildOutranksParent
          | maybe False (not . higherNode compareItems node) (nodeRight node) -> Left RightChildOutranksParent
          | nodeCount node /= expectedCount || nodeHeight node /= expectedHeight -> Left InvalidCachedMetadata
          | nodeDigest node /= expectedDigest -> Left InvalidCachedDigest
          | otherwise ->
              let !priority = (geometricRank (nodeRank node), secondaryRank (nodeRank node))
                  !alreadySeen = Set.member priority priorities
                  !updatedPriorities = Set.insert priority priorities
                  !updatedPending = pushChildren node lower upper depth rest
              in walk
                   updatedPending
                   updatedPriorities
                   (seenCount + 1)
                   (max maximumDepth depth)
                   (max maximumRank (geometricRank (nodeRank node)))
                   (if alreadySeen then collisions + 1 else collisions)
          where
            !expectedCount = 1 + maybe 0 nodeCount (nodeLeft node) + maybe 0 nodeCount (nodeRight node)
            !expectedHeight = 1 + max (maybe 0 nodeHeight (nodeLeft node)) (maybe 0 nodeHeight (nodeRight node))
            !expectedDigest = digestFor (nodeRank node) (nodeLeft node) (nodeRight node)

    pushChildren node lower upper depth rest =
      let !withRight =
            case nodeRight node of
              Nothing -> rest
              Just right -> ValidationFrame right (Just (nodeItem node)) upper (depth + 1) : rest
      in case nodeLeft node of
           Nothing -> withRight
           Just left -> ValidationFrame left lower (Just (nodeItem node)) (depth + 1) : withRight

-- | Returns the exact pre-order topology with child subtree counts.
shapeForTesting :: CanonicalSortedSet a -> [CanonicalNodeShape a]
shapeForTesting (CanonicalSortedSet _ root) = List.reverse (walk (maybeToList root) [])
  where
    walk !pending !result =
      case pending of
        [] -> result
        node : rest ->
          let !shape = CanonicalNodeShape
                (nodeItem node)
                (maybe 0 nodeCount (nodeLeft node))
                (maybe 0 nodeCount (nodeRight node))
              !next = maybeToList (nodeLeft node) ++ maybeToList (nodeRight node) ++ rest
          in walk next (shape : result)

-- | IO-only diagnostic that tests root identity without exposing constructors.
sharesRootWith :: CanonicalSortedSet a -> CanonicalSortedSet a -> IO Bool
sharesRootWith (CanonicalSortedSet _ Nothing) (CanonicalSortedSet _ Nothing) = pure True
sharesRootWith (CanonicalSortedSet _ (Just left)) (CanonicalSortedSet _ (Just right)) = do
  leftName <- makeStableName left
  rightName <- makeStableName right
  pure (leftName `eqStableName` rightName)
sharesRootWith _ _ = pure False

-- | IO-only diagnostic that counts nodes retained by identity across versions.
sharedNodeCount :: CanonicalSortedSet a -> CanonicalSortedSet a -> IO Int
sharedNodeCount (CanonicalSortedSet _ leftRoot) (CanonicalSortedSet _ rightRoot) = do
  leftNames <- mapM makeStableName (nodes leftRoot)
  let !index = List.foldl' indexName IntMap.empty leftNames
  rightNames <- mapM makeStableName (nodes rightRoot)
  pure (List.foldl' (countName index) 0 rightNames)
  where
    indexName table name = IntMap.insertWith (++) (hashStableName name) [name] table
    countName table total name =
      case IntMap.lookup (hashStableName name) table of
        Just candidates | List.any (`eqStableName` name) candidates -> total + 1
        _ -> total

samePolicy :: CanonicalSortedSet a -> CanonicalSortedSet a -> Bool
samePolicy left right = policyIdentity (setPolicy left) == policyIdentity (setPolicy right)

foldInsert
  :: CanonicalSortedSet a
  -> [a]
  -> Either CanonicalSetError (CanonicalSortedSet a)
foldInsert = foldM (flip insert)

uniqueRanked
  :: ZipTreeRankPolicy a
  -> [a]
  -> Either CanonicalSetError [(a, ZipTreeRank)]
uniqueRanked policy = collect []
  where
    compareItems = policyCompare policy

    collect !result [] = Right (List.reverse result)
    collect !result (representative : rest) =
      let !rank = rankFor policy representative
      in consume result representative rank rest

    consume !result representative !rank remaining =
      case remaining of
        candidate : rest
          | compareItems representative candidate == EQ ->
              let !candidateRank = rankFor policy candidate
              in if candidateRank == rank
                   then consume result representative rank rest
                   else Left InconsistentRankHash
        _ -> collect ((representative, rank) : result) remaining

buildCanonical :: (a -> a -> Ordering) -> [(a, ZipTreeRank)] -> Maybe (Node a)
buildCanonical compareItems ranked = close Nothing spine
  where
    !spine = List.foldl' push [] ranked

    push !pending (item, rank) =
      case popHigher Nothing pending of
        (!left, !rest) -> OpenNode item rank left : rest
      where
        popHigher !subtree frames =
          case frames of
            OpenNode parent parentRank parentLeft : rest
              | higher compareItems item rank parent parentRank ->
                  let !completed = mkNode parent parentRank parentLeft subtree
                  in popHigher (Just completed) rest
            _ -> (subtree, frames)

    close !subtree frames =
      case frames of
        [] -> subtree
        OpenNode item rank left : rest ->
          let !completed = mkNode item rank left subtree
          in close (Just completed) rest

insertNode
  :: (a -> a -> Ordering)
  -> a
  -> ZipTreeRank
  -> Maybe (Node a)
  -> Node a
insertNode compareItems item rank root =
  let (!path, !remainder) = descend [] root
      (!left, !right) = splitNode compareItems item remainder
      !inserted = mkNode item rank left right
  in List.foldl' rebuild inserted path
  where
    descend !path cursor =
      case cursor of
        Just node
          | not (higher compareItems item rank (nodeItem node) (nodeRank node)) ->
              let !wentLeft = compareItems item (nodeItem node) == LT
                  !next = if wentLeft then nodeLeft node else nodeRight node
              in descend (PathStep node wentLeft : path) next
        _ -> (path, cursor)

    rebuild !result (PathStep node wentLeft)
      | wentLeft = mkNode (nodeItem node) (nodeRank node) (Just result) (nodeRight node)
      | otherwise = mkNode (nodeItem node) (nodeRank node) (nodeLeft node) (Just result)

splitNode
  :: (a -> a -> Ordering)
  -> a
  -> Maybe (Node a)
  -> (Maybe (Node a), Maybe (Node a))
splitNode compareItems item root = rebuild (Nothing, Nothing) path
  where
    !path = descend [] root

    descend !steps cursor =
      case cursor of
        Nothing -> steps
        Just node ->
          let !wentLeft = compareItems item (nodeItem node) == LT
              !next = if wentLeft then nodeLeft node else nodeRight node
          in descend (PathStep node wentLeft : steps) next

    rebuild = List.foldl' step

    step (!left, !right) (PathStep node wentLeft)
      | wentLeft =
          let !newRight = mkNode (nodeItem node) (nodeRank node) right (nodeRight node)
          in (left, Just newRight)
      | otherwise =
          let !newLeft = mkNode (nodeItem node) (nodeRank node) (nodeLeft node) left
          in (Just newLeft, right)

-- Nothing means "not found"; Just updatedRoot represents a successful delete,
-- including Just Nothing when the last element was removed.
removeNode
  :: (a -> a -> Ordering)
  -> a
  -> Maybe (Node a)
  -> Maybe (Maybe (Node a))
removeNode compareItems item root = descend [] root
  where
    descend !path cursor =
      case cursor of
        Nothing -> Nothing
        Just node ->
          case compareItems item (nodeItem node) of
            EQ ->
              let !merged = mergeNodes compareItems (nodeLeft node) (nodeRight node)
                  !rebuilt = List.foldl' rebuild merged path
              in Just rebuilt
            ordering ->
              let !wentLeft = ordering == LT
                  !next = if wentLeft then nodeLeft node else nodeRight node
              in descend (PathStep node wentLeft : path) next

    rebuild !result (PathStep node wentLeft)
      | wentLeft = Just (mkNode (nodeItem node) (nodeRank node) result (nodeRight node))
      | otherwise = Just (mkNode (nodeItem node) (nodeRank node) (nodeLeft node) result)

mergeNodes
  :: (a -> a -> Ordering)
  -> Maybe (Node a)
  -> Maybe (Node a)
  -> Maybe (Node a)
mergeNodes compareItems initialLeft initialRight =
  let (!path, !remainder) = descend [] initialLeft initialRight
  in List.foldl' rebuild remainder path
  where
    descend !path left right =
      case (left, right) of
        (Just leftNode, Just rightNode)
          | higherNode compareItems leftNode rightNode ->
              descend (MergeStep leftNode True : path) (nodeRight leftNode) right
          | otherwise ->
              descend (MergeStep rightNode False : path) left (nodeLeft rightNode)
        _ -> (path, case left of Just _ -> left; Nothing -> right)

    rebuild !result (MergeStep node choseLeft)
      | choseLeft = Just (mkNode (nodeItem node) (nodeRank node) (nodeLeft node) result)
      | otherwise = Just (mkNode (nodeItem node) (nodeRank node) result (nodeRight node))

findNode :: a -> CanonicalSortedSet a -> Maybe (Node a)
findNode item (CanonicalSortedSet policy root) = descend root
  where
    descend cursor =
      case cursor of
        Nothing -> Nothing
        Just node ->
          case policyCompare policy item (nodeItem node) of
            EQ -> Just node
            LT -> descend (nodeLeft node)
            GT -> descend (nodeRight node)

nodesEquivalent
  :: (a -> a -> Ordering)
  -> Maybe (Node a)
  -> Maybe (Node a)
  -> Bool
nodesEquivalent compareItems leftRoot rightRoot = walk [(leftRoot, rightRoot)]
  where
    walk !pending =
      case pending of
        [] -> True
        (Nothing, Nothing) : rest -> walk rest
        (Just left, Just right) : rest
          | compareItems (nodeItem left) (nodeItem right) == EQ ->
              walk ((nodeLeft left, nodeLeft right) : (nodeRight left, nodeRight right) : rest)
        _ -> False

semanticUnique :: (a -> a -> Ordering) -> [a] -> [a]
semanticUnique compareItems values = deduplicate (List.sortBy compareItems values)
  where
    deduplicate [] = []
    deduplicate (item : rest) = item : dropEquivalent item rest

    dropEquivalent _ [] = []
    dropEquivalent item remaining@(candidate : rest)
      | compareItems item candidate == EQ = dropEquivalent item rest
      | otherwise = deduplicate remaining

equivalentLists :: (a -> a -> Ordering) -> [a] -> [a] -> Bool
equivalentLists compareItems = walk
  where
    walk [] [] = True
    walk (left : leftRest) (right : rightRest)
      | compareItems left right == EQ = walk leftRest rightRest
    walk _ _ = False

orderedSubset :: (a -> a -> Ordering) -> [a] -> [a] -> Bool
orderedSubset compareItems = walk
  where
    walk [] _ = True
    walk _ [] = False
    walk leftItems@(left : leftRest) (right : rightRest) =
      case compareItems left right of
        LT -> False
        EQ -> walk leftRest rightRest
        GT -> walk leftItems rightRest

orderedOverlaps :: (a -> a -> Ordering) -> [a] -> [a] -> Bool
orderedOverlaps compareItems = walk
  where
    walk [] _ = False
    walk _ [] = False
    walk leftItems@(left : leftRest) rightItems@(right : rightRest) =
      case compareItems left right of
        LT -> walk leftRest rightItems
        EQ -> True
        GT -> walk leftItems rightRest

higherNode :: (a -> a -> Ordering) -> Node a -> Node a -> Bool
higherNode compareItems left right =
  higher compareItems (nodeItem left) (nodeRank left) (nodeItem right) (nodeRank right)

higher
  :: (a -> a -> Ordering)
  -> a
  -> ZipTreeRank
  -> a
  -> ZipTreeRank
  -> Bool
higher compareItems leftItem leftRank rightItem rightRank =
  case compare (geometricRank leftRank) (geometricRank rightRank) of
    GT -> True
    LT -> False
    EQ ->
      case compare (secondaryRank leftRank) (secondaryRank rightRank) of
        GT -> True
        LT -> False
        EQ -> compareItems leftItem rightItem == LT

mkNode
  :: a
  -> ZipTreeRank
  -> Maybe (Node a)
  -> Maybe (Node a)
  -> Node a
mkNode item rank left right =
  let !cachedCount = 1 + maybe 0 nodeCount left + maybe 0 nodeCount right
      !cachedHeight = 1 + max (maybe 0 nodeHeight left) (maybe 0 nodeHeight right)
      !cachedDigest = digestFor rank left right
  in Node item rank left right cachedCount cachedHeight cachedDigest

digestFor :: ZipTreeRank -> Maybe (Node a) -> Maybe (Node a) -> Word64
digestFor rank left right =
  mix
    ( contentWord rank
        `xor` rotateL (maybe leftDigestSeed nodeDigest left) 17
        `xor` rotateL (maybe rightDigestSeed nodeDigest right) 43
    )

mix :: Word64 -> Word64
mix initial =
  let !first = (initial `xor` (initial `shiftR` 30)) * 0xbf58476d1ce4e5b9
      !second = (first `xor` (first `shiftR` 27)) * 0x94d049bb133111eb
  in second `xor` (second `shiftR` 31)

nodes :: Maybe (Node a) -> [Node a]
nodes root = List.reverse (walk (maybeToList root) [])
  where
    walk !pending !result =
      case pending of
        [] -> result
        node : rest ->
          let !next = maybeToList (nodeLeft node) ++ maybeToList (nodeRight node) ++ rest
          in walk next (node : result)

maybeToList :: Maybe a -> [a]
maybeToList Nothing = []
maybeToList (Just value) = [value]

zzt2Prefix :: ByteString
zzt2Prefix = ByteString.pack [0x5a, 0x5a, 0x54, 0x32]

word64BigEndian :: Word64 -> ByteString
word64BigEndian value = ByteString.pack
  [ byte 56
  , byte 48
  , byte 40
  , byte 32
  , byte 24
  , byte 16
  , byte 8
  , byte 0
  ]
  where
    byte :: Int -> Word8
    byte amount = fromIntegral (value `shiftR` amount)

word64At :: Int -> ByteString -> Word64
word64At offset bytes = List.foldl' appendByte 0 [offset .. offset + 7]
  where
    appendByte result index = result * 256 + fromIntegral (ByteString.index bytes index)

sha256 :: ByteString -> ByteString
sha256 bytes = ByteArray.convert (hash bytes :: Digest SHA256)

hmacSha256 :: ByteString -> ByteString -> ByteString
hmacSha256 key bytes = ByteArray.convert (hmac key bytes :: HMAC SHA256)
