module Main (main) where

import Prelude hiding (drop, last, lookup, null, reverse, take)

import Control.Monad (foldM_)
import Data.Char (toLower)
import qualified Data.List as List
import Data.Maybe (fromMaybe)

import Data.Structures.Hamt.Hashable (hash)
import Data.Structures.Hamt.HashMap (HashPolicy(..))
import qualified Data.Structures.Wolfram.Association as Association
import qualified Data.Structures.Wolfram.List as WolframList

main :: IO ()
main = do
  testListExamples
  testAssociationOrderingExamples
  testAssociationCustomPolicy
  testAssociationRelabelStress
  testAssociationGeneratedHistory
  putStrLn "tools-data-structures-wolfram tests passed"

testListExamples :: IO ()
testListExamples = do
  let values = WolframList.fromList [1 :: Int .. 5]
  assertEqual "list count" 5 (WolframList.count values)
  assertEqual "list endpoints" (Just 1, Just 5) (WolframList.first values, WolframList.last values)
  assertEqual "list index" (Just 3) (WolframList.index 2 values)
  assertEqual "list append" [1, 2, 3, 4, 5, 6, 7] (WolframList.toList (WolframList.addRange values [6, 7]))
  assertEqual "list prepend" [0, 1, 2, 3, 4, 5] (WolframList.toList (WolframList.cons 0 values))
  assertEqual "list insert" (Just [1, 2, 99, 3, 4, 5]) (WolframList.toList <$> WolframList.insertAt 2 99 values)
  assertEqual "list insert range" (Just [1, 8, 9, 2, 3, 4, 5]) (WolframList.toList <$> WolframList.insertRange 1 [8, 9] values)
  assertEqual "list delete" (Just [1, 2, 4, 5]) (WolframList.toList <$> WolframList.deleteAt 2 values)
  assertEqual "list remove range" (Just [1, 5]) (WolframList.toList <$> WolframList.removeRange 1 3 values)
  assertEqual "list slice" (Just [2, 3, 4]) (WolframList.toList <$> WolframList.slice 1 3 values)
  assertEqual "list take" (Just [1, 2]) (WolframList.toList <$> WolframList.take 2 values)
  assertEqual "list drop" (Just [3, 4, 5]) (WolframList.toList <$> WolframList.drop 2 values)
  assertEqual "list reverse" [5, 4, 3, 2, 1] (WolframList.toList (WolframList.reverse values))
  assertEqual "list map" [2, 4, 6, 8, 10] (WolframList.toList (WolframList.map (* 2) values))
  assertEqual "list indexOf" (Just 3) (WolframList.indexOf (==) 4 values)
  assertBool "list member" (WolframList.member (==) 5 values)
  assertEqual "list bad slice rejected" Nothing (WolframList.slice 4 2 values)

testAssociationOrderingExamples :: IO ()
testAssociationOrderingExamples = do
  let duplicates = Association.fromList [("a", 1 :: Int), ("b", 2), ("a", 3)]
  assertEqual "association duplicate construction" [("a", 3), ("b", 2)] (Association.toList duplicates)
  assertEqual "association setItem preserves position" [("a", 5), ("b", 2)] (Association.toList (Association.setItem "a" 5 duplicates))
  assertEqual "association setItem appends new key" [("a", 3), ("b", 2), ("c", 4)] (Association.toList (Association.setItem "c" 4 duplicates))
  assertEqual "association append moves existing key" [("b", 2), ("a", 9)] (Association.toList (Association.append "a" 9 duplicates))
  assertEqual "association prepend moves existing key" [("b", 9), ("a", 3)] (Association.toList (Association.prepend "b" 9 duplicates))

  let joined = Association.join (Association.fromList [("a", 1 :: Int), ("b", 2)]) (Association.fromList [("a", 3 :: Int), ("c", 4)])
  assertEqual "association join keeps receiver positions" [("a", 3), ("b", 2), ("c", 4)] (Association.toList joined)
  assertEqual "association insert existing key wins new position" (Just [("b", 2), ("a", 9), ("c", 3)]) (Association.toList <$> Association.insertAt 2 "a" 9 (Association.fromList [("a", 1 :: Int), ("b", 2), ("c", 3)]))
  assertEqual "association delete" [("a", 1), ("c", 3)] (Association.toList (Association.delete "b" (Association.fromList [("a", 1 :: Int), ("b", 2), ("c", 3)])))
  assertEqual "association deleteAt" (Just [("a", 1), ("c", 3)]) (Association.toList <$> Association.deleteAt 1 (Association.fromList [("a", 1 :: Int), ("b", 2), ("c", 3)]))
  assertEqual "association take" (Just [("a", 1), ("b", 2)]) (Association.toList <$> Association.take 2 (Association.fromList [("a", 1 :: Int), ("b", 2), ("c", 3)]))
  assertEqual "association drop" (Just [("b", 2), ("c", 3)]) (Association.toList <$> Association.drop 1 (Association.fromList [("a", 1 :: Int), ("b", 2), ("c", 3)]))
  assertEqual "association reverse" [("c", 3), ("b", 2), ("a", 1)] (Association.toList (Association.reverse (Association.fromList [("a", 1 :: Int), ("b", 2), ("c", 3)])))
  assertEqual "association keySort stable" [("a", 3), ("b", 2), ("c", 1)] (Association.toList (Association.keySort (Association.fromList [("c", 1 :: Int), ("b", 2), ("a", 3)])))
  assertEqual "association sort by value stable" [("c", 1), ("a", 2), ("b", 2)] (Association.toList (Association.sort (Association.fromList [("a", 2 :: Int), ("b", 2), ("c", 1)])))
  assertEqual "association keyTake follows request order" [("c", 3), ("a", 1)] (Association.toList (Association.keyTake ["missing", "c", "a", "c"] (Association.fromList [("a", 1 :: Int), ("b", 2), ("c", 3)])))

