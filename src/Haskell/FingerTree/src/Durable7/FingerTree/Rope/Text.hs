module Durable7.FingerTree.Rope.Text
  ( NewlineMeasure(..)
  , TextRope
  , TextRopeCursor
  , TextRopeCursorSearch
  , fromString
  , fromText
  , toString
  , toText
  , lineCount
  , lineOfOffset
  , lineStartOffset
  , lineColumnOf
  , offsetOf
  , getLine
  , lines
  , cursor
  , cursorAt
  , cursorByMeasure
  , searchFound
  , searchCursor
  , cursorCount
  , cursorNull
  , cursorPosition
  , isAtStart
  , isAtEnd
  , measureBefore
  , measureAfter
  , peekPrevious
  , peekNext
  , movePrevious
  , moveNext
  , seek
  , seekByMeasure
  , lineColumnOfCursor
  , insert
  , insertRange
  , deletePrevious
  , deleteNext
  , replaceNext
  , snapshot
  ) where

import Prelude hiding (getLine, lines)

import qualified Data.Text as Text
import qualified Durable7.FingerTree.MeasuredRope as MeasuredRope

newtype NewlineMeasure = NewlineMeasure { getNewlineCount :: Int }
  deriving (Eq, Ord, Read, Show)

instance Semigroup NewlineMeasure where
  NewlineMeasure left <> NewlineMeasure right = NewlineMeasure (left + right)

instance Monoid NewlineMeasure where
  mempty = NewlineMeasure 0

type TextRope = MeasuredRope.MeasuredRope NewlineMeasure Char

-- | The measured snapshot-plus-gap cursor specialized to newline measures.
-- 'TextRope' is a true type alias, so snapshots retain the full text surface
-- without materialization or an additional facade wrapper.
type TextRopeCursor = MeasuredRope.MeasuredRopeCursor NewlineMeasure Char

type TextRopeCursorSearch = MeasuredRope.MeasuredRopeCursorSearch NewlineMeasure Char

fromString :: String -> TextRope
fromString = MeasuredRope.fromListWith measureChar

fromText :: Text.Text -> TextRope
fromText = fromString . Text.unpack

toString :: TextRope -> String
toString = MeasuredRope.toList

toText :: TextRope -> Text.Text
toText = Text.pack . toString

lineCount :: TextRope -> Int
lineCount rope
  | totalNewlines == maxBound =
      error "Durable7.FingerTree.Rope.Text: line count overflow"
  | otherwise = totalNewlines + 1
  where
    totalNewlines = newlineCount rope

lineOfOffset :: Int -> TextRope -> Maybe Int
lineOfOffset offset rope
  | offset < 0 || offset > MeasuredRope.count rope = Nothing
  | otherwise = getNewlineCount <$> MeasuredRope.prefixMeasure offset rope

lineStartOffset :: Int -> TextRope -> Maybe Int
lineStartOffset line rope
  | line < 0 || line > newlineCount rope = Nothing
  | line == 0 = Just 0
  | otherwise = do
      (newlineOffset, _, _) <- MeasuredRope.locateByMeasure
        (\(NewlineMeasure seen) -> seen >= line)
        rope
      pure (newlineOffset + 1)

lineColumnOf :: Int -> TextRope -> Maybe (Int, Int)
lineColumnOf offset rope = do
  line <- lineOfOffset offset rope
  start <- lineStartOffset line rope
  pure (line, offset - start)

offsetOf :: Int -> Int -> TextRope -> Maybe Int
offsetOf line column rope
  | column < 0 = Nothing
  | otherwise = do
      start <- lineStartOffset line rope
      end <- lineEndOffset line rope
      if column <= end - start
        then Just (start + column)
        else Nothing

getLine :: Int -> TextRope -> Maybe String
getLine line rope
  | line < 0 || line > newlineCount rope = Nothing
  | otherwise = do
      start <- lineStartOffset line rope
      end <- lineEndOffset line rope
      segment <- MeasuredRope.slice start (end - start) rope
      pure (MeasuredRope.toList segment)

