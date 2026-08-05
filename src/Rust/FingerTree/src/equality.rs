//! Retained equality policies for the crate's delta-tracking structures.
//!
//! Delta structures here take value equality from an [`EqualityPolicy`] value rather than from a
//! `T: PartialEq` bound, so a collection can decide what counts as "the same value" by a
//! caller-supplied rule and can remember which rule it was built with. The rule is load-bearing
//! rather than cosmetic: it defines which writes are semantic no-ops and when a recorded change
//! cancels against its checkpoint, so it determines a collection's observable change set, not just
//! the answer to an incidental comparison.
//!
//! Remembering matters for binary operations: comparing, merging, or replaying the changes of two
//! structures is only meaningful when both decided cancellation the same way, and a retained policy
//! makes that check possible instead of silently producing a malformed result.
//!
//! Policy identity is deliberate. Cloning a custom policy preserves identity, so derived versions
//! stay compatible with their source; two independently constructed custom policies are distinct
//! even when they compare identically, because there is no way to prove two closures agree.
//! [`NaturalEqualityComparer`] is the shared natural-[`PartialEq`] policy and compares equal to
//! itself everywhere.
//!
//! [`EqualityPolicy::natural`] requires [`Eq`] rather than [`PartialEq`], because a policy that is
//! not an equivalence relation silently corrupts the structures that retain it, and IEEE-754
//! equality is not reflexive: `NaN != NaN`. Raw `f32` and `f64` are therefore deliberately excluded
//! from the natural policy and fail to compile against it. [`EqualityPolicy::reflexive_ieee`] is the
//! documented exit for float payloads: a canonical policy under which `NaN` equals `NaN` and `-0.0`
//! equals `+0.0`, matching `EqualityComparer<double>.Default`, which is what the C# baseline uses.
//! [`NaturalEqualityComparer`]'s own [`EqualityComparer`] implementation stays on [`PartialEq`], so
//! a custom policy that genuinely wants raw IEEE semantics can still ask for them.
//!
//! An implementation must define a stable equivalence relation for as long as any version that
//! retains it is alive. Values retained by a version must not mutate state the comparer observes;
//! replacing such a value through the owning collection's update operations is the supported way to
//! change equality-observable state. Concurrent reads over retained versions are safe when the
//! comparer is safe for concurrent calls, which the [`Send`] + [`Sync`] bound on
//! [`EqualityComparer`] requires.

use std::fmt;
use std::sync::Arc;

/// Compares values for equivalence.
///
/// Implementations must define a stable equivalence relation: reflexive, symmetric, and
/// transitive. Returning `true` places two concrete values in the same equality-equivalence class,
/// which the owning collection treats as a semantic no-op and as grounds for cancelling a recorded
/// change against its checkpoint.
pub trait EqualityComparer<T>: Send + Sync {
    /// Reports whether two values are equivalent.
    fn equals(&self, left: &T, right: &T) -> bool;
}

impl<T, F> EqualityComparer<T> for F
where
    F: Fn(&T, &T) -> bool + Send + Sync,
{
    fn equals(&self, left: &T, right: &T) -> bool {
        self(left, right)
    }
}

/// Natural [`PartialEq`] equivalence.
///
/// The bound is [`PartialEq`] rather than [`Eq`] so that a custom policy can still select raw
/// [`PartialEq`] semantics for a partially ordered payload. [`EqualityPolicy::natural`] is the
/// stricter surface and admits only [`Eq`] types.
#[derive(Clone, Copy, Debug, Default)]
pub struct NaturalEqualityComparer;

impl<T: PartialEq> EqualityComparer<T> for NaturalEqualityComparer {
    fn equals(&self, left: &T, right: &T) -> bool {
        left == right
    }
}

/// Reflexive IEEE-754 equivalence for binary floating-point payloads.
///
/// `==` on `f32` and `f64` is not reflexive, so it is not an equivalence relation and cannot be the
/// natural policy of a delta structure. This comparer adds the missing reflexivity and resolves the
/// signed zeros the way .NET's default comparer does: every `NaN` is equivalent to every other
/// `NaN`, and `-0.0` stays equivalent to `+0.0`. It is the comparer behind
/// [`EqualityPolicy::reflexive_ieee`], which is its public surface.
#[derive(Clone, Copy, Debug, Default)]
struct ReflexiveIeeeComparer;

impl EqualityComparer<f64> for ReflexiveIeeeComparer {
    fn equals(&self, left: &f64, right: &f64) -> bool {
        left == right || (left.is_nan() && right.is_nan())
    }
}

impl EqualityComparer<f32> for ReflexiveIeeeComparer {
    fn equals(&self, left: &f32, right: &f32) -> bool {
        left == right || (left.is_nan() && right.is_nan())
    }
}

