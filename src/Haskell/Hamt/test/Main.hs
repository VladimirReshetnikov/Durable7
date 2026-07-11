module Main (main) where

import Prelude hiding (lookup, null)

import Control.Concurrent (forkIO, newEmptyMVar, putMVar, takeMVar)
import Control.Exception (SomeException, evaluate, try)
import Control.Monad (forM_, replicateM)
import Data.Char (toLower)
import Data.List (sort)

import Data.Structures.Hamt.Hashable (hash)
import Data.Structures.Hamt.HashMap (HashPolicy(..))
import qualified Data.Structures.Hamt.HashMap as HashMap
import qualified Data.Structures.Hamt.HashSet as HashSet

main :: IO ()
main = do
  testMapBasics
  testCollisionPolicy
  testCollisionShrinkCanonicalization
  testActualKeyPreservation
  testAdjustAndStrictMapping
  testSetAlgebra
  testCrossPolicySetRelations
  testLargeFromList
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
