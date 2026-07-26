-- | Tests for Merkle storage, verification, and synchronization.
module PersistenceTests (runPersistenceTests) where

import qualified Data.ByteString as ByteString
import Data.ByteString (ByteString)
import Data.Bits (xor)
import Data.Int (Int32)
import Data.Word (Word64)

import Durable7.Hamt.MerkleEncoding
  ( MerkleCodec(..)
  , MerkleDigest
  , MerkleSearchTreePolicy
  , digestBytes
  , digestHex
  , hashBytes
  , int32MerkleCodec
  , int32BigEndian
  , makeMerkleSearchTreePolicy
  , merkleDomainDigest
  , merkleEmptyDigest
  , nullableUtf8MerkleCodec
  )
import Durable7.Hamt.MerklePersistence
import qualified Durable7.Hamt.MerkleSearchTree as Tree

type Value = Maybe String
type TestTree = Tree.MerkleSearchTree Int32 Value

-- | Runs the Merkle storage, verification, and synchronization test cases.
runPersistenceTests :: IO ()
runPersistenceTests = do
  testGoldenAndRoundTrips
  testMalformedMissingAndConflictFailures
  testAllSevenBudgets
  testProofAdmissionPrecedesCodecs
  testPointRangeAndTamperedProofs
  testSynchronization
  testMerge
  testRetainedVersionsAndModel

policyWithId :: String -> IO (MerkleSearchTreePolicy Int32 Value)
policyWithId identifier = expectRight "construct persistence policy"
  (makeMerkleSearchTreePolicy identifier compare int32MerkleCodec nullableUtf8MerkleCodec)

testPolicy :: IO (MerkleSearchTreePolicy Int32 Value)
testPolicy = policyWithId "haskell-persistence-algorithms-v1"

createTree :: MerkleSearchTreePolicy Int32 Value -> Int32 -> IO TestTree
createTree policy count = expectRight "construct persistence tree" (Tree.fromList source policy)
  where
    first = negate (count `div` 2)
    source =
      [ (key, if key `mod` 29 == 0 then Nothing else Just ("value:" ++ show key))
      | key <- [first .. first + count - 1]
      ]

testGoldenAndRoundTrips :: IO ()
testGoldenAndRoundTrips = do
  goldenPolicy <- policyWithId "golden-int-string-v1"
  goldenTree <- expectRight "construct golden persistence tree"
    (Tree.insert 42 (Just "forty-two") (Tree.empty goldenPolicy))
  let goldenPack = exportPack goldenTree
      expectedBytes = decodeHex
        "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2000000000100000001000000040000002a0000000a01666f7274792d74776f98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb398900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
  assertEqual "persistence golden block count" 1 (packBlockCount goldenPack)
  assertEqual "persistence golden bytes" expectedBytes
    (merkleBlockBytes (only "golden block" (packBlocks goldenPack)))
  assertEqual "persistence golden root"
    "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"
    (digestHex (packRootDigest goldenPack))

  policy <- testPolicy
  tree <- createTree policy 513
  let pack = exportPack tree
  assertEqual "complete pack is exact closure" (Tree.blockCount tree) (packBlockCount pack)
  assertBool "complete pack contains root" (packContainsRootBlock pack)
  (added, store) <- expectRight "save complete closure" (saveTree tree emptyBlockStore)
  assertEqual "save added every block" (Tree.blockCount tree) added
  (addedAgain, sameStore) <- expectRight "repeat save is idempotent" (saveTree tree store)
  assertEqual "repeat save adds nothing" 0 addedAgain
  assertEqual "repeat save retains store" store sameStore
  loaded <- expectRight "load complete closure" (loadTree (Tree.rootDigest tree) policy store)
  assertTreesEqual "loaded exact closure" tree loaded
  (imported, importedStore) <- expectRight "import complete pack"
    (importPack pack policy emptyBlockStore)
  assertTreesEqual "imported exact closure" tree imported
  assertEqual "import publishes every block" (Tree.blockCount tree) (blockStoreSize importedStore)
  assertEqual "round-trip pack bytes" pack (exportPack imported)
  assertFailure "pack rejects duplicate addresses" DuplicateBlock
    (makeMerkleBlockPack (packAlgorithmId pack) (packDomainDigest pack)
      (packRootDigest pack) [firstTotal "duplicate block" (packBlocks pack),
        firstTotal "duplicate block" (packBlocks pack)])
  assertFailure "explicit export rejects duplicate addresses" DuplicateBlock
    (exportPackFor [Tree.rootDigest tree, Tree.rootDigest tree] tree)
  assertFailure "explicit export rejects unknown address" MissingBlock
    (exportPackFor [hashBytes (ByteString.pack [1, 2, 3])] tree)

  let emptyTree = Tree.empty policy
      emptyPack = exportPack emptyTree
  assertEqual "empty pack has no blocks" 0 (packBlockCount emptyPack)
  emptyLoaded <- expectRight "load empty root"
    (loadTree (merkleEmptyDigest policy) policy emptyBlockStore)
  assertTreesEqual "empty root round trip" emptyTree emptyLoaded

