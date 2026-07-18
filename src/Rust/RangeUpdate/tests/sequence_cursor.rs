use tools_data_structures_range_update::{MeasurePolicy, RangeUpdateAlgebra, RangeUpdateSequence};

#[derive(Debug)]
struct Additive;

impl MeasurePolicy<i64> for Additive {
    type Measure = i64;

    fn empty() -> Self::Measure {
        0
    }

    fn measure(element: &i64) -> Self::Measure {
        *element
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left + right
    }
}

impl RangeUpdateAlgebra<i64> for Additive {
    type Tag = i64;

    fn identity_tag() -> Self::Tag {
        0
    }

    fn is_identity(tag: &Self::Tag) -> bool {
        *tag == 0
    }

    fn compose(newer: &Self::Tag, older: &Self::Tag) -> Self::Tag {
        newer + older
    }

    fn apply_element(tag: &Self::Tag, element: &i64) -> i64 {
        element + tag
    }

    fn apply_measure(tag: &Self::Tag, measure: &Self::Measure, count: usize) -> Self::Measure {
        measure + tag * count as i64
    }
}

#[test]
fn range_cursor_preserves_logical_measures_and_directional_tags() {
    let basis =
        RangeUpdateSequence::<_, Additive>::from_items([1, 2, 3, 4]).expect("valid sequence");
    let cursor = basis.cursor_at(2).expect("valid boundary");
    assert_eq!(cursor.measure_before(), 3);
    assert_eq!(cursor.measure_after(), 7);
    assert_eq!(cursor.measure_previous(2), Ok(3));
    assert_eq!(cursor.measure_next(2), Ok(7));

    let edited = cursor
        .apply_previous(1, 10)
        .expect("valid previous range")
        .apply_next(2, 20)
        .expect("valid next range")
        .replace_next(99)
        .expect("next element");
    assert_eq!(edited.position(), 2);
    assert_eq!(edited.snapshot().to_vec(), [1, 12, 99, 24]);
    assert_eq!(basis.to_vec(), [1, 2, 3, 4]);
}
