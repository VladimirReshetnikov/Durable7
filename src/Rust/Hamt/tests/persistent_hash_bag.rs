use std::collections::{BTreeMap, BTreeSet};
use std::hash::{BuildHasher, BuildHasherDefault, Hash, Hasher};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use tools_data_structures_hamt::{HashBagError, PersistentHashBag};

#[derive(Clone, Debug)]
struct Representative {
    class: i32,
    label: &'static str,
}

impl Hash for Representative {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.class.hash(state);
    }
}

impl PartialEq for Representative {
    fn eq(&self, other: &Self) -> bool {
        self.class == other.class
    }
}

impl Eq for Representative {}

#[derive(Clone, Default)]
struct CountingState {
    builds: Arc<AtomicUsize>,
}

impl CountingState {
    fn reset(&self) {
        self.builds.store(0, Ordering::Relaxed);
    }

    fn count(&self) -> usize {
        self.builds.load(Ordering::Relaxed)
    }
}

impl BuildHasher for CountingState {
    type Hasher = FnvHasher;

    fn build_hasher(&self) -> Self::Hasher {
        self.builds.fetch_add(1, Ordering::Relaxed);
        FnvHasher::default()
    }
}

struct FnvHasher(u64);

impl Default for FnvHasher {
    fn default() -> Self {
        Self(0xcbf_29ce4_8422_2325)
    }
}

impl Hasher for FnvHasher {
    fn finish(&self) -> u64 {
        self.0
    }

    fn write(&mut self, bytes: &[u8]) {
        for byte in bytes {
            self.0 ^= u64::from(*byte);
            self.0 = self.0.wrapping_mul(0x0000_0100_0000_01b3);
        }
    }
}

#[derive(Default)]
struct ConstantHasher;

impl Hasher for ConstantHasher {
    fn finish(&self) -> u64 {
        0
    }

    fn write(&mut self, _bytes: &[u8]) {}
}

type ConstantState = BuildHasherDefault<ConstantHasher>;

#[derive(Clone)]
struct SwitchState {
    panic_on_hash: Arc<AtomicBool>,
}

impl SwitchState {
    fn new() -> Self {
        Self {
            panic_on_hash: Arc::new(AtomicBool::new(false)),
        }
    }
}

impl BuildHasher for SwitchState {
    type Hasher = ConstantHasher;

    fn build_hasher(&self) -> Self::Hasher {
        assert!(
            !self.panic_on_hash.load(Ordering::Relaxed),
            "intentional receiver-policy hash failure",
        );
        ConstantHasher
    }
}

#[test]
fn construction_queries_and_all_three_iteration_surfaces_agree() {
    let bag = PersistentHashBag::try_from_items([3, 1, 3, 2, 1, 3]).expect("valid bag");
    let collected: PersistentHashBag<i32> = [1, 3, 2, 3, 1, 3].into_iter().collect();

    assert_eq!(bag, collected);
    assert_eq!(bag.distinct_count(), 3);
    assert_eq!(bag.total_count(), 6);
    assert!(!bag.is_empty());
    assert!(bag.contains(&1));
    assert_eq!(bag.count_of(&1), 2);
    assert_eq!(bag.count_of(&9), 0);

    let distinct = bag.distinct_items().copied().collect::<BTreeSet<_>>();
    assert_eq!(distinct, BTreeSet::from([1, 2, 3]));
    let entry_model = bag
        .entries()
        .map(|entry| (*entry.item, entry.count))
        .collect::<BTreeMap<_, _>>();
    assert_eq!(entry_model, BTreeMap::from([(1, 2), (2, 1), (3, 3)]));

    let first_expansion = bag.iter().copied().collect::<Vec<_>>();
    let second_expansion = bag.iter().copied().collect::<Vec<_>>();
    assert_eq!(first_expansion, second_expansion);
    assert_eq!(first_expansion.len(), 6);
    let expanded_model = first_expansion
        .into_iter()
        .fold(BTreeMap::new(), |mut model, item| {
            *model.entry(item).or_insert(0) += 1;
            model
        });
    assert_eq!(expanded_model, entry_model);
    assert_eq!(bag.to_vec().expect("small expansion").len(), 6);

    let mut iterator = bag.iter();
    assert_eq!(iterator.size_hint(), (6, Some(6)));
    let _ = iterator.next();
    assert_eq!(iterator.size_hint(), (5, Some(5)));
}

