{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE FlexibleContexts #-}
{-# LANGUAGE FlexibleInstances #-}
{-# LANGUAGE MultiParamTypeClasses #-}

-- | A persistent sequence that ranks and selects events whose occurrence depends on finite left
-- context.
--
-- A sum-measured sequence can rank elements that carry a fixed local weight. It cannot rank a
-- comma that is a field separator in one prefix context and quoted data in another, because no
-- single weight is correct for both. A streaming automaton keeps that context but has to replay a
-- prefix after every edit.
--
-- This module resolves that by lifting a finite deterministic machine into the measure of
-- "Durable7.FingerTree.Measured". Every subtree caches, /for every possible incoming state/, the
-- outgoing state and the number of events emitted while consuming that subtree. Ordered
-- composition feeds the left subtree's outgoing state into the right subtree's summary, which
-- turns a context-dependent scan into an associative measure:
--
-- > (a <> b) `finalState` q = b `finalState` (a `finalState` q)
-- > (a <> b) `events`     q = a `events` q + b `events` (a `finalState` q)
-- > (a <> b) `count`        = a `count` + b `count`
--
-- The identity maps every state to itself, emits zero events, and holds zero elements. Composition
-- is /not/ commutative: the left operand chooses which row of the right operand's table is read.
-- Because event counts are nonnegative, the select predicate @prefix events q0 > r@ is monotone,
-- so a single measure-directed descent finds the element that emitted event @r@ — no binary search
-- and no prefix replay.
--
-- == Bounds
--
-- Let @n@ be the element count and @s@ the machine's fixed state count. Full-sequence evaluation
-- is @O(1)@: it reads the cached root summary, as do 'count' and 'null'. Prefix evaluation,
-- indexing, contextual event rank and select, insertion, replacement, removal, splitting, and
-- slicing are @O(s log n)@: a measure-directed descent over @O(log n)@ levels, each level
-- composing one @O(s)@ effect table. Endpoint updates ('cons', 'snoc') are @O(s)@ amortized, and
-- concatenation is @O(s log(min(n, m)))@ — this port sits on the package's Hinze–Paterson finger
-- tree, whose short digits and lazy middle spine deliver those endpoint and concatenation bounds
-- under persistent reuse, so the managed reference's figures carry over unchanged. Building from a
-- list is @O(s n)@.
--
-- Enumeration is the one figure that does /not/ carry over: it is @Theta(s n)@ here, not the
-- reference's @O(n)@. 'Durable7.FingerTree.Measured.toList' is a left-view fold, and every step
-- rebuilds a spine node whose strict measure field forces one fresh @O(s)@ effect-table
-- composition, so each yielded element pays an @s@-wide monoid append. The managed reference
-- instead walks an explicit traversal stack over the immutable snapshot and combines no measures at
-- all. Nothing in this module can avoid that without a measure-free traversal on the substrate.
--
-- Cached summaries and reported query results are forced rather than suspended, so the cost a
-- bound describes is paid by the operation that reports it and never deferred into the caller's
-- later inspection. Stored payload elements stay lazy; only the machine's view of them is forced.
--
-- Each retained changed spine adds @O(s log n)@ summary storage while untouched structure stays
-- shared, so the structure costs @O(s n)@ summary space rather than @O(n)@. When the machine is
-- fixed, @s@ is a constant and the contextual queries are @O(log n)@ instead of the @Θ(n)@ replay
-- a state-free persistent sequence needs; if @s@ is treated as an input rather than a fixed
-- policy, this structure does /not/ dominate a plain persistent sequence, because both its edits
-- and its storage explicitly carry the @s@ factor.
--
-- == Deliberate limits
--
-- Context must be finite and must flow left to right. Event weights must be nonnegative so select
-- stays monotone. One input element occupies one measured leaf, so this is not a chunked text rope
-- and makes no cache-density claim. There is no way to change the machine of an existing sequence;
-- rebuilding under another machine costs @O(s n)@.
--
-- == The machine is a value, not a class
--
-- The managed reference makes the machine a static-abstract interface and a type parameter, so the
-- compiler proves that two concatenated sequences share one machine. This package's convention is
-- a record of functions instead, as with
-- 'Durable7.FingerTree.RangeUpdateSequence.RangeUpdateAlgebra': a sequence retains its machine, so
-- empty results stay usable without any global policy facility. A record of functions has no
-- decidable identity, so 'append' cannot reproduce the reference's type-level gate. It checks the
-- one part of machine compatibility that /is/ observable — the declared state count, whose
-- mismatch would otherwise index one operand's effect table out of range — and leaves deeper
-- agreement as a caller precondition, exactly as
-- 'Durable7.FingerTree.RangeUpdateSequence.append' does for its algebra.
module Durable7.FingerTree.ContextualRankSequence
  ( ContextualTransition(..)
  , ContextualEventMachine(..)
  , ContextualPrefixSummary(..)
  , ContextualEventLocation(..)
  , ContextualRankSequence
  , ValidationStatistics(..)
  , emptyWith
  , singletonWith
  , fromListWith
  , toList
  , count
  , null
  , stateCount
  , index
  , evaluate
  , evaluatePrefix
  , eventRank
  , trySelectEvent
  , cons
  , snoc
  , append
  , insertAt
  , setAt
  , deleteAt
  , splitAt
  , slice
  , validateStructure
  , sharesRootWith
  ) where

import Prelude hiding (null, splitAt)

import qualified Control.Exception as Exception
import Data.Array (Array, (!), bounds, elems, listArray)
import qualified Data.List as List
import System.Mem.StableName (eqStableName, makeStableName)

import Durable7.FingerTree.Measured (Measured(..))
import qualified Durable7.FingerTree.Measured as Measured

-- | One transition of a deterministic additive event machine.
data ContextualTransition = ContextualTransition
  { transitionNextState :: !Int
    -- ^ The state after consuming the element. Must lie in @0 .. stateCount - 1@.
  , transitionEventCount :: !Integer
    -- ^ The nonnegative number of logical events the transition emits; one transition may emit
    -- more than one.
  }
  deriving (Eq, Show)

-- | A finite deterministic machine whose transitions emit nonnegative event counts.
--
-- 'machineStateCount' must be positive. For every state in @0 .. stateCount - 1@,
-- 'machineTransition' must return a state in the same range and a nonnegative event count, and it
-- must be deterministic and free of side effects. Violating any of those is a policy bug and
-- raises the module's 'error', not a 'Maybe' miss.
data ContextualEventMachine element = ContextualEventMachine
  { machineStateCount :: !Int
    -- ^ The finite number of machine states. Must be greater than zero.
  , machineTransition :: Int -> element -> ContextualTransition
    -- ^ Consumes one element from one valid state.
  }

-- | The result of running a contextual event machine over a prefix.
data ContextualPrefixSummary = ContextualPrefixSummary
  { prefixFinalState :: !Int
    -- ^ The machine state after the prefix.
  , prefixEventCount :: !Integer
    -- ^ The number of events emitted by the prefix.
  }
  deriving (Eq, Show)

-- | Identifies one contextual event and the input element that emitted it.
data ContextualEventLocation = ContextualEventLocation
  { locationElementIndex :: !Int
    -- ^ The zero-based index of the emitting element.
  , locationEventIndexInElement :: !Integer
    -- ^ The event's zero-based ordinal among the events emitted by that one transition.
  , locationStateBefore :: !Int
    -- ^ The machine state before the element.
  , locationStateAfter :: !Int
    -- ^ The machine state after the element.
  , locationElementEventCount :: !Integer
    -- ^ The total number of events emitted by that transition.
  }
  deriving (Eq, Show)

-- | Independently recomputed statistics returned by a successful 'validateStructure'.
data ValidationStatistics = ValidationStatistics
  { validationCount :: !Int
    -- ^ The number of elements, recounted by enumeration.
  , validationStateCount :: !Int
    -- ^ The machine's state count.
  , validationMaximumEventCount :: !Integer
    -- ^ The largest total event count over all initial states, recomputed by direct scan.
  }
  deriving (Eq, Show)

-- | An immutable contextual-rank sequence. The machine record is retained so empty results remain
-- usable without a global policy facility.
data ContextualRankSequence element = ContextualRankSequence
  { sequenceMachine :: !(ContextualEventMachine element)
  , sequenceTree :: !(Tree element)
  }

-- | The effect of one subtree on one particular incoming state.
data StateEffect = StateEffect !Int !Integer

-- | A cached subtree summary: the element count plus one 'StateEffect' per machine state.
--
-- 'IdentitySummary' is the monoid identity. It is the one summary that cannot name a state count,
-- which is what lets a runtime machine record supply @s@ while the measure monoid stays a plain
-- 'Monoid'.
data ContextualSummary
  = IdentitySummary
  | ContextualSummary !Int !(Array Int StateEffect)

-- | One measured leaf: the payload element, kept lazy, beside its forced effect table.
data Leaf element = Leaf element !ContextualSummary

type Tree element = Measured.FingerTree ContextualSummary (Leaf element)

instance Semigroup ContextualSummary where
  IdentitySummary <> right = right
  left <> IdentitySummary = left
  ContextualSummary leftCount leftEffects <> ContextualSummary rightCount rightEffects
    | width /= effectWidth rightEffects =
        error (messagePrefix ++ "composed summaries of machines with different state counts")
    | otherwise =
        let !total = checkedAdd leftCount rightCount
            !effects = buildEffects width composeState
        in ContextualSummary total effects
    where
      width = effectWidth leftEffects
      composeState state =
        case leftEffects ! state of
          StateEffect middle events ->
            case rightEffects ! middle of
              StateEffect next moreEvents -> StateEffect next (events + moreEvents)

instance Monoid ContextualSummary where
  mempty = IdentitySummary

instance Measured ContextualSummary (Leaf element) where
  measure (Leaf _ summary) = summary

-- | Constructs an empty sequence using the supplied machine. A nonpositive
-- 'machineStateCount' is a policy bug and raises the module's 'error'. @O(1)@.
emptyWith :: ContextualEventMachine element -> ContextualRankSequence element
emptyWith machine =
  checkedStateCount machine `seq` ContextualRankSequence machine Measured.empty

-- | Constructs a one-element sequence. @O(s)@.
singletonWith :: ContextualEventMachine element
              -> element
              -> ContextualRankSequence element
singletonWith machine value =
  let !leaf = makeLeaf machine value
  in ContextualRankSequence machine (Measured.singleton leaf)

-- | Constructs a sequence holding a list's elements in input order. @O(s n)@.
fromListWith :: ContextualEventMachine element
             -> [element]
             -> ContextualRankSequence element
fromListWith machine values =
  checkedStateCount machine
    `seq` ContextualRankSequence machine (List.foldl' pushBack Measured.empty values)
  where
    pushBack tree value =
      let !leaf = makeLeaf machine value
      in Measured.snoc tree leaf

-- | Enumerates the stored elements in sequence order. @Theta(s n)@, not the managed reference's
-- @O(n)@: the substrate's left-view fold reforces one @O(s)@ effect-table composition per step. See
-- the module's bounds section.
toList :: ContextualRankSequence element -> [element]
toList sequenceValue = map leafValue (Measured.toList (sequenceTree sequenceValue))

-- | The cached element count. @O(1)@.
count :: ContextualRankSequence element -> Int
count sequenceValue = summaryCount (rootSummary sequenceValue)

-- | Whether the sequence holds no elements. @O(1)@.
null :: ContextualRankSequence element -> Bool
null sequenceValue = Measured.null (sequenceTree sequenceValue)

-- | The retained machine's finite state count. This is the instance-level stand-in for the
-- reference's static @StateCount@, because a record of functions cannot carry static members.
-- @O(1)@.
stateCount :: ContextualRankSequence element -> Int
stateCount sequenceValue = checkedStateCount (sequenceMachine sequenceValue)

-- | The element at a zero-based index, or 'Nothing' when the index is outside the sequence.
-- @O(s log n)@.
index :: Int -> ContextualRankSequence element -> Maybe element
index position sequenceValue
  | position < 0 || position >= count sequenceValue = Nothing
  | otherwise =
      case Measured.locate (positionReached position) (sequenceTree sequenceValue) of
        Just (_, leaf) -> Just (leafValue leaf)
        Nothing -> error (messagePrefix ++ "a validated index missed the measured descent")

-- | Runs the machine over the whole sequence from an initial state, or reports 'Nothing' when the
-- state is outside @0 .. stateCount - 1@. @O(1)@: the answer is the cached root summary.
evaluate :: Int -> ContextualRankSequence element -> Maybe ContextualPrefixSummary
evaluate initialState sequenceValue
  | not (validState sequenceValue initialState) = Nothing
  | otherwise =
      let !summary = summaryEvaluate initialState (rootSummary sequenceValue)
      in Just summary

-- | Runs the machine over the first @elementCount@ elements from an initial state.
--
-- Reports 'Nothing' when the prefix length is outside @0 .. count@ or the state is outside
-- @0 .. stateCount - 1@. @O(s log n)@, and @O(1)@ at either endpoint, where the answer is the
-- identity or the cached root summary.
evaluatePrefix :: Int
               -> Int
               -> ContextualRankSequence element
               -> Maybe ContextualPrefixSummary
evaluatePrefix elementCount initialState sequenceValue
  | not (validState sequenceValue initialState) = Nothing
  | elementCount < 0 || elementCount > total = Nothing
  | elementCount == 0 =
      let !summary = ContextualPrefixSummary initialState 0
      in Just summary
  | elementCount == total =
      let !summary = summaryEvaluate initialState (rootSummary sequenceValue)
      in Just summary
  | otherwise =
      case Measured.locate (positionReached elementCount) (sequenceTree sequenceValue) of
        Just (before, _) ->
          let !summary = summaryEvaluate initialState before
          in Just summary
        Nothing -> error (messagePrefix ++ "a validated prefix missed the measured descent")
  where
    total = count sequenceValue

-- | The number of contextual events strictly before an element boundary. Reports 'Nothing' on the
-- same inputs as 'evaluatePrefix'. @O(s log n)@.
eventRank :: Int -> Int -> ContextualRankSequence element -> Maybe Integer
eventRank elementCount initialState sequenceValue =
  case evaluatePrefix elementCount initialState sequenceValue of
    Nothing -> Nothing
    Just summary ->
      let !events = prefixEventCount summary
      in Just events

-- | Locates the zero-based contextual event of a given rank, counting from an initial state.
--
-- Reports 'Nothing' when the state is outside @0 .. stateCount - 1@, or when the rank is negative
-- or at or past the sequence's total event count. Because event counts are nonnegative the
-- predicate @prefix events q0 > r@ is monotone, so this is one measure-directed descent — no
-- binary search, no prefix replay — plus exactly one machine transition, for the located element.
-- @O(s log n)@.
trySelectEvent :: Integer
               -> Int
               -> ContextualRankSequence element
               -> Maybe ContextualEventLocation
trySelectEvent eventIndex initialState sequenceValue
  | not (validState sequenceValue initialState) = Nothing
  | eventIndex < 0 = Nothing
  | eventIndex >= summaryEventCount initialState (rootSummary sequenceValue) = Nothing
  | otherwise =
      case Measured.locate (eventReached initialState eventIndex) (sequenceTree sequenceValue) of
        Nothing -> error (messagePrefix ++ "the contextual event summary is inconsistent")
        Just (before, Leaf value _) ->
          let stateBefore = summaryFinalState initialState before
              transition = checkedTransition (sequenceMachine sequenceValue) stateBefore value
              !location = ContextualEventLocation
                { locationElementIndex = summaryCount before
                , locationEventIndexInElement =
                    eventIndex - summaryEventCount initialState before
                , locationStateBefore = stateBefore
                , locationStateAfter = transitionNextState transition
                , locationElementEventCount = transitionEventCount transition
                }
          in Just location

-- | A sequence with one element prepended. @O(s)@ amortized. @O(s log n)@ storage is added for the
-- changed spine; every earlier version stays valid.
cons :: element -> ContextualRankSequence element -> ContextualRankSequence element
cons value sequenceValue =
  let machine = sequenceMachine sequenceValue
      !leaf = makeLeaf machine value
  in ContextualRankSequence machine (Measured.cons leaf (sequenceTree sequenceValue))

-- | A sequence with one element appended. @O(s)@ amortized.
snoc :: ContextualRankSequence element -> element -> ContextualRankSequence element
snoc sequenceValue value =
  let machine = sequenceMachine sequenceValue
      !leaf = makeLeaf machine value
  in ContextualRankSequence machine (Measured.snoc (sequenceTree sequenceValue) leaf)

-- | Concatenates two sequences, reusing a nonempty operand when the other is empty.
--
-- Both operands must have been built with compatible machines; a record of functions has no
-- decidable identity, so that is a caller precondition just as it is for
-- 'Durable7.FingerTree.RangeUpdateSequence.append'. Declared state counts /are/ comparable, and a
-- mismatch there would index an effect table out of range, so it raises the module's 'error'
-- rather than producing a corrupt summary. @O(s log(min(n, m)))@.
append :: ContextualRankSequence element
       -> ContextualRankSequence element
       -> ContextualRankSequence element
append left right
  | machineStateCount leftMachine /= machineStateCount rightMachine =
      error (messagePrefix ++ "concatenated sequences declare different machine state counts")
  | Measured.null (sequenceTree right) = left
  | Measured.null (sequenceTree left) = right
  | otherwise =
      ContextualRankSequence
        leftMachine
        (Measured.append (sequenceTree left) (sequenceTree right))
  where
    leftMachine = sequenceMachine left
    rightMachine = sequenceMachine right

-- | Inserts one element at a boundary in @0 .. count@, or reports 'Nothing' for any other
-- boundary. @O(s log n)@, and @O(s)@ amortized at either endpoint.
insertAt :: Int
         -> element
         -> ContextualRankSequence element
         -> Maybe (ContextualRankSequence element)
insertAt position value sequenceValue
  | position < 0 || position > total = Nothing
  | position == 0 = Just (cons value sequenceValue)
  | position == total = Just (snoc sequenceValue value)
  | otherwise =
      case Measured.split (positionReached position) (sequenceTree sequenceValue) of
        Nothing -> error (messagePrefix ++ "a validated boundary missed the measured descent")
        Just (before, found, after) ->
          let machine = sequenceMachine sequenceValue
              !leaf = makeLeaf machine value
          in Just
               (ContextualRankSequence
                  machine
                  (Measured.append
                     (Measured.snoc before leaf)
                     (Measured.cons found after)))
  where
    total = count sequenceValue

-- | Replaces one element, or reports 'Nothing' when the index is outside the sequence.
-- @O(s log n)@.
setAt :: Int
      -> element
      -> ContextualRankSequence element
      -> Maybe (ContextualRankSequence element)
setAt position value sequenceValue
  | position < 0 || position >= count sequenceValue = Nothing
  | otherwise =
      case Measured.split (positionReached position) (sequenceTree sequenceValue) of
        Nothing -> error (messagePrefix ++ "a validated index missed the measured descent")
        Just (before, _, after) ->
          let machine = sequenceMachine sequenceValue
              !leaf = makeLeaf machine value
          in Just
               (ContextualRankSequence machine (Measured.append (Measured.snoc before leaf) after))

-- | Removes one element, or reports 'Nothing' when the index is outside the sequence.
-- @O(s log n)@.
deleteAt :: Int
         -> ContextualRankSequence element
         -> Maybe (ContextualRankSequence element)
deleteAt position sequenceValue
  | position < 0 || position >= count sequenceValue = Nothing
  | otherwise =
      case Measured.split (positionReached position) (sequenceTree sequenceValue) of
        Nothing -> error (messagePrefix ++ "a validated index missed the measured descent")
        Just (before, _, after) ->
          Just
            (ContextualRankSequence
               (sequenceMachine sequenceValue)
               (Measured.append before after))

-- | Splits at a boundary in @0 .. count@, or reports 'Nothing' for any other boundary. Endpoint
-- splits retain the exact receiver as the nonempty half. @O(s log n)@.
splitAt :: Int
        -> ContextualRankSequence element
        -> Maybe (ContextualRankSequence element, ContextualRankSequence element)
splitAt position sequenceValue
  | position < 0 || position > total = Nothing
  | position == 0 = Just (emptyWith machine, sequenceValue)
  | position == total = Just (sequenceValue, emptyWith machine)
  | otherwise =
      case Measured.split (positionReached position) (sequenceTree sequenceValue) of
        Nothing -> error (messagePrefix ++ "a validated boundary missed the measured descent")
        Just (before, found, after) ->
          Just
            ( ContextualRankSequence machine before
            , ContextualRankSequence machine (Measured.cons found after)
            )
  where
    machine = sequenceMachine sequenceValue
    total = count sequenceValue

-- | A contiguous subsequence, or 'Nothing' when the range falls outside the sequence. A range
-- covering everything retains the exact receiver. @O(s log n)@.
slice :: Int
      -> Int
      -> ContextualRankSequence element
      -> Maybe (ContextualRankSequence element)
slice position amount sequenceValue
  | not (validRange position amount total) = Nothing
  | amount == 0 = Just (emptyWith (sequenceMachine sequenceValue))
  | amount == total = Just sequenceValue
  | otherwise = do
      (_, tailPart) <- splitAt position sequenceValue
      (middle, _) <- splitAt amount tailPart
      pure middle
  where
    total = count sequenceValue

-- | Recomputes the cached root summary and every cached leaf summary by direct machine scan and
-- compares them with the tree, reporting the first violation observed.
--
-- A defensive audit; ordinary operations maintain these invariants. Interior spine summaries are
-- built from their children's cached measures, so a corrupted interior cache propagates into the
-- root summary this audit recomputes from scratch. The check invokes machine callbacks @s n@
-- times and therefore carries no production-operation complexity claim. @O(s n)@.
validateStructure :: ContextualRankSequence element -> Either String ValidationStatistics
validateStructure sequenceValue
  | width <= 0 = Left (messagePrefix ++ "a contextual event machine must have at least one state")
  | not (summaryHasWidth width root) =
      Left (messagePrefix ++ "the cached summary does not cover every machine state")
  | summaryCount root /= elementCount =
      Left (messagePrefix ++ "the cached element count disagrees with enumeration")
  | otherwise = do
      mapM_ (validateLeaf machine width) leaves
      totals <- traverse (validateState machine width root values) [0 .. width - 1]
      pure
        ValidationStatistics
          { validationCount = elementCount
          , validationStateCount = width
          , validationMaximumEventCount = maximum (0 : totals)
          }
  where
    machine = sequenceMachine sequenceValue
    width = machineStateCount machine
    root = rootSummary sequenceValue
    leaves = Measured.toList (sequenceTree sequenceValue)
    values = map leafValue leaves
    elementCount = length leaves

-- | Whether two versions share one root, so neither can observe a change made through the other.
--
-- This is a representation test, not an equality test, and is how this package expresses the
-- reference suite's reference-identity assertions about no-op results. It forces only the two
-- roots. @O(1)@.
sharesRootWith :: ContextualRankSequence element -> ContextualRankSequence element -> IO Bool
sharesRootWith left right
  | Measured.null leftTree && Measured.null rightTree = pure True
  | Measured.null leftTree || Measured.null rightTree = pure False
  | otherwise = do
      leftValue <- Exception.evaluate leftTree
      rightValue <- Exception.evaluate rightTree
      leftName <- makeStableName leftValue
      rightName <- makeStableName rightValue
      pure (leftName `eqStableName` rightName)
  where
    leftTree = sequenceTree left
    rightTree = sequenceTree right

validateLeaf :: ContextualEventMachine element -> Int -> Leaf element -> Either String ()
validateLeaf machine width (Leaf value summary)
  | summaryCount summary /= 1 =
      Left (messagePrefix ++ "a leaf summary does not describe exactly one element")
  | not (summaryHasWidth width summary) =
      Left (messagePrefix ++ "a leaf summary does not cover every machine state")
  | otherwise = mapM_ checkOneState [0 .. width - 1]
  where
    checkOneState state
      | next < 0 || next >= width =
          Left (messagePrefix ++ "the machine returned a state outside its declared range")
      | events < 0 =
          Left (messagePrefix ++ "the machine returned a negative event count")
      | summaryFinalState state summary /= next =
          Left (messagePrefix ++ "a cached outgoing state disagrees with a direct scan")
      | summaryEventCount state summary /= events =
          Left (messagePrefix ++ "a cached event count disagrees with a direct scan")
      | otherwise = Right ()
      where
        transition = machineTransition machine state value
        next = transitionNextState transition
        events = transitionEventCount transition

validateState :: ContextualEventMachine element
              -> Int
              -> ContextualSummary
              -> [element]
              -> Int
              -> Either String Integer
validateState machine width root values initialState = do
  (finalState, events) <- scanValues initialState 0 values
  if finalState /= summaryFinalState initialState root
    then Left (messagePrefix ++ "a cached outgoing state disagrees with a direct scan")
    else if events /= summaryEventCount initialState root
      then Left (messagePrefix ++ "a cached event count disagrees with a direct scan")
      else Right events
  where
    scanValues !state !events [] = Right (state, events)
    scanValues !state !events (value : rest)
      | next < 0 || next >= width =
          Left (messagePrefix ++ "the machine returned a state outside its declared range")
      | emitted < 0 = Left (messagePrefix ++ "the machine returned a negative event count")
      | otherwise = scanValues next (events + emitted) rest
      where
        transition = machineTransition machine state value
        next = transitionNextState transition
        emitted = transitionEventCount transition

makeLeaf :: ContextualEventMachine element -> element -> Leaf element
makeLeaf machine value =
  let !summary = elementSummary machine value
  in Leaf value summary

leafValue :: Leaf element -> element
leafValue (Leaf value _) = value

elementSummary :: ContextualEventMachine element -> element -> ContextualSummary
elementSummary machine value =
  let width = checkedStateCount machine
      !effects = buildEffects width oneState
  in ContextualSummary 1 effects
  where
    oneState state =
      let transition = checkedTransition machine state value
      in StateEffect (transitionNextState transition) (transitionEventCount transition)

checkedTransition :: ContextualEventMachine element
                  -> Int
                  -> element
                  -> ContextualTransition
checkedTransition machine state value
  | next < 0 || next >= machineStateCount machine =
      error (messagePrefix ++ "the machine returned a state outside its declared range")
  | events < 0 = error (messagePrefix ++ "the machine returned a negative event count")
  | otherwise = transition
  where
    transition = machineTransition machine state value
    next = transitionNextState transition
    events = transitionEventCount transition

checkedStateCount :: ContextualEventMachine element -> Int
checkedStateCount machine
  | width <= 0 = error (messagePrefix ++ "a contextual event machine must have at least one state")
  | otherwise = width
  where
    width = machineStateCount machine

rootSummary :: ContextualRankSequence element -> ContextualSummary
rootSummary sequenceValue = Measured.measureTree (sequenceTree sequenceValue)

validState :: ContextualRankSequence element -> Int -> Bool
validState sequenceValue state = state >= 0 && state < stateCount sequenceValue

validRange :: Int -> Int -> Int -> Bool
validRange position amount lengthValue =
  position >= 0
    && amount >= 0
    && position <= lengthValue
    && amount <= lengthValue - position

positionReached :: Int -> ContextualSummary -> Bool
positionReached position summary = summaryCount summary > position

eventReached :: Int -> Integer -> ContextualSummary -> Bool
eventReached initialState eventIndex summary =
  summaryEventCount initialState summary > eventIndex

summaryCount :: ContextualSummary -> Int
summaryCount IdentitySummary = 0
summaryCount (ContextualSummary elementCount _) = elementCount

summaryFinalState :: Int -> ContextualSummary -> Int
summaryFinalState initialState IdentitySummary = initialState
summaryFinalState initialState (ContextualSummary _ effects) =
  case effects ! initialState of
    StateEffect next _ -> next

summaryEventCount :: Int -> ContextualSummary -> Integer
summaryEventCount _ IdentitySummary = 0
summaryEventCount initialState (ContextualSummary _ effects) =
  case effects ! initialState of
    StateEffect _ events -> events

summaryEvaluate :: Int -> ContextualSummary -> ContextualPrefixSummary
summaryEvaluate initialState IdentitySummary = ContextualPrefixSummary initialState 0
summaryEvaluate initialState (ContextualSummary _ effects) =
  case effects ! initialState of
    StateEffect next events -> ContextualPrefixSummary next events

summaryHasWidth :: Int -> ContextualSummary -> Bool
summaryHasWidth _ IdentitySummary = True
summaryHasWidth width (ContextualSummary _ effects) = effectWidth effects == width

effectWidth :: Array Int StateEffect -> Int
effectWidth effects = snd (bounds effects) + 1

-- | Builds one effect table and forces every entry, so a cached measure is never a thunk that
-- would defer machine work past the operation that paid for it.
buildEffects :: Int -> (Int -> StateEffect) -> Array Int StateEffect
buildEffects width build =
  let table = listArray (0, width - 1) (map build [0 .. width - 1])
  in List.foldl' (\accumulated effect -> effect `seq` accumulated) table (elems table)

checkedAdd :: Int -> Int -> Int
checkedAdd left right
  | left < 0 || right < 0 = error (messagePrefix ++ "internal negative count")
  | right > maxBound - left = error (messagePrefix ++ "length overflow")
  | otherwise = left + right

messagePrefix :: String
messagePrefix = "Durable7.FingerTree.ContextualRankSequence: "
