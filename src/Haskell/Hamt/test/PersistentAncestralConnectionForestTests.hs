{-# LANGUAGE BangPatterns #-}

-- | Tests for the persistent ancestral connection forest: version-tagged union edges, fully
-- branching histories, persistence, and the first-connection query.
module PersistentAncestralConnectionForestTests (run) where

import Control.Monad (forM_)
import Data.Bits (shiftR, xor)
import qualified Data.List as List
import Data.Maybe (isNothing)
import Data.Word (Word32, Word64)

import Durable7.Hamt.PersistentAncestralConnectionForest
  ( AncestralConnectionForestStatistics(..)
  , AncestralConnectionVersion
  , PersistentAncestralConnectionForest
  )
import qualified Durable7.Hamt.HashMap as HashMap
import qualified Durable7.Hamt.PersistentAncestralConnectionForest as Forest

-- | Runs this module's test cases.
run :: IO ()
run = do
  testCreateProducesSingletonComponentsAndRootHistory
  testLinkRecordsTheFirstConnectionVersion
  testComponentSizeReportsTheCachedUnionSize
  testRedundantLinksCreateVersionsAndShareConnectivityState
  testSuccessfulUnionRebuildsOnlyTheTouchedCells
  testSiblingBranchesKeepEqualDepthVersionTagsDistinct
  testFirstConnectedUsesPairLcaRatherThanLatestComponentBirth
  testFirstConnectedHandlesOneEndpointAtLca
  testBalancedUnionsRespectUnionBySizeHeight
  testRandomBranchingHistoriesMatchNaiveAncestorScan
  testLongRedundantHistoryKeepsTheUnionWitness
  testHugeSparseUniverseStoresOnlyTouchedVertices
  testOperationsRejectVerticesOutsideTheUniverse
  testVersionIdentityIsStructural
  testThePinnedVertexHashIsInjectiveByInverseRoundTrip
  testCreateRejectsAUniverseTheInjectiveHashCannotCover


-- | Inverting the pinned hash step by step proves injectivity on the tested vertices: an
-- xor-shift by at least half the word is self-inverse, the 13-bit xor-shift inverts by applying
-- its expansion twice, and each odd multiplier inverts by its Newton-iterated modular inverse.
-- Exhibiting the inverse is what a sampling-only test cannot do.
testThePinnedVertexHashIsInjectiveByInverseRoundTrip :: IO ()
testThePinnedVertexHashIsInjectiveByInverseRoundTrip = do
  let modularInverse :: Word32 -> Word32
      modularInverse value = iterate step value !! 5
        where step inverse = inverse * (2 - value * inverse)
      firstInverse = modularInverse 0x85EBCA6B
      secondInverse = modularInverse 0xC2B2AE35
  assertEqual "first multiplier inverts" 1 (firstInverse * 0x85EBCA6B)
  assertEqual "second multiplier inverts" 1 (secondInverse * 0xC2B2AE35)
  let unmix :: Word32 -> Word32
      unmix hashed = original
        where
          step4 = hashed `xor` (hashed `shiftR` 16)
          step3 = step4 * secondInverse
          step2 = step3 `xor` (step3 `shiftR` 13) `xor` (step3 `shiftR` 26)
          step1 = step2 * firstInverse
          original = step1 `xor` (step1 `shiftR` 16)
      hashOf :: Int -> Word32
      hashOf = fromIntegral . HashMap.hashKey Forest.vertexHashPolicy
      samples =
        [index * 524287 `mod` Forest.maximumVertexCount | index <- [0 .. 8191]]
          ++ [0, 1, Forest.maximumVertexCount - 1, Forest.maximumVertexCount]
  forM_ samples $ \vertex ->
    assertEqual ("fmix32 round-trips " ++ show vertex)
      vertex
      (fromIntegral (unmix (hashOf vertex)))

-- | The universe cap is load-bearing: a 32-bit hash cannot be injective over a larger domain.
testCreateRejectsAUniverseTheInjectiveHashCannotCover :: IO ()
testCreateRejectsAUniverseTheInjectiveHashCannotCover = do
  assertBool "the maximal universe is admitted"
    (not (isNothing (Forest.create Forest.maximumVertexCount)))
  assertBool "one past the cap is rejected"
    (isNothing (Forest.create (Forest.maximumVertexCount + 1)))
  assertBool "a negative universe is still rejected" (isNothing (Forest.create (-1)))

-- | An edgeless root uses the fixed universe and root-version semantics.
testCreateProducesSingletonComponentsAndRootHistory :: IO ()
testCreateProducesSingletonComponentsAndRootHistory = do
  let root = forestOf "create 5" 5
      rootVersion = Forest.version root
  assertEqual "root vertex count" 5 (Forest.vertexCount root)
  assertEqual "root component count" 5 (Forest.componentCount root)
  assertEqual "root depth" 0 (Forest.versionDepth rootVersion)
  assertBool "root has no parent" (isNothing (Forest.versionParent rootVersion))
  assertEqual "root is its own history root" rootVersion (Forest.versionRoot rootVersion)
  forM_ [0 .. 4] $ \vertex -> do
    assertEqual "singleton representative" (Just vertex) (Forest.find vertex root)
    assertEqual "self connected" (Just True) (Forest.connected vertex vertex root)
    assertEqual "self witness is the history root"
      (Just (Just rootVersion))
      (Forest.firstConnected vertex vertex root)
    assertEqual "singleton size" (Just 1) (Forest.componentSize vertex root)
  assertEqual "distinct singletons are disconnected" (Just False) (Forest.connected 0 1 root)
  assertEqual "disconnected pair has no witness" (Just Nothing) (Forest.firstConnected 0 1 root)

  statistics <- expectValid "root" root
  assertEqual "root stores no cells" 0 (statisticsStoredCellCount statistics)
  assertEqual "root validated components" 5 (statisticsComponentCount statistics)
  assertEqual "root validated path length" 0 (statisticsMaximumParentPathLength statistics)
  assertEqual "root validated depth" 0 (statisticsHistoryDepth statistics)

  let empty = forestOf "create 0" 0
  assertEqual "empty universe" 0 (Forest.vertexCount empty)
  assertEqual "empty components" 0 (Forest.componentCount empty)
  _ <- expectValid "empty" empty
  assertBool "negative universe rejected" (isNothing (Forest.create (-1)))

-- | Successful links retain the exact first version for old and cross-component pairs.
testLinkRecordsTheFirstConnectionVersion :: IO ()
testLinkRecordsTheFirstConnectionVersion = do
  let root = forestOf "link root" 6
      first = linkOf "0-1" 0 1 root
      second = linkOf "2-3" 2 3 first
      bridge = linkOf "0-2" 0 2 second
      attach = linkOf "4-0" 4 0 bridge
  assertEqual "attach components" 2 (Forest.componentCount attach)
  assertWitness "0-1 witness" first attach 0 1
  assertWitness "2-3 witness" second attach 2 3
  assertWitness "0-3 witness" bridge attach 0 3
  assertWitness "1-2 witness" bridge attach 1 2
  assertWitness "4-1 witness" attach attach 4 1
  assertEqual "self witness is the history root"
    (Just (Just (Forest.versionRoot (Forest.version attach))))
    (Forest.firstConnected 4 4 attach)
  assertEqual "0-5 remains disconnected" (Just Nothing) (Forest.firstConnected 0 5 attach)
  assertEqual "union-by-size representative" (Just 0) (Forest.find 0 attach)
  assertEqual "attached vertex representative" (Just 0) (Forest.find 4 attach)

  -- Every retained version keeps its own connectivity, untouched by later descendants.
  assertEqual "root components retained" 6 (Forest.componentCount root)
  assertEqual "root pair still separate" (Just False) (Forest.connected 0 1 root)
  assertEqual "first pair still separate" (Just False) (Forest.connected 0 2 first)
  assertEqual "first components retained" 5 (Forest.componentCount first)
  forM_ [("root", root), ("first", first), ("second", second), ("bridge", bridge), ("attach", attach)] $
    \(label, forest) -> do
      _ <- expectValid label forest
      pure ()

-- | Component sizes are cached-root reads that agree with the connectivity partition.
testComponentSizeReportsTheCachedUnionSize :: IO ()
testComponentSizeReportsTheCachedUnionSize = do
  let root = forestOf "size root" 6
  forM_ [0 .. 5] $ \vertex ->
    assertEqual "isolated size" (Just 1) (Forest.componentSize vertex root)

  let first = linkOf "0-1" 0 1 root
  assertEqual "pair size at 0" (Just 2) (Forest.componentSize 0 first)
  assertEqual "pair size at 1" (Just 2) (Forest.componentSize 1 first)
  assertEqual "untouched size" (Just 1) (Forest.componentSize 2 first)

  let second = linkOf "2-3" 2 3 first
      bridge = linkOf "0-2" 0 2 second
      attach = linkOf "4-0" 4 0 bridge
  assertEqual "second size" (Just 2) (Forest.componentSize 3 second)
  assertEqual "bridge size" (Just 4) (Forest.componentSize 3 bridge)
  assertEqual "attach size" (Just 5) (Forest.componentSize 4 attach)
  assertEqual "attach singleton" (Just 1) (Forest.componentSize 5 attach)
  forM_ [0 .. 4] $ \vertex ->
    assertEqual "whole component agrees" (Just 5) (Forest.componentSize vertex attach)

  -- A redundant link changes no size, and retained versions keep their own sizes.
  let redundant = linkOf "1-4" 1 4 attach
  assertEqual "redundant size" (Just 5) (Forest.componentSize 1 redundant)
  assertEqual "redundant singleton" (Just 1) (Forest.componentSize 5 redundant)
  assertEqual "ancestor size retained" (Just 2) (Forest.componentSize 0 first)
  assertEqual "bridge size retained" (Just 4) (Forest.componentSize 0 bridge)

  -- Sibling branches over the same ancestor keep independent sizes.
  let sibling = linkOf "5-0" 5 0 bridge
  assertEqual "sibling size" (Just 5) (Forest.componentSize 5 sibling)
  assertEqual "attach unaffected" (Just 1) (Forest.componentSize 5 attach)
  assertEqual "bridge unaffected" (Just 4) (Forest.componentSize 0 bridge)

  forM_ [ ("size root", root), ("first", first), ("second", second), ("bridge", bridge)
        , ("attach", attach), ("redundant", redundant), ("sibling", sibling) ] $
    \(label, forest) -> do
      let sizes = componentSizesByRoot forest
      assertEqual (label ++ " distinct roots") (Forest.componentCount forest) (length sizes)
      assertEqual (label ++ " sizes partition the universe")
        (Forest.vertexCount forest)
        (sum (map snd sizes))
      _ <- expectValid label forest
      pure ()

-- | Redundant events advance history but retain the exact persistent connectivity index.
testRedundantLinksCreateVersionsAndShareConnectivityState :: IO ()
testRedundantLinksCreateVersionsAndShareConnectivityState = do
  let root = forestOf "redundant root" 4
      linked = linkOf "0-1" 0 1 root
      reverseDuplicate = linkOf "1-0" 1 0 linked
      siblingDuplicate = linkOf "sibling 1-0" 1 0 linked
      selfDuplicate = linkOf "0-0" 0 0 reverseDuplicate
  assertEqual "linked depth" 1 (Forest.versionDepth (Forest.version linked))
  assertEqual "duplicate depth" 2 (Forest.versionDepth (Forest.version reverseDuplicate))
  assertEqual "self-link depth" 3 (Forest.versionDepth (Forest.version selfDuplicate))
  assertEqual "duplicate parent"
    (Just (Forest.version linked))
    (Forest.versionParent (Forest.version reverseDuplicate))
  assertEqual "sibling parent"
    (Just (Forest.version linked))
    (Forest.versionParent (Forest.version siblingDuplicate))
  assertEqual "self-link parent"
    (Just (Forest.version reverseDuplicate))
    (Forest.versionParent (Forest.version selfDuplicate))
  assertBool "a link always produces a distinct child version"
    (Forest.version linked /= Forest.version reverseDuplicate)
  -- Divergence from the reference: two identical pure calls are one value, not two versions.
  assertEqual "identically built siblings collapse under structural identity"
    (Forest.version reverseDuplicate)
    (Forest.version siblingDuplicate)
  assertEqual "redundant link keeps the component count"
    (Forest.componentCount linked)
    (Forest.componentCount reverseDuplicate)
  assertEqual "self-link keeps the component count"
    (Forest.componentCount linked)
    (Forest.componentCount selfDuplicate)
  assertShares "redundant link shares connectivity" linked reverseDuplicate
  assertShares "self-link shares connectivity" reverseDuplicate selfDuplicate
  assertDoesNotShare "successful union publishes a new index" root linked
  assertWitness "witness survives redundant history" linked selfDuplicate 0 1
  assertEqual "self witness is the history root"
    (Just (Just (Forest.versionRoot (Forest.version selfDuplicate))))
    (Forest.firstConnected 2 2 selfDuplicate)
  _ <- expectValid "self-link" selfDuplicate
  pure ()

-- | A successful union rebuilds only the touched cells and honours the first-endpoint tie-break.
testSuccessfulUnionRebuildsOnlyTheTouchedCells :: IO ()
testSuccessfulUnionRebuildsOnlyTheTouchedCells = do
  let root = forestOf "sharing root" 16
      first = linkOf "0-1" 0 1 root
      second = linkOf "2-3" 2 3 first
  assertDoesNotShare "successful union publishes a new index" first second
  assertShares "redundant union retains the index" second (linkOf "0-1 again" 0 1 second)
  assertShares "reversed redundant union retains the index" second (linkOf "3-2" 3 2 second)

  -- Union by size: on a tie the first endpoint's root stays the representative.
  assertEqual "left tie-break winner" (Just 0) (Forest.find 1 second)
  assertEqual "right tie-break winner" (Just 2) (Forest.find 3 second)
  let merged = linkOf "3-1" 3 1 second
  assertEqual "tie-break makes the first endpoint's root the representative"
    (Just 2)
    (Forest.find 0 merged)
  assertEqual "merged components" 13 (Forest.componentCount merged)
  _ <- expectValid "merged" merged
  pure ()

-- | Sibling versions at equal depths never confuse their union-edge witnesses.
testSiblingBranchesKeepEqualDepthVersionTagsDistinct :: IO ()
testSiblingBranchesKeepEqualDepthVersionTagsDistinct = do
  let root = forestOf "sibling root" 5
      common = linkOf "0-1" 0 1 root
      left = linkOf "0-2" 0 2 common
      right = linkOf "0-3" 0 3 common
      leftLater = linkOf "2-4" 2 4 left
      rightLater = linkOf "3-4" 3 4 right
  assertEqual "equal sibling depths"
    (Forest.versionDepth (Forest.version left))
    (Forest.versionDepth (Forest.version right))
  assertBool "distinct sibling versions" (Forest.version left /= Forest.version right)
  assertWitness "left branch keeps the common witness" common left 0 1
  assertWitness "right branch keeps the common witness" common right 0 1
  assertWitness "left branch witness" left leftLater 1 2
  assertWitness "right branch witness" right rightLater 1 3
  assertWitness "left branch latest witness" leftLater leftLater 0 4
  assertWitness "right branch latest witness" rightLater rightLater 0 4
  assertEqual "left branch does not see the right union" (Just False) (Forest.connected 0 3 leftLater)
  assertEqual "right branch does not see the left union" (Just False) (Forest.connected 0 2 rightLater)
  assertEqual "left branch history root"
    (Forest.version root)
    (Forest.versionRoot (Forest.version leftLater))
  assertEqual "right branch history root"
    (Forest.version root)
    (Forest.versionRoot (Forest.version rightLater))
  _ <- expectValid "left branch" leftLater
  _ <- expectValid "right branch" rightLater
  pure ()

-- | The pair-specific lowest-common-ancestor witness is not overwritten by later mergers.
testFirstConnectedUsesPairLcaRatherThanLatestComponentBirth :: IO ()
testFirstConnectedUsesPairLcaRatherThanLatestComponentBirth = do
  let root = forestOf "lca root" 8
      ab = linkOf "0-1" 0 1 root
      cd = linkOf "2-3" 2 3 ab
      abcd = linkOf "1-3" 1 3 cd
      ef = linkOf "4-5" 4 5 abcd
      gh = linkOf "6-7" 6 7 ef
      efgh = linkOf "4-6" 4 6 gh
      whole = linkOf "0-4" 0 4 efgh
  assertWitness "ab" ab whole 0 1
  assertWitness "cd" cd whole 2 3
  assertWitness "abcd" abcd whole 0 2
  assertWitness "ef" ef whole 4 5
  assertWitness "gh" gh whole 6 7
  assertWitness "efgh" efgh whole 5 7
  assertWitness "whole" whole whole 1 6

  -- Union-edge versions on one lineage are unique per depth, which is why the `later` tie rule --
  -- keep the left candidate on equal depth -- can never change an answer.
  let witnesses =
        [ witness
        | leftVertex <- [0 .. 7]
        , rightVertex <- [0 .. 7]
        , leftVertex /= rightVertex
        , Just witness <- [firstOf "tie survey" leftVertex rightVertex whole]
        ]
  assertBool "equal witness depths imply equal witnesses"
    (and [ (Forest.versionDepth a == Forest.versionDepth b) == (a == b)
         | a <- witnesses, b <- witnesses ])
  _ <- expectValid "whole" whole
  pure ()

-- | The boundary where one queried vertex is the pair's forest lowest common ancestor.
testFirstConnectedHandlesOneEndpointAtLca :: IO ()
testFirstConnectedHandlesOneEndpointAtLca = do
  let root = forestOf "boundary root" 5
      pair = linkOf "0-1" 0 1 root          -- 1 -> 0
      otherPair = linkOf "2-3" 2 3 pair     -- 3 -> 2
      larger = linkOf "2-4" 2 4 otherPair
      merged = linkOf "2-0" 2 0 larger      -- the component rooted at 0 becomes a child of 2
  assertEqual "merged representative" (Just 2) (Forest.find 0 merged)
  assertWitness "pair witness below the new root" pair merged 0 1
  assertWitness "merge witness" merged merged 0 2
  assertWitness "cross-component witness" merged merged 1 4
  _ <- expectValid "merged" merged
  pure ()

-- | A maximum-height balanced union history validates the logarithmic path invariant.
testBalancedUnionsRespectUnionBySizeHeight :: IO ()
testBalancedUnionsRespectUnionBySizeHeight = do
  let count = 1024 :: Int
      root = forestOf "balanced root" count
      operations =
        [ (width, start)
        | width <- takeWhile (< count) (iterate (* 2) 1)
        , start <- [0, width * 2 .. count - 1]
        ]
      step (forest, firstPair, finalBridge) (width, start) =
        let next = linkOf "balanced link" start (start + width) forest
            token = Forest.version next
         in ( next
            , if start == 0 && width == 1 then Just token else firstPair
            , if start == 0 && width == count `div` 2 then Just token else finalBridge
            )
      (final, firstPairVersion, finalBridgeVersion) =
        List.foldl' step (root, Nothing, Nothing) operations
  assertEqual "single component" 1 (Forest.componentCount final)
  assertEqual "single representative" (Just 0) (Forest.find (count - 1) final)
  assertEqual "first pair witness" (Just firstPairVersion) (Forest.firstConnected 0 1 final)
  assertEqual "final bridge witness"
    (Just finalBridgeVersion)
    (Forest.firstConnected 0 (count - 1) final)
  assertEqual "root retained" count (Forest.componentCount root)

  statistics <- expectValid "balanced" final
  assertEqual "every vertex stored" count (statisticsStoredCellCount statistics)
  assertEqual "validated components" 1 (statisticsComponentCount statistics)
  assertEqual "logarithmic height" 10 (statisticsMaximumParentPathLength statistics)
  assertEqual "history depth" (toInteger count - 1) (statisticsHistoryDepth statistics)

-- | One model version of the naive branching reference implementation.
data ModelVersion = ModelVersion
  { modelForest :: PersistentAncestralConnectionForest
  , modelClasses :: [Int]
  , modelParent :: Maybe ModelVersion
  }

-- | Fully branching random histories agree with a naive root-to-version scan.
testRandomBranchingHistoriesMatchNaiveAncestorScan :: IO ()
testRandomBranchingHistoriesMatchNaiveAncestorScan = do
  let universe = 12 :: Int
      rootState = ModelVersion (forestOf "model root" universe) [0 .. universe - 1] Nothing
      go !operation !state states !stateCount
        | operation == (350 :: Int) = pure (state, states, stateCount)
        | otherwise = do
            let (afterParent, parentDraw) = nextRandom state
                (afterLeft, leftDraw) = nextRandom afterParent
                (afterRight, rightDraw) = nextRandom afterLeft
                parentState = states !! (parentDraw `mod` stateCount)
                leftVertex = leftDraw `mod` universe
                rightVertex = rightDraw `mod` universe
                classes = unionClasses leftVertex rightVertex (modelClasses parentState)
                actual = linkOf "model link" leftVertex rightVertex (modelForest parentState)
                current = ModelVersion actual classes (Just parentState)
            assertEqual "model child parent"
              (Just (Forest.version (modelForest parentState)))
              (Forest.versionParent (Forest.version actual))
            assertEqual "model child depth"
              (Forest.versionDepth (Forest.version (modelForest parentState)) + 1)
              (Forest.versionDepth (Forest.version actual))
            assertEqual "model component count"
              (length (List.nub classes))
              (Forest.componentCount actual)
            forM_ [0 .. universe - 1] $ \queryLeft ->
              forM_ [0 .. universe - 1] $ \queryRight -> do
                assertEqual "model connected"
                  (Just (classes !! queryLeft == classes !! queryRight))
                  (Forest.connected queryLeft queryRight actual)
                assertEqual "model first connected"
                  (Just (firstConnectedByScan current queryLeft queryRight))
                  (Forest.firstConnected queryLeft queryRight actual)
            if operation `mod` 25 == 0
              then do
                _ <- expectValid "model" actual
                pure ()
              else pure ()
            go (operation + 1) afterRight (states ++ [current]) (stateCount + 1)
  (finalState, allStates, allCount) <- go 0 0x5EED2026 [rootState] 1

  -- Recheck retained versions after every sibling branch has been constructed.
  let resample !sample !state
        | sample == (80 :: Int) = pure ()
        | otherwise = do
            let (afterIndex, indexDraw) = nextRandom state
                (afterLeft, leftDraw) = nextRandom afterIndex
                (afterRight, rightDraw) = nextRandom afterLeft
                sampled = allStates !! (indexDraw `mod` allCount)
                leftVertex = leftDraw `mod` 12
                rightVertex = rightDraw `mod` 12
            assertEqual "retained version first connected"
              (Just (firstConnectedByScan sampled leftVertex rightVertex))
              (Forest.firstConnected leftVertex rightVertex (modelForest sampled))
            resample (sample + 1) afterRight
  resample 0 finalState

-- | Query cost and witnesses are independent of a long tail of redundant history.
testLongRedundantHistoryKeepsTheUnionWitness :: IO ()
testLongRedundantHistoryKeepsTheUnionWitness = do
  let root = forestOf "long root" 3
      first = linkOf "0-1" 0 1 root
      final = List.foldl' (\forest _ -> linkOf "2-2" 2 2 forest) first [1 .. 20000 :: Int]
  assertEqual "history depth" 20001 (Forest.versionDepth (Forest.version final))
  assertWitness "witness survives a deep redundant tail" first final 0 1
  assertEqual "still disconnected" (Just Nothing) (Forest.firstConnected 0 2 final)
  assertShares "the whole redundant tail shares one index" first final

-- | Sparse construction and validation do not scan an untouched huge universe. The maximal
-- admissible universe is 'Forest.maximumVertexCount': a larger one is rejected, because the
-- pinned injective vertex hash cannot cover it (see the cap test below).
testHugeSparseUniverseStoresOnlyTouchedVertices :: IO ()
testHugeSparseUniverseStoresOnlyTouchedVertices = do
  let huge = Forest.maximumVertexCount
      root = forestOf "huge root" huge
  rootStatistics <- expectValid "huge root" root
  assertEqual "huge root stores nothing" 0 (statisticsStoredCellCount rootStatistics)

  let linked = linkOf "huge link" 0 (huge - 1) root
  assertEqual "huge components" (huge - 1) (Forest.componentCount linked)
  assertEqual "linked endpoints" (Just True) (Forest.connected 0 (huge - 1) linked)
  assertEqual "untouched neighbour" (Just False) (Forest.connected 0 (huge - 2) linked)
  assertEqual "untouched representative" (Just (huge - 2)) (Forest.find (huge - 2) linked)
  assertEqual "untouched size" (Just 1) (Forest.componentSize (huge - 2) linked)
  assertEqual "linked size" (Just 2) (Forest.componentSize 0 linked)
  assertWitness "huge witness" linked linked 0 (huge - 1)

  statistics <- expectValid "huge linked" linked
  assertEqual "only the touched vertices are stored" 2 (statisticsStoredCellCount statistics)
  assertEqual "single edge path" 1 (statisticsMaximumParentPathLength statistics)

-- | Every vertex-taking operation rejects endpoints outside the fixed universe.
testOperationsRejectVerticesOutsideTheUniverse :: IO ()
testOperationsRejectVerticesOutsideTheUniverse = do
  let root = forestOf "reject root" 3
  assertBool "find below" (isNothing (Forest.find (-1) root))
  assertBool "find above" (isNothing (Forest.find 3 root))
  assertBool "connected below" (isNothing (Forest.connected (-1) 0 root))
  assertBool "connected above" (isNothing (Forest.connected 0 3 root))
  assertBool "component size below" (isNothing (Forest.componentSize (-1) root))
  assertBool "component size above" (isNothing (Forest.componentSize 3 root))
  assertBool "link below" (isNothing (Forest.link (-1) 0 root))
  assertBool "link above" (isNothing (Forest.link 0 3 root))
  assertBool "first connected below" (isNothing (Forest.firstConnected (-1) 0 root))
  assertBool "first connected above" (isNothing (Forest.firstConnected 0 3 root))
  assertBool "self link out of range" (isNothing (Forest.link 3 3 root))
  assertBool "empty universe admits nothing"
    (isNothing (Forest.find 0 (forestOf "reject empty" 0)))

  -- A rejected operation publishes nothing.
  assertEqual "root version unchanged"
    (Forest.version root)
    (Forest.versionRoot (Forest.version root))
  assertEqual "root depth unchanged" 0 (Forest.versionDepth (Forest.version root))
  assertEqual "root components unchanged" 3 (Forest.componentCount root)
  _ <- expectValid "reject root" root
  pure ()

-- | Version identity is structural, which refines the reference's reference identity everywhere
-- referential transparency permits.
testVersionIdentityIsStructural :: IO ()
testVersionIdentityIsStructural = do
  let leftRoot = forestOf "identity left" 2
      rightRoot = forestOf "identity right" 2
      otherRoot = forestOf "identity other" 3
      leftLinked = linkOf "0-1" 0 1 leftRoot
      rightLinked = linkOf "0-1" 0 1 rightRoot
  -- Documented divergence: identical pure histories are one value, so identically built roots and
  -- versions compare equal instead of being distinct objects.
  assertEqual "identically built roots collapse"
    (Forest.version leftRoot)
    (Forest.version rightRoot)
  assertEqual "identically built links collapse"
    (Forest.version leftLinked)
    (Forest.version rightLinked)
  -- Everything referential transparency does allow stays distinct.
  assertBool "distinct universes have distinct history roots"
    (Forest.version otherRoot /= Forest.version leftRoot)
  let branchLeft = linkOf "0-1" 0 1 otherRoot
      branchRight = linkOf "0-2" 0 2 otherRoot
  assertEqual "equal branch depths"
    (Forest.versionDepth (Forest.version branchLeft))
    (Forest.versionDepth (Forest.version branchRight))
  assertBool "distinct link endpoints give distinct versions"
    (Forest.version branchLeft /= Forest.version branchRight)
  assertBool "a version differs from its parent"
    (Forest.version branchLeft /= Forest.version otherRoot)
  assertWitness "left branch witness" leftLinked leftLinked 0 1
  assertWitness "right branch witness" rightLinked rightLinked 0 1

-- | The connected classes after uniting two vertices.
unionClasses :: Int -> Int -> [Int] -> [Int]
unionClasses leftVertex rightVertex classes
  | leftClass == rightClass = classes
  | otherwise = map (\value -> if value == rightClass then leftClass else value) classes
  where
    leftClass = classes !! leftVertex
    rightClass = classes !! rightVertex

-- | The earliest version on a model version's lineage in which two vertices shared a class.
firstConnectedByScan :: ModelVersion -> Int -> Int -> Maybe AncestralConnectionVersion
firstConnectedByScan state leftVertex rightVertex = scan (lineage state)
  where
    lineage current = case modelParent current of
      Nothing -> [current]
      Just parent -> lineage parent ++ [current]
    scan [] = Nothing
    scan (candidate : rest)
      | modelClasses candidate !! leftVertex == modelClasses candidate !! rightVertex =
          Just (Forest.version (modelForest candidate))
      | otherwise = scan rest

-- | One cached component size per distinct current root.
componentSizesByRoot :: PersistentAncestralConnectionForest -> [(Int, Int)]
componentSizesByRoot forest =
  List.nubBy (\left right -> fst left == fst right)
    [ (expect "component root" (Forest.find vertex forest)
      , expect "component size" (Forest.componentSize vertex forest))
    | vertex <- [0 .. Forest.vertexCount forest - 1]
    ]

-- | The linear congruential generator the Rust port's model test uses.
nextRandom :: Word64 -> (Word64, Int)
nextRandom state = (advanced, fromIntegral (advanced `shiftR` 33))
  where
    advanced = state * 6364136223846793005 + 1442695040888963407

forestOf :: String -> Int -> PersistentAncestralConnectionForest
forestOf label count = expect label (Forest.create count)

linkOf :: String
       -> Int
       -> Int
       -> PersistentAncestralConnectionForest
       -> PersistentAncestralConnectionForest
linkOf label leftVertex rightVertex forest =
  expect label (Forest.link leftVertex rightVertex forest)

firstOf :: String
        -> Int
        -> Int
        -> PersistentAncestralConnectionForest
        -> Maybe AncestralConnectionVersion
firstOf label leftVertex rightVertex forest =
  expect label (Forest.firstConnected leftVertex rightVertex forest)

-- | Asserts that a pair's first-connection witness is a given version's token.
assertWitness :: String
              -> PersistentAncestralConnectionForest
              -> PersistentAncestralConnectionForest
              -> Int
              -> Int
              -> IO ()
assertWitness label expected forest leftVertex rightVertex =
  assertEqual label (Just (Forest.version expected)) (firstOf label leftVertex rightVertex forest)

assertShares :: String
             -> PersistentAncestralConnectionForest
             -> PersistentAncestralConnectionForest
             -> IO ()
assertShares label left right = do
  shares <- Forest.sharesConnectivityRootWith left right
  assertBool label shares

assertDoesNotShare :: String
                   -> PersistentAncestralConnectionForest
                   -> PersistentAncestralConnectionForest
                   -> IO ()
assertDoesNotShare label left right = do
  shares <- Forest.sharesConnectivityRootWith left right
  assertBool label (not shares)

expectValid :: String
            -> PersistentAncestralConnectionForest
            -> IO AncestralConnectionForestStatistics
expectValid label forest = case Forest.validateStructure forest of
  Right statistics -> pure statistics
  Left message -> error (label ++ ": structural validation failed: " ++ message)

expect :: String -> Maybe value -> value
expect _ (Just value) = value
expect label Nothing = error (label ++ ": expected a result, got none")

assertBool :: String -> Bool -> IO ()
assertBool _ True = pure ()
assertBool label False = error (label ++ ": assertion failed")

assertEqual :: (Eq value, Show value) => String -> value -> value -> IO ()
assertEqual _ expected actual | expected == actual = pure ()
assertEqual label expected actual =
  error (label ++ ": expected " ++ show expected ++ ", actual " ++ show actual)
