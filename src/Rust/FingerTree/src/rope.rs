use crate::measured::MeasurePolicy;
use std::marker::PhantomData;
use std::sync::Arc;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Rope<T> {
    items: Arc<Vec<T>>,
}

impl<T> Rope<T> {
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: Arc::new(Vec::new()),
        }
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self {
            items: Arc::new(items),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.items.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    #[must_use]
    pub fn front(&self) -> Option<&T> {
        self.items.first()
    }

    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.items.last()
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        self.items.get(index)
    }

    pub fn iter(&self) -> std::slice::Iter<'_, T> {
        self.items.iter()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.items, &other.items)
    }
}

impl<T> Default for Rope<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for Rope<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T> Rope<T>
where
    T: Clone,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.as_ref().clone()
    }

    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        let mut next = Vec::with_capacity(self.len() + 1);
        next.push(item);
        next.extend(self.items.iter().cloned());
        Self::from_vec(next)
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        let mut next = self.to_vec();
        next.push(item);
        Self::from_vec(next)
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next[index] = item;
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next.insert(index, item);
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn insert_range<I>(&self, index: usize, items: I) -> Option<Self>
    where
        I: IntoIterator<Item = T>,
    {
        if index > self.len() {
            return None;
        }

        let inserted: Vec<T> = items.into_iter().collect();
        let mut next = Vec::with_capacity(self.len() + inserted.len());
        next.extend(self.items[..index].iter().cloned());
        next.extend(inserted);
        next.extend(self.items[index..].iter().cloned());
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next.remove(index);
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn remove_range(&self, index: usize, count: usize) -> Option<Self> {
        let end = checked_range_end(self.len(), index, count)?;
        let mut next = Vec::with_capacity(self.len() - count);
        next.extend(self.items[..index].iter().cloned());
        next.extend(self.items[end..].iter().cloned());
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn slice(&self, index: usize, count: usize) -> Option<Self> {
        let end = checked_range_end(self.len(), index, count)?;
        Some(Self::from_vec(self.items[index..end].to_vec()))
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<(Self, Self)> {
        if index > self.len() {
            return None;
        }

        Some((
            Self::from_vec(self.items[..index].to_vec()),
            Self::from_vec(self.items[index..].to_vec()),
        ))
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        if self.is_empty() {
            return other.clone();
        }

        if other.is_empty() {
            return self.clone();
        }

        let mut next = Vec::with_capacity(self.len() + other.len());
        next.extend(self.items.iter().cloned());
        next.extend(other.items.iter().cloned());
        Self::from_vec(next)
    }

    #[must_use]
    pub fn compact(&self) -> Self {
        Self::from_vec(self.to_vec())
    }
}

pub struct MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    items: Arc<Vec<T>>,
    measure: P::Measure,
    _policy: PhantomData<P>,
}

#[derive(Clone)]
pub struct MeasuredRopeSplit<T, P>
where
    P: MeasurePolicy<T>,
{
    pub left: MeasuredRope<T, P>,
    pub right: MeasuredRope<T, P>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MeasuredRopeLocate<T, M> {
    pub index: usize,
    pub measure_before: M,
    pub value: Option<T>,
}

impl<T, P> Clone for MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        Self {
            items: Arc::clone(&self.items),
            measure: self.measure.clone(),
            _policy: PhantomData,
        }
    }
}

impl<T, P> MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: Arc::new(Vec::new()),
            measure: P::empty(),
            _policy: PhantomData,
        }
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        let measure = measure_slice::<T, P>(&items);
        Self {
            items: Arc::new(items),
            measure,
            _policy: PhantomData,
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.items.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    #[must_use]
    pub fn measure(&self) -> &P::Measure {
        &self.measure
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        self.items.get(index)
    }
}