testMalformedMissingAndConflictFailures :: IO ()
testMalformedMissingAndConflictFailures = do
  policy <- testPolicy
  tree <- createTree policy 257
  let pack = exportPack tree
      missing = lastTotal "last persistence block" (packBlocks pack)
      incompleteBlocks = filter ((/= merkleBlockDigest missing) . merkleBlockDigest) (packBlocks pack)
  incomplete <- expectRight "construct partial pack"
    (makeMerkleBlockPack (packAlgorithmId pack) (packDomainDigest pack)
      (packRootDigest pack) incompleteBlocks)
  assertFailure "missing closure is rejected" MissingBlock
    (fst <$> importPack incomplete policy emptyBlockStore)

  let root = findBlock (packRootDigest pack) (packBlocks pack)
      changedBytes = flipLastBit (merkleBlockBytes root)
      tampered = root { merkleBlockBytes = changedBytes }
  tamperedPack <- replaceBlock pack tampered
  assertFailure "digest tampering is rejected" DigestMismatch
    (fst <$> importPack tamperedPack policy emptyBlockStore)

  let trailingBytes = ByteString.snoc (merkleBlockBytes root) 0
      trailing = MerkleBlock (hashBytes trailingBytes) trailingBytes
  trailingPack <- expectRight "construct trailing-byte pack"
    (makeMerkleBlockPack (packAlgorithmId pack) (packDomainDigest pack)
      (merkleBlockDigest trailing) [trailing])
  assertFailure "trailing block bytes are noncanonical" NonCanonicalBlock
    (fst <$> importPack trailingPack policy emptyBlockStore)

  let wrongMagicBytes = ByteString.cons 0 (ByteString.drop 1 (merkleBlockBytes root))
      wrongMagic = MerkleBlock (hashBytes wrongMagicBytes) wrongMagicBytes
  wrongMagicPack <- expectRight "construct malformed pack"
    (makeMerkleBlockPack (packAlgorithmId pack) (packDomainDigest pack)
      (merkleBlockDigest wrongMagic) [wrongMagic])
  assertFailure "wrong block magic is malformed" MalformedBlock
    (fst <$> importPack wrongMagicPack policy emptyBlockStore)

  foreignPolicy <- policyWithId "foreign-haskell-persistence-v1"
  foreignTree <- createTree foreignPolicy 8
  let foreignPack = exportPack foreignTree
  relabeled <- expectRight "relabel foreign pack"
    (makeMerkleBlockPack (packAlgorithmId pack) (packDomainDigest pack)
      (packRootDigest foreignPack) (packBlocks foreignPack))
  assertFailure "foreign block domain is rejected" DomainMismatch
    (fst <$> importPack relabeled policy emptyBlockStore)

  unsupported <- expectRight "construct unsupported envelope"
    (makeMerkleBlockPack "mst-sha256-b16-v999" (packDomainDigest pack)
      (packRootDigest pack) (packBlocks pack))
  assertFailure "unsupported algorithm is rejected" UnsupportedAlgorithm
    (fst <$> importPack unsupported policy emptyBlockStore)

  let conflict = MerkleBlock (merkleBlockDigest missing) (ByteString.pack [0xde, 0xad])
  (_, conflictStore) <- expectRight "seed conflicting destination"
    (putBlock conflict emptyBlockStore)
  assertFailure "save preflights late conflict" ConflictingBlock
    (snd <$> saveTree tree conflictStore)
  assertEqual "failed save returns no successor mutation" 1 (blockStoreSize conflictStore)
  assertFailure "import preflights late conflict" ConflictingBlock
    (snd <$> importPack pack policy conflictStore)
  assertEqual "failed import returns no successor mutation" 1 (blockStoreSize conflictStore)

  referenceTree <- createTree policy 2049
  let referencePack = exportPack referenceTree
      referenceRoot = findBlock (packRootDigest referencePack) (packBlocks referencePack)
      declaredCount = readInt32At 38 (merkleBlockBytes referenceRoot)
      wrongCountBytes = replaceBytes 38 4 (int32BigEndian (declaredCount + 1))
        (merkleBlockBytes referenceRoot)
      wrongCount = MerkleBlock (hashBytes wrongCountBytes) wrongCountBytes
  wrongCountPack <- replaceRoot referencePack wrongCount
  assertFailure "authenticated wrong subtree count is rejected" InvalidReference
    (fst <$> importPack wrongCountPack policy emptyBlockStore)

  let crossedBytes = swapFirstNonemptyChildren (merkleEmptyDigest policy)
        (merkleBlockBytes referenceRoot)
      crossed = MerkleBlock (hashBytes crossedBytes) crossedBytes
  crossedPack <- replaceRoot referencePack crossed
  assertFailure "authenticated crossed child intervals are rejected" InvalidReference
    (fst <$> importPack crossedPack policy emptyBlockStore)