lines :: TextRope -> [String]
lines rope = map lineAt [0 .. lineCount rope - 1]
  where
    lineAt line =
      case getLine line rope of
        Just value -> value
        Nothing -> error "Durable7.FingerTree.Rope.Text.lines: inconsistent line measure"

cursor :: TextRope -> TextRopeCursor
cursor = MeasuredRope.cursor

cursorAt :: Int -> TextRope -> Maybe TextRopeCursor
cursorAt = MeasuredRope.cursorAt

cursorByMeasure :: (NewlineMeasure -> Bool) -> TextRope -> TextRopeCursorSearch
cursorByMeasure = MeasuredRope.cursorByMeasure

searchFound :: TextRopeCursorSearch -> Bool
searchFound = MeasuredRope.searchFound

searchCursor :: TextRopeCursorSearch -> TextRopeCursor
searchCursor = MeasuredRope.searchCursor

cursorCount :: TextRopeCursor -> Int
cursorCount = MeasuredRope.cursorCount

cursorNull :: TextRopeCursor -> Bool
cursorNull = MeasuredRope.cursorNull

cursorPosition :: TextRopeCursor -> Int
cursorPosition = MeasuredRope.cursorPosition

isAtStart :: TextRopeCursor -> Bool
isAtStart = MeasuredRope.isAtStart

isAtEnd :: TextRopeCursor -> Bool
isAtEnd = MeasuredRope.isAtEnd

measureBefore :: TextRopeCursor -> NewlineMeasure
measureBefore = MeasuredRope.measureBefore

measureAfter :: TextRopeCursor -> NewlineMeasure
measureAfter = MeasuredRope.measureAfter

peekPrevious :: TextRopeCursor -> Maybe Char
peekPrevious = MeasuredRope.peekPrevious

peekNext :: TextRopeCursor -> Maybe Char
peekNext = MeasuredRope.peekNext

movePrevious :: TextRopeCursor -> Maybe TextRopeCursor
movePrevious = MeasuredRope.movePrevious

moveNext :: TextRopeCursor -> Maybe TextRopeCursor
moveNext = MeasuredRope.moveNext

seek :: Int -> TextRopeCursor -> Maybe TextRopeCursor
seek = MeasuredRope.seek

seekByMeasure :: (NewlineMeasure -> Bool) -> TextRopeCursor -> TextRopeCursorSearch
seekByMeasure = MeasuredRope.seekByMeasure

-- | Returns the zero-based line and 'Char'-element column at the cursor gap.
lineColumnOfCursor :: TextRopeCursor -> (Int, Int)
lineColumnOfCursor cursorValue =
  case lineColumnOf (cursorPosition cursorValue) (snapshot cursorValue) of
    Just value -> value
    Nothing -> error "Durable7.FingerTree.Rope.Text.lineColumnOfCursor: invalid cursor invariant"

insert :: Char -> TextRopeCursor -> TextRopeCursor
insert = MeasuredRope.insert

insertRange :: String -> TextRopeCursor -> TextRopeCursor
insertRange = MeasuredRope.insertRange

deletePrevious :: TextRopeCursor -> Maybe TextRopeCursor
deletePrevious = MeasuredRope.deletePrevious

deleteNext :: TextRopeCursor -> Maybe TextRopeCursor
deleteNext = MeasuredRope.deleteNext

replaceNext :: Char -> TextRopeCursor -> Maybe TextRopeCursor
replaceNext = MeasuredRope.replaceNext

snapshot :: TextRopeCursor -> TextRope
snapshot = MeasuredRope.snapshot

measureChar :: Char -> NewlineMeasure
measureChar '\n' = NewlineMeasure 1
measureChar _ = NewlineMeasure 0

newlineCount :: TextRope -> Int
newlineCount rope =
  case MeasuredRope.measure rope of
    NewlineMeasure value -> value

lineEndOffset :: Int -> TextRope -> Maybe Int
lineEndOffset line rope
  | line < 0 || line > totalNewlines = Nothing
  | line < totalNewlines = subtract 1 <$> lineStartOffset (line + 1) rope
  | otherwise = Just (MeasuredRope.count rope)
  where
    totalNewlines = newlineCount rope
