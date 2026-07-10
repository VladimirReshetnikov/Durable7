use crate::{Rope, TextRope};
use unicode_segmentation::UnicodeSegmentation;

/// The newline convention or conventions present in a character rope.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum NewlineStyle {
    /// No carriage return or line feed occurs.
    #[default]
    None,
    /// Every line break is a lone line feed (`\n`).
    Lf,
    /// Every line break is a carriage-return/line-feed pair (`\r\n`).
    CrLf,
    /// Every line break is a lone carriage return (`\r`).
    Cr,
    /// More than one newline convention occurs.
    Mixed,
}

impl Rope<char> {
    /// Returns the number of Unicode scalar values.
    ///
    /// Rust `char` is already a Unicode scalar value, so this is the rope's O(1) character
    /// length rather than a UTF-16 surrogate-pair scan.
    #[must_use]
    pub fn code_point_count(&self) -> usize {
        self.len()
    }

    /// Streams the rope's Unicode scalar values without materializing a string.
    pub fn code_points(&self) -> impl Iterator<Item = char> + '_ {
        self.iter().copied()
    }

    /// Counts extended grapheme clusters according to Unicode UAX #29.
    ///
    /// This materializes the rope because a grapheme boundary can depend on surrounding scalar
    /// values across chunk boundaries.
    #[must_use]
    pub fn grapheme_count(&self) -> usize {
        self.as_text().graphemes(true).count()
    }

    /// Returns the extended grapheme clusters according to Unicode UAX #29.
    ///
    /// Both the text and returned clusters are owned so no result borrows temporary materialized
    /// storage.
    #[must_use]
    pub fn graphemes(&self) -> Vec<String> {
        self.as_text().graphemes(true).map(str::to_owned).collect()
    }

    /// Classifies lone LF, CRLF, lone CR, mixed, and absent newline conventions.
    #[must_use]
    pub fn detect_newline_style(&self) -> NewlineStyle {
        detect_newline_style(self.iter().copied())
    }

    /// Maps a code-point index to a character offset, accepting the end boundary.
    ///
    /// Rust rope character offsets and code-point indexes are both scalar-value indexes.
    #[must_use]
    pub fn code_point_index_to_char_offset(&self, code_point_index: usize) -> Option<usize> {
        (code_point_index <= self.len()).then_some(code_point_index)
    }

    /// Maps a character offset to a code-point index, accepting the end boundary.
    #[must_use]
    pub fn char_offset_to_code_point_index(&self, char_offset: usize) -> Option<usize> {
        (char_offset <= self.len()).then_some(char_offset)
    }

    /// Returns the character offset at which a grapheme begins, accepting the end boundary.
    #[must_use]
    pub fn grapheme_index_to_char_offset(&self, grapheme_index: usize) -> Option<usize> {
        grapheme_index_to_char_offset(&self.as_text(), grapheme_index)
    }

    /// Returns the number of grapheme-cluster starts strictly before a character offset.
    ///
    /// An exact cluster boundary maps to that cluster's index; an offset inside a cluster counts
    /// that cluster's start. The end boundary is accepted.
    #[must_use]
    pub fn char_offset_to_grapheme_index(&self, char_offset: usize) -> Option<usize> {
        char_offset_to_grapheme_index(&self.as_text(), char_offset, self.len())
    }

    fn as_text(&self) -> String {
        self.iter().copied().collect()
    }
}

impl TextRope {
    /// Returns the number of Unicode scalar values. Rust character offsets already count scalars.
    #[must_use]
    pub fn code_point_count(&self) -> usize {
        self.len()
    }

