//! Persistent Brodal-Okasaki heap with lazily composable monotone priority actions.
//!
//! [`PersistentMonotoneActionHeap`] is the action-tagged sibling of
//! [`BrodalOkasakiHeap`](crate::BrodalOkasakiHeap). It keeps the same bootstrapped skew-binomial
//! representation — a global minimum root above a skew-binomial forest — and adds one immutable
//! *pending action* to every tree and every forest spine cell. A tree tag applies to that tree and
//! all of its descendants; a forest tag applies uniformly to every tree in that suffix.
//!
//! The point of the tags is [`PersistentMonotoneActionHeap::transform_all`], which applies one
//! monotone action to every priority *currently* in a version by composing a single tag at the
//! root. It costs O(1) worst-case time and allocates O(1) new structure. Its semantics are
//! deliberately temporal: entries inserted later, and entries arriving through a later meld, are
//! **not** retroactively transformed. Two heaps transformed independently can still meld in O(1)
//! worst-case time, including when the actions are non-invertible (a clamp has no inverse).
//!
//! The ordinary heap bounds are unchanged: insert, minimum, and meld are O(1) worst-case;
//! delete-min is O(log n) worst-case; enumeration and [`validate_structure`] are Theta(n); forking
//! by retaining a version is O(1). These bounds include the O(1)-time action-policy calls and
//! priority comparisons they perform, so a policy whose `compose`/`apply`/`is_identity` is not
//! O(1), or whose composed actions are not fixed size, invalidates them.
//!
//! Ordering comes from a retained [`OrderPolicy`](crate::ordering::OrderPolicy) and the action
//! family from a retained [`ActionPolicy`], both *values* rather than trait bounds.
//! [`PersistentMonotoneActionHeap::meld`] rejects operands whose policies are incompatible with
//! [`MonotoneActionMeldError`] instead of silently mixing two ordering rules or two action
//! algebras. [`OrderClampPolicy`] is the shipped closed family — identity, floor, cap, validated
//! two-sided clamp, and exact constant — and it names the exact order policy it is monotone for,
//! so heap creation rejects any other one with [`MonotoneActionPolicyError`].
//!
//! The action policy is trusted. Violating its identity, associative-composition, monotonicity, or
//! O(1)-operation contract invalidates the semantic and complexity guarantees. Every update is
//! persistent and leaves all source versions usable; a failed operation publishes nothing.
//!
//! [`validate_structure`]: PersistentMonotoneActionHeap::validate_structure

use crate::ordering::OrderPolicy;
use std::error::Error;
use std::fmt;
use std::hash::{Hash, Hasher};
use std::sync::Arc;

// ---------------------------------------------------------------------------------------------
// Action algebra
// ---------------------------------------------------------------------------------------------

/// A constant-time-composable monotone action on heap priorities.
///
/// Implementations must supply a semantic identity and an associative composition, where
/// `compose(outer, inner)` denotes `outer(inner(priority))`. Every action must be monotone for the
/// order policy retained by its heap: if `p <= q` then `apply(a, p) <= apply(a, q)`. Identity
/// testing, composition, and application are part of the heap's advertised bounds and must each
/// take O(1) time over fixed-size actions. Results must remain deterministic and semantically
/// stable for the lifetime of every heap version.
///
/// Priorities are passed and returned as `Arc<P>` so that a policy neither requires `P: Clone` nor
/// loses the exact representative it chose. That matters when the order policy considers distinct
/// priority values equal: an exact-constant action must return its own stored representative.
pub trait MonotoneHeapAction<P, A>: Send + Sync {
    /// Returns the semantic identity action.
    fn identity(&self) -> A;

    /// Returns whether `action` is the semantic identity.
    fn is_identity(&self, action: &A) -> bool;

    /// Composes two actions as `outer(inner(priority))`.
    fn compose(&self, outer: &A, inner: &A) -> A;

    /// Applies one action to one priority.
    fn apply(&self, action: &A, priority: &Arc<P>) -> Arc<P>;
}

struct ActionPolicyInner<P, A> {
    action: Arc<dyn MonotoneHeapAction<P, A>>,
    bound_order: Option<OrderPolicy<P>>,
}

/// A retained monotone-action policy.
///
/// Policy identity is representation-compatibility state, exactly as for
/// [`OrderPolicy`](crate::ordering::OrderPolicy): cloning preserves identity, so derived versions
/// stay meldable with their source, while two independently constructed policies are distinct even
/// when their action algebras agree — there is no way to prove that two implementations compose
/// the same way.
///
/// A policy may be *bound* to the exact order policy it is monotone for. Heap creation then adopts
/// that order policy, and rejects any other one.
pub struct ActionPolicy<P, A> {
    inner: Arc<ActionPolicyInner<P, A>>,
}

impl<P, A> Clone for ActionPolicy<P, A> {
    fn clone(&self) -> Self {
        Self {
            inner: Arc::clone(&self.inner),
        }
    }
}

impl<P, A> fmt::Debug for ActionPolicy<P, A> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("ActionPolicy")
            .field("bound", &self.inner.bound_order.is_some())
            .finish_non_exhaustive()
    }
}

impl<P, A> ActionPolicy<P, A> {
    /// Creates a distinct policy that is monotone for whatever order policy the caller supplies.
    #[must_use]
    pub fn custom<M>(action: M) -> Self
    where
        M: MonotoneHeapAction<P, A> + 'static,
    {
        Self {
            inner: Arc::new(ActionPolicyInner {
                action: Arc::new(action),
                bound_order: None,
            }),
        }
    }

    /// Creates a distinct policy that is monotone only for `order`.
    #[must_use]
    pub fn bound<M>(action: M, order: OrderPolicy<P>) -> Self
    where
        M: MonotoneHeapAction<P, A> + 'static,
    {
        Self {
            inner: Arc::new(ActionPolicyInner {
                action: Arc::new(action),
                bound_order: Some(order),
            }),
        }
    }

    /// Creates a policy retaining a caller-shared action-object identity.
    ///
    /// Policies built from clones of the same `Arc` may meld. This is the direct analogue of
    /// retaining the same policy object in managed ports.
    #[must_use]
    pub fn shared(
        action: Arc<dyn MonotoneHeapAction<P, A>>,
        bound_order: Option<OrderPolicy<P>>,
    ) -> Self {
        Self {
            inner: Arc::new(ActionPolicyInner {
                action,
                bound_order,
            }),
        }
    }

    /// Returns the exact order policy this action family is monotone for, when it names one.
    #[must_use]
    pub fn bound_order(&self) -> Option<&OrderPolicy<P>> {
        self.inner.bound_order.as_ref()
    }

    /// Returns the semantic identity action.
    #[must_use]
    pub fn identity(&self) -> A {
        self.inner.action.identity()
    }

    /// Returns whether `action` is the semantic identity.
    #[must_use]
    pub fn is_identity(&self, action: &A) -> bool {
        self.inner.action.is_identity(action)
    }

    /// Composes two actions as `outer(inner(priority))`.
    #[must_use]
    pub fn compose(&self, outer: &A, inner: &A) -> A {
        self.inner.action.compose(outer, inner)
    }

    /// Applies one action to one priority.
    #[must_use]
    pub fn apply(&self, action: &A, priority: &Arc<P>) -> Arc<P> {
        self.inner.action.apply(action, priority)
    }

    /// Returns whether two policies retain the same action-object identity.
    #[must_use]
    pub fn has_same_identity(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.inner.action, &other.inner.action)
    }

    /// Returns whether two policies may participate in one representation-level operation.
    #[must_use]
    pub fn is_compatible_with(&self, other: &Self) -> bool {
        self.has_same_identity(other)
    }
}

// ---------------------------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------------------------

/// A heap could not be created from the supplied action and order policies.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MonotoneActionPolicyError {
    /// The action policy names no order policy, so none can be adopted.
    UnboundActionPolicy,
    /// The action policy is monotone only for a different order policy.
    MismatchedOrderPolicy,
}

impl fmt::Display for MonotoneActionPolicyError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnboundActionPolicy => formatter
                .write_str("the action policy names no order policy, so one must be supplied"),
            Self::MismatchedOrderPolicy => formatter.write_str(
                "the order policy must be the exact policy retained by the action policy",
            ),
        }
    }
}

impl Error for MonotoneActionPolicyError {}

/// Meld was requested for heaps whose retained order or action policies are incompatible.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MonotoneActionMeldError;

impl fmt::Display for MonotoneActionMeldError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(
            "action-heap melding requires the same order-policy and action-policy identities",
        )
    }
}

impl Error for MonotoneActionMeldError {}

/// A structural invariant violation found by
/// [`PersistentMonotoneActionHeap::validate_structure`].
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MonotoneActionInvariantError {
    message: &'static str,
}

impl MonotoneActionInvariantError {
    fn new(message: &'static str) -> Self {
        Self { message }
    }
}

impl fmt::Display for MonotoneActionInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.message)
    }
}

impl Error for MonotoneActionInvariantError {}

/// A two-sided clamp was requested whose lower bound compares greater than its upper bound.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ClampBoundsError;

impl fmt::Display for ClampBoundsError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("a clamp lower bound must not compare greater than its upper bound")
    }
}

impl Error for ClampBoundsError {}

// ---------------------------------------------------------------------------------------------
// Shipped clamp action family
// ---------------------------------------------------------------------------------------------

/// A lower bound, an upper bound, both bounds, an exact constant, or the identity.
///
/// Values are created by [`OrderClampPolicy`] so that two-bound actions are validated with the
/// same order policy used for application and composition. A missing bound denotes negative or
/// positive infinity. Bounds and constants are retained as `Arc<T>`, which is what lets
/// composition and application hand back the exact representative they selected.
pub struct OrderClamp<T> {
    constant: Option<Arc<T>>,
    lower: Option<Arc<T>>,
    upper: Option<Arc<T>>,
}

impl<T> Clone for OrderClamp<T> {
    fn clone(&self) -> Self {
        Self {
            constant: self.constant.clone(),
            lower: self.lower.clone(),
            upper: self.upper.clone(),
        }
    }
}

impl<T> Default for OrderClamp<T> {
    fn default() -> Self {
        Self::identity()
    }
}

impl<T: fmt::Debug> fmt::Debug for OrderClamp<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("OrderClamp")
            .field("constant", &self.constant)
            .field("lower", &self.lower)
            .field("upper", &self.upper)
            .finish()
    }
}

impl<T: PartialEq> PartialEq for OrderClamp<T> {
    fn eq(&self, other: &Self) -> bool {
        fn same<T: PartialEq>(left: &Option<Arc<T>>, right: &Option<Arc<T>>) -> bool {
            match (left, right) {
                (None, None) => true,
                (Some(left), Some(right)) => left.as_ref() == right.as_ref(),
                _ => false,
            }
        }
        same(&self.constant, &other.constant)
            && same(&self.lower, &other.lower)
            && same(&self.upper, &other.upper)
    }
}

impl<T: Eq> Eq for OrderClamp<T> {}

impl<T: Hash> Hash for OrderClamp<T> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.constant.as_deref().hash(state);
        self.lower.as_deref().hash(state);
        self.upper.as_deref().hash(state);
    }
}

impl<T> OrderClamp<T> {
    /// Returns the identity action.
    #[must_use]
    pub fn identity() -> Self {
        Self {
            constant: None,
            lower: None,
            upper: None,
        }
    }

    /// Returns whether this action is an exact constant function.
    #[must_use]
    pub fn is_constant(&self) -> bool {
        self.constant.is_some()
    }

    /// Returns the constant result, or `None` when the action is not constant.
    #[must_use]
    pub fn constant_value(&self) -> Option<&T> {
        self.constant.as_deref()
    }

    /// Returns whether this action has a lower bound.
    #[must_use]
    pub fn has_lower_bound(&self) -> bool {
        self.lower.is_some()
    }

    /// Returns whether this action has an upper bound.
    #[must_use]
    pub fn has_upper_bound(&self) -> bool {
        self.upper.is_some()
    }

    /// Returns the lower bound, or `None` when the action has none.
    #[must_use]
    pub fn lower_bound(&self) -> Option<&T> {
        self.lower.as_deref()
    }

    /// Returns the upper bound, or `None` when the action has none.
    #[must_use]
    pub fn upper_bound(&self) -> Option<&T> {
        self.upper.as_deref()
    }
}

