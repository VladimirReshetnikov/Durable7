use std::cmp::Ordering;
use std::fmt;
use std::sync::Arc;

/// Compares values for ordering and equivalence.
///
/// Implementations must define a stable total preorder. Returning [`Ordering::Equal`] places two
/// concrete values in the same ordering-equivalence class.
pub trait OrderComparer<T>: Send + Sync {
    /// Compares two values.
    fn compare(&self, left: &T, right: &T) -> Ordering;
}

impl<T, F> OrderComparer<T> for F
where
    F: Fn(&T, &T) -> Ordering + Send + Sync,
{
    fn compare(&self, left: &T, right: &T) -> Ordering {
        self(left, right)
    }
}

/// Natural [`Ord`] comparison.
#[derive(Clone, Copy, Debug, Default)]
pub struct NaturalOrderComparer;

impl<T: Ord> OrderComparer<T> for NaturalOrderComparer {
    fn compare(&self, left: &T, right: &T) -> Ordering {
        left.cmp(right)
    }
}

struct OrderPolicyInner<T> {
    comparer: Arc<dyn OrderComparer<T>>,
    natural: bool,
}

/// A retained ordering policy.
///
/// Cloning a custom policy preserves its identity. Independently created custom policies are not
/// representation-compatible even when their comparison functions happen to agree. Natural-order
/// policies are canonical and therefore compatible across independent construction.
pub struct OrderPolicy<T> {
    inner: Arc<OrderPolicyInner<T>>,
}

impl<T> Clone for OrderPolicy<T> {
    fn clone(&self) -> Self {
        Self {
            inner: Arc::clone(&self.inner),
        }
    }
}

impl<T> fmt::Debug for OrderPolicy<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("OrderPolicy")
            .field("natural", &self.inner.natural)
            .finish_non_exhaustive()
    }
}

impl<T> OrderPolicy<T> {
    /// Creates a distinct custom ordering policy.
    #[must_use]
    pub fn custom<C>(comparer: C) -> Self
    where
        C: OrderComparer<T> + 'static,
    {
        Self {
            inner: Arc::new(OrderPolicyInner {
                comparer: Arc::new(comparer),
                natural: false,
            }),
        }
    }

    /// Creates a custom ordering policy retaining a caller-shared comparer identity.
    ///
    /// Independently constructed policies that receive clones of the same `Arc` are compatible
    /// for representation-level operations such as Brodal-Okasaki melding.
    #[must_use]
    pub fn shared(comparer: Arc<dyn OrderComparer<T>>) -> Self {
        Self {
            inner: Arc::new(OrderPolicyInner {
                comparer,
                natural: false,
            }),
        }
    }

    /// Compares two values using the retained policy.
    #[must_use]
    pub fn compare(&self, left: &T, right: &T) -> Ordering {
        self.inner.comparer.compare(left, right)
    }

    /// Returns whether this is a canonical natural-order policy.
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
}

impl<T: Ord> OrderPolicy<T> {
    /// Creates a canonical natural-order policy.
    #[must_use]
    pub fn natural() -> Self {
        Self {
            inner: Arc::new(OrderPolicyInner {
                comparer: Arc::new(NaturalOrderComparer),
                natural: true,
            }),
        }
    }
}