    /// Streams the rope's Unicode scalar values without materializing a string.
    pub fn code_points(&self) -> impl Iterator<Item = char> + '_ {
        self.iter().copied()
    }

    /// Counts extended grapheme clusters according to Unicode UAX #29.
    #[must_use]
    pub fn grapheme_count(&self) -> usize {
        self.as_string().graphemes(true).count()
    }

    /// Returns owned extended grapheme clusters according to Unicode UAX #29.
    #[must_use]
    pub fn graphemes(&self) -> Vec<String> {
        self.as_string()
            .graphemes(true)
            .map(str::to_owned)
            .collect()
    }

    /// Classifies lone LF, CRLF, lone CR, mixed, and absent newline conventions.
    #[must_use]
    pub fn detect_newline_style(&self) -> NewlineStyle {
        detect_newline_style(self.iter().copied())
    }

    /// Returns line text without the carriage return preceding a line feed.
    #[must_use]
    pub fn get_line_text(&self, line: usize) -> Option<String> {
        self.get_line(line).map(trim_trailing_carriage_return)
    }

    /// Returns all LF-delimited lines with a trailing carriage return removed from each.
    #[must_use]
    pub fn lines_text(&self) -> Vec<String> {
        self.lines()
            .into_iter()
            .map(trim_trailing_carriage_return)
            .collect()
    }

    /// Maps a code-point index to a character offset, accepting the end boundary.
    #[must_use]
    pub fn code_point_index_to_char_offset(&self, code_point_index: usize) -> Option<usize> {
        (code_point_index <= self.len()).then_some(code_point_index)
    }

    /// Maps a character offset to a code-point index, accepting the end boundary.
    #[must_use]
    pub fn char_offset_to_code_point_index(&self, char_offset: usize) -> Option<usize> {
        (char_offset <= self.len()).then_some(char_offset)
    }

    /// Returns the character offset at which a grapheme begins, accepting the end boundary.
    #[must_use]
    pub fn grapheme_index_to_char_offset(&self, grapheme_index: usize) -> Option<usize> {
        grapheme_index_to_char_offset(&self.as_string(), grapheme_index)
    }

    /// Returns the number of grapheme-cluster starts strictly before a character offset.
    ///
    /// An exact cluster boundary maps to that cluster's index; an offset inside a cluster counts
    /// that cluster's start. The end boundary is accepted.
    #[must_use]
    pub fn char_offset_to_grapheme_index(&self, char_offset: usize) -> Option<usize> {
        char_offset_to_grapheme_index(&self.as_string(), char_offset, self.len())
    }
}

fn grapheme_index_to_char_offset(text: &str, grapheme_index: usize) -> Option<usize> {
    let starts = grapheme_char_starts(text);
    if grapheme_index > starts.len() {
        return None;
    }

    Some(
        starts
            .get(grapheme_index)
            .copied()
            .unwrap_or_else(|| text.chars().count()),
    )
}

fn char_offset_to_grapheme_index(
    text: &str,
    char_offset: usize,
    char_count: usize,
) -> Option<usize> {
    if char_offset > char_count {
        return None;
    }

    let starts = grapheme_char_starts(text);
    Some(
        starts
            .binary_search(&char_offset)
            .unwrap_or_else(|index| index),
    )
}

fn grapheme_char_starts(text: &str) -> Vec<usize> {
    let mut starts = Vec::new();
    let mut previous_byte = 0;
    let mut char_offset = 0;
    for (byte_offset, _) in text.grapheme_indices(true) {
        char_offset += text[previous_byte..byte_offset].chars().count();
        starts.push(char_offset);
        previous_byte = byte_offset;
    }
    starts
}

fn detect_newline_style(characters: impl IntoIterator<Item = char>) -> NewlineStyle {
    let mut seen = NewlineStyle::None;
    let mut pending_cr = false;

    for character in characters {
        if pending_cr {
            pending_cr = false;
            if character == '\n' {
                seen = combine_newline_styles(seen, NewlineStyle::CrLf);
                continue;
            }
            seen = combine_newline_styles(seen, NewlineStyle::Cr);
        }

        if character == '\r' {
            pending_cr = true;
        } else if character == '\n' {
            seen = combine_newline_styles(seen, NewlineStyle::Lf);
        }
    }

    if pending_cr {
        seen = combine_newline_styles(seen, NewlineStyle::Cr);
    }
    seen
}

fn combine_newline_styles(existing: NewlineStyle, found: NewlineStyle) -> NewlineStyle {
    if existing == NewlineStyle::None {
        found
    } else if existing == found {
        existing
    } else {
        NewlineStyle::Mixed
    }
}