/// The closed monotone action family `x -> clamp(x, lower, upper)`.
///
/// Missing bounds denote negative or positive infinity, and composition always yields either one
/// constant-sized clamp or an explicit constant function, so `compose` and `apply` are O(1). The
/// explicit constant case preserves exact results even when the order policy treats distinct
/// priority values as equal: an inclusive `[k, k]` clamp preserves an input equivalent to `k`,
/// whereas the constant function must return the exact `k` representative. For overlapping
/// clamps, composition keeps the older boundary when two boundary values compare equal.
///
/// The retained order policy is the exact policy this family is monotone for. Wrapping the family
/// with [`OrderClampPolicy::into_action_policy`] binds that policy, so heap creation adopts it and
/// rejects any other one.
pub struct OrderClampPolicy<T> {
    order: OrderPolicy<T>,
}

impl<T> Clone for OrderClampPolicy<T> {
    fn clone(&self) -> Self {
        Self {
            order: self.order.clone(),
        }
    }
}

impl<T> fmt::Debug for OrderClampPolicy<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("OrderClampPolicy")
            .field("order", &self.order)
            .finish()
    }
}

impl<T: Ord> Default for OrderClampPolicy<T> {
    fn default() -> Self {
        Self::natural()
    }
}

impl<T: Ord> OrderClampPolicy<T> {
    /// Creates a clamp family over canonical natural ordering.
    #[must_use]
    pub fn natural() -> Self {
        Self::with_order(OrderPolicy::natural())
    }
}

impl<T> OrderClampPolicy<T> {
    /// Creates a clamp family monotone for `order`.
    #[must_use]
    pub fn with_order(order: OrderPolicy<T>) -> Self {
        Self { order }
    }

    /// Returns the exact order policy this family is monotone for.
    #[must_use]
    pub fn order_policy(&self) -> &OrderPolicy<T> {
        &self.order
    }

    /// Creates a floor action.
    #[must_use]
    pub fn at_least(&self, lower_bound: impl Into<Arc<T>>) -> OrderClamp<T> {
        OrderClamp {
            constant: None,
            lower: Some(lower_bound.into()),
            upper: None,
        }
    }

    /// Creates a cap action.
    #[must_use]
    pub fn at_most(&self, upper_bound: impl Into<Arc<T>>) -> OrderClamp<T> {
        OrderClamp {
            constant: None,
            lower: None,
            upper: Some(upper_bound.into()),
        }
    }

    /// Creates an exact constant action.
    #[must_use]
    pub fn constant(&self, value: impl Into<Arc<T>>) -> OrderClamp<T> {
        OrderClamp {
            constant: Some(value.into()),
            lower: None,
            upper: None,
        }
    }

    /// Creates a two-sided clamp action.
    ///
    /// Fails with [`ClampBoundsError`] when the lower bound compares greater than the upper bound.
    pub fn between(
        &self,
        lower_bound: impl Into<Arc<T>>,
        upper_bound: impl Into<Arc<T>>,
    ) -> Result<OrderClamp<T>, ClampBoundsError> {
        let lower = lower_bound.into();
        let upper = upper_bound.into();
        if self.order.compare(&lower, &upper).is_gt() {
            return Err(ClampBoundsError);
        }
        Ok(OrderClamp {
            constant: None,
            lower: Some(lower),
            upper: Some(upper),
        })
    }

    /// Wraps this family in a retained action policy bound to its order policy.
    ///
    /// Each call mints a fresh policy identity, exactly as
    /// [`OrderPolicy::custom`](crate::ordering::OrderPolicy::custom) does. Clone the returned value
    /// to build meldable heaps; construct it twice to build deliberately incompatible ones.
    #[must_use]
    pub fn into_action_policy(self) -> ActionPolicy<T, OrderClamp<T>>
    where
        T: Send + Sync + 'static,
    {
        let order = self.order.clone();
        ActionPolicy::bound(self, order)
    }
}

impl<T> MonotoneHeapAction<T, OrderClamp<T>> for OrderClampPolicy<T>
where
    T: Send + Sync,
{
    fn identity(&self) -> OrderClamp<T> {
        OrderClamp::identity()
    }

    fn is_identity(&self, action: &OrderClamp<T>) -> bool {
        action.constant.is_none() && action.lower.is_none() && action.upper.is_none()
    }

    fn compose(&self, outer: &OrderClamp<T>, inner: &OrderClamp<T>) -> OrderClamp<T> {
        if self.is_identity(outer) {
            return inner.clone();
        }
        if self.is_identity(inner) {
            return outer.clone();
        }
        if outer.constant.is_some() {
            return outer.clone();
        }
        if let Some(value) = &inner.constant {
            return OrderClamp {
                constant: Some(self.apply(outer, value)),
                lower: None,
                upper: None,
            };
        }

        // Disjoint bounds collapse to an explicit constant carrying the newer boundary.
        if let (Some(inner_upper), Some(outer_lower)) = (&inner.upper, &outer.lower)
            && self.order.compare(inner_upper, outer_lower).is_lt()
        {
            return OrderClamp {
                constant: Some(Arc::clone(outer_lower)),
                lower: None,
                upper: None,
            };
        }
        if let (Some(inner_lower), Some(outer_upper)) = (&inner.lower, &outer.upper)
            && self.order.compare(inner_lower, outer_upper).is_gt()
        {
            return OrderClamp {
                constant: Some(Arc::clone(outer_upper)),
                lower: None,
                upper: None,
            };
        }

        // Overlapping bounds intersect; equal boundaries keep the older (inner) representative.
        let lower = match (&inner.lower, &outer.lower) {
            (None, outer_lower) => outer_lower.clone(),
            (Some(inner_lower), None) => Some(Arc::clone(inner_lower)),
            (Some(inner_lower), Some(outer_lower)) => {
                Some(if self.order.compare(inner_lower, outer_lower).is_ge() {
                    Arc::clone(inner_lower)
                } else {
                    Arc::clone(outer_lower)
                })
            }
        };
        let upper = match (&inner.upper, &outer.upper) {
            (None, outer_upper) => outer_upper.clone(),
            (Some(inner_upper), None) => Some(Arc::clone(inner_upper)),
            (Some(inner_upper), Some(outer_upper)) => {
                Some(if self.order.compare(inner_upper, outer_upper).is_le() {
                    Arc::clone(inner_upper)
                } else {
                    Arc::clone(outer_upper)
                })
            }
        };
        OrderClamp {
            constant: None,
            lower,
            upper,
        }
    }

    fn apply(&self, action: &OrderClamp<T>, priority: &Arc<T>) -> Arc<T> {
        if let Some(value) = &action.constant {
            return Arc::clone(value);
        }
        if let Some(lower) = &action.lower
            && self.order.compare(priority, lower).is_lt()
        {
            return Arc::clone(lower);
        }
        if let Some(upper) = &action.upper
            && self.order.compare(priority, upper).is_gt()
        {
            return Arc::clone(upper);
        }
        Arc::clone(priority)
    }
}

// ---------------------------------------------------------------------------------------------
// Representation
// ---------------------------------------------------------------------------------------------

type TreeLink<E, P, A> = Option<Arc<Tree<E, P, A>>>;
type ForestLink<E, P, A> = Option<Arc<Forest<E, P, A>>>;

/// A minimum-ranked tree lifted out of a forest, paired with the forest that remains.
type MinimumSplit<E, P, A> = (Arc<Tree<E, P, A>>, ForestLink<E, P, A>);

/// One skew-binomial tree carrying a pending action for itself and all of its descendants.
struct Tree<E, P, A> {
    rank: usize,
    element: Arc<E>,
    /// Raw priority; the logical priority is `apply(action, priority)`.
    priority: Arc<P>,
    /// Raw children; the logical children are `tag_forest(children, action)`.
    children: ForestLink<E, P, A>,
    action: A,
}

/// One forest spine cell carrying a pending action for its whole suffix.
struct Forest<E, P, A> {
    /// Raw head; the logical head is `tag_tree(head, action)`.
    head: Arc<Tree<E, P, A>>,
    /// Raw tail; the logical tail is `tag_forest(tail, action)`.
    tail: ForestLink<E, P, A>,
    action: A,
}

struct SplitForest<E, P, A> {
    zeros: ForestLink<E, P, A>,
    trees: ForestLink<E, P, A>,
    embedded_forest: ForestLink<E, P, A>,
}

impl<P, A> ActionPolicy<P, A> {
    /// Pushes a newer action onto one tree, composing it with the tree's pending action.
    fn tag_tree<E>(&self, node: &Arc<Tree<E, P, A>>, outer: &A) -> Arc<Tree<E, P, A>> {
        if self.is_identity(outer) {
            return Arc::clone(node);
        }
        Arc::new(Tree {
            rank: node.rank,
            element: Arc::clone(&node.element),
            priority: Arc::clone(&node.priority),
            children: node.children.clone(),
            action: self.compose(outer, &node.action),
        })
    }

    /// Pushes a newer action onto one forest suffix, composing it with the suffix's action.
    fn tag_forest<E>(&self, link: &Arc<Forest<E, P, A>>, outer: &A) -> Arc<Forest<E, P, A>> {
        if self.is_identity(outer) {
            return Arc::clone(link);
        }
        Arc::new(Forest {
            head: Arc::clone(&link.head),
            tail: link.tail.clone(),
            action: self.compose(outer, &link.action),
        })
    }

    /// Materializes one tree's pending action into its own priority and its child forest.
    fn expose<E>(&self, node: &Arc<Tree<E, P, A>>) -> Arc<Tree<E, P, A>> {
        if self.is_identity(&node.action) {
            return Arc::clone(node);
        }
        Arc::new(Tree {
            rank: node.rank,
            element: Arc::clone(&node.element),
            priority: self.apply(&node.action, &node.priority),
            children: node
                .children
                .as_ref()
                .map(|link| self.tag_forest(link, &node.action)),
            action: self.identity(),
        })
    }

    /// Builds an identity-tagged spine cell, so an attached tree keeps only its own history.
    fn cons<E>(
        &self,
        head: Arc<Tree<E, P, A>>,
        tail: ForestLink<E, P, A>,
    ) -> Arc<Forest<E, P, A>> {
        Arc::new(Forest {
            head,
            tail,
            action: self.identity(),
        })
    }

    /// Returns the logical head of a spine cell.
    fn forest_head<E>(&self, link: &Arc<Forest<E, P, A>>) -> Arc<Tree<E, P, A>> {
        self.tag_tree(&link.head, &link.action)
    }

    /// Returns the logical tail of a spine cell, retaining its uniform action.
    fn forest_tail<E>(&self, link: &Arc<Forest<E, P, A>>) -> ForestLink<E, P, A> {
        link.tail
            .as_ref()
            .map(|tail| self.tag_forest(tail, &link.action))
    }

    /// Returns the logical priority of one tree.
    fn logical_priority<E>(&self, node: &Tree<E, P, A>) -> Arc<P> {
        self.apply(&node.action, &node.priority)
    }

    /// Collects the logical trees of a forest in spine order.
    fn forest_to_vec<E>(&self, mut current: ForestLink<E, P, A>) -> Vec<Arc<Tree<E, P, A>>> {
        let mut result = Vec::new();
        while let Some(link) = current {
            result.push(self.forest_head(&link));
            current = self.forest_tail(&link);
        }
        result
    }
}

// ---------------------------------------------------------------------------------------------
// Entries and views
// ---------------------------------------------------------------------------------------------

/// A payload paired with its current logical priority.
///
/// Both halves are owned shared handles, so an entry stays usable independently of the heap
/// version it came from without requiring `E: Clone` or `P: Clone`.
pub struct MonotoneHeapEntry<E, P> {
    /// The payload.
    pub element: Arc<E>,
    /// The logical priority, with every pending action already applied.
    pub priority: Arc<P>,
}

impl<E, P> MonotoneHeapEntry<E, P> {
    /// Pairs an owned payload with an owned priority.
    #[must_use]
    pub fn new(element: E, priority: P) -> Self {
        Self {
            element: Arc::new(element),
            priority: Arc::new(priority),
        }
    }
}

impl<E, P> Clone for MonotoneHeapEntry<E, P> {
    fn clone(&self) -> Self {
        Self {
            element: Arc::clone(&self.element),
            priority: Arc::clone(&self.priority),
        }
    }
}

impl<E: fmt::Debug, P: fmt::Debug> fmt::Debug for MonotoneHeapEntry<E, P> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("MonotoneHeapEntry")
            .field("element", &self.element)
            .field("priority", &self.priority)
            .finish()
    }
}

impl<E: PartialEq, P: PartialEq> PartialEq for MonotoneHeapEntry<E, P> {
    fn eq(&self, other: &Self) -> bool {
        self.element.as_ref() == other.element.as_ref()
            && self.priority.as_ref() == other.priority.as_ref()
    }
}

