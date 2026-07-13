module Main (main) where

import Prelude hiding (lookup, null)

import Control.Concurrent (forkIO, newEmptyMVar, putMVar, takeMVar)
import Control.Exception (SomeException, evaluate, try)
import Control.Monad (forM_, replicateM)
import Data.Bits ((.&.), shiftL)
import qualified Data.ByteString as ByteString
import qualified Data.ByteString.Char8 as ByteStringChar8
import Data.Char (toLower)
import Data.Int (Int32, Int64)
import Data.IORef (atomicModifyIORef', newIORef, readIORef, writeIORef)
import Data.List (sort)
import Data.Word (Word8)
import System.IO.Unsafe (unsafePerformIO)

import Data.Structures.Hamt.Hashable (hash)
import Data.Structures.Hamt.HashMap (HashPolicy(..))
import qualified Data.Structures.Hamt.HashMap as HashMap
import qualified Data.Structures.Hamt.HashSet as HashSet
import Data.Structures.Hamt.MerkleEncoding
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
import qualified Data.Structures.Hamt.MerkleSearchTree as Merkle
import qualified Data.Structures.Hamt.Patricia as Patricia
import PersistenceTests (runPersistenceTests)

main :: IO ()
main = do
  testMapBasics
  testCollisionPolicy
  testCollisionShrinkCanonicalization
  testChampCanonicalizationAndDiff
  testChampTopologyRejectsDifferentCollisionKeys
  testChampTerminalHashFragments
  testChampStructuralAlgebra
  testPatriciaMapsAndSets
  testActualKeyPreservation
  testAdjustAndStrictMapping
  testSetAlgebra
  testCrossPolicySetRelations
  testLargeFromList
  testMerkleEncodingAndCore
  runPersistenceTests
  testConcurrentReads
  putStrLn "tools-data-structures-hamt tests passed"

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

testChampTopologyRejectsDifferentCollisionKeys :: IO ()
testChampTopologyRejectsDifferentCollisionKeys = do
  let collisionPolicy = HashPolicy (const 7) (==)
      left = HashMap.fromListWith collisionPolicy [(1 :: Int, "a"), (2, "b")]
      sameReversed = HashMap.fromListWith collisionPolicy [(2 :: Int, "b"), (1, "a")]
      different = HashMap.fromListWith collisionPolicy [(1 :: Int, "a"), (3, "c")]
  assertBool "collision topology ignores insertion order" (HashMap.sameTopology left sameReversed)
  assertBool "collision topology compares key contents" (not (HashMap.sameTopology left different))

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
