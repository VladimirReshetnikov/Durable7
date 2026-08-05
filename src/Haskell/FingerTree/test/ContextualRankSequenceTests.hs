{-# LANGUAGE BangPatterns #-}

-- | Tests for the contextual-rank sequence: the lifted machine measure and its composition order,
-- measure-directed event select, persistence across retained branches, sharing of no-op results,
-- and the machine-policy failure contract.
module ContextualRankSequenceTests (run) where

import Control.Exception (ErrorCall, displayException, evaluate, try)
import Control.Monad (forM_)
import Data.Bits (shiftR, xor, (.&.))
import Data.IORef (IORef, modifyIORef', newIORef, readIORef, writeIORef)
import qualified Data.List as List
import Data.Word (Word64)
import System.IO.Unsafe (unsafePerformIO)

import qualified Durable7.FingerTree.ContextualRankSequence as Contextual

-- | Two states: outside quotes (0) and inside quotes (1). A comma outside quotes is an event.
quotedCommaMachine :: Contextual.ContextualEventMachine Char
quotedCommaMachine = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = 2
  , Contextual.machineTransition = \state element ->
      case element of
        '"' -> Contextual.ContextualTransition (1 - state) 0
        ',' | state == 0 -> Contextual.ContextualTransition state 1
        _ -> Contextual.ContextualTransition state 0
  }

-- | Three states, with transitions that emit zero, one, or two events.
ternaryMachine :: Contextual.ContextualEventMachine Int
ternaryMachine = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = 3
  , Contextual.machineTransition = \state element ->
      let next = (state + abs element) `mod` 3
      in Contextual.ContextualTransition next (toInteger next)
  }

-- | One state and no events, for the count-overflow growth test.
silentMachine :: Contextual.ContextualEventMachine Int
silentMachine = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = 1
  , Contextual.machineTransition = \_ _ -> Contextual.ContextualTransition 0 0
  }

-- | Declares one state but leaves it, violating the machine contract.
invalidStateMachine :: Contextual.ContextualEventMachine Int
invalidStateMachine = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = 1
  , Contextual.machineTransition = \_ _ -> Contextual.ContextualTransition 1 0
  }

-- | Emits a negative event count, violating the machine contract.
negativeEventMachine :: Contextual.ContextualEventMachine Int
negativeEventMachine = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = 1
  , Contextual.machineTransition = \_ _ -> Contextual.ContextualTransition 0 (-1)
  }

-- | Declares no states at all, which is a policy bug.
zeroStateMachine :: Contextual.ContextualEventMachine Int
zeroStateMachine = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = 0
  , Contextual.machineTransition = \state _ -> Contextual.ContextualTransition state 0
  }

-- | Declares a negative state count, which is a policy bug.
negativeStateMachine :: Contextual.ContextualEventMachine Int
negativeStateMachine = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = -1
  , Contextual.machineTransition = \state _ -> Contextual.ContextualTransition state 0
  }

-- | Emits the largest event count the managed reference can represent, so two elements would
-- overflow its checked @long@ but compose exactly here.
hugeEventMachine :: Contextual.ContextualEventMachine Int
hugeEventMachine = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = 1
  , Contextual.machineTransition = \_ _ -> Contextual.ContextualTransition 0 hugeEventCount
  }

hugeEventCount :: Integer
hugeEventCount = 9223372036854775807

-- | A two-state machine over characters, for the state-count mismatch check.
wideCharacterMachine :: Contextual.ContextualEventMachine Char
wideCharacterMachine = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = 3
  , Contextual.machineTransition = \state _ -> Contextual.ContextualTransition state 0
  }

-- | Counts every transition so tests can prove cached queries do not rescan the input.
countingMachine :: IORef Int -> Contextual.ContextualEventMachine Int
countingMachine counter = Contextual.ContextualEventMachine
  { Contextual.machineStateCount = 2
  , Contextual.machineTransition = countingTransition counter
  }

