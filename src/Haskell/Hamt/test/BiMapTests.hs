-- | Tests for the persistent bidirectional map, including its conflict rules.
module BiMapTests (runBiMapTests) where

import Control.Concurrent (forkIO, newEmptyMVar, putMVar, takeMVar)
import Control.Exception (SomeException, evaluate, try)
import Control.Monad (replicateM)
import Data.Char (toLower)
import Data.List (sort)

import qualified Durable7.Hamt.BiMap as BiMap
import Durable7.Hamt.HashMap (HashPolicy(..))
import Durable7.Hamt.Hashable (hash)

-- | Runs the bidirectional map test cases.
runBiMapTests :: IO ()
runBiMapTests = do
  testStrictConflictsAndReplacement
  testIndependentPoliciesAndRepresentatives
  testRemovalInverseNullableAndClear
  testDeterministicModel
  testFailureAtomicity
  testConcurrentReaders

testStrictConflictsAndReplacement :: IO ()
testStrictConflictsAndReplacement = do
  source <- expectRight "strict source" (BiMap.add (1 :: Int) "one" BiMap.empty)
  let (keySame, keyConflict) = BiMap.tryAdd 1 "uno" source
      (valueSame, valueConflict) = BiMap.tryAdd 2 "one" source
      (pairSame, pairConflict) = BiMap.tryAdd 1 "one" source
  assertEqual "key conflict" (Just BiMap.KeyConflict) keyConflict
  assertEqual "value conflict" (Just BiMap.ValueConflict) valueConflict
  assertEqual "complete pair is strict key conflict" (Just BiMap.KeyConflict) pairConflict
  assertBool "key conflict keeps roots" (BiMap.sharesRootsWith source keySame)
  assertBool "value conflict keeps roots" (BiMap.sharesRootsWith source valueSame)
  assertBool "pair conflict keeps roots" (BiMap.sharesRootsWith source pairSame)

  two <- expectRight "second pair" (BiMap.add 2 "two" source)
  case BiMap.set 1 "two" two of
    Left BiMap.ValueConflict -> pure ()
    _ -> error "set cannot displace: expected value conflict"
  replaced <- expectRight "free replacement" (BiMap.set 1 "uno" two)
  assertEqual "replacement forward" (Just "uno") (BiMap.lookup 1 replaced)
  assertEqual "replacement inverse" (Just 1) (BiMap.lookupKey "uno" replaced)
  assertEqual "old value removed" Nothing (BiMap.lookupKey "one" replaced)
  assertEqual "source retained" (Just "one") (BiMap.lookup 1 two)
  assertBool "replacement valid" (BiMap.validStructure replaced)

testIndependentPoliciesAndRepresentatives :: IO ()
testIndependentPoliciesAndRepresentatives = do
  let logical (name, _) = map toLower name
      policy = HashPolicy (hash . logical) (\left right -> logical left == logical right)
      storedKey = ("Alpha", 1 :: Int)
      storedValue = ("One", 1 :: Int)
      lookupKey = ("ALPHA", 99 :: Int)
      lookupValue = ("ONE", 99 :: Int)
  source <- expectRight "representative source"
    (BiMap.add storedKey storedValue (BiMap.emptyWith policy policy))
  assertEqual "stored key representative" (Just storedKey) (BiMap.actualKey lookupKey source)
  assertEqual "stored value representative" (Just storedValue) (BiMap.actualValue lookupValue source)

  equivalent <- expectRight "equivalent set" (BiMap.set lookupKey lookupValue source)
  assertBool "equivalent set shares roots" (BiMap.sharesRootsWith source equivalent)
  assertEqual "equivalent value retained" (Just storedValue) (BiMap.lookup lookupKey equivalent)

  replaced <- expectRight "distinct representative replacement"
    (BiMap.set lookupKey ("Two", 2 :: Int) source)
  assertEqual "replacement retains key representative"
    (Just storedKey) (BiMap.lookupKey ("TWO", 0 :: Int) replaced)
  assertBool "policy-driven map valid" (BiMap.validStructure replaced)

testRemovalInverseNullableAndClear :: IO ()
testRemovalInverseNullableAndClear = do
  source <- expectRight "nullable source"
    (BiMap.add (1 :: Int) (Nothing :: Maybe Int) BiMap.empty)
  assertEqual "present Nothing lookup" (Just Nothing) (BiMap.lookup 1 source)
  assertEqual "present Nothing inverse" (Just 1) (BiMap.lookupKey Nothing source)
  let inverted = BiMap.inverse source
  assertEqual "inverse lookup" (Just 1) (BiMap.lookup Nothing inverted)
  assertBool "double inverse shares roots"
    (BiMap.sharesRootsWith source (BiMap.inverse inverted))
  let (removed, opposite) = BiMap.tryRemoveKey 1 source
  assertEqual "removed Nothing is presence-safe" (Just Nothing) opposite
  assertBool "both sides removed" (BiMap.null removed && BiMap.validStructure removed)
  let (miss, missing) = BiMap.tryRemoveValue (Just 99) source
  assertEqual "value miss" Nothing missing
  assertBool "value miss shares roots" (BiMap.sharesRootsWith source miss)
  let cleared = BiMap.clear source
  assertBool "clear empties" (BiMap.null cleared)
  assertBool "empty clear shares roots" (BiMap.sharesRootsWith cleared (BiMap.clear cleared))