struct EqualityPolicyInner<T> {
    comparer: Arc<dyn EqualityComparer<T>>,
    natural: bool,
}

/// A retained equality policy.
///
/// Cloning a custom policy preserves its identity. Independently created custom policies are not
/// representation-compatible even when their comparison functions happen to agree. Canonical
/// policies — [`EqualityPolicy::natural`] and [`EqualityPolicy::reflexive_ieee`] — are compatible
/// across independent construction.
pub struct EqualityPolicy<T> {
    inner: Arc<EqualityPolicyInner<T>>,
}

impl<T> Clone for EqualityPolicy<T> {
    fn clone(&self) -> Self {
        Self {
            inner: Arc::clone(&self.inner),
        }
    }
}

impl<T> fmt::Debug for EqualityPolicy<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("EqualityPolicy")
            .field("natural", &self.inner.natural)
            .finish_non_exhaustive()
    }
}

impl<T> EqualityPolicy<T> {
    /// Creates a distinct custom equality policy.
    #[must_use]
    pub fn custom<C>(comparer: C) -> Self
    where
        C: EqualityComparer<T> + 'static,
    {
        Self {
            inner: Arc::new(EqualityPolicyInner {
                comparer: Arc::new(comparer),
                natural: false,
            }),
        }
    }

    /// Creates a custom equality policy retaining a caller-shared comparer identity.
    ///
    /// Independently constructed policies that receive clones of the same `Arc` are compatible for
    /// representation-level operations such as comparing or merging two delta structures.
    #[must_use]
    pub fn shared(comparer: Arc<dyn EqualityComparer<T>>) -> Self {
        Self {
            inner: Arc::new(EqualityPolicyInner {
                comparer,
                natural: false,
            }),
        }
    }

    /// Reports whether two values are equivalent under the retained policy.
    #[must_use]
    pub fn equals(&self, left: &T, right: &T) -> bool {
        self.inner.comparer.equals(left, right)
    }

    /// Returns whether this is a canonical policy.
    ///
    /// True for [`EqualityPolicy::natural`] and for [`EqualityPolicy::reflexive_ieee`], which is the
    /// canonical policy for float payloads; false for every custom or shared policy.
    #[must_use]
    pub fn is_natural(&self) -> bool {
        self.inner.natural
    }

    /// Returns whether two policies have the same retained identity.
    #[must_use]
    pub fn has_same_identity(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.inner.comparer, &other.inner.comparer)
    }

    /// Returns whether two policies may safely participate in representation-level operations.
    #[must_use]
    pub fn is_compatible_with(&self, other: &Self) -> bool {
        (self.inner.natural && other.inner.natural) || self.has_same_identity(other)
    }

    /// Creates a canonical policy: one whose identity is its rule, so independently constructed
    /// instances are compatible.
    fn canonical<C>(comparer: C) -> Self
    where
        C: EqualityComparer<T> + 'static,
    {
        Self {
            inner: Arc::new(EqualityPolicyInner {
                comparer: Arc::new(comparer),
                natural: true,
            }),
        }
    }
}

impl<T: Eq> EqualityPolicy<T> {
    /// Creates a canonical natural-equality policy.
    ///
    /// The bound is [`Eq`], not [`PartialEq`], because a retained policy must be an equivalence
    /// relation: a structure that cancels a change when the policy reports equality is silently
    /// wrong under a non-reflexive rule. This excludes raw `f32` and `f64`, whose `==` is not
    /// reflexive on `NaN`; use [`EqualityPolicy::reflexive_ieee`] for those.
    #[must_use]
    pub fn natural() -> Self {
        Self::canonical(NaturalEqualityComparer)
    }
}

impl EqualityPolicy<f64> {
    /// Creates the canonical reflexive IEEE-754 policy for `f64`.
    ///
    /// `NaN` is equivalent to `NaN` and `-0.0` is equivalent to `+0.0`, which is both an equivalence
    /// relation and the behavior of `EqualityComparer<double>.Default` in the C# baseline. Being
    /// canonical, two independently constructed instances are [`EqualityPolicy::is_compatible_with`]
    /// each other, exactly as natural policies are.
    #[must_use]
    pub fn reflexive_ieee() -> Self {
        Self::canonical(ReflexiveIeeeComparer)
    }
}

