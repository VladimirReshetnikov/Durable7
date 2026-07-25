use std::cell::Cell;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::Arc;
use std::thread;

use durable7_range_update::{
    MeasurePolicy, RangeUpdateAlgebra, RangeUpdateError, RangeUpdateSequence,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct AffineTag {
    assignment: Option<i64>,
    addition: i64,
    identity_witness: u8,
}

impl AffineTag {
    const fn add(delta: i64) -> Self {
        Self {
            assignment: None,
            addition: delta,
            identity_witness: 0,
        }
    }

    const fn assign(value: i64) -> Self {
        Self {
            assignment: Some(value),
            addition: 0,
            identity_witness: 0,
        }
    }

    const fn alternate_identity() -> Self {
        Self {
            assignment: None,
            addition: 0,
            identity_witness: 1,
        }
    }
}

#[derive(Debug)]
struct AffineSum;

impl MeasurePolicy<i64> for AffineSum {
    type Measure = i64;

    fn empty() -> Self::Measure {
        0
    }

    fn measure(element: &i64) -> Self::Measure {
        *element
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left.checked_add(*right).expect("test sum overflow")
    }
}

impl RangeUpdateAlgebra<i64> for AffineSum {
    type Tag = AffineTag;

    fn identity_tag() -> Self::Tag {
        AffineTag::add(0)
    }

    fn is_identity(tag: &Self::Tag) -> bool {
        // Distinct witness values deliberately share identity semantics.
        let _representation_witness = tag.identity_witness;
        tag.assignment.is_none() && tag.addition == 0
    }

    fn compose(newer: &Self::Tag, older: &Self::Tag) -> Self::Tag {
        if Self::is_identity(newer) {
            return *older;
        }
        if Self::is_identity(older) {
            return *newer;
        }
        if newer.assignment.is_some() {
            return *newer;
        }
        match older.assignment {
            Some(value) => AffineTag::assign(
                value
                    .checked_add(older.addition)
                    .and_then(|value| value.checked_add(newer.addition))
                    .expect("test tag overflow"),
            ),
            None => AffineTag::add(
                older
                    .addition
                    .checked_add(newer.addition)
                    .expect("test tag overflow"),
            ),
        }
    }

    fn apply_element(tag: &Self::Tag, element: &i64) -> i64 {
        tag.assignment
            .unwrap_or(*element)
            .checked_add(tag.addition)
            .expect("test element overflow")
    }

    fn apply_measure(tag: &Self::Tag, measure: &i64, count: usize) -> i64 {
        if count == 0 {
            return 0;
        }
        let count = i64::try_from(count).expect("test count fits i64");
        match tag.assignment {
            Some(value) => value
                .checked_add(tag.addition)
                .and_then(|value| value.checked_mul(count))
                .expect("test assigned measure overflow"),
            None => tag
                .addition
                .checked_mul(count)
                .and_then(|delta| measure.checked_add(delta))
                .expect("test measure overflow"),
        }
    }
}

type Sequence = RangeUpdateSequence<i64, AffineSum>;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum CaseTag {
    Identity,
    Upper,
}

#[derive(Debug)]
struct TokenList;

impl MeasurePolicy<String> for TokenList {
    type Measure = Vec<String>;

    fn empty() -> Self::Measure {
        Vec::new()
    }

    fn measure(element: &String) -> Self::Measure {
        vec![element.clone()]
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        let mut result = left.clone();
        result.extend(right.iter().cloned());
        result
    }
}

impl RangeUpdateAlgebra<String> for TokenList {
    type Tag = CaseTag;

    fn identity_tag() -> Self::Tag {
        CaseTag::Identity
    }

    fn is_identity(tag: &Self::Tag) -> bool {
        matches!(tag, CaseTag::Identity)
    }

    fn compose(newer: &Self::Tag, older: &Self::Tag) -> Self::Tag {
        match (newer, older) {
            (CaseTag::Upper, _) | (_, CaseTag::Upper) => CaseTag::Upper,
            (CaseTag::Identity, older) => *older,
        }
    }

    fn apply_element(tag: &Self::Tag, element: &String) -> String {
        match tag {
            CaseTag::Identity => element.clone(),
            CaseTag::Upper => element.to_ascii_uppercase(),
        }
    }

    fn apply_measure(tag: &Self::Tag, measure: &Vec<String>, count: usize) -> Vec<String> {
        assert_eq!(measure.len(), count);
        match tag {
            CaseTag::Identity => measure.clone(),
            CaseTag::Upper => measure
                .iter()
                .map(|item| item.to_ascii_uppercase())
                .collect(),
        }
    }
}

#[test]
fn algebra_laws_include_directional_composition_and_distinct_identities() {
    let identities = [
        <AffineSum as RangeUpdateAlgebra<i64>>::identity_tag(),
        AffineTag::alternate_identity(),
    ];
    let tags = [
        AffineTag::add(-3),
        AffineTag::add(5),
        AffineTag::assign(7),
        AffineTag {
            assignment: Some(4),
            addition: 2,
            identity_witness: 0,
        },
    ];
    for identity in identities {
        assert!(<AffineSum as RangeUpdateAlgebra<i64>>::is_identity(
            &identity
        ));
        for tag in tags {
            let left = <AffineSum as RangeUpdateAlgebra<i64>>::compose(&identity, &tag);
            let right = <AffineSum as RangeUpdateAlgebra<i64>>::compose(&tag, &identity);
            for value in [-11, 0, 13] {
                assert_eq!(
                    <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&left, &value),
                    <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&tag, &value)
                );
                assert_eq!(
                    <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&right, &value),
                    <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&tag, &value)
                );
            }
        }
    }

    let add_then_assign =
        <AffineSum as RangeUpdateAlgebra<i64>>::compose(&AffineTag::assign(7), &AffineTag::add(10));
    let assign_then_add =
        <AffineSum as RangeUpdateAlgebra<i64>>::compose(&AffineTag::add(10), &AffineTag::assign(7));
    assert_eq!(
        <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&add_then_assign, &2),
        7
    );
    assert_eq!(
        <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&assign_then_add, &2),
        17
    );

    for older in tags {
        for newer in tags {
            let composed = <AffineSum as RangeUpdateAlgebra<i64>>::compose(&newer, &older);
            for value in [-9, 0, 12] {
                let sequential = <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(
                    &newer,
                    &<AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&older, &value),
                );
                assert_eq!(
                    <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&composed, &value),
                    sequential
                );
            }
            let measure = 17;
            let count = 4;
            let sequential = <AffineSum as RangeUpdateAlgebra<i64>>::apply_measure(
                &newer,
                &<AffineSum as RangeUpdateAlgebra<i64>>::apply_measure(&older, &measure, count),
                count,
            );
            assert_eq!(
                <AffineSum as RangeUpdateAlgebra<i64>>::apply_measure(&composed, &measure, count,),
                sequential
            );
        }
    }

    for first in tags {
        for second in tags {
            for third in tags {
                let left = <AffineSum as RangeUpdateAlgebra<i64>>::compose(
                    &third,
                    &<AffineSum as RangeUpdateAlgebra<i64>>::compose(&second, &first),
                );
                let right = <AffineSum as RangeUpdateAlgebra<i64>>::compose(
                    &<AffineSum as RangeUpdateAlgebra<i64>>::compose(&third, &second),
                    &first,
                );
                for value in [-9, 0, 12] {
                    assert_eq!(
                        <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&left, &value),
                        <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&right, &value)
                    );
                }
            }
        }
    }

    for tag in tags {
        for values in [vec![], vec![3], vec![-2, 5, 9]] {
            let measure = values.iter().sum::<i64>();
            let transformed = values
                .iter()
                .map(|value| <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&tag, value))
                .sum::<i64>();
            assert_eq!(
                <AffineSum as RangeUpdateAlgebra<i64>>::apply_measure(&tag, &measure, values.len(),),
                transformed
            );
        }
    }
}