impl<E: Eq, P: Eq> Eq for MonotoneHeapEntry<E, P> {}

/// One successful minimum deletion.
pub struct MonotoneActionMinimumView<E, P, A> {
    /// The removed minimum entry.
    pub minimum: MonotoneHeapEntry<E, P>,
    /// The persistent heap after removing that entry.
    pub remainder: PersistentMonotoneActionHeap<E, P, A>,
}

impl<E: fmt::Debug, P: fmt::Debug, A> fmt::Debug for MonotoneActionMinimumView<E, P, A> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("MonotoneActionMinimumView")
            .field("minimum", &self.minimum)
            .finish_non_exhaustive()
    }
}

/// Statistics returned by a complete monotone-action-heap representation audit.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct PersistentMonotoneActionHeapStatistics {
    /// Logical entry count.
    pub len: usize,
    /// Number of skew-binomial trees immediately below the global root.
    pub root_forest_len: usize,
    /// Largest skew-binomial rank encountered.
    pub maximum_rank: usize,
    /// Deepest global-root-to-tree path.
    pub maximum_depth: usize,
    /// Number of pending tree/forest-tag observations made during the audit.
    ///
    /// This is not a distinct-object count: exposing one uniform forest tag produces several
    /// tagged logical views of the same retained cells.
    pub tagged_component_count: usize,
}

// ---------------------------------------------------------------------------------------------
// Heap
// ---------------------------------------------------------------------------------------------

/// An immutable bootstrapped skew-binomial min-heap with pending monotone priority actions.
///
/// Insert, minimum, and meld are O(1) worst-case; delete-min is O(log n) worst-case;
/// [`transform_all`](Self::transform_all) is O(1) worst-case time and allocates O(1) new
/// structure; enumeration and [`validate_structure`](Self::validate_structure) are Theta(n);
/// retaining a version as a fork is O(1). These bounds assume fixed-size actions with O(1)
/// identity, composition, application, and priority comparison.
///
/// Payloads, priorities, and structural nodes are retained through [`Arc`], so no operation
/// requires `E: Clone` or `P: Clone`. Snapshots are `Send + Sync` whenever `E`, `P`, and `A` are.
pub struct PersistentMonotoneActionHeap<E, P, A> {
    root: TreeLink<E, P, A>,
    len: usize,
    order: OrderPolicy<P>,
    action_policy: ActionPolicy<P, A>,
}

impl<E, P, A> Clone for PersistentMonotoneActionHeap<E, P, A> {
    fn clone(&self) -> Self {
        Self {
            root: self.root.clone(),
            len: self.len,
            order: self.order.clone(),
            action_policy: self.action_policy.clone(),
        }
    }
}

impl<E, P, A> fmt::Debug for PersistentMonotoneActionHeap<E, P, A>
where
    E: fmt::Debug,
    P: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("PersistentMonotoneActionHeap")
            .field("len", &self.len)
            .field("minimum", &self.minimum())
            .field("order", &self.order)
            .field("action_policy", &self.action_policy)
            .finish_non_exhaustive()
    }
}

impl<E, P, A> PersistentMonotoneActionHeap<E, P, A> {
    /// Creates an empty heap adopting the order policy named by `action_policy`.
    ///
    /// Fails with [`MonotoneActionPolicyError::UnboundActionPolicy`] when the action policy names
    /// none, because there is then no policy it promises to be monotone for.
    pub fn with_policy(
        action_policy: ActionPolicy<P, A>,
    ) -> Result<Self, MonotoneActionPolicyError> {
        let Some(order) = action_policy.bound_order().cloned() else {
            return Err(MonotoneActionPolicyError::UnboundActionPolicy);
        };
        Ok(Self {
            root: None,
            len: 0,
            order,
            action_policy,
        })
    }

    /// Creates an empty heap retaining both policies.
    ///
    /// Fails with [`MonotoneActionPolicyError::MismatchedOrderPolicy`] when `action_policy` names
    /// a different order policy: its monotonicity promise would not cover `order`.
    pub fn with_policies(
        action_policy: ActionPolicy<P, A>,
        order: OrderPolicy<P>,
    ) -> Result<Self, MonotoneActionPolicyError> {
        if let Some(bound) = action_policy.bound_order()
            && !bound.is_compatible_with(&order)
        {
            return Err(MonotoneActionPolicyError::MismatchedOrderPolicy);
        }
        Ok(Self {
            root: None,
            len: 0,
            order,
            action_policy,
        })
    }

    /// Builds a heap from entries in O(n) time by repeated worst-case-O(1) insertion, adopting the
    /// order policy named by `action_policy`.
    pub fn from_entries<I>(
        entries: I,
        action_policy: ActionPolicy<P, A>,
    ) -> Result<Self, MonotoneActionPolicyError>
    where
        I: IntoIterator<Item = (E, P)>,
    {
        let mut result = Self::with_policy(action_policy)?;
        for (element, priority) in entries {
            result = result.insert(element, priority);
        }
        Ok(result)
    }

    /// Builds a heap from entries in O(n) time by repeated worst-case-O(1) insertion.
    pub fn from_entries_with_policies<I>(
        entries: I,
        action_policy: ActionPolicy<P, A>,
        order: OrderPolicy<P>,
    ) -> Result<Self, MonotoneActionPolicyError>
    where
        I: IntoIterator<Item = (E, P)>,
    {
        let mut result = Self::with_policies(action_policy, order)?;
        for (element, priority) in entries {
            result = result.insert(element, priority);
        }
        Ok(result)
    }

    /// Returns the retained priority order policy.
    #[must_use]
    pub fn order_policy(&self) -> &OrderPolicy<P> {
        &self.order
    }

    /// Returns the retained action policy.
    #[must_use]
    pub fn action_policy(&self) -> &ActionPolicy<P, A> {
        &self.action_policy
    }

    /// Returns the number of entries.
    #[must_use]
    pub fn len(&self) -> usize {
        self.len
    }

    /// Returns whether the heap is empty.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.root.is_none()
    }

    /// Returns a minimum entry in O(1) worst-case time, or `None` when empty.
    #[must_use]
    pub fn minimum(&self) -> Option<MonotoneHeapEntry<E, P>> {
        self.root.as_ref().map(|root| self.entry_of(root))
    }

    /// Inserts an entry in O(1) worst-case time.
    ///
    /// The new entry joins untransformed: earlier [`transform_all`](Self::transform_all) actions
    /// never reach it.
    #[must_use]
    pub fn insert(&self, element: E, priority: P) -> Self {
        let singleton = Arc::new(Tree {
            rank: 0,
            element: Arc::new(element),
            priority: Arc::new(priority),
            children: None,
            action: self.action_policy.identity(),
        });
        let Some(root) = &self.root else {
            return self.new_version(Some(singleton), 1);
        };

        let updated_root = if self.less_or_equal(&singleton, root) {
            Arc::new(Tree {
                rank: 0,
                element: Arc::clone(&singleton.element),
                priority: Arc::clone(&singleton.priority),
                children: Some(self.action_policy.cons(Arc::clone(root), None)),
                action: self.action_policy.identity(),
            })
        } else {
            // Exposing the old root first is what stops its pending action from leaking onto a
            // child that did not exist when the action was applied.
            let exposed = self.action_policy.expose(root);
            Arc::new(Tree {
                rank: 0,
                element: Arc::clone(&exposed.element),
                priority: Arc::clone(&exposed.priority),
                children: Some(self.skew_insert(singleton, exposed.children.clone())),
                action: self.action_policy.identity(),
            })
        };
        self.new_version(
            Some(updated_root),
            self.len
                .checked_add(1)
                .expect("monotone-action heap length overflow"),
        )
    }

    /// Applies `action` to every priority present in this version in O(1) worst-case time,
    /// allocating O(1) new structure.
    ///
    /// Later insertions and later melds are not retroactively transformed. An empty heap or an
    /// identity action returns a version sharing this one's root.
    #[must_use]
    pub fn transform_all(&self, action: &A) -> Self {
        let Some(root) = &self.root else {
            return self.clone();
        };
        if self.action_policy.is_identity(action) {
            return self.clone();
        }
        self.new_version(Some(self.action_policy.tag_tree(root, action)), self.len)
    }

    /// Melds two heaps in O(1) worst-case time.
    ///
    /// Each operand keeps its own action history: neither heap's pending actions are applied to
    /// the other's entries, even for non-invertible actions.
    pub fn meld(&self, other: &Self) -> Result<Self, MonotoneActionMeldError> {
        if !self.order.is_compatible_with(&other.order)
            || !self.action_policy.is_compatible_with(&other.action_policy)
        {
            return Err(MonotoneActionMeldError);
        }
        let Some(left_root) = &self.root else {
            return Ok(other.clone());
        };
        let Some(right_root) = &other.root else {
            return Ok(self.clone());
        };

        let left_wins = self.less_or_equal(left_root, right_root);
        let (winner, loser) = if left_wins {
            (left_root, right_root)
        } else {
            (right_root, left_root)
        };
        let exposed = self.action_policy.expose(winner);
        let root = Arc::new(Tree {
            rank: 0,
            element: Arc::clone(&exposed.element),
            priority: Arc::clone(&exposed.priority),
            children: Some(self.skew_insert(Arc::clone(loser), exposed.children.clone())),
            action: self.action_policy.identity(),
        });
        Ok(self.new_version(
            Some(root),
            self.len
                .checked_add(other.len)
                .expect("monotone-action heap length overflow"),
        ))
    }

    /// Removes one minimum entry in O(log n) worst-case time, or returns `None` when empty.
    #[must_use]
    pub fn delete_minimum(&self) -> Option<Self> {
        let root_link = self.root.as_ref()?;
        let root = self.action_policy.expose(root_link);
        let Some(children) = &root.children else {
            return Some(self.new_version(None, 0));
        };

        let (tagged_minimum, remainder) = self.get_minimum(children);
        let minimum = self.action_policy.expose(&tagged_minimum);
        let split = self.split_forest(minimum.rank, minimum.children.clone());
        let mut merged = self.skew_meld(
            self.skew_meld(split.trees, remainder),
            split.embedded_forest,
        );
        for zero in self
            .action_policy
            .forest_to_vec(split.zeros)
            .into_iter()
            .rev()
        {
            merged = Some(self.skew_insert(zero, merged));
        }
        Some(self.new_version(
            Some(Arc::new(Tree {
                rank: 0,
                element: Arc::clone(&minimum.element),
                priority: Arc::clone(&minimum.priority),
                children: merged,
                action: self.action_policy.identity(),
            })),
            self.len - 1,
        ))
    }

    /// Removes and returns one minimum entry in O(log n) worst-case time, or `None` when empty.
    #[must_use]
    pub fn minimum_view(&self) -> Option<MonotoneActionMinimumView<E, P, A>> {
        let minimum = self.minimum()?;
        let remainder = self
            .delete_minimum()
            .expect("a present root must permit minimum deletion");
        Some(MonotoneActionMinimumView { minimum, remainder })
    }

    /// Iterates every entry in Theta(n), in unspecified structural order.
    #[must_use]
    pub fn iter(&self) -> MonotoneActionHeapIter<E, P, A> {
        MonotoneActionHeapIter {
            policy: self.action_policy.clone(),
            pending: self.root.iter().cloned().collect(),
        }
    }

    /// Returns whether two versions retain exactly the same root node.
    ///
    /// This is the Rust equivalent of comparing retained root object identity in the managed port:
    /// it is what proves that a no-op update reused its source rather than rebuilding it.
    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        match (&self.root, &other.root) {
            (Some(left), Some(right)) => Arc::ptr_eq(left, right),
            (None, None) => true,
            _ => false,
        }
    }

    /// Returns whether two versions' roots retain exactly the same child forest.
    ///
    /// A whole-heap transform composes one tag at the root, so the successor must reuse the
    /// receiver's entire child forest. This is the structural half of the O(1)-allocation claim.
    #[cfg(test)]
    fn shares_children_with(&self, other: &Self) -> bool {
        match (&self.root, &other.root) {
            (Some(left), Some(right)) => match (&left.children, &right.children) {
                (Some(left), Some(right)) => Arc::ptr_eq(left, right),
                (None, None) => true,
                _ => false,
            },
            (None, None) => true,
            _ => false,
        }
    }

    fn new_version(&self, root: TreeLink<E, P, A>, len: usize) -> Self {
        Self {
            root,
            len,
            order: self.order.clone(),
            action_policy: self.action_policy.clone(),
        }
    }

    fn entry_of(&self, node: &Tree<E, P, A>) -> MonotoneHeapEntry<E, P> {
        MonotoneHeapEntry {
            element: Arc::clone(&node.element),
            priority: self.action_policy.logical_priority(node),
        }
    }

    fn less_or_equal(&self, left: &Tree<E, P, A>, right: &Tree<E, P, A>) -> bool {
        let left_priority = self.action_policy.logical_priority(left);
        let right_priority = self.action_policy.logical_priority(right);
        self.order.compare(&left_priority, &right_priority).is_le()
    }
}