testDeterministicModel :: IO ()
testDeterministicModel = go 0 0xB1A4D00D BiMap.empty [] [] []
  where
    go :: Int -> Integer -> BiMap.BiMap Int Int -> [(Int, Int)] -> [(Int, Int)]
      -> [BiMap.BiMap Int Int] -> IO ()
    go step state mapValue forward backward snapshots
      | step == 2000 = mapM_ (assertBool "retained model snapshot valid" . BiMap.validStructure) snapshots
      | otherwise =
          let state1 = nextState state
              operation = state1 `mod` 4
              state2 = nextState state1
              key = fromIntegral (state2 `mod` 32)
              state3 = nextState state2
              value = 100 + fromIntegral (state3 `mod` 32)
              keyHit = modelLookup key forward
              valueHit = modelLookup value backward
              (nextMap, nextForward, nextBackward) =
                case operation of
                  0 ->
                    let (candidate, conflict) = BiMap.tryAdd key value mapValue
                     in if keyHit == Nothing && valueHit == Nothing
                          then (candidate, modelSet key value forward, modelSet value key backward)
                          else
                            let expected = if keyHit /= Nothing then BiMap.KeyConflict else BiMap.ValueConflict
                             in if conflict == Just expected && BiMap.sharesRootsWith candidate mapValue
                                  then (mapValue, forward, backward)
                                  else error "bimap model add conflict disagreed"
                  1 ->
                    case valueHit of
                      Just owner | owner /= key ->
                        case BiMap.set key value mapValue of
                          Left BiMap.ValueConflict -> (mapValue, forward, backward)
                          _ -> error "bimap model set should conflict"
                      _ ->
                        case BiMap.set key value mapValue of
                          Left _ -> error "bimap model set unexpectedly conflicted"
                          Right candidate ->
                            let backwardWithoutOld = maybe backward (`modelDelete` backward) keyHit
                             in (candidate, modelSet key value forward, modelSet value key backwardWithoutOld)
                  2 ->
                    let (candidate, removedValue) = BiMap.tryRemoveKey key mapValue
                     in if removedValue == keyHit
                          then (candidate, modelDelete key forward, maybe backward (`modelDelete` backward) keyHit)
                          else error "bimap model key removal disagreed"
                  _ ->
                    let (candidate, removedKey) = BiMap.tryRemoveValue value mapValue
                     in if removedKey == valueHit
                          then (candidate, maybe forward (`modelDelete` forward) valueHit, modelDelete value backward)
                          else error "bimap model value removal disagreed"
              nextSnapshots = if step `mod` 127 == 0 then nextMap : snapshots else snapshots
           in if validateModel nextMap nextForward
                then go (step + 1) state3 nextMap nextForward nextBackward nextSnapshots
                else error "bimap model contents disagreed"

    nextState :: Integer -> Integer
    nextState value = value * 6364136223846793005 + 1

validateModel :: BiMap.BiMap Int Int -> [(Int, Int)] -> Bool
validateModel mapValue expected =
  BiMap.validStructure mapValue
    && BiMap.size mapValue == length expected
    && sort (BiMap.toList mapValue) == sort expected
    && all (\(key, value) -> BiMap.lookupKey value mapValue == Just key) expected

modelLookup :: Eq key => key -> [(key, value)] -> Maybe value
modelLookup _ [] = Nothing
modelLookup key ((storedKey, value) : rest)
  | key == storedKey = Just value
  | otherwise = modelLookup key rest

modelSet :: Eq key => key -> value -> [(key, value)] -> [(key, value)]
modelSet key value entries = (key, value) : modelDelete key entries

modelDelete :: Eq key => key -> [(key, value)] -> [(key, value)]
modelDelete key = filter ((/= key) . fst)

testFailureAtomicity :: IO ()
testFailureAtomicity = do
  let failingValuePolicy = HashPolicy
        (\value -> if value == (20 :: Int) then error "injected value hash failure" else value)
        (==)
      sourceResult = BiMap.add (1 :: Int) 10 (BiMap.emptyWith (HashPolicy id (==)) failingValuePolicy)
  source <- expectRight "failure source" sourceResult
  failed <- try (evaluate (either (const (-1)) BiMap.size (BiMap.add 2 20 source))) :: IO (Either SomeException Int)
  case failed of
    Left _ -> pure ()
    Right _ -> error "expected bimap hash failure"
  assertEqual "failure leaves source" (Just 10) (BiMap.lookup 1 source)
  assertBool "failure leaves source valid" (BiMap.validStructure source)

testConcurrentReaders :: IO ()
testConcurrentReaders = do
  source <- expectRight "concurrent source"
    (BiMap.fromList [(value, value + 10000) | value <- [0 :: Int .. 255]])
  completions <- replicateM 4 newEmptyMVar
  mapM_ (\completion -> do
    _ <- forkIO (putMVar completion (all (readerCheck source) [0 :: Int .. 255]))
    pure ()) completions
  results <- mapM takeMVar completions
  assertBool "concurrent readers" (and results)
  where
    readerCheck source value =
      BiMap.lookup value source == Just (value + 10000)
        && BiMap.lookupKey (value + 10000) source == Just value

expectRight :: Show errorValue => String -> Either errorValue value -> IO value
expectRight _ (Right value) = pure value
expectRight label (Left errorValue) = error (label ++ ": " ++ show errorValue)

assertBool :: String -> Bool -> IO ()
assertBool _ True = pure ()
assertBool label False = error (label ++ ": assertion failed")

assertEqual :: (Eq value, Show value) => String -> value -> value -> IO ()
assertEqual _ expected actual | expected == actual = pure ()
assertEqual label expected actual =
  error (label ++ ": expected " ++ show expected ++ ", actual " ++ show actual)