{-# NOINLINE countingTransition #-}
countingTransition :: IORef Int -> Int -> Int -> Contextual.ContextualTransition
countingTransition counter state element = unsafePerformIO $ do
  modifyIORef' counter (+ 1)
  pure
    (Contextual.ContextualTransition
      ((state + (element .&. 1)) .&. 1)
      (if ((element `xor` state) .&. 3) == 0 then 1 else 0))

-- | Runs this module's test cases, reporting each result.
run :: IO ()
run = do
  testQuotedDelimiterMachine
  testCompositionOrder
  testExhaustiveSmallWords
  testRandomizedRetainedBranches
  testRetainedBranchesStayIndependent
  testBoundariesMultiEventsAndInvalidArguments
  testSharing
  testCachedQueriesDoNotRescan
  testPolicyFailures
  testUnboundedEventCounts
  testValidationStatistics
  testLengthOverflow

testQuotedDelimiterMachine :: IO ()
testQuotedDelimiterMachine = do
  let text = Contextual.fromListWith quotedCommaMachine "\"a,b\",c,d"
  assertEqual "outside-quotes evaluation"
    (Just (Contextual.ContextualPrefixSummary 0 2))
    (Contextual.evaluate 0 text)
  assertEqual "inside-quotes evaluation"
    (Just (Contextual.ContextualPrefixSummary 1 1))
    (Contextual.evaluate 1 text)
  assertEqual "rank before the first separator" (Just 0) (Contextual.eventRank 5 0 text)
  assertEqual "rank after the first separator" (Just 1) (Contextual.eventRank 6 0 text)
  assertEqual "first separator location"
    (Just (Contextual.ContextualEventLocation 5 0 0 0 1))
    (Contextual.trySelectEvent 0 0 text)
  assertEqual "second separator element"
    (Just 7)
    (Contextual.locationElementIndex <$> Contextual.trySelectEvent 1 0 text)
  assertEqual "past-end select" Nothing (Contextual.trySelectEvent 2 0 text)
  assertEqual "negative select" Nothing (Contextual.trySelectEvent (-1) 0 text)
  assertEqual "out-of-range state select" Nothing (Contextual.trySelectEvent 0 2 text)
  assertEqual "initially quoted select"
    (Just 2)
    (Contextual.locationElementIndex <$> Contextual.trySelectEvent 0 1 text)
  assertEqual "stored elements" "\"a,b\",c,d" (Contextual.toList text)

  -- Editing publishes a new version and leaves the retained one exactly as it was.
  edited <- expectJust "leading quote" (Contextual.insertAt 0 '"' text)
  assertEqual "retained version after edit"
    (Just (Contextual.ContextualPrefixSummary 0 2))
    (Contextual.evaluate 0 text)
  assertEqual "edited version"
    (Just (Contextual.ContextualPrefixSummary 1 1))
    (Contextual.evaluate 0 edited)
  assertMatchesModel "edited text" quotedCommaMachine "\"\"a,b\",c,d" edited

-- | The lifted measure is associative but deliberately not commutative: the left operand chooses
-- which row of the right operand's effect table is read.
testCompositionOrder :: IO ()
testCompositionOrder = do
  let quote = Contextual.fromListWith quotedCommaMachine "\""
      comma = Contextual.fromListWith quotedCommaMachine ","
      quoteThenComma = Contextual.append quote comma
      commaThenQuote = Contextual.append comma quote
  assertEqual "quote before comma suppresses the event"
    (Just (Contextual.ContextualPrefixSummary 1 0))
    (Contextual.evaluate 0 quoteThenComma)
  assertEqual "comma before quote emits the event"
    (Just (Contextual.ContextualPrefixSummary 1 1))
    (Contextual.evaluate 0 commaThenQuote)
  assertBool "composition is not commutative"
    (Contextual.evaluate 0 quoteThenComma /= Contextual.evaluate 0 commaThenQuote)
  assertMatchesModel "quote then comma" quotedCommaMachine "\"," quoteThenComma
  assertMatchesModel "comma then quote" quotedCommaMachine ",\"" commaThenQuote

  -- Associativity: the same three elements regrouped give the identical cached root summary.
  let text = Contextual.fromListWith quotedCommaMachine "x"
      leftGrouped = Contextual.append (Contextual.append quote comma) text
      rightGrouped = Contextual.append quote (Contextual.append comma text)
  forM_ [0, 1] $ \initialState ->
    assertEqual ("regrouped composition from state " ++ show initialState)
      (Contextual.evaluate initialState leftGrouped)
      (Contextual.evaluate initialState rightGrouped)

testExhaustiveSmallWords :: IO ()
testExhaustiveSmallWords =
  forM_ [0 .. 7 :: Int] $ \wordLength ->
    forM_ (words' wordLength) $ \word ->
      assertMatchesModel
        ("exhaustive word " ++ show word)
        quotedCommaMachine
        word
        (Contextual.fromListWith quotedCommaMachine word)
  where
    alphabet = "\",x"
    words' 0 = [""]
    words' remaining = [character : rest | rest <- words' (remaining - 1), character <- alphabet]

testRandomizedRetainedBranches :: IO ()
testRandomizedRetainedBranches = do
  versions <- walk (0 :: Int) 240 0x61C72026 [(Contextual.emptyWith ternaryMachine, [])]
  forM_ (zip [0 :: Int ..] versions) $ \(position, (sequenceValue, model)) ->
    assertMatchesModel ("retained branch " ++ show position) ternaryMachine model sequenceValue
  where
    walk step limit seed versions
      | step == limit = pure versions
      | otherwise = do
          let (parentIndex, seed1) = randomBelow seed (length versions)
              (parent, parentModel) = versions !! parentIndex
              (choice, seed2) = randomBelow seed1 8
              (magnitude, seed3) = randomBelow seed2 19
              value = magnitude - 9
              modelLength = length parentModel
          (next, nextModel, seed') <-
            case choice of
              0 -> pure (Contextual.cons value parent, value : parentModel, seed3)
              1 -> pure (Contextual.snoc parent value, parentModel ++ [value], seed3)
              2 -> do
                let (position, seedAfter) = randomBelow seed3 (modelLength + 1)
                updated <- expectJust "random insert" (Contextual.insertAt position value parent)
                pure (updated, insertList position value parentModel, seedAfter)
              3 | modelLength > 0 -> do
                let (position, seedAfter) = randomBelow seed3 modelLength
                updated <- expectJust "random set" (Contextual.setAt position value parent)
                pure (updated, replaceList position value parentModel, seedAfter)
              4 | modelLength > 0 -> do
                let (position, seedAfter) = randomBelow seed3 modelLength
                updated <- expectJust "random delete" (Contextual.deleteAt position parent)
                pure (updated, deleteList position parentModel, seedAfter)
              5 -> do
                let (position, seedMiddle) = randomBelow seed3 (modelLength + 1)
                    (amount, seedAfter) = randomBelow seedMiddle (modelLength - position + 1)
                updated <- expectJust "random slice" (Contextual.slice position amount parent)
                pure (updated, take amount (drop position parentModel), seedAfter)
              _ -> do
                let (suffixIndex, seedAfter) = randomBelow seed3 (length versions)
                    (suffix, suffixModel) = versions !! suffixIndex
                if modelLength + length suffixModel > 96
                  then pure (parent, parentModel, seedAfter)
                  else
                    pure
                      ( Contextual.append parent suffix
                      , parentModel ++ suffixModel
                      , seedAfter
                      )
          assertMatchesModel ("random step " ++ show step) ternaryMachine nextModel next
          let extended = versions ++ [(next, nextModel)]
              retained =
                if length extended > 48
                  then take 1 extended ++ drop 2 extended
                  else extended
          walk (step + 1) limit seed' retained

testRetainedBranchesStayIndependent :: IO ()
testRetainedBranchesStayIndependent = do
  let source = Contextual.fromListWith quotedCommaMachine "a,b,c"
  quoted <- expectJust "branch insert" (Contextual.insertAt 0 '"' source)
  trimmed <- expectJust "branch delete" (Contextual.deleteAt 1 source)
  replaced <- expectJust "branch set" (Contextual.setAt 1 '"' source)
  assertMatchesModel "branch source" quotedCommaMachine "a,b,c" source
  assertMatchesModel "branch quoted" quotedCommaMachine "\"a,b,c" quoted
  assertMatchesModel "branch trimmed" quotedCommaMachine "ab,c" trimmed
  assertMatchesModel "branch replaced" quotedCommaMachine "a\"b,c" replaced
  assertEqual "source summary" (Just (Contextual.ContextualPrefixSummary 0 2))
    (Contextual.evaluate 0 source)
  assertEqual "quoted summary" (Just (Contextual.ContextualPrefixSummary 1 0))
    (Contextual.evaluate 0 quoted)
  assertEqual "trimmed summary" (Just (Contextual.ContextualPrefixSummary 0 1))
    (Contextual.evaluate 0 trimmed)
  assertEqual "replaced summary" (Just (Contextual.ContextualPrefixSummary 1 0))
    (Contextual.evaluate 0 replaced)

testBoundariesMultiEventsAndInvalidArguments :: IO ()
testBoundariesMultiEventsAndInvalidArguments = do
  let sequenceValue = Contextual.fromListWith ternaryMachine [2, -3, 4, 0]
      empty = Contextual.emptyWith ternaryMachine
      total = Contextual.count sequenceValue
  forM_ [0 .. total] $ \boundary -> do
    (left, right) <- expectJust "boundary split" (Contextual.splitAt boundary sequenceValue)
    assertEqual ("split round trip at " ++ show boundary)
      (Contextual.toList sequenceValue)
      (Contextual.toList (Contextual.append left right))
    assertEqual ("split counts at " ++ show boundary)
      total
      (Contextual.count left + Contextual.count right)

  located <- expectJust "multi-event select" (Contextual.trySelectEvent 0 0 sequenceValue)
  assertBool "event ordinal inside its element"
    (Contextual.locationEventIndexInElement located >= 0
      && Contextual.locationEventIndexInElement located
           < Contextual.locationElementEventCount located)
  assertBool "the ternary machine exercises multi-event transitions"
    (any
      (\position ->
        case (Contextual.eventRank (position + 1) 0 sequenceValue,
              Contextual.eventRank position 0 sequenceValue) of
          (Just after, Just before) -> after - before > 1
          _ -> False)
      [0 .. total - 1])

  assertEqual "negative state" Nothing (Contextual.evaluate (-1) sequenceValue)
  assertEqual "past-end state" Nothing (Contextual.evaluate 3 sequenceValue)
  assertEqual "negative prefix" Nothing (Contextual.evaluatePrefix (-1) 0 sequenceValue)
  assertEqual "past-end prefix" Nothing (Contextual.evaluatePrefix (total + 1) 0 sequenceValue)
  assertEqual "past-end rank" Nothing (Contextual.eventRank (total + 1) 0 sequenceValue)
  assertEqual "out-of-range state select" Nothing (Contextual.trySelectEvent 0 3 sequenceValue)
  assertEqual "negative index" Nothing (Contextual.index (-1) sequenceValue)
  assertEqual "past-end index" Nothing (Contextual.index total sequenceValue)
  assertEqual "past-end insert" NothingResult
    (tagMaybe (Contextual.insertAt (total + 1) 0 sequenceValue))
  assertEqual "negative insert" NothingResult (tagMaybe (Contextual.insertAt (-1) 0 sequenceValue))
  assertEqual "past-end set" NothingResult (tagMaybe (Contextual.setAt total 0 sequenceValue))
  assertEqual "past-end delete" NothingResult (tagMaybe (Contextual.deleteAt total sequenceValue))
  assertEqual "past-end split" NothingResult (tagMaybe (Contextual.splitAt (total + 1) sequenceValue))
  assertEqual "overlong slice" NothingResult (tagMaybe (Contextual.slice 1 total sequenceValue))
  assertEqual "negative slice" NothingResult (tagMaybe (Contextual.slice (-1) 1 sequenceValue))

  assertBool "empty sequence is empty" (Contextual.null empty)
  assertEqual "empty evaluation carries the state through"
    (Just (Contextual.ContextualPrefixSummary 2 0))
    (Contextual.evaluate 2 empty)
  assertEqual "empty prefix evaluation"
    (Just (Contextual.ContextualPrefixSummary 1 0))
    (Contextual.evaluatePrefix 0 1 empty)
  assertEqual "empty select" Nothing (Contextual.trySelectEvent 0 0 empty)
  assertEqual "empty machine state count" 3 (Contextual.stateCount empty)
  assertMatchesModel "empty sequence" ternaryMachine [] empty

  let singleton = Contextual.snoc empty 5
  assertMatchesModel "singleton" ternaryMachine [5] singleton
  emptied <- expectJust "singleton delete" (Contextual.deleteAt 0 singleton)
  assertBool "deleting the last element empties the sequence" (Contextual.null emptied)
  assertMatchesModel "emptied" ternaryMachine [] emptied

testSharing :: IO ()
testSharing = do
  let sequenceValue = Contextual.fromListWith ternaryMachine [1 .. 32]
      empty = Contextual.emptyWith ternaryMachine
      total = Contextual.count sequenceValue
  (_, wholeRight) <- expectJust "zero split" (Contextual.splitAt 0 sequenceValue)
  (wholeLeft, _) <- expectJust "end split" (Contextual.splitAt total sequenceValue)
  wholeSlice <- expectJust "whole slice" (Contextual.slice 0 total sequenceValue)
  assertShares "right empty concatenation retains root" sequenceValue
    (Contextual.append sequenceValue empty)
  assertShares "left empty concatenation retains root" sequenceValue
    (Contextual.append empty sequenceValue)
  assertShares "zero split retains root" sequenceValue wholeRight
  assertShares "end split retains root" sequenceValue wholeLeft
  assertShares "whole slice retains root" sequenceValue wholeSlice
  assertShares "empty results share" empty (Contextual.emptyWith ternaryMachine)
  changed <- Contextual.sharesRootWith sequenceValue (Contextual.snoc sequenceValue 1)
  assertBool "an append publishes a new root" (not changed)
  mixed <- Contextual.sharesRootWith sequenceValue empty
  assertBool "an empty version shares no root with a nonempty one" (not mixed)

testCachedQueriesDoNotRescan :: IO ()
testCachedQueriesDoNotRescan = do
  counter <- newIORef 0
  let machine = countingMachine counter
      elementCount = 8192
      sequenceValue = Contextual.fromListWith machine [0 .. elementCount - 1]
  assertEqual "counted element count" elementCount (Contextual.count sequenceValue)
  buildCalls <- readIORef counter
  assertEqual "construction forces exactly one transition per state per element"
    (2 * elementCount)
    buildCalls

  writeIORef counter 0
  forM_ [0, 17 .. elementCount] $ \boundary -> do
    rank <-
      expectJust ("cached rank at " ++ show boundary)
        (Contextual.eventRank boundary (boundary `mod` 2) sequenceValue)
    assertBool ("nonnegative rank at " ++ show boundary) (rank >= 0)
  rankCalls <- readIORef counter
  assertEqual "cached prefix ranks never invoke the machine" 0 rankCalls

  total <-
    fmap Contextual.prefixEventCount
      (expectJust "total events" (Contextual.evaluate 0 sequenceValue))
  assertBool "the counting machine emits events" (total > 0)
  let ranks = takeWhile (< total) [0, 31 ..]
  writeIORef counter 0
  forM_ ranks $ \rank -> do
    located <-
      expectJust ("select at rank " ++ show rank)
        (Contextual.trySelectEvent rank 0 sequenceValue)
    assertBool ("located element in range " ++ show rank)
      (Contextual.locationElementIndex located >= 0
        && Contextual.locationElementIndex located < elementCount)
  selectCalls <- readIORef counter
  assertEqual "each successful select invokes exactly one transition"
    (length ranks)
    selectCalls

testPolicyFailures :: IO ()
testPolicyFailures = do
  invalidState <-
    tryCount (Contextual.singletonWith invalidStateMachine 1)
  assertErrorPrefix "invalid next state"
    "Durable7.FingerTree.ContextualRankSequence: the machine returned a state outside its declared range"
    invalidState
  negativeEvents <- tryCount (Contextual.singletonWith negativeEventMachine 1)
  assertErrorPrefix "negative event count"
    "Durable7.FingerTree.ContextualRankSequence: the machine returned a negative event count"
    negativeEvents
  zeroStates <- tryCount (Contextual.emptyWith zeroStateMachine)
  assertErrorPrefix "zero state count"
    "Durable7.FingerTree.ContextualRankSequence: a contextual event machine must have at least one state"
    zeroStates
  negativeStates <- tryCount (Contextual.emptyWith negativeStateMachine)
  assertErrorPrefix "negative state count"
    "Durable7.FingerTree.ContextualRankSequence: a contextual event machine must have at least one state"
    negativeStates
  zeroStateQuery <- try (evaluate (Contextual.stateCount (Contextual.emptyWith zeroStateMachine)))
  assertErrorPrefix "zero state count query"
    "Durable7.FingerTree.ContextualRankSequence: a contextual event machine must have at least one state"
    zeroStateQuery

  -- A retained version survives a rejected successor: the failure publishes nothing.
  let source = Contextual.fromListWith ternaryMachine [2, -3]
  rejected <- tryCount (Contextual.append source (Contextual.fromListWith silentMachine [1]))
  assertErrorPrefix "machine state-count mismatch"
    "Durable7.FingerTree.ContextualRankSequence: concatenated sequences declare different machine state counts"
    rejected
  assertMatchesModel "source after a rejected concatenation" ternaryMachine [2, -3] source

  mismatched <-
    tryCount
      (Contextual.append
        (Contextual.fromListWith quotedCommaMachine "a")
        (Contextual.fromListWith wideCharacterMachine "b"))
  assertErrorPrefix "character machine mismatch"
    "Durable7.FingerTree.ContextualRankSequence: concatenated sequences declare different machine state counts"
    mismatched

  -- The state-count gate runs at construction, so a machine that declares no states is rejected
  -- before any audit can observe it.
  zeroStateValidation <- tryValidation (Contextual.emptyWith zeroStateMachine)
  assertErrorPrefix "validation of a zero-state machine"
    "Durable7.FingerTree.ContextualRankSequence: a contextual event machine must have at least one state"
    zeroStateValidation

-- | The managed reference stores event counts in a checked @long@ and raises on overflow. This
-- port uses 'Integer', so the same composition is exact and no overflow contract exists.
testUnboundedEventCounts :: IO ()
testUnboundedEventCounts = do
  let source = Contextual.singletonWith hugeEventMachine 1
      doubled = Contextual.snoc source 2
      quadrupled = Contextual.append doubled doubled
  assertEqual "single huge transition"
    (Just (Contextual.ContextualPrefixSummary 0 hugeEventCount))
    (Contextual.evaluate 0 source)
  assertEqual "composition beyond the managed reference's range"
    (Just (Contextual.ContextualPrefixSummary 0 (2 * hugeEventCount)))
    (Contextual.evaluate 0 doubled)
  assertEqual "further composition stays exact"
    (Just (Contextual.ContextualPrefixSummary 0 (4 * hugeEventCount)))
    (Contextual.evaluate 0 quadrupled)
  assertEqual "select past the managed reference's range"
    (Just (Contextual.ContextualEventLocation 3 0 0 0 hugeEventCount))
    (Contextual.trySelectEvent (3 * hugeEventCount) 0 quadrupled)
  assertEqual "retained version unchanged"
    (Just (Contextual.ContextualPrefixSummary 0 hugeEventCount))
    (Contextual.evaluate 0 source)

testValidationStatistics :: IO ()
testValidationStatistics = do
  let text = Contextual.fromListWith quotedCommaMachine "\"a,b\",c,d"
  statistics <- expectRight "statistics" (Contextual.validateStructure text)
  assertEqual "validated count" 9 (Contextual.validationCount statistics)
  assertEqual "validated state count" 2 (Contextual.validationStateCount statistics)
  assertEqual "validated maximum event count" 2 (Contextual.validationMaximumEventCount statistics)
  assertEqual "reported state count" 2 (Contextual.stateCount text)

testLengthOverflow :: IO ()
testLengthOverflow = do
  let seed = Contextual.singletonWith silentMachine 0
      grow sequenceValue
        | Contextual.count sequenceValue > maxBound `div` 2 = sequenceValue
        | otherwise = grow (Contextual.append sequenceValue sequenceValue)
      power = grow seed
  almostPower <- expectJust "maximum-count delete" (Contextual.deleteAt 0 power)
  let maximumSequence = Contextual.append power almostPower
  assertEqual "maximum cached count" maxBound (Contextual.count maximumSequence)
  assertEqual "maximum front" (Just 0) (Contextual.index 0 maximumSequence)
  assertEqual "maximum back" (Just 0) (Contextual.index (maxBound - 1) maximumSequence)
  assertEqual "maximum evaluation"
    (Just (Contextual.ContextualPrefixSummary 0 0))
    (Contextual.evaluate 0 maximumSequence)

  consResult <- tryCount (Contextual.cons 0 maximumSequence)
  snocResult <- tryCount (Contextual.snoc maximumSequence 0)
  insertAtStart <- tryMaybeCount (Contextual.insertAt 0 0 maximumSequence)
  insertInside <- tryMaybeCount (Contextual.insertAt 1 0 maximumSequence)
  insertAtEnd <- tryMaybeCount (Contextual.insertAt maxBound 0 maximumSequence)
  appendResult <- tryCount (Contextual.append maximumSequence seed)
  forM_
    [ ("overflowing prepend", consResult)
    , ("overflowing append", snocResult)
    , ("overflowing insert at the start", insertAtStart)
    , ("overflowing insert inside", insertInside)
    , ("overflowing insert at the end", insertAtEnd)
    , ("overflowing concatenation", appendResult)
    ]
    (\(label, result) ->
      assertErrorPrefix label
        "Durable7.FingerTree.ContextualRankSequence: length overflow"
        result)

  -- Range validation still precedes the capacity guard.
  assertEqual "invalid insert boundary" NothingResult
    (tagMaybe (Contextual.insertAt (-1) 0 maximumSequence))
  assertEqual "maximum snapshot remains usable" maxBound (Contextual.count maximumSequence)

assertMatchesModel :: (Eq element, Show element)
                   => String
                   -> Contextual.ContextualEventMachine element
                   -> [element]
                   -> Contextual.ContextualRankSequence element
                   -> IO ()
assertMatchesModel label machine model sequenceValue = do
  assertEqual (label ++ " contents") model (Contextual.toList sequenceValue)
  assertEqual (label ++ " count") (length model) (Contextual.count sequenceValue)
  assertEqual (label ++ " emptiness") (List.null model) (Contextual.null sequenceValue)
  statistics <- expectRight (label ++ " validation") (Contextual.validateStructure sequenceValue)
  assertEqual (label ++ " validated count") (length model) (Contextual.validationCount statistics)
  assertEqual (label ++ " validated state count") states
    (Contextual.validationStateCount statistics)
  forM_ (zip [0 ..] model) $ \(position, value) ->
    assertEqual (label ++ " element " ++ show position) (Just value)
      (Contextual.index position sequenceValue)
  forM_ [0 .. states - 1] $ \initialState -> do
    let steps = runMachine machine initialState model
        summaries =
          List.scanl' advance (Contextual.ContextualPrefixSummary initialState 0) steps
        advance summary (_, transition) =
          Contextual.ContextualPrefixSummary
            (Contextual.transitionNextState transition)
            (Contextual.prefixEventCount summary + Contextual.transitionEventCount transition)
        locations =
          [ Contextual.ContextualEventLocation
              { Contextual.locationElementIndex = position
              , Contextual.locationEventIndexInElement = ordinal
              , Contextual.locationStateBefore = stateBefore
              , Contextual.locationStateAfter = Contextual.transitionNextState transition
              , Contextual.locationElementEventCount = Contextual.transitionEventCount transition
              }
          | (position, (stateBefore, transition)) <- zip [0 ..] steps
          , ordinal <- [0 .. Contextual.transitionEventCount transition - 1]
          ]
        stateLabel suffix = label ++ " from state " ++ show initialState ++ " " ++ suffix
    forM_ (zip [0 ..] summaries) $ \(boundary, expected) -> do
      assertEqual (stateLabel ("prefix " ++ show boundary)) (Just expected)
        (Contextual.evaluatePrefix boundary initialState sequenceValue)
      assertEqual (stateLabel ("rank " ++ show boundary))
        (Just (Contextual.prefixEventCount expected))
        (Contextual.eventRank boundary initialState sequenceValue)
    assertEqual (stateLabel "whole evaluation") (Just (last summaries))
      (Contextual.evaluate initialState sequenceValue)
    forM_ (zip [0 ..] locations) $ \(rank, expected) ->
      assertEqual (stateLabel ("select " ++ show rank)) (Just expected)
        (Contextual.trySelectEvent rank initialState sequenceValue)
    assertEqual (stateLabel "select past the last event") Nothing
      (Contextual.trySelectEvent (toInteger (length locations)) initialState sequenceValue)
  where
    states = Contextual.machineStateCount machine

runMachine :: Contextual.ContextualEventMachine element
           -> Int
           -> [element]
           -> [(Int, Contextual.ContextualTransition)]
runMachine _ _ [] = []
runMachine machine state (value : rest) =
  let transition = Contextual.machineTransition machine state value
  in (state, transition)
       : runMachine machine (Contextual.transitionNextState transition) rest

nextRandom :: Word64 -> Word64
nextRandom state = state * 6364136223846793005 + 1442695040888963407

randomBelow :: Word64 -> Int -> (Int, Word64)
randomBelow seed bound =
  let advanced = nextRandom seed
  in (fromIntegral ((advanced `shiftR` 33) `mod` fromIntegral bound), advanced)

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

data MaybeResult = NothingResult | JustResult
  deriving (Eq, Show)

tagMaybe :: Maybe a -> MaybeResult
tagMaybe Nothing = NothingResult
tagMaybe (Just _) = JustResult

tryCount :: Contextual.ContextualRankSequence element -> IO (Either ErrorCall Int)
tryCount sequenceValue = try (evaluate (Contextual.count sequenceValue))

tryMaybeCount :: Maybe (Contextual.ContextualRankSequence element)
              -> IO (Either ErrorCall Int)
tryMaybeCount result = try (evaluate (maybe (-1) Contextual.count result))

tryValidation :: Contextual.ContextualRankSequence element
              -> IO (Either ErrorCall (Either String Contextual.ValidationStatistics))
tryValidation sequenceValue = try (evaluate (Contextual.validateStructure sequenceValue))

assertShares :: String
             -> Contextual.ContextualRankSequence element
             -> Contextual.ContextualRankSequence element
             -> IO ()
assertShares label left right = do
  shares <- Contextual.sharesRootWith left right
  assertBool label shares

assertErrorPrefix :: String -> String -> Either ErrorCall a -> IO ()
assertErrorPrefix label expected result =
  case result of
    Left exception ->
      assertBool
        (label ++ ": wrong error: " ++ displayException exception)
        (expected `List.isPrefixOf` displayException exception)
    Right _ -> fail (label ++ ": expected " ++ expected)

expectJust :: String -> Maybe a -> IO a
expectJust _ (Just value) = pure value
expectJust label Nothing = fail (label ++ ": expected Just")

expectRight :: String -> Either String a -> IO a
expectRight _ (Right value) = pure value
expectRight label (Left message) = fail (label ++ ": " ++ message)

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual
  | expected == actual = pure ()
  | otherwise = fail (label ++ ": expected " ++ show expected ++ ", got " ++ show actual)

assertBool :: String -> Bool -> IO ()
assertBool label condition
  | condition = pure ()
  | otherwise = fail (label ++ ": expected true")