// ---------------------------------------------------------------------------------------------
// Skew-binomial kernel over logical (tag-composed) views
// ---------------------------------------------------------------------------------------------

impl<E, P, A> PersistentMonotoneActionHeap<E, P, A> {
    fn skew_insert(
        &self,
        node: Arc<Tree<E, P, A>>,
        forest: ForestLink<E, P, A>,
    ) -> Arc<Forest<E, P, A>> {
        if let Some(first) = &forest
            && let Some(tail) = self.action_policy.forest_tail(first)
            && first.head.rank == tail.head.rank
        {
            let head = self.action_policy.forest_head(first);
            let second = self.action_policy.forest_head(&tail);
            let rest = self.action_policy.forest_tail(&tail);
            return self
                .action_policy
                .cons(self.skew_link(node, head, second), rest);
        }
        self.action_policy.cons(node, forest)
    }

    fn skew_link(
        &self,
        zero: Arc<Tree<E, P, A>>,
        first: Arc<Tree<E, P, A>>,
        second: Arc<Tree<E, P, A>>,
    ) -> Arc<Tree<E, P, A>> {
        debug_assert_eq!(first.rank, second.rank);
        // Only the logical winner is exposed; the losing trees are attached through
        // identity-tagged spine cells so they keep their own action histories.
        if self.less_or_equal(&first, &zero) && self.less_or_equal(&first, &second) {
            let winner = self.action_policy.expose(&first);
            let children = self.action_policy.cons(
                zero,
                Some(self.action_policy.cons(second, winner.children.clone())),
            );
            return self.ranked_tree(first.rank + 1, &winner, Some(children));
        }
        if self.less_or_equal(&second, &zero) && self.less_or_equal(&second, &first) {
            let winner = self.action_policy.expose(&second);
            let children = self.action_policy.cons(
                zero,
                Some(self.action_policy.cons(first, winner.children.clone())),
            );
            return self.ranked_tree(second.rank + 1, &winner, Some(children));
        }
        let winner = self.action_policy.expose(&zero);
        let rank = first.rank + 1;
        let children = self.action_policy.cons(
            first,
            Some(self.action_policy.cons(second, winner.children.clone())),
        );
        self.ranked_tree(rank, &winner, Some(children))
    }

    fn link(
        &self,
        first: Arc<Tree<E, P, A>>,
        second: Arc<Tree<E, P, A>>,
    ) -> Arc<Tree<E, P, A>> {
        debug_assert_eq!(first.rank, second.rank);
        let first_wins = self.less_or_equal(&first, &second);
        let (winner_link, loser) = if first_wins {
            (&first, Arc::clone(&second))
        } else {
            (&second, Arc::clone(&first))
        };
        let winner = self.action_policy.expose(winner_link);
        let rank = winner.rank + 1;
        let children = self.action_policy.cons(loser, winner.children.clone());
        self.ranked_tree(rank, &winner, Some(children))
    }

    fn ranked_tree(
        &self,
        rank: usize,
        winner: &Tree<E, P, A>,
        children: ForestLink<E, P, A>,
    ) -> Arc<Tree<E, P, A>> {
        Arc::new(Tree {
            rank,
            element: Arc::clone(&winner.element),
            priority: Arc::clone(&winner.priority),
            children,
            action: self.action_policy.identity(),
        })
    }

    fn get_minimum(&self, forest: &Arc<Forest<E, P, A>>) -> MinimumSplit<E, P, A> {
        let head = self.action_policy.forest_head(forest);
        let Some(tail) = self.action_policy.forest_tail(forest) else {
            return (head, None);
        };
        let (minimum, remainder) = self.get_minimum(&tail);
        if self.less_or_equal(&head, &minimum) {
            (head, Some(tail))
        } else {
            (minimum, Some(self.action_policy.cons(head, remainder)))
        }
    }

    fn split_forest(
        &self,
        initial_rank: usize,
        initial: ForestLink<E, P, A>,
    ) -> SplitForest<E, P, A> {
        let mut rank = initial_rank;
        let mut zeros = None;
        let mut trees = None;
        let mut current = initial;
        loop {
            if rank == 0 {
                return SplitForest {
                    zeros,
                    trees,
                    embedded_forest: current,
                };
            }
            let first_forest = current
                .clone()
                .expect("valid skew-binomial trees have structural children");
            let first = self.action_policy.forest_head(&first_forest);
            let tail = self.action_policy.forest_tail(&first_forest);
            if rank == 1 && tail.is_none() {
                return SplitForest {
                    zeros,
                    trees: Some(self.action_policy.cons(first, trees)),
                    embedded_forest: None,
                };
            }
            let tail = tail.expect("valid ranked skew-binomial trees have complete children");
            let second = self.action_policy.forest_head(&tail);
            let rest = self.action_policy.forest_tail(&tail);
            if rank == 1 {
                // The rank-zero ambiguity is resolved in favor of the structural prefix.
                return if second.rank == 0 {
                    SplitForest {
                        zeros: Some(self.action_policy.cons(first, zeros)),
                        trees: Some(self.action_policy.cons(second, trees)),
                        embedded_forest: rest,
                    }
                } else {
                    SplitForest {
                        zeros,
                        trees: Some(self.action_policy.cons(first, trees)),
                        embedded_forest: Some(self.action_policy.cons(second, rest)),
                    }
                };
            }
            if first.rank == second.rank {
                return SplitForest {
                    zeros,
                    trees: Some(
                        self.action_policy
                            .cons(first, Some(self.action_policy.cons(second, trees))),
                    ),
                    embedded_forest: rest,
                };
            }
            if first.rank == 0 {
                zeros = Some(self.action_policy.cons(first, zeros));
                trees = Some(self.action_policy.cons(second, trees));
                current = rest;
            } else {
                trees = Some(self.action_policy.cons(first, trees));
                current = Some(self.action_policy.cons(second, rest));
            }
            rank -= 1;
        }
    }

    fn skew_meld(
        &self,
        left: ForestLink<E, P, A>,
        right: ForestLink<E, P, A>,
    ) -> ForestLink<E, P, A> {
        let left = self.uniquify(left);
        let right = self.uniquify(right);
        self.union_unique(left, right)
    }

    fn uniquify(&self, current: ForestLink<E, P, A>) -> ForestLink<E, P, A> {
        current.map(|link| {
            let tail = self.uniquify(self.action_policy.forest_tail(&link));
            self.insert_ranked(self.action_policy.forest_head(&link), tail)
        })
    }

    fn insert_ranked(
        &self,
        node: Arc<Tree<E, P, A>>,
        current: ForestLink<E, P, A>,
    ) -> Arc<Forest<E, P, A>> {
        let Some(first) = current else {
            return self.action_policy.cons(node, None);
        };
        let head = self.action_policy.forest_head(&first);
        debug_assert!(node.rank <= head.rank);
        if node.rank < head.rank {
            return self.action_policy.cons(node, Some(first));
        }
        let tail = self.action_policy.forest_tail(&first);
        self.insert_ranked(self.link(node, head), tail)
    }

    fn union_unique(
        &self,
        left: ForestLink<E, P, A>,
        right: ForestLink<E, P, A>,
    ) -> ForestLink<E, P, A> {
        let Some(left) = left else {
            return right;
        };
        let Some(right) = right else {
            return Some(left);
        };
        let left_head = self.action_policy.forest_head(&left);
        let right_head = self.action_policy.forest_head(&right);
        if left_head.rank < right_head.rank {
            let tail = self.action_policy.forest_tail(&left);
            let merged = self.union_unique(tail, Some(right));
            return Some(self.action_policy.cons(left_head, merged));
        }
        if left_head.rank > right_head.rank {
            let tail = self.action_policy.forest_tail(&right);
            let merged = self.union_unique(Some(left), tail);
            return Some(self.action_policy.cons(right_head, merged));
        }
        let left_tail = self.action_policy.forest_tail(&left);
        let right_tail = self.action_policy.forest_tail(&right);
        let merged = self.union_unique(left_tail, right_tail);
        Some(self.insert_ranked(self.link(left_head, right_head), merged))
    }
}

// ---------------------------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------------------------

impl<P, A> ActionPolicy<P, A> {
    /// Walks the fused primitive-child encoding of one exposed tree and returns its embedded
    /// skew-binomial forest.
    fn validate_fused_children<E>(
        &self,
        node: &Tree<E, P, A>,
    ) -> Result<ForestLink<E, P, A>, MonotoneActionInvariantError> {
        let mut rank = node.rank;
        let mut current = node.children.clone();
        while rank > 0 {
            let first_forest = current.clone().ok_or_else(|| {
                MonotoneActionInvariantError::new(
                    "a ranked action-heap tree is missing structural children",
                )
            })?;
            let first = self.forest_head(&first_forest);
            let tail = self.forest_tail(&first_forest);
            if rank == 1 {
                if first.rank != 0 {
                    return Err(MonotoneActionInvariantError::new(
                        "a rank-one action-heap tree must begin with a rank-zero child",
                    ));
                }
                let Some(tail) = tail else {
                    return Ok(None);
                };
                return if self.forest_head(&tail).rank == 0 {
                    Ok(self.forest_tail(&tail))
                } else {
                    Ok(Some(tail))
                };
            }
            let tail = tail.ok_or_else(|| {
                MonotoneActionInvariantError::new(
                    "a ranked action-heap tree has incomplete child encoding",
                )
            })?;
            let second = self.forest_head(&tail);
            if first.rank == second.rank {
                if first.rank != rank - 1 {
                    return Err(MonotoneActionInvariantError::new(
                        "a skew-linked action-heap tree has invalid child ranks",
                    ));
                }
                return Ok(self.forest_tail(&tail));
            }
            if first.rank == 0 {
                if second.rank != rank - 1 {
                    return Err(MonotoneActionInvariantError::new(
                        "a skew-linked action-heap tree has an invalid ranked child",
                    ));
                }
                current = self.forest_tail(&tail);
            } else {
                if first.rank != rank - 1 {
                    return Err(MonotoneActionInvariantError::new(
                        "a linked action-heap tree has an invalid ranked child",
                    ));
                }
                current = Some(tail);
            }
            rank -= 1;
        }
        Ok(current)
    }

    /// Checks that a skew-binomial forest has nondecreasing ranks with at most one leading pair.
    fn validate_skew_forest<E>(
        &self,
        mut current: ForestLink<E, P, A>,
    ) -> Result<usize, MonotoneActionInvariantError> {
        let mut len = 0_usize;
        let mut previous_rank: Option<usize> = None;
        let mut duplicate = false;
        while let Some(link) = current {
            let rank = link.head.rank;
            if let Some(previous) = previous_rank {
                if rank < previous {
                    return Err(MonotoneActionInvariantError::new(
                        "action-heap forest ranks are not nondecreasing",
                    ));
                }
                if rank == previous {
                    if len != 1 || duplicate {
                        return Err(MonotoneActionInvariantError::new(
                            "only the first two action-heap forest ranks may be equal",
                        ));
                    }
                    duplicate = true;
                }
            }
            previous_rank = Some(rank);
            len += 1;
            current = self.forest_tail(&link);
        }
        Ok(len)
    }
}