testAssociationCustomPolicy :: IO ()
testAssociationCustomPolicy = do
  let casePolicy = HashPolicy (hash . fmap toLower) (\left right -> fmap toLower left == fmap toLower right)
      values = Association.setItem "HELLO" 2 (Association.fromListWith casePolicy [("Hello", 1 :: Int), ("World", 3)])
  assertEqual "association custom lookup" (Just 2) (Association.lookup "hello" values)
  assertEqual "association custom actual key preserved by setItem" (Just "Hello") (Association.actualKey "hello" values)
  assertEqual "association custom setItem keeps stored key" [("Hello", 2), ("World", 3)] (Association.toList values)
  assertEqual "association custom append adopts supplied key" [("World", 3), ("hello", 5)] (Association.toList (Association.append "hello" 5 values))

testAssociationRelabelStress :: IO ()
testAssociationRelabelStress = do
  let seed = Association.fromList [(0 :: Int, 0 :: Int), (999, 999)]
      values = List.foldl' insertMiddle seed [1 :: Int .. 25]
      insertMiddle association value =
        case Association.insertAt 1 value value association of
          Just result -> result
          Nothing -> error "validated relabel insertion failed"
  assertEqual "association relabel count" 27 (Association.count values)
  assertEqual "association relabel lookup" (Just 25) (Association.lookup 25 values)
  assertEqual "association relabel first" (Just (0, 0)) (Association.first values)
  assertEqual "association relabel last" (Just (999, 999)) (Association.last values)
  assertEqual "association relabel middle prefix" [0, 25, 24, 23, 22, 21] (fmap fst (List.take 6 (Association.toList values)))

testAssociationGeneratedHistory :: IO ()
testAssociationGeneratedHistory =
  foldM_ step (Association.empty :: Association.PersistentAssociation Int Int, []) [0 :: Int .. 119]
  where
    step (association, model) command = do
      let key = (command * 37 + 11) `mod` 17
          value = command * 101 - key
          (association', model') =
            case command `mod` 7 of
              0 -> (Association.setItem key value association, modelSet key value model)
              1 -> (Association.append key value association, modelAppend key value model)
              2 -> (Association.prepend key value association, modelPrepend key value model)
              3 ->
                let position = command `mod` (List.length model + 1)
                    inserted = fromMaybe association (Association.insertAt position key value association)
                 in (inserted, modelInsertAt position key value model)
              4 -> (Association.delete key association, modelDelete key model)
              5 ->
                let position = if List.null model then 0 else command `mod` List.length model
                    deleted = fromMaybe association (Association.deleteAt position association)
                 in (deleted, modelDeleteAt position model)
              _ ->
                let start = if List.null model then 0 else command `mod` List.length model
                    lengthValue = (command `div` 3) `mod` (List.length model - start + 1)
                    sliced = fromMaybe association (Association.slice start lengthValue association)
                 in (sliced, List.take lengthValue (List.drop start model))
      assertEqual ("association generated command " ++ show command) model' (Association.toList association')
      pure (association', model')

modelSet :: Eq k => k -> v -> [(k, v)] -> [(k, v)]
modelSet key value [] = [(key, value)]
modelSet key value ((candidate, oldValue) : rest)
  | candidate == key = (candidate, value) : rest
  | otherwise = (candidate, oldValue) : modelSet key value rest

modelAppend :: Eq k => k -> v -> [(k, v)] -> [(k, v)]
modelAppend key value model = modelDelete key model ++ [(key, value)]

modelPrepend :: Eq k => k -> v -> [(k, v)] -> [(k, v)]
modelPrepend key value model = (key, value) : modelDelete key model

modelInsertAt :: Eq k => Int -> k -> v -> [(k, v)] -> [(k, v)]
modelInsertAt position key value model =
  let oldPosition = List.findIndex ((== key) . fst) model
      without = modelDelete key model
      adjusted =
        case oldPosition of
          Just old | old < position -> position - 1
          _ -> position
      (before, after) = List.splitAt adjusted without
   in before ++ [(key, value)] ++ after

modelDelete :: Eq k => k -> [(k, v)] -> [(k, v)]
modelDelete key = filter ((/= key) . fst)

modelDeleteAt :: Int -> [(k, v)] -> [(k, v)]
modelDeleteAt position model =
  let (before, after) = List.splitAt position model
   in case after of
        [] -> model
        _ : rest -> before ++ rest

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")