#[test]
fn ordered_noncommutative_measures_remain_in_sequence_order() {
    type TextSequence = RangeUpdateSequence<String, TokenList>;
    let source = TextSequence::from_items(["left", "Middle", "right"].map(String::from))
        .expect("small source must build");
    assert_eq!(
        source.measure(),
        &vec![
            String::from("left"),
            String::from("Middle"),
            String::from("right")
        ]
    );
    let updated = source
        .apply_range(1, 2, CaseTag::Upper)
        .expect("valid update must succeed");
    assert_eq!(
        updated.to_vec(),
        [
            String::from("left"),
            String::from("MIDDLE"),
            String::from("RIGHT")
        ]
    );
    assert_eq!(
        updated.measure_range(0, 2).expect("valid range"),
        vec![String::from("left"), String::from("MIDDLE")]
    );
    assert!(updated.validate_structure().is_ok());
}

#[test]
fn boundaries_edits_splits_ranges_and_identity_shortcuts_are_explicit() {
    let empty = Sequence::new();
    assert!(empty.is_empty());
    assert_eq!(empty.len(), 0);
    assert_eq!(*empty.measure(), 0);
    assert_eq!(empty.get(0), None);
    assert!(matches!(
        empty.remove_at(0),
        Err(RangeUpdateError::ElementIndexOutOfRange { .. })
    ));
    assert!(matches!(
        empty.split_at(1),
        Err(RangeUpdateError::BoundaryIndexOutOfRange { .. })
    ));
    assert!(matches!(
        empty.apply_range(1, 0, <AffineSum as RangeUpdateAlgebra<i64>>::identity_tag(),),
        Err(RangeUpdateError::RangeOutOfRange { .. })
    ));

    let original = Sequence::from_items([1, 2, 3, 4]).expect("small source must build");
    let empty_update = original
        .apply_range(2, 0, AffineTag::add(99))
        .expect("empty range is valid");
    let identity_update = original
        .apply_range(0, original.len(), AffineTag::alternate_identity())
        .expect("identity update is valid");
    assert!(original.shares_root_with(&empty_update));
    assert!(original.shares_root_with(&identity_update));

    for boundary in 0..=original.len() {
        let split = original.split_at(boundary).expect("valid split boundary");
        assert_eq!(split.left.to_vec(), original.to_vec()[..boundary]);
        assert_eq!(split.right.to_vec(), original.to_vec()[boundary..]);
        let round_trip = split.left.concat(&split.right).expect("small concat");
        assert_eq!(round_trip.to_vec(), original.to_vec());
    }
    assert!(
        original
            .split_at(0)
            .expect("endpoint split")
            .right
            .shares_root_with(&original)
    );
    assert!(
        original
            .split_at(original.len())
            .expect("endpoint split")
            .left
            .shares_root_with(&original)
    );

    for index in 0..=original.len() {
        for count in 0..=original.len() - index {
            let range = original.get_range(index, count).expect("valid range");
            assert_eq!(range.to_vec(), original.to_vec()[index..index + count]);
            assert_eq!(
                original
                    .measure_range(index, count)
                    .expect("valid measure range"),
                original.to_vec()[index..index + count].iter().sum::<i64>()
            );
        }
    }
    assert!(
        original
            .get_range(0, original.len())
            .expect("full range")
            .shares_root_with(&original)
    );

    let added = original
        .apply_range(0, original.len(), AffineTag::add(10))
        .expect("whole update");
    let inserted = added.insert(2, 99).expect("valid insertion");
    let replaced = inserted.set_item(0, -7).expect("valid replacement");
    let removed = replaced.remove_at(3).expect("valid removal");
    assert_eq!(added.to_vec(), [11, 12, 13, 14]);
    assert_eq!(inserted.to_vec(), [11, 12, 99, 13, 14]);
    assert_eq!(replaced.to_vec(), [-7, 12, 99, 13, 14]);
    assert_eq!(removed.to_vec(), [-7, 12, 99, 14]);
    assert_eq!(original.to_vec(), [1, 2, 3, 4]);
    assert!(removed.validate_structure().is_ok());

    let unrelated_empty = Sequence::new();
    assert!(
        unrelated_empty
            .concat(&original)
            .expect("empty prefix")
            .shares_root_with(&original)
    );
    assert!(
        original
            .concat(&unrelated_empty)
            .expect("empty suffix")
            .shares_root_with(&original)
    );
}

