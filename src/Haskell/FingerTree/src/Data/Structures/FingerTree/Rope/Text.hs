module Data.Structures.FingerTree.Rope.Text
  ( NewlineMeasure(..)
  , TextRope
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
  ) where

import Prelude hiding (getLine, lines)

import qualified Data.Text as Text
import qualified Data.Structures.FingerTree.MeasuredRope as MeasuredRope

newtype NewlineMeasure = NewlineMeasure { getNewlineCount :: Int }
  deriving (Eq, Ord, Read, Show)

instance Semigroup NewlineMeasure where
  NewlineMeasure left <> NewlineMeasure right = NewlineMeasure (left + right)

instance Monoid NewlineMeasure where
  mempty = NewlineMeasure 0

type TextRope = MeasuredRope.MeasuredRope NewlineMeasure Char

fromString :: String -> TextRope
fromString = MeasuredRope.fromListWith measureChar

fromText :: Text.Text -> TextRope
fromText = fromString . Text.unpack

toString :: TextRope -> String
toString = MeasuredRope.toList

toText :: TextRope -> Text.Text
toText = Text.pack . toString

lineCount :: TextRope -> Int
lineCount rope =
  case MeasuredRope.measure rope of
    NewlineMeasure newlineCount -> newlineCount + 1

lineOfOffset :: Int -> TextRope -> Maybe Int
lineOfOffset offset rope
  | offset < 0 || offset > MeasuredRope.count rope = Nothing
  | otherwise = Just (length (filter (== '\n') (take offset (toString rope))))

lineStartOffset :: Int -> TextRope -> Maybe Int
lineStartOffset line rope
  | line < 0 || line >= lineCount rope = Nothing
  | line == 0 = Just 0
  | otherwise = findStart line 0 0 (toString rope)

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
      lineText <- getLine line rope
      if column <= length lineText
        then Just (start + column)
        else Nothing

getLine :: Int -> TextRope -> Maybe String
getLine line rope
  | line < 0 || line >= length allLines = Nothing
  | otherwise = Just (allLines !! line)
  where
    allLines = lines rope

lines :: TextRope -> [String]
lines = splitLines . toString

measureChar :: Char -> NewlineMeasure
measureChar '\n' = NewlineMeasure 1
measureChar _ = NewlineMeasure 0

findStart :: Int -> Int -> Int -> String -> Maybe Int
findStart _ _ _ [] = Nothing
findStart targetLine currentLine offset (c : rest)
  | c == '\n' && currentLine + 1 == targetLine = Just (offset + 1)
  | c == '\n' = findStart targetLine (currentLine + 1) (offset + 1) rest
  | otherwise = findStart targetLine currentLine (offset + 1) rest

splitLines :: String -> [String]
splitLines [] = [""]
splitLines text =
  case break (== '\n') text of
    (line, []) -> [line]
    (line, _ : rest) -> line : splitLines rest