testAllSevenBudgets :: IO ()
testAllSevenBudgets = do
  policy <- testPolicy
  tree <- createTree policy 513
  let pack = exportPack tree
      defaults = defaultMerkleVerificationBudget
      rootLength = ByteString.length (merkleBlockBytes
        (findBlock (packRootDigest pack) (packBlocks pack)))
      reject label budget = assertFailure label ResourceLimitExceeded
        (fst <$> importPackWithBudget pack policy emptyBlockStore budget)
      make blocks total blockBytes depth entries children queryBytes =
        expectPureRight "construct verification budget"
          (makeMerkleVerificationBudget blocks total blockBytes depth entries children queryBytes)
  reject "block-count budget" (make 1 (maxTotalByteCount defaults)
    (maxBlockByteCount defaults) (maxDepth defaults) (maxEntryCount defaults)
    (maxChildReferencesPerBlock defaults) (maxProofQueryByteCount defaults))
  reject "total-byte budget" (make (maxBlockCount defaults) (fromIntegral rootLength)
    rootLength (maxDepth defaults) (maxEntryCount defaults)
    (maxChildReferencesPerBlock defaults) rootLength)
  reject "per-block budget" (make (maxBlockCount defaults) (maxTotalByteCount defaults)
    (rootLength - 1) (maxDepth defaults) (maxEntryCount defaults)
    (maxChildReferencesPerBlock defaults) (rootLength - 1))
  reject "reference-depth budget" (make (maxBlockCount defaults) (maxTotalByteCount defaults)
    (maxBlockByteCount defaults) 1 (maxEntryCount defaults)
    (maxChildReferencesPerBlock defaults) (maxProofQueryByteCount defaults))
  reject "decoded-entry budget" (make (maxBlockCount defaults) (maxTotalByteCount defaults)
    (maxBlockByteCount defaults) (maxDepth defaults) 1
    (maxChildReferencesPerBlock defaults) (maxProofQueryByteCount defaults))
  reject "child-reference budget" (make (maxBlockCount defaults) (maxTotalByteCount defaults)
    (maxBlockByteCount defaults) (maxDepth defaults) (maxEntryCount defaults) 1
    (maxProofQueryByteCount defaults))
  proof <- expectRight "create budget proof" (createProof 0 tree)
  let queryBudget = make (maxBlockCount defaults) (maxTotalByteCount defaults)
        (maxBlockByteCount defaults) (maxDepth defaults) (maxEntryCount defaults)
        (maxChildReferencesPerBlock defaults) (ByteString.length (proofQuery proof) - 1)
      queryResult = verifyProofWithBudget proof policy queryBudget
  assertEqual "proof-query budget classification" ResourceLimitExceeded (proofFailureKind queryResult)
  assertEqual "proof-query budget decodes no blocks" 0 (proofVerifiedBlockCount queryResult)
  assertEqual "proof-query budget accounts no bytes" 0 (proofVerifiedByteCount queryResult)

  assertBool "zero budget rejected" (isLeft
    (makeMerkleVerificationBudget 0 1 1 1 1 1 1))
  assertBool "block greater than total rejected" (isLeft
    (makeMerkleVerificationBudget 1 1 2 1 1 1 1))
  assertBool "query greater than total rejected" (isLeft
    (makeMerkleVerificationBudget 1 1 1 1 1 1 2))

