module Main (main) where

import Prelude hiding (drop, last, null, reverse, take)

import Control.Concurrent (forkIO, newEmptyMVar, putMVar, takeMVar)
import Control.Exception (SomeException, evaluate, try)
import Control.Monad (foldM, forM_, replicateM)
import Data.Char (toLower)
import qualified Data.List as List
import Data.Maybe (fromMaybe)

import Durable7.Hamt.Hashable (hash)
import Durable7.Hamt.HashMap (HashPolicy(..), defaultPolicy)
import qualified Durable7.Ordered.Cursor as Cursor
import qualified Durable7.Ordered.PersistentOrderedSet as Ordered
import qualified Durable7.Ordered.PersistentOrderedMap as OrderedMap
import qualified Durable7.Ordered.PersistentOrderedMultimap as OrderedMultimap

main :: IO ()
main = do
  testConstructionAndRepresentatives
  testInsertionMovementAndRelabel
  testRemovalRangesAndOrder
  testAlgebraAndRelations
  testDeterministicModel
  testFailureIsolation
  testConcurrentReads
  testOrderedMap
  testOrderedMultimap
  testOrderedCursors
  testOrderedMultimapNonReflexiveValues
  putStrLn "durable7-ordered tests passed"

-- | A @NaN@ payload is one the collection accepts but a content re-lookup can
-- never find again, because it is not equal to itself.  Neither the multimap
-- cursor's delete nor its insert may depend on such a re-lookup: the delete
-- would otherwise report success while changing nothing, and the insert would
-- reach an @error@ from a pure signature that gives callers no failure channel.
testOrderedMultimapNonReflexiveValues :: IO ()
testOrderedMultimapNonReflexiveValues = do
  let nan = 0 / 0 :: Double
      single = OrderedMultimap.fromListWith defaultPolicy nanPolicy [("k", nan)]
      singleCursor = expectJust "non-reflexive cursor" (Cursor.orderedMultimapCursorAt 0 single)
      singleEnd = expectJust "non-reflexive end cursor" (Cursor.orderedMultimapCursorAt 1 single)
  assertEqual "non-reflexive pair count" 1 (OrderedMultimap.size single)
  assertEqual "non-reflexive peek key" (Just "k")
    (fmap fst (Cursor.orderedMultimapCursorPeekNext singleCursor))
  assertEqual "non-reflexive delete next reports failure" Nothing
    (fmap (OrderedMultimap.size . Cursor.orderedMultimapCursorSnapshot)
      (Cursor.orderedMultimapCursorDeleteNext singleCursor))
  assertEqual "non-reflexive delete previous reports failure" Nothing
    (fmap (OrderedMultimap.size . Cursor.orderedMultimapCursorSnapshot)
      (Cursor.orderedMultimapCursorDeletePrevious singleEnd))

  let inserted = Cursor.orderedMultimapCursorInsert "k" nan singleCursor
  assertEqual "non-reflexive insert pair count" 2
    (OrderedMultimap.size (Cursor.orderedMultimapCursorSnapshot inserted))
  assertEqual "non-reflexive insert gap" 2 (Cursor.orderedMultimapCursorPosition inserted)

  let mixed = OrderedMultimap.fromListWith defaultPolicy nanPolicy
        [("a", 1), ("k", nan), ("z", 2)]
      mixedCursor = expectJust "interior cursor" (Cursor.orderedMultimapCursorAt 0 mixed)
      mixedInserted = Cursor.orderedMultimapCursorInsert "k" nan mixedCursor
  assertEqual "interior group insert gap" 3 (Cursor.orderedMultimapCursorPosition mixedInserted)
  assertEqual "interior group insert order" ["a", "k", "k", "z"]
    (map fst (OrderedMultimap.toList (Cursor.orderedMultimapCursorSnapshot mixedInserted)))

  -- A value that is reflexive under the policy still deletes through the same path.
  let deleted = expectJust "reflexive delete" (Cursor.orderedMultimapCursorDeleteNext mixedCursor)
  assertEqual "reflexive delete order" ["k", "z"]
    (map fst (OrderedMultimap.toList (Cursor.orderedMultimapCursorSnapshot deleted)))
  assertEqual "reflexive delete gap" 0 (Cursor.orderedMultimapCursorPosition deleted)

