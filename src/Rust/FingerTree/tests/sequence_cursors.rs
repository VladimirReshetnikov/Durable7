use durable7_fingertree::{
    FingerTree, PersistentDeque, ReversibleDeque, RrbVector, SumMeasure,
};

#[test]
fn deque_cursor_retains_versions_and_distinguishes_stored_none() {
    let basis: PersistentDeque<Option<i32>> = [Some(1), None, Some(3)].into_iter().collect();
    let cursor = basis.cursor_at(2).expect("valid boundary");
    assert_eq!(cursor.peek_previous(), Some(&None));
    assert_eq!(cursor.peek_next(), Some(&Some(3)));

    let edited = cursor
        .insert_range([Some(7), Some(8)])
        .delete_previous()
        .expect("previous element")
        .replace_next(Some(9))
        .expect("next element");
    assert_eq!(edited.position(), 3);
    assert_eq!(
        edited.snapshot().into_iter().collect::<Vec<_>>(),
        [Some(1), None, Some(7), Some(9)]
    );
    assert_eq!(
        basis.into_iter().collect::<Vec<_>>(),
        [Some(1), None, Some(3)]
    );
}

#[test]
fn reversible_cursor_uses_logical_order_and_maps_the_reversed_gap() {
    let basis = [1, 2, 3, 4]
        .into_iter()
        .collect::<ReversibleDeque<_>>()
        .reverse();
    let cursor = basis.cursor_at(1).expect("valid boundary");
    assert_eq!(cursor.peek_previous(), Some(&4));
    assert_eq!(cursor.peek_next(), Some(&3));

    let edited = cursor
        .insert(9)
        .delete_next()
        .expect("next element after insertion");
    assert_eq!(edited.snapshot().to_vec(), [4, 9, 2, 1]);
    let reversed = edited.reverse();
    assert_eq!(reversed.position(), 2);
    assert_eq!(reversed.snapshot().to_vec(), [1, 2, 9, 4]);
}

#[test]
fn general_cursor_exposes_measures_without_a_public_position() {
    let tree: FingerTree<i32, SumMeasure<i32>> = [2, 3, 5, 7].into_iter().collect();
    let located = tree.cursor_by_measure(|total| *total >= 6);
    assert!(located.found);
    assert_eq!(located.cursor.measure_before(), 5);
    assert_eq!(located.cursor.measure_after(), 12);
    assert_eq!(located.cursor.peek_next(), Some(&5));

    let edited = located
        .cursor
        .insert(11)
        .delete_next()
        .expect("next element")
        .replace_next(13)
        .expect("next element");
    assert_eq!(edited.snapshot().to_vec(), [2, 3, 11, 13]);
    assert_eq!(tree.to_vec(), [2, 3, 5, 7]);
    assert_eq!(
        edited
            .seek_by_measure(|total| *total >= 16)
            .cursor
            .peek_next(),
        Some(&11)
    );
}

#[test]
fn rrb_cursor_splices_existing_vectors_and_keeps_the_source_version() {
    let basis: RrbVector<_> = (0..96).collect();
    let inserted: RrbVector<_> = [500, 501, 502].into_iter().collect();
    let cursor = basis
        .cursor_at(32)
        .expect("valid boundary")
        .insert_vector(&inserted);

    assert_eq!(cursor.position(), 35);
    assert_eq!(
        cursor
            .snapshot()
            .get_range(30, 7)
            .expect("valid range")
            .into_iter()
            .collect::<Vec<_>>(),
        [30, 31, 500, 501, 502, 32, 33]
    );
    assert_eq!(
        basis.into_iter().collect::<Vec<_>>(),
        (0..96).collect::<Vec<_>>()
    );
}