impl<T, P> Default for MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, P> FromIterator<T> for MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T, P> MeasuredRope<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.as_ref().clone()
    }

    #[must_use]
    pub fn prefix_measure(&self, count: usize) -> Option<P::Measure> {
        if count > self.len() {
            return None;
        }

        Some(measure_slice::<T, P>(&self.items[..count]))
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        let mut next = self.to_vec();
        next.push(item);
        Self::from_vec(next)
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next[index] = item;
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<MeasuredRopeSplit<T, P>> {
        if index > self.len() {
            return None;
        }

        Some(MeasuredRopeSplit {
            left: Self::from_vec(self.items[..index].to_vec()),
            right: Self::from_vec(self.items[index..].to_vec()),
        })
    }

    #[must_use]
    pub fn split_by_measure<F>(&self, predicate: F) -> MeasuredRopeSplit<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let index = self.boundary_index(predicate).unwrap_or(self.len());
        MeasuredRopeSplit {
            left: Self::from_vec(self.items[..index].to_vec()),
            right: Self::from_vec(self.items[index..].to_vec()),
        }
    }

    #[must_use]
    pub fn locate_by_measure<F>(&self, mut predicate: F) -> MeasuredRopeLocate<T, P::Measure>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let mut before = P::empty();
        for (index, item) in self.items.iter().enumerate() {
            let next = P::combine(&before, &P::measure(item));
            if predicate(&next) {
                return MeasuredRopeLocate {
                    index,
                    measure_before: before,
                    value: Some(item.clone()),
                };
            }

            before = next;
        }

        MeasuredRopeLocate {
            index: self.len(),
            measure_before: before,
            value: None,
        }
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        let mut next = self.to_vec();
        next.extend(other.items.iter().cloned());
        Self::from_vec(next)
    }

    fn boundary_index<F>(&self, mut predicate: F) -> Option<usize>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let mut prefix = P::empty();
        for (index, item) in self.items.iter().enumerate() {
            prefix = P::combine(&prefix, &P::measure(item));
            if predicate(&prefix) {
                return Some(index);
            }
        }

        None
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NewlineMeasure;

impl MeasurePolicy<char> for NewlineMeasure {
    type Measure = usize;

    fn empty() -> Self::Measure {
        0
    }