nanPolicy :: HashPolicy Double
nanPolicy = HashPolicy (\value -> if isNaN value then 0 else truncate value) (==)

testOrderedCursors :: IO ()
testOrderedCursors = do
  let setSource = Ordered.fromListWith casePolicy ["Alpha", "beta", "gamma"]
      setCursor = expectJust "ordered-set cursor" (Cursor.orderedSetCursorAt 1 setSource)
      setInserted = Cursor.orderedSetCursorInsert "delta" setCursor
      setDuplicate = Cursor.orderedSetCursorTryInsert "BETA" setInserted
      setDeleted = expectJust "ordered-set cursor delete" (Cursor.orderedSetCursorDeletePrevious setInserted)
      setFound = Cursor.findOrderedSetCursor "BETA" setSource
      setMiss = Cursor.findOrderedSetCursor "missing" setSource
  assertEqual "ordered-set cursor neighbors" (Just "Alpha", Just "beta")
    (Cursor.orderedSetCursorPeekPrevious setCursor, Cursor.orderedSetCursorPeekNext setCursor)
  assertEqual "ordered-set cursor insertion" ["Alpha", "delta", "beta", "gamma"]
    (Ordered.toList (Cursor.orderedSetCursorSnapshot setInserted))
  assertEqual "ordered-set cursor insertion gap" 2 (Cursor.orderedSetCursorPosition setInserted)
  assertBool "ordered-set cursor duplicate flag" (not (Cursor.orderedCursorAdded setDuplicate))
  assertEqual "ordered-set duplicate preserves gap" 2
    (Cursor.orderedSetCursorPosition (Cursor.orderedInsertionCursor setDuplicate))
  assertEqual "ordered-set cursor deletion" (Ordered.toList setSource)
    (Ordered.toList (Cursor.orderedSetCursorSnapshot setDeleted))
  assertBool "ordered-set cursor retains policy"
    (Ordered.contains "ALPHA" (Cursor.orderedSetCursorSnapshot setDeleted))
  assertBool "ordered-set cursor find" (Cursor.orderedCursorFound setFound)
  assertEqual "ordered-set cursor find position" 1
    (Cursor.orderedSetCursorPosition (Cursor.orderedSearchCursor setFound))
  assertEqual "ordered-set cursor miss position" (Ordered.size setSource)
    (Cursor.orderedSetCursorPosition (Cursor.orderedSearchCursor setMiss))
  assertEqual "ordered-set cursor source snapshot" ["Alpha", "beta", "gamma"]
    (Ordered.toList (Cursor.orderedSetCursorSnapshot setCursor))

  let mapSource = OrderedMap.fromList [('a', 1 :: Int), ('b', 2), ('c', 3)]
      mapCursor = expectJust "ordered-map cursor" (Cursor.orderedMapCursorAt 1 mapSource)
      mapInserted = expectJust "ordered-map cursor insert" (Cursor.orderedMapCursorInsert 'x' 9 mapCursor)
      mapUpdated = expectJust "ordered-map cursor set next" (Cursor.orderedMapCursorSetNextValue 20 mapInserted)
      mapDuplicate = Cursor.orderedMapCursorTryInsert 'b' 200 mapUpdated
      mapWithoutPrevious = expectJust "ordered-map delete previous" (Cursor.orderedMapCursorDeletePrevious mapUpdated)
      mapDeleted = expectJust "ordered-map delete next" (Cursor.orderedMapCursorDeleteNext mapWithoutPrevious)
  assertEqual "ordered-map cursor edits" [('a', 1), ('x', 9), ('b', 20), ('c', 3)]
    (OrderedMap.toList (Cursor.orderedMapCursorSnapshot mapUpdated))
  assertBool "ordered-map cursor duplicate flag" (not (Cursor.orderedCursorAdded mapDuplicate))
  assertEqual "ordered-map duplicate focuses key" 2
    (Cursor.orderedMapCursorPosition (Cursor.orderedInsertionCursor mapDuplicate))
  assertEqual "ordered-map cursor deletes" [('a', 1), ('c', 3)]
    (OrderedMap.toList (Cursor.orderedMapCursorSnapshot mapDeleted))
  assertEqual "ordered-map cursor delete gap" 1 (Cursor.orderedMapCursorPosition mapDeleted)
  assertEqual "ordered-map cursor source snapshot" [('a', 1), ('b', 2), ('c', 3)]
    (OrderedMap.toList (Cursor.orderedMapCursorSnapshot mapCursor))

  let multimapSource = OrderedMultimap.fromList [('b', 2 :: Int), ('a', 9), ('b', 1), ('c', 7)]
      pairFound = Cursor.findOrderedMultimapCursor 'b' 1 multimapSource
      multimapAdded = Cursor.orderedMultimapCursorInsert 'b' 3 (Cursor.orderedSearchCursor pairFound)
      multimapDuplicate = Cursor.orderedMultimapCursorTryInsert 'b' 3 multimapAdded
      multimapWithoutPrevious = expectJust "ordered-multimap delete previous"
        (Cursor.orderedMultimapCursorDeletePrevious multimapAdded)
      multimapDeleted = expectJust "ordered-multimap delete next"
        (Cursor.orderedMultimapCursorDeleteNext multimapWithoutPrevious)
      groupFound = Cursor.findOrderedMultimapGroupCursor 'a' multimapSource
      groupMiss = Cursor.findOrderedMultimapGroupCursor 'z' multimapSource
  assertEqual "ordered-multimap grouped source" [('b', 2), ('b', 1), ('a', 9), ('c', 7)]
    (OrderedMultimap.toList multimapSource)
  assertBool "ordered-multimap pair find" (Cursor.orderedCursorFound pairFound)
  assertEqual "ordered-multimap pair position" 1
    (Cursor.orderedMultimapCursorPosition (Cursor.orderedSearchCursor pairFound))
  assertEqual "ordered-multimap grouped insertion gap" 3 (Cursor.orderedMultimapCursorPosition multimapAdded)
  assertBool "ordered-multimap duplicate flag" (not (Cursor.orderedCursorAdded multimapDuplicate))
  assertEqual "ordered-multimap duplicate preserves gap" 3
    (Cursor.orderedMultimapCursorPosition (Cursor.orderedInsertionCursor multimapDuplicate))
  assertEqual "ordered-multimap cursor deletion" [('b', 2), ('b', 1), ('c', 7)]
    (OrderedMultimap.toList (Cursor.orderedMultimapCursorSnapshot multimapDeleted))
  assertEqual "ordered-multimap cursor delete gap" 2 (Cursor.orderedMultimapCursorPosition multimapDeleted)
  assertEqual "ordered-multimap cursor next" (Just ('c', 7))
    (Cursor.orderedMultimapCursorPeekNext multimapDeleted)
  assertBool "ordered-multimap group find" (Cursor.orderedCursorFound groupFound)
  assertEqual "ordered-multimap group position" 2
    (Cursor.orderedMultimapCursorPosition (Cursor.orderedSearchCursor groupFound))
  assertEqual "ordered-multimap group miss position" (OrderedMultimap.size multimapSource)
    (Cursor.orderedMultimapCursorPosition (Cursor.orderedSearchCursor groupMiss))
  assertEqual "ordered-multimap cursor source snapshot" [('b', 2), ('b', 1), ('a', 9), ('c', 7)]
    (OrderedMultimap.toList (Cursor.orderedMultimapCursorSnapshot (Cursor.orderedSearchCursor pairFound)))

