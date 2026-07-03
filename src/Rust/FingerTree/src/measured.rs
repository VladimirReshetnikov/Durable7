use std::marker::PhantomData;
use std::ops::Add;
use std::sync::Arc;

pub trait MeasurePolicy<T> {
    type Measure: Clone;

    fn empty() -> Self::Measure;
    fn measure(element: &T) -> Self::Measure;
    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure;
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SizeMeasure;

impl<T> MeasurePolicy<T> for SizeMeasure {
    type Measure = usize;

    fn empty() -> Self::Measure {
        0
    }

    fn measure(_element: &T) -> Self::Measure {
        1
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left + right
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SumMeasure<T>(PhantomData<T>);

impl<T> MeasurePolicy<T> for SumMeasure<T>
where
    T: Add<Output = T> + Clone + Default,
{
    type Measure = T;

    fn empty() -> Self::Measure {
        T::default()
    }

    fn measure(element: &T) -> Self::Measure {
        element.clone()
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left.clone() + right.clone()
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MaxMeasure;

impl<T> MeasurePolicy<T> for MaxMeasure
where
    T: Clone + Ord,
{
    type Measure = Option<T>;

    fn empty() -> Self::Measure {
        None
    }

    fn measure(element: &T) -> Self::Measure {
        Some(element.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        match (left, right) {
            (Some(left), Some(right)) => Some(left.max(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MinMeasure;

impl<T> MeasurePolicy<T> for MinMeasure
where
    T: Clone + Ord,
{
    type Measure = Option<T>;

    fn empty() -> Self::Measure {
        None
    }

    fn measure(element: &T) -> Self::Measure {
        Some(element.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        match (left, right) {
            (Some(left), Some(right)) => Some(left.min(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        }
    }
}

pub struct FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    items: Arc<Vec<T>>,
    measure: P::Measure,
    _policy: PhantomData<P>,
}

#[derive(Clone)]
pub struct MeasuredSplit<T, P>
where
    P: MeasurePolicy<T>,
{
    pub left: FingerTree<T, P>,
    pub right: FingerTree<T, P>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LocateResult<T, M> {
    pub index: usize,
    pub measure_before: M,
    pub item: Option<T>,
}

impl<T, P> Clone for FingerTree<T, P>
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

impl<T, P> FingerTree<T, P>
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
    pub fn front(&self) -> Option<&T> {
        self.items.first()
    }

    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.items.last()
    }

    pub fn iter(&self) -> std::slice::Iter<'_, T> {
        self.items.iter()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.items, &other.items)
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
}

impl<T, P> Default for FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, P> FromIterator<T> for FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T, P> FingerTree<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.as_ref().clone()
    }

    #[must_use]
    pub fn prepend(&self, item: T) -> Self {
        let mut next = Vec::with_capacity(self.len() + 1);
        next.push(item);
        next.extend(self.items.iter().cloned());
        Self::from_vec(next)
    }

    #[must_use]
    pub fn append(&self, item: T) -> Self {
        let mut next = self.to_vec();
        next.push(item);
        Self::from_vec(next)
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
    pub fn try_view_left(&self) -> Option<(T, Self)> {
        let first = self.front()?.clone();
        Some((first, Self::from_vec(self.items[1..].to_vec())))
    }

    #[must_use]
    pub fn try_view_right(&self) -> Option<(T, Self)> {
        let last = self.back()?.clone();
        Some((last, Self::from_vec(self.items[..self.len() - 1].to_vec())))
    }

    #[must_use]
    pub fn split<F>(&self, predicate: F) -> MeasuredSplit<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let index = self.boundary_index(predicate).unwrap_or(self.len());
        MeasuredSplit {
            left: Self::from_vec(self.items[..index].to_vec()),
            right: Self::from_vec(self.items[index..].to_vec()),
        }
    }

    #[must_use]
    pub fn try_split_find<F>(&self, predicate: F) -> Option<(Self, T, Self)>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let index = self.boundary_index(predicate)?;
        Some((
            Self::from_vec(self.items[..index].to_vec()),
            self.items[index].clone(),
            Self::from_vec(self.items[index + 1..].to_vec()),
        ))
    }

    #[must_use]
    pub fn try_locate<F>(&self, mut predicate: F) -> LocateResult<T, P::Measure>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let mut before = P::empty();
        for (index, item) in self.items.iter().enumerate() {
            let next = P::combine(&before, &P::measure(item));
            if predicate(&next) {
                return LocateResult {
                    index,
                    measure_before: before,
                    item: Some(item.clone()),
                };
            }

            before = next;
        }

        LocateResult {
            index: self.len(),
            measure_before: before,
            item: None,
        }
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

impl<T> FingerTree<T, SizeMeasure>
where
    T: Clone,
{
    #[must_use]
    pub fn split_at_index(&self, index: usize) -> Option<MeasuredSplit<T, SizeMeasure>> {
        if index > self.len() {
            return None;
        }

        Some(MeasuredSplit {
            left: Self::from_vec(self.items[..index].to_vec()),
            right: Self::from_vec(self.items[index..].to_vec()),
        })
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn size_measure_tracks_count() {
        let tree: FingerTree<_, SizeMeasure> = [10, 20, 30].into_iter().collect();
        let appended = tree.append(40);

        assert_eq!(*tree.measure(), 3);
        assert_eq!(*appended.measure(), 4);
        assert_eq!(tree.to_vec(), vec![10, 20, 30]);
    }

    #[test]
    fn split_and_locate_use_prefix_measure() {
        let tree: FingerTree<_, SumMeasure<i32>> = [2, 3, 5, 7].into_iter().collect();
        let split = tree.split(|sum| *sum >= 6);
        let located = tree.try_locate(|sum| *sum >= 6);

        assert_eq!(split.left.to_vec(), vec![2, 3]);
        assert_eq!(split.right.to_vec(), vec![5, 7]);
        assert_eq!(located.index, 2);
        assert_eq!(located.measure_before, 5);
        assert_eq!(located.item, Some(5));
    }

    #[test]
    fn max_and_min_measures_are_monoids() {
        let max_tree: FingerTree<_, MaxMeasure> = [3, 1, 4, 2].into_iter().collect();
        let min_tree: FingerTree<_, MinMeasure> = [3, 1, 4, 2].into_iter().collect();

        assert_eq!(max_tree.measure(), &Some(4));
        assert_eq!(min_tree.measure(), &Some(1));
    }
}