#[test]
fn first_representative_is_retained_and_zero_or_invalid_updates_do_not_hash() {
    let state = CountingState::default();
    let first = Representative {
        class: 1,
        label: "first",
    };
    let bag = PersistentHashBag::with_hasher(state.clone())
        .add_copies(first, 2)
        .expect("initial copies")
        .add(Representative {
            class: 1,
            label: "second",
        })
        .expect("equivalent copy");
    assert_eq!(
        bag.count_of(&Representative {
            class: 1,
            label: "probe"
        }),
        3
    );
    assert_eq!(
        bag.get_stored(&Representative {
            class: 1,
            label: "probe"
        })
        .expect("stored representative")
        .label,
        "first",
    );

    state.reset();
    let zero_added = bag
        .add_copies(
            Representative {
                class: 99,
                label: "zero",
            },
            0,
        )
        .expect("zero add");
    let zero_removed = bag
        .remove_copies(
            &Representative {
                class: 99,
                label: "zero",
            },
            0,
        )
        .expect("zero remove");
    let invalid = bag.add_copies(
        Representative {
            class: 99,
            label: "bad",
        },
        -1,
    );
    assert_eq!(state.count(), 0);
    assert!(bag.shares_root_with(&zero_added));
    assert!(bag.shares_root_with(&zero_removed));
    assert_eq!(invalid.err(), Some(HashBagError::NegativeCopies(-1)));
}

#[test]
fn checked_multiplicities_and_i64_totals_fail_atomically() {
    let first = PersistentHashBag::new()
        .add_copies(1, i32::MAX)
        .expect("maximum legal multiplicity");
    let before = first.clone();
    assert_eq!(first.total_count(), i64::from(i32::MAX));
    assert_eq!(first.add(1).err(), Some(HashBagError::MultiplicityOverflow));
    assert!(first.shares_root_with(&before));
    assert_eq!(first.count_of(&1), i32::MAX);

    let wide_total = first
        .add_copies(2, i32::MAX)
        .expect("a second class keeps its own checked count");
    assert_eq!(wide_total.total_count(), 2_i64 * i64::from(i32::MAX));
    assert_eq!(wide_total.distinct_count(), 2);

    let one = PersistentHashBag::new().add(1).expect("one copy");
    assert_eq!(
        first.sum(&one).expect_err("sum must check multiplicity"),
        HashBagError::MultiplicityOverflow,
    );
    assert!(first.shares_root_with(&before));
}

#[test]
fn removal_is_saturating_and_preserves_unchanged_roots() {
    let bag = PersistentHashBag::new()
        .add_copies("a", 3)
        .expect("a copies")
        .add_copies("b", 2)
        .expect("b copies");

    let missing = bag.remove(&"missing");
    assert!(bag.shares_root_with(&missing));

    let partial = bag.remove_copies(&"a", 2).expect("partial removal");
    assert_eq!(partial.count_of(&"a"), 1);
    assert_eq!(partial.total_count(), 3);
    assert_eq!(bag.count_of(&"a"), 3);

    let saturated = partial
        .remove_copies(&"a", i32::MAX)
        .expect("saturated removal");
    assert_eq!(saturated.count_of(&"a"), 0);
    assert_eq!(saturated.distinct_count(), 1);
    assert_eq!(saturated.total_count(), 2);

    let cleared_class = bag.remove_all(&"b");
    assert_eq!(cleared_class.count_of(&"b"), 0);
    assert_eq!(cleared_class.total_count(), 3);
    assert!(bag.remove_all(&"missing").shares_root_with(&bag));
    assert!(bag.clear().is_empty());
}

#[test]
fn algebra_uses_receiver_policy_counts_and_representatives() {
    let receiver_state = CountingState::default();
    let argument_state = CountingState::default();
    let receiver = PersistentHashBag::with_hasher(receiver_state)
        .add_copies(
            Representative {
                class: 1,
                label: "receiver overlap",
            },
            2,
        )
        .expect("receiver overlap")
        .add_copies(
            Representative {
                class: 2,
                label: "receiver only",
            },
            3,
        )
        .expect("receiver only");
    let argument = PersistentHashBag::with_hasher(argument_state)
        .add_copies(
            Representative {
                class: 1,
                label: "argument overlap",
            },
            4,
        )
        .expect("argument overlap")
        .add_copies(
            Representative {
                class: 3,
                label: "argument only",
            },
            5,
        )
        .expect("argument only");

    let union = receiver.union(&argument).expect("union");
    assert_bag_counts(&union, &[(1, 4), (2, 3), (3, 5)]);
    assert_eq!(union.total_count(), 12);
    assert_eq!(stored_label(&union, 1), "receiver overlap");
    assert_eq!(stored_label(&union, 3), "argument only");

    let intersection = receiver.intersect(&argument).expect("intersection");
    assert_bag_counts(&intersection, &[(1, 2)]);
    assert_eq!(intersection.total_count(), 2);
    assert_eq!(stored_label(&intersection, 1), "receiver overlap");

    let difference = receiver.except(&argument).expect("difference");
    assert_bag_counts(&difference, &[(2, 3)]);
    assert_eq!(difference.total_count(), 3);
    assert_eq!(stored_label(&difference, 2), "receiver only");

    let sum = receiver.sum(&argument).expect("sum");
    assert_bag_counts(&sum, &[(1, 6), (2, 3), (3, 5)]);
    assert_eq!(sum.total_count(), 14);
    assert_eq!(stored_label(&sum, 1), "receiver overlap");
    assert_eq!(stored_label(&sum, 3), "argument only");

    let same = receiver.clone();
    assert!(
        receiver
            .union(&same)
            .expect("self union")
            .shares_root_with(&receiver)
    );
    assert!(
        receiver
            .intersect(&same)
            .expect("self intersection")
            .shares_root_with(&receiver)
    );
    assert!(receiver.except(&same).expect("self difference").is_empty());
}