testOrderedMultimap :: IO ()
testOrderedMultimap = do
  let values0 = OrderedMultimap.fromListWith casePolicy casePolicy
        [("Alpha", "One"), ("Beta", "Two"), ("ALPHA", "Three"), ("alpha", "ONE")]
      snapshot = values0
      values1 = OrderedMultimap.delete "ALPHA" "one" values0
      values2 = OrderedMultimap.deleteKey "beta" values1
  assertEqual "ordered multimap grouped order"
    [("Alpha", "One"), ("Alpha", "Three"), ("Beta", "Two")]
    (OrderedMultimap.toList values0)
  assertEqual "ordered multimap pair count" 3 (OrderedMultimap.size values0)
  assertEqual "ordered multimap key count" 2 (OrderedMultimap.keyCount values0)
  assertEqual "ordered multimap key representative" (Just "Alpha") (OrderedMultimap.actualKey "alpha" values0)
  assertEqual "ordered multimap value representative" (Just "Three") (OrderedMultimap.actualValue "alpha" "THREE" values0)
  assertBool "ordered multimap removal preserves snapshot" (OrderedMultimap.member "alpha" "one" snapshot)
  assertEqual "ordered multimap group removal" [("Alpha", "Three")] (OrderedMultimap.toList values2)
  assertBool "ordered multimap invariant" (OrderedMultimap.validStructure values2)