testProofAdmissionPrecedesCodecs :: IO ()
testProofAdmissionPrecedesCodecs = do
  normalPolicy <- policyWithId "haskell-proof-admission-v1"
  tree <- createTree normalPolicy 513
  proof <- expectRight "create admission proof" (createProof 0 tree)
  assertBool "admission proof spans blocks" (length (proofSteps proof) > 1)
  bombPolicy <- expectRight "construct same-domain bomb policy"
    (makeMerkleSearchTreePolicy "haskell-proof-admission-v1" compare
      (bombCodec int32MerkleCodec) (bombCodec nullableUtf8MerkleCodec))
  assertEqual "bomb policy preserves domain" (merkleDomainDigest normalPolicy)
    (merkleDomainDigest bombPolicy)
  decodeBombPolicy <- expectRight "construct same-domain decode-bomb policy"
    (makeMerkleSearchTreePolicy "haskell-proof-admission-v1" compare
      (decodeBombCodec int32MerkleCodec) (decodeBombCodec nullableUtf8MerkleCodec))
  decodeBombTree <- createTree decodeBombPolicy 257
  decodeBombProof <- expectRight "local proof construction never decodes retained entries"
    (createProof 0 decodeBombTree)
  assertValidProof "decode-free local proof construction" decodeBombProof normalPolicy
  assertEqual "decode-free local sync construction" (exportPack decodeBombTree)
    (createSyncPack emptyBlockStore decodeBombTree)
  let defaults = defaultMerkleVerificationBudget
      make blocks children queryBytes = expectPureRight "construct proof admission budget"
        (makeMerkleVerificationBudget blocks (maxTotalByteCount defaults)
          (maxBlockByteCount defaults) (maxDepth defaults) (maxEntryCount defaults)
          children queryBytes)
      queryLimited = make (maxBlockCount defaults) (maxChildReferencesPerBlock defaults)
        (ByteString.length (proofQuery proof) - 1)
      queryResult = verifyProofWithBudget proof bombPolicy queryLimited
  assertEarlyLimit "query preflight precedes bomb codecs" 0 queryResult

  let stepLimited = make (length (proofSteps proof) - 1)
        (maxChildReferencesPerBlock defaults) (maxProofQueryByteCount defaults)
      stepResult = verifyProofWithBudget proof bombPolicy stepLimited
  assertEarlyLimit "step preflight precedes bomb codecs"
    (fromIntegral (ByteString.length (proofQuery proof))) stepResult

  let firstStep = firstTotal "first proof step" (proofSteps proof)
  expandedStep <- expectRight "construct expansion bomb proof step"
    (makeMerkleProofStep (proofStepBlock firstStep) [0, 1])
  expandedProof <- expectRight "construct expansion bomb proof"
    (makeMerkleProof (proofAlgorithmId proof) (proofDomainDigest proof)
      (proofRootDigest proof) (proofKind proof) (proofQuery proof)
      (expandedStep : drop 1 (proofSteps proof)))
  let expansionResult = verifyProofWithBudget expandedProof bombPolicy
        (make (maxBlockCount defaults) 1 (maxProofQueryByteCount defaults))
  assertEarlyLimit "expansion preflight precedes bomb codecs"
    (fromIntegral (ByteString.length (proofQuery proof))) expansionResult

testPointRangeAndTamperedProofs :: IO ()
testPointRangeAndTamperedProofs = do
  policy <- testPolicy
  tree <- createTree policy 513
  membership <- expectRight "create membership proof" (createProof 0 tree)
  assertEqual "membership kind" MembershipProof (proofKind membership)
  assertEqual "membership MSP2 prefix" (ByteString.pack [77, 83, 80, 50, 0])
    (ByteString.take 5 (proofQuery membership))
  assertValidProof "membership proof" membership policy
  nonmembership <- expectRight "create nonmembership proof" (createProof 10000 tree)
  assertEqual "nonmembership kind" NonMembershipProof (proofKind nonmembership)
  assertValidProof "nonmembership proof" nonmembership policy
  rangeProof <- expectRight "create range proof" (createRangeProof (-20) 20 tree)
  assertEqual "range kind" RangeProof (proofKind rangeProof)
  assertValidProof "inclusive range proof" rangeProof policy
  assertBool "reversed range proof rejected" (isLeft (createRangeProof 20 (-20) tree))

  let empty = Tree.empty policy
  emptyPoint <- expectRight "create empty point proof" (createProof 1 empty)
  emptyRange <- expectRight "create empty range proof" (createRangeProof (-1) 1 empty)
  assertValidProof "empty nonmembership proof" emptyPoint policy
  assertValidProof "empty range proof" emptyRange policy

  let firstStep = firstTotal "first membership step" (proofSteps membership)
      damagedBlock = (proofStepBlock firstStep)
        { merkleBlockBytes = flipLastBit (merkleBlockBytes (proofStepBlock firstStep)) }
  damagedStep <- expectRight "construct damaged proof step"
    (makeMerkleProofStep damagedBlock (proofStepExpandedChildIndexes firstStep))
  damaged <- rebuildProof membership (damagedStep : drop 1 (proofSteps membership))
    (proofQuery membership)
  assertInvalidProof "tampered proof block" DigestMismatch damaged policy
  let damagedResult = verifyProof damaged policy
  assertEqual "tampered proof accounts admitted block" 1 (proofVerifiedBlockCount damagedResult)
  assertEqual "tampered proof accounts query and admitted block"
    (fromIntegral (ByteString.length (proofQuery damaged)
      + ByteString.length (merkleBlockBytes damagedBlock)))
    (proofVerifiedByteCount damagedResult)

  trailing <- rebuildProof membership (proofSteps membership) (ByteString.snoc (proofQuery membership) 0)
  assertInvalidProof "trailing proof query" ProofMismatch trailing policy
  wrongExpansion <- expectRight "construct wrong expansion"
    (makeMerkleProofStep (proofStepBlock firstStep) [])
  wrong <- rebuildProof membership (wrongExpansion : drop 1 (proofSteps membership))
    (proofQuery membership)
  assertInvalidProof "noncanonical point expansion" ProofMismatch wrong policy
  omitted <- rebuildProof membership (dropLast (proofSteps membership)) (proofQuery membership)
  assertInvalidProof "omitted proof child" MissingBlock omitted policy

  let proofDigests = map (merkleBlockDigest . proofStepBlock) (proofSteps membership)
      extraBlock = firstTotal "extra proof block"
        (filter ((`notElem` proofDigests) . merkleBlockDigest) (packBlocks (exportPack tree)))
  extraStep <- expectRight "construct extra proof step" (makeMerkleProofStep extraBlock [])
  extra <- rebuildProof membership (proofSteps membership ++ [extraStep]) (proofQuery membership)
  assertInvalidProof "unreachable proof step" ProofMismatch extra policy

