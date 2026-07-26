-- | Entry point for the durable7-hamt test suite.
module Main (main) where

import Prelude hiding (lookup, null)

import Control.Concurrent (forkIO, newEmptyMVar, putMVar, takeMVar)
import Control.Exception (SomeException, evaluate, try)
import Control.Monad (forM_, replicateM)
import Data.Bits ((.&.), (.|.), shiftL)
import qualified Data.ByteString as ByteString
import qualified Data.ByteString.Char8 as ByteStringChar8
import Data.Char (toLower)
import Data.Int (Int32, Int64)
import Data.IORef (IORef, atomicModifyIORef', newIORef, readIORef, writeIORef)
import Data.List (sort)
import Data.Maybe (listToMaybe)
import Data.Word (Word8)
import System.IO.Unsafe (unsafePerformIO)

import Durable7.Hamt.Hashable (hash)
import qualified Durable7.Hamt.HashBag as HashBag
import Durable7.Hamt.HashMap (HashPolicy(..))
import qualified Durable7.Hamt.HashMap as HashMap
import qualified Durable7.Hamt.HashMultimap as HashMultimap
import qualified Durable7.Hamt.HashSet as HashSet
import qualified Durable7.Hamt.Relation as Relation
import qualified Durable7.Hamt.Transient as Transient
import Durable7.Hamt.MerkleEncoding
  ( MerkleCodec(..)
  , digestHex
  , hashBytes
  , int32MerkleCodec
  , int64MerkleCodec
  , makeMerkleSearchTreePolicy
  , merkleDomainDigest
  , merkleEmptyDigest
  , nullableBytesMerkleCodec
  , nullableUtf8MerkleCodec
  , parseDigestHex
  )
import qualified Durable7.Hamt.MerkleSearchTree as Merkle
import qualified Durable7.Hamt.Patricia as Patricia
import qualified Durable7.Hamt.PersistentDirectedGraph as DirectedGraph
import qualified Durable7.Hamt.PersistentIndexedMap as IndexedMap
import qualified Durable7.Hamt.PersistentMapPatch as MapPatch
import PersistenceTests (runPersistenceTests)
import BiMapTests (runBiMapTests)

data CountedValue = CountedValue !(IORef Int) !Int !Int

instance Eq CountedValue where
  (==) = countedValuesEqual

instance Show CountedValue where
  show (CountedValue _ value _) = "CountedValue " ++ show value

countedValuesEqual :: CountedValue -> CountedValue -> Bool
countedValuesEqual (CountedValue calls left _) (CountedValue _ right _) = unsafePerformIO $ do
  atomicModifyIORef' calls (\count -> (count + 1, ()))
  pure (left == right)
{-# NOINLINE countedValuesEqual #-}

instrumentHash :: IORef Int -> (k -> Int) -> k -> Int
instrumentHash calls hashFunction key = unsafePerformIO $ do
  atomicModifyIORef' calls (\count -> (count + 1, ()))
  pure (hashFunction key)
{-# NOINLINE instrumentHash #-}

data ExplosiveValue = ExplosiveValue !Int

instance Eq ExplosiveValue where
  _ == _ = error "intentional value-equality failure"

-- | Entry point running this package's test suite.
main :: IO ()
main = do
  testMapBasics
  testCollisionPolicy
  testCollisionShrinkCanonicalization
  testChampCanonicalizationAndDiff
  testChampEqualityAndDiffPruneSharedDescendants
  testCrossHashPolicyMapComparison
  testChampTopologyRejectsDifferentCollisionKeys
  testChampTerminalHashFragments
  testChampStructuralAlgebra
  testPatriciaMapsAndSets
  testPatriciaCursors
  testActualKeyPreservation
  testAdjustAndStrictMapping
  testPersistentMapFactories
  testPersistentHashBag
  testPersistentHashBagAlgebra
  testPersistentHashBagDeterministicModel
  runBiMapTests
  testHashMultimapAndRelation
  testPersistentMapPatch
  testPersistentDirectedGraph
  testPersistentIndexedMap
  testSetAlgebra
  testCrossPolicySetRelations
  testTransientSessions
  testLargeFromList
  testMerkleEncodingAndCore
  runPersistenceTests
  testConcurrentReads
  putStrLn "durable7-hamt tests passed"

testPersistentMapPatch :: IO ()
testPersistentMapPatch = do
  let source = HashMap.fromList [(1 :: Int, "one"), (2, "two"), (4, "four")]
      target = HashMap.fromList [(1 :: Int, "ONE"), (3, "three"), (4, "four")]
      patch = MapPatch.between source target
  applied <- expectRight "map patch apply" (MapPatch.apply patch source)
  assertBool "map patch reaches target" (HashMap.mapEquals target applied)
  restored <- expectRight "map patch invert" (MapPatch.apply (MapPatch.invert patch) applied)
  assertBool "map patch inverse restores source" (HashMap.mapEquals source restored)
  assertEqual "map patch strict conflict" (Left (1 :: Int))
    (fmap HashMap.toList (MapPatch.apply patch (HashMap.insert 1 "wrong" source)))
  let middle = HashMap.insert 5 "five" target
      next = MapPatch.between target middle
  composed <- expectRight "map patch compose" (MapPatch.compose patch next)
  final <- expectRight "composed patch apply" (MapPatch.apply composed source)
  assertBool "map patch composition reaches final map" (HashMap.mapEquals middle final)
  assertBool "map patch invariant" (MapPatch.validStructure composed)

testPersistentDirectedGraph :: IO ()
testPersistentDirectedGraph = do
  let graph0 = DirectedGraph.empty :: DirectedGraph.PersistentDirectedGraph Int
      graph1 = DirectedGraph.insertEdge 1 2 (DirectedGraph.insertEdge 2 3 (DirectedGraph.insertVertex 4 graph0))
      snapshot = graph1
      reversed = DirectedGraph.reverse graph1
      graph2 = DirectedGraph.deleteVertex 2 graph1
  assertEqual "directed graph vertex count" 4 (DirectedGraph.vertexCount graph1)
  assertEqual "directed graph edge count" 2 (DirectedGraph.edgeCount graph1)
  assertBool "directed graph implicit endpoints" (DirectedGraph.memberVertex 3 graph1)
  assertEqual "directed graph successor" [2] (sort (HashSet.toList (DirectedGraph.successors 1 graph1)))
  assertBool "directed graph reverse" (DirectedGraph.memberEdge 2 1 reversed)
  assertBool "directed graph vertex removal clears incident edges" (DirectedGraph.edgeCount graph2 == 0)
  assertBool "directed graph snapshot preserved" (DirectedGraph.memberEdge 1 2 snapshot)
  assertBool "directed graph invariant" (DirectedGraph.validStructure graph2)

testPersistentIndexedMap :: IO ()
testPersistentIndexedMap = do
  let selector _ value = value `mod` (2 :: Int)
      values0 = IndexedMap.fromList selector [(1 :: Int, 10 :: Int), (2, 11), (3, 12)]
      snapshot = values0
      values1 = IndexedMap.set 1 13 values0
      values2 = IndexedMap.delete 2 values1
  assertEqual "indexed map size" 3 (IndexedMap.size values0)
  assertEqual "indexed map secondary group" (Just [1, 3])
    (sort . HashSet.toList <$> IndexedMap.lookupKeysByIndex 0 values0)
  assertEqual "indexed map moves secondary membership" (Just [1, 2])
    (sort . HashSet.toList <$> IndexedMap.lookupKeysByIndex 1 values1)
  assertEqual "indexed map snapshot value" (Just 10) (IndexedMap.lookup 1 snapshot)
  assertEqual "indexed map removal" Nothing (IndexedMap.lookup 2 values2)
  assertBool "indexed map invariant" (IndexedMap.validStructure values2)

testHashMultimapAndRelation :: IO ()
testHashMultimapAndRelation = do
  let values0 = HashMultimap.empty :: HashMultimap.HashMultimap Int String
      values1 = HashMultimap.insert 1 "a" (HashMultimap.insert 1 "b" (HashMultimap.insert 2 "b" values0))
      snapshot = values1
      values2 = HashMultimap.delete 1 "a" values1
  assertEqual "multimap pair count" 3 (HashMultimap.size values1)
  assertEqual "multimap key count" 2 (HashMultimap.keyCount values1)
  assertBool "multimap membership" (HashMultimap.member 1 "a" snapshot)
  assertBool "multimap delete preserves snapshot" (not (HashMultimap.member 1 "a" values2))
  assertBool "multimap invariant" (HashMultimap.validStructure values2)
  let relation0 = Relation.empty :: Relation.Relation Int String
      relation1 = Relation.insert 1 "a" (Relation.insert 1 "b" (Relation.insert 2 "b" relation0))
      relation2 = Relation.deleteLeft 1 relation1
  assertEqual "relation pair count" 3 (Relation.size relation1)
  assertBool "relation reverse lookup" (maybe False (HashSet.member 2) (Relation.lookupLefts "b" relation1))
  assertEqual "relation group removal" 1 (Relation.size relation2)
  assertBool "relation inverse" (Relation.member "b" 2 (Relation.inverse relation1))
  assertBool "relation invariant" (Relation.validStructure relation2)

testMapBasics :: IO ()
testMapBasics = do
  let values = HashMap.fromList [(1 :: Int, "one"), (2, "two"), (1, "uno")]
  assertEqual "map size after last-wins build" 2 (HashMap.size values)
  assertEqual "map lookup uses last value" (Just "uno") (HashMap.lookup 1 values)
  assertEqual "map lookup miss" Nothing (HashMap.lookup 3 values)
  assertBool "duplicate insertNew is rejected" (isNothing (HashMap.insertNew 1 "ein" values))
  let removed = HashMap.tryRemove 2 values
  assertEqual "tryRemove returns removed value" (Just "two") (fst <$> removed)
  assertEqual "delete removes key" Nothing (HashMap.lookup 2 (maybe values snd removed))

testCollisionPolicy :: IO ()
testCollisionPolicy = do
  let collisionPolicy = HashPolicy (const 7) (==)
      values = HashMap.fromListWith collisionPolicy [(1 :: Int, "a"), (2, "b"), (3, "c")]
  assertEqual "collision map size" 3 (HashMap.size values)
  assertEqual "collision lookup first" (Just "a") (HashMap.lookup 1 values)
  assertEqual "collision lookup middle" (Just "b") (HashMap.lookup 2 values)
  assertEqual "collision lookup last" (Just "c") (HashMap.lookup 3 values)
  assertEqual "collision delete keeps siblings" [1, 3] (sort (HashMap.keys (HashMap.delete 2 values)))

testCollisionShrinkCanonicalization :: IO ()
testCollisionShrinkCanonicalization = do
  let splitHash key
        | key < (10 :: Int) = 7
        | otherwise = key
      collisionPolicy = HashPolicy splitHash (==)
      collided = HashMap.fromListWith collisionPolicy [(1 :: Int, "a"), (2, "b"), (3, "c")]
      singletonLeaf = HashMap.delete 3 (HashMap.delete 2 collided)
      branched = HashMap.insert 42 "z" singletonLeaf
  assertEqual "collision shrink leaves one entry" [(1, "a")] (HashMap.toList singletonLeaf)
  assertBool "collision shrink demotes the singleton bucket" (HashMap.validStructure singletonLeaf)
  assertEqual "collision-shrunk entry survives a new branch" (Just "a") (HashMap.lookup 1 branched)
  assertBool "branch beside collision-shrunk leaf stays canonical" (HashMap.validStructure branched)
  assertEqual "new branch beside collision-shrunk leaf" (Just "z") (HashMap.lookup 42 branched)
  assertEqual "removing collision-shrunk leaf preserves sibling" [(42, "z")] (HashMap.toList (HashMap.delete 1 branched))

testChampCanonicalizationAndDiff :: IO ()
testChampCanonicalizationAndDiff = do
  let ascending = HashMap.fromList [(key, key) | key <- [0 :: Int .. 511]]
      descending = HashMap.fromList [(key, key) | key <- reverse [0 :: Int .. 511]]
      changed = HashMap.insert 1000 1000 (HashMap.insert 9 (-9) (HashMap.delete 7 descending))
      differences = HashMap.diff ascending changed
  assertBool "independent histories have canonical valid shape" (HashMap.validStructure ascending && HashMap.validStructure descending)
  assertBool "independent histories have identical CHAMP topology" (HashMap.sameTopology ascending descending)
  assertBool "independent histories compare equal" (HashMap.mapEquals ascending descending)
  assertEqual "equal histories have empty diff" [] (HashMap.diff ascending descending)
  assertEqual "typed diff count" 3 (length differences)
  assertBool "typed diff removal" (HashMap.EntryRemoved 7 7 `elem` differences)
  assertBool "typed diff change" (HashMap.EntryChanged 9 9 (-9) `elem` differences)
  assertBool "typed diff addition" (HashMap.EntryAdded 1000 1000 `elem` differences)

testChampEqualityAndDiffPruneSharedDescendants :: IO ()
testChampEqualityAndDiffPruneSharedDescendants = do
  hashCalls <- newIORef (0 :: Int)
  keyEqualityCalls <- newIORef (0 :: Int)
  valueEqualityCalls <- newIORef (0 :: Int)
  let branchingHash key = (key `div` 2) .|. ((key .&. 1) `shiftL` 5)
      countedHash key = unsafePerformIO $ do
        atomicModifyIORef' hashCalls (\count -> (count + 1, ()))
        pure (branchingHash key)
      countedKeyEquality left right = unsafePerformIO $ do
        atomicModifyIORef' keyEqualityCalls (\count -> (count + 1, ()))
        pure (left == right)
      countedPolicy = HashPolicy countedHash countedKeyEquality
      source = HashMap.fromListWith countedPolicy
        [(key, CountedValue valueEqualityCalls key 0) | key <- [0 :: Int .. 63]]
      equalEdit = HashMap.insert 0 (CountedValue valueEqualityCalls 0 1) source

  _ <- evaluate (HashMap.size equalEdit)
  writeIORef hashCalls 0
  writeIORef keyEqualityCalls 0
  writeIORef valueEqualityCalls 0
  assertBool "lockstep map equality accepts a partially shared equal edit"
    (HashMap.mapEquals source equalEdit)
  equalityHashes <- readIORef hashCalls
  equalityKeys <- readIORef keyEqualityCalls
  equalityValues <- readIORef valueEqualityCalls
  assertEqual "lockstep map equality never rehashes" 0 equalityHashes
  assertEqual "lockstep map equality prunes 31 pointer-identical child nodes" 2 equalityKeys
  assertEqual "lockstep map equality compares only the rebuilt value" 1 equalityValues

  writeIORef hashCalls 0
  writeIORef keyEqualityCalls 0
  writeIORef valueEqualityCalls 0
  assertEqual "lockstep diff accepts a partially shared equal edit" []
    (HashMap.diff source equalEdit)
  diffHashes <- readIORef hashCalls
  diffKeys <- readIORef keyEqualityCalls
  diffValues <- readIORef valueEqualityCalls
  assertEqual "lockstep diff never rehashes" 0 diffHashes
  assertEqual "lockstep diff prunes 31 pointer-identical child nodes" 4 diffKeys
  assertEqual "lockstep diff compares only the rebuilt value" 1 diffValues

testCrossHashPolicyMapComparison :: IO ()
testCrossHashPolicyMapComparison = do
  let leftPolicy = HashPolicy id (==)
      rightPolicy = HashPolicy negate (==)
      left = HashMap.fromListWith leftPolicy
        [(1 :: Int, "one"), (2, "two"), (3, "three")]
      equalRight = HashMap.fromListWith rightPolicy
        [(3 :: Int, "three"), (2, "two"), (1, "one")]
      changedRight = HashMap.fromListWith rightPolicy
        [(1 :: Int, "one"), (2, "TWO"), (4, "four")]
      differences = HashMap.diff left changedRight
  assertBool "compatible policies with distinct hashes compare semantically"
    (HashMap.mapEquals left equalRight)
  assertEqual "compatible policies with distinct hashes have an empty semantic diff"
    [] (HashMap.diff left equalRight)
  assertBool "cross-hash-policy diff reports a removal"
    (HashMap.EntryRemoved 3 "three" `elem` differences)
  assertBool "cross-hash-policy diff reports a change"
    (HashMap.EntryChanged 2 "two" "TWO" `elem` differences)
  assertBool "cross-hash-policy diff reports an addition"
    (HashMap.EntryAdded 4 "four" `elem` differences)

testChampTopologyRejectsDifferentCollisionKeys :: IO ()
testChampTopologyRejectsDifferentCollisionKeys = do
  let collisionPolicy = HashPolicy (const 7) (==)
      left = HashMap.fromListWith collisionPolicy [(1 :: Int, "a"), (2, "b")]
      sameReversed = HashMap.fromListWith collisionPolicy [(2 :: Int, "b"), (1, "a")]
      different = HashMap.fromListWith collisionPolicy [(1 :: Int, "a"), (3, "c")]
  assertBool "collision topology ignores insertion order" (HashMap.sameTopology left sameReversed)
  assertBool "collision topology compares key contents" (not (HashMap.sameTopology left different))
  assertBool "collision map equality ignores insertion order" (HashMap.mapEquals left sameReversed)
  assertEqual "collision diff ignores insertion order" [] (HashMap.diff left sameReversed)

testChampTerminalHashFragments :: IO ()
testChampTerminalHashFragments = do
  let explicitPolicy = HashPolicy snd (\left right -> fst left == fst right)
      keys =
        [ (0 :: Int, 0)
        , (1, 1 `shiftL` 30)
        , (2, 1 `shiftL` 31)
        , (3, 3 `shiftL` 30)
        ]
      values = HashMap.fromListWith explicitPolicy [(key, fst key) | key <- keys]
  assertBool "terminal hash slots 0 through 3 form a valid CHAMP" (HashMap.validStructure values)
  assertEqual "terminal slot 3 remains reachable" (Just 3) (HashMap.lookup (3, 3 `shiftL` 30) values)

testChampStructuralAlgebra :: IO ()
testChampStructuralAlgebra = do
  let casePolicy = HashPolicy (hash . map toLower) (\left right -> map toLower left == map toLower right)
      base = HashMap.emptyWith casePolicy
      leftMap = HashMap.insert "left" 10 (HashMap.insert "Alpha" (1 :: Int) base)
      rightMap = HashMap.insert "right" 20 (HashMap.insert "ALPHA" (2 :: Int) base)
      unitedMap = HashMap.union leftMap rightMap
      intersectedMap = HashMap.intersection leftMap rightMap
      exceptedMap = HashMap.difference leftMap rightMap
      symmetricMap = HashMap.symmetricDifference leftMap rightMap
  assertEqual "structural CHAMP union is right-valued" (Just 2) (HashMap.lookup "alpha" unitedMap)
  assertEqual "structural CHAMP union keeps the left key" (Just "Alpha") (HashMap.actualKey "alpha" unitedMap)
  assertEqual "structural CHAMP union count" 3 (HashMap.size unitedMap)
  assertEqual "structural CHAMP intersection count" 1 (HashMap.size intersectedMap)
  assertEqual "structural CHAMP difference count" 1 (HashMap.size exceptedMap)
  assertEqual "structural CHAMP symmetric difference count" 2 (HashMap.size symmetricMap)
  assertBool "structural CHAMP map results stay canonical"
    (all HashMap.validStructure [unitedMap, intersectedMap, exceptedMap, symmetricMap])

  calls <- newIORef (0 :: Int)
  let countingHash key = unsafePerformIO $
        atomicModifyIORef' calls (\count -> (count + 1, hash key))
      countingPolicy = HashPolicy countingHash (==)
      basis = HashSet.fromListWith countingPolicy [0 :: Int .. 255]
      left = HashSet.insert 1000 basis
      right = HashSet.insert 2000 basis
  _ <- evaluate (HashSet.size left + HashSet.size right)
  writeIORef calls 0
  let united = HashSet.union left right
      intersected = HashSet.intersection left right
      excepted = HashSet.difference left right
      symmetric = HashSet.symmetricDifference left right
      relationScore =
        fromEnum (HashSet.isSubsetOf left united) +
        fromEnum (HashSet.isSupersetOf united right) +
        fromEnum (HashSet.overlaps left right) +
        fromEnum (HashSet.setEquals left left)
  _ <- evaluate
    (HashSet.size united + HashSet.size intersected + HashSet.size excepted +
      HashSet.size symmetric + relationScore)
  structuralHashes <- readIORef calls
  assertEqual "shared-policy structural CHAMP algebra does not rehash" 0 structuralHashes
  assertEqual "shared-policy structural union count" 258 (HashSet.size united)
  assertEqual "shared-policy structural intersection count" 256 (HashSet.size intersected)
  assertEqual "shared-policy structural difference" [1000] (HashSet.toList excepted)
  assertEqual "shared-policy structural symmetric difference" [1000, 2000]
    (sort (HashSet.toList symmetric))

  let collisionPolicy = HashPolicy (const 0) (==)
      histories = take 200 (iterate (\state -> state * 1664525 + 1013904223) (0x51a7e5 :: Int))
      modelValues state bit = [value | value <- [-20 .. 20], ((state `div` (bit + value + 21)) .&. 1) /= 0]
      checkHistory state =
        let leftValues = modelValues state 1
            rightValues = modelValues (state * 1103515245 + 12345) 2
            leftSet = HashSet.fromListWith collisionPolicy leftValues
            rightSet = HashSet.fromListWith collisionPolicy rightValues
         in sort (HashSet.toList (HashSet.union leftSet rightSet)) == sort (unique (leftValues ++ rightValues)) &&
            sort (HashSet.toList (HashSet.intersection leftSet rightSet)) == sort [x | x <- unique leftValues, x `elem` rightValues] &&
            sort (HashSet.toList (HashSet.difference leftSet rightSet)) == sort [x | x <- unique leftValues, x `notElem` rightValues]
  assertBool "collision-heavy structural CHAMP histories match list models" (all checkHistory histories)
  where
    unique = foldr (\value rest -> if value `elem` rest then rest else value : rest) []

testPatriciaMapsAndSets :: IO ()
testPatriciaMapsAndSets = do
  let intKeys = [minBound, -1, 0, 1, maxBound] :: [Int32]
      intMap = Patricia.fromList [(key, show key) | key <- reverse intKeys]
      longKeys = [minBound, -1, 0, 1, maxBound] :: [Int64]
      longMap = Patricia.fromList [(key, key) | key <- reverse longKeys]
  assertEqual "Int32 Patricia signed order" intKeys (map fst (Patricia.toAscList intMap))
  assertEqual "Int64 Patricia signed order" longKeys (map fst (Patricia.toAscList longMap))
  assertEqual "Int32 Patricia minimum lookup" (Just (show (minBound :: Int32))) (Patricia.lookup minBound intMap)

  let history = take 10000 (drop 1 (iterate (\state -> state * 1664525 + 1013904223) (0x1234abcd :: Int)))
      actual = foldl applyPatricia (Patricia.empty :: Patricia.IntMap32 Int) history
      expected = foldl applyModel [] history
      applyPatricia values state =
        let key = fromIntegral (((state `div` 256) `mod` 401) - 200) :: Int32
         in if state `mod` 4 == 0 then Patricia.delete key values else Patricia.insert key state values
      applyModel values state =
        let key = fromIntegral (((state `div` 256) `mod` 401) - 200) :: Int32
            rest = filter ((/= key) . fst) values
         in if state `mod` 4 == 0 then rest else (key, state) : rest
  assertEqual "Patricia randomized history" (sort expected) (Patricia.toAscList actual)

  let leftMap = Patricia.fromList [(1 :: Int32, "left"), (2, "two")]
      rightMap = Patricia.fromList [(1 :: Int32, "right"), (3, "three")]
      combinedUnion = Patricia.unionWith (\left right -> left ++ "+" ++ right) leftMap rightMap
      keyedUnion = Patricia.unionWithKey (\key left right -> show key ++ "=" ++ left ++ "/" ++ right) leftMap rightMap
      combinedIntersection = Patricia.intersectionWith (\left right -> left ++ "+" ++ right) leftMap rightMap
      keyedIntersection = Patricia.intersectionWithKey (\key left right -> show key ++ "=" ++ left ++ "/" ++ right) leftMap rightMap
      longLeft = Patricia.fromList [(minBound :: Int64, 10), (0, 20)] :: Patricia.IntMap64 Int
      longRight = Patricia.fromList [(minBound :: Int64, 1), (maxBound, 2)] :: Patricia.IntMap64 Int
      longCombined = Patricia.unionWith (+) longLeft longRight
      leftSet = Patricia.setFromList [-3, -1, 1, 3 :: Int32]
      rightSet = Patricia.setFromList [-1, 0, 1 :: Int32]
      rightBiased = Patricia.union leftMap rightMap
      leftValued = Patricia.intersection leftMap rightMap
      removed = Patricia.difference leftMap rightMap
  assertEqual "Patricia right-biased map union" [(1, "right"), (2, "two"), (3, "three")] (Patricia.toAscList rightBiased)
  assertEqual "Patricia unionWith receives left then right" [(1, "left+right"), (2, "two"), (3, "three")] (Patricia.toAscList combinedUnion)
  assertEqual "Patricia unionWithKey receives key, left, then right" [(1, "1=left/right"), (2, "two"), (3, "three")] (Patricia.toAscList keyedUnion)
  assertEqual "Patricia left-valued intersection" [(1, "left")] (Patricia.toAscList leftValued)
  assertEqual "Patricia intersectionWith receives left then right" [(1, "left+right")] (Patricia.toAscList combinedIntersection)
  assertEqual "Patricia intersectionWithKey receives key, left, then right" [(1, "1=left/right")] (Patricia.toAscList keyedIntersection)
  assertEqual "Int64 Patricia unionWith" [(minBound, 11), (0, 20), (maxBound, 2)] (Patricia.toAscList longCombined)
  assertBool "Patricia cached cardinalities survive randomized history" (Patricia.validStructure actual)
  assertBool "Patricia cached cardinalities survive structural algebra"
    (all Patricia.validStructure [rightBiased, combinedUnion, keyedUnion, leftValued, combinedIntersection, keyedIntersection, removed])
  assertBool "Int64 Patricia cached cardinalities survive combining union" (Patricia.validStructure longCombined)
  assertEqual "Patricia set union" [-3, -1, 0, 1, 3] (Patricia.setToAscList (Patricia.setUnion leftSet rightSet))
  assertEqual "Patricia set intersection" [-1, 1] (Patricia.setToAscList (Patricia.setIntersection leftSet rightSet))
  assertEqual "Patricia set difference" [-3, 3] (Patricia.setToAscList (Patricia.setDifference leftSet rightSet))

testPatriciaCursors :: IO ()
testPatriciaCursors = do
  let keys = [minBound, -1, 0, 17, maxBound] :: [Int32]
      values = Patricia.fromList [(key, if key == 0 then Nothing else Just (show key)) | key <- keys]
      required message = maybe (error message) id
      cursorAtRank position = required "expected valid Patricia cursor rank" (Patricia.cursorAt position values)
      expectedPrevious position = if position == 0 then Nothing else Just (keys !! (position - 1))
      expectedNext position = if position == length keys then Nothing else Just (keys !! position)
  forM_ [0 .. length keys] $ \position -> do
    let cursorValue = cursorAtRank position
    assertEqual "Patricia cursor position" position (Patricia.cursorPosition cursorValue)
    assertEqual "Patricia cursor count" (length keys) (Patricia.cursorCount cursorValue)
    assertEqual "Patricia cursor start" (position == 0) (Patricia.cursorIsAtStart cursorValue)
    assertEqual "Patricia cursor end" (position == length keys) (Patricia.cursorIsAtEnd cursorValue)
    assertEqual "Patricia cursor previous" (expectedPrevious position) (fst <$> Patricia.cursorPeekPrevious cursorValue)
    assertEqual "Patricia cursor next" (expectedNext position) (fst <$> Patricia.cursorPeekNext cursorValue)
    assertEqual "Patricia cursor clean snapshot" (Patricia.toAscList values)
      (Patricia.toAscList (Patricia.cursorSnapshot cursorValue))

  assertEqual "Patricia lower-bound cursor" 1
    (Patricia.cursorPosition (Patricia.lowerBoundCursor (-2) values))
  assertEqual "Patricia upper-bound cursor" 2
    (Patricia.cursorPosition (Patricia.upperBoundCursor (-1) values))
  assertEqual "Patricia missing lower-bound cursor" 4
    (Patricia.cursorPosition (Patricia.lowerBoundCursor 18 values))
  assertEqual "Patricia maximum upper-bound cursor" (length keys)
    (Patricia.cursorPosition (Patricia.upperBoundCursor maxBound values))
  let exact = Patricia.cursorAtKey 0 values
      miss = Patricia.cursorAtKey 1 values
  assertBool "Patricia exact cursor hit" (Patricia.cursorSearchFound exact)
  assertEqual "Patricia cursor stored Nothing" (Just (0, Nothing))
    (Patricia.cursorPeekNext (Patricia.cursorSearchCursor exact))
  assertBool "Patricia exact cursor miss" (not (Patricia.cursorSearchFound miss))
  assertEqual "Patricia exact miss rank" 3
    (Patricia.cursorPosition (Patricia.cursorSearchCursor miss))
  assertEqual "Patricia exact miss candidate" (Just 17)
    (fst <$> Patricia.cursorPeekNext (Patricia.cursorSearchCursor miss))
  assertBool "Patricia invalid negative rank" (isNothing (Patricia.cursorAt (-1) values))
  assertBool "Patricia invalid excessive rank" (isNothing (Patricia.cursorAt (length keys + 1) values))
  assertBool "Patricia start move previous" (isNothing (Patricia.cursorMovePrevious (Patricia.cursor values)))
  assertBool "Patricia end move next" (isNothing (Patricia.cursorMoveNext (Patricia.cursorAtEnd values)))

  let source = Patricia.fromList [(-10, Just "a"), (0, Nothing), (10, Just "c")] :: Patricia.IntMap32 (Maybe String)
      atZero = Patricia.cursorSearchCursor (Patricia.cursorAtKey 0 source)
      updated = required "expected focused Patricia value update" (Patricia.cursorSetNextValue (Just "b") atZero)
      deletedNext = required "expected Patricia next deletion" (Patricia.cursorDeleteNext atZero)
      deletedPrevious = required "expected Patricia previous deletion" (Patricia.cursorDeletePrevious atZero)
      missing = Patricia.cursorSearchCursor (Patricia.cursorAtKey 5 source)
      inserted = required "expected Patricia insertion" (Patricia.cursorInsert 5 (Just "five") missing)
  assertEqual "Patricia cursor value update rank" 1 (Patricia.cursorPosition updated)
  assertEqual "Patricia cursor value update" (Just (Just "b")) (Patricia.lookup 0 (Patricia.cursorSnapshot updated))
  assertEqual "Patricia cursor retained source" (Just Nothing) (Patricia.lookup 0 source)
  assertEqual "Patricia cursor delete next" [-10, 10]
    (map fst (Patricia.toAscList (Patricia.cursorSnapshot deletedNext)))
  assertEqual "Patricia cursor delete previous" [0, 10]
    (map fst (Patricia.toAscList (Patricia.cursorSnapshot deletedPrevious)))
  assertEqual "Patricia cursor insertion rank" 3 (Patricia.cursorPosition inserted)
  assertEqual "Patricia cursor insertion" [-10, 0, 5, 10]
    (map fst (Patricia.toAscList (Patricia.cursorSnapshot inserted)))
  assertEqual "Patricia cursor insertion retained source" [-10, 0, 10]
    (map fst (Patricia.toAscList source))
  assertBool "strict Patricia cursor duplicate" (isNothing (Patricia.cursorInsert 0 Nothing atZero))
  assertBool "Patricia cursor wrong insertion gap"
    (isNothing (Patricia.cursorInsert 5 (Just "wrong") (Patricia.cursor source)))
  assertBool "Patricia cursor end update"
    (isNothing (Patricia.cursorSetNextValue Nothing (Patricia.cursorAtEnd source)))
  assertBool "Patricia cursor start delete previous"
    (isNothing (Patricia.cursorDeletePrevious (Patricia.cursor source)))
  assertBool "Patricia cursor end delete next"
    (isNothing (Patricia.cursorDeleteNext (Patricia.cursorAtEnd source)))

  let longValues = Patricia.fromList
        [(minBound, minBound), (-1, -1), (0, 0), (2 ^ (40 :: Int), 1), (maxBound, maxBound)] :: Patricia.IntMap64 Int64
  assertEqual "Int64 Patricia lower boundary cursor" 0
    (Patricia.cursorPosition (Patricia.lowerBoundCursor minBound longValues))
  assertEqual "Int64 Patricia upper boundary cursor" 1
    (Patricia.cursorPosition (Patricia.upperBoundCursor minBound longValues))
  assertEqual "Int64 Patricia missing lower cursor" 3
    (Patricia.cursorPosition (Patricia.lowerBoundCursor 1 longValues))
  assertEqual "Int64 Patricia maximum upper cursor" 5
    (Patricia.cursorPosition (Patricia.upperBoundCursor maxBound longValues))

  let intSet = Patricia.setFromList [minBound, -1, 0, maxBound] :: Patricia.IntSet32
      (setFound, setExact) = Patricia.setCursorAtItem 0 intSet
      (setMissFound, setMiss) = Patricia.setCursorAtItem (-2) intSet
      setAdded = required "expected Patricia set insertion" (Patricia.setCursorInsert (-2) setMiss)
      setDuplicate = required "expected Patricia set duplicate no-op" (Patricia.setCursorInsert 0 setExact)
  assertBool "Patricia set exact cursor" setFound
  assertBool "Patricia set missing cursor" (not setMissFound)
  assertEqual "Patricia set cursor insertion rank" 2 (Patricia.setCursorPosition setAdded)
  assertEqual "Patricia set cursor insertion" [minBound, -2, -1, 0, maxBound]
    (Patricia.setToAscList (Patricia.setCursorSnapshot setAdded))
  assertEqual "Patricia set duplicate cursor" (Patricia.setToAscList intSet)
    (Patricia.setToAscList (Patricia.setCursorSnapshot setDuplicate))

  let histories = take 128 (iterate (\state -> state * 1664525 + 1013904223) (0x6d2b79f5 :: Int))
      checkRanks state =
        let generated = [fromIntegral (((state `div` (offset + 1)) `mod` 1001) - 500) :: Int32 | offset <- [0 .. 63]]
            sortedKeys = uniqueSorted generated
            randomMap = Patricia.fromList [(key, key) | key <- sortedKeys]
            probes = [-550, -528 .. 550] :: [Int32]
            checkProbe probe =
              let lower = length (takeWhile (< probe) sortedKeys)
                  upper = length (takeWhile (<= probe) sortedKeys)
                  searched = Patricia.cursorAtKey probe randomMap
               in Patricia.cursorPosition (Patricia.lowerBoundCursor probe randomMap) == lower &&
                  Patricia.cursorPosition (Patricia.upperBoundCursor probe randomMap) == upper &&
                  Patricia.cursorPosition (Patricia.cursorSearchCursor searched) == lower &&
                  Patricia.cursorSearchFound searched == (probe `elem` sortedKeys)
         in all checkProbe probes
  assertBool "randomized Patricia cursor ranks match sorted model" (all checkRanks histories)
  where
    uniqueSorted = foldr (\value rest -> if value `elem` rest then rest else value : rest) [] . sort

testActualKeyPreservation :: IO ()
testActualKeyPreservation = do
  let casePolicy = HashPolicy (hash . map toLower) (\left right -> map toLower left == map toLower right)
      values = HashMap.insert "HELLO" 2 (HashMap.singletonWith casePolicy "Hello" (1 :: Int))
  assertEqual "case-insensitive lookup" (Just 2) (HashMap.lookup "hello" values)
  assertEqual "replacement preserves original key" (Just "Hello") (HashMap.actualKey "hello" values)

testAdjustAndStrictMapping :: IO ()
testAdjustAndStrictMapping = do
  let casePolicy = HashPolicy (hash . map toLower) (\left right -> map toLower left == map toLower right)
      values = HashMap.fromListWith casePolicy [("Alpha", 1 :: Int), ("Beta", 2)]
      adjusted = HashMap.adjust (+ 10) "ALPHA" values
      absent = HashMap.adjust (+ 10) "missing" adjusted
  assertEqual "adjust updates in one policy-aware path" (Just 11) (HashMap.lookup "alpha" adjusted)
  assertEqual "adjust preserves stored key" (Just "Alpha") (HashMap.actualKey "alpha" adjusted)
  assertEqual "absent adjust preserves contents" (sort (HashMap.toList adjusted)) (sort (HashMap.toList absent))
  strictResult <- try (evaluate (HashMap.mapValues (\_ -> error "mapped value stayed lazy") values)) :: IO (Either SomeException (HashMap.HashMap String Int))
  assertBool "mapValues forces mapped results to WHNF" (isLeft strictResult)

testPersistentMapFactories :: IO ()
testPersistentMapFactories = do
  hashCalls <- newIORef (0 :: Int)
  equalityCalls <- newIORef (0 :: Int)
  addCalls <- newIORef (0 :: Int)
  updateCalls <- newIORef (0 :: Int)
  let countedHash key = unsafePerformIO $ do
        atomicModifyIORef' hashCalls (\count -> (count + 1, ()))
        pure (hash key)
      countedEquality left right = unsafePerformIO $ do
        atomicModifyIORef' equalityCalls (\count -> (count + 1, ()))
        pure (left == right)
      countedAdd key = unsafePerformIO $
        atomicModifyIORef' addCalls (\count -> (count + 1, key * 10))
      countedUpdate caller stored = unsafePerformIO $ do
        atomicModifyIORef' updateCalls (\count -> (count + 1, ()))
        pure (if caller == (7 :: Int) then stored + 1 else error "wrong caller key")
      countedPolicy = HashPolicy countedHash countedEquality
      source = HashMap.singletonWith countedPolicy (7 :: Int) 70

  _ <- evaluate (HashMap.size source)
  mapM_ (`writeIORef` 0) [hashCalls, equalityCalls, addCalls, updateCalls]
  (hitMap, hitValue) <- evaluate (HashMap.getOrAdd 7 countedAdd source)
  assertBool "getOrAdd hit preserves the exact root" (HashMap.sharesRootWith source hitMap)
  assertEqual "getOrAdd hit returns the stored value" 70 hitValue
  assertEqual "getOrAdd hit skips the add factory" 0 =<< readIORef addCalls
  assertEqual "getOrAdd hit hashes exactly once" 1 =<< readIORef hashCalls
  assertEqual "getOrAdd hit compares one leaf" 1 =<< readIORef equalityCalls

  let functionSource = HashMap.singleton (1 :: Int) ((+ 1) :: Int -> Int)
  (functionHit, selectedFunction) <- evaluate
    (HashMap.getOrAdd 1 (const ((* 2) :: Int -> Int)) functionSource)
  assertBool "getOrAdd remains unconstrained by value Eq"
    (HashMap.sharesRootWith functionSource functionHit)
  assertEqual "unconstrained getOrAdd returns stored function" 3 (selectedFunction 2)

  let lazyStoredSource = HashMap.singleton
        (1 :: Int)
        (error "stored hit value was forced" :: Int)
  writeIORef addCalls 0
  lazyHit <- try
    (evaluate (HashMap.getOrAdd 1 countedAdd lazyStoredSource)) ::
    IO (Either SomeException (HashMap.HashMap Int Int, Int))
  case lazyHit of
    Left problem -> fail ("getOrAdd hit forced an unselected value: " ++ show problem)
    Right (lazyHitMap, _) -> do
      assertEqual "lazy stored hit preserves cardinality" 1 (HashMap.size lazyHitMap)
      assertBool "lazy stored hit preserves the exact root"
        (HashMap.sharesRootWith lazyStoredSource lazyHitMap)
      assertEqual "lazy stored hit skips the add factory" 0 =<< readIORef addCalls

  mapM_ (`writeIORef` 0) [hashCalls, equalityCalls, addCalls, updateCalls]
  (missMap, missValue) <- evaluate (HashMap.getOrAdd 8 countedAdd source)
  assertEqual "getOrAdd miss returns its selected value" 80 missValue
  assertEqual "getOrAdd miss invokes its factory once" 1 =<< readIORef addCalls
  assertEqual "getOrAdd miss hashes exactly once" 1 =<< readIORef hashCalls
  assertEqual "getOrAdd miss publishes the selected value" (Just 80) (HashMap.lookup 8 missMap)

  mapM_ (`writeIORef` 0) [hashCalls, equalityCalls, addCalls, updateCalls]
  (updatedMap, updatedValue) <- evaluate
    (HashMap.addOrUpdate 7 countedAdd countedUpdate source)
  assertEqual "addOrUpdate hit returns its update" 71 updatedValue
  assertEqual "addOrUpdate hit skips add" 0 =<< readIORef addCalls
  assertEqual "addOrUpdate hit invokes update once" 1 =<< readIORef updateCalls
  assertEqual "addOrUpdate hit hashes exactly once" 1 =<< readIORef hashCalls
  assertEqual "addOrUpdate hit publishes its update" (Just 71) (HashMap.lookup 7 updatedMap)

  mapM_ (`writeIORef` 0) [hashCalls, equalityCalls, addCalls, updateCalls]
  (addedMap, addedValue) <- evaluate
    (HashMap.addOrUpdate 9 countedAdd countedUpdate source)
  assertEqual "addOrUpdate miss returns its add value" 90 addedValue
  assertEqual "addOrUpdate miss invokes add once" 1 =<< readIORef addCalls
  assertEqual "addOrUpdate miss skips update" 0 =<< readIORef updateCalls
  assertEqual "addOrUpdate miss hashes exactly once" 1 =<< readIORef hashCalls
  assertEqual "addOrUpdate miss publishes its value" (Just 90) (HashMap.lookup 9 addedMap)

  collisionHashes <- newIORef (0 :: Int)
  collisionEqualities <- newIORef (0 :: Int)
  let collisionPolicy = HashPolicy
        (instrumentHash collisionHashes (const 0))
        (\left right -> unsafePerformIO $ do
          atomicModifyIORef' collisionEqualities (\count -> (count + 1, ()))
          pure (left == right))
      collisionSource :: HashMap.HashMap Int Int
      collisionSource = HashMap.fromListWith collisionPolicy
        [(1, 10), (2, 20), (3, 30)]
  _ <- evaluate (HashMap.size collisionSource)
  assertBool "collision factory source starts canonical"
    (HashMap.validStructure collisionSource)
  writeIORef collisionHashes 0
  writeIORef collisionEqualities 0
  (collisionUpdated, collisionValue) <- evaluate
    (HashMap.addOrUpdate 3 (const (-1)) (\_ stored -> stored + 1) collisionSource)
  assertEqual "collision factory hit updates the selected entry" 31 collisionValue
  assertEqual "collision factory hit preserves cached size" 3
    (HashMap.size collisionUpdated)
  assertEqual "collision factory hit preserves enumeration cardinality" 3
    (length (HashMap.toList collisionUpdated))
  assertEqual "collision factory hit hashes once" 1 =<< readIORef collisionHashes
  assertEqual "collision factory hit scans its bucket once" 3 =<< readIORef collisionEqualities
  assertBool "collision factory hit preserves topology"
    (HashMap.sameTopology collisionSource collisionUpdated)
  assertBool "collision factory hit remains canonical"
    (HashMap.validStructure collisionUpdated)
  writeIORef collisionHashes 0
  writeIORef collisionEqualities 0
  (collisionAdded, collisionMissValue) <- evaluate
    (HashMap.getOrAdd 4 (const 40) collisionSource)
  assertEqual "collision factory miss adds its value" 40 collisionMissValue
  assertBool "collision factory miss remains canonical"
    (HashMap.validStructure collisionAdded)
  assertEqual "collision factory miss hashes once" 1 =<< readIORef collisionHashes
  assertEqual "collision factory miss scans its bucket once" 3 =<< readIORef collisionEqualities

  bitmapHashes <- newIORef (0 :: Int)
  bitmapEqualities <- newIORef (0 :: Int)
  let tableHash key = case key of
        1 -> 0
        2 -> 32
        3 -> 1
        4 -> 64
        _ -> key
      bitmapPolicy = HashPolicy
        (\key -> unsafePerformIO $ do
          atomicModifyIORef' bitmapHashes (\count -> (count + 1, ()))
          pure (tableHash key))
        (\left right -> unsafePerformIO $ do
          atomicModifyIORef' bitmapEqualities (\count -> (count + 1, ()))
          pure (left == right))
      bitmapSource :: HashMap.HashMap Int Int
      bitmapSource = HashMap.fromListWith bitmapPolicy
        [(1, 10), (2, 20), (3, 30)]
  _ <- evaluate (HashMap.size bitmapSource)
  writeIORef bitmapHashes 0
  writeIORef bitmapEqualities 0
  (bitmapHit, bitmapValue) <- evaluate
    (HashMap.getOrAdd 2 (\_ -> error "bitmap hit selected add") bitmapSource)
  assertBool "bitmap child hit preserves the exact root"
    (HashMap.sharesRootWith bitmapSource bitmapHit)
  assertEqual "bitmap child hit returns stored value" 20 bitmapValue
  assertEqual "bitmap child hit hashes once" 1 =<< readIORef bitmapHashes
  assertEqual "bitmap child hit compares one payload" 1 =<< readIORef bitmapEqualities

  writeIORef bitmapHashes 0
  writeIORef bitmapEqualities 0
  (_, inlineValue) <- evaluate
    (HashMap.addOrUpdate 3
      (\_ -> error "bitmap inline hit selected add")
      (\_ stored -> stored + 1)
      bitmapSource)
  assertEqual "bitmap inline payload updates in one route" 31 inlineValue
  assertEqual "bitmap inline hit hashes once" 1 =<< readIORef bitmapHashes
  assertEqual "bitmap inline hit compares one payload" 1 =<< readIORef bitmapEqualities

  writeIORef bitmapHashes 0
  writeIORef bitmapEqualities 0
  (_, bitmapMissValue) <- evaluate
    (HashMap.getOrAdd 4 (const 40) bitmapSource)
  assertEqual "bitmap child miss adds its value" 40 bitmapMissValue
  assertEqual "bitmap child miss hashes once" 1 =<< readIORef bitmapHashes
  assertEqual "bitmap child miss compares no unrelated payload" 0 =<< readIORef bitmapEqualities

  case HashMap.addOrUpdateEither 7
        (\_ -> Left "unselected add")
        (\_ stored -> Right (stored + 2))
        source of
    Right (_, selected) -> assertEqual "fallible hit selects only update" 72 selected
    Left problem -> fail problem
  case HashMap.getOrAddEither 99 (\_ -> Left "selected add failed") source of
    Left problem -> assertEqual "fallible selected factory propagates" "selected add failed" problem
    Right _ -> fail "fallible selected factory unexpectedly published a map"
  case HashMap.addOrUpdateEither 7
        (\_ -> Right 0)
        (\_ _ -> Left "selected update failed")
        source of
    Left problem -> assertEqual "fallible selected update propagates" "selected update failed" problem
    Right _ -> fail "fallible selected update unexpectedly published a map"
  assertEqual "fallible failure leaves source usable" (Just 70) (HashMap.lookup 7 source)

  pureFactoryFailure <- try
    (evaluate (HashMap.getOrAdd 99 (\_ -> error "selected pure factory failed") source)) ::
    IO (Either SomeException (HashMap.HashMap Int Int, Int))
  assertBool "pure selected factory failure exposes no successor" (isLeft pureFactoryFailure)
  let hashFailurePolicy :: HashPolicy Int
      hashFailurePolicy = HashPolicy
        (\_ -> error "intentional hash failure")
        (==)
      hashFailureSource = HashMap.emptyWith hashFailurePolicy
  hashFailure <- try
    (evaluate (HashMap.getOrAdd 1 (const (10 :: Int)) hashFailureSource)) ::
    IO (Either SomeException (HashMap.HashMap Int Int, Int))
  assertBool "hash failure exposes no successor" (isLeft hashFailure)
  let equalityFailurePolicy :: HashPolicy Int
      equalityFailurePolicy = HashPolicy
        (const 0)
        (\_ _ -> error "intentional key-equality failure")
      equalityFailureSource = HashMap.singletonWith equalityFailurePolicy 1 (10 :: Int)
  keyEqualityFailure <- try
    (evaluate (HashMap.addOrUpdate 1 (const 0) (\_ stored -> stored + 1) equalityFailureSource)) ::
    IO (Either SomeException (HashMap.HashMap Int Int, Int))
  assertBool "key-equality failure exposes no successor" (isLeft keyEqualityFailure)
  assertEqual "callback failures leave source cardinality" 1 (HashMap.size source)

  valueEqualityCalls <- newIORef (0 :: Int)
  let representativePolicy = HashPolicy (hash . fst) (\left right -> fst left == fst right)
      storedKey = ("alpha", 1 :: Int)
      lookupKey = ("alpha", 2 :: Int)
      storedValue = CountedValue valueEqualityCalls 10 1
      equalCandidate = CountedValue valueEqualityCalls 10 2
      representativeSource = HashMap.singletonWith representativePolicy storedKey storedValue
      updateRepresentative caller stored =
        case (caller, stored) of
          (("alpha", 2), CountedValue _ 10 1) -> equalCandidate
          _ -> error "update factory did not receive caller key and stored value"
  (equalMap, selectedRepresentative) <- evaluate
    (HashMap.addOrUpdate lookupKey
      (\_ -> CountedValue valueEqualityCalls 0 0)
      updateRepresentative
      representativeSource)
  assertBool "equal update preserves the exact source root"
    (HashMap.sharesRootWith representativeSource equalMap)
  assertEqual "equal update retains stored key representative"
    (Just storedKey) (HashMap.actualKey lookupKey equalMap)
  case selectedRepresentative of
    CountedValue _ value identity -> do
      assertEqual "equal update returns stored value" 10 value
      assertEqual "equal update returns stored value representative" 1 identity
  assertEqual "equal update performs one value comparison" 1 =<< readIORef valueEqualityCalls

  let explosiveStored = ExplosiveValue 1
  _ <- evaluate explosiveStored
  let explosiveSource = HashMap.singleton (1 :: Int) explosiveStored
  (sameReferenceMap, _) <- evaluate
    (HashMap.addOrUpdate 1 (const (ExplosiveValue 0)) (\_ _ -> explosiveStored) explosiveSource)
  assertBool "same-reference update bypasses value equality"
    (HashMap.sharesRootWith explosiveSource sameReferenceMap)
  equalityFailure <- try
    (evaluate
      (HashMap.addOrUpdate
        1
        (const (ExplosiveValue 0))
        (\_ _ -> ExplosiveValue 2)
        explosiveSource)) ::
    IO (Either SomeException (HashMap.HashMap Int ExplosiveValue, ExplosiveValue))
  assertBool "value-equality failure exposes no successor" (isLeft equalityFailure)
  assertEqual "value-equality failure leaves source cardinality" 1
    (HashMap.size explosiveSource)

  let nullableSource = HashMap.singleton (1 :: Int) (Nothing :: Maybe Int)
  (nullableMap, nullableValue) <- evaluate
    (HashMap.getOrAdd 1 (const (Just 2)) nullableSource)
  assertBool "present Nothing is a getOrAdd hit"
    (HashMap.sharesRootWith nullableSource nullableMap)
  assertEqual "present Nothing remains distinguishable from absence" Nothing nullableValue

testPersistentHashBag :: IO ()
testPersistentHashBag = do
  let representativePolicy = HashPolicy
        (hash . map toLower . fst)
        (\left right -> map toLower (fst left) == map toLower (fst right))
      alpha = ("Alpha", 1 :: Int)
      equalAlpha = ("alpha", 2 :: Int)
      beta = ("Beta", 3 :: Int)
  bag <- expectRight "construct representative hash bag"
    (HashBag.fromListWith representativePolicy [alpha, equalAlpha, beta])
  assertEqual "hash bag distinct count" 2 (HashBag.distinctCount bag)
  assertEqual "hash bag expanded total" 3 (HashBag.totalCount bag)
  assertEqual "hash bag multiplicity uses its policy" 2 (HashBag.countOf equalAlpha bag)
  assertEqual "hash bag retains first representative" (Just alpha) (HashBag.actualValue equalAlpha bag)
  assertEqual "distinct and entry views have identical order"
    (HashBag.distinctItems bag) (map fst (HashBag.entries bag))
  assertEqual "expanded enumeration size" 3 (length (HashBag.toList bag))
  assertEqual "expanded enumeration repeats retained representative"
    2 (length (filter (== alpha) (HashBag.toList bag)))
  assertBool "constructed hash bag satisfies invariants" (HashBag.validStructure bag)

  hashCalls <- newIORef (0 :: Int)
  let countedPolicy = HashPolicy
        (\key -> unsafePerformIO $ do
          atomicModifyIORef' hashCalls (\count -> (count + 1, ()))
          pure (hash key))
        (==)
  counted <- expectRight "construct counted bag"
    (HashBag.addCopies (1 :: Int) 3 (HashBag.emptyWith countedPolicy))
  writeIORef hashCalls 0
  assertBool "negative copies are rejected" $
    case HashBag.addCopies 1 (-1) counted of
      Left (HashBag.NegativeCopies copies) -> copies == -1
      _ -> False
  assertEqual "negative copies are rejected before hashing" 0 =<< readIORef hashCalls
  zero <- expectRight "zero-copy addition" (HashBag.addCopies 1 0 counted)
  assertBool "zero-copy addition preserves the root" (HashBag.sharesRootWith counted zero)
  assertBool "negative removal is rejected" $
    case HashBag.removeCopies 1 (-1) counted of
      Left (HashBag.NegativeCopies copies) -> copies == -1
      _ -> False
  zeroRemoval <- expectRight "zero-copy removal" (HashBag.removeCopies 1 0 counted)
  assertBool "zero-copy removal preserves the root"
    (HashBag.sharesRootWith counted zeroRemoval)
  assertEqual "zero-copy addition avoids hashing" 0 =<< readIORef hashCalls
  positive <- expectRight "positive one-descent bag addition" (HashBag.addCopies 1 2 counted)
  assertEqual "positive bag addition hashes once" 1 =<< readIORef hashCalls
  assertEqual "positive bag addition updates multiplicity" 5 (HashBag.countOf 1 positive)

  partial <- expectRight "partial hash-bag removal" (HashBag.removeCopies 1 2 counted)
  assertEqual "partial removal retains class" 1 (HashBag.countOf 1 partial)
  saturated <- expectRight "saturated hash-bag removal"
    (HashBag.removeCopies 1 maxBound partial)
  assertBool "saturated removal deletes the class" (HashBag.null saturated)
  assertEqual "saturated removal updates total" 0 (HashBag.totalCount saturated)

  maximumBag <- expectRight "construct maximum multiplicity"
    (HashBag.addCopies (7 :: Int) maxBound HashBag.empty)
  assertBool "per-class overflow is checked" $
    case HashBag.add 7 maximumBag of
      Left HashBag.MultiplicityOverflow -> True
      _ -> False
  assertEqual "overflow retains source multiplicity" maxBound (HashBag.countOf 7 maximumBag)
  assertEqual "overflow retains source total"
    (fromIntegral (maxBound :: Int32)) (HashBag.totalCount maximumBag)

  nullable <- expectRight "construct nullable hash bag"
    (HashBag.fromList [Nothing, Nothing, Just ("value" :: String)])
  assertBool "hash bag supports Nothing representatives" (HashBag.member Nothing nullable)
  assertEqual "Nothing multiplicity" 2 (HashBag.countOf Nothing nullable)
  assertEqual "Nothing representative" (Just Nothing) (HashBag.actualValue Nothing nullable)

testPersistentHashBagAlgebra :: IO ()
testPersistentHashBagAlgebra = do
  let collisionPolicy = HashPolicy (const 0) (==)
  leftOne <- expectRight "left bag first class"
    (HashBag.addCopies (1 :: Int) 2 (HashBag.emptyWith collisionPolicy))
  left <- expectRight "left bag second class" (HashBag.add 2 leftOne)
  rightOne <- expectRight "right bag first class"
    (HashBag.add (1 :: Int) (HashBag.emptyWith collisionPolicy))
  rightTwo <- expectRight "right bag second class" (HashBag.addCopies 2 3 rightOne)
  right <- expectRight "right bag third class" (HashBag.add 3 rightTwo)

  united <- expectRight "hash-bag union" (HashBag.union left right)
  intersected <- expectRight "hash-bag intersection" (HashBag.intersection left right)
  excepted <- expectRight "hash-bag difference" (HashBag.difference left right)
  added <- expectRight "hash-bag sum" (HashBag.sum left right)
  assertEqual "union uses maximum multiplicities" [2, 3, 1]
    [HashBag.countOf key united | key <- [1, 2, 3]]
  assertEqual "intersection uses minimum multiplicities" [1, 1, 0]
    [HashBag.countOf key intersected | key <- [1, 2, 3]]
  assertEqual "difference uses saturating subtraction" [1, 0, 0]
    [HashBag.countOf key excepted | key <- [1, 2, 3]]
  assertEqual "sum uses checked addition" [3, 4, 1]
    [HashBag.countOf key added | key <- [1, 2, 3]]
  assertEqual "union total" 6 (HashBag.totalCount united)
  assertEqual "intersection total" 2 (HashBag.totalCount intersected)
  assertEqual "difference total" 1 (HashBag.totalCount excepted)
  assertEqual "sum total" 8 (HashBag.totalCount added)
  assertBool "union preserves invariants" (HashBag.validStructure united)
  assertBool "intersection preserves invariants" (HashBag.validStructure intersected)
  assertBool "difference preserves invariants" (HashBag.validStructure excepted)
  assertBool "sum preserves invariants" (HashBag.validStructure added)

  selfUnion <- expectRight "self union" (HashBag.union left left)
  selfIntersection <- expectRight "self intersection" (HashBag.intersection left left)
  selfDifference <- expectRight "self difference" (HashBag.difference left left)
  selfSum <- expectRight "self sum" (HashBag.sum left left)
  assertBool "self union shares receiver root" (HashBag.sharesRootWith left selfUnion)
  assertBool "self intersection shares receiver root" (HashBag.sharesRootWith left selfIntersection)
  assertBool "self difference is empty" (HashBag.null selfDifference)
  assertEqual "self sum genuinely doubles multiplicities" [4, 2]
    [HashBag.countOf key selfSum | key <- [1, 2]]
  let emptyArgument = HashBag.emptyWith collisionPolicy
  emptyUnion <- expectRight "union with empty" (HashBag.union left emptyArgument)
  emptySum <- expectRight "sum with empty" (HashBag.sum left emptyArgument)
  assertBool "union with empty shares receiver root" (HashBag.sharesRootWith left emptyUnion)
  assertBool "sum with empty shares receiver root" (HashBag.sharesRootWith left emptySum)
  maximumSelf <- expectRight "construct self-sum overflow bag"
    (HashBag.addCopies (99 :: Int) maxBound emptyArgument)
  assertBool "self sum checks multiplicity overflow" $
    case HashBag.sum maximumSelf maximumSelf of
      Left HashBag.MultiplicityOverflow -> True
      _ -> False

  let receiverPolicy = HashPolicy
        (hash . fst)
        (\candidateLeft candidateRight -> fst candidateLeft == fst candidateRight)
      argumentPolicy = HashPolicy hash (==)
      receiverAlpha = ("alpha", 0 :: Int)
      argumentAlphaOne = ("alpha", 1 :: Int)
      argumentAlphaTwo = ("alpha", 2 :: Int)
      argumentBetaOne = ("beta", 3 :: Int)
      argumentBetaTwo = ("beta", 4 :: Int)
  receiver <- expectRight "construct receiver-policy bag"
    (HashBag.addCopies receiverAlpha 2 (HashBag.emptyWith receiverPolicy))
  argument0 <- expectRight "construct strict alpha one"
    (HashBag.add argumentAlphaOne (HashBag.emptyWith argumentPolicy))
  argument1 <- expectRight "construct strict alpha two"
    (HashBag.addCopies argumentAlphaTwo 2 argument0)
  argument2 <- expectRight "construct strict beta one" (HashBag.add argumentBetaOne argument1)
  argument <- expectRight "construct strict beta two" (HashBag.add argumentBetaTwo argument2)
  let firstArgumentBeta = case listToMaybe
        [item | item <- HashBag.distinctItems argument, fst item == "beta"] of
        Just item -> item
        Nothing -> error "expected a beta representative in the normalized argument"
  normalizedUnion <- expectRight "receiver-policy normalized union"
    (HashBag.union receiver argument)
  assertEqual "normalization sums collapsed alpha class" 3
    (HashBag.countOf receiverAlpha normalizedUnion)
  assertEqual "normalization sums collapsed beta class" 2
    (HashBag.countOf argumentBetaOne normalizedUnion)
  assertEqual "receiver representative wins surviving class"
    (Just receiverAlpha) (HashBag.actualValue argumentAlphaOne normalizedUnion)
  assertEqual "first argument-order representative wins absent class"
    (Just firstArgumentBeta) (HashBag.actualValue argumentBetaOne normalizedUnion)
  normalizedIntersection <- expectRight "receiver-policy normalized intersection"
    (HashBag.intersection receiver argument)
  normalizedDifference <- expectRight "receiver-policy normalized difference"
    (HashBag.difference receiver argument)
  normalizedSum <- expectRight "receiver-policy normalized sum"
    (HashBag.sum receiver argument)
  assertBool "normalized logical no-op intersection shares receiver root"
    (HashBag.sharesRootWith receiver normalizedIntersection)
  assertBool "normalized difference saturates the receiver class"
    (HashBag.null normalizedDifference)
  assertEqual "normalized sum adds collapsed alpha counts" 5
    (HashBag.countOf receiverAlpha normalizedSum)
  assertEqual "normalized sum introduces collapsed beta counts" 2
    (HashBag.countOf argumentBetaOne normalizedSum)
  assertEqual "normalized sum retains receiver representative"
    (Just receiverAlpha) (HashBag.actualValue argumentAlphaOne normalizedSum)

  overflow0 <- expectRight "construct normalization maximum"
    (HashBag.addCopies argumentAlphaOne maxBound (HashBag.emptyWith argumentPolicy))
  overflowArgument <- expectRight "construct normalization overflow argument"
    (HashBag.add argumentAlphaTwo overflow0)
  let emptyReceiver = HashBag.emptyWith receiverPolicy
  assertBool "mismatched-policy normalization is eager and checked" $
    case HashBag.intersection emptyReceiver overflowArgument of
      Left HashBag.MultiplicityOverflow -> True
      _ -> False
  assertEqual "normalization failure leaves receiver unchanged" 0
    (HashBag.totalCount emptyReceiver)
  assertEqual "normalization failure leaves argument unchanged"
    (fromIntegral (maxBound :: Int32) + 1) (HashBag.totalCount overflowArgument)

testPersistentHashBagDeterministicModel :: IO ()
testPersistentHashBagDeterministicModel =
  go 0 0x5eedba9 (HashBag.emptyWith (HashPolicy (const 0) (==))) []
  where
    go operation seed bagValue model
      | operation == (1000 :: Int) = validate operation bagValue model
      | otherwise = do
          let seed' = (seed * 1103515245 + 12345) .&. 0x7fffffff :: Int64
              item = fromIntegral (seed' `mod` 24) :: Int
              copies = fromIntegral (1 + (seed' `div` 24) `mod` 4) :: Int32
              command = fromIntegral ((seed' `div` 97) `mod` 4) :: Int
          (nextBag, nextModel) <- case command of
            0 -> do
              updated <- expectRight "model addCopies" (HashBag.addCopies item copies bagValue)
              pure (updated, modelAdd item copies model)
            1 -> do
              updated <- expectRight "model removeCopies" (HashBag.removeCopies item copies bagValue)
              pure (updated, modelRemove item copies model)
            2 -> pure (HashBag.removeAll item bagValue, modelRemoveAll item model)
            _ -> do
              updated <- expectRight "model add" (HashBag.add item bagValue)
              pure (updated, modelAdd item 1 model)
          if operation `mod` 37 == 0
            then validate operation nextBag nextModel
            else pure ()
          go (operation + 1) seed' nextBag nextModel

    validate operation bagValue model = do
      let orderedModel = sort model
          expectedTotal = Prelude.sum [fromIntegral count | (_, count) <- model]
          expectedExpanded = concatMap
            (\(item, copies) -> replicate (fromIntegral copies) item)
            orderedModel
      assertEqual ("model distinct count at " ++ show operation)
        (length model) (HashBag.distinctCount bagValue)
      assertEqual ("model total at " ++ show operation)
        expectedTotal (HashBag.totalCount bagValue)
      assertEqual ("model entries at " ++ show operation)
        orderedModel (sort (HashBag.entries bagValue))
      assertEqual ("model expanded values at " ++ show operation)
        expectedExpanded (sort (HashBag.toList bagValue))
      assertBool ("model invariants at " ++ show operation)
        (HashBag.validStructure bagValue)

modelAdd :: Int -> Int32 -> [(Int, Int32)] -> [(Int, Int32)]
modelAdd item copies [] = [(item, copies)]
modelAdd item copies ((candidate, count) : rest)
  | item == candidate = (candidate, count + copies) : rest
  | otherwise = (candidate, count) : modelAdd item copies rest

modelRemove :: Int -> Int32 -> [(Int, Int32)] -> [(Int, Int32)]
modelRemove _ _ [] = []
modelRemove item copies ((candidate, count) : rest)
  | item /= candidate = (candidate, count) : modelRemove item copies rest
  | count > copies = (candidate, count - copies) : rest
  | otherwise = rest

modelRemoveAll :: Int -> [(Int, Int32)] -> [(Int, Int32)]
modelRemoveAll item = filter ((/= item) . fst)

testSetAlgebra :: IO ()
testSetAlgebra = do
  let left = HashSet.fromList [1 :: Int, 2, 3]
      right = HashSet.fromList [3 :: Int, 4]
  assertEqual "set union" [1, 2, 3, 4] (sort (HashSet.toList (HashSet.union left right)))
  assertEqual "set intersection" [3] (sort (HashSet.toList (HashSet.intersection left right)))
  assertEqual "set difference" [1, 2] (sort (HashSet.toList (HashSet.difference left right)))
  assertEqual "set symmetric difference" [1, 2, 4] (sort (HashSet.toList (HashSet.symmetricDifference left right)))
  assertBool "subset relation" (HashSet.isSubsetOf (HashSet.fromList [1 :: Int, 2]) left)
  assertBool "overlap relation" (HashSet.overlaps left right)
  assertBool "set equality ignores duplicates" (HashSet.setEquals left (HashSet.fromList [3 :: Int, 2, 1, 2]))

testCrossPolicySetRelations :: IO ()
testCrossPolicySetRelations = do
  let moduloTen = HashPolicy (`mod` 10) (\left right -> left `mod` 10 == (right :: Int) `mod` 10)
      receiver = HashSet.fromListWith moduloTen [1 :: Int, 2]
      equivalentArgument = HashSet.fromList [11 :: Int, 12]
      strictSupersetArgument = HashSet.fromList [11 :: Int, 12, 99]
  assertBool "cross-policy subset uses receiver policy" (HashSet.isSubsetOf receiver equivalentArgument)
  assertBool "cross-policy superset uses receiver policy" (HashSet.isSupersetOf receiver equivalentArgument)
  assertBool "cross-policy equality normalizes under receiver policy" (HashSet.setEquals receiver equivalentArgument)
  assertBool "cross-policy proper subset uses normalized membership" (HashSet.isProperSubsetOf receiver strictSupersetArgument)
  assertBool "cross-policy overlap uses receiver policy" (HashSet.overlaps receiver (HashSet.fromList [42 :: Int]))
  -- Proper-relation strictness must count the argument as the receiver's
  -- policy sees it: {11, 21} collapses to one element mod 10, so it is not a
  -- proper superset of {1} even though its raw size is larger, and {1, 2} is
  -- a proper superset of it even though the raw sizes are equal.
  let singleElement = HashSet.fromListWith moduloTen [1 :: Int]
      collapsingArgument = HashSet.fromList [11 :: Int, 21]
  assertBool
    "proper subset counts argument under receiver policy"
    (not (HashSet.isProperSubsetOf singleElement collapsingArgument))
  assertBool
    "proper superset counts argument under receiver policy"
    (HashSet.isProperSupersetOf receiver collapsingArgument)
  -- Symmetric difference must toggle each distinct argument element once
  -- under the receiver's policy: {11, 21} collapses to one element mod 10,
  -- so it removes 1 rather than deleting 1 and re-inserting 21.
  assertEqual
    "symmetric difference deduplicates argument under receiver policy"
    []
    (sort (HashSet.toList (HashSet.symmetricDifference singleElement collapsingArgument)))

testLargeFromList :: IO ()
testLargeFromList = do
  let upper = 100000 :: Int
      values = HashMap.fromList [(key, negate key) | key <- [0 .. upper - 1]]
      setValues = HashSet.fromList [0 .. upper - 1]
  assertEqual "large map fromList count" upper (HashMap.size values)
  assertEqual "large map fromList tail lookup" (Just (1 - upper)) (HashMap.lookup (upper - 1) values)
  assertEqual "large set fromList count" upper (HashSet.size setValues)
  assertBool "large set fromList tail membership" (HashSet.member (upper - 1) setValues)

testMerkleEncodingAndCore :: IO ()
testMerkleEncodingAndCore = do
  assertEqual
    "pure SHA-256 matches the standard abc vector"
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    (digestHex (hashBytes (ByteStringChar8.pack "abc")))
  let sequentialBytes length' = ByteString.pack (take length' [0 ..])
  assertEqual "pure SHA-256 handles a 55-byte padding boundary"
    "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"
    (digestHex (hashBytes (sequentialBytes 55)))
  assertEqual "pure SHA-256 handles a 56-byte padding boundary"
    "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"
    (digestHex (hashBytes (sequentialBytes 56)))
  assertEqual "pure SHA-256 handles an exact 64-byte block"
    "fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"
    (digestHex (hashBytes (sequentialBytes 64)))
  assertEqual "pure SHA-256 handles a 65-byte multi-block input"
    "4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"
    (digestHex (hashBytes (sequentialBytes 65)))
  assertEqual "Int32 codec big-endian vector"
    (Right (ByteString.pack [1, 2, 3, 4]))
    (encodeMerkleValue int32MerkleCodec 0x01020304)
  assertEqual "Int64 codec rejects short bytes" True
    (isLeft (decodeMerkleValue int64MerkleCodec (ByteString.replicate 7 0)))
  assertEqual "nullable UTF-8 vector"
    (Right (ByteString.pack [1, 65, 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80]))
    (encodeMerkleValue nullableUtf8MerkleCodec (Just "Aé😀"))
  assertBool "nullable UTF-8 rejects overlong encoding"
    (isLeft (decodeMerkleValue nullableUtf8MerkleCodec (ByteString.pack [1, 0xc0, 0x80])))
  assertEqual "nullable bytes preserve embedded zeros"
    (Right (Just (ByteString.pack [0, 1, 0xff])))
    (decodeMerkleValue nullableBytesMerkleCodec (ByteString.pack [1, 0, 1, 0xff]))
  assertBool "digest parser rejects the wrong length" (isLeft (parseDigestHex "00"))

  stringPolicy <- expectRight "construct golden Merkle policy"
    (makeMerkleSearchTreePolicy "golden-int-string-v1" compare int32MerkleCodec nullableUtf8MerkleCodec)
  assertEqual "golden policy domain"
    "fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"
    (digestHex (merkleDomainDigest stringPolicy))
  assertEqual "golden empty root"
    "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
    (digestHex (merkleEmptyDigest stringPolicy))
  goldenTree <- expectRight "insert golden entry"
    (Merkle.insert 42 (Just "forty-two") (Merkle.empty stringPolicy))
  assertEqual "golden single-entry root"
    "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"
    (digestHex (Merkle.rootDigest goldenTree))
  let expectedGoldenBlock = decodeHex
        "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2000000000100000001000000040000002a0000000a01666f7274792d74776f98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb398900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
  assertEqual "golden MST2 block bytes"
    [Merkle.MerkleBlockView (Merkle.rootDigest goldenTree) expectedGoldenBlock]
    (Merkle.blocksPreorder goldenTree)
  assertBool "golden tree validates" (isRight (Merkle.validateStructure goldenTree))

  widePolicy <- expectRight "construct wide golden Merkle policy"
    (makeMerkleSearchTreePolicy "golden-wide-i32-i32-v1" compare int32MerkleCodec int32MerkleCodec)
  let wideKeys = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 38, 44, 59, 464] :: [Int32]
  wideTree <- expectRight "construct wide multi-level golden Merkle tree"
    (Merkle.fromList [(key, negate key - 1) | key <- wideKeys] widePolicy)
  assertEqual "wide golden policy domain"
    "eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917"
    (digestHex (merkleDomainDigest widePolicy))
  assertEqual "wide multi-level golden root"
    "9afd7ba98ec91f72074c5f2c272ca1334244fb43a631e0fb440e02799eee8755"
    (digestHex (Merkle.rootDigest wideTree))
  assertEqual "wide multi-level golden size" 14 (Merkle.size wideTree)
  assertEqual "wide multi-level golden block count" 4 (Merkle.blockCount wideTree)
  assertEqual "wide multi-level golden height" 3 (Merkle.height wideTree)
  let expectedWideRootBlock = decodeHex
        "4d53543201eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917020000000e00000002000000040000003b00000004ffffffc400000004000001d000000004fffffe2f790b862e0ef81c9e6debdf38c1099c565887fe87aed84f26dfba736de256d4d5018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608"
  case Merkle.blocksPreorder wideTree of
    Merkle.MerkleBlockView rootDigest rootBytes : _ -> do
      assertEqual "wide multi-level preorder starts at root" (Merkle.rootDigest wideTree) rootDigest
      assertEqual "wide multi-level root level byte" 2 (ByteString.index rootBytes 37)
      assertEqual "wide multi-level root block bytes" expectedWideRootBlock rootBytes
    [] -> fail "wide multi-level golden tree unexpectedly has no blocks"
  assertBool "wide multi-level golden tree validates" (isRight (Merkle.validateStructure wideTree))

  intPolicy <- expectRight "construct integer Merkle policy"
    (makeMerkleSearchTreePolicy "haskell-core-model-v1" compare int32MerkleCodec int32MerkleCodec)
  let ascendingSource = [(key, key * 7 - 3) | key <- [0 :: Int32 .. 2047]]
  forward <- expectRight "build ascending canonical Merkle tree" (Merkle.fromList ascendingSource intPolicy)
  backward <- expectRight "build descending canonical Merkle tree" (Merkle.fromList (reverse ascendingSource) intPolicy)
  assertEqual "independent histories have one root digest" (Merkle.rootDigest forward) (Merkle.rootDigest backward)
  assertEqual "independent histories have exact preorder blocks" (Merkle.blocksPreorder forward) (Merkle.blocksPreorder backward)
  assertEqual "canonical tree size" 2048 (Merkle.size forward)
  assertBool "canonical tree uses wide blocks" (any ((> 1) . Merkle.shapeEntriesInBlock) (Merkle.shape forward))
  assertBool "canonical tree validates" (isRight (Merkle.validateStructure forward))

  let cursorTreeSource = take 3 [(key, key * 7 - 3) | key <- [-10, 0, 10 :: Int32]]
  cursorTree <- expectRight "build Merkle cursor source" (Merkle.fromList cursorTreeSource intPolicy)
  let cursorKeys = [-10, 0, 10 :: Int32]
  forM_ [0 .. Merkle.size cursorTree] $ \position -> do
    cursorValue <- maybe (fail "expected Merkle cursor rank") pure (Merkle.cursorAt position cursorTree)
    assertEqual "Merkle cursor position" position (Merkle.cursorPosition cursorValue)
    assertEqual "Merkle cursor start" (position == 0) (Merkle.cursorIsAtStart cursorValue)
    assertEqual "Merkle cursor end" (position == Merkle.size cursorTree) (Merkle.cursorIsAtEnd cursorValue)
    assertEqual "Merkle cursor previous" (atMay cursorKeys (position - 1))
      (Merkle.entryKey <$> Merkle.cursorPeekPrevious cursorValue)
    assertEqual "Merkle cursor next" (atMay cursorKeys position)
      (Merkle.entryKey <$> Merkle.cursorPeekNext cursorValue)
  assertEqual "Merkle cursor lower bound" 1
    (Merkle.cursorPosition (Merkle.lowerBoundCursor (-5) cursorTree))
  assertEqual "Merkle cursor upper bound" 2
    (Merkle.cursorPosition (Merkle.upperBoundCursor 0 cursorTree))
  let exactCursor = Merkle.cursorSearchCursor (Merkle.cursorAtKey 0 cursorTree)
      missCursor = Merkle.cursorAtKey 5 cursorTree
  assertBool "Merkle cursor exact hit" (Merkle.cursorSearchFound (Merkle.cursorAtKey 0 cursorTree))
  assertEqual "Merkle cursor exact miss rank" 2
    (Merkle.cursorPosition (Merkle.cursorSearchCursor missCursor))
  assertBool "Merkle cursor exact miss" (not (Merkle.cursorSearchFound missCursor))
  changedCursor <- expectRight "replace Merkle cursor next value"
    (Merkle.cursorSetNextValue 999 exactCursor) >>= maybe (fail "expected Merkle next entry") pure
  assertEqual "Merkle cursor source remains unchanged" (Just (-3)) (Merkle.lookup 0 cursorTree)
  assertEqual "Merkle cursor changed value" (Just 999)
    (Merkle.lookup 0 (Merkle.cursorSnapshot changedCursor))
  insertedCursor <- expectRight "insert through Merkle cursor"
    (Merkle.cursorInsert 5 500 (Merkle.lowerBoundCursor 5 cursorTree))
  assertEqual "Merkle cursor insertion position" 3 (Merkle.cursorPosition insertedCursor)
  assertEqual "Merkle cursor insertion keys" [-10, 0, 5, 10]
    (map fst (Merkle.toAscList (Merkle.cursorSnapshot insertedCursor)))
  restoredCursor <- expectRight "delete previous Merkle cursor entry"
    (Merkle.cursorDeletePrevious insertedCursor) >>= maybe (fail "expected previous entry") pure
  assertEqual "Merkle cursor hash restoration" (Merkle.rootDigest cursorTree)
    (Merkle.rootDigest (Merkle.cursorSnapshot restoredCursor))
  assertEqual "Merkle cursor invalid rank" True (isNothing (Merkle.cursorAt 4 cursorTree))
  assertEqual "Merkle cursor before start" True (isNothing (Merkle.cursorMovePrevious (Merkle.cursor cursorTree)))
  assertEqual "Merkle cursor after end" True (isNothing (Merkle.cursorMoveNext (Merkle.cursorAtEnd cursorTree)))
  assertEqual "Merkle cursor duplicate" (Left Merkle.MerkleCursorDuplicateKey)
    (fmap Merkle.cursorPosition (Merkle.cursorInsert 0 1 exactCursor))
  assertEqual "Merkle cursor wrong gap" (Left (Merkle.MerkleCursorWrongGap 2 0))
    (fmap Merkle.cursorPosition (Merkle.cursorInsert 5 1 (Merkle.cursor cursorTree)))
  let rankKeys = filter ((/= 0) . (`mod` 7)) [-500 .. 500 :: Int32]
  rankTree <- expectRight "build Merkle cursor rank model"
    (Merkle.fromList [(key, key) | key <- rankKeys] intPolicy)
  forM_ [-550, -539 .. 550 :: Int32] $ \probe -> do
    let rank = length (takeWhile (< probe) rankKeys)
        found = atMay rankKeys rank == Just probe
        searched = Merkle.cursorAtKey probe rankTree
    assertEqual "Merkle cursor model lower rank" rank
      (Merkle.cursorPosition (Merkle.lowerBoundCursor probe rankTree))
    assertEqual "Merkle cursor model upper rank" (rank + if found then 1 else 0)
      (Merkle.cursorPosition (Merkle.upperBoundCursor probe rankTree))
    assertEqual "Merkle cursor model exact rank" rank
      (Merkle.cursorPosition (Merkle.cursorSearchCursor searched))
    assertEqual "Merkle cursor model exact presence" found (Merkle.cursorSearchFound searched)

  noOp <- expectRight "replace with exact encoded value" (Merkle.insert 99 (99 * 7 - 3) forward)
  assertEqual "exact replacement is a root no-op" (Merkle.rootDigest forward) (Merkle.rootDigest noOp)
  changed <- expectRight "replace one Merkle value" (Merkle.insert 99 (-99) forward)
  removed <- expectRight "remove one Merkle key" (Merkle.delete 17 changed)
  added <- expectRight "add one Merkle key" (Merkle.insert 5000 12345 removed)
  differences <- expectRight "diff compatible Merkle trees" (Merkle.diffWith (==) forward added)
  assertEqual "typed Merkle diff count" 3 (length differences)
  assertBool "one update retains off-path content blocks"
    (Merkle.commonBlockCount forward changed >= Merkle.blockCount forward - Merkle.height forward)
  assertEqual "retained old version remains unchanged" (Just (99 * 7 - 3)) (Merkle.lookup 99 forward)
  assertEqual "changed successor has new value" (Just (-99)) (Merkle.lookup 99 changed)
  assertEqual "removed successor lacks key" Nothing (Merkle.lookup 17 removed)
  assertEqual "added successor has key" (Just 12345) (Merkle.lookup 5000 added)
  assertBool "edited tree validates" (isRight (Merkle.validateStructure added))
  rangeValues <- expectRight "enumerate inclusive Merkle range" (Merkle.range 100 110 added)
  assertEqual "inclusive Merkle range keys" [100 .. 110] (map Merkle.entryKey rangeValues)
  assertBool "reversed Merkle range rejected" (isLeft (Merkle.range 3 2 added))

  let folded = foldl applyMerkle (Right (Merkle.empty intPolicy, [])) (take 10000 (iterate nextState (0x13579bdf :: Int)))
      nextState state = state * 1664525 + 1013904223
      applyMerkle result state = do
        (tree, model) <- result
        let key = fromIntegral (((state `div` 256) `mod` 401) - 200) :: Int32
            value = fromIntegral (state `mod` 100000) :: Int32
            rest = filter ((/= key) . fst) model
        if state `mod` 5 == 0
          then do
            successor <- Merkle.delete key tree
            Right (successor, rest)
          else do
            successor <- Merkle.insert key value tree
            Right (successor, (key, value) : rest)
  (modelTree, modelValues) <- expectRight "run retained Merkle model" folded
  assertEqual "Merkle randomized model" (sort modelValues) (Merkle.toAscList modelTree)
  assertBool "Merkle randomized model validates" (isRight (Merkle.validateStructure modelTree))

  let caseCodec = MerkleCodec
        { merkleEncodingId = "ascii-lower-v1"
        , encodeMerkleValue = Right . ByteStringChar8.pack . map toLower
        , decodeMerkleValue = Right . ByteStringChar8.unpack
        }
  casePolicy <- expectRight "construct equivalence-coherent string policy"
    (makeMerkleSearchTreePolicy "case-key-v1" (\left right -> compare (map toLower left) (map toLower right)) caseCodec int32MerkleCodec)
  firstKey <- expectRight "insert first key representative" (Merkle.insert "Hello" 1 (Merkle.empty casePolicy))
  replacedKey <- expectRight "replace equivalent key representative" (Merkle.insert "HELLO" 2 firstKey)
  assertEqual "first equivalent key representative survives" ["Hello"] (map fst (Merkle.toAscList replacedKey))
  assertEqual "last equivalent value survives" (Just 2) (Merkle.lookup "hello" replacedKey)

  runConcurrent "Merkle concurrent reads" 8 $ do
    forM_ [1 :: Int .. 32] $ \_ -> do
      assertEqual "concurrent Merkle root" (Merkle.rootDigest added) (Merkle.rootDigest added)
      assertEqual "concurrent Merkle lookup" (Just (-99)) (Merkle.lookup 99 added)
      assertBool "concurrent Merkle validation" (isRight (Merkle.validateStructure added))

testConcurrentReads :: IO ()
testConcurrentReads = do
  let mapValues = HashMap.fromList [(key, key * 3 - 100) | key <- [0 :: Int .. 255]]
      setValues = HashSet.fromList [0 :: Int .. 255]
      expectedMap = [(key, key * 3 - 100) | key <- [0 :: Int .. 255]]
      expectedSet = [0 :: Int .. 255]
  runConcurrent "hamt concurrent reads" 8 $ do
    forM_ [1 :: Int .. 128] $ \_ -> do
      assertEqual "concurrent map size" 256 (HashMap.size mapValues)
      assertEqual "concurrent map lookup" (Just 284) (HashMap.lookup 128 mapValues)
      assertEqual "concurrent map contents" expectedMap (sort (HashMap.toList mapValues))
      assertEqual "concurrent set size" 256 (HashSet.size setValues)
      assertBool "concurrent set membership" (HashSet.member 200 setValues)
      assertEqual "concurrent set contents" expectedSet (sort (HashSet.toList setValues))

testTransientSessions :: IO ()
testTransientSessions = do
  let casePolicy = HashPolicy length (\left right -> map toLower left == map toLower right)
      source = HashMap.fromListWith casePolicy [("Alpha", 1 :: Int), ("Beta", 2)]

  clean <- Transient.mapToTransient source
  assertEqual "transient map initial size" 2 =<< Transient.mapTransientSize clean
  assertEqual "transient map lookup" (Just 1) =<< Transient.mapTransientLookup "ALPHA" clean
  assertEqual "transient map keeps representative" (Just "Alpha") =<<
    Transient.mapTransientActualKey "ALPHA" clean
  assertBool "transient duplicate is a no-op" . not =<<
    Transient.mapTransientTryAdd "ALPHA" 99 clean
  assertBool "transient equal put is a no-op" . not =<<
    Transient.mapTransientPut "alpha" 1 clean
  assertBool "transient missing delete is a no-op" . not =<<
    Transient.mapTransientDelete "missing" clean
  cleanPublished <- Transient.persistMap clean
  assertBool "clean transient publication retains source root"
    (HashMap.sharesRootWith source cleanPublished)
  cleanConsumed <- try (Transient.mapTransientSize clean) :: IO (Either Transient.TransientException Int)
  assertEqual "map transient is consumed" (Left Transient.TransientConsumed) cleanConsumed

  edited <- Transient.mapToTransient source
  assertBool "transient put changes value" =<< Transient.mapTransientPut "ALPHA" 3 edited
  assertEqual "put retains first key representative" (Just "Alpha") =<<
    Transient.mapTransientActualKey "alpha" edited
  assertBool "transient try-add inserts" =<< Transient.mapTransientTryAdd "Gamma" 4 edited
  duplicate <- try (Transient.mapTransientAdd "GAMMA" 5 edited) ::
    IO (Either Transient.TransientException ())
  assertEqual "transient add rejects duplicate" (Left Transient.TransientDuplicateKey) duplicate
  assertBool "transient delete removes" =<< Transient.mapTransientDelete "beta" edited
  editedPublished <- Transient.persistMap edited
  assertEqual "transient map edited contents"
    [("Alpha", 3), ("Gamma", 4)]
    (sort (HashMap.toList editedPublished))
  assertEqual "persistent source remains isolated"
    [("Alpha", 1), ("Beta", 2)]
    (sort (HashMap.toList source))

  model <- Transient.newMapTransient
  forM_ [0 :: Int .. 127] $ \key -> do
    _ <- Transient.mapTransientPut key (key * 7) model
    pure ()
  forM_ [0, 3 .. 126] $ \key -> do
    _ <- Transient.mapTransientDelete key model
    pure ()
  forM_ [64 :: Int .. 191] $ \key -> do
    _ <- Transient.mapTransientPut key (negate key) model
    pure ()
  modeled <- Transient.persistMap model
  let reference0 = HashMap.fromList [(key, key * 7) | key <- [0 :: Int .. 127]]
      reference1 = foldr HashMap.delete reference0 [0, 3 .. 126]
      reference2 = foldr (\key -> HashMap.insert key (negate key)) reference1 [64 :: Int .. 191]
  assertEqual "transient deterministic history matches persistent model"
    (sort (HashMap.toList reference2))
    (sort (HashMap.toList modeled))
  assertBool "transient history remains canonical" (HashMap.validStructure modeled)

  let failingPolicy = HashPolicy
        (\key -> if key == (99 :: Int) then error "injected hash failure" else key)
        (==)
      failureSource = HashMap.fromListWith failingPolicy [(1 :: Int, "one")]
  failing <- Transient.mapToTransient failureSource
  failedEdit <- try (Transient.mapTransientTryAdd 99 "bad" failing) :: IO (Either SomeException Bool)
  assertBool "transient callback failure propagates" (isLeft failedEdit)
  assertEqual "failed transient edit leaves session active" (Just "one") =<<
    Transient.mapTransientLookup 1 failing
  failurePublished <- Transient.persistMap failing
  assertBool "failed transient edit retains source root"
    (HashMap.sharesRootWith failureSource failurePublished)

  clearedMap <- Transient.mapToTransient source
  assertBool "transient map clear changes a nonempty session" =<<
    Transient.mapTransientClear clearedMap
  assertEqual "transient map clear empties the session" 0 =<<
    Transient.mapTransientSize clearedMap
  assertBool "transient map clear is a no-op when already empty" . not =<<
    Transient.mapTransientClear clearedMap
  assertBool "transient map clear preserves the session policy" =<<
    Transient.mapTransientPut "DELTA" 5 clearedMap
  assertEqual "transient map remains usable after clear" (Just 5) =<<
    Transient.mapTransientLookup "delta" clearedMap
  assertBool "transient map can be cleared again" =<<
    Transient.mapTransientClear clearedMap
  clearedMapPublished <- Transient.persistMap clearedMap
  assertEqual "transient map publishes the cleared state" 0 (HashMap.size clearedMapPublished)
  assertEqual "transient map clear leaves its source isolated" 2 (HashMap.size source)

  let setSource = HashSet.fromListWith casePolicy ["Alpha", "Beta"]
      equivalentSet = HashSet.fromListWith casePolicy ["ALPHA", "beta"]
      setSuperset = HashSet.fromListWith casePolicy ["alpha", "BETA", "Gamma"]
      setSubset = HashSet.fromListWith casePolicy ["ALPHA"]
      disjointSet = HashSet.fromListWith casePolicy ["missing"]
  relationSession <- Transient.setToTransient setSource
  assertBool "transient set equality uses the receiver policy" =<<
    Transient.setTransientEquals relationSession equivalentSet
  assertBool "transient set subset relation is direct" =<<
    Transient.setTransientIsSubsetOf relationSession setSuperset
  assertBool "transient set proper-subset relation is direct" =<<
    Transient.setTransientIsProperSubsetOf relationSession setSuperset
  assertBool "transient set superset relation is direct" =<<
    Transient.setTransientIsSupersetOf relationSession setSubset
  assertBool "transient set proper-superset relation is direct" =<<
    Transient.setTransientIsProperSupersetOf relationSession setSubset
  assertBool "transient set overlap relation finds an equivalent representative" =<<
    Transient.setTransientOverlaps relationSession equivalentSet
  assertBool "transient set overlap relation rejects a disjoint set" . not =<<
    Transient.setTransientOverlaps relationSession disjointSet
  assertBool "transient set clear changes a nonempty session" =<<
    Transient.setTransientClear relationSession
  assertEqual "transient set clear empties the session" 0 =<<
    Transient.setTransientSize relationSession
  assertBool "transient set clear is a no-op when already empty" . not =<<
    Transient.setTransientClear relationSession
  assertBool "transient set clear preserves the session policy" =<<
    Transient.setTransientAdd "DELTA" relationSession
  assertBool "transient set remains usable after clear" =<<
    Transient.setTransientMember "delta" relationSession
  assertBool "transient set can be cleared again" =<<
    Transient.setTransientClear relationSession
  clearedSetPublished <- Transient.persistSet relationSession
  assertEqual "transient set publishes the cleared state" 0 (HashSet.size clearedSetPublished)
  assertEqual "transient set clear leaves its source isolated" 2 (HashSet.size setSource)

  setSession <- Transient.setToTransient setSource
  assertBool "transient set duplicate is a no-op" . not =<<
    Transient.setTransientAdd "ALPHA" setSession
  assertEqual "transient set keeps representative" (Just "Alpha") =<<
    Transient.setTransientActualValue "alpha" setSession
  assertBool "transient set inserts" =<< Transient.setTransientAdd "Gamma" setSession
  assertBool "transient set removes" =<< Transient.setTransientDelete "beta" setSession
  setPublished <- Transient.persistSet setSession
  assertEqual "transient set edited contents" ["Alpha", "Gamma"] (sort (HashSet.toList setPublished))
  assertEqual "persistent set source remains isolated" ["Alpha", "Beta"] (sort (HashSet.toList setSource))
  setConsumed <- try (Transient.setTransientMember "Alpha" setSession) ::
    IO (Either Transient.TransientException Bool)
  assertEqual "set transient is consumed" (Left Transient.TransientConsumed) setConsumed

runConcurrent :: String -> Int -> IO () -> IO ()
runConcurrent label workerCount action = do
  boxes <- replicateM workerCount newEmptyMVar
  forM_ boxes $ \box -> do
    _ <- forkIO $ do
      result <- try action :: IO (Either SomeException ())
      putMVar box result
    pure ()
  results <- mapM takeMVar boxes
  forM_ results $ \result ->
    case result of
      Right () -> pure ()
      Left exception -> fail (label ++ ": worker failed: " ++ show exception)

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")

isNothing :: Maybe a -> Bool
isNothing Nothing = True
isNothing _ = False

isLeft :: Either a b -> Bool
isLeft (Left _) = True
isLeft _ = False

isRight :: Either a b -> Bool
isRight (Right _) = True
isRight _ = False

atMay :: [a] -> Int -> Maybe a
atMay values index
  | index < 0 = Nothing
  | otherwise = case drop index values of
      value : _ -> Just value
      [] -> Nothing

expectRight :: Show error => String -> Either error value -> IO value
expectRight _ (Right value) = pure value
expectRight label (Left problem) = fail (label ++ ": " ++ show problem)

decodeHex :: String -> ByteString.ByteString
decodeHex source = ByteString.pack (go 0 source)
  where
    go _ [] = []
    go index (high : low : rest) =
      let highValue = hexDigit index high
          lowValue = hexDigit (index + 1) low
       in (highValue * 16 + lowValue) : go (index + 2) rest
    go _ _ = error "test hexadecimal vector has odd length"

    hexDigit :: Int -> Char -> Word8
    hexDigit _ value | value >= '0' && value <= '9' = fromIntegral (fromEnum value - fromEnum '0')
    hexDigit _ value | value >= 'a' && value <= 'f' = fromIntegral (fromEnum value - fromEnum 'a' + 10)
    hexDigit _ value | value >= 'A' && value <= 'F' = fromIntegral (fromEnum value - fromEnum 'A' + 10)
    hexDigit index _ = error ("invalid test hexadecimal digit at " ++ show index)