testOrderedMap :: IO ()
testOrderedMap = do
  let map0 = OrderedMap.fromList [(1 :: Int, "one"), (2, "two"), (3, "three")]
      map1 = OrderedMap.set 2 "TWO" map0
      map2 = expectJust "ordered map movement" (OrderedMap.moveToFirst 3 map1)
      map3 = OrderedMap.sortBy (\(_, left) (_, right) -> compare right left) map2
      range = expectJust "ordered map range" (OrderedMap.getRange 1 2 map3)
  assertEqual "ordered map replacement keeps order" [1, 2, 3] (map fst (OrderedMap.toList map1))
  assertEqual "ordered map snapshot" (Just "two") (OrderedMap.lookup 2 map0)
  assertEqual "ordered map movement" [3, 1, 2] (map fst (OrderedMap.toList map2))
  assertEqual "ordered map range count" 2 (OrderedMap.size range)
  assertBool "ordered map invariant" (OrderedMap.validStructure range)

casePolicy :: HashPolicy String
casePolicy = HashPolicy
  (hash . map toLower)
  (\left right -> map toLower left == map toLower right)

testConstructionAndRepresentatives :: IO ()
testConstructionAndRepresentatives = do
  let source = Ordered.fromListWith casePolicy ["Alpha", "BETA", "alpha", "Gamma", "beta"]
      duplicate = Ordered.addFirst "ALPHA" source
  assertEqual "first representatives and order" ["Alpha", "BETA", "Gamma"] (Ordered.toList source)
  assertEqual "custom-policy membership" True (Ordered.contains "beta" source)
  assertEqual "stored representative recovery" (Just "Alpha") (Ordered.actualValue "ALPHA" source)
  assertEqual "representative index" 1 (Ordered.indexOf "beta" source)
  assertEqual "absent index" (-1) (Ordered.indexOf "missing" source)
  assertEqual "positional lookup" (Just "BETA") (Ordered.at 1 source)
  assertEqual "endpoints" (Just "Alpha", Just "Gamma") (Ordered.first source, Ordered.last source)
  assertBool "duplicate addition shares membership root" (Ordered.sharesIndexWith source duplicate)
  assertEqual "duplicate addition preserves order" (Ordered.toList source) (Ordered.toList duplicate)
  assertBool "construction satisfies dual-index invariant" (Ordered.validStructure source)

testInsertionMovementAndRelabel :: IO ()
testInsertionMovementAndRelabel = do
  let seed = Ordered.fromList [0 :: Int, 999]
      stressed = List.foldl' insertMiddle seed [1 :: Int .. 25]
      insertMiddle setValue value =
        fromMaybe (error "validated midpoint insertion failed") (Ordered.insertAt 1 value setValue)
  assertEqual "midpoint relabel count" 27 (Ordered.size stressed)
  assertEqual "midpoint relabel prefix" [0, 25, 24, 23, 22, 21] (List.take 6 (Ordered.toList stressed))
  assertBool "relabel result is valid" (Ordered.validStructure stressed)

  let base = Ordered.fromList [1 :: Int, 2, 3, 4]
      movedFirst = expectJust "move first" (Ordered.moveToFirst 3 base)
      movedLast = expectJust "move last" (Ordered.moveToLast 1 movedFirst)
      movedMiddle = expectJust "move final position" (Ordered.moveTo 1 4 movedLast)
      alreadyThere = expectJust "move no-op" (Ordered.moveTo 1 4 movedMiddle)
  assertEqual "explicit movement order" [3, 4, 2, 1] (Ordered.toList movedMiddle)
  assertBool "movement retains representative" (Ordered.actualValue 4 movedMiddle == Just 4)
  assertBool "already-positioned move shares index" (Ordered.sharesIndexWith movedMiddle alreadyThere)
  assertEqual "absent movement" Nothing (Ordered.toList <$> Ordered.moveToFirst 99 base)
  assertEqual "invalid insert" Nothing (Ordered.toList <$> Ordered.insertAt 5 8 base)
  assertEqual "invalid move validates position" Nothing (Ordered.toList <$> Ordered.moveTo 4 1 base)