impl<E, P, A> PersistentMonotoneActionHeap<E, P, A> {
    /// Validates rank encodings, logical heap order through every pending action, and length in
    /// Theta(n), using an explicit worklist rather than recursion.
    pub fn validate_structure(
        &self,
    ) -> Result<PersistentMonotoneActionHeapStatistics, MonotoneActionInvariantError> {
        let Some(root_link) = &self.root else {
            if self.len != 0 {
                return Err(MonotoneActionInvariantError::new(
                    "an empty action heap has a nonzero length",
                ));
            }
            return Ok(PersistentMonotoneActionHeapStatistics::default());
        };
        if root_link.rank != 0 {
            return Err(MonotoneActionInvariantError::new(
                "the action-heap global root must have rank zero",
            ));
        }

        let mut tagged_component_count = 0_usize;
        let normalized_root = self.action_policy.expose(root_link);
        let root_forest_len = self
            .action_policy
            .validate_skew_forest(normalized_root.children.clone())?;
        let mut pending = vec![(Arc::clone(root_link), 1_usize)];
        let mut logical_count = 0_usize;
        let mut maximum_rank = 0_usize;
        let mut maximum_depth = 0_usize;
        while let Some((tagged, depth)) = pending.pop() {
            if !self.action_policy.is_identity(&tagged.action) {
                tagged_component_count += 1;
            }
            let node = self.action_policy.expose(&tagged);
            logical_count = logical_count.checked_add(1).ok_or_else(|| {
                MonotoneActionInvariantError::new("validated action-heap length overflow")
            })?;
            maximum_rank = maximum_rank.max(node.rank);
            maximum_depth = maximum_depth.max(depth);
            let embedded = self.action_policy.validate_fused_children(&node)?;
            self.action_policy.validate_skew_forest(embedded)?;

            let mut children = node.children.clone();
            while let Some(link) = children {
                if !self.action_policy.is_identity(&link.action) {
                    tagged_component_count += 1;
                }
                let child = self.action_policy.forest_head(&link);
                if !self.less_or_equal(&node, &child) {
                    return Err(MonotoneActionInvariantError::new(
                        "an action-heap child outranks its parent",
                    ));
                }
                pending.push((child, depth + 1));
                children = self.action_policy.forest_tail(&link);
            }
        }
        if logical_count != self.len {
            return Err(MonotoneActionInvariantError::new(
                "the action heap's logical length disagrees with its tree graph",
            ));
        }
        Ok(PersistentMonotoneActionHeapStatistics {
            len: self.len,
            root_forest_len,
            maximum_rank,
            maximum_depth,
            tagged_component_count,
        })
    }
}

// ---------------------------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------------------------

/// Structural-order entry iterator for [`PersistentMonotoneActionHeap`].
///
/// Every yielded priority is fully composed: the iterator reaches children only through the
/// tag-composing accessors, so a pending action is never skipped and never double-applied.
pub struct MonotoneActionHeapIter<E, P, A> {
    policy: ActionPolicy<P, A>,
    pending: Vec<Arc<Tree<E, P, A>>>,
}

impl<E, P, A> Iterator for MonotoneActionHeapIter<E, P, A> {
    type Item = MonotoneHeapEntry<E, P>;

    fn next(&mut self) -> Option<Self::Item> {
        let tagged = self.pending.pop()?;
        let node = self.policy.expose(&tagged);
        let mut children = node.children.clone();
        while let Some(link) = children {
            self.pending.push(self.policy.forest_head(&link));
            children = self.policy.forest_tail(&link);
        }
        Some(MonotoneHeapEntry {
            element: Arc::clone(&node.element),
            priority: Arc::clone(&node.priority),
        })
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (0, None)
    }
}