#[test]
fn nested_assignment_and_addition_preserve_composition_direction() {
    let source = Sequence::from_items([1, 2, 3, 4, 5]).expect("small source must build");
    let updated = source
        .apply_range(1, 3, AffineTag::add(10))
        .expect("valid add")
        .apply_range(2, 2, AffineTag::assign(7))
        .expect("valid assignment")
        .apply_range(0, 4, AffineTag::add(-2))
        .expect("valid add");
    assert_eq!(updated.to_vec(), [-1, 10, 5, 5, 5]);
    assert_eq!(*updated.measure(), 24);
    assert_eq!(updated.measure_range(1, 3).expect("valid range"), 20);
    assert_eq!(source.to_vec(), [1, 2, 3, 4, 5]);
    assert!(updated.validate_structure().is_ok());
}

#[test]
fn whole_and_proper_updates_retain_structural_sharing() {
    let source = Sequence::from_items(0..128).expect("small source must build");
    let whole = source
        .apply_range(0, source.len(), AffineTag::add(1))
        .expect("whole update");
    assert_eq!(source.shared_node_count(&whole), source.len() - 1);
    let statistics = whole.structure_statistics();
    assert_eq!(statistics.pending_tag_node_count, 1);
    assert_eq!(statistics.maximum_pending_tag_depth, 1);

    let proper = whole
        .apply_range(32, 64, AffineTag::assign(5))
        .expect("proper update");
    assert!(whole.shared_node_count(&proper) > 0);
    assert_eq!(source.to_vec(), (0..128).collect::<Vec<_>>());
    assert_eq!(&proper.to_vec()[32..96], vec![5; 64].as_slice());
    assert!(proper.validate_structure().is_ok());
}