testSynchronization :: IO ()
testSynchronization = do
  policy <- testPolicy
  target <- createTree policy 513
  let local = Tree.empty policy
      complete = createSyncPack emptyBlockStore target
  assertEqual "empty receiver gets complete closure" (exportPack target) complete
  (completeTree, completeStore) <- expectRight "import complete sync pack"
    (importPack complete policy emptyBlockStore)
  assertTreesEqual "complete sync tree" target completeTree

  let missing = lastTotal "sync missing leaf" (packBlocks (exportPack target))
      (_, partialStore) = removeBlock (merkleBlockDigest missing) completeStore
  plan <- expectRight "plan one missing leaf" (planSync local partialStore target)
  assertEqual "partial plan requests one frontier" [merkleBlockDigest missing]
    (syncRequestedBlocks plan)
  partialPack <- expectRight "export requested frontier"
    (exportPackFor (syncRequestedBlocks plan) target)
  assertBool "partial frontier need not contain root" (not (packContainsRootBlock partialPack))
  (repaired, repairedStore) <- expectRight "repair partial store"
    (importPack partialPack policy partialStore)
  assertTreesEqual "partial sync repair" target repaired
  converged <- expectRight "plan converged roots" (planSync repaired repairedStore target)
  assertEqual "converged plan has no requests" [] (syncRequestedBlocks converged)
  assertEqual "converged plan examines no blocks" 0 (syncExaminedBlockCount converged)

  (rounds, frontierStore) <- repairIteratively 0 emptyBlockStore local target
  assertBool "iterative repair takes at least tree height" (rounds >= Tree.height target)
  frontierLoaded <- expectRight "load iterative repair"
    (loadTree (Tree.rootDigest target) policy frontierStore)
  assertTreesEqual "iterative frontier repair" target frontierLoaded