fn trim_trailing_carriage_return(mut line: String) -> String {
    if line.ends_with('\r') {
        line.pop();
    }
    line
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rust_character_offsets_are_unicode_scalar_offsets() {
        let rope = Rope::from("a\u{301}😀z");
        let text = TextRope::from("a\u{301}😀z");

        assert_eq!(rope.code_point_count(), 4);
        assert_eq!(
            rope.code_points().collect::<Vec<_>>(),
            vec!['a', '\u{301}', '😀', 'z']
        );
        assert_eq!(rope.code_point_index_to_char_offset(4), Some(4));
        assert_eq!(rope.char_offset_to_code_point_index(4), Some(4));
        assert_eq!(rope.code_point_index_to_char_offset(5), None);
        assert_eq!(text.code_points().collect::<String>(), "a\u{301}😀z");
        assert_eq!(text.char_offset_to_code_point_index(5), None);
    }

    #[test]
    fn grapheme_operations_follow_extended_uax_29_boundaries() {
        let rope = Rope::from("a\u{301}x😀🇺🇸z");
        let text = TextRope::from("a\u{301}x😀🇺🇸z");

        assert_eq!(rope.graphemes(), vec!["a\u{301}", "x", "😀", "🇺🇸", "z"]);
        assert_eq!(rope.grapheme_count(), 5);
        assert_eq!(text.graphemes(), rope.graphemes());
        assert_eq!(text.grapheme_count(), 5);

        let starts = [0, 2, 3, 4, 6, 7];
        for (index, offset) in starts.into_iter().enumerate() {
            assert_eq!(rope.grapheme_index_to_char_offset(index), Some(offset));
            assert_eq!(rope.char_offset_to_grapheme_index(offset), Some(index));
            assert_eq!(text.grapheme_index_to_char_offset(index), Some(offset));
        }

        assert_eq!(rope.char_offset_to_grapheme_index(1), Some(1));
        assert_eq!(rope.char_offset_to_grapheme_index(5), Some(4));
        assert_eq!(rope.grapheme_index_to_char_offset(6), None);
        assert_eq!(rope.char_offset_to_grapheme_index(8), None);

        let empty = Rope::from("");
        assert_eq!(empty.grapheme_count(), 0);
        assert_eq!(empty.grapheme_index_to_char_offset(0), Some(0));
        assert_eq!(empty.char_offset_to_grapheme_index(0), Some(0));
        assert_eq!(TextRope::from("").grapheme_index_to_char_offset(0), Some(0));
    }

    #[test]
    fn newline_detection_distinguishes_lf_crlf_cr_and_mixed_text() {
        let cases = [
            ("", NewlineStyle::None),
            ("plain", NewlineStyle::None),
            ("a\nb\n", NewlineStyle::Lf),
            ("a\r\nb\r\n", NewlineStyle::CrLf),
            ("a\rb\r", NewlineStyle::Cr),
            ("a\r\nb\nc", NewlineStyle::Mixed),
            ("a\nb\rc", NewlineStyle::Mixed),
        ];

        for (value, expected) in cases {
            assert_eq!(Rope::from(value).detect_newline_style(), expected);
            assert_eq!(TextRope::from(value).detect_newline_style(), expected);
        }
    }

    #[test]
    fn crlf_aware_line_text_removes_only_the_trailing_carriage_return() {
        let text = TextRope::from("first\r\nsecond\r\nthird\r");

        assert_eq!(text.get_line(0).as_deref(), Some("first\r"));
        assert_eq!(text.get_line_text(0).as_deref(), Some("first"));
        assert_eq!(text.get_line_text(1).as_deref(), Some("second"));
        assert_eq!(text.get_line_text(2).as_deref(), Some("third"));
        assert_eq!(text.lines_text(), vec!["first", "second", "third"]);
        assert_eq!(text.get_line_text(3), None);

        let edges = TextRope::from("\r\nbody\r\n");
        assert_eq!(edges.line_count(), 3);
        assert_eq!(edges.lines_text(), vec!["", "body", ""]);
        assert_eq!(edges.get_line_text(0).as_deref(), Some(""));
        assert_eq!(edges.get_line_text(2).as_deref(), Some(""));
        assert_eq!(edges.detect_newline_style(), NewlineStyle::CrLf);
    }
}