    fn measure(element: &char) -> Self::Measure {
        usize::from(*element == '\n')
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left + right
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TextRope {
    chars: Rope<char>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LineColumn {
    pub line: usize,
    pub column: usize,
}

impl TextRope {
    #[must_use]
    pub fn new() -> Self {
        Self { chars: Rope::new() }
    }

    #[must_use]
    pub fn from_text(text: &str) -> Self {
        Self {
            chars: text.chars().collect(),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.chars.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.chars.is_empty()
    }

    #[must_use]
    pub fn as_string(&self) -> String {
        self.chars.iter().collect()
    }

    #[must_use]
    pub fn line_count(&self) -> usize {
        self.chars.iter().filter(|ch| **ch == '\n').count() + 1
    }

    #[must_use]
    pub fn line_of_offset(&self, offset: usize) -> Option<usize> {
        if offset > self.len() {
            return None;
        }

        Some(
            self.chars.items[..offset]
                .iter()
                .filter(|ch| **ch == '\n')
                .count(),
        )
    }

    #[must_use]
    pub fn line_start_offset(&self, line: usize) -> Option<usize> {
        if line == 0 {
            return Some(0);
        }

        let mut seen = 0;
        for (index, ch) in self.chars.iter().enumerate() {
            if *ch == '\n' {
                seen += 1;
                if seen == line {
                    return Some(index + 1);
                }
            }
        }

        None
    }

    #[must_use]
    pub fn line_column_of(&self, offset: usize) -> Option<LineColumn> {
        let line = self.line_of_offset(offset)?;
        let start = self.line_start_offset(line)?;
        Some(LineColumn {
            line,
            column: offset - start,
        })
    }

    #[must_use]
    pub fn offset_of(&self, line: usize, column: usize) -> Option<usize> {
        let start = self.line_start_offset(line)?;
        let end = self.line_end_offset(line)?;
        let offset = start.checked_add(column)?;
        (offset <= end).then_some(offset)
    }

    #[must_use]
    pub fn get_line(&self, line: usize) -> Option<String> {
        let start = self.line_start_offset(line)?;
        let end = self.line_end_offset(line)?;
        Some(self.chars.items[start..end].iter().collect())
    }

    #[must_use]
    pub fn lines(&self) -> Vec<String> {
        (0..self.line_count())
            .filter_map(|line| self.get_line(line))
            .collect()
    }

    #[must_use]
    pub fn to_char_rope(&self) -> Rope<char> {
        self.chars.clone()
    }

    #[must_use]
    pub fn to_measured_rope(&self) -> MeasuredRope<char, NewlineMeasure> {
        self.chars.iter().cloned().collect()
    }

    fn line_end_offset(&self, line: usize) -> Option<usize> {
        let start = self.line_start_offset(line)?;
        for (relative, ch) in self.chars.items[start..].iter().enumerate() {
            if *ch == '\n' {
                return Some(start + relative);
            }
        }

        Some(self.len())
    }
}

impl Default for TextRope {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RopeBuilder {
    text: String,
}

impl RopeBuilder {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn append(&mut self, text: &str) -> &mut Self {
        self.text.push_str(text);
        self
    }

    pub fn append_line(&mut self, text: &str) -> &mut Self {
        self.text.push_str(text);
        self.text.push('\n');
        self
    }

    pub fn clear(&mut self) -> &mut Self {
        self.text.clear();
        self
    }

    #[must_use]
    pub fn to_rope(&self) -> Rope<char> {
        self.text.chars().collect()
    }

    #[must_use]
    pub fn to_text_rope(&self) -> TextRope {
        TextRope::from_text(&self.text)
    }
}

fn measure_slice<T, P>(items: &[T]) -> P::Measure
where
    P: MeasurePolicy<T>,
{
    items.iter().fold(P::empty(), |measure, item| {
        P::combine(&measure, &P::measure(item))
    })
}

fn checked_range_end(len: usize, index: usize, count: usize) -> Option<usize> {
    let end = index.checked_add(count)?;
    (index <= len && end <= len).then_some(end)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::measured::SumMeasure;

    #[test]
    fn rope_edits_preserve_snapshots() {
        let rope: Rope<_> = [1, 2, 3].into_iter().collect();
        let edited = rope.insert_at(1, 9).unwrap().remove_at(3).unwrap();

        assert_eq!(rope.to_vec(), vec![1, 2, 3]);
        assert_eq!(edited.to_vec(), vec![1, 9, 2]);
        assert_eq!(rope.slice(1, 2).unwrap().to_vec(), vec![2, 3]);
    }

    #[test]
    fn measured_rope_locates_by_user_measure() {
        let rope: MeasuredRope<_, SumMeasure<i32>> = [2, 4, 8].into_iter().collect();
        let located = rope.locate_by_measure(|sum| *sum > 5);

        assert_eq!(rope.measure(), &14);
        assert_eq!(rope.prefix_measure(2), Some(6));
        assert_eq!(located.index, 1);
        assert_eq!(located.measure_before, 2);
        assert_eq!(located.value, Some(4));
    }

    #[test]
    fn text_rope_uses_character_offsets_and_editor_line_count() {
        let text = TextRope::from_text("alpha\nbeta\n");

        assert_eq!(text.line_count(), 3);
        assert_eq!(
            text.line_column_of(7),
            Some(LineColumn { line: 1, column: 1 })
        );
        assert_eq!(text.offset_of(1, 2), Some(8));
        assert_eq!(text.get_line(0).unwrap(), "alpha");
        assert_eq!(text.get_line(2).unwrap(), "");
        assert_eq!(text.lines(), vec!["alpha", "beta", ""]);
    }

    #[test]
    fn builder_creates_char_and_text_ropes() {
        let mut builder = RopeBuilder::new();
        builder.append("hello").append_line(" world");

        assert_eq!(builder.to_rope().to_vec().len(), 12);
        assert_eq!(builder.to_text_rope().as_string(), "hello world\n");
    }
}