#[derive(Clone)]
struct ModelState {
    sequence: Sequence,
    values: Vec<i64>,
}

fn next_random(state: &mut u64) -> u64 {
    *state = state
        .wrapping_mul(6_364_136_223_846_793_005)
        .wrapping_add(1_442_695_040_888_963_407);
    *state
}

fn assert_model(state: &ModelState) {
    assert_eq!(state.sequence.len(), state.values.len());
    assert_eq!(state.sequence.to_vec(), state.values);
    assert_eq!(*state.sequence.measure(), state.values.iter().sum::<i64>());
    assert!(state.sequence.validate_structure().is_ok());
    for (index, value) in state.values.iter().enumerate() {
        assert_eq!(state.sequence.get(index), Some(*value));
    }
}

#[test]
fn generated_branching_history_matches_a_mutable_vector_model() {
    let mut random = 0x5eed_f00d_dead_beef_u64;
    let mut current = ModelState {
        sequence: Sequence::new(),
        values: Vec::new(),
    };
    let mut versions = vec![current.clone()];

    for step in 0..1_500 {
        let command = next_random(&mut random) % 10;
        match command {
            0 if current.values.len() < 96 => {
                let value = (next_random(&mut random) % 101) as i64 - 50;
                current.sequence = current.sequence.append(value).expect("bounded append");
                current.values.push(value);
            }
            1 if current.values.len() < 96 => {
                let index = (next_random(&mut random) as usize) % (current.values.len() + 1);
                let value = (next_random(&mut random) % 101) as i64 - 50;
                current.sequence = current
                    .sequence
                    .insert(index, value)
                    .expect("bounded insertion");
                current.values.insert(index, value);
            }
            2 if !current.values.is_empty() => {
                let index = (next_random(&mut random) as usize) % current.values.len();
                let value = (next_random(&mut random) % 101) as i64 - 50;
                current.sequence = current
                    .sequence
                    .set_item(index, value)
                    .expect("valid replacement");
                current.values[index] = value;
            }
            3 if !current.values.is_empty() => {
                let index = (next_random(&mut random) as usize) % current.values.len();
                current.sequence = current.sequence.remove_at(index).expect("valid removal");
                current.values.remove(index);
            }
            4 | 5 if !current.values.is_empty() => {
                let index = (next_random(&mut random) as usize) % (current.values.len() + 1);
                let count =
                    (next_random(&mut random) as usize) % (current.values.len() - index + 1);
                let tag = if command == 4 {
                    AffineTag::add((next_random(&mut random) % 9) as i64 - 4)
                } else {
                    AffineTag::assign((next_random(&mut random) % 31) as i64 - 15)
                };
                current.sequence = current
                    .sequence
                    .apply_range(index, count, tag)
                    .expect("valid update range");
                for value in &mut current.values[index..index + count] {
                    *value = <AffineSum as RangeUpdateAlgebra<i64>>::apply_element(&tag, value);
                }
            }
            6 => {
                let boundary = (next_random(&mut random) as usize) % (current.values.len() + 1);
                let split = current
                    .sequence
                    .split_at(boundary)
                    .expect("valid split boundary");
                assert_eq!(split.left.to_vec(), current.values[..boundary]);
                assert_eq!(split.right.to_vec(), current.values[boundary..]);
                current.sequence = split.left.concat(&split.right).expect("bounded concat");
            }
            7 => {
                let index = (next_random(&mut random) as usize) % (current.values.len() + 1);
                let count =
                    (next_random(&mut random) as usize) % (current.values.len() - index + 1);
                let range = current
                    .sequence
                    .get_range(index, count)
                    .expect("valid range");
                assert_eq!(range.to_vec(), current.values[index..index + count]);
                assert_eq!(
                    current
                        .sequence
                        .measure_range(index, count)
                        .expect("valid range measure"),
                    current.values[index..index + count].iter().sum::<i64>()
                );
            }
            8 if versions.len() > 1 => {
                let version = (next_random(&mut random) as usize) % versions.len();
                current = versions[version].clone();
            }
            _ if current.values.len() < 94 => {
                let suffix_values = [
                    (next_random(&mut random) % 21) as i64 - 10,
                    (next_random(&mut random) % 21) as i64 - 10,
                ];
                let suffix = Sequence::from_items(suffix_values).expect("small suffix");
                current.sequence = current.sequence.concat(&suffix).expect("bounded concat");
                current.values.extend(suffix_values);
            }
            _ => {
                current.sequence = current
                    .sequence
                    .remove_at(0)
                    .expect("nonempty bounded state");
                current.values.remove(0);
            }
        }

        assert_model(&current);
        if step % 7 == 0 {
            versions.push(current.clone());
        }
        if step % 113 == 0 {
            for retained in versions.iter().step_by(11) {
                assert_model(retained);
            }
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Callback {
    Empty,
    Measure,
    Combine,
    Identity,
    Compose,
    ApplyElement,
    ApplyMeasure,
}

thread_local! {
    static FAIL_AT: Cell<Option<Callback>> = const { Cell::new(None) };
    static CALLBACK_COUNT: Cell<usize> = const { Cell::new(0) };
}

fn hit(callback: Callback) {
    CALLBACK_COUNT.with(|count| count.set(count.get() + 1));
    FAIL_AT.with(|fail| {
        assert_ne!(fail.get(), Some(callback), "injected {callback:?} panic");
    });
}

#[derive(Debug)]
struct FailAlgebra;

impl MeasurePolicy<i64> for FailAlgebra {
    type Measure = i64;

    fn empty() -> Self::Measure {
        hit(Callback::Empty);
        0
    }

    fn measure(element: &i64) -> Self::Measure {
        hit(Callback::Measure);
        *element
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        hit(Callback::Combine);
        *left + *right
    }
}

impl RangeUpdateAlgebra<i64> for FailAlgebra {
    type Tag = i64;

    fn identity_tag() -> Self::Tag {
        0
    }

    fn is_identity(tag: &Self::Tag) -> bool {
        hit(Callback::Identity);
        *tag == 0
    }

    fn compose(newer: &Self::Tag, older: &Self::Tag) -> Self::Tag {
        hit(Callback::Compose);
        *newer + *older
    }

    fn apply_element(tag: &Self::Tag, element: &i64) -> i64 {
        hit(Callback::ApplyElement);
        *element + *tag
    }

    fn apply_measure(tag: &Self::Tag, measure: &i64, count: usize) -> i64 {
        hit(Callback::ApplyMeasure);
        *measure + *tag * i64::try_from(count).expect("small test count")
    }
}

type FailSequence = RangeUpdateSequence<i64, FailAlgebra>;

fn inject_panic<F>(callback: Callback, operation: F)
where
    F: FnOnce(),
{
    FAIL_AT.with(|fail| fail.set(Some(callback)));
    let result = catch_unwind(AssertUnwindSafe(operation));
    FAIL_AT.with(|fail| fail.set(None));
    assert!(result.is_err(), "{callback:?} failpoint was not reached");
}

#[test]
fn every_policy_panic_leaves_all_published_snapshots_unchanged() {
    FAIL_AT.with(|fail| fail.set(None));
    let source = FailSequence::from_items([1, 2, 3, 4, 5]).expect("baseline builds");
    let expected = source.to_vec();

    inject_panic(Callback::Measure, || {
        let _ = source.set_item(2, 99);
    });
    inject_panic(Callback::Combine, || {
        let _ = source.set_item(2, 99);
    });
    inject_panic(Callback::Identity, || {
        let _ = source.apply_range(0, source.len(), 1);
    });

    // Empty and invalid ranges return before identity recognition.
    FAIL_AT.with(|fail| fail.set(Some(Callback::Identity)));
    let empty_update = source
        .apply_range(2, 0, 1)
        .expect("an empty update must bypass identity recognition");
    let invalid_update = source.apply_range(source.len() + 1, 0, 1);
    FAIL_AT.with(|fail| fail.set(None));
    assert!(empty_update.shares_root_with(&source));
    assert!(matches!(
        invalid_update,
        Err(RangeUpdateError::RangeOutOfRange { .. })
    ));

    inject_panic(Callback::ApplyElement, || {
        let _ = source.apply_range(0, source.len(), 1);
    });
    inject_panic(Callback::ApplyMeasure, || {
        let _ = source.apply_range(0, source.len(), 1);
    });

    let tagged = source
        .apply_range(0, source.len(), 1)
        .expect("baseline tag succeeds");
    let tagged_expected = tagged.to_vec();
    inject_panic(Callback::Compose, || {
        let _ = tagged.apply_range(0, tagged.len(), 2);
    });

    inject_panic(Callback::Empty, || {
        let _ = FailSequence::new();
    });

    assert_eq!(source.to_vec(), expected);
    assert_eq!(tagged.to_vec(), tagged_expected);
    assert!(source.validate_structure().is_ok());
    assert!(tagged.validate_structure().is_ok());
}

struct ExplodingSource {
    index: usize,
}

impl Iterator for ExplodingSource {
    type Item = i64;

    fn next(&mut self) -> Option<Self::Item> {
        if self.index == 2 {
            panic!("injected source panic");
        }
        self.index += 1;
        Some(self.index as i64)
    }
}

#[test]
fn source_enumeration_finishes_before_any_policy_callback() {
    FAIL_AT.with(|fail| fail.set(None));
    CALLBACK_COUNT.with(|count| count.set(0));
    let result = catch_unwind(AssertUnwindSafe(|| {
        let _ = FailSequence::from_items(ExplodingSource { index: 0 });
    }));
    assert!(result.is_err());
    assert_eq!(CALLBACK_COUNT.with(|count| count.get()), 0);
}

#[test]
fn independent_iterators_and_concurrent_snapshot_readers_are_stable() {
    fn assert_send_sync<T: Send + Sync>() {}
    assert_send_sync::<Sequence>();

    let source = Sequence::from_items(0..256)
        .expect("small source")
        .apply_range(32, 160, AffineTag::add(3))
        .expect("valid update")
        .apply_range(80, 40, AffineTag::assign(-7))
        .expect("valid update");
    let expected = source.to_vec();

    let mut first = source.iter();
    let mut second = source.iter();
    assert_eq!(first.next(), Some(expected[0]));
    assert_eq!(first.next(), Some(expected[1]));
    assert_eq!(second.next(), Some(expected[0]));
    assert_eq!(first.len(), expected.len() - 2);
    assert_eq!(second.len(), expected.len() - 1);

    let shared = Arc::new(source);
    let mut readers = Vec::new();
    for _ in 0..4 {
        let snapshot = shared.clone();
        let expected = expected.clone();
        readers.push(thread::spawn(move || {
            for _ in 0..32 {
                assert_eq!(snapshot.to_vec(), expected);
                assert_eq!(snapshot.get(111), Some(expected[111]));
                assert_eq!(
                    snapshot.measure_range(40, 80).expect("valid range"),
                    expected[40..120].iter().sum::<i64>()
                );
            }
        }));
    }
    for reader in readers {
        reader.join().expect("snapshot reader failed");
    }
}