testRemovalRangesAndOrder :: IO ()
testRemovalRangesAndOrder = do
  let source = Ordered.fromList [0 :: Int .. 7]
      removed = Ordered.delete 3 source
      missing = Ordered.delete 99 source
      byIndex = expectJust "delete at" (Ordered.deleteAt 2 source)
      middle = expectJust "range" (Ordered.getRange 2 4 source)
  assertEqual "remove by class" [0, 1, 2, 4, 5, 6, 7] (Ordered.toList removed)
  assertBool "absent delete shares index" (Ordered.sharesIndexWith source missing)
  assertEqual "remove by position" [0, 1, 3, 4, 5, 6, 7] (Ordered.toList byIndex)
  assertEqual "range contents" [2, 3, 4, 5] (Ordered.toList middle)
  assertEqual "take contents" (Just [0, 1, 2]) (Ordered.toList <$> Ordered.take 3 source)
  assertEqual "drop contents" (Just [5, 6, 7]) (Ordered.toList <$> Ordered.drop 5 source)
  assertEqual "invalid range" Nothing (Ordered.toList <$> Ordered.getRange 7 2 source)
  assertEqual "reverse contents" [7, 6 .. 0] (Ordered.toList (Ordered.reverse source))

  let unsorted = Ordered.fromList [3 :: Int, 4, 1, 2]
      parity left right = compare (left `mod` 2) (right `mod` 2)
      sorted = Ordered.sortBy parity unsorted
      sortedAgain = Ordered.sortBy parity sorted
  assertEqual "stable one-shot sort" [4, 2, 3, 1] (Ordered.toList sorted)
  assertBool "unchanged stable sort shares index" (Ordered.sharesIndexWith sorted sortedAgain)
  assertBool "derived structures remain valid"
    (all Ordered.validStructure [removed, byIndex, middle, sorted])

testAlgebraAndRelations :: IO ()
testAlgebraAndRelations = do
  let receiver = Ordered.fromListWith casePolicy ["Alpha", "beta", "delta"]
      argument = ["ALPHA", "Gamma", "gamma", "BETA"]
      unionValue = Ordered.union argument receiver
      intersectionValue = Ordered.intersection argument receiver
      differenceValue = Ordered.difference argument receiver
      symmetricValue = Ordered.symmetricDifference argument receiver
  assertEqual "receiver-policy union" ["Alpha", "beta", "delta", "Gamma"] (Ordered.toList unionValue)
  assertEqual "receiver-policy intersection" ["Alpha", "beta"] (Ordered.toList intersectionValue)
  assertEqual "receiver-policy difference" ["delta"] (Ordered.toList differenceValue)
  assertEqual "receiver-policy symmetric difference" ["delta", "Gamma"] (Ordered.toList symmetricValue)
  assertBool "subset relation" (Ordered.isSubsetOf ["ALPHA", "BETA", "DELTA", "extra"] receiver)
  assertBool "proper subset relation" (Ordered.isProperSubsetOf ["ALPHA", "BETA", "DELTA", "extra"] receiver)
  assertBool "superset relation" (Ordered.isSupersetOf ["ALPHA", "delta"] receiver)
  assertBool "proper superset relation" (Ordered.isProperSupersetOf ["ALPHA", "delta"] receiver)
  assertBool "overlap relation" (Ordered.overlaps ["none", "BETA"] receiver)
  assertBool "set equality ignores order" (Ordered.setEquals ["DELTA", "alpha", "BETA"] receiver)
  assertBool "algebra outputs remain valid"
    (all Ordered.validStructure [unionValue, intersectionValue, differenceValue, symmetricValue])