#[test]
fn mismatched_policy_normalization_precedes_empty_shortcuts() {
    let receiver_state = SwitchState::new();
    let receiver: PersistentHashBag<i32, SwitchState> =
        PersistentHashBag::with_hasher(receiver_state.clone());
    let argument = PersistentHashBag::with_hasher(SwitchState::new())
        .add(1)
        .expect("argument copy");
    receiver_state.panic_on_hash.store(true, Ordering::Relaxed);

    let failure = catch_unwind(AssertUnwindSafe(|| receiver.intersect(&argument)));
    assert!(failure.is_err());
    assert!(receiver.is_empty());
}

#[test]
fn collision_heavy_updates_match_a_deterministic_multiset_model() {
    let mut bag: PersistentHashBag<i32, ConstantState> =
        PersistentHashBag::with_hasher(ConstantState::default());
    let mut model = BTreeMap::<i32, i32>::new();
    let mut random = 0xd1b5_4a32_d192_ed03_u64;

    for step in 0..4_096_u64 {
        random ^= random << 7;
        random ^= random >> 9;
        random ^= random << 8;
        let item = ((random >> 15) % 61) as i32 - 30;
        let copies = ((random >> 31) % 6) as i32;

        let operation = (random ^ step) % 5;
        match operation {
            0 | 1 => {
                bag = bag.add_copies(item, copies).expect("bounded addition");
                if copies > 0 {
                    *model.entry(item).or_insert(0) += copies;
                }
            }
            2 | 3 => {
                bag = bag.remove_copies(&item, copies).expect("bounded removal");
                let remove_class = if let Some(count) = model.get_mut(&item) {
                    *count = count.saturating_sub(copies).max(0);
                    *count == 0
                } else {
                    false
                };
                if remove_class {
                    model.remove(&item);
                }
            }
            _ => {
                bag = bag.remove_all(&item);
                model.remove(&item);
            }
        }

        assert_eq!(
            bag.total_count(),
            model.values().map(|count| i64::from(*count)).sum::<i64>(),
            "step {step}, operation {operation}, item {item}, copies {copies}",
        );

        if step % 127 == 0 {
            assert_eq!(bag.distinct_count(), model.len(), "step {step}");
            assert_eq!(
                bag.total_count(),
                model.values().map(|count| i64::from(*count)).sum::<i64>(),
                "step {step}",
            );
            for probe in -30..=30 {
                assert_eq!(
                    bag.count_of(&probe),
                    model.get(&probe).copied().unwrap_or(0),
                    "step {step}, probe {probe}",
                );
            }
        }
    }

    let expanded = bag.iter().fold(BTreeMap::new(), |mut counts, item| {
        *counts.entry(*item).or_insert(0) += 1;
        counts
    });
    assert_eq!(expanded, model);
}

fn assert_bag_counts<S>(bag: &PersistentHashBag<Representative, S>, expected: &[(i32, i32)])
where
    S: BuildHasher + Clone,
{
    assert_eq!(bag.distinct_count(), expected.len());
    for (class, count) in expected {
        assert_eq!(
            bag.count_of(&Representative {
                class: *class,
                label: "probe"
            }),
            *count,
        );
    }
}

fn stored_label<S>(bag: &PersistentHashBag<Representative, S>, class: i32) -> &'static str
where
    S: BuildHasher + Clone,
{
    bag.get_stored(&Representative {
        class,
        label: "probe",
    })
    .expect("stored representative")
    .label
}