testMerge :: IO ()
testMerge = do
  policy <- testPolicy
  base <- expectRight "construct merge base"
    (Tree.fromList [(1, Just "one"), (2, Just "two"), (3, Just "three")] policy)
  left <- expectRight "edit merge left" (Tree.insert 1 (Just "ONE") base)
  right <- expectRight "edit merge right" (Tree.insert 2 (Just "TWO") base)
  disjoint <- expectRight "merge disjoint edits" (mergeThreeWay Nothing base left right)
  mergedDisjoint <- expectMerged "disjoint merge" disjoint
  assertEqual "merge retains left edit" (Just (Just "ONE")) (Tree.lookup 1 mergedDisjoint)
  assertEqual "merge retains right edit" (Just (Just "TWO")) (Tree.lookup 2 mergedDisjoint)

  conflictLeft <- expectRight "construct conflicting left" (Tree.insert 1 (Just "left") base)
  conflictRight <- expectRight "construct conflicting right" (Tree.insert 1 (Just "right") base)
  unresolved <- expectRight "collect unresolved conflict"
    (mergeThreeWay Nothing base conflictLeft conflictRight)
  case unresolved of
    MerkleMergeSucceeded _ -> fail "unresolved merge exposed a partial tree"
    MerkleMergeConflicted conflicts -> do
      let conflict = only "merge conflict" conflicts
      assertEqual "conflict base value" (MergePresent (Just "one")) (mergeConflictBase conflict)
      assertEqual "conflict left value" (MergePresent (Just "left")) (mergeConflictLeft conflict)
      assertEqual "conflict right value" (MergePresent (Just "right")) (mergeConflictRight conflict)
  resolved <- expectRight "resolve merge conflict"
    (mergeThreeWay (Just (const (MergeSetValue (Just "resolved"))))
      base conflictLeft conflictRight)
  mergedResolved <- expectMerged "resolved merge" resolved
  assertEqual "resolver value published" (Just (Just "resolved")) (Tree.lookup 1 mergedResolved)

  presentNothing <- expectRight "set present Nothing" (Tree.insert 1 Nothing base)
  deleted <- expectRight "delete merge side" (Tree.delete 1 base)
  nullConflict <- expectRight "merge present Nothing against deletion"
    (mergeThreeWay Nothing base presentNothing deleted)
  case nullConflict of
    MerkleMergeSucceeded _ -> fail "present Nothing was confused with deletion"
    MerkleMergeConflicted conflicts -> do
      let conflict = only "present-null conflict" conflicts
      assertEqual "present Nothing remains present" (MergePresent Nothing) (mergeConflictLeft conflict)
      assertEqual "deletion remains absent" MergeAbsent (mergeConflictRight conflict)

  keepNothing <- expectRight "resolve with present Nothing"
    (mergeThreeWay (Just (const (MergeSetValue Nothing))) base presentNothing deleted)
  mergedNothing <- expectMerged "present Nothing resolution" keepNothing
  assertBool "resolved Nothing key is present" (Tree.member 1 mergedNothing)
  assertEqual "resolved Nothing value" (Just Nothing) (Tree.lookup 1 mergedNothing)