testDeterministicModel :: IO ()
testDeterministicModel = do
  _ <- foldM step (Ordered.empty :: Ordered.PersistentOrderedSet Int, []) [0 :: Int .. 999]
  pure ()
  where
    step (setValue, model) command = do
      let value = (command * 37 + 11) `mod` 47
          count = length model
          position = if count == 0 then 0 else (command * 13 + 5) `mod` count
          insertionPosition = (command * 17 + 3) `mod` (count + 1)
          (nextSet, nextModel) =
            case command `mod` 8 of
              0 -> (Ordered.add value setValue, modelAdd value model)
              1 -> (Ordered.addFirst value setValue, modelAddFirst value model)
              2 ->
                ( expectJust "model insert" (Ordered.insertAt insertionPosition value setValue)
                , modelInsert insertionPosition value model
                )
              3 ->
                case List.elemIndex value model of
                  Nothing -> (setValue, model)
                  Just _ ->
                    ( expectJust "model move" (Ordered.moveTo position value setValue)
                    , modelMove position value model
                    )
              4 -> (Ordered.delete value setValue, List.delete value model)
              5
                | count == 0 -> (setValue, model)
                | otherwise ->
                    ( expectJust "model deleteAt" (Ordered.deleteAt position setValue)
                    , deleteModelAt position model
                    )
              6 -> (Ordered.reverse setValue, List.reverse model)
              _
                | count == 0 -> (setValue, model)
                | otherwise ->
                    let start = command `mod` (count + 1)
                        kept = (command `div` 3) `mod` (count - start + 1)
                     in ( expectJust "model range" (Ordered.getRange start kept setValue)
                        , List.take kept (List.drop start model)
                        )
      assertEqual ("model contents at command " ++ show command) nextModel (Ordered.toList nextSet)
      assertBool ("model invariant at command " ++ show command) (Ordered.validStructure nextSet)
      pure (nextSet, nextModel)

testFailureIsolation :: IO ()
testFailureIsolation = do
  let failingPolicy = HashPolicy
        (\value -> if value == (99 :: Int) then error "intentional hash failure" else hash value)
        (==)
      source = Ordered.fromListWith failingPolicy [1 :: Int, 2, 3]
  failure <- try (evaluate (Ordered.add 99 source)) ::
    IO (Either SomeException (Ordered.PersistentOrderedSet Int))
  assertBool "policy failure exposes no successor" (isLeft failure)
  assertEqual "policy failure leaves source usable" [1, 2, 3] (Ordered.toList source)
  assertBool "source remains valid after failure" (Ordered.validStructure source)

testConcurrentReads :: IO ()
testConcurrentReads = do
  let source = Ordered.fromList [0 :: Int .. 255]
      action = forM_ [0 :: Int .. 127] $ \iteration -> do
        assertEqual "concurrent count" 256 (Ordered.size source)
        assertEqual "concurrent lookup" (Just iteration) (Ordered.at iteration source)
        assertBool "concurrent membership" (Ordered.contains (255 - iteration) source)
        assertBool "concurrent invariant" (Ordered.validStructure source)
  boxes <- replicateM 4 newEmptyMVar
  forM_ boxes $ \box -> do
    _ <- forkIO $ do
      result <- try action :: IO (Either SomeException ())
      putMVar box result
    pure ()
  results <- mapM takeMVar boxes
  forM_ results $ \result ->
    case result of
      Right () -> pure ()
      Left problem -> fail ("concurrent reader failed: " ++ show problem)

modelAdd :: Eq a => a -> [a] -> [a]
modelAdd value model
  | value `elem` model = model
  | otherwise = model ++ [value]

modelAddFirst :: Eq a => a -> [a] -> [a]
modelAddFirst value model
  | value `elem` model = model
  | otherwise = value : model

modelInsert :: Eq a => Int -> a -> [a] -> [a]
modelInsert position value model
  | value `elem` model = model
  | otherwise = insertModelAt position value model

modelMove :: Eq a => Int -> a -> [a] -> [a]
modelMove position value model = insertModelAt position value (List.delete value model)

insertModelAt :: Int -> a -> [a] -> [a]
insertModelAt position value model =
  let (before, after) = List.splitAt position model
   in before ++ (value : after)

deleteModelAt :: Int -> [a] -> [a]
deleteModelAt position model =
  let (before, after) = List.splitAt position model
   in case after of
        [] -> model
        _ : rest -> before ++ rest

expectJust :: String -> Maybe a -> a
expectJust _ (Just value) = value
expectJust label Nothing = error (label ++ ": expected Just")

isLeft :: Either a b -> Bool
isLeft (Left _) = True
isLeft _ = False

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")
