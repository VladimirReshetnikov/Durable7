{-# LANGUAGE BangPatterns #-}

-- | Authenticated persistence, proofs, synchronization, and merge for the canonical Merkle tree.
--
-- The API is deliberately pure.  A 'MerkleBlockStore' is an immutable snapshot and operations
-- that publish blocks return a successor store only after every byte, closure, budget, and
-- destination conflict has been verified.
module Data.Structures.Hamt.MerklePersistence
  ( MerkleBlock(..)
  , MerkleBlockStore
  , emptyBlockStore
  , blockStoreSize
  , blockStoreNull
  , blockStoreDigests
  , blockStoreContains
  , blockStoreLookup
  , putBlock
  , removeBlock
  , clearBlockStore
  , MerkleBlockPack
  , makeMerkleBlockPack
  , packAlgorithmId
  , packDomainDigest
  , packRootDigest
  , packBlocks
  , packBlockCount
  , packTotalByteCount
  , packContainsRootBlock
  , MerkleVerificationFailureKind(..)
  , MerkleVerificationError(..)
  , MerkleBudgetError(..)
  , MerkleVerificationBudget
  , maxBlockCount
  , maxTotalByteCount
  , maxBlockByteCount
  , maxDepth
  , maxEntryCount
  , maxChildReferencesPerBlock
  , maxProofQueryByteCount
  , makeMerkleVerificationBudget
  , defaultMerkleVerificationBudget
  , saveTree
  , exportPack
  , exportPackFor
  , loadTree
  , loadTreeWithBudget
  , importPack
  , importPackWithBudget
  , createSyncPack
  , MerkleSyncPlan(..)
  , planSync
  , MerkleProofKind(..)
  , MerkleProofStep
  , makeMerkleProofStep
  , proofStepBlock
  , proofStepExpandedChildIndexes
  , MerkleProof
  , makeMerkleProof
  , proofAlgorithmId
  , proofDomainDigest
  , proofRootDigest
  , proofKind
  , proofQuery
  , proofSteps
  , proofTotalByteCount
  , MerkleProofCreationError(..)
  , createProof
  , createRangeProof
  , MerkleProofVerificationResult(..)
  , verifyProof
  , verifyProofWithBudget
  , MerkleMergeValue(..)
  , MerkleThreeWayMergeConflict(..)
  , MerkleMergeResolution(..)
  , MerkleThreeWayMergeResult(..)
  , mergeThreeWay
  , mergeThreeWayWith
  ) where

import Prelude

import qualified Data.ByteString as ByteString
import Data.ByteString (ByteString)
import Data.Char (isSpace)
import Data.Int (Int32)
import Data.List (sort)
import qualified Data.Map.Strict as Map
import Data.Map.Strict (Map)
import qualified Data.Set as Set
import Data.Set (Set)
import Data.Word (Word8, Word64)

import Data.Structures.Hamt.MerkleEncoding
  ( MerkleCodec
  , MerkleCodecError
  , MerkleDigest
  , MerkleSearchTreePolicy
  , compareMerkleKeys
  , compatibleMerklePolicies
  , decodeMerkleValue
  , digestBytes
  , encodeMerkleValue
  , hashBytes
  , hashMerkleKeyBytes
  , int32BigEndian
  , merkleAlgorithmId
  , merkleDomainDigest
  , merkleEmptyDigest
  , merkleKeyCodec
  , merkleLevel
  , merkleValueCodec
  , parseDigestBytes
  )
import qualified Data.Structures.Hamt.MerkleSearchTree as Tree
import Data.Structures.Hamt.MerkleSearchTree
  ( MerkleSearchTree
  , MerkleTreeError(..)
  )

-- | One immutable serialized block paired with its claimed content address.
--
-- Construction intentionally does not authenticate the pair.  Stores are format agnostic;
-- verified load, import, and proof operations enforce the address and exact bytes.
data MerkleBlock = MerkleBlock
  { merkleBlockDigest :: !MerkleDigest
  , merkleBlockBytes :: !ByteString
  } deriving (Eq, Show)

-- | A language-idiomatic immutable block-store snapshot.
newtype MerkleBlockStore = MerkleBlockStore (Map MerkleDigest MerkleBlock)
  deriving (Eq, Show)

emptyBlockStore :: MerkleBlockStore
emptyBlockStore = MerkleBlockStore Map.empty

blockStoreSize :: MerkleBlockStore -> Int
blockStoreSize (MerkleBlockStore blocks) = Map.size blocks

blockStoreNull :: MerkleBlockStore -> Bool
blockStoreNull (MerkleBlockStore blocks) = Map.null blocks

blockStoreDigests :: MerkleBlockStore -> [MerkleDigest]
blockStoreDigests (MerkleBlockStore blocks) = Map.keys blocks

blockStoreContains :: MerkleDigest -> MerkleBlockStore -> Bool
blockStoreContains digest (MerkleBlockStore blocks) = Map.member digest blocks

blockStoreLookup :: MerkleDigest -> MerkleBlockStore -> Maybe MerkleBlock
blockStoreLookup digest (MerkleBlockStore blocks) = Map.lookup digest blocks

-- | Idempotently inserts a block, returning whether a new address was added and the successor.
putBlock
  :: MerkleBlock
  -> MerkleBlockStore
  -> Either MerkleVerificationError (Bool, MerkleBlockStore)
putBlock block store@(MerkleBlockStore blocks) =
  case Map.lookup (merkleBlockDigest block) blocks of
    Nothing -> Right (True, MerkleBlockStore (Map.insert (merkleBlockDigest block) block blocks))
    Just existing
      | existing == block -> Right (False, store)
      | otherwise -> Left (verification ConflictingBlock
          "a digest is already associated with different block bytes"
          (Just (merkleBlockDigest block)))

removeBlock :: MerkleDigest -> MerkleBlockStore -> (Bool, MerkleBlockStore)
removeBlock digest store@(MerkleBlockStore blocks)
  | Map.member digest blocks = (True, MerkleBlockStore (Map.delete digest blocks))
  | otherwise = (False, store)

clearBlockStore :: MerkleBlockStore -> MerkleBlockStore
clearBlockStore store
  | blockStoreNull store = store
  | otherwise = emptyBlockStore

-- | An immutable complete or partial transfer pack with unique addresses.
data MerkleBlockPack = MerkleBlockPack
  { packAlgorithmId :: !String
  , packDomainDigest :: !MerkleDigest
  , packRootDigest :: !MerkleDigest
  , packBlocks :: ![MerkleBlock]
  , packTotalByteCount :: !Word64
  , packContainsRootBlock :: !Bool
  } deriving (Eq, Show)

makeMerkleBlockPack
  :: String
  -> MerkleDigest
  -> MerkleDigest
  -> [MerkleBlock]
  -> Either MerkleVerificationError MerkleBlockPack
makeMerkleBlockPack algorithm domain root blocks
  | null algorithm || all isSpace algorithm =
      Left (verification MalformedBlock "a Merkle pack algorithm identifier must not be empty" Nothing)
  | otherwise = do
      (seen, total) <- foldl' account (Right (Set.empty, 0)) blocks
      let _ = seen
      Right MerkleBlockPack
        { packAlgorithmId = algorithm
        , packDomainDigest = domain
        , packRootDigest = root
        , packBlocks = blocks
        , packTotalByteCount = total
        , packContainsRootBlock = any ((== root) . merkleBlockDigest) blocks
        }
  where
    account result block = do
      (seen, total) <- result
      if Set.member (merkleBlockDigest block) seen
        then Left (verification DuplicateBlock "a Merkle pack repeats a block digest"
          (Just (merkleBlockDigest block)))
        else do
          total' <- checkedAddWord64 total (ByteString.length (merkleBlockBytes block))
            "a Merkle pack byte count overflowed" (Just (merkleBlockDigest block))
          Right (Set.insert (merkleBlockDigest block) seen, total')

packBlockCount :: MerkleBlockPack -> Int
packBlockCount = length . packBlocks

-- | Stable failure classifications shared with the managed and native ports.
data MerkleVerificationFailureKind
  = NoVerificationFailure
  | UnsupportedAlgorithm
  | DomainMismatch
  | MissingBlock
  | DigestMismatch
  | MalformedBlock
  | NonCanonicalBlock
  | DuplicateBlock
  | ConflictingBlock
  | InvalidReference
  | CycleDetected
  | ProofMismatch
  | RootMismatch
  | ResourceLimitExceeded
  deriving (Eq, Show)

data MerkleVerificationError = MerkleVerificationError
  { verificationFailureKind :: !MerkleVerificationFailureKind
  , verificationMessage :: !String
  , verificationBlockDigest :: !(Maybe MerkleDigest)
  } deriving (Eq, Show)

data MerkleBudgetError
  = ZeroVerificationLimit !String
  | BlockByteLimitExceedsTotal
  | ProofQueryLimitExceedsTotal
  deriving (Eq, Show)

-- | Seven finite trust-boundary limits, each applied before corresponding allocation or decode.
data MerkleVerificationBudget = MerkleVerificationBudget
  { verificationMaxBlockCount :: !Int
  , verificationMaxTotalByteCount :: !Word64
  , verificationMaxBlockByteCount :: !Int
  , verificationMaxDepth :: !Int
  , verificationMaxEntryCount :: !Word64
  , verificationMaxChildReferencesPerBlock :: !Int
  , verificationMaxProofQueryByteCount :: !Int
  } deriving (Eq, Show)

maxBlockCount :: MerkleVerificationBudget -> Int
maxBlockCount = verificationMaxBlockCount

maxTotalByteCount :: MerkleVerificationBudget -> Word64
maxTotalByteCount = verificationMaxTotalByteCount

maxBlockByteCount :: MerkleVerificationBudget -> Int
maxBlockByteCount = verificationMaxBlockByteCount

maxDepth :: MerkleVerificationBudget -> Int
maxDepth = verificationMaxDepth

maxEntryCount :: MerkleVerificationBudget -> Word64
maxEntryCount = verificationMaxEntryCount

maxChildReferencesPerBlock :: MerkleVerificationBudget -> Int
maxChildReferencesPerBlock = verificationMaxChildReferencesPerBlock

maxProofQueryByteCount :: MerkleVerificationBudget -> Int
maxProofQueryByteCount = verificationMaxProofQueryByteCount

makeMerkleVerificationBudget
  :: Int -> Word64 -> Int -> Int -> Word64 -> Int -> Int
  -> Either MerkleBudgetError MerkleVerificationBudget
makeMerkleVerificationBudget blocks totalBytes blockBytes depth entries children queryBytes = do
  positive "maxBlockCount" blocks
  positive "maxBlockByteCount" blockBytes
  positive "maxDepth" depth
  positive "maxChildReferencesPerBlock" children
  positive "maxProofQueryByteCount" queryBytes
  if totalBytes == 0 then Left (ZeroVerificationLimit "maxTotalByteCount") else Right ()
  if entries == 0 then Left (ZeroVerificationLimit "maxEntryCount") else Right ()
  if fromIntegral blockBytes > totalBytes then Left BlockByteLimitExceedsTotal else Right ()
  if fromIntegral queryBytes > totalBytes then Left ProofQueryLimitExceedsTotal else Right ()
  Right MerkleVerificationBudget
    { verificationMaxBlockCount = blocks
    , verificationMaxTotalByteCount = totalBytes
    , verificationMaxBlockByteCount = blockBytes
    , verificationMaxDepth = depth
    , verificationMaxEntryCount = entries
    , verificationMaxChildReferencesPerBlock = children
    , verificationMaxProofQueryByteCount = queryBytes
    }
  where
    positive name value
      | value > 0 = Right ()
      | otherwise = Left (ZeroVerificationLimit name)

defaultMerkleVerificationBudget :: MerkleVerificationBudget
defaultMerkleVerificationBudget = MerkleVerificationBudget
  { verificationMaxBlockCount = 1000000
  , verificationMaxTotalByteCount = 1 `shiftLWord64` 30
  , verificationMaxBlockByteCount = 16 * 1024 * 1024
  , verificationMaxDepth = 256
  , verificationMaxEntryCount = 100000000
  , verificationMaxChildReferencesPerBlock = 65536
  , verificationMaxProofQueryByteCount = 16 * 1024 * 1024
  }

-- Avoid importing the whole bit API for one manifest constant.
shiftLWord64 :: Word64 -> Int -> Word64
shiftLWord64 value amount = value * (2 ^ amount)

data VerificationContext = VerificationContext
  { contextBudget :: !MerkleVerificationBudget
  , contextBlockCount :: !Int
  , contextTotalBytes :: !Word64
  , contextEntryCount :: !Word64
  , contextBlocks :: !(Set MerkleDigest)
  }

newContext :: MerkleVerificationBudget -> VerificationContext
newContext budget = VerificationContext budget 0 0 0 Set.empty

checkDepth :: Int -> Maybe MerkleDigest -> VerificationContext -> Either MerkleVerificationError ()
checkDepth depth digest context
  | depth <= 0 || depth > maxDepth (contextBudget context) =
      Left (verification ResourceLimitExceeded
        "Merkle verification exceeded the maximum reference depth" digest)
  | otherwise = Right ()

accountBlock
  :: Int
  -> MerkleBlock
  -> VerificationContext
  -> Either MerkleVerificationError (Bool, VerificationContext)
accountBlock depth block context = do
  checkDepth depth (Just digest) context
  if Set.member digest (contextBlocks context)
    then Right (False, context)
    else do
      let byteCount = ByteString.length (merkleBlockBytes block)
          budget = contextBudget context
      if byteCount > maxBlockByteCount budget
        then Left (limit "a Merkle block exceeds the per-block byte budget" digest)
        else Right ()
      bytes <- intToWord64 byteCount "a Merkle block byte count overflowed" (Just digest)
      if contextBlockCount context >= maxBlockCount budget
          || bytes > maxTotalByteCount budget - contextTotalBytes context
        then Left (limit "Merkle verification exceeded its block or total-byte budget" digest)
        else Right (True, context
          { contextBlockCount = contextBlockCount context + 1
          , contextTotalBytes = contextTotalBytes context + bytes
          , contextBlocks = Set.insert digest (contextBlocks context)
          })
  where
    digest = merkleBlockDigest block

accountEntries
  :: Int -> MerkleDigest -> VerificationContext
  -> Either MerkleVerificationError VerificationContext
accountEntries count digest context = do
  count64 <- intToWord64 count "a decoded entry count overflowed" (Just digest)
  let budget = contextBudget context
  if count64 > maxEntryCount budget - contextEntryCount context
    then Left (limit "Merkle verification exceeded its decoded-entry budget" digest)
    else Right context { contextEntryCount = contextEntryCount context + count64 }

accountProofQuery
  :: Int -> MerkleDigest -> VerificationContext
  -> Either MerkleVerificationError VerificationContext
accountProofQuery count root context = do
  count64 <- intToWord64 count "a proof query byte count overflowed" (Just root)
  let budget = contextBudget context
  if count > maxProofQueryByteCount budget
      || count64 > maxTotalByteCount budget - contextTotalBytes context
    then Left (limit "a Merkle proof query exceeds its query or total-byte budget" root)
    else Right context { contextTotalBytes = contextTotalBytes context + count64 }

data DecodedEntry k v = DecodedEntry
  { decodedKey :: k
  , decodedValue :: v
  , decodedKeyBytes :: !ByteString
  , decodedValueBytes :: !ByteString
  }

data DecodedBlock k v = DecodedBlock
  { decodedBlockSource :: !MerkleBlock
  , decodedLevel :: !Word8
  , decodedSubtreeCount :: !Int
  , decodedEntries :: ![DecodedEntry k v]
  , decodedChildDigests :: ![MerkleDigest]
  }

data LoadedNode k v = LoadedNode
  { loadedLevel :: !Word8
  , loadedCount :: !Int
  , loadedMinimum :: k
  , loadedMaximum :: k
  , loadedEntries :: ![(k, v)]
  }

-- | Exports the complete closure in deterministic preorder.
exportPack :: MerkleSearchTree k v -> MerkleBlockPack
exportPack tree = trustedPack tree
  [ MerkleBlock (Tree.blockViewDigest view) (Tree.blockViewBytes view)
  | view <- Tree.blocksPreorder tree
  ]

-- | Exports unique explicitly requested blocks in caller order.
exportPackFor
  :: [MerkleDigest]
  -> MerkleSearchTree k v
  -> Either MerkleVerificationError MerkleBlockPack
exportPackFor requested tree = do
  let available = Map.fromList
        [ (Tree.blockViewDigest view, MerkleBlock (Tree.blockViewDigest view) (Tree.blockViewBytes view))
        | view <- Tree.blocksPreorder tree
        ]
  (_, result) <- foldl' (select available) (Right (Set.empty, [])) requested
  makeMerkleBlockPack merkleAlgorithmId (merkleDomainDigest (Tree.policy tree))
    (Tree.rootDigest tree) result
  where
    select available result digest = do
      (seen, blocks) <- result
      if Set.member digest seen
        then Left (verification DuplicateBlock "a digest was requested more than once" (Just digest))
        else case Map.lookup digest available of
          Nothing -> Left (verification MissingBlock "a requested digest is not in this tree" (Just digest))
          Just block -> Right (Set.insert digest seen, blocks ++ [block])

trustedPack :: MerkleSearchTree k v -> [MerkleBlock] -> MerkleBlockPack
trustedPack tree blocks =
  case makeMerkleBlockPack merkleAlgorithmId (merkleDomainDigest (Tree.policy tree))
      (Tree.rootDigest tree) blocks of
    Right pack -> pack
    Left problem -> error ("trusted Merkle tree emitted an invalid pack: " ++ show problem)

-- | Atomically saves the complete closure after checking every destination conflict.
saveTree
  :: MerkleSearchTree k v
  -> MerkleBlockStore
  -> Either MerkleVerificationError (Int, MerkleBlockStore)
saveTree tree store = do
  let blocks = packBlocks (exportPack tree)
  preflightStore blocks store
  foldl' publish (Right (0, store)) blocks
  where
    publish result block = do
      (added, current) <- result
      (isNew, successor) <- putBlock block current
      Right (added + if isNew then 1 else 0, successor)

preflightStore :: [MerkleBlock] -> MerkleBlockStore -> Either MerkleVerificationError ()
preflightStore blocks store = mapM_ check blocks
  where
    check block = case blockStoreLookup (merkleBlockDigest block) store of
      Just existing | existing /= block -> Left (verification ConflictingBlock
        "a destination digest is associated with different block bytes"
        (Just (merkleBlockDigest block)))
      _ -> Right ()

verifyEnvelope
  :: String -> MerkleDigest -> MerkleSearchTreePolicy k v
  -> Either MerkleVerificationError ()
verifyEnvelope algorithm domain policy
  | algorithm /= merkleAlgorithmId = Left (verification UnsupportedAlgorithm
      "the Merkle envelope uses an unsupported algorithm" Nothing)
  | domain /= merkleDomainDigest policy = Left (verification DomainMismatch
      "the Merkle envelope belongs to another policy domain" Nothing)
  | otherwise = Right ()

-- | Loads and fully verifies one closure under the cross-language default limits.
loadTree
  :: MerkleDigest -> MerkleSearchTreePolicy k v -> MerkleBlockStore
  -> Either MerkleVerificationError (MerkleSearchTree k v)
loadTree root policy store = loadTreeWithBudget root policy store defaultMerkleVerificationBudget

loadTreeWithBudget
  :: MerkleDigest
  -> MerkleSearchTreePolicy k v
  -> MerkleBlockStore
  -> MerkleVerificationBudget
  -> Either MerkleVerificationError (MerkleSearchTree k v)
loadTreeWithBudget root policy store budget = fst <$> loadWithContext root policy store (newContext budget)

loadWithContext
  :: MerkleDigest
  -> MerkleSearchTreePolicy k v
  -> MerkleBlockStore
  -> VerificationContext
  -> Either MerkleVerificationError (MerkleSearchTree k v, VerificationContext)
loadWithContext root policy store initial
  | root == merkleEmptyDigest policy = Right (Tree.empty policy, initial)
  | otherwise = do
      (loaded, _, context) <- loadNode root 1 policy store Map.empty Set.empty initial
      tree <- mapTreeError root (Tree.fromList (loadedEntries loaded) policy)
      if Tree.rootDigest tree /= root
        then Left (verification RootMismatch "the reconstructed root does not match the requested root" (Just root))
        else Right ()
      case Tree.validateStructure tree of
        Left problem -> Left (verification InvalidReference
          ("the verified closure failed final structure validation: " ++ problem) (Just root))
        Right _ -> Right ()
      Right (tree, context)

loadNode
  :: MerkleDigest
  -> Int
  -> MerkleSearchTreePolicy k v
  -> MerkleBlockStore
  -> Map MerkleDigest (LoadedNode k v)
  -> Set MerkleDigest
  -> VerificationContext
  -> Either MerkleVerificationError
       (LoadedNode k v, Map MerkleDigest (LoadedNode k v), VerificationContext)
loadNode digest depth policy store cache active context = do
  checkDepth depth (Just digest) context
  case Map.lookup digest cache of
    Just loaded -> Right (loaded, cache, context)
    Nothing
      | Set.member digest active -> Left (verification CycleDetected
          "the Merkle closure contains a reference cycle" (Just digest))
      | otherwise -> do
          block <- maybe (Left (verification MissingBlock "a required Merkle block is absent" (Just digest)))
            Right (blockStoreLookup digest store)
          (decoded, context1) <- decodeBlock policy block depth context
          (children, cache1, context2) <- loadChildren decoded 0 cache
            (Set.insert digest active) context1
          validateChildren decoded children policy
          let actualCount64 = fromIntegral (length (decodedEntries decoded))
                + sum [fromIntegral (loadedCount child) | Just child <- children] :: Word64
          if actualCount64 /= fromIntegral (decodedSubtreeCount decoded)
            then Left (verification InvalidReference
              "a block's declared subtree count disagrees with its closure" (Just digest))
            else Right ()
          let actualCount = fromIntegral actualCount64
          let ordered = interleave (decodedEntries decoded) children
          case ordered of
            [] -> Left (verification InvalidReference "a Merkle node cannot be empty" (Just digest))
            firstEntry : remainingEntries -> do
              let finalEntry = foldl' (\_ entry -> entry) firstEntry remainingEntries
              let loaded = LoadedNode
                    { loadedLevel = decodedLevel decoded
                    , loadedCount = actualCount
                    , loadedMinimum = fst firstEntry
                    , loadedMaximum = fst finalEntry
                    , loadedEntries = ordered
                    }
              Right (loaded, Map.insert digest loaded cache1, context2)
  where
    loadChildren decoded index currentCache currentActive currentContext
      | index >= length (decodedChildDigests decoded) = Right ([], currentCache, currentContext)
      | otherwise = do
          let childDigest = decodedChildDigests decoded !! index
          if childDigest == merkleEmptyDigest policy
            then do
              (rest, nextCache, nextContext) <- loadChildren decoded (index + 1)
                currentCache currentActive currentContext
              Right (Nothing : rest, nextCache, nextContext)
            else do
              (child, nextCache, nextContext) <- loadNode childDigest (depth + 1) policy store
                currentCache currentActive currentContext
              (rest, finalCache, finalContext) <- loadChildren decoded (index + 1)
                nextCache currentActive nextContext
              Right (Just child : rest, finalCache, finalContext)

interleave :: [DecodedEntry k v] -> [Maybe (LoadedNode k v)] -> [(k, v)]
interleave entries children = concat
  [ maybe [] loadedEntries (children !! index)
      ++ [(decodedKey entry, decodedValue entry)]
  | (index, entry) <- zip [0 ..] entries
  ] ++ maybe [] loadedEntries (last children)

validateChildren
  :: DecodedBlock k v
  -> [Maybe (LoadedNode k v)]
  -> MerkleSearchTreePolicy k v
  -> Either MerkleVerificationError ()
validateChildren parent children policy = mapM_ validate (zip [0 ..] children)
  where
    parentEntries = decodedEntries parent
    digest = merkleBlockDigest (decodedBlockSource parent)
    validate (_, Nothing) = Right ()
    validate (index, Just child)
      | loadedLevel child >= decodedLevel parent = invalid "a child is not below its parent level"
      | index /= 0
          && compareMerkleKeys policy (loadedMinimum child)
               (decodedKey (parentEntries !! (index - 1))) /= GT =
          invalid "a child crosses its lower key separator"
      | index /= length parentEntries
          && compareMerkleKeys policy (loadedMaximum child)
               (decodedKey (parentEntries !! index)) /= LT =
          invalid "a child crosses its upper key separator"
      | otherwise = Right ()
    invalid message = Left (verification InvalidReference message (Just digest))

decodeBlock
  :: MerkleSearchTreePolicy k v
  -> MerkleBlock
  -> Int
  -> VerificationContext
  -> Either MerkleVerificationError (DecodedBlock k v, VerificationContext)
decodeBlock policy block depth context0 = do
  (firstVisit, context1) <- accountBlock depth block context0
  let digest = merkleBlockDigest block
      bytes = merkleBlockBytes block
  if hashBytes bytes /= digest
    then Left (verification DigestMismatch "a block's bytes do not match its claimed digest" (Just digest))
    else Right ()
  if ByteString.length bytes < 78
    then Left (verification MalformedBlock "a Merkle block is shorter than the minimum encoding" (Just digest))
    else Right ()
  (magic, offset1) <- takeBytes bytes 0 4 (Just digest)
  if magic /= ascii "MST2" then malformed digest "a Merkle block has the wrong magic" else Right ()
  (tag, offset2) <- takeBytes bytes offset1 1 (Just digest)
  if tag /= ByteString.singleton 1 then malformed digest "a Merkle block has an unsupported tag" else Right ()
  (domainBytes, offset3) <- takeBytes bytes offset2 32 (Just digest)
  domain <- either (const (malformed digest "a Merkle block has a malformed domain digest")) Right
    (parseDigestBytes domainBytes)
  if domain /= merkleDomainDigest policy
    then Left (verification DomainMismatch "a Merkle block belongs to another policy domain" (Just digest))
    else Right ()
  (levelBytes, offset4) <- takeBytes bytes offset3 1 (Just digest)
  let level = ByteString.head levelBytes
  if level > 64 then malformed digest "a Merkle block layer exceeds the SHA-256 nibble range" else Right ()
  (subtreeCount, offset5) <- readPositiveInt32 bytes offset4 digest "subtree"
  (entryCount, offset6) <- readPositiveInt32 bytes offset5 digest "entry"
  if subtreeCount < entryCount
    then malformed digest "a Merkle block has more entries than its declared subtree"
    else Right ()
  if entryCount >= maxChildReferencesPerBlock (contextBudget context1)
    then Left (limit "a Merkle block exceeds the child-reference budget" digest)
    else Right ()
  let minimumLength = 46 + entryCount * (8 + 32) + 32
  if minimumLength > ByteString.length bytes
    then malformed digest "a Merkle block cannot contain its declared entries and children"
    else Right ()
  context2 <- if firstVisit then accountEntries entryCount digest context1 else Right context1
  (entries, offset7) <- decodeEntries policy digest level entryCount bytes offset6 []
  let childCount = entryCount + 1
  (children, offset8) <- readDigests childCount bytes offset7 digest []
  if offset8 /= ByteString.length bytes
    then Left (verification NonCanonicalBlock "a Merkle block has trailing bytes" (Just digest))
    else Right ()
  let canonical = encodeRawBlock policy level subtreeCount entries children
  if canonical /= bytes
    then Left (verification NonCanonicalBlock "a Merkle block is not uniquely canonical" (Just digest))
    else Right (DecodedBlock block level subtreeCount entries children, context2)

decodeEntries
  :: MerkleSearchTreePolicy k v -> MerkleDigest -> Word8 -> Int -> ByteString -> Int
  -> [DecodedEntry k v]
  -> Either MerkleVerificationError ([DecodedEntry k v], Int)
decodeEntries policy digest level count bytes offset result
  | count == 0 = Right (reverse result, offset)
  | otherwise = do
      (keyBytes, offset1) <- readLengthPrefixed bytes offset digest
      (valueBytes, offset2) <- readLengthPrefixed bytes offset1 digest
      key <- codecDecode MalformedBlock "a key codec rejected serialized bytes"
        digest (merkleKeyCodec policy) keyBytes
      value <- codecDecode MalformedBlock "a value codec rejected serialized bytes"
        digest (merkleValueCodec policy) valueBytes
      canonicalKey <- codecEncode NonCanonicalBlock "a decoded key could not be re-encoded"
        digest (merkleKeyCodec policy) key
      canonicalValue <- codecEncode NonCanonicalBlock "a decoded value could not be re-encoded"
        digest (merkleValueCodec policy) value
      if canonicalKey /= keyBytes || canonicalValue /= valueBytes
        then Left (verification NonCanonicalBlock
          "a decoded entry does not round-trip to its exact bytes" (Just digest))
        else Right ()
      let entryLevel = merkleLevel (hashMerkleKeyBytes policy keyBytes)
      if entryLevel /= level
        then Left (verification NonCanonicalBlock
          "an entry is stored at a level other than its hash-derived level" (Just digest))
        else Right ()
      case result of
        previous : _ | compareMerkleKeys policy (decodedKey previous) key /= LT ->
          Left (verification NonCanonicalBlock
            "Merkle block entries are not strictly comparer-ordered" (Just digest))
        _ -> Right ()
      decodeEntries policy digest level (count - 1) bytes offset2
        (DecodedEntry key value keyBytes valueBytes : result)

readDigests
  :: Int -> ByteString -> Int -> MerkleDigest -> [MerkleDigest]
  -> Either MerkleVerificationError ([MerkleDigest], Int)
readDigests count bytes offset digest result
  | count == 0 = Right (reverse result, offset)
  | otherwise = do
      (raw, next) <- takeBytes bytes offset 32 (Just digest)
      parsed <- either (const (malformed digest "a child digest is malformed")) Right
        (parseDigestBytes raw)
      readDigests (count - 1) bytes next digest (parsed : result)

encodeRawBlock
  :: MerkleSearchTreePolicy k v
  -> Word8 -> Int -> [DecodedEntry k v] -> [MerkleDigest] -> ByteString
encodeRawBlock policy level subtreeCount entries children = ByteString.concat
  [ ascii "MST2"
  , ByteString.singleton 1
  , digestBytes (merkleDomainDigest policy)
  , ByteString.singleton level
  , int32BigEndian (fromIntegral subtreeCount)
  , int32BigEndian (fromIntegral (length entries))
  , ByteString.concat
      [ ByteString.concat
          [ int32BigEndian (fromIntegral (ByteString.length (decodedKeyBytes entry)))
          , decodedKeyBytes entry
          , int32BigEndian (fromIntegral (ByteString.length (decodedValueBytes entry)))
          , decodedValueBytes entry
          ]
      | entry <- entries
      ]
  , ByteString.concat (map digestBytes children)
  ]

-- | Verifies a complete or partial pack and publishes all supplied canonical blocks atomically.
importPack
  :: MerkleBlockPack
  -> MerkleSearchTreePolicy k v
  -> MerkleBlockStore
  -> Either MerkleVerificationError (MerkleSearchTree k v, MerkleBlockStore)
importPack pack policy destination =
  importPackWithBudget pack policy destination defaultMerkleVerificationBudget

importPackWithBudget
  :: MerkleBlockPack
  -> MerkleSearchTreePolicy k v
  -> MerkleBlockStore
  -> MerkleVerificationBudget
  -> Either MerkleVerificationError (MerkleSearchTree k v, MerkleBlockStore)
importPackWithBudget pack policy destination budget = do
  verifyEnvelope (packAlgorithmId pack) (packDomainDigest pack) policy
  context <- foldl' verifySupplied (Right (newContext budget)) (packBlocks pack)
  staged <- foldl' stage (Right Map.empty) (packBlocks pack)
  let MerkleBlockStore fallback = destination
      overlay = MerkleBlockStore (Map.union staged fallback)
  (tree, _) <- loadWithContext (packRootDigest pack) policy overlay context
  preflightStore (packBlocks pack) destination
  published <- foldl' publish (Right destination) (packBlocks pack)
  Right (tree, published)
  where
    verifySupplied result block = do
      current <- result
      snd <$> decodeBlock policy block 1 current
    stage result block = do
      blocks <- result
      case Map.lookup (merkleBlockDigest block) blocks of
        Nothing -> Right (Map.insert (merkleBlockDigest block) block blocks)
        Just existing
          | existing == block -> Left (verification DuplicateBlock
              "the pack repeats a block digest" (Just (merkleBlockDigest block)))
          | otherwise -> Left (verification ConflictingBlock
              "the pack associates one digest with conflicting bytes" (Just (merkleBlockDigest block)))
    publish result block = do
      store <- result
      (_, successor) <- putBlock block store
      Right successor

-- | Exports target blocks absent from a receiver, pruning each known complete closure.
createSyncPack :: MerkleBlockStore -> MerkleSearchTree k v -> MerkleBlockPack
createSyncPack receiver tree = trustedPack tree (walk [Tree.rootDigest tree] [])
  where
    available = trustedDecoded tree
    blocks = Map.fromList
      [ (Tree.blockViewDigest view, MerkleBlock (Tree.blockViewDigest view) (Tree.blockViewBytes view))
      | view <- Tree.blocksPreorder tree
      ]
    walk [] result = reverse result
    walk (digest : rest) result
      | digest == merkleEmptyDigest (Tree.policy tree) = walk rest result
      | blockStoreContains digest receiver = walk rest result
      | otherwise = case (Map.lookup digest blocks, Map.lookup digest available) of
          (Just block, Just decoded) -> walk (decodedChildDigests decoded ++ rest) (block : result)
          _ -> error "trusted Merkle tree closure is incomplete"

-- | One deterministic iterative synchronization frontier.
data MerkleSyncPlan = MerkleSyncPlan
  { syncAlgorithmId :: !String
  , syncDomainDigest :: !MerkleDigest
  , syncLocalRootDigest :: !MerkleDigest
  , syncTargetRootDigest :: !MerkleDigest
  , syncRequestedBlocks :: ![MerkleDigest]
  , syncExaminedBlockCount :: !Int
  , syncExaminedByteCount :: !Word64
  } deriving (Eq, Show)

planSync
  :: MerkleSearchTree k v
  -> MerkleBlockStore
  -> MerkleSearchTree k v
  -> Either MerkleTreeError MerkleSyncPlan
planSync local receiver target
  | not (compatibleMerklePolicies (Tree.policy local) (Tree.policy target)) =
      Left IncompatibleMerklePolicy
  | Tree.rootDigest local == Tree.rootDigest target = Right (makePlan [] 0 0)
  | otherwise = do
      (requested, count, bytes) <- walk [Tree.rootDigest target] [] 0 0
      Right (makePlan (reverse requested) count bytes)
  where
    decoded = trustedDecoded target
    blocks = Map.fromList
      [ (Tree.blockViewDigest view, Tree.blockViewBytes view)
      | view <- Tree.blocksPreorder target
      ]
    makePlan requested count bytes = MerkleSyncPlan
      merkleAlgorithmId (merkleDomainDigest (Tree.policy target))
      (Tree.rootDigest local) (Tree.rootDigest target) requested count bytes
    walk [] requested count bytes = Right (requested, count, bytes)
    walk (digest : rest) requested count bytes
      | digest == merkleEmptyDigest (Tree.policy target) = walk rest requested count bytes
      | otherwise = case (Map.lookup digest blocks, Map.lookup digest decoded) of
          (Just blockBytes, Just block) -> do
            bytes' <- case checkedAddWord64 bytes (ByteString.length blockBytes)
                "a synchronization examined-byte count overflowed" (Just digest) of
              Left _ -> Left MerkleTreeSizeOverflow
              Right value -> Right value
            if blockStoreContains digest receiver
              then walk (decodedChildDigests block ++ rest) requested (count + 1) bytes'
              else walk rest (digest : requested) (count + 1) bytes'
          _ -> Left MerkleTreeSizeOverflow

trustedDecoded :: MerkleSearchTree k v -> Map MerkleDigest (DecodedBlock k v)
trustedDecoded tree = Map.fromList
  [ (Tree.persistenceBlockDigest view, DecodedBlock
      { decodedBlockSource = MerkleBlock
          (Tree.persistenceBlockDigest view) (Tree.persistenceBlockBytes view)
      , decodedLevel = Tree.persistenceBlockLevel view
      , decodedSubtreeCount = Tree.persistenceBlockSubtreeCount view
      , decodedEntries = map trustedEntry (Tree.persistenceBlockEntries view)
      , decodedChildDigests = Tree.persistenceBlockChildDigests view
      })
  | view <- Tree.persistenceBlocksPreorder tree
  ]
  where
    trustedEntry entry = DecodedEntry
      { decodedKey = Tree.entryKey entry
      , decodedValue = Tree.entryValue entry
      , decodedKeyBytes = Tree.entryKeyBytes entry
      , decodedValueBytes = Tree.entryValueBytes entry
      }

-- Proof and merge declarations are implemented below the wire helpers.

data MerkleProofKind = MembershipProof | NonMembershipProof | RangeProof
  deriving (Eq, Show)

data MerkleProofStep = MerkleProofStep
  { proofStepBlock :: !MerkleBlock
  , proofStepExpandedChildIndexes :: ![Int]
  } deriving (Eq, Show)

makeMerkleProofStep
  :: MerkleBlock -> [Int] -> Either MerkleVerificationError MerkleProofStep
makeMerkleProofStep block indexes
  | any (< 0) indexes = Left (verification ProofMismatch
      "a proof step contains a negative child index" (Just (merkleBlockDigest block)))
  | adjacentDuplicate sorted = Left (verification ProofMismatch
      "a proof step expands one child index more than once" (Just (merkleBlockDigest block)))
  | otherwise = Right (MerkleProofStep block sorted)
  where
    sorted = sort indexes

data MerkleProof = MerkleProof
  { proofAlgorithmId :: !String
  , proofDomainDigest :: !MerkleDigest
  , proofRootDigest :: !MerkleDigest
  , proofKind :: !MerkleProofKind
  , proofQuery :: !ByteString
  , proofSteps :: ![MerkleProofStep]
  , proofTotalByteCount :: !Word64
  } deriving (Eq, Show)

makeMerkleProof
  :: String -> MerkleDigest -> MerkleDigest -> MerkleProofKind -> ByteString
  -> [MerkleProofStep] -> Either MerkleVerificationError MerkleProof
makeMerkleProof algorithm domain root kind query steps
  | null algorithm || all isSpace algorithm = Left (verification MalformedBlock
      "a Merkle proof algorithm identifier must not be empty" Nothing)
  | otherwise = do
      queryBytes <- intToWord64 (ByteString.length query)
        "a Merkle proof query length overflowed" (Just root)
      (_, total) <- foldl' account (Right (Set.empty, queryBytes)) steps
      Right (MerkleProof algorithm domain root kind query steps total)
  where
    account result step = do
      (seen, total) <- result
      let digest = merkleBlockDigest (proofStepBlock step)
      if Set.member digest seen
        then Left (verification DuplicateBlock "a proof repeats a block digest" (Just digest))
        else do
          total' <- checkedAddWord64 total (ByteString.length (merkleBlockBytes (proofStepBlock step)))
            "a Merkle proof byte count overflowed" (Just digest)
          Right (Set.insert digest seen, total')

data MerkleProofCreationError
  = ProofCodecError !MerkleCodecError
  | ReversedProofRange
  | ProofSizeOverflow
  deriving (Eq, Show)

data MerkleProofVerificationResult = MerkleProofVerificationResult
  { proofIsValid :: !Bool
  , proofFailureKind :: !MerkleVerificationFailureKind
  , proofFailureMessage :: !(Maybe String)
  , proofComputedRootDigest :: !(Maybe MerkleDigest)
  , proofVerifiedBlockCount :: !Int
  , proofVerifiedByteCount :: !Word64
  } deriving (Eq, Show)

data MerkleMergeValue v = MergeAbsent | MergePresent v
  deriving (Eq, Show)

data MerkleThreeWayMergeConflict k v = MerkleThreeWayMergeConflict
  { mergeConflictKey :: k
  , mergeConflictBase :: MerkleMergeValue v
  , mergeConflictLeft :: MerkleMergeValue v
  , mergeConflictRight :: MerkleMergeValue v
  } deriving (Eq, Show)

data MerkleMergeResolution v
  = MergeUnresolved
  | MergeUseBase
  | MergeUseLeft
  | MergeUseRight
  | MergeSetValue v
  | MergeDelete
  deriving (Eq, Show)

data MerkleThreeWayMergeResult k v
  = MerkleMergeSucceeded (MerkleSearchTree k v)
  | MerkleMergeConflicted [MerkleThreeWayMergeConflict k v]

createProof :: k -> MerkleSearchTree k v -> Either MerkleProofCreationError MerkleProof
createProof = createPointProof

createRangeProof :: k -> k -> MerkleSearchTree k v -> Either MerkleProofCreationError MerkleProof
createRangeProof = createRangeProofInternal

verifyProof
  :: MerkleProof -> MerkleSearchTreePolicy k v -> MerkleProofVerificationResult
verifyProof proof policy = verifyProofWithBudget proof policy defaultMerkleVerificationBudget

mergeThreeWay
  :: Eq v
  => Maybe (MerkleThreeWayMergeConflict k v -> MerkleMergeResolution v)
  -> MerkleSearchTree k v -> MerkleSearchTree k v -> MerkleSearchTree k v
  -> Either MerkleTreeError (MerkleThreeWayMergeResult k v)
mergeThreeWay = mergeThreeWayWith (==)

createPointProof :: k -> MerkleSearchTree k v -> Either MerkleProofCreationError MerkleProof
createPointProof key tree = do
  keyBytes <- mapProofCodec (encodeMerkleValue (merkleKeyCodec treePolicy) key)
  let (kind, valueBytes, steps) = walk (Tree.rootDigest tree) []
  query <- encodePointQuery kind keyBytes valueBytes
  trustedProof tree kind query (reverse steps)
  where
    treePolicy = Tree.policy tree
    decoded = trustedDecoded tree
    walk digest result
      | digest == merkleEmptyDigest treePolicy = (NonMembershipProof, Nothing, result)
      | otherwise = case Map.lookup digest decoded of
          Nothing -> error "trusted Merkle tree proof path is incomplete"
          Just block ->
            let (position, found) = findDecodedPosition treePolicy (decodedEntries block) key
                child = decodedChildDigests block !! position
                expanded = [position | not found && child /= merkleEmptyDigest treePolicy]
                step = trustedProofStep (decodedBlockSource block) expanded
             in if found
                  then (MembershipProof,
                    Just (decodedValueBytes (decodedEntries block !! position)), step : result)
                  else if child == merkleEmptyDigest treePolicy
                    then (NonMembershipProof, Nothing, step : result)
                    else walk child (step : result)

createRangeProofInternal
  :: k -> k -> MerkleSearchTree k v -> Either MerkleProofCreationError MerkleProof
createRangeProofInternal minimumKey maximumKey tree
  | compareMerkleKeys treePolicy minimumKey maximumKey == GT = Left ReversedProofRange
  | otherwise = do
      minimumBytes <- mapProofCodec (encodeMerkleValue (merkleKeyCodec treePolicy) minimumKey)
      maximumBytes <- mapProofCodec (encodeMerkleValue (merkleKeyCodec treePolicy) maximumKey)
      query <- encodeRangeQuery minimumBytes maximumBytes
      trustedProof tree RangeProof query (collect (Tree.rootDigest tree))
  where
    treePolicy = Tree.policy tree
    decoded = trustedDecoded tree
    collect digest
      | digest == merkleEmptyDigest treePolicy = []
      | otherwise = case Map.lookup digest decoded of
          Nothing -> error "trusted Merkle range proof closure is incomplete"
          Just block ->
            let indexes =
                  [ index
                  | (index, child) <- zip [0 ..] (decodedChildDigests block)
                  , child /= merkleEmptyDigest treePolicy
                  , childIntervalIntersects treePolicy (decodedEntries block) index minimumKey maximumKey
                  ]
             in trustedProofStep (decodedBlockSource block) indexes
                  : concatMap (collect . (decodedChildDigests block !!)) indexes

trustedProofStep :: MerkleBlock -> [Int] -> MerkleProofStep
trustedProofStep block indexes = case makeMerkleProofStep block indexes of
  Right step -> step
  Left problem -> error ("trusted proof step is invalid: " ++ show problem)

trustedProof
  :: MerkleSearchTree k v -> MerkleProofKind -> ByteString -> [MerkleProofStep]
  -> Either MerkleProofCreationError MerkleProof
trustedProof tree kind query steps =
  case makeMerkleProof merkleAlgorithmId (merkleDomainDigest (Tree.policy tree))
      (Tree.rootDigest tree) kind query steps of
    Right proof -> Right proof
    Left _ -> Left ProofSizeOverflow

encodePointQuery
  :: MerkleProofKind -> ByteString -> Maybe ByteString
  -> Either MerkleProofCreationError ByteString
encodePointQuery kind keyBytes valueBytes = do
  checkedProofLength keyBytes
  mapM_ checkedProofLength valueBytes
  let tag = case kind of
        MembershipProof -> 0
        NonMembershipProof -> 1
        RangeProof -> error "a range proof uses its dedicated query encoder"
      suffix = case (kind, valueBytes) of
        (MembershipProof, Just value) -> [int32BigEndian (fromIntegral (ByteString.length value)), value]
        (NonMembershipProof, Nothing) -> []
        _ -> error "a trusted point proof has inconsistent kind and value"
  Right (ByteString.concat
    ([ascii "MSP2", ByteString.singleton tag,
      int32BigEndian (fromIntegral (ByteString.length keyBytes)), keyBytes] ++ suffix))

encodeRangeQuery
  :: ByteString -> ByteString -> Either MerkleProofCreationError ByteString
encodeRangeQuery minimumBytes maximumBytes = do
  checkedProofLength minimumBytes
  checkedProofLength maximumBytes
  Right (ByteString.concat
    [ ascii "MSP2"
    , ByteString.singleton 2
    , int32BigEndian (fromIntegral (ByteString.length minimumBytes))
    , minimumBytes
    , int32BigEndian (fromIntegral (ByteString.length maximumBytes))
    , maximumBytes
    ])

checkedProofLength :: ByteString -> Either MerkleProofCreationError ()
checkedProofLength bytes
  | ByteString.length bytes > fromIntegral (maxBound :: Int32) = Left ProofSizeOverflow
  | otherwise = Right ()

mapProofCodec :: Either MerkleCodecError a -> Either MerkleProofCreationError a
mapProofCodec = either (Left . ProofCodecError) Right

-- | Verifies an untrusted proof under explicit finite limits.
--
-- Admission order is security-significant: query budget first, proof shape second, envelope third,
-- and only then maps, hashing, codecs, and block decoding.
verifyProofWithBudget
  :: MerkleProof
  -> MerkleSearchTreePolicy k v
  -> MerkleVerificationBudget
  -> MerkleProofVerificationResult
verifyProofWithBudget proof policy budget =
  case accountProofQuery (ByteString.length (proofQuery proof)) (proofRootDigest proof) initial of
    Left problem -> failed problem initial
    Right queryContext -> case preflightProofStructure proof queryContext of
      Left problem -> failed problem queryContext
      Right () -> case verifyEnvelope (proofAlgorithmId proof) (proofDomainDigest proof) policy of
        Left problem -> failed problem queryContext
        Right () ->
          let (decodedResult, finalContext) = decodeProofSteps policy (proofSteps proof)
                Map.empty Map.empty queryContext
           in case decodedResult of
                Left problem -> failed problem finalContext
                Right (decoded, stepsByDigest) ->
                  case verifyDecodedProof proof policy decoded stepsByDigest finalContext of
                    Left problem -> failed problem finalContext
                    Right () -> MerkleProofVerificationResult True NoVerificationFailure Nothing
                      (Just (proofRootDigest proof))
                      (contextBlockCount finalContext) (contextTotalBytes finalContext)
  where
    initial = newContext budget
    failed problem context = MerkleProofVerificationResult False
      (verificationFailureKind problem) (Just (verificationMessage problem))
      (Just (proofRootDigest proof)) (contextBlockCount context) (contextTotalBytes context)

preflightProofStructure
  :: MerkleProof -> VerificationContext -> Either MerkleVerificationError ()
preflightProofStructure proof context
  | length (proofSteps proof) > maxBlockCount budget =
      Left (limit "a Merkle proof exceeds its block-count budget" (proofRootDigest proof))
  | otherwise = mapM_ check (proofSteps proof)
  where
    budget = contextBudget context
    check step
      | length (proofStepExpandedChildIndexes step) > maxChildReferencesPerBlock budget =
          Left (limit "a Merkle proof step exceeds its expanded-child budget"
            (merkleBlockDigest (proofStepBlock step)))
      | otherwise = Right ()

decodeProofSteps
  :: MerkleSearchTreePolicy k v
  -> [MerkleProofStep]
  -> Map MerkleDigest (DecodedBlock k v)
  -> Map MerkleDigest MerkleProofStep
  -> VerificationContext
  -> (Either MerkleVerificationError
       (Map MerkleDigest (DecodedBlock k v), Map MerkleDigest MerkleProofStep), VerificationContext)
decodeProofSteps _ [] decoded steps context = (Right (decoded, steps), context)
decodeProofSteps policy (step : rest) decoded steps context =
  let block = proofStepBlock step
      digest = merkleBlockDigest block
   in case accountBlock 1 block context of
        Left problem -> (Left problem, context)
        Right (_, admittedContext) -> case decodeBlock policy block 1 context of
          Left problem -> (Left problem, admittedContext)
          Right (parsed, nextContext)
            | Map.member digest decoded || Map.member digest steps ->
              (Left (verification DuplicateBlock "a proof repeats a block digest" (Just digest)), nextContext)
            | otherwise -> case validateExpandedIndexes policy parsed step of
              Left problem -> (Left problem, nextContext)
              Right () -> decodeProofSteps policy rest (Map.insert digest parsed decoded)
                (Map.insert digest step steps) nextContext

validateExpandedIndexes
  :: MerkleSearchTreePolicy k v -> DecodedBlock k v -> MerkleProofStep
  -> Either MerkleVerificationError ()
validateExpandedIndexes policy block step = mapM_ check (proofStepExpandedChildIndexes step)
  where
    digest = merkleBlockDigest (proofStepBlock step)
    children = decodedChildDigests block
    check index
      | index < 0 || index >= length children = Left (verification ProofMismatch
          "a proof expands a child index outside its block" (Just digest))
      | children !! index == merkleEmptyDigest policy = Left (verification ProofMismatch
          "a proof expands an empty child interval" (Just digest))
      | otherwise = Right ()

verifyDecodedProof
  :: MerkleProof
  -> MerkleSearchTreePolicy k v
  -> Map MerkleDigest (DecodedBlock k v)
  -> Map MerkleDigest MerkleProofStep
  -> VerificationContext
  -> Either MerkleVerificationError ()
verifyDecodedProof proof policy decoded steps context
  | root == merkleEmptyDigest policy = do
      if null (proofSteps proof)
        then Right ()
        else Left (verification ProofMismatch "an empty-root proof must not contain blocks" (Just root))
      verifyEmptyProofQuery proof policy
  | not (Map.member root decoded) = Left (verification RootMismatch
      "the proof does not contain its declared root block" (Just root))
  | otherwise = do
      visited <- case proofKind proof of
        MembershipProof -> verifyPointProof proof policy decoded steps context
        NonMembershipProof -> verifyPointProof proof policy decoded steps context
        RangeProof -> verifyRangeProof proof policy decoded steps context
      if Set.size visited /= length (proofSteps proof)
        then Left (verification ProofMismatch
          "the proof contains blocks outside the canonical query expansion" (Just root))
        else Right ()
  where
    root = proofRootDigest proof

verifyEmptyProofQuery
  :: MerkleProof -> MerkleSearchTreePolicy k v -> Either MerkleVerificationError ()
verifyEmptyProofQuery proof policy = case proofKind proof of
  MembershipProof -> Left (verification ProofMismatch
    "an empty tree cannot prove membership" (Just (proofRootDigest proof)))
  NonMembershipProof -> () <$ decodePointQuery proof policy
  RangeProof -> () <$ decodeRangeQuery proof policy

verifyPointProof
  :: MerkleProof
  -> MerkleSearchTreePolicy k v
  -> Map MerkleDigest (DecodedBlock k v)
  -> Map MerkleDigest MerkleProofStep
  -> VerificationContext
  -> Either MerkleVerificationError (Set MerkleDigest)
verifyPointProof proof policy decoded steps context = do
  (key, claimedValue) <- decodePointQuery proof policy
  walk (proofRootDigest proof) key claimedValue 1 Set.empty
  where
    walk digest key claimed depth visited = do
      checkDepth depth (Just digest) context
      if Set.member digest visited
        then Left (verification CycleDetected "a point proof contains a cycle" (Just digest))
        else Right ()
      block <- maybe (Left (verification MissingBlock
        "a point proof omits an expanded block" (Just digest))) Right (Map.lookup digest decoded)
      step <- maybe (Left (verification MissingBlock
        "a point proof omits an expanded step" (Just digest))) Right (Map.lookup digest steps)
      let visited' = Set.insert digest visited
          (position, found) = findDecodedPosition policy (decodedEntries block) key
      if found
        then do
          requireExpandedExactly step []
          if proofKind proof /= MembershipProof
            then Left (verification ProofMismatch "the proof claims absence for a present key" (Just digest))
            else Right ()
          if claimed /= Just (decodedValueBytes (decodedEntries block !! position))
            then Left (verification ProofMismatch
              "the membership value does not match the authenticated entry" (Just digest))
            else Right visited'
        else do
          let childDigest = decodedChildDigests block !! position
          if childDigest == merkleEmptyDigest policy
            then do
              requireExpandedExactly step []
              if proofKind proof /= NonMembershipProof
                then Left (verification ProofMismatch
                  "the proof claims membership for an absent key" (Just digest))
                else Right visited'
            else do
              requireExpandedExactly step [position]
              child <- maybe (Left (verification MissingBlock
                "a point proof omits its expanded child" (Just childDigest)))
                Right (Map.lookup childDigest decoded)
              validateProofReference policy block position child
              walk childDigest key claimed (depth + 1) visited'

verifyRangeProof
  :: MerkleProof
  -> MerkleSearchTreePolicy k v
  -> Map MerkleDigest (DecodedBlock k v)
  -> Map MerkleDigest MerkleProofStep
  -> VerificationContext
  -> Either MerkleVerificationError (Set MerkleDigest)
verifyRangeProof proof policy decoded steps context = do
  (minimumKey, maximumKey) <- decodeRangeQuery proof policy
  walk (proofRootDigest proof) minimumKey maximumKey 1 Set.empty
  where
    walk digest minimumKey maximumKey depth visited = do
      checkDepth depth (Just digest) context
      if Set.member digest visited
        then Left (verification CycleDetected "a range proof repeats a block" (Just digest))
        else Right ()
      block <- maybe (Left (verification MissingBlock
        "a range proof omits an expanded block" (Just digest))) Right (Map.lookup digest decoded)
      step <- maybe (Left (verification MissingBlock
        "a range proof omits an expanded step" (Just digest))) Right (Map.lookup digest steps)
      let expected =
            [ index
            | (index, childDigest) <- zip [0 ..] (decodedChildDigests block)
            , childDigest /= merkleEmptyDigest policy
            , childIntervalIntersects policy (decodedEntries block) index minimumKey maximumKey
            ]
          visited' = Set.insert digest visited
      requireExpandedExactly step expected
      foldl' (visitChild block minimumKey maximumKey depth) (Right visited') expected
    visitChild parent minimumKey maximumKey depth result index = do
      visited <- result
      let childDigest = decodedChildDigests parent !! index
      child <- maybe (Left (verification MissingBlock
        "a range proof omits an expanded child" (Just childDigest))) Right (Map.lookup childDigest decoded)
      validateProofReference policy parent index child
      walk childDigest minimumKey maximumKey (depth + 1) visited

validateProofReference
  :: MerkleSearchTreePolicy k v
  -> DecodedBlock k v -> Int -> DecodedBlock k v
  -> Either MerkleVerificationError ()
validateProofReference policy parent index child
  | decodedLevel child >= decodedLevel parent = invalid "an expanded child is not below its parent level"
  | otherwise = mapM_ validateEntry (decodedEntries child)
  where
    childDigest = merkleBlockDigest (decodedBlockSource child)
    invalid message = Left (verification InvalidReference message (Just childDigest))
    validateEntry entry
      | index /= 0 && compareMerkleKeys policy (decodedKey entry)
          (decodedKey (decodedEntries parent !! (index - 1))) /= GT =
          invalid "an expanded child crosses its lower separator"
      | index /= length (decodedEntries parent) && compareMerkleKeys policy (decodedKey entry)
          (decodedKey (decodedEntries parent !! index)) /= LT =
          invalid "an expanded child crosses its upper separator"
      | otherwise = Right ()

requireExpandedExactly :: MerkleProofStep -> [Int] -> Either MerkleVerificationError ()
requireExpandedExactly step expected
  | proofStepExpandedChildIndexes step == expected = Right ()
  | otherwise = Left (verification ProofMismatch
      "a proof step expands a noncanonical set of child intervals"
      (Just (merkleBlockDigest (proofStepBlock step))))

childIntervalIntersects
  :: MerkleSearchTreePolicy k v -> [DecodedEntry k v] -> Int -> k -> k -> Bool
childIntervalIntersects policy entries index minimumKey maximumKey =
  (index == 0 || compareMerkleKeys policy (decodedKey (entries !! (index - 1))) maximumKey == LT)
    && (index == length entries || compareMerkleKeys policy (decodedKey (entries !! index)) minimumKey == GT)

findDecodedPosition :: MerkleSearchTreePolicy k v -> [DecodedEntry k v] -> k -> (Int, Bool)
findDecodedPosition policy entries key = go 0 entries
  where
    go index [] = (index, False)
    go index (entry : rest) = case compareMerkleKeys policy (decodedKey entry) key of
      LT -> go (index + 1) rest
      EQ -> (index, True)
      GT -> (index, False)

decodePointQuery
  :: MerkleProof -> MerkleSearchTreePolicy k v
  -> Either MerkleVerificationError (k, Maybe ByteString)
decodePointQuery proof policy = do
  let expectedTag = case proofKind proof of
        MembershipProof -> 0
        NonMembershipProof -> 1
        RangeProof -> 2
  if proofKind proof == RangeProof
    then Left (proofProblem proof "a point proof has the wrong proof kind")
    else Right ()
  offset <- readProofHeader proof expectedTag
  (keyBytes, offset1) <- readProofField proof offset
  (valueBytes, offset2) <- if proofKind proof == MembershipProof
    then do
      (bytes, next) <- readProofField proof offset1
      value <- proofCodecDecode proof "a membership query has an invalid value encoding"
        (merkleValueCodec policy) bytes
      canonical <- proofCodecEncode proof "a membership query value could not be re-encoded"
        (merkleValueCodec policy) value
      if canonical /= bytes
        then Left (proofProblem proof "a membership query value is not canonical")
        else Right (Just bytes, next)
    else Right (Nothing, offset1)
  if offset2 /= ByteString.length (proofQuery proof)
    then Left (proofProblem proof "a point proof query has trailing bytes")
    else Right ()
  key <- decodeCanonicalQueryKey proof policy keyBytes
  Right (key, valueBytes)

decodeRangeQuery
  :: MerkleProof -> MerkleSearchTreePolicy k v
  -> Either MerkleVerificationError (k, k)
decodeRangeQuery proof policy = do
  if proofKind proof /= RangeProof
    then Left (proofProblem proof "a range proof has the wrong proof kind")
    else Right ()
  offset <- readProofHeader proof 2
  (minimumBytes, offset1) <- readProofField proof offset
  (maximumBytes, offset2) <- readProofField proof offset1
  if offset2 /= ByteString.length (proofQuery proof)
    then Left (proofProblem proof "a range proof query has trailing bytes")
    else Right ()
  minimumKey <- decodeCanonicalQueryKey proof policy minimumBytes
  maximumKey <- decodeCanonicalQueryKey proof policy maximumBytes
  if compareMerkleKeys policy minimumKey maximumKey == GT
    then Left (proofProblem proof "a range proof query has reversed bounds")
    else Right (minimumKey, maximumKey)

decodeCanonicalQueryKey
  :: MerkleProof -> MerkleSearchTreePolicy k v -> ByteString
  -> Either MerkleVerificationError k
decodeCanonicalQueryKey proof policy bytes = do
  key <- proofCodecDecode proof "a proof query has an invalid key encoding"
    (merkleKeyCodec policy) bytes
  canonical <- proofCodecEncode proof "a proof query key could not be re-encoded"
    (merkleKeyCodec policy) key
  if canonical /= bytes
    then Left (proofProblem proof "a proof query key is not canonical")
    else Right key

readProofHeader :: MerkleProof -> Word8 -> Either MerkleVerificationError Int
readProofHeader proof tag = do
  (magic, offset1) <- proofTake proof 0 4
  if magic /= ascii "MSP2" then Left (proofProblem proof "a proof query has the wrong magic") else Right ()
  (actual, offset2) <- proofTake proof offset1 1
  if ByteString.head actual /= tag
    then Left (proofProblem proof "a proof query kind disagrees with its envelope")
    else Right offset2

readProofField :: MerkleProof -> Int -> Either MerkleVerificationError (ByteString, Int)
readProofField proof offset = do
  (lengthBytes, next) <- proofTake proof offset 4
  let word = fromIntegral (ByteString.index lengthBytes 0) * 0x1000000
        + fromIntegral (ByteString.index lengthBytes 1) * 0x10000
        + fromIntegral (ByteString.index lengthBytes 2) * 0x100
        + fromIntegral (ByteString.index lengthBytes 3) :: Word64
      signed = fromIntegral word :: Int32
  if signed < 0
    then Left (proofProblem proof "a proof query field has a negative length")
    else proofTake proof next (fromIntegral signed)

proofTake :: MerkleProof -> Int -> Int -> Either MerkleVerificationError (ByteString, Int)
proofTake proof offset count
  | offset < 0 || count < 0 || count > ByteString.length query - offset =
      Left (proofProblem proof "a proof query field is truncated")
  | otherwise = Right (ByteString.take count (ByteString.drop offset query), offset + count)
  where
    query = proofQuery proof

proofProblem :: MerkleProof -> String -> MerkleVerificationError
proofProblem proof message = verification ProofMismatch message (Just (proofRootDigest proof))

proofCodecDecode
  :: MerkleProof -> String -> MerkleCodec a -> ByteString
  -> Either MerkleVerificationError a
proofCodecDecode proof message codec bytes = case decodeMerkleValue codec bytes of
  Left problem -> Left (proofProblem proof (message ++ ": " ++ show problem))
  Right value -> Right value

proofCodecEncode
  :: MerkleProof -> String -> MerkleCodec a -> a
  -> Either MerkleVerificationError ByteString
proofCodecEncode proof message codec value = case encodeMerkleValue codec value of
  Left problem -> Left (proofProblem proof (message ++ ": " ++ show problem))
  Right bytes -> Right bytes

-- | Three-way merges descendants with caller-supplied semantic value equality.
mergeThreeWayWith
  :: (v -> v -> Bool)
  -> Maybe (MerkleThreeWayMergeConflict k v -> MerkleMergeResolution v)
  -> MerkleSearchTree k v
  -> MerkleSearchTree k v
  -> MerkleSearchTree k v
  -> Either MerkleTreeError (MerkleThreeWayMergeResult k v)
mergeThreeWayWith valuesEqual resolver base left right
  | not (compatibleMerklePolicies (Tree.policy base) (Tree.policy left))
      || not (compatibleMerklePolicies (Tree.policy base) (Tree.policy right)) =
      Left IncompatibleMerklePolicy
  | Tree.rootDigest left == Tree.rootDigest base = Right (MerkleMergeSucceeded right)
  | Tree.rootDigest right == Tree.rootDigest base
      || Tree.rootDigest left == Tree.rootDigest right = Right (MerkleMergeSucceeded left)
  | otherwise = finish (walk baseEntries leftEntries rightEntries [] [])
  where
    treePolicy = Tree.policy base
    baseEntries = Tree.entries base
    leftEntries = Tree.entries left
    rightEntries = Tree.entries right

    finish (output, []) = MerkleMergeSucceeded <$> Tree.fromList (reverse output) treePolicy
    finish (_, conflicts) = Right (MerkleMergeConflicted (reverse conflicts))

    walk [] [] [] output conflicts = (output, conflicts)
    walk baseRest leftRest rightRest output conflicts =
      let key = minimumEntryKey treePolicy baseRest leftRest rightRest
          (baseSide, baseTail) = takeMergeSide treePolicy key baseRest
          (leftSide, leftTail) = takeMergeSide treePolicy key leftRest
          (rightSide, rightTail) = takeMergeSide treePolicy key rightRest
          continue = walk baseTail leftTail rightTail
       in if mergeSidesEqual valuesEqual leftSide baseSide
            then continue (appendSide rightSide output) conflicts
          else if mergeSidesEqual valuesEqual rightSide baseSide
              || mergeSidesEqual valuesEqual leftSide rightSide
            then continue (appendSide leftSide output) conflicts
          else
            let representative = case leftSide of
                  Just entry -> entry
                  Nothing -> case rightSide of
                    Just entry -> entry
                    Nothing -> case baseSide of
                      Just entry -> entry
                      Nothing -> error "a nonempty merge frontier has no representative"
                conflict = MerkleThreeWayMergeConflict
                  (Tree.entryKey representative) (mergeValue baseSide)
                  (mergeValue leftSide) (mergeValue rightSide)
                resolution = maybe MergeUnresolved ($ conflict) resolver
             in case resolution of
                  MergeUnresolved -> continue output (conflict : conflicts)
                  MergeUseBase -> continue (appendSide baseSide output) conflicts
                  MergeUseLeft -> continue (appendSide leftSide output) conflicts
                  MergeUseRight -> continue (appendSide rightSide output) conflicts
                  MergeDelete -> continue output conflicts
                  MergeSetValue value -> continue ((Tree.entryKey representative, value) : output) conflicts

minimumEntryKey
  :: MerkleSearchTreePolicy k v
  -> [Tree.MerkleEntry k v] -> [Tree.MerkleEntry k v] -> [Tree.MerkleEntry k v] -> k
minimumEntryKey policy base left right = case candidates of
  [] -> error "a nonempty merge frontier has no minimum"
  first : rest -> foldl' choose first rest
  where
    candidates = [Tree.entryKey entry | entry : _ <- [base, left, right]]
    choose current candidate
      | compareMerkleKeys policy candidate current == LT = candidate
      | otherwise = current

takeMergeSide
  :: MerkleSearchTreePolicy k v -> k -> [Tree.MerkleEntry k v]
  -> (Maybe (Tree.MerkleEntry k v), [Tree.MerkleEntry k v])
takeMergeSide _ _ [] = (Nothing, [])
takeMergeSide policy key source@(entry : rest)
  | compareMerkleKeys policy (Tree.entryKey entry) key == EQ = (Just entry, rest)
  | otherwise = (Nothing, source)

mergeSidesEqual
  :: (v -> v -> Bool) -> Maybe (Tree.MerkleEntry k v) -> Maybe (Tree.MerkleEntry k v) -> Bool
mergeSidesEqual _ Nothing Nothing = True
mergeSidesEqual valuesEqual (Just left) (Just right) =
  valuesEqual (Tree.entryValue left) (Tree.entryValue right)
mergeSidesEqual _ _ _ = False

appendSide :: Maybe (Tree.MerkleEntry k v) -> [(k, v)] -> [(k, v)]
appendSide Nothing output = output
appendSide (Just entry) output = (Tree.entryKey entry, Tree.entryValue entry) : output

mergeValue :: Maybe (Tree.MerkleEntry k v) -> MerkleMergeValue v
mergeValue Nothing = MergeAbsent
mergeValue (Just entry) = MergePresent (Tree.entryValue entry)

verification
  :: MerkleVerificationFailureKind -> String -> Maybe MerkleDigest
  -> MerkleVerificationError
verification = MerkleVerificationError

limit :: String -> MerkleDigest -> MerkleVerificationError
limit message digest = verification ResourceLimitExceeded message (Just digest)

malformed :: MerkleDigest -> String -> Either MerkleVerificationError a
malformed digest message = Left (verification MalformedBlock message (Just digest))

codecDecode
  :: MerkleVerificationFailureKind -> String -> MerkleDigest
  -> MerkleCodec a -> ByteString -> Either MerkleVerificationError a
codecDecode kind message digest codec bytes =
  case decodeMerkleValue codec bytes of
    Left problem -> Left (verification kind (message ++ ": " ++ show problem) (Just digest))
    Right value -> Right value

codecEncode
  :: MerkleVerificationFailureKind -> String -> MerkleDigest
  -> MerkleCodec a -> a -> Either MerkleVerificationError ByteString
codecEncode kind message digest codec value =
  case encodeMerkleValue codec value of
    Left problem -> Left (verification kind (message ++ ": " ++ show problem) (Just digest))
    Right bytes -> Right bytes

takeBytes
  :: ByteString -> Int -> Int -> Maybe MerkleDigest
  -> Either MerkleVerificationError (ByteString, Int)
takeBytes source offset count digest
  | offset < 0 || count < 0 || count > ByteString.length source - offset =
      Left (verification MalformedBlock "a serialized Merkle field is truncated" digest)
  | otherwise = Right (ByteString.take count (ByteString.drop offset source), offset + count)

readInt32
  :: ByteString -> Int -> Maybe MerkleDigest
  -> Either MerkleVerificationError (Int32, Int)
readInt32 source offset digest = do
  (bytes, next) <- takeBytes source offset 4 digest
  let word = fromIntegral (ByteString.index bytes 0) * 0x1000000
        + fromIntegral (ByteString.index bytes 1) * 0x10000
        + fromIntegral (ByteString.index bytes 2) * 0x100
        + fromIntegral (ByteString.index bytes 3) :: Word64
  Right (fromIntegral word, next)

readPositiveInt32
  :: ByteString -> Int -> MerkleDigest -> String
  -> Either MerkleVerificationError (Int, Int)
readPositiveInt32 source offset digest field = do
  (value, next) <- readInt32 source offset (Just digest)
  if value <= 0
    then malformed digest ("a Merkle block has an invalid " ++ field ++ " count")
    else Right (fromIntegral value, next)

readLengthPrefixed
  :: ByteString -> Int -> MerkleDigest
  -> Either MerkleVerificationError (ByteString, Int)
readLengthPrefixed source offset digest = do
  (count, next) <- readInt32 source offset (Just digest)
  if count < 0
    then malformed digest "a length-prefixed Merkle field has a negative length"
    else takeBytes source next (fromIntegral count) (Just digest)

ascii :: String -> ByteString
ascii = ByteString.pack . map (fromIntegral . fromEnum)

adjacentDuplicate :: Eq a => [a] -> Bool
adjacentDuplicate (left : right : rest) = left == right || adjacentDuplicate (right : rest)
adjacentDuplicate _ = False

intToWord64
  :: Int -> String -> Maybe MerkleDigest -> Either MerkleVerificationError Word64
intToWord64 value message digest
  | value < 0 = Left (verification ResourceLimitExceeded message digest)
  | otherwise = Right (fromIntegral value)

checkedAddWord64
  :: Word64 -> Int -> String -> Maybe MerkleDigest
  -> Either MerkleVerificationError Word64
checkedAddWord64 left right message digest = do
  right64 <- intToWord64 right message digest
  if right64 > maxBound - left
    then Left (verification ResourceLimitExceeded message digest)
    else Right (left + right64)

mapTreeError
  :: MerkleDigest -> Either MerkleTreeError a -> Either MerkleVerificationError a
mapTreeError digest result = case result of
  Right value -> Right value
  Left MerkleTreeSizeOverflow -> Left (limit "a decoded tree exceeds the size limit" digest)
  Left problem -> Left (verification NonCanonicalBlock
    ("a decoded tree could not be reconstructed: " ++ show problem) (Just digest))
