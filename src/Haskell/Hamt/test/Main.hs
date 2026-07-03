module Main (main) where

import Prelude hiding (lookup, null)

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
  testActualKeyPreservation
  testSetAlgebra
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

testActualKeyPreservation :: IO ()
testActualKeyPreservation = do
  let casePolicy = HashPolicy (hash . map toLower) (\left right -> map toLower left == map toLower right)
      values = HashMap.insert "HELLO" 2 (HashMap.singletonWith casePolicy "Hello" (1 :: Int))
  assertEqual "case-insensitive lookup" (Just 2) (HashMap.lookup "hello" values)
  assertEqual "replacement preserves original key" (Just "Hello") (HashMap.actualKey "hello" values)

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
