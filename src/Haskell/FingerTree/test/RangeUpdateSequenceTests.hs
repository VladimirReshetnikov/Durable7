{-# LANGUAGE BangPatterns #-}

module RangeUpdateSequenceTests (run) where

import Prelude hiding (null, sequence, splitAt)

import Control.Exception (ErrorCall, displayException, evaluate, try)
import Control.Monad (forM_)
import qualified Data.List as List

import qualified Durable7.FingerTree.RangeUpdateSequence as Range

data AffineTag = AffineTag
  { identityWitness :: !Bool
  , assignment :: !(Maybe Integer)
  , addition :: !Integer
  }
  deriving (Eq, Show)

addTag :: Integer -> AffineTag
addTag amount = AffineTag False Nothing amount

assignTag :: Integer -> AffineTag
assignTag value = AffineTag False (Just value) 0

identityVariant :: AffineTag
identityVariant = AffineTag True Nothing 0

affineAlgebra :: Range.RangeUpdateAlgebra Integer Integer AffineTag
affineAlgebra = Range.RangeUpdateAlgebra
  { Range.algebraEmptyMeasure = 0
  , Range.algebraCombine = (+)
  , Range.algebraMeasureElement = id
  , Range.algebraIdentityTag = AffineTag False Nothing 0
  , Range.algebraIsIdentity = \tag -> assignment tag == Nothing && addition tag == 0
  , Range.algebraCompose = composeAffine
  , Range.algebraApplyElement = applyAffineElement
  , Range.algebraApplyMeasure = applyAffineMeasure
  }

composeAffine :: AffineTag -> AffineTag -> AffineTag
composeAffine newer older
  | Range.algebraIsIdentity affineAlgebra newer = older
  | Range.algebraIsIdentity affineAlgebra older = newer
  | Just _ <- assignment newer = newer
  | Just oldValue <- assignment older =
      AffineTag False (Just (oldValue + addition older + addition newer)) 0
  | otherwise = addTag (addition older + addition newer)

applyAffineElement :: AffineTag -> Integer -> Integer
applyAffineElement tag value = maybe value id (assignment tag) + addition tag

applyAffineMeasure :: AffineTag -> Integer -> Int -> Integer
applyAffineMeasure _ _ 0 = 0
applyAffineMeasure tag value amount =
  case assignment tag of
    Just assigned -> (assigned + addition tag) * toInteger amount
    Nothing -> value + addition tag * toInteger amount

run :: IO ()
run = do
  testAlgebraLaws
  testSurfaceAndPersistence
  testOrderedMeasure
  testGeneratedModel
  testSharing
  testValidationPrecedence
  testLengthOverflow

testAlgebraLaws :: IO ()
testAlgebraLaws = do
  let identity = Range.algebraIdentityTag affineAlgebra
      tags =
        [ identity
        , identityVariant
        , addTag 5
        , addTag (-3)
        , assignTag 7
        , AffineTag False (Just 4) 9
        ]
      values = [-5, 0, 11]
  assertBool "identity tag recognized" (Range.algebraIsIdentity affineAlgebra identity)
  assertBool "value-distinct identity recognized" (Range.algebraIsIdentity affineAlgebra identityVariant)
  forM_ tags $ \tag -> do
    forM_ values $ \value -> do
      assertEqual "left tag identity"
        (applyAffineElement tag value)
        (applyAffineElement (composeAffine identity tag) value)
      assertEqual "right tag identity"
        (applyAffineElement tag value)
        (applyAffineElement (composeAffine tag identityVariant) value)
    forM_ tags $ \newer ->
      forM_ values $ \value ->
        assertEqual "element action composition"
          (applyAffineElement newer (applyAffineElement tag value))
          (applyAffineElement (composeAffine newer tag) value)
  forM_ tags $ \oldest ->
    forM_ tags $ \middle ->
      forM_ tags $ \newest ->
        forM_ values $ \value ->
          assertEqual "tag associativity by action"
            (applyAffineElement
              (composeAffine newest (composeAffine middle oldest)) value)
            (applyAffineElement
              (composeAffine (composeAffine newest middle) oldest) value)
  assertEqual "assignment after addition wins"
    7
    (applyAffineElement (composeAffine (assignTag 7) (addTag 10)) 3)
  assertEqual "addition after assignment accumulates"
    17
    (applyAffineElement (composeAffine (addTag 10) (assignTag 7)) 3)

testSurfaceAndPersistence :: IO ()
testSurfaceAndPersistence = do
  let original = Range.fromListWith affineAlgebra [1 .. 8]
  assertEqual "base count" 8 (Range.count original)
  assertEqual "base measure" 36 (Range.measure original)
  assertEqual "base index" (Just 4) (Range.index 3 original)
  assertEqual "negative index" Nothing (Range.index (-1) original)
  added <- expectJust "range addition" (Range.applyRange 2 4 (addTag 10) original)
  assertSequence "range addition contents" [1, 2, 13, 14, 15, 16, 7, 8] added
  assigned <- expectJust "range assignment" (Range.applyRange 3 3 (assignTag 7) added)
  assertSequence "range assignment contents" [1, 2, 13, 7, 7, 7, 7, 8] assigned
  assertEqual "ordered range measure" (Just 34) (Range.measureRange 2 4 assigned)
  assertEqual "empty range measure" (Just 0) (Range.measureRange 8 0 assigned)
  assertEqual "original snapshot retained" [1 .. 8] (Range.toList original)

  fullyTagged <- expectJust "whole update" (Range.applyRange 0 8 (addTag 5) original)
  replaced <- expectJust "replacement after lazy update" (Range.setAt 3 100 fullyTagged)
  assertSequence "replacement is not retroactively tagged"
    [6, 7, 8, 100, 10, 11, 12, 13]
    replaced
  inserted <- expectJust "insertion after lazy update" (Range.insertAt 2 50 fullyTagged)
  assertSequence "inserted representative is not retroactively tagged"
    [6, 7, 50, 8, 9, 10, 11, 12, 13]
    inserted
  deleted <- expectJust "delete after lazy update" (Range.deleteAt 1 fullyTagged)
  assertSequence "delete after lazy update contents" [6, 8, 9, 10, 11, 12, 13] deleted

  (left, right) <- expectJust "interior split" (Range.splitAt 3 assigned)
  assertSequence "split left" [1, 2, 13] left
  assertSequence "split right" [7, 7, 7, 7, 8] right
  assertSequence "split append round trip" (Range.toList assigned) (Range.append left right)
  middle <- expectJust "slice" (Range.slice 2 4 assigned)
  assertSequence "slice contents" [13, 7, 7, 7] middle

  assertEqual "invalid insert" NothingResult (tagMaybe (Range.insertAt 9 0 original))
  assertEqual "invalid set" NothingResult (tagMaybe (Range.setAt 8 0 original))
  assertEqual "invalid delete" NothingResult (tagMaybe (Range.deleteAt (-1) original))
  assertEqual "invalid split" NothingResult (tagMaybe (Range.splitAt 9 original))
  assertEqual "invalid slice" NothingResult (tagMaybe (Range.slice 7 2 original))
  assertEqual "overflow-safe range rejection" NothingResult
    (tagMaybe (Range.applyRange 1 maxBound (addTag 1) original))

testOrderedMeasure :: IO ()
testOrderedMeasure = do
  let traceAlgebra = Range.RangeUpdateAlgebra
        { Range.algebraEmptyMeasure = Trace []
        , Range.algebraCombine = \(Trace left) (Trace right) -> Trace (left ++ right)
        , Range.algebraMeasureElement = \value -> Trace [value]
        , Range.algebraIdentityTag = 0 :: Integer
        , Range.algebraIsIdentity = (== 0)
        , Range.algebraCompose = (+)
        , Range.algebraApplyElement = (+)
        , Range.algebraApplyMeasure =
            \tag (Trace values) _ -> Trace (map (+ tag) values)
        }
      original = Range.fromListWith traceAlgebra [1, 2, 3, 4, 5]
  changed <- expectJust "trace range update" (Range.applyRange 1 3 10 original)
  assertEqual "noncommutative whole measure"
    (Trace [1, 12, 13, 14, 5])
    (Range.measure changed)
  assertEqual "noncommutative range measure"
    (Just (Trace [12, 13, 14]))
    (Range.measureRange 1 3 changed)

testGeneratedModel :: IO ()
testGeneratedModel = do
  let initialValues = [0 .. 15]
      initial = Range.fromListWith affineAlgebra initialValues
  final <- walk 0 240 initial initialValues
  assertSequence "generated original snapshot retained" initialValues initial
  assertBool "generated history changed" (Range.toList final /= initialValues)
  where
    walk step limit sequence model
      | step == limit = pure sequence
      | otherwise = do
          let lengthValue = length model
              seed = step * 1103515245 + 12345
              boundary = if lengthValue == 0 then 0 else seed `mod` (lengthValue + 1)
              position = if lengthValue == 0 then 0 else seed `mod` lengthValue
              rangeStart = if lengthValue == 0 then 0 else (seed `div` 7) `mod` (lengthValue + 1)
              rangeLength =
                if lengthValue == 0
                  then 0
                  else (seed `div` 17) `mod` (lengthValue - rangeStart + 1)
          (next, nextModel) <-
            case step `mod` 6 of
              0 -> do
                let value = toInteger (step * 3 - 20)
                updated <- expectJust "model insert" (Range.insertAt boundary value sequence)
                pure (updated, insertList boundary value model)
              1 | lengthValue > 1 -> do
                updated <- expectJust "model delete" (Range.deleteAt position sequence)
                pure (updated, deleteList position model)
              2 | lengthValue > 0 -> do
                let value = toInteger (1000 + step)
                updated <- expectJust "model set" (Range.setAt position value sequence)
                pure (updated, replaceList position value model)
              3 -> do
                let tag = addTag (toInteger (step `mod` 11 - 5))
                updated <- expectJust "model add range"
                  (Range.applyRange rangeStart rangeLength tag sequence)
                pure
                  ( updated
                  , modifyRange rangeStart rangeLength (applyAffineElement tag) model
                  )
              4 -> do
                let tag = assignTag (toInteger (step `mod` 19 - 9))
                updated <- expectJust "model assign range"
                  (Range.applyRange rangeStart rangeLength tag sequence)
                pure
                  ( updated
                  , modifyRange rangeStart rangeLength (applyAffineElement tag) model
                  )
              _ -> do
                assertEqual "model range query"
                  (Just (sum (take rangeLength (drop rangeStart model))))
                  (Range.measureRange rangeStart rangeLength sequence)
                pure (sequence, model)
          assertSequence ("generated step " ++ show step) nextModel next
          walk (step + 1) limit next nextModel

testSharing :: IO ()
testSharing = do
  let sequence = Range.fromListWith affineAlgebra [1 .. 32]
      empty = Range.emptyWith affineAlgebra
  identityResult <- expectJust "identity update" (Range.applyRange 3 10 identityVariant sequence)
  emptyResult <- expectJust "empty update" (Range.applyRange 7 0 (addTag 99) sequence)
  (_, splitRight) <- expectJust "zero split" (Range.splitAt 0 sequence)
  assertShares "identity update retains root" sequence identityResult
  assertShares "empty update retains root" sequence emptyResult
  assertShares "endpoint split retains root" sequence splitRight
  assertShares "right empty append retains root" sequence (Range.append sequence empty)
  assertShares "left empty append retains root" sequence (Range.append empty sequence)
  full <- expectJust "sharing full update" (Range.applyRange 0 32 (addTag 1) sequence)
  shares <- Range.sharesRootWith sequence full
  assertBool "nonidentity full update replaces root" (not shares)
  case Range.validateStructure full of
    Just statistics ->
      assertBool "whole update leaves a pending root tag"
        (Range.validationPendingTagCount statistics > 0)
    Nothing -> fail "whole update failed structural validation"

testValidationPrecedence :: IO ()
testValidationPrecedence = do
  let hostile = affineAlgebra
        { Range.algebraIsIdentity = \_ -> error "identity predicate should not run" }
      sequence = Range.fromListWith hostile [1, 2, 3]
  emptyUpdate <- expectJust "hostile empty update"
    (Range.applyRange 1 0 (error "empty tag forced") sequence)
  assertShares "empty update bypasses tag policy" sequence emptyUpdate
  assertEqual "invalid update validates before identity"
    NothingResult
    (tagMaybe (Range.applyRange 4 0 (error "invalid tag forced") sequence))

testLengthOverflow :: IO ()
testLengthOverflow = do
  let seed = Range.singletonWith affineAlgebra 7
      grow sequence
        | Range.count sequence > maxBound `div` 2 = sequence
        | otherwise = grow (Range.append sequence sequence)
      power = grow seed
  almostPower <- expectJust "maximum-count delete" (Range.deleteAt 0 power)
  let maximumSequence = Range.append power almostPower
  assertEqual "maximum cached count" maxBound (Range.count maximumSequence)
  assertEqual "maximum front" (Just 7) (Range.index 0 maximumSequence)
  assertEqual "maximum back" (Just 7) (Range.index (maxBound - 1) maximumSequence)
  insertResult <- try (evaluate (tagMaybe (Range.insertAt 0 8 maximumSequence)))
  appendResult <- try (evaluate (Range.count (Range.append maximumSequence seed)))
  assertErrorPrefix "overflowing insertion" insertResult
  assertErrorPrefix "overflowing append" appendResult
  assertEqual "maximum snapshot remains usable" maxBound (Range.count maximumSequence)

data Trace = Trace [Integer]
  deriving (Eq, Show)

data MaybeResult = NothingResult | JustResult
  deriving (Eq, Show)

tagMaybe :: Maybe a -> MaybeResult
tagMaybe Nothing = NothingResult
tagMaybe (Just _) = JustResult

assertSequence :: String
               -> [Integer]
               -> Range.RangeUpdateSequence Integer Integer AffineTag
               -> IO ()
assertSequence label expected sequence = do
  assertEqual (label ++ " contents") expected (Range.toList sequence)
  assertEqual (label ++ " count") (length expected) (Range.count sequence)
  assertEqual (label ++ " measure") (sum expected) (Range.measure sequence)
  case Range.validateStructure sequence of
    Nothing -> fail (label ++ ": structural validation failed")
    Just statistics -> do
      assertEqual (label ++ " validated count") (length expected)
        (Range.validationCount statistics)
      assertBool (label ++ " logarithmic AVL height")
        (Range.validationHeight statistics <= avlHeightCeiling (length expected))

avlHeightCeiling :: Int -> Int
avlHeightCeiling lengthValue
  | lengthValue <= 0 = 0
  | otherwise = 2 * integerLog2 (lengthValue + 1) + 1

integerLog2 :: Int -> Int
integerLog2 value = go 0 value
  where
    go !result current
      | current <= 1 = result
      | otherwise = go (result + 1) (current `div` 2)

insertList :: Int -> a -> [a] -> [a]
insertList position value values =
  let (left, right) = List.splitAt position values
  in left ++ value : right

deleteList :: Int -> [a] -> [a]
deleteList position values =
  let (left, right) = List.splitAt position values
  in left ++ drop 1 right

replaceList :: Int -> a -> [a] -> [a]
replaceList position value values =
  let (left, right) = List.splitAt position values
  in left ++ value : drop 1 right

modifyRange :: Int -> Int -> (a -> a) -> [a] -> [a]
modifyRange position amount operation values =
  let (left, tailValues) = List.splitAt position values
      (middle, right) = List.splitAt amount tailValues
  in left ++ map operation middle ++ right

assertShares :: String
             -> Range.RangeUpdateSequence element measureValue tag
             -> Range.RangeUpdateSequence element measureValue tag
             -> IO ()
assertShares label left right = do
  shares <- Range.sharesRootWith left right
  assertBool label shares

assertErrorPrefix :: String -> Either ErrorCall a -> IO ()
assertErrorPrefix label result =
  case result of
    Left exception ->
      assertBool (label ++ ": wrong exception")
        ("Durable7.FingerTree.RangeUpdateSequence: length overflow"
          `List.isPrefixOf` displayException exception)
    Right _ -> fail (label ++ ": expected length-overflow exception")

expectJust :: String -> Maybe a -> IO a
expectJust _ (Just value) = pure value
expectJust label Nothing = fail (label ++ ": expected Just")

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")