impl EqualityPolicy<f32> {
    /// Creates the canonical reflexive IEEE-754 policy for `f32`.
    ///
    /// `NaN` is equivalent to `NaN` and `-0.0` is equivalent to `+0.0`, which is both an equivalence
    /// relation and the behavior of `EqualityComparer<float>.Default` in the C# baseline. Being
    /// canonical, two independently constructed instances are [`EqualityPolicy::is_compatible_with`]
    /// each other, exactly as natural policies are.
    #[must_use]
    pub fn reflexive_ieee() -> Self {
        Self::canonical(ReflexiveIeeeComparer)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
    use std::thread;

    /// A value with no `Debug`, `Ord`, or `Hash` implementation, used to prove the policy surface
    /// needs none of them.
    #[derive(Clone, Copy, Eq, PartialEq)]
    struct Opaque(i32);

    fn case_insensitive() -> EqualityPolicy<String> {
        EqualityPolicy::custom(|left: &String, right: &String| {
            left.eq_ignore_ascii_case(right.as_str())
        })
    }

    #[test]
    fn natural_policy_uses_partial_eq() {
        let policy = EqualityPolicy::natural();
        assert!(policy.equals(&7, &7));
        assert!(!policy.equals(&7, &8));
        assert!(policy.is_natural());
    }

    #[test]
    fn natural_comparer_is_usable_directly() {
        let comparer = NaturalEqualityComparer;
        assert!(EqualityComparer::equals(&comparer, &"a", &"a"));
        assert!(!EqualityComparer::equals(&comparer, &"a", &"b"));
        assert!(EqualityComparer::equals(&comparer, &1_u8, &1_u8));
        assert!(!EqualityComparer::equals(&comparer, &1_u8, &2_u8));
    }

    #[test]
    fn reflexive_ieee_policy_is_an_equivalence_relation_over_floats() {
        let policy = EqualityPolicy::<f64>::reflexive_ieee();
        // Reflexivity is the property the natural policy cannot supply for floats: the raw comparer
        // is still reachable through a custom policy and still reports `NaN != NaN`.
        assert!(policy.equals(&f64::NAN, &f64::NAN));
        assert!(!EqualityComparer::equals(
            &NaturalEqualityComparer,
            &f64::NAN,
            &f64::NAN
        ));
        // Matching the .NET default comparer, the two zeros stay one class.
        assert!(policy.equals(&-0.0, &0.0));
        assert!(policy.equals(&1.5, &1.5));
        assert!(!policy.equals(&1.5, &f64::NAN));
        assert!(!policy.equals(&f64::INFINITY, &f64::NEG_INFINITY));
        assert!(policy.equals(&f64::INFINITY, &f64::INFINITY));

        let narrow = EqualityPolicy::<f32>::reflexive_ieee();
        assert!(narrow.equals(&f32::NAN, &f32::NAN));
        assert!(narrow.equals(&-0.0, &0.0));
        assert!(!narrow.equals(&1.5, &2.5));

        let comparer = ReflexiveIeeeComparer;
        assert!(EqualityComparer::equals(&comparer, &f64::NAN, &f64::NAN));
        assert!(EqualityComparer::equals(&comparer, &f32::NAN, &f32::NAN));
    }

    #[test]
    fn independently_created_reflexive_ieee_policies_are_compatible() {
        let left = EqualityPolicy::<f64>::reflexive_ieee();
        let right = EqualityPolicy::<f64>::reflexive_ieee();
        assert!(left.is_natural());
        assert!(left.is_compatible_with(&right));
        assert!(right.is_compatible_with(&left));
        // Canonical compatibility does not depend on sharing one allocation.
        assert!(!left.has_same_identity(&right));

        let custom = EqualityPolicy::custom(|left: &f64, right: &f64| left == right);
        assert!(!custom.is_natural());
        assert!(!left.is_compatible_with(&custom));
    }

    #[test]
    fn independently_created_natural_policies_are_compatible() {
        let left = EqualityPolicy::<i32>::natural();
        let right = EqualityPolicy::<i32>::natural();
        assert!(left.is_compatible_with(&right));
        assert!(right.is_compatible_with(&left));
        // Canonical compatibility does not depend on sharing one allocation.
        assert!(!left.has_same_identity(&right));
    }

    #[test]
    fn natural_and_custom_policies_are_incompatible() {
        let natural = EqualityPolicy::<String>::natural();
        let custom = case_insensitive();
        assert!(natural.is_natural());
        assert!(!custom.is_natural());
        assert!(!natural.is_compatible_with(&custom));
        assert!(!custom.is_compatible_with(&natural));
        assert!(!natural.has_same_identity(&custom));
    }

    #[test]
    fn independently_created_custom_policies_are_incompatible() {
        let left = case_insensitive();
        let right = case_insensitive();
        // Both agree on every input, but agreement between two closures is not provable.
        let inputs = [("Ab", "aB"), ("Ab", "ab"), ("Ab", "zz"), ("", "")];
        for (first, second) in inputs {
            let first = first.to_owned();
            let second = second.to_owned();
            assert_eq!(left.equals(&first, &second), right.equals(&first, &second));
        }
        assert!(!left.is_compatible_with(&right));
        assert!(!left.has_same_identity(&right));
    }

    #[test]
    fn clone_preserves_identity_and_naturalness() {
        let custom = case_insensitive();
        let derived = custom.clone();
        assert!(custom.has_same_identity(&derived));
        assert!(derived.has_same_identity(&custom));
        assert!(custom.is_compatible_with(&derived));
        assert!(!derived.is_natural());

        let natural = EqualityPolicy::<i32>::natural();
        let natural_clone = natural.clone();
        assert!(natural.has_same_identity(&natural_clone));
        assert!(natural_clone.is_natural());
        assert!(natural.is_compatible_with(&natural_clone));
    }

    #[test]
    fn compatibility_is_reflexive_symmetric_and_transitive_through_clones() {
        let root = case_insensitive();
        let first = root.clone();
        let second = first.clone();
        assert!(root.is_compatible_with(&root));
        assert!(root.is_compatible_with(&first) && first.is_compatible_with(&root));
        assert!(first.is_compatible_with(&second) && root.is_compatible_with(&second));
    }

    #[test]
    fn shared_comparer_identity_survives_independent_construction() {
        let comparer: Arc<dyn EqualityComparer<String>> =
            Arc::new(|left: &String, right: &String| left.len() == right.len());
        let left = EqualityPolicy::shared(Arc::clone(&comparer));
        let right = EqualityPolicy::shared(comparer);
        assert!(left.has_same_identity(&right));
        assert!(left.is_compatible_with(&right));
        assert!(!left.is_natural());
        assert!(left.equals(&"ab".to_owned(), &"xy".to_owned()));
        assert!(!left.equals(&"ab".to_owned(), &"xyz".to_owned()));

        // A separate allocation of the same closure is a separate identity.
        let other = EqualityPolicy::shared(Arc::new(|left: &String, right: &String| {
            left.len() == right.len()
        }));
        assert!(!left.has_same_identity(&other));
        assert!(!left.is_compatible_with(&other));
    }

    #[test]
    fn closure_policy_defines_no_ops_and_cancellation() {
        // A coarser-than-natural policy is what makes a write a semantic no-op: the checkpoint and
        // the candidate differ naturally yet cancel under the retained rule.
        let policy = case_insensitive();
        let checkpoint = "Value".to_owned();
        let candidate = "vALUE".to_owned();
        assert!(checkpoint != candidate);
        assert!(policy.equals(&checkpoint, &candidate));

        // A finer-than-natural policy makes a naturally equal write a real change.
        let counter = AtomicUsize::new(0);
        let never_equal = EqualityPolicy::custom(move |_: &i32, _: &i32| {
            counter.fetch_add(1, AtomicOrdering::Relaxed);
            false
        });
        assert!(!never_equal.equals(&1, &1));
    }

    #[test]
    fn custom_policy_accepts_a_named_comparer_type() {
        struct ByMagnitude;

        impl EqualityComparer<i64> for ByMagnitude {
            fn equals(&self, left: &i64, right: &i64) -> bool {
                left.unsigned_abs() == right.unsigned_abs()
            }
        }

        let policy = EqualityPolicy::custom(ByMagnitude);
        assert!(policy.equals(&-5, &5));
        assert!(!policy.equals(&-5, &6));
        assert!(!policy.is_natural());
    }

    #[test]
    fn debug_does_not_require_element_debug() {
        let natural = EqualityPolicy::<Opaque>::natural();
        let custom = EqualityPolicy::custom(|left: &Opaque, right: &Opaque| left.0 == right.0);
        let natural_text = format!("{natural:?}");
        let custom_text = format!("{custom:?}");
        assert!(natural_text.contains("EqualityPolicy"));
        assert!(natural_text.contains("natural: true"));
        assert!(custom_text.contains("natural: false"));
        assert!(natural.equals(&Opaque(3), &Opaque(3)));
        assert!(!natural.equals(&Opaque(3), &Opaque(4)));
    }

    #[test]
    fn policies_are_send_and_sync_and_shareable_across_threads() {
        fn assert_send_sync<P: Send + Sync>() {}
        assert_send_sync::<EqualityPolicy<String>>();
        assert_send_sync::<NaturalEqualityComparer>();

        let policy = case_insensitive();
        let results: Vec<bool> = thread::scope(|scope| {
            let handles: Vec<_> = (0..4)
                .map(|index| {
                    let policy = policy.clone();
                    scope.spawn(move || {
                        let left = format!("Item{index}");
                        let right = format!("iTEM{index}");
                        assert!(policy.is_compatible_with(&policy));
                        policy.equals(&left, &right)
                    })
                })
                .collect();
            handles
                .into_iter()
                .map(|handle| handle.join().unwrap())
                .collect()
        });
        assert_eq!(results, vec![true; 4]);
    }
}