testRetainedVersionsAndModel :: IO ()
testRetainedVersionsAndModel = do
  policy <- testPolicy
  base <- createTree policy 257
  changed <- expectRight "change retained tree" (Tree.insert 0 (Just "changed") base)
  removed <- expectRight "remove from retained tree" (Tree.delete 1 changed)
  (_, baseStore) <- expectRight "save retained base" (saveTree base emptyBlockStore)
  (_, allStore) <- expectRight "save retained successor" (saveTree removed baseStore)
  loadedBase <- expectRight "load retained base" (loadTree (Tree.rootDigest base) policy allStore)
  loadedRemoved <- expectRight "load retained successor" (loadTree (Tree.rootDigest removed) policy allStore)
  assertTreesEqual "old root remains loadable" base loadedBase
  assertTreesEqual "new root remains loadable" removed loadedRemoved

  let states = take 2000 (iterate (\state -> state * 1664525 + 1013904223) (0x13579bdf :: Int))
      apply result state = do
        (tree, model) <- result
        let key = fromIntegral (((state `div` 256) `mod` 101) - 50) :: Int32
            value = if state `mod` 17 == 0 then Nothing else Just ("m:" ++ show state)
            rest = filter ((/= key) . fst) model
        if state `mod` 5 == 0
          then do successor <- Tree.delete key tree; Right (successor, rest)
          else do successor <- Tree.insert key value tree; Right (successor, (key, value) : rest)
  (modeled, model) <- expectRight "run persistence model"
    (foldl' apply (Right (Tree.empty policy, [])) states)
  (_, modelStore) <- expectRight "save modeled tree" (saveTree modeled emptyBlockStore)
  loaded <- expectRight "load modeled tree" (loadTree (Tree.rootDigest modeled) policy modelStore)
  assertEqual "modeled persistence contents" (Tree.toAscList modeled) (Tree.toAscList loaded)
  assertEqual "model agrees after persistence" (sortPairs model) (Tree.toAscList loaded)

repairIteratively
  :: Int -> MerkleBlockStore -> TestTree -> TestTree -> IO (Int, MerkleBlockStore)
repairIteratively rounds store local target = do
  plan <- expectRight "plan iterative sync" (planSync local store target)
  case syncRequestedBlocks plan of
    [] -> pure (rounds, store)
    requested -> do
      pack <- expectRight "export iterative frontier" (exportPackFor requested target)
      nextStore <- foldl' put (pure store) (packBlocks pack)
      repairIteratively (rounds + 1) nextStore local target
  where
    put result block = do
      current <- result
      (_, successor) <- expectRight "publish iterative frontier" (putBlock block current)
      pure successor

bombCodec :: MerkleCodec a -> MerkleCodec a
bombCodec codec = MerkleCodec
  { merkleEncodingId = merkleEncodingId codec
  , encodeMerkleValue = \_ -> error "proof admission forced bomb encoder"
  , decodeMerkleValue = \_ -> error "proof admission forced bomb decoder"
  }

decodeBombCodec :: MerkleCodec a -> MerkleCodec a
decodeBombCodec codec = MerkleCodec
  { merkleEncodingId = merkleEncodingId codec
  , encodeMerkleValue = encodeMerkleValue codec
  , decodeMerkleValue = \_ -> error "local trusted construction forced bomb decoder"
  }

rebuildProof :: MerkleProof -> [MerkleProofStep] -> ByteString -> IO MerkleProof
rebuildProof proof steps query = expectRight "rebuild proof"
  (makeMerkleProof (proofAlgorithmId proof) (proofDomainDigest proof)
    (proofRootDigest proof) (proofKind proof) query steps)

replaceBlock :: MerkleBlockPack -> MerkleBlock -> IO MerkleBlockPack
replaceBlock pack replacement = expectRight "replace pack block"
  (makeMerkleBlockPack (packAlgorithmId pack) (packDomainDigest pack)
    (packRootDigest pack)
    [ if merkleBlockDigest block == merkleBlockDigest replacement then replacement else block
    | block <- packBlocks pack
    ])

replaceRoot :: MerkleBlockPack -> MerkleBlock -> IO MerkleBlockPack
replaceRoot pack replacement = expectRight "replace addressed root"
  (makeMerkleBlockPack (packAlgorithmId pack) (packDomainDigest pack)
    (merkleBlockDigest replacement)
    [ if merkleBlockDigest block == packRootDigest pack then replacement else block
    | block <- packBlocks pack
    ])

findBlock :: MerkleDigest -> [MerkleBlock] -> MerkleBlock
findBlock digest blocks = case filter ((== digest) . merkleBlockDigest) blocks of
  [block] -> block
  _ -> error "test block digest was not unique"

assertTreesEqual :: String -> TestTree -> TestTree -> IO ()
assertTreesEqual label expected actual = do
  assertEqual (label ++ " root") (Tree.rootDigest expected) (Tree.rootDigest actual)
  assertEqual (label ++ " contents") (Tree.toAscList expected) (Tree.toAscList actual)
  assertEqual (label ++ " blocks") (exportPack expected) (exportPack actual)
  assertBool (label ++ " validates") (isRight (Tree.validateStructure actual))

assertFailure
  :: String -> MerkleVerificationFailureKind
  -> Either MerkleVerificationError a -> IO ()
assertFailure label expected result = case result of
  Left problem -> do
    assertEqual (label ++ " classification") expected (verificationFailureKind problem)
    assertBool (label ++ " diagnostic") (not (null (verificationMessage problem)))
  Right _ -> fail (label ++ ": expected failure")

assertValidProof
  :: String -> MerkleProof -> MerkleSearchTreePolicy Int32 Value -> IO ()
assertValidProof label proof policy = do
  let result = verifyProof proof policy
  assertBool label (proofIsValid result)
  assertEqual (label ++ " classification") NoVerificationFailure (proofFailureKind result)
  assertEqual (label ++ " block accounting") (length (proofSteps proof))
    (proofVerifiedBlockCount result)
  assertEqual (label ++ " byte accounting") (proofTotalByteCount proof)
    (proofVerifiedByteCount result)

assertInvalidProof
  :: String -> MerkleVerificationFailureKind -> MerkleProof
  -> MerkleSearchTreePolicy Int32 Value -> IO ()
assertInvalidProof label expected proof policy = do
  let result = verifyProof proof policy
  assertBool (label ++ " is invalid") (not (proofIsValid result))
  assertEqual (label ++ " classification") expected (proofFailureKind result)

assertEarlyLimit :: String -> Word64 -> MerkleProofVerificationResult -> IO ()
assertEarlyLimit label expectedBytes result = do
  assertBool (label ++ " fails") (not (proofIsValid result))
  assertEqual (label ++ " classification") ResourceLimitExceeded (proofFailureKind result)
  assertEqual (label ++ " blocks") 0 (proofVerifiedBlockCount result)
  assertEqual (label ++ " bytes") (fromIntegral expectedBytes) (proofVerifiedByteCount result)

expectMerged :: String -> MerkleThreeWayMergeResult k v -> IO (Tree.MerkleSearchTree k v)
expectMerged _ (MerkleMergeSucceeded tree) = pure tree
expectMerged label (MerkleMergeConflicted _) = fail (label ++ ": unexpectedly conflicted")

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool _ True = pure ()
assertBool label False = fail (label ++ ": expected true")

expectRight :: Show e => String -> Either e a -> IO a
expectRight _ (Right value) = pure value
expectRight label (Left problem) = fail (label ++ ": " ++ show problem)

expectPureRight :: Show e => String -> Either e a -> a
expectPureRight _ (Right value) = value
expectPureRight label (Left problem) = error (label ++ ": " ++ show problem)

only :: String -> [a] -> a
only _ [value] = value
only label _ = error (label ++ " was not a singleton")

firstTotal :: String -> [a] -> a
firstTotal _ (value : _) = value
firstTotal label [] = error (label ++ " was empty")

lastTotal :: String -> [a] -> a
lastTotal label values = case values of
  [] -> error (label ++ " was empty")
  first : rest -> foldl' (\_ value -> value) first rest

dropLast :: [a] -> [a]
dropLast [] = []
dropLast [_] = []
dropLast (value : rest) = value : dropLast rest

flipLastBit :: ByteString -> ByteString
flipLastBit bytes = case ByteString.unsnoc bytes of
  Nothing -> error "cannot tamper with empty bytes"
  Just (prefix, final) -> ByteString.snoc prefix (final `xor` 1)

readInt32At :: Int -> ByteString -> Int32
readInt32At offset bytes =
  let word = fromIntegral (ByteString.index bytes offset) * 0x1000000
        + fromIntegral (ByteString.index bytes (offset + 1)) * 0x10000
        + fromIntegral (ByteString.index bytes (offset + 2)) * 0x100
        + fromIntegral (ByteString.index bytes (offset + 3)) :: Word64
   in fromIntegral word

replaceBytes :: Int -> Int -> ByteString -> ByteString -> ByteString
replaceBytes offset count replacement source = ByteString.concat
  [ ByteString.take offset source
  , replacement
  , ByteString.drop (offset + count) source
  ]

childDigestOffset :: ByteString -> (Int, Int)
childDigestOffset bytes = (walk 46 entryCount, entryCount + 1)
  where
    entryCount = fromIntegral (readInt32At 42 bytes)
    walk offset 0 = offset
    walk offset remaining =
      let keyLength = fromIntegral (readInt32At offset bytes)
          valueOffset = offset + 4 + keyLength
          valueLength = fromIntegral (readInt32At valueOffset bytes)
       in walk (valueOffset + 4 + valueLength) (remaining - 1)

swapFirstNonemptyChildren :: MerkleDigest -> ByteString -> ByteString
swapFirstNonemptyChildren emptyDigest bytes = case nonempty of
  first : second : _ ->
    let firstOffset = offset + first * 32
        secondOffset = offset + second * 32
        firstBytes = ByteString.take 32 (ByteString.drop firstOffset bytes)
        secondBytes = ByteString.take 32 (ByteString.drop secondOffset bytes)
     in replaceBytes secondOffset 32 firstBytes
          (replaceBytes firstOffset 32 secondBytes bytes)
  _ -> error "reference-tamper root did not contain two nonempty children"
  where
    (offset, count) = childDigestOffset bytes
    nonempty =
      [ index
      | index <- [0 .. count - 1]
      , ByteString.take 32 (ByteString.drop (offset + index * 32) bytes) /= digestBytes emptyDigest
      ]

decodeHex :: String -> ByteString
decodeHex source = ByteString.pack (go source)
  where
    go [] = []
    go (high : low : rest) = (hex high * 16 + hex low) : go rest
    go _ = error "odd test hexadecimal length"
    hex value
      | value >= '0' && value <= '9' = fromIntegral (fromEnum value - fromEnum '0')
      | value >= 'a' && value <= 'f' = fromIntegral (fromEnum value - fromEnum 'a' + 10)
      | otherwise = error "invalid test hexadecimal digit"

sortPairs :: Ord a => [(a, b)] -> [(a, b)]
sortPairs = sortByKey
  where
    sortByKey [] = []
    sortByKey (value : rest) =
      sortByKey [item | item <- rest, fst item < fst value]
        ++ [value]
        ++ sortByKey [item | item <- rest, fst item >= fst value]

isLeft :: Either a b -> Bool
isLeft (Left _) = True
isLeft _ = False

isRight :: Either a b -> Bool
isRight (Right _) = True
isRight _ = False