impl<E, P, A> IntoIterator for &PersistentMonotoneActionHeap<E, P, A> {
    type Item = MonotoneHeapEntry<E, P>;
    type IntoIter = MonotoneActionHeapIter<E, P, A>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ordering::OrderComparer;
    use std::cmp::Ordering;
    use std::panic::{self, AssertUnwindSafe};
    use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};

    type ClampAction = OrderClamp<i32>;
    type ClampPolicy = ActionPolicy<i32, ClampAction>;
    type IntHeap = PersistentMonotoneActionHeap<i32, i32, ClampAction>;
    type StringHeap = PersistentMonotoneActionHeap<String, i32, ClampAction>;

    /// Deterministic xorshift generator; the suite must not depend on an external crate.
    struct Rng(u64);

    impl Rng {
        fn new(seed: u64) -> Self {
            Self(seed | 1)
        }

        fn next_u64(&mut self) -> u64 {
            let mut state = self.0;
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            self.0 = state;
            state
        }

        fn below(&mut self, bound: usize) -> usize {
            (self.next_u64() % bound as u64) as usize
        }

        /// Uniform in `[low, high]`.
        fn between(&mut self, low: i32, high: i32) -> i32 {
            low + self.below((high - low + 1) as usize) as i32
        }
    }

    fn clamp_family() -> (OrderClampPolicy<i32>, ClampPolicy) {
        let clamp = OrderClampPolicy::<i32>::natural();
        let policy = clamp.clone().into_action_policy();
        (clamp, policy)
    }

    fn int_heap(policy: &ClampPolicy) -> IntHeap {
        IntHeap::with_policy(policy.clone()).expect("a bound clamp policy names its order policy")
    }

    fn string_heap(policy: &ClampPolicy) -> StringHeap {
        StringHeap::with_policy(policy.clone()).expect("a bound clamp policy names its order policy")
    }

    fn applied(policy: &OrderClampPolicy<i32>, action: &ClampAction, value: i32) -> i32 {
        *policy.apply(action, &Arc::new(value))
    }

    fn assert_clamp_equivalent(
        policy: &OrderClampPolicy<i32>,
        expected: &ClampAction,
        actual: &ClampAction,
        samples: &[i32],
    ) {
        for &sample in samples {
            assert_eq!(
                applied(policy, expected, sample),
                applied(policy, actual, sample),
                "clamp actions disagree at {sample}",
            );
        }
    }

    fn entries<E, P, A>(heap: &PersistentMonotoneActionHeap<E, P, A>) -> Vec<(E, P)>
    where
        E: Clone + Ord,
        P: Clone + Ord,
    {
        let mut result: Vec<(E, P)> = heap
            .iter()
            .map(|entry| ((*entry.element).clone(), (*entry.priority).clone()))
            .collect();
        result.sort();
        result
    }

    fn assert_entries<E, P, A>(heap: &PersistentMonotoneActionHeap<E, P, A>, expected: &[(E, P)])
    where
        E: Clone + Ord + fmt::Debug,
        P: Clone + Ord + fmt::Debug,
    {
        let mut expected = expected.to_vec();
        expected.sort();
        assert_eq!(entries(heap), expected);
        assert_eq!(heap.len(), expected.len());
    }

    fn validate<E, P, A>(heap: &PersistentMonotoneActionHeap<E, P, A>) {
        let statistics = heap
            .validate_structure()
            .expect("a published action heap must satisfy its invariants");
        assert_eq!(statistics.len, heap.len());
    }

    fn drain<E, P, A>(heap: &PersistentMonotoneActionHeap<E, P, A>) -> Vec<(E, P)>
    where
        E: Clone,
        P: Clone,
    {
        let mut current = heap.clone();
        let mut result = Vec::new();
        let mut index = 0_usize;
        while let Some(view) = current.minimum_view() {
            result.push((
                (*view.minimum.element).clone(),
                (*view.minimum.priority).clone(),
            ));
            current = view.remainder;
            if index.is_multiple_of(256) {
                validate(&current);
            }
            index += 1;
        }
        assert!(current.is_empty());
        assert_eq!(result.len(), heap.len());
        result
    }

    #[test]
    fn clamp_composition_obeys_identity_direction_associativity_and_monotonicity() {
        let clamp = OrderClampPolicy::<i32>::natural();
        let identity = ClampAction::identity();
        let actions = [
            identity.clone(),
            clamp.at_least(-3),
            clamp.at_least(8),
            clamp.at_most(-4),
            clamp.at_most(11),
            clamp.between(-6, 9).expect("ordered bounds"),
            clamp.between(5, 5).expect("ordered bounds"),
            clamp.constant(-9),
        ];
        let mut samples: Vec<i32> = (-20..=20).collect();
        samples.push(i32::MIN);
        samples.push(i32::MAX);

        assert!(clamp.is_identity(&identity));
        assert!(!clamp.is_identity(&clamp.at_least(0)));
        for action in &actions {
            assert_clamp_equivalent(&clamp, action, &clamp.compose(&identity, action), &samples);
            assert_clamp_equivalent(&clamp, action, &clamp.compose(action, &identity), &samples);
        }

        // compose(outer, inner) is outer(inner(p)), so a cap applied after a floor collapses.
        let floor_then_cap = clamp.compose(&clamp.at_most(5), &clamp.at_least(10));
        let cap_then_floor = clamp.compose(&clamp.at_least(10), &clamp.at_most(5));
        assert_eq!(floor_then_cap, clamp.constant(5));
        assert_eq!(cap_then_floor, clamp.constant(10));
        assert!(floor_then_cap.is_constant());
        assert!(cap_then_floor.is_constant());
        assert_eq!(floor_then_cap.constant_value(), Some(&5));
        assert_eq!(cap_then_floor.constant_value(), Some(&10));
        for &sample in &samples {
            assert_eq!(applied(&clamp, &floor_then_cap, sample), 5);
            assert_eq!(applied(&clamp, &cap_then_floor, sample), 10);
        }

        for inner in &actions {
            for outer in &actions {
                let composed = clamp.compose(outer, inner);
                for &sample in &samples {
                    assert_eq!(
                        applied(&clamp, outer, applied(&clamp, inner, sample)),
                        applied(&clamp, &composed, sample),
                    );
                }
            }
        }

        for first in &actions {
            for second in &actions {
                for third in &actions {
                    assert_clamp_equivalent(
                        &clamp,
                        &clamp.compose(third, &clamp.compose(second, first)),
                        &clamp.compose(&clamp.compose(third, second), first),
                        &samples,
                    );
                }
            }
        }

        for action in &actions {
            for lower in -12..=12 {
                for upper in lower..=12 {
                    assert!(
                        applied(&clamp, action, lower) <= applied(&clamp, action, upper),
                        "a clamp action must be monotone for the retained order",
                    );
                }
            }
        }
    }

    #[derive(Debug)]
    struct RepresentativePriority {
        key: i32,
        label: &'static str,
    }

    struct RepresentativeComparer;

    impl OrderComparer<RepresentativePriority> for RepresentativeComparer {
        fn compare(
            &self,
            left: &RepresentativePriority,
            right: &RepresentativePriority,
        ) -> Ordering {
            left.key.cmp(&right.key)
        }
    }

    fn representative(key: i32, label: &'static str) -> Arc<RepresentativePriority> {
        Arc::new(RepresentativePriority { key, label })
    }

    #[test]
    fn clamp_composition_coarse_comparer_preserves_exact_boundary_representatives() {
        let clamp = OrderClampPolicy::with_order(OrderPolicy::custom(RepresentativeComparer));
        let older_lower = representative(10, "older lower");
        let newer_upper = representative(5, "newer upper");

        let floor_then_cap = clamp.compose(
            &clamp.at_most(Arc::clone(&newer_upper)),
            &clamp.at_least(Arc::clone(&older_lower)),
        );
        assert!(floor_then_cap.is_constant());
        assert!(std::ptr::eq(
            floor_then_cap.constant_value().expect("constant"),
            newer_upper.as_ref(),
        ));
        for probe in [
            representative(5, "comparer-equal input"),
            representative(-100, "below"),
            representative(100, "above"),
        ] {
            assert!(Arc::ptr_eq(
                &clamp.apply(&floor_then_cap, &probe),
                &newer_upper,
            ));
        }

        let older_upper = representative(5, "older upper");
        let newer_lower = representative(10, "newer lower");
        let cap_then_floor = clamp.compose(
            &clamp.at_least(Arc::clone(&newer_lower)),
            &clamp.at_most(Arc::clone(&older_upper)),
        );
        assert!(cap_then_floor.is_constant());
        for probe in [
            representative(10, "comparer-equal input"),
            representative(-100, "below"),
            representative(100, "above"),
        ] {
            assert!(Arc::ptr_eq(
                &clamp.apply(&cap_then_floor, &probe),
                &newer_lower,
            ));
        }

        // Equal order classes overlap rather than collapse: each side keeps its own exact
        // representative and an input inside the shared class survives unchanged.
        let older_cap = representative(7, "older cap");
        let newer_floor = representative(7, "newer floor");
        let inside = representative(7, "inside representative");
        let touching = clamp.compose(
            &clamp.at_least(Arc::clone(&newer_floor)),
            &clamp.at_most(Arc::clone(&older_cap)),
        );
        assert!(!touching.is_constant());
        assert!(touching.has_lower_bound() && touching.has_upper_bound());
        assert!(std::ptr::eq(
            touching.lower_bound().expect("lower"),
            newer_floor.as_ref(),
        ));
        assert!(std::ptr::eq(
            touching.upper_bound().expect("upper"),
            older_cap.as_ref(),
        ));
        assert!(Arc::ptr_eq(
            &clamp.apply(&touching, &representative(6, "below")),
            &newer_floor,
        ));
        assert!(Arc::ptr_eq(&clamp.apply(&touching, &inside), &inside));
        assert!(Arc::ptr_eq(
            &clamp.apply(&touching, &representative(8, "above")),
            &older_cap,
        ));
        assert_eq!(inside.label, "inside representative");
    }

    fn owned(pairs: &[(&str, i32)]) -> Vec<(String, i32)> {
        pairs
            .iter()
            .map(|(name, priority)| ((*name).to_owned(), *priority))
            .collect()
    }

    #[test]
    fn insert_after_noninvertible_transform_has_temporal_semantics() {
        let (clamp, policy) = clamp_family();
        let source = string_heap(&policy)
            .insert("low".to_owned(), -10)
            .insert("middle".to_owned(), 5)
            .insert("high".to_owned(), 20);
        let transformed = source.transform_all(&clamp.at_least(10));
        let inserted = transformed.insert("future".to_owned(), 0);

        assert_entries(
            &source,
            &owned(&[("high", 20), ("low", -10), ("middle", 5)]),
        );
        assert_entries(
            &transformed,
            &owned(&[("high", 20), ("low", 10), ("middle", 10)]),
        );
        assert_entries(
            &inserted,
            &owned(&[("future", 0), ("high", 20), ("low", 10), ("middle", 10)]),
        );
        let minimum = inserted.minimum().expect("nonempty");
        assert_eq!(*minimum.element, "future");
        assert_eq!(*minimum.priority, 0);

        let deleted = inserted.delete_minimum().expect("nonempty");
        assert_entries(
            &deleted,
            &owned(&[("high", 20), ("low", 10), ("middle", 10)]),
        );
        assert_entries(
            &transformed,
            &owned(&[("high", 20), ("low", 10), ("middle", 10)]),
        );
        for heap in [&source, &transformed, &inserted, &deleted] {
            validate(heap);
        }

        // The opposite root-selection case: the transformed root wins, so it is exposed before the
        // future child is skew-inserted. That is what stops the old cap from leaking onto it.
        let bounded = string_heap(&policy)
            .insert("old-low".to_owned(), -10)
            .insert("old-high".to_owned(), 100)
            .transform_all(&clamp.between(0, 10).expect("ordered bounds"));
        let later_high = bounded.insert("future-high".to_owned(), 50);
        assert_entries(&bounded, &owned(&[("old-low", 0), ("old-high", 10)]));
        assert_entries(
            &later_high,
            &owned(&[("old-low", 0), ("old-high", 10), ("future-high", 50)]),
        );
        let transformed_again = later_high.transform_all(&clamp.at_least(5));
        let newest = transformed_again.insert("newest-low".to_owned(), -20);
        assert_entries(
            &transformed_again,
            &owned(&[("old-low", 5), ("old-high", 10), ("future-high", 50)]),
        );
        assert_entries(
            &newest,
            &owned(&[
                ("old-low", 5),
                ("old-high", 10),
                ("future-high", 50),
                ("newest-low", -20),
            ]),
        );
        for heap in [&bounded, &later_high, &transformed_again, &newest] {
            validate(heap);
        }
    }

    #[test]
    fn meld_combines_independently_transformed_heaps_without_cross_applying_actions() {
        let (clamp, policy) = clamp_family();
        let raw_left = string_heap(&policy)
            .insert("L0".to_owned(), -20)
            .insert("L1".to_owned(), 3);
        let raw_right = string_heap(&policy)
            .insert("R0".to_owned(), -4)
            .insert("R1".to_owned(), 30);
        let left = raw_left.transform_all(&clamp.at_least(10));
        let right = raw_right.transform_all(&clamp.at_most(-10));
        let melded = left.meld(&right).expect("shared policies");

        assert_entries(&raw_left, &owned(&[("L0", -20), ("L1", 3)]));
        assert_entries(&raw_right, &owned(&[("R0", -4), ("R1", 30)]));
        assert_entries(&left, &owned(&[("L0", 10), ("L1", 10)]));
        assert_entries(&right, &owned(&[("R0", -10), ("R1", -10)]));
        assert_entries(
            &melded,
            &owned(&[("L0", 10), ("L1", 10), ("R0", -10), ("R1", -10)]),
        );
        assert_eq!(*melded.minimum().expect("nonempty").priority, -10);
        for heap in [&left, &right, &melded] {
            validate(heap);
        }

        const COUNT: i32 = 257;
        let left_action = clamp.between(-30, 20).expect("ordered bounds");
        let right_action = clamp.between(40, 70).expect("ordered bounds");
        let mut many_left = int_heap(&policy);
        let mut many_right = int_heap(&policy);
        let mut expected = Vec::new();
        for index in 0..COUNT {
            let left_priority = ((index * 37) % 401) - 200;
            let right_priority = ((index * 53) % 401) - 200;
            many_left = many_left.insert(index, left_priority);
            many_right = many_right.insert(index + COUNT, right_priority);
            expected.push(applied(&clamp, &left_action, left_priority));
            expected.push(applied(&clamp, &right_action, right_priority));
        }
        many_left = many_left.transform_all(&left_action);
        many_right = many_right.transform_all(&right_action);
        let many_melded = many_left.meld(&many_right).expect("shared policies");
        let drained: Vec<i32> = drain(&many_melded)
            .into_iter()
            .map(|(_, priority)| priority)
            .collect();
        expected.sort_unstable();
        assert_eq!(drained, expected);
        let mut sorted = drained.clone();
        sorted.sort_unstable();
        assert_eq!(sorted, drained, "delete-min must drain in nondecreasing order");
        for heap in [&many_left, &many_right, &many_melded] {
            validate(heap);
        }
    }

    #[test]
    fn updates_are_fully_persistent_across_divergent_branches() {
        let (clamp, policy) = clamp_family();
        let original = string_heap(&policy)
            .insert("a".to_owned(), 1)
            .insert("b".to_owned(), 4)
            .insert("c".to_owned(), 9);
        let original_snapshot = original.clone();
        let floored = original.transform_all(&clamp.at_least(5));
        let with_future = floored.insert("future".to_owned(), 0);
        let deleted = with_future.delete_minimum().expect("nonempty");
        let capped = original.transform_all(&clamp.at_most(3));
        let melded = deleted.meld(&capped).expect("shared policies");

        assert!(original.shares_root_with(&original_snapshot));
        assert!(!original.shares_root_with(&floored));
        assert!(original.shares_root_with(&original.transform_all(&ClampAction::identity())));
        assert_entries(&original, &owned(&[("a", 1), ("b", 4), ("c", 9)]));
        assert_entries(&floored, &owned(&[("a", 5), ("b", 5), ("c", 9)]));
        assert_entries(
            &with_future,
            &owned(&[("a", 5), ("b", 5), ("c", 9), ("future", 0)]),
        );
        assert_entries(&deleted, &owned(&[("a", 5), ("b", 5), ("c", 9)]));
        assert_entries(&capped, &owned(&[("a", 1), ("b", 3), ("c", 3)]));
        assert_entries(
            &melded,
            &owned(&[
                ("a", 1),
                ("a", 5),
                ("b", 3),
                ("b", 5),
                ("c", 3),
                ("c", 9),
            ]),
        );

        for heap in [
            &original,
            &floored,
            &with_future,
            &deleted,
            &capped,
            &melded,
        ] {
            validate(heap);
        }
    }

    fn random_clamp(clamp: &OrderClampPolicy<i32>, random: &mut Rng) -> ClampAction {
        let first = random.between(-100, 100);
        let second = random.between(-100, 100);
        match random.below(6) {
            0 => ClampAction::identity(),
            1 => clamp.at_least(first),
            2 => clamp.at_most(first),
            3 => clamp
                .between(first.min(second), first.max(second))
                .expect("ordered bounds"),
            4 => clamp.between(first, first).expect("ordered bounds"),
            _ => clamp.constant(first),
        }
    }

    struct ModelVersion {
        heap: IntHeap,
        model: Vec<(i32, i32)>,
    }

    fn assert_matches_model(version: &ModelVersion) {
        let statistics = version
            .heap
            .validate_structure()
            .expect("a published action heap must satisfy its invariants");
        assert_eq!(version.heap.len(), version.model.len());
        assert_eq!(statistics.len, version.model.len());
        let mut expected = version.model.clone();
        expected.sort_unstable();
        assert_eq!(entries(&version.heap), expected);
        if version.model.is_empty() {
            assert!(version.heap.is_empty());
            assert!(version.heap.minimum().is_none());
        } else {
            assert!(!version.heap.is_empty());
            let smallest = version
                .model
                .iter()
                .map(|&(_, priority)| priority)
                .min()
                .expect("nonempty");
            assert_eq!(*version.heap.minimum().expect("nonempty").priority, smallest);
        }
    }

    #[test]
    fn retained_branches_randomized_model_matches_insert_delete_meld_and_transform() {
        let (clamp, policy) = clamp_family();
        let mut random = Rng::new(0x5A17_2026);
        let empty = int_heap(&policy);
        let mut versions = vec![ModelVersion {
            heap: empty,
            model: Vec::new(),
        }];
        let mut next_payload = 0;
        let (mut inserts, mut deletes, mut melds, mut transforms) = (0, 0, 0, 0);

        const STEPS: usize = 3_000;
        for step in 0..STEPS {
            let source_index = random.below(versions.len());
            let source_len = versions[source_index].model.len();
            let source_heap = versions[source_index].heap.clone();
            let operation = random.below(100);
            let result = if source_len == 0 || (source_len < 384 && operation < 30) {
                let entry = (next_payload, random.between(-100, 100));
                next_payload += 1;
                let heap = source_heap.insert(entry.0, entry.1);
                let mut model = versions[source_index].model.clone();
                model.push(entry);
                inserts += 1;
                ModelVersion { heap, model }
            } else if operation < 50 {
                let view = source_heap.minimum_view().expect("nonempty source");
                let smallest = versions[source_index]
                    .model
                    .iter()
                    .map(|&(_, priority)| priority)
                    .min()
                    .expect("nonempty");
                assert_eq!(*view.minimum.priority, smallest);
                let removed = (*view.minimum.element, *view.minimum.priority);
                let mut model = versions[source_index].model.clone();
                let position = model
                    .iter()
                    .position(|entry| *entry == removed)
                    .expect("delete-min returned an entry absent from the model");
                model.remove(position);
                deletes += 1;
                ModelVersion {
                    heap: view.remainder,
                    model,
                }
            } else if operation < 75 {
                let action = random_clamp(&clamp, &mut random);
                let heap = source_heap.transform_all(&action);
                let model = versions[source_index]
                    .model
                    .iter()
                    .map(|&(element, priority)| (element, applied(&clamp, &action, priority)))
                    .collect();
                transforms += 1;
                ModelVersion { heap, model }
            } else {
                let mut partner = None;
                for _ in 0..24 {
                    let candidate = random.below(versions.len());
                    if source_len + versions[candidate].model.len() <= 384 {
                        partner = Some(candidate);
                        break;
                    }
                }
                match partner {
                    None => {
                        let action = random_clamp(&clamp, &mut random);
                        let heap = source_heap.transform_all(&action);
                        let model = versions[source_index]
                            .model
                            .iter()
                            .map(|&(element, priority)| {
                                (element, applied(&clamp, &action, priority))
                            })
                            .collect();
                        transforms += 1;
                        ModelVersion { heap, model }
                    }
                    Some(candidate) => {
                        let partner_snapshot = versions[candidate].heap.clone();
                        let heap = source_heap
                            .meld(&partner_snapshot)
                            .expect("every version shares one policy pair");
                        let mut model = versions[source_index].model.clone();
                        model.extend_from_slice(&versions[candidate].model);
                        assert!(versions[candidate].heap.shares_root_with(&partner_snapshot));
                        melds += 1;
                        ModelVersion { heap, model }
                    }
                }
            };

            assert!(versions[source_index].heap.shares_root_with(&source_heap));
            versions.push(result);
            if step % 64 == 0 {
                assert_matches_model(versions.last().expect("just pushed"));
            }
        }

        assert!(inserts > 300, "inserts: {inserts}");
        assert!(deletes > 150, "deletes: {deletes}");
        assert!(melds > 100, "melds: {melds}");
        assert!(transforms > 300, "transforms: {transforms}");
        for index in (0..versions.len()).step_by(97) {
            assert_matches_model(&versions[index]);
        }
        assert_matches_model(versions.last().expect("nonempty"));
    }

    struct CountingClampPolicy {
        inner: OrderClampPolicy<i32>,
        identity_gets: AtomicUsize,
        identity_tests: AtomicUsize,
        compositions: AtomicUsize,
        applications: AtomicUsize,
    }

    impl CountingClampPolicy {
        fn new(inner: OrderClampPolicy<i32>) -> Self {
            Self {
                inner,
                identity_gets: AtomicUsize::new(0),
                identity_tests: AtomicUsize::new(0),
                compositions: AtomicUsize::new(0),
                applications: AtomicUsize::new(0),
            }
        }

        fn reset(&self) {
            for counter in [
                &self.identity_gets,
                &self.identity_tests,
                &self.compositions,
                &self.applications,
            ] {
                counter.store(0, AtomicOrdering::Relaxed);
            }
        }

        fn counts(&self) -> (usize, usize, usize, usize) {
            (
                self.identity_gets.load(AtomicOrdering::Relaxed),
                self.identity_tests.load(AtomicOrdering::Relaxed),
                self.compositions.load(AtomicOrdering::Relaxed),
                self.applications.load(AtomicOrdering::Relaxed),
            )
        }
    }

    impl MonotoneHeapAction<i32, ClampAction> for CountingClampPolicy {
        fn identity(&self) -> ClampAction {
            self.identity_gets.fetch_add(1, AtomicOrdering::Relaxed);
            self.inner.identity()
        }

        fn is_identity(&self, action: &ClampAction) -> bool {
            self.identity_tests.fetch_add(1, AtomicOrdering::Relaxed);
            self.inner.is_identity(action)
        }

        fn compose(&self, outer: &ClampAction, inner: &ClampAction) -> ClampAction {
            self.compositions.fetch_add(1, AtomicOrdering::Relaxed);
            self.inner.compose(outer, inner)
        }

        fn apply(&self, action: &ClampAction, priority: &Arc<i32>) -> Arc<i32> {
            self.applications.fetch_add(1, AtomicOrdering::Relaxed);
            self.inner.apply(action, priority)
        }
    }

    #[test]
    fn transform_all_has_strict_constant_policy_calls_and_structure() {
        let clamp = OrderClampPolicy::<i32>::natural();
        let counting = Arc::new(CountingClampPolicy::new(clamp.clone()));
        let policy = ActionPolicy::shared(
            Arc::clone(&counting) as Arc<dyn MonotoneHeapAction<i32, ClampAction>>,
            Some(clamp.order_policy().clone()),
        );
        let action = clamp.at_least(17);

        for count in [1_usize, 1_024, 8_192] {
            let mut heap = int_heap(&policy);
            for index in 0..count {
                heap = heap.insert(index as i32, index as i32);
            }

            let _ = heap.transform_all(&action);
            counting.reset();
            let transformed = heap.transform_all(&action);
            // One identity test in transform_all, one in tag_tree, one composition, no
            // applications, and no identity construction: constant work regardless of size.
            assert_eq!(counting.counts(), (0, 2, 1, 0));
            assert_eq!(transformed.len(), heap.len());
            assert!(!transformed.shares_root_with(&heap));
            assert!(
                transformed.shares_children_with(&heap),
                "a whole-heap transform must allocate exactly one new tree node",
            );
        }

        let nonempty = int_heap(&policy).insert(1, 1).insert(2, 2);
        counting.reset();
        let unchanged = nonempty.transform_all(&ClampAction::identity());
        assert_eq!(counting.counts(), (0, 1, 0, 0));
        assert!(unchanged.shares_root_with(&nonempty));

        let empty = int_heap(&policy);
        counting.reset();
        let still_empty = empty.transform_all(&action);
        assert_eq!(counting.counts(), (0, 0, 0, 0));
        assert!(still_empty.is_empty());
    }

    #[test]
    fn validate_structure_accepts_deeply_tagged_adversarial_shapes() {
        let (clamp, policy) = clamp_family();
        let mut left = int_heap(&policy);
        let mut right = int_heap(&policy);
        for index in 0..2_048 {
            left = left.insert(index, (index % 137) - 68);
            right = right.insert(index + 2_048, 90 - (index % 181));
        }

        left = left
            .transform_all(&clamp.at_least(-20))
            .transform_all(&clamp.at_most(30));
        right = right
            .transform_all(&clamp.at_most(25))
            .transform_all(&clamp.at_least(-35));
        let mut heap = left
            .meld(&right)
            .expect("shared policies")
            .transform_all(&clamp.between(-12, 18).expect("ordered bounds"));
        for index in 0..96 {
            heap = heap.delete_minimum().expect("nonempty");
            if index % 8 == 0 {
                heap = heap.transform_all(&clamp.at_least(-10 + (index / 8)));
            }
        }
        heap = heap
            .insert(-1, -100)
            .transform_all(&clamp.between(-9, 11).expect("ordered bounds"));

        let statistics = heap.validate_structure().expect("valid representation");
        assert_eq!(statistics.len, heap.len());
        assert_eq!(statistics.len, 4_001);
        assert!(statistics.root_forest_len > 0);
        assert!(statistics.maximum_rank > 0);
        assert!(statistics.maximum_depth > 1);
        assert!(statistics.tagged_component_count > 0);
        assert_eq!(heap.iter().count(), heap.len());
        for entry in heap.iter() {
            assert!((-9..=11).contains(&*entry.priority));
        }

        assert_eq!(left.validate_structure().expect("valid").len, 2_048);
        assert_eq!(right.validate_structure().expect("valid").len, 2_048);
    }

    #[test]
    fn collapsed_priorities_preserve_every_payload() {
        let (clamp, policy) = clamp_family();
        let mut source = string_heap(&policy);
        let expected: Vec<String> = (0..2_048).map(|index| format!("payload-{index:04}")).collect();
        for (index, payload) in expected.iter().enumerate() {
            source = source.insert(payload.clone(), (index as i32 % 257) - 128);
        }

        let collapsed = source.transform_all(&clamp.constant(7));
        let drained = drain(&collapsed);

        assert_eq!(drained.len(), expected.len());
        assert!(drained.iter().all(|&(_, priority)| priority == 7));
        let mut payloads: Vec<String> = drained.into_iter().map(|(payload, _)| payload).collect();
        payloads.sort();
        assert_eq!(payloads, expected);
        assert_eq!(source.len(), expected.len());
        assert!(source.iter().any(|entry| *entry.priority != 7));
        validate(&source);
        validate(&collapsed);
    }

    struct AddPolicy;

    impl MonotoneHeapAction<i64, i64> for AddPolicy {
        fn identity(&self) -> i64 {
            0
        }

        fn is_identity(&self, action: &i64) -> bool {
            *action == 0
        }

        fn compose(&self, outer: &i64, inner: &i64) -> i64 {
            outer + inner
        }

        fn apply(&self, action: &i64, priority: &Arc<i64>) -> Arc<i64> {
            Arc::new(action + **priority)
        }
    }

    #[test]
    fn policy_mismatch_failures_are_atomic() {
        let (clamp, policy) = clamp_family();
        let other_policy = clamp.clone().into_action_policy();
        let left = int_heap(&policy).insert(1, 5).insert(2, -1);
        let mismatch = int_heap(&other_policy).insert(3, 0);
        let left_snapshot = left.clone();
        let mismatch_snapshot = mismatch.clone();
        let left_entries = entries(&left);

        assert_eq!(left.meld(&mismatch).err(), Some(MonotoneActionMeldError));
        assert_eq!(mismatch.meld(&left).err(), Some(MonotoneActionMeldError));
        assert!(left.shares_root_with(&left_snapshot));
        assert!(mismatch.shares_root_with(&mismatch_snapshot));
        assert_eq!(entries(&left), left_entries);
        validate(&left);
        validate(&mismatch);

        // A clamp policy is monotone only for the order policy it names.
        assert_eq!(
            IntHeap::with_policies(policy.clone(), OrderPolicy::custom(RepresentativeIntComparer))
                .err(),
            Some(MonotoneActionPolicyError::MismatchedOrderPolicy),
        );
        // Natural order policies are canonical, so the bound one is accepted.
        assert!(IntHeap::with_policies(policy.clone(), OrderPolicy::natural()).is_ok());
        assert!(clamp.between(2, 1).is_err());
        assert_eq!(clamp.between(2, 1), Err(ClampBoundsError));

        // An unbound generic policy names no order policy and must be given one.
        type AddHeap = PersistentMonotoneActionHeap<i32, i64, i64>;
        let additive = ActionPolicy::custom(AddPolicy);
        assert_eq!(
            AddHeap::with_policy(additive.clone()).err(),
            Some(MonotoneActionPolicyError::UnboundActionPolicy),
        );
        let additive_heap = AddHeap::with_policies(additive.clone(), OrderPolicy::natural())
            .expect("an unbound policy accepts any order policy")
            .insert(10, 10)
            .insert(20, 20);
        let shifted = additive_heap.transform_all(&5).insert(30, 12);
        assert_eq!(entries(&shifted), vec![(10, 15), (20, 25), (30, 12)]);
        assert_eq!(entries(&additive_heap), vec![(10, 10), (20, 20)]);
        assert_eq!(*shifted.minimum().expect("nonempty").priority, 12);
        validate(&shifted);

        let independent = ActionPolicy::custom(AddPolicy);
        let stranger = AddHeap::with_policies(independent, OrderPolicy::natural())
            .expect("valid")
            .insert(40, 1);
        assert_eq!(shifted.meld(&stranger).err(), Some(MonotoneActionMeldError));
    }

    /// The payload the rigged policy panics with. Naming it lets the installed hook suppress
    /// exactly those panics while a genuine assertion failure still reports normally.
    const FAILPOINT_PANIC: &str = "monotone action-policy failpoint";

    /// A countdown that panics on its `ordinal`th observation and then disarms itself.
    #[derive(Clone)]
    struct Failpoint(Arc<AtomicUsize>);

    impl Failpoint {
        fn new() -> Self {
            Self(Arc::new(AtomicUsize::new(0)))
        }

        fn arm(&self, ordinal: usize) {
            self.0.store(ordinal, AtomicOrdering::Relaxed);
        }

        fn disarm(&self) {
            self.0.store(0, AtomicOrdering::Relaxed);
        }

        fn hit(&self) {
            let remaining = self.0.load(AtomicOrdering::Relaxed);
            if remaining == 0 {
                return;
            }

            if remaining == 1 {
                self.0.store(0, AtomicOrdering::Relaxed);
                panic::panic_any(FAILPOINT_PANIC);
            }

            self.0.store(remaining - 1, AtomicOrdering::Relaxed);
        }
    }

    /// A clamp family that misbehaves by panicking out of a chosen policy call.
    ///
    /// A panicking callback is the Rust analogue of the throwing action policy the managed suite
    /// uses: it abandons the operation at an arbitrary interior point.
    struct FailingClampPolicy {
        inner: OrderClampPolicy<i32>,
        failpoint: Failpoint,
    }

    impl MonotoneHeapAction<i32, ClampAction> for FailingClampPolicy {
        fn identity(&self) -> ClampAction {
            self.failpoint.hit();
            self.inner.identity()
        }

        fn is_identity(&self, action: &ClampAction) -> bool {
            self.failpoint.hit();
            self.inner.is_identity(action)
        }

        fn compose(&self, outer: &ClampAction, inner: &ClampAction) -> ClampAction {
            self.failpoint.hit();
            self.inner.compose(outer, inner)
        }

        fn apply(&self, action: &ClampAction, priority: &Arc<i32>) -> Arc<i32> {
            self.failpoint.hit();
            self.inner.apply(action, priority)
        }
    }

    type PanicHook = Box<dyn Fn(&panic::PanicHookInfo<'_>) + Sync + Send + 'static>;

    /// Silences the rigged failpoint panics for as long as it is held, forwarding every other
    /// panic to the hook it replaced.
    struct SilentFailpointPanics(Option<Arc<PanicHook>>);

    impl SilentFailpointPanics {
        fn install() -> Self {
            let previous = Arc::new(panic::take_hook());
            let forwarded = Arc::clone(&previous);
            panic::set_hook(Box::new(move |info| {
                let rigged = info
                    .payload()
                    .downcast_ref::<&str>()
                    .is_some_and(|message| *message == FAILPOINT_PANIC);
                if !rigged {
                    (**forwarded)(info);
                }
            }));
            Self(Some(previous))
        }
    }

    impl Drop for SilentFailpointPanics {
        fn drop(&mut self) {
            // `take_hook` and `set_hook` themselves panic on a panicking thread, which would turn
            // a failed assertion in this test into a process abort. Leaving the forwarding hook
            // installed is harmless: it suppresses nothing but the sentinel payload.
            if std::thread::panicking() {
                return;
            }
            if let Some(previous) = self.0.take() {
                let _ = panic::take_hook();
                panic::set_hook(Box::new(move |info| (**previous)(info)));
            }
        }
    }

    /// A retained version paired with the contents and minimum it must keep reporting.
    struct RetainedVersion {
        heap: IntHeap,
        entries: Vec<(i32, i32)>,
        minimum: Option<(i32, i32)>,
    }

    fn minimum_pair(heap: &IntHeap) -> Option<(i32, i32)> {
        heap.minimum()
            .map(|entry| (*entry.element, *entry.priority))
    }

    fn retained_version(heap: &IntHeap) -> RetainedVersion {
        RetainedVersion {
            heap: heap.clone(),
            entries: entries(heap),
            minimum: minimum_pair(heap),
        }
    }

    /// Arms `failpoint` at every ordinal in turn until `operation` runs to completion, asserting
    /// after each caught panic. Returns the number of distinct interior failure points observed.
    fn exercise_every_failure<F, A>(failpoint: &Failpoint, operation: F, assert_intact: A) -> usize
    where
        F: Fn(),
        A: Fn(),
    {
        for ordinal in 1..10_000 {
            failpoint.arm(ordinal);
            let result = panic::catch_unwind(AssertUnwindSafe(&operation));
            failpoint.disarm();
            match result {
                Ok(()) => return ordinal - 1,
                Err(_) => assert_intact(),
            }
        }

        panic!("the operation never completed within the failpoint ceiling");
    }

    #[test]
    fn panicking_action_policy_publishes_no_partial_successor() {
        let clamp = OrderClampPolicy::<i32>::natural();
        let failpoint = Failpoint::new();
        let policy = ActionPolicy::shared(
            Arc::new(FailingClampPolicy {
                inner: clamp.clone(),
                failpoint: failpoint.clone(),
            }) as Arc<dyn MonotoneHeapAction<i32, ClampAction>>,
            Some(clamp.order_policy().clone()),
        );

        // Build a tagged history with the failpoint disarmed, retaining every intermediate
        // version: each one must survive every later failure unchanged.
        let mut base = int_heap(&policy);
        for index in 0..64 {
            base = base.insert(index, ((index * 37) % 101) - 50);
        }
        let floored = base.transform_all(&clamp.at_least(-20));
        let capped = floored.transform_all(&clamp.at_most(25));
        let melded = capped.meld(&base).expect("shared policies");
        let deleted = melded.delete_minimum().expect("nonempty");
        let retained: Vec<RetainedVersion> = [&base, &floored, &capped, &melded, &deleted]
            .into_iter()
            .map(retained_version)
            .collect();

        let assert_retained_versions_intact = || {
            for version in &retained {
                validate(&version.heap);
                assert_eq!(entries(&version.heap), version.entries);
                assert_eq!(minimum_pair(&version.heap), version.minimum);
            }
        };

        let silent = SilentFailpointPanics::install();

        // An abandoned operation cannot publish a partial successor: every update returns its new
        // version by value, so an unwound call hands back nothing at all. What has to be checked
        // is the other half — that the versions the operation read from are untouched.
        let action = clamp.between(-10, 10).expect("ordered bounds");
        let transform_failures = exercise_every_failure(
            &failpoint,
            || {
                let _ = melded.transform_all(&action);
            },
            assert_retained_versions_intact,
        );
        assert!(
            transform_failures >= 3,
            "every one of transform_all's policy calls must be a tested failure point, not \
             {transform_failures}",
        );

        let delete_failures = exercise_every_failure(
            &failpoint,
            || {
                let _ = melded.delete_minimum();
            },
            assert_retained_versions_intact,
        );
        assert!(
            delete_failures > 32,
            "delete-min exercised only {delete_failures} interior failure points",
        );

        let insert_failures = exercise_every_failure(
            &failpoint,
            || {
                let _ = melded.insert(999, -999);
            },
            assert_retained_versions_intact,
        );
        assert!(insert_failures > 0);

        let meld_failures = exercise_every_failure(
            &failpoint,
            || {
                let _ = capped.meld(&floored);
            },
            assert_retained_versions_intact,
        );
        assert!(meld_failures > 0);

        drop(silent);

        // The abandoned operations left nothing behind: repeating them with a well-behaved
        // countdown still produces the results the intact versions imply.
        let transformed = melded.transform_all(&action);
        validate(&transformed);
        assert_eq!(transformed.len(), melded.len());
        assert!(
            transformed
                .iter()
                .all(|entry| (-10..=10).contains(&*entry.priority))
        );

        let remainder = melded.delete_minimum().expect("nonempty");
        validate(&remainder);
        assert_eq!(remainder.len(), melded.len() - 1);
        assert_eq!(minimum_pair(&remainder), minimum_pair(&deleted));
        assert_eq!(entries(&remainder), retained[4].entries);
        assert_retained_versions_intact();
    }

    #[test]
    fn from_entries_matches_folded_insertion_and_drains_in_priority_order() {
        let (clamp, policy) = clamp_family();

        let empty = IntHeap::from_entries(Vec::new(), policy.clone())
            .expect("a bound policy names an order");
        assert!(empty.is_empty());
        assert_eq!(empty.len(), 0);
        assert!(empty.minimum().is_none());
        assert!(empty.delete_minimum().is_none());
        assert_eq!(empty.iter().count(), 0);
        assert!(empty.order_policy().has_same_identity(clamp.order_policy()));
        assert!(empty.action_policy().has_same_identity(&policy));
        validate(&empty);

        let single = IntHeap::from_entries([(7, -3)], policy.clone()).expect("a bound policy");
        assert_entries(&single, &[(7, -3)]);
        assert_eq!(minimum_pair(&single), Some((7, -3)));
        assert_eq!(drain(&single), vec![(7, -3)]);
        validate(&single);

        let source: Vec<(i32, i32)> = (0..1_024)
            .map(|index| (index, ((index * 37) % 257) - 128))
            .collect();
        let many = IntHeap::from_entries(source.clone(), policy.clone()).expect("a bound policy");
        validate(&many);
        assert_entries(&many, &source);
        let drained = drain(&many);
        assert!(
            drained.windows(2).all(|pair| pair[0].1 <= pair[1].1),
            "repeated delete-min must yield nondecreasing priorities",
        );
        let mut drained_contents = drained.clone();
        drained_contents.sort();
        let mut expected_contents = source.clone();
        expected_contents.sort();
        assert_eq!(drained_contents, expected_contents);

        // Construction is exactly a fold of insert over the same entries, down to the order
        // repeated delete-min resolves equal priorities in.
        let folded = source
            .iter()
            .fold(int_heap(&policy), |heap, &(element, priority)| {
                heap.insert(element, priority)
            });
        assert_eq!(many.len(), folded.len());
        assert_eq!(entries(&many), entries(&folded));
        assert_eq!(drained, drain(&folded));

        // The explicit-policy form retains exactly the two policies it was handed.
        let order = clamp.order_policy().clone();
        let explicit =
            IntHeap::from_entries_with_policies(source.clone(), policy.clone(), order.clone())
                .expect("the supplied order policy is the one the action family names");
        validate(&explicit);
        assert_entries(&explicit, &source);
        assert!(explicit.order_policy().has_same_identity(&order));
        assert!(explicit.action_policy().has_same_identity(&policy));
        assert!(explicit.meld(&many).is_ok());

        // An independently constructed clamp family is a distinct action algebra, so a heap built
        // from it is unmeldable with one built from `policy`.
        let stranger_clamp = OrderClampPolicy::<i32>::natural();
        let stranger_policy = stranger_clamp.clone().into_action_policy();
        let stranger = IntHeap::from_entries_with_policies(
            [(1, 1)],
            stranger_policy.clone(),
            stranger_clamp.order_policy().clone(),
        )
        .expect("its own bound order policy");
        assert!(!stranger.action_policy().is_compatible_with(&policy));
        assert_eq!(explicit.meld(&stranger).err(), Some(MonotoneActionMeldError));
        assert_eq!(stranger.meld(&explicit).err(), Some(MonotoneActionMeldError));

        // Both forms reject an unusable policy pairing before consuming any entry.
        assert_eq!(
            IntHeap::from_entries_with_policies(
                source.clone(),
                policy.clone(),
                OrderPolicy::custom(RepresentativeIntComparer),
            )
            .err(),
            Some(MonotoneActionPolicyError::MismatchedOrderPolicy),
        );
        type AddHeap = PersistentMonotoneActionHeap<i32, i64, i64>;
        assert_eq!(
            AddHeap::from_entries([(1, 10_i64)], ActionPolicy::custom(AddPolicy)).err(),
            Some(MonotoneActionPolicyError::UnboundActionPolicy),
        );
        let additive = AddHeap::from_entries_with_policies(
            [(1, 10_i64), (2, -4), (3, 7)],
            ActionPolicy::custom(AddPolicy),
            OrderPolicy::natural(),
        )
        .expect("an unbound action family accepts any order policy");
        validate(&additive);
        assert_eq!(drain(&additive), vec![(2, -4), (3, 7), (1, 10)]);
    }

    struct RepresentativeIntComparer;

    impl OrderComparer<i32> for RepresentativeIntComparer {
        fn compare(&self, left: &i32, right: &i32) -> Ordering {
            left.cmp(right)
        }
    }

    fn assert_send_sync<T: Send + Sync>() {}

    #[test]
    fn snapshots_and_policies_are_send_and_sync() {
        assert_send_sync::<IntHeap>();
        assert_send_sync::<StringHeap>();
        assert_send_sync::<ClampAction>();
        assert_send_sync::<ClampPolicy>();
        assert_send_sync::<OrderClampPolicy<i32>>();
        assert_send_sync::<MonotoneHeapEntry<String, i32>>();
        assert_send_sync::<MonotoneActionMinimumView<String, i32, ClampAction>>();
    }

    #[test]
    fn empty_singleton_and_clamp_boundaries_have_defined_behavior() {
        let (clamp, policy) = clamp_family();
        let empty = int_heap(&policy);

        assert!(empty.is_empty());
        assert_eq!(empty.len(), 0);
        assert!(empty.minimum().is_none());
        assert!(empty.minimum_view().is_none());
        assert!(empty.delete_minimum().is_none());
        assert!(empty.transform_all(&clamp.at_least(3)).is_empty());
        assert!(
            empty
                .transform_all(&ClampAction::identity())
                .shares_root_with(&empty)
        );
        assert!(
            empty
                .meld(&empty)
                .expect("shared policies")
                .shares_root_with(&empty)
        );
        assert_eq!(
            empty.validate_structure(),
            Ok(PersistentMonotoneActionHeapStatistics::default()),
        );
        assert_eq!(empty.iter().count(), 0);

        let singleton = empty.insert(7, i32::MAX);
        let minimum = singleton.minimum().expect("nonempty");
        assert_eq!((*minimum.element, *minimum.priority), (7, i32::MAX));
        let view = singleton.minimum_view().expect("nonempty");
        assert_eq!((*view.minimum.element, *view.minimum.priority), (7, i32::MAX));
        assert!(view.remainder.is_empty());
        assert_eq!(singleton.len(), 1);

        let identity = ClampAction::identity();
        assert!(!identity.is_constant());
        assert!(!identity.has_lower_bound());
        assert!(!identity.has_upper_bound());
        assert_eq!(identity.constant_value(), None);
        assert_eq!(identity.lower_bound(), None);
        assert_eq!(identity.upper_bound(), None);
        let floor = clamp.at_least(i32::MIN);
        let cap = clamp.at_most(i32::MAX);
        let constant = clamp.constant(42);
        assert_eq!(floor.lower_bound(), Some(&i32::MIN));
        assert!(!floor.is_constant() && !floor.has_upper_bound());
        assert_eq!(floor.constant_value(), None);
        assert_eq!(cap.upper_bound(), Some(&i32::MAX));
        assert!(!cap.is_constant() && !cap.has_lower_bound());
        assert!(constant.is_constant());
        assert_eq!(constant.constant_value(), Some(&42));
        assert_eq!(constant.lower_bound(), None);
        assert_eq!(constant.upper_bound(), None);

        let extremes = empty.insert(1, i32::MIN).insert(2, i32::MAX);
        let bounded = extremes.transform_all(&clamp.between(-10, 10).expect("ordered bounds"));
        assert_entries(&bounded, &[(1, -10), (2, 10)]);
        assert!(
            extremes
                .transform_all(&ClampAction::default())
                .shares_root_with(&extremes)
        );
        assert!(
            extremes
                .meld(&empty)
                .expect("shared policies")
                .shares_root_with(&extremes)
        );
        assert!(
            empty
                .meld(&extremes)
                .expect("shared policies")
                .shares_root_with(&extremes)
        );
        for heap in [&singleton, &view.remainder, &extremes, &bounded] {
            validate(heap);
        }
    }
}
